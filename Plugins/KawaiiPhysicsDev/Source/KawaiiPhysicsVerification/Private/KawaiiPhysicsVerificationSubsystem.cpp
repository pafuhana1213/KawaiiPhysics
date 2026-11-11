// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsVerificationSubsystem.h"

#include "AnimNode_KawaiiPhysics.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstance.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsRuntimeInfoTypes.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ProfilingDebugging/CsvProfilerConfig.h"
#if CSV_PROFILER
#include "ProfilingDebugging/CsvProfiler.h"
#endif
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogKawaiiPhysicsVerification, Log, All);

namespace KawaiiPhysicsVerificationPrivate
{
	bool ReadManifestArgument(FString& OutValue)
	{
		return FParse::Value(FCommandLine::Get(), TEXT("-KPVerify="), OutValue);
	}

	FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values->Num() == 3)
		{
			return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		}
		return FVector::ZeroVector;
	}

	double ReadPositive(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const double DefaultValue)
	{
		double Value = DefaultValue;
		return Object.IsValid() && Object->TryGetNumberField(Field, Value) && Value > 0.0 ? Value : DefaultValue;
	}

	// 差分を厳密に比較できるよう、double を丸めずに書き出す。
	void AppendNumber(FString& Output, const double Value)
	{
		// nan / inf は JSON に書けず記録全体が読めなくなるため、null にして判定側で検出する。
		Output += FMath::IsFinite(Value) ? FString::Printf(TEXT("%.17g"), Value) : FString(TEXT("null"));
	}

	void AppendNumbers(FString& Output, const TArray<double>& Values)
	{
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			if (Index > 0)
			{
				Output += TEXT(",");
			}
			AppendNumber(Output, Values[Index]);
		}
	}

	FString EscapeJson(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}

	FString ReadEffectiveCVarsJson()
	{
		FString Result = TEXT("{");
		const TCHAR* Names[] = {TEXT("a.ParallelAnimEvaluation"), TEXT("a.ParallelAnimUpdate")};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Names[Index]);
			Result += FString::Printf(TEXT("%s\"%s\":"), Index ? TEXT(",") : TEXT(""), Names[Index]);
			Result += Variable ? FString::Printf(TEXT("\"%s\""), *EscapeJson(Variable->GetString())) : TEXT("null");
		}
		return Result + TEXT("}");
	}

	bool ApplyOverride(TArray<FKawaiiPhysicsReference>& Nodes, const FName PropertyName, const FString& ValueText,
	                   const FString& CellId)
	{
		for (FKawaiiPhysicsReference& Node : Nodes)
		{
			FAnimNode_KawaiiPhysics& AnimNode = Node.GetAnimNode<FAnimNode_KawaiiPhysics>();
			if (!UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(AnimNode, PropertyName, ValueText))
			{
				UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: cannot set %s to %s."), *CellId,
				       *PropertyName.ToString(), *ValueText);
				return false;
			}
		}
		return true;
	}

	void DisableKawaiiPhysicsNodes(TArray<FKawaiiPhysicsReference>& Nodes)
	{
		for (FKawaiiPhysicsReference& Reference : Nodes)
		{
			FAnimNode_KawaiiPhysics& Node = Reference.GetAnimNode<FAnimNode_KawaiiPhysics>();
			// Alpha=0 で SkeletalControlBase が評価を飛ばす。
			Node.AlphaInputType = EAnimAlphaInputType::Float;
			Node.Alpha = 0.f;
		}
	}

	const TCHAR* ReadRunnerBuild()
	{
#if WITH_EDITOR
		return TEXT("editor");
#elif UE_BUILD_DEBUG
		return TEXT("game_debug");
#elif UE_BUILD_DEVELOPMENT
		return TEXT("game_development");
#elif UE_BUILD_TEST
		return TEXT("game_test");
#elif UE_BUILD_SHIPPING
		return TEXT("game_shipping");
#else
		return TEXT("unknown");
#endif
	}

	constexpr int32 GCalibrationTrials = 5;
	constexpr int32 GCalibrationIterations = 47000;
	static volatile double GCalibrationSink = 0.0;

	// KawaiiPhysicsPerfTest.cpp の較正ループと同じ計算量の固定処理。実行時のマシン状態を見るために時間だけを測る。
	FORCENOINLINE void RunCalibrationLoop()
	{
		constexpr int32 PointCount = 256;
		constexpr double Dt = 1.0 / 60.0;
		constexpr double DtSquared = Dt * Dt;
		FVector Positions[PointCount];
		FVector PreviousPositions[PointCount];
		for (int32 Index = 0; Index < PointCount; ++Index)
		{
			const double Scale = static_cast<double>(Index + 1);
			Positions[Index] = FVector(Scale * 0.25, Scale * -0.125, Scale * 0.0625);
			PreviousPositions[Index] = Positions[Index] - FVector(0.01 * Scale, -0.02 * Scale, 0.015 * Scale);
		}

		for (int32 Iteration = 0; Iteration < GCalibrationIterations; ++Iteration)
		{
			for (int32 Index = 0; Index < PointCount; ++Index)
			{
				const double AccelScale = static_cast<double>((Index % 17) + 1);
				const FVector Accel(AccelScale * 0.001, AccelScale * -0.002, -0.01 - AccelScale * 0.0005);
				const FVector Current = Positions[Index];
				const FVector Next = Current + (Current - PreviousPositions[Index]) * 0.99 + Accel * DtSquared;
				PreviousPositions[Index] = Current;
				Positions[Index] = Next;
			}
		}

		double Sum = 0.0;
		for (int32 Index = 0; Index < PointCount; ++Index)
		{
			Sum += Positions[Index].X + Positions[Index].Y + Positions[Index].Z;
		}
		GCalibrationSink = GCalibrationSink + Sum;
	}

	void MeasureCalibration(TArray<double>& OutMilliseconds)
	{
		for (int32 Trial = 0; Trial < GCalibrationTrials; ++Trial)
		{
			const double StartSeconds = FPlatformTime::Seconds();
			RunCalibrationLoop();
			OutMilliseconds.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		}
	}
}

bool UKawaiiPhysicsVerificationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString ManifestPath;
	if (!KawaiiPhysicsVerificationPrivate::ReadManifestArgument(ManifestPath) || !Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UKawaiiPhysicsVerificationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(
		this, &UKawaiiPhysicsVerificationSubsystem::HandlePreActorTick);
}

void UKawaiiPhysicsVerificationSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
	Super::Deinitialize();
}

TStatId UKawaiiPhysicsVerificationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKawaiiPhysicsVerificationSubsystem, STATGROUP_Tickables);
}

void UKawaiiPhysicsVerificationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FString ManifestPath;
	KawaiiPhysicsVerificationPrivate::ReadManifestArgument(ManifestPath);
	if (!FParse::Value(FCommandLine::Get(), TEXT("-KPVerifyOut="), OutputPath))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("-KPVerifyOut=<jsonl> is required."));
		FPlatformMisc::RequestExitWithStatus(false, 2);
		return;
	}

	if (!LoadManifest(ManifestPath))
	{
		WriteRecordingAndExit(false);
		return;
	}

	// マニフェスト適用後、計測開始前の GameThread で実効 CVar を確定する。
	EffectiveCVarsJson = KawaiiPhysicsVerificationPrivate::ReadEffectiveCVarsJson();
	FParse::Value(FCommandLine::Get(), TEXT("-KPVerifyCapture="), CaptureDirectory);
	KawaiiPhysicsVerificationPrivate::MeasureCalibration(CalibrationStartMilliseconds);
	bRunning = true;
	LastTickSeconds = FPlatformTime::Seconds();
	UE_LOG(LogKawaiiPhysicsVerification, Display, TEXT("Running %d cells for %d frames."), Cells.Num(), NumFrames);
}

bool UKawaiiPhysicsVerificationSubsystem::LoadManifest(const FString& ManifestPath)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ManifestPath))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cannot read manifest %s."), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cannot parse manifest %s."), *ManifestPath);
		return false;
	}

	if (!Root->TryGetNumberField(TEXT("frames"), NumFrames) || NumFrames <= 0)
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Manifest needs positive frames."));
		return false;
	}
	Root->TryGetBoolField(TEXT("record"), bRecordTransforms);
	Root->TryGetBoolField(TEXT("record_nodes"), bRecordNodes);
	bRecordNodes &= bRecordTransforms;
	if (Root->HasField(TEXT("disable_kawaii_physics")) &&
		!Root->TryGetBoolField(TEXT("disable_kawaii_physics"), bDisableKawaiiPhysics))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("disable_kawaii_physics must be a bool."));
		return false;
	}
#if CSV_PROFILER
	const TSharedPtr<FJsonObject>* CsvObject = nullptr;
	if (Root->TryGetObjectField(TEXT("csv_capture"), CsvObject))
	{
		if (!(*CsvObject)->TryGetNumberField(TEXT("start_frame"), CsvStartFrame) ||
			!(*CsvObject)->TryGetNumberField(TEXT("frames"), CsvFrames) ||
			!(*CsvObject)->TryGetStringField(TEXT("directory"), CsvDirectory) ||
			!(*CsvObject)->TryGetStringField(TEXT("filename"), CsvFilename) ||
			CsvStartFrame < 0 || CsvFrames <= 0 || CsvDirectory.IsEmpty() || CsvFilename.IsEmpty() ||
			FPaths::GetCleanFilename(CsvFilename) != CsvFilename)
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Invalid csv_capture settings."));
			return false;
		}
		CsvState = ECsvCaptureState::Waiting;
		NumFrames = FMath::Max(NumFrames, CsvStartFrame + CsvFrames + 2);
	}
#else
	if (Root->HasField(TEXT("csv_capture")))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("CSV capture is unavailable in this build."));
		return false;
	}
#endif
	const TSharedPtr<FJsonObject>* SettingsObject = nullptr;
	if (Root->TryGetObjectField(TEXT("settings"), SettingsObject))
	{
		UKawaiiPhysicsDeveloperSettings* Settings = GetMutableDefault<UKawaiiPhysicsDeveloperSettings>();
		bool bFixedSubstepping = false;
		if ((*SettingsObject)->TryGetBoolField(TEXT("fixed_substepping"), bFixedSubstepping))
		{
			Settings->bUseFixedSubstepping = bFixedSubstepping;
		}
		int32 MaxSubsteps = 0;
		if ((*SettingsObject)->TryGetNumberField(TEXT("max_substeps"), MaxSubsteps))
		{
			Settings->MaxSubsteps = MaxSubsteps;
		}
		double TimeDilation = 1.0;
		if ((*SettingsObject)->TryGetNumberField(TEXT("time_dilation"), TimeDilation))
		{
			GetWorld()->GetWorldSettings()->SetTimeDilation(static_cast<float>(TimeDilation));
			if (!FMath::IsNearlyEqual(static_cast<double>(GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation()), TimeDilation, 1e-6))
			{
				UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cannot apply time_dilation %g."), TimeDilation);
				return false;
			}
		}
	}

	const TSharedPtr<FJsonObject>* CameraObject = nullptr;
	if (Root->TryGetObjectField(TEXT("camera"), CameraObject))
	{
		ReadCamera(*CameraObject);
	}

	// scan はレベルに置かれたものをそのまま記録する。配置やアニメには手を加えず、セルの生成もしない。
	const TSharedPtr<FJsonObject>* ScanObject = nullptr;
	if (Root->TryGetObjectField(TEXT("scan"), ScanObject))
	{
		int32 AfterFrames = 1;
		(*ScanObject)->TryGetNumberField(TEXT("after_frames"), AfterFrames);
		ScanFramesRemaining = FMath::Max(AfterFrames, 1);
		PrepareWorldComponents();
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* CellValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("cells"), CellValues))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Manifest needs a cells array or a scan object."));
		return false;
	}

	// 撮影時は照明とテクスチャが落ち着いてからセルを出す。初回評価そのものを画面に収めるため、動かすのではなく生成を遅らせる。
	Root->TryGetNumberField(TEXT("start_delay_frames"), StartDelayFramesRemaining);
	if (StartDelayFramesRemaining > 0)
	{
		PendingCellValues = *CellValues;
		return true;
	}
	return SpawnCells(*CellValues);
}

bool UKawaiiPhysicsVerificationSubsystem::SpawnCells(const TArray<TSharedPtr<FJsonValue>>& CellValues)
{
	UWorld* World = GetWorld();
	for (const TSharedPtr<FJsonValue>& CellValue : CellValues)
	{
		if (!SpawnCell(*World, CellValue->AsObject()))
		{
			return false;
		}
	}
	return !Cells.IsEmpty();
}

void UKawaiiPhysicsVerificationSubsystem::ReadCamera(const TSharedPtr<FJsonObject>& CameraObject)
{
	using namespace KawaiiPhysicsVerificationPrivate;

	const FVector Rotation = ReadVector(CameraObject, TEXT("rotation"));
	ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(ReadVector(CameraObject, TEXT("location")),
	                                                            FRotator(Rotation.X, Rotation.Y, Rotation.Z));
	if (Camera)
	{
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		Camera->GetCameraComponent()->SetFieldOfView(ReadPositive(CameraObject, TEXT("fov"), 40.0));
		CameraActor = Camera;
	}
}

void UKawaiiPhysicsVerificationSubsystem::HoldCameraView() const
{
	// GameMode が開始時に視点を戻すことがあるため、毎フレーム指定し直す。
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (Controller && CameraActor.IsValid() && Controller->GetViewTarget() != CameraActor.Get())
	{
		Controller->SetViewTarget(CameraActor.Get());
	}
}

void UKawaiiPhysicsVerificationSubsystem::RequestFrameCapture() const
{
	if (!CaptureDirectory.IsEmpty())
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(CaptureDirectory, FString::Printf(TEXT("frame_%05d.png"), FrameIndex)), false, false);
	}
}

bool UKawaiiPhysicsVerificationSubsystem::SpawnCell(UWorld& InWorld, const TSharedPtr<FJsonObject>& CellObject)
{
	using namespace KawaiiPhysicsVerificationPrivate;

	FCell Cell;
	FString MeshPath;
	FString AnimClassPath;
	// anim_asset はアニメを単体再生し（Sequencer のアニメトラックと同じ構成）、KawaiiPhysics はメッシュの PostProcess ABP で動かす。
	// anim_play_rate でアニメの再生速度だけを変えられる。
	FString AnimAssetPath;
	double AnimPlayRate = 1.0;
	const bool bHasAnimAsset = CellObject.IsValid() && CellObject->TryGetStringField(TEXT("anim_asset"), AnimAssetPath);
	if (!CellObject.IsValid() || !CellObject->TryGetStringField(TEXT("id"), Cell.Id) ||
		!CellObject->TryGetStringField(TEXT("mesh"), MeshPath) ||
		(!CellObject->TryGetStringField(TEXT("anim_class"), AnimClassPath) && !bHasAnimAsset))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("A cell needs id, mesh and anim_class or anim_asset."));
		return false;
	}
	CellObject->TryGetNumberField(TEXT("anim_play_rate"), AnimPlayRate);

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	UClass* AnimClass = bHasAnimAsset ? nullptr : LoadClass<UAnimInstance>(nullptr, *AnimClassPath);
	UAnimationAsset* AnimAsset = bHasAnimAsset ? LoadObject<UAnimationAsset>(nullptr, *AnimAssetPath) : nullptr;
	if (!Mesh || (bHasAnimAsset ? !AnimAsset : !AnimClass))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: cannot load %s or %s."), *Cell.Id, *MeshPath,
		       bHasAnimAsset ? *AnimAssetPath : *AnimClassPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* MotionObject = nullptr;
	Cell.Motion.BaseLocation = ReadVector(CellObject, TEXT("location"));
	CellObject->TryGetNumberField(TEXT("base_yaw_degrees"), Cell.Motion.BaseYawDegrees);
	if (CellObject->TryGetObjectField(TEXT("motion"), MotionObject))
	{
		Cell.Motion.LinearAmplitude = ReadVector(*MotionObject, TEXT("linear_amplitude"));
		Cell.Motion.LinearPeriod = ReadPositive(*MotionObject, TEXT("linear_period"), 1.0);
		(*MotionObject)->TryGetNumberField(TEXT("yaw_amplitude_degrees"), Cell.Motion.YawAmplitudeDegrees);
		Cell.Motion.YawPeriod = ReadPositive(*MotionObject, TEXT("yaw_period"), 1.0);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASkeletalMeshActor* Actor = InWorld.SpawnActor<ASkeletalMeshActor>(
		Cell.Motion.BaseLocation, FRotator::ZeroRotator, SpawnParameters);
	USkeletalMeshComponent* Component = Actor ? Actor->GetSkeletalMeshComponent() : nullptr;
	if (!Component)
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: cannot spawn its actor."), *Cell.Id);
		return false;
	}

	Component->SetMobility(EComponentMobility::Movable);
	// ASkeletalMeshActor の既定 AlwaysTickPose は描画時しかボーンを評価せず、-nullrhi では物理が一度も走らない。
	Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// InitAnim 内の評価を止める。走ると上書き前の設定でボーンが作られ、ABP に保存した場合と別の経路を通ってしまう。
	Component->bUseRefPoseOnInitAnim = true;
	if (bHasAnimAsset)
	{
		Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	}
	else
	{
		Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Component->SetAnimInstanceClass(AnimClass);
	}
	Component->SetSkeletalMeshAsset(Mesh);
	Component->InitAnim(true);

	UAnimInstance* AnimInstance = Component->GetAnimInstance();
	TArray<FKawaiiPhysicsReference> Nodes;
	if (!AnimInstance ||
		!UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, Component, FGameplayTagContainer(), false))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: no KawaiiPhysics node in %s."), *Cell.Id,
		       bHasAnimAsset ? *MeshPath : *AnimClassPath);
		return false;
	}

	for (FKawaiiPhysicsReference& Node : Nodes)
	{
		if (!Node.GetAnimNode<FAnimNode_KawaiiPhysics>().ModifyBones.IsEmpty())
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: the node evaluated before its overrides."),
			       *Cell.Id);
			return false;
		}
	}

	// ExternalForces はインスタンス構造体で文字列上書きできないため、外すだけの指定を別に用意する。
	bool bClearExternalForces = false;
	CellObject->TryGetBoolField(TEXT("clear_external_forces"), bClearExternalForces);
	if (bClearExternalForces)
	{
		for (FKawaiiPhysicsReference& Node : Nodes)
		{
			Node.GetAnimNode<FAnimNode_KawaiiPhysics>().ExternalForces.Reset();
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* ExternalForces = nullptr;
	const bool bHasExternalForces = CellObject->TryGetArrayField(TEXT("external_forces"), ExternalForces);
	if (bHasExternalForces)
	{
		for (const TSharedPtr<FJsonValue>& ForceValue : *ExternalForces)
		{
			const TSharedPtr<FJsonObject> ForceObject = ForceValue->AsObject();
			FString StructPath;
			FString Text;
			if (!ForceObject.IsValid() || !ForceObject->TryGetStringField(TEXT("struct"), StructPath) ||
				!ForceObject->TryGetStringField(TEXT("text"), Text))
			{
				UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: invalid external force entry."), *Cell.Id);
				return false;
			}
			UScriptStruct* ForceStruct = FindObject<UScriptStruct>(nullptr, *StructPath);
			if (!ForceStruct || !ForceStruct->IsChildOf(FKawaiiPhysics_ExternalForce::StaticStruct()))
			{
				UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: invalid external force struct %s."), *Cell.Id, *StructPath);
				return false;
			}
			FInstancedStruct Force;
			Force.InitializeAs(ForceStruct);
			if (!ForceStruct->ImportText(*Text, Force.GetMutableMemory(), nullptr, PPF_None, nullptr, ForceStruct->GetName()))
			{
				UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: cannot import external force %s."), *Cell.Id, *StructPath);
				return false;
			}
			for (FKawaiiPhysicsReference& Node : Nodes)
			{
				Node.GetAnimNode<FAnimNode_KawaiiPhysics>().ExternalForces.Add(Force);
			}
		}
	}

	const TSharedPtr<FJsonObject>* Overrides = nullptr;
	const bool bHasOverrides = CellObject->TryGetObjectField(TEXT("overrides"), Overrides);
	if (bHasOverrides)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Override : (*Overrides)->Values)
		{
			if (!ApplyOverride(Nodes, FName(*Override.Key), Override.Value->AsString(), Cell.Id))
			{
				return false;
			}
		}
	}

	if (bHasOverrides || bClearExternalForces || bHasExternalForces)
	{
		// Initialize_AnyThread は SimulationSpace に応じて初期状態を作るため、ABP に保存した場合と揃えるには上書き後に初期化し直す。
		AnimInstance->UninitializeAnimation();
		AnimInstance->InitializeAnimation();
		AnimInstance->RecalcRequiredBones();

		// 再初期化で Linked Anim Graph のインスタンスが作り直されるため、ノードの参照を取り直す。
		Nodes.Reset();
		if (!UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, Component, FGameplayTagContainer(), false) ||
			Nodes.IsEmpty())
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error,
			       TEXT("Cell %s: cannot collect KawaiiPhysics nodes after reinitialization."), *Cell.Id);
			return false;
		}
	}

	if (bDisableKawaiiPhysics)
	{
		DisableKawaiiPhysicsNodes(Nodes);
	}

	if (!ReadCellEvents(CellObject, Cell))
	{
		return false;
	}

	Cell.Component = Component;
	if (bRecordTransforms)
	{
		Cell.Samples.Reserve(NumFrames * Component->GetNumBones() * 7);
	}
	if (bHasAnimAsset)
	{
		// 上書き後の再初期化で単体再生の状態が消えるため、最後に再生を始める
		Component->PlayAnimation(AnimAsset, true);
		Component->SetPlayRate(static_cast<float>(AnimPlayRate));
	}
	ApplyMotion(Cell, 0.0);
	Cells.Add(MoveTemp(Cell));
	return true;
}

bool UKawaiiPhysicsVerificationSubsystem::ReadCellEvents(const TSharedPtr<FJsonObject>& CellObject, FCell& Cell) const
{
	const TArray<TSharedPtr<FJsonValue>>* EventValues = nullptr;
	if (!CellObject->TryGetArrayField(TEXT("events"), EventValues))
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& EventValue : *EventValues)
	{
		const TSharedPtr<FJsonObject> EventObject = EventValue->AsObject();
		FCellEvent Event;
		// 範囲外のイベントは一度も発火せず、変化を起こさないまま比較に合格してしまう。
		if (!EventObject.IsValid() || !EventObject->TryGetNumberField(TEXT("frame"), Event.Frame) ||
			Event.Frame < 0 || Event.Frame >= NumFrames)
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: an event needs a frame inside the run."), *Cell.Id);
			return false;
		}

		EventObject->TryGetBoolField(TEXT("reset_dynamics"), Event.bResetDynamics);
		const TSharedPtr<FJsonObject>* Move = nullptr;
		if (EventObject->TryGetObjectField(TEXT("move"), Move))
		{
			Event.LocationOffset = KawaiiPhysicsVerificationPrivate::ReadVector(*Move, TEXT("location_offset"));
			(*Move)->TryGetNumberField(TEXT("yaw_offset_degrees"), Event.YawOffsetDegrees);
		}
		const TSharedPtr<FJsonObject>* Overrides = nullptr;
		if (EventObject->TryGetObjectField(TEXT("overrides"), Overrides))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Override : (*Overrides)->Values)
			{
				Event.Overrides.Emplace(FName(*Override.Key), Override.Value->AsString());
			}
		}
		Cell.Events.Add(MoveTemp(Event));
	}
	return true;
}

bool UKawaiiPhysicsVerificationSubsystem::ApplyDueEvents(FCell& Cell) const
{
	USkeletalMeshComponent* Component = Cell.Component.Get();
	for (const FCellEvent& Event : Cell.Events)
	{
		if (Event.Frame != FrameIndex || !Component)
		{
			continue;
		}
		Cell.Motion.BaseLocation += Event.LocationOffset;
		Cell.Motion.BaseYawDegrees += Event.YawOffsetDegrees;

		TArray<FKawaiiPhysicsReference> Nodes;
		UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, Component, FGameplayTagContainer(), false);
		for (const TPair<FName, FString>& Override : Event.Overrides)
		{
			if (!KawaiiPhysicsVerificationPrivate::ApplyOverride(Nodes, Override.Key, Override.Value, Cell.Id))
			{
				return false;
			}
		}
		if (Event.bResetDynamics)
		{
			Component->ResetAnimInstanceDynamics(ETeleportType::ResetPhysics);
		}
	}
	return true;
}

void UKawaiiPhysicsVerificationSubsystem::PrepareWorldComponents() const
{
	const UWorld* World = GetWorld();
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		if (It->GetWorld() == World)
		{
			// -nullrhi では描画されないため、既定のままだとボーンが評価されない（SpawnCell と同じ理由）。
			It->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
	}
}

void UKawaiiPhysicsVerificationSubsystem::DiscoverWorldCells()
{
	const UWorld* World = GetWorld();
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Component = *It;
		TArray<FKawaiiPhysicsReference> Nodes;
		if (Component->GetWorld() != World || !Component->GetOwner() ||
			!UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, Component, FGameplayTagContainer(), false))
		{
			continue;
		}

		FCell Cell;
		Cell.Id = FString::Printf(TEXT("%s.%s"), *Component->GetOwner()->GetActorNameOrLabel(), *Component->GetName());
		Cell.bDriven = false;
		Cell.Component = Component;
		if (bRecordTransforms)
		{
			Cell.Samples.Reserve(NumFrames * Component->GetNumBones() * 7);
		}
		Cells.Add(MoveTemp(Cell));
	}
	UE_LOG(LogKawaiiPhysicsVerification, Display, TEXT("Scan found %d cells."), Cells.Num());
}

void UKawaiiPhysicsVerificationSubsystem::ApplyMotion(const FCell& Cell, const double Time) const
{
	USkeletalMeshComponent* Component = Cell.Component.Get();
	if (!Component || !Cell.bDriven)
	{
		return;
	}

	const FCellMotion& Motion = Cell.Motion;
	const FVector Location = Motion.BaseLocation +
		Motion.LinearAmplitude * FMath::Sin(UE_DOUBLE_TWO_PI * Time / Motion.LinearPeriod);
	const double Yaw = Motion.BaseYawDegrees + Motion.YawAmplitudeDegrees * FMath::Sin(UE_DOUBLE_TWO_PI * Time / Motion.YawPeriod);
	Component->SetWorldLocationAndRotationNoPhysics(Location, FRotator(0.0, Yaw, 0.0));
}

void UKawaiiPhysicsVerificationSubsystem::HandlePreActorTick(UWorld* InWorld, ELevelTick TickType,
                                                             const float DeltaSeconds)
{
	if (!bRunning || InWorld != GetWorld())
	{
		return;
	}

	SimulationTime += DeltaSeconds;
	HoldCameraView();
	for (FCell& Cell : Cells)
	{
		if (!ApplyDueEvents(Cell))
		{
			bRunning = false;
			WriteRecordingAndExit(false);
			return;
		}
		ApplyMotion(Cell, SimulationTime);
	}
}

void UKawaiiPhysicsVerificationSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bRunning)
	{
		return;
	}

	if (StartDelayFramesRemaining > 0)
	{
		if (--StartDelayFramesRemaining == 0 && !SpawnCells(PendingCellValues))
		{
			bRunning = false;
			WriteRecordingAndExit(false);
		}
		LastTickSeconds = FPlatformTime::Seconds();
		return;
	}

	if (ScanFramesRemaining > 0)
	{
		// ストリーミングされるサブレベルと開始直後の揺れを待ってから記録を始める。
		PrepareWorldComponents();
		if (--ScanFramesRemaining == 0)
		{
			DiscoverWorldCells();
		}
		LastTickSeconds = FPlatformTime::Seconds();
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const bool bCsvWaitingForWrite = CsvState == ECsvCaptureState::Writing || CsvState == ECsvCaptureState::Complete;
	if (!AdvanceCsvCapture())
	{
		bRunning = false;
		WriteRecordingAndExit(false);
		return;
	}
	if (!bCsvWaitingForWrite)
	{
		FrameMilliseconds.Add((NowSeconds - LastTickSeconds) * 1000.0);
		FrameDeltaSeconds.Add(DeltaTime);
	}

	for (FCell& Cell : Cells)
	{
		const USkeletalMeshComponent* Component = bRecordTransforms ? Cell.Component.Get() : nullptr;
		if (Component)
		{
			for (const FTransform& BoneTransform : Component->GetComponentSpaceTransforms())
			{
				const FVector Location = BoneTransform.GetLocation();
				const FQuat Rotation = BoneTransform.GetRotation();
				Cell.Samples.Append({Location.X, Location.Y, Location.Z, Rotation.X, Rotation.Y, Rotation.Z, Rotation.W});
			}
		}
		if (bRecordNodes && !RecordCellNodes(Cell))
		{
			bRunning = false;
			WriteRecordingAndExit(false);
			return;
		}
	}

	RequestFrameCapture();
	++FrameIndex;
	if (FrameIndex >= NumFrames && (CsvState == ECsvCaptureState::Disabled || CsvState == ECsvCaptureState::Complete))
	{
		bRunning = false;
		KawaiiPhysicsVerificationPrivate::MeasureCalibration(CalibrationEndMilliseconds);
		WriteRecordingAndExit(true);
		return;
	}
	LastTickSeconds = FPlatformTime::Seconds();
}

bool UKawaiiPhysicsVerificationSubsystem::AdvanceCsvCapture()
{
#if CSV_PROFILER
	FCsvProfiler* Profiler = FCsvProfiler::Get();
	switch (CsvState)
	{
	case ECsvCaptureState::Disabled:
	case ECsvCaptureState::Complete:
		return true;
	case ECsvCaptureState::Waiting:
		if (FrameIndex < CsvStartFrame)
		{
			return true;
		}
		if (FCsvProfiler::IsCapturing() || !IFileManager::Get().MakeDirectory(*CsvDirectory, true))
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("CSV capture is already active or its directory cannot be created."));
			return false;
		}
		// csv.BlockOnCaptureEnd は既定の 1 のままにする。0 にして書き出しを非同期にすると、UE 5.8 では
		// CSV の処理スレッドが FinalizeCsvFile 中の FCsvStreamWriter::GetAllocatedSize で落ちることがある。
		// 既定では EndFrame の中で書き出しを終えるので、Writing 状態の Tick（frame_ms に含めない）で Future が準備完了になる。
		// 開始は次の BeginFrame で処理されるため、後続の Tick で確認する。
		Profiler->BeginCapture(-1, CsvDirectory, CsvFilename, ECsvProfilerFlags::None);
		CsvState = ECsvCaptureState::Starting;
		return true;
	case ECsvCaptureState::Starting:
		if (FCsvProfiler::IsCapturing())
		{
			CsvStartRunnerFrame = FrameIndex;
			CsvStartCsvFrame = Profiler->GetCaptureFrameNumber() - 1;
			CsvState = ECsvCaptureState::Capturing;
			return true;
		}
		if (++CsvWaitTicks > 120)
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("CSV capture did not start."));
			return false;
		}
		return true;
	case ECsvCaptureState::Capturing:
		if (!FCsvProfiler::IsCapturing())
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("CSV capture ended unexpectedly."));
			return false;
		}
		if (Profiler->GetCaptureFrameNumber() - CsvStartCsvFrame >= CsvFrames)
		{
			// EndFrame が停止要求を遅延処理する。Future の Get は準備完了後だけ呼ぶ。
			CsvWriteFuture = Profiler->EndCapture();
			if (!CsvWriteFuture.IsValid())
			{
				return false;
			}
			CsvWriteStartSeconds = FPlatformTime::Seconds();
			CsvState = ECsvCaptureState::Writing;
		}
		return true;
	case ECsvCaptureState::Writing:
		if (CsvWriteFuture.IsReady())
		{
			CsvWrittenPath = CsvWriteFuture.Get();
			if (CsvWrittenPath.IsEmpty() || !IFileManager::Get().FileExists(*CsvWrittenPath))
			{
				UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("CSV capture did not write a file."));
				return false;
			}
			CsvState = ECsvCaptureState::Complete;
			return true;
		}
		if (FPlatformTime::Seconds() - CsvWriteStartSeconds > 120.0)
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("CSV capture write timed out."));
			return false;
		}
		return true;
	}
	return false;
#else
	return true;
#endif
}

bool UKawaiiPhysicsVerificationSubsystem::RecordCellNodes(FCell& Cell) const
{
	using namespace KawaiiPhysicsVerificationPrivate;
	USkeletalMeshComponent* Component = Cell.Component.Get();
	TArray<FKawaiiPhysicsReference> Nodes;
	if (!Component || !UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, Component, FGameplayTagContainer(), false) ||
		(!Cell.NodeRecords.IsEmpty() && Nodes.Num() != Cell.NodeRecords.Num()))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: node count changed during recording."), *Cell.Id);
		return false;
	}
	// 初回評価までは ModifyBones が空なので、全ノードが揃ったフレームから記録を始める。
	if (Cell.NodeRecords.IsEmpty())
	{
		for (FKawaiiPhysicsReference& Node : Nodes)
		{
			if (Node.GetAnimNode<FAnimNode_KawaiiPhysics>().ModifyBones.IsEmpty())
			{
				return true;
			}
		}
	}
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		const FAnimNode_KawaiiPhysics& Node = Nodes[NodeIndex].GetAnimNode<FAnimNode_KawaiiPhysics>();
		FKawaiiPhysicsRuntimeNodeInfo Info;
		KawaiiPhysics::BuildRuntimeNodeInfo(Node, Info);
		if (Info.Bones.Num() != Node.ModifyBones.Num())
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: runtime bone count differs from the node."), *Cell.Id);
			return false;
		}
		if (Cell.NodeRecords.Num() == NodeIndex)
		{
			FCell::FNodeRecord& Record = Cell.NodeRecords.AddDefaulted_GetRef();
			Record.Root = Node.RootBone.BoneName.ToString();
			Record.FirstFrame = FrameIndex;
			Record.BoneCount = Info.Bones.Num();
			Record.BonesJson = TEXT("[");
			for (int32 BoneIndex = 0; BoneIndex < Info.Bones.Num(); ++BoneIndex)
			{
				const FKawaiiPhysicsRuntimeBoneInfo& Bone = Info.Bones[BoneIndex];
				const FKawaiiPhysicsModifyBone& ModifyBone = Node.ModifyBones[BoneIndex];
				const bool bPerAxis = ModifyBone.PhysicsSettings.LimitAngleMode == EKawaiiPhysicsLimitAngleMode::PerAxis;
				Record.BonesJson += FString::Printf(TEXT("%s{\"name\":\"%s\",\"dummy\":\"%s\",\"parent\":%d,\"skip\":%s,\"angle_parent\":%d,\"angle_mode\":\"%s\"}"),
					BoneIndex ? TEXT(",") : TEXT(""), Bone.BoneName.IsNone() ? TEXT("") : *EscapeJson(Bone.BoneName.ToString()),
					*UEnum::GetValueAsString(Bone.DummyType).RightChop(FString(TEXT("EKawaiiPhysicsDummyBoneType::")).Len()),
					Bone.ParentIndex, Bone.bSkipSimulate ? TEXT("true") : TEXT("false"),
					Node.IsAngleLimitTargetBone(ModifyBone) ? Node.GetAngleLimitParentIndex(ModifyBone) : INDEX_NONE,
					bPerAxis ? TEXT("PerAxis") : TEXT("Cone"));
			}
			Record.BonesJson += TEXT("]");
		}
		FCell::FNodeRecord& Record = Cell.NodeRecords[NodeIndex];
		if (Record.BoneCount != Info.Bones.Num() || Info.Bones.Num() != Node.ModifyBones.Num())
		{
			UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: bone count changed during recording."), *Cell.Id);
			return false;
		}
		for (int32 BoneIndex = 0; BoneIndex < Info.Bones.Num(); ++BoneIndex)
		{
			const FKawaiiPhysicsRuntimeBoneInfo& Bone = Info.Bones[BoneIndex];
			const FKawaiiPhysicsSettings& Settings = Node.ModifyBones[BoneIndex].PhysicsSettings;
			const FKawaiiPhysicsPerAxisLimitAngle& Axis = Settings.PerAxisLimitAngle;
			const double LimitAngle = Settings.LimitAngleMode == EKawaiiPhysicsLimitAngleMode::Cone ?
				static_cast<double>(Settings.LimitAngle) :
				FMath::Max(FMath::Max(FMath::Max(static_cast<double>(Axis.X.Min), static_cast<double>(Axis.X.Max)),
					FMath::Max(static_cast<double>(Axis.Y.Min), static_cast<double>(Axis.Y.Max))),
					FMath::Max(static_cast<double>(Axis.Z.Min), static_cast<double>(Axis.Z.Max)));
			Record.BoneFrames.Append({Bone.Location.X, Bone.Location.Y, Bone.Location.Z,
				Bone.PoseLocation.X, Bone.PoseLocation.Y, Bone.PoseLocation.Z, static_cast<double>(Bone.Radius), LimitAngle});
		}
		TArray<double>& Limits = Record.LimitFrames.AddDefaulted_GetRef();
		for (const FKawaiiPhysicsRuntimeLimitInfo& Limit : Info.Limits)
		{
			Limits.Append({static_cast<double>(static_cast<uint8>(Limit.LimitType)), Limit.bEnabled ? 1.0 : 0.0,
				Limit.bInnerSphere ? 1.0 : 0.0, Limit.Location.X, Limit.Location.Y, Limit.Location.Z,
				Limit.Rotation.X, Limit.Rotation.Y, Limit.Rotation.Z, Limit.Rotation.W,
				Limit.Start.X, Limit.Start.Y, Limit.Start.Z, Limit.End.X, Limit.End.Y, Limit.End.Z,
				static_cast<double>(Limit.Radius0), static_cast<double>(Limit.Radius1),
				Limit.Extent.X, Limit.Extent.Y, Limit.Extent.Z,
				Limit.PlaneNormal.X, Limit.PlaneNormal.Y, Limit.PlaneNormal.Z});
		}
	}
	return true;
}

void UKawaiiPhysicsVerificationSubsystem::WriteRecordingAndExit(const bool bSucceeded)
{
	using namespace KawaiiPhysicsVerificationPrivate;

	int32 NumKawaiiPhysicsNodes = 0;
	int32 NumDisabledNodes = 0;
	for (const FCell& Cell : Cells)
	{
		USkeletalMeshComponent* Component = Cell.Component.Get();
		TArray<FKawaiiPhysicsReference> Nodes;
		if (Component && UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, Component, FGameplayTagContainer(), false))
		{
			NumKawaiiPhysicsNodes += Nodes.Num();
			for (FKawaiiPhysicsReference& Node : Nodes)
			{
				if (bDisableKawaiiPhysics && Node.GetAnimNode<FAnimNode_KawaiiPhysics>().Alpha == 0.f)
				{
					++NumDisabledNodes;
				}
			}
		}
	}
	FString Output = FString::Printf(TEXT("{\"type\":\"run\",\"version\":2,\"ok\":%s,\"build\":\"%s\","
		"\"kp_disabled_requested\":%s,\"kp_nodes\":%d,\"kp_disabled_nodes\":%d,\"frames\":%d,\"frame_ms\":["),
		bSucceeded ? TEXT("true") : TEXT("false"), ReadRunnerBuild(),
		bDisableKawaiiPhysics ? TEXT("true") : TEXT("false"), NumKawaiiPhysicsNodes, NumDisabledNodes, FrameIndex);
	AppendNumbers(Output, FrameMilliseconds);
	Output += TEXT("],\"delta_seconds\":[");
	AppendNumbers(Output, FrameDeltaSeconds);
	const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	const IConsoleVariable* ParallelEvaluation = IConsoleManager::Get().FindConsoleVariable(TEXT("a.ParallelAnimEvaluation"));
	const IConsoleVariable* ParallelUpdate = IConsoleManager::Get().FindConsoleVariable(TEXT("a.ParallelAnimUpdate"));
	Output += FString::Printf(TEXT("],\"settings\":{\"fixed_substepping\":%s,\"max_substeps\":%d,\"time_dilation\":"),
		Settings->bUseFixedSubstepping ? TEXT("true") : TEXT("false"), Settings->MaxSubsteps);
	AppendNumber(Output, GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation());
	Output += FString::Printf(TEXT(",\"parallel_anim_evaluation\":%d,\"parallel_anim_update\":%d}"),
		ParallelEvaluation ? ParallelEvaluation->GetInt() : 0, ParallelUpdate ? ParallelUpdate->GetInt() : 0);
	Output += TEXT(",\"effective_cvars\":");
	Output += EffectiveCVarsJson;
	Output += TEXT(",\"calib_start_ms\":[");
	AppendNumbers(Output, CalibrationStartMilliseconds);
	Output += TEXT("],\"calib_end_ms\":[");
	AppendNumbers(Output, CalibrationEndMilliseconds);
	Output += TEXT("]");
	if (CsvState != ECsvCaptureState::Disabled)
	{
		Output += FString::Printf(TEXT(",\"csv\":{\"path\":\"%s\",\"start_runner_frame\":%d,\"start_csv_frame\":%d,\"frames\":%d}"),
			*EscapeJson(CsvWrittenPath.Replace(TEXT("\\"), TEXT("/"))), CsvStartRunnerFrame, CsvStartCsvFrame, CsvFrames);
	}
	Output += TEXT("}\n");

	for (const FCell& Cell : Cells)
	{
		const USkeletalMeshComponent* Component = Cell.Component.Get();
		if (!Component || !Component->GetSkinnedAsset())
		{
			continue;
		}

		const FReferenceSkeleton& RefSkeleton = Component->GetSkinnedAsset()->GetRefSkeleton();
		const UAnimInstance* AnimInstance = Component->GetAnimInstance();
		Output += FString::Printf(TEXT("{\"type\":\"cell\",\"id\":\"%s\",\"anim_class\":\"%s\",\"bones\":["),
		                          *EscapeJson(Cell.Id),
		                          AnimInstance ? *EscapeJson(AnimInstance->GetClass()->GetName()) : TEXT(""));
		for (int32 Bone = 0; Bone < RefSkeleton.GetNum(); ++Bone)
		{
			Output += FString::Printf(TEXT("%s[\"%s\",%d]"), Bone > 0 ? TEXT(",") : TEXT(""),
			                          *RefSkeleton.GetBoneName(Bone).ToString(), RefSkeleton.GetParentIndex(Bone));
		}
		Output += TEXT("],\"ref_pose\":[");
		TArray<double> RefPoseValues;
		RefPoseValues.Reserve(RefSkeleton.GetNum() * 7);
		for (int32 Bone = 0; Bone < RefSkeleton.GetNum(); ++Bone)
		{
			const FTransform RefTransform = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, Bone);
			const FVector Location = RefTransform.GetLocation();
			const FQuat Rotation = RefTransform.GetRotation();
			RefPoseValues.Append({Location.X, Location.Y, Location.Z, Rotation.X, Rotation.Y, Rotation.Z, Rotation.W});
		}
		AppendNumbers(Output, RefPoseValues);
		Output += TEXT("],\"nodes\":[");
		// 上書きが効かないまま一致して合格にならないよう、実際にノードへ入った値を記録する。
		TArray<FKawaiiPhysicsReference> Nodes;
		UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, const_cast<USkeletalMeshComponent*>(Component),
		                                                 FGameplayTagContainer(), false);
		for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
		{
			const FAnimNode_KawaiiPhysics& AnimNode = Nodes[NodeIndex].GetAnimNode<FAnimNode_KawaiiPhysics>();
			Output += FString::Printf(
				TEXT("%s{\"root\":\"%s\",\"space\":\"%s\",\"base\":\"%s\",\"gravity\":\"%s\",\"bones\":%d,"
					"\"limits\":[%d,%d,%d,%d,%d],\"limits_asset\":%s,\"physics_asset\":%s,\"constraints\":%d,"
					"\"constraints_asset\":%s,\"sync_bones\":%d,\"forces\":%d,\"dummy\":%g,\"subdivision\":%d,"
					"\"planar\":%d,\"world_collision\":%s,\"simple_world\":%s,\"bone_names\":["),
				NodeIndex > 0 ? TEXT(",") : TEXT(""), *AnimNode.RootBone.BoneName.ToString(),
				*UEnum::GetValueAsString(AnimNode.SimulationSpace),
				*AnimNode.SimulationBaseBone.BoneName.ToString(), *AnimNode.Gravity.ToString(),
				AnimNode.ModifyBones.Num(), AnimNode.SphericalLimits.Num(), AnimNode.CapsuleLimits.Num(),
				AnimNode.TaperedCapsuleLimits.Num(), AnimNode.BoxLimits.Num(), AnimNode.PlanarLimits.Num(),
				AnimNode.LimitsDataAsset ? TEXT("true") : TEXT("false"),
				AnimNode.PhysicsAssetForLimits ? TEXT("true") : TEXT("false"), AnimNode.BoneConstraints.Num(),
				AnimNode.BoneConstraintsDataAsset ? TEXT("true") : TEXT("false"), AnimNode.SyncBones.Num(),
				AnimNode.ExternalForces.Num(), AnimNode.DummyBoneLength, AnimNode.BoneSubdivisionCount,
				static_cast<int32>(AnimNode.PlanarConstraint), AnimNode.bAllowWorldCollision ? TEXT("true") : TEXT("false"),
				AnimNode.bUseSimpleWorldCollision ? TEXT("true") : TEXT("false"));
			bool bFirstBone = true;
			for (const FKawaiiPhysicsModifyBone& Bone : AnimNode.ModifyBones)
			{
				if (!Bone.bDummy)
				{
					Output += FString::Printf(TEXT("%s\"%s\""), bFirstBone ? TEXT("") : TEXT(","),
					                          *Bone.BoneRef.BoneName.ToString());
					bFirstBone = false;
				}
			}
			Output += TEXT("],\"physics_settings\":{");
			const FKawaiiPhysicsSettings& Physics = AnimNode.PhysicsSettings;
			Output += TEXT("\"damping\":"); AppendNumber(Output, Physics.Damping);
			Output += TEXT(",\"stiffness\":"); AppendNumber(Output, Physics.Stiffness);
			Output += TEXT(",\"world_damping_location\":"); AppendNumber(Output, Physics.WorldDampingLocation);
			Output += TEXT(",\"world_damping_rotation\":"); AppendNumber(Output, Physics.WorldDampingRotation);
			Output += TEXT(",\"radius\":"); AppendNumber(Output, Physics.Radius);
			Output += TEXT(",\"limit_angle\":"); AppendNumber(Output, Physics.LimitAngle);
			Output += FString::Printf(TEXT(",\"limit_angle_mode\":\"%s\"}}"),
				Physics.LimitAngleMode == EKawaiiPhysicsLimitAngleMode::Cone ? TEXT("Cone") : TEXT("PerAxis"));
		}
		Output += TEXT("],\"transforms\":[");
		AppendNumbers(Output, Cell.Samples);
		if (bRecordNodes)
		{
			Output += TEXT("],\"node_records\":[");
			for (int32 NodeIndex = 0; NodeIndex < Cell.NodeRecords.Num(); ++NodeIndex)
			{
				const FCell::FNodeRecord& Record = Cell.NodeRecords[NodeIndex];
				Output += FString::Printf(TEXT("%s{\"root\":\"%s\",\"first_frame\":%d,\"bones\":%s,\"bone_frames\":["),
					NodeIndex ? TEXT(",") : TEXT(""), *EscapeJson(Record.Root), Record.FirstFrame, *Record.BonesJson);
				AppendNumbers(Output, Record.BoneFrames);
				Output += TEXT("],\"limit_frames\":[");
				for (int32 Frame = 0; Frame < Record.LimitFrames.Num(); ++Frame)
				{
					Output += Frame ? TEXT(",[") : TEXT("[");
					AppendNumbers(Output, Record.LimitFrames[Frame]);
					Output += TEXT("]");
				}
				Output += TEXT("]}");
			}
		}
		Output += TEXT("]}\n");
	}

	const bool bWritten = FFileHelper::SaveStringToFile(Output, *OutputPath,
	                                                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_CLOG(!bWritten, LogKawaiiPhysicsVerification, Error, TEXT("Cannot write %s."), *OutputPath);
	FPlatformMisc::RequestExitWithStatus(false, bSucceeded && bWritten ? 0 : 1);
}
