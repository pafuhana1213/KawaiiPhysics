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
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

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
	if (!CellObject.IsValid() || !CellObject->TryGetStringField(TEXT("id"), Cell.Id) ||
		!CellObject->TryGetStringField(TEXT("mesh"), MeshPath) ||
		!CellObject->TryGetStringField(TEXT("anim_class"), AnimClassPath))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("A cell needs id, mesh and anim_class."));
		return false;
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, *AnimClassPath);
	if (!Mesh || !AnimClass)
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: cannot load %s or %s."), *Cell.Id, *MeshPath,
		       *AnimClassPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* MotionObject = nullptr;
	Cell.Motion.BaseLocation = ReadVector(CellObject, TEXT("location"));
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
	Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Component->SetAnimInstanceClass(AnimClass);
	Component->SetSkeletalMeshAsset(Mesh);
	Component->InitAnim(true);

	UAnimInstance* AnimInstance = Component->GetAnimInstance();
	TArray<FKawaiiPhysicsReference> Nodes;
	if (!AnimInstance ||
		!UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, Component, FGameplayTagContainer(), false))
	{
		UE_LOG(LogKawaiiPhysicsVerification, Error, TEXT("Cell %s: no KawaiiPhysics node in %s."), *Cell.Id,
		       *AnimClassPath);
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

	if (bHasOverrides || bClearExternalForces)
	{
		// Initialize_AnyThread は SimulationSpace に応じて初期状態を作るため、ABP に保存した場合と揃えるには上書き後に初期化し直す。
		AnimInstance->UninitializeAnimation();
		AnimInstance->InitializeAnimation();
		AnimInstance->RecalcRequiredBones();
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

bool UKawaiiPhysicsVerificationSubsystem::ApplyDueEvents(const FCell& Cell) const
{
	USkeletalMeshComponent* Component = Cell.Component.Get();
	for (const FCellEvent& Event : Cell.Events)
	{
		if (Event.Frame != FrameIndex || !Component)
		{
			continue;
		}

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
	const double Yaw = Motion.YawAmplitudeDegrees * FMath::Sin(UE_DOUBLE_TWO_PI * Time / Motion.YawPeriod);
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
	for (const FCell& Cell : Cells)
	{
		ApplyMotion(Cell, SimulationTime);
		if (!ApplyDueEvents(Cell))
		{
			bRunning = false;
			WriteRecordingAndExit(false);
			return;
		}
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
	FrameMilliseconds.Add((NowSeconds - LastTickSeconds) * 1000.0);

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
	}

	RequestFrameCapture();
	if (++FrameIndex >= NumFrames)
	{
		bRunning = false;
		KawaiiPhysicsVerificationPrivate::MeasureCalibration(CalibrationEndMilliseconds);
		WriteRecordingAndExit(true);
		return;
	}
	LastTickSeconds = FPlatformTime::Seconds();
}

void UKawaiiPhysicsVerificationSubsystem::WriteRecordingAndExit(const bool bSucceeded)
{
	using namespace KawaiiPhysicsVerificationPrivate;

	FString Output = FString::Printf(TEXT("{\"type\":\"run\",\"version\":1,\"ok\":%s,\"frames\":%d,\"frame_ms\":["),
	                                 bSucceeded ? TEXT("true") : TEXT("false"), FrameIndex);
	AppendNumbers(Output, FrameMilliseconds);
	Output += TEXT("],\"calib_start_ms\":[");
	AppendNumbers(Output, CalibrationStartMilliseconds);
	Output += TEXT("],\"calib_end_ms\":[");
	AppendNumbers(Output, CalibrationEndMilliseconds);
	Output += TEXT("]}\n");

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
			Output += TEXT("]}");
		}
		Output += TEXT("],\"transforms\":[");
		AppendNumbers(Output, Cell.Samples);
		Output += TEXT("]}\n");
	}

	const bool bWritten = FFileHelper::SaveStringToFile(Output, *OutputPath,
	                                                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_CLOG(!bWritten, LogKawaiiPhysicsVerification, Error, TEXT("Cannot write %s."), *OutputPath);
	FPlatformMisc::RequestExitWithStatus(false, bSucceeded && bWritten ? 0 : 1);
}
