// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Basic.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Curve.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"
#include "Animation/AnimInstanceProxy.h"
#include "UObject/UnrealType.h"

namespace KawaiiPhysicsWindSpecNodeTestPrivate
{

KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsWindSpecLocal, "KawaiiPhysics.Test.WindSpec.Local");
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsWindSpecMissing, "KawaiiPhysics.Test.WindSpec.Missing");

KawaiiPhysicsNodeFixture::FOptions MakeConstantWindOptions(const FVector& AuthoredDirection, const float Strength)
{
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(3);
	NodeOptions.bPreUpdateEveryFrame = true;
	NodeOptions.ConfigureNode = [AuthoredDirection, Strength](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
		auto& AuthoredWind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode);
		AuthoredWind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
		AuthoredWind.SharedWindTag = TAG_KawaiiPhysicsWindSpecLocal;
		AuthoredWind.WindDirection = AuthoredDirection;
		AuthoredWind.WindDirectionNoiseAngle = 0.0f;
		AuthoredWind.ConstantForce = Strength;
		AuthoredWind.SwayForce = 0.0f;
		AuthoredWind.RippleForce = 0.0f;
		AuthoredWind.RandomForce = 0.0f;
		AuthoredWind.StrengthCycleRange = FFloatInterval(1.0f, 1.0f);
	};
	return NodeOptions;
}

bool HasSameBonePositions(const TArray<FBoneTransform>& First, const TArray<FBoneTransform>& Second)
{
	if (First.Num() != Second.Num()) return false;
	for (int32 BoneIndex = 0; BoneIndex < First.Num(); ++BoneIndex)
	{
		if (First[BoneIndex].BoneIndex != Second[BoneIndex].BoneIndex ||
			First[BoneIndex].Transform.GetLocation() != Second[BoneIndex].Transform.GetLocation()) return false;
	}
	return true;
}

// TW-SH-31。生き残り mutant なし。Local が Publisher を誤採用すると、強さ 900 の公開風で 60 フレームの位置のビット一致が崩れる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecNodeLocalIgnoresPublisherTest,
	"KawaiiPhysics.WindSpec.Node.LocalIgnoresPublisher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecNodeLocalIgnoresPublisherTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture WithoutPublisher(MakeConstantWindOptions(FVector::ForwardVector, 200.0f));
	KawaiiPhysicsNodeFixture::FFixture WithPublisher(MakeConstantWindOptions(FVector::ForwardVector, 200.0f));
	KawaiiPhysicsNodeFixture::FFixture WithoutWind(MakeConstantWindOptions(FVector::ForwardVector, 0.0f));
	FAnimNode_KawaiiPhysicsSharedPublisher SharedPublisher;
	SharedPublisher.SharedGroupTag = TAG_KawaiiPhysicsWindSpecLocal;
	SharedPublisher.SharedWind.ConstantForce = 900.0f;
	SharedPublisher.SharedWind.EnsureRuntimeState();
	bool bOk = TestTrue(TEXT("風源は既定の Local"),
		WithPublisher.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce_ProceduralWind>().WindSource ==
		EKawaiiPhysicsProceduralWindSource::Local);
	uint64 PreviousSerial = 0;
	bool bWindMovedBone = false;
	for (int32 FrameIndex = 0; FrameIndex < 60; ++FrameIndex)
	{
		WithPublisher.World->TimeSeconds = (FrameIndex + 1) / 60.0f;
		SharedPublisher.PreUpdate(WithPublisher.Anim);
		FAnimInstanceProxy PublisherProxy(WithPublisher.Anim);
		SharedPublisher.Update_AnyThread(FAnimationUpdateContext(&PublisherProxy, 1.0f / 60.0f));
		const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublishedEntry = SharedPublisher.GetSharedPublisherEntry();
		bOk &= TestTrue(TEXT("対照: Publisher が同じ Actor に公開"), PublishedEntry.IsValid());
		if (!PublishedEntry.IsValid()) return false;
		const uint64 CurrentSerial = PublishedEntry->GetPublishSerial();
		bOk &= TestTrue(TEXT("対照: 公開 serial が進む"), CurrentSerial > PreviousSerial);
		PreviousSerial = CurrentSerial;
		const auto& ReferenceOutput = WithoutPublisher.EvaluateFrame();
		const auto& PublishedOutput = WithPublisher.EvaluateFrame();
		const auto& StillOutput = WithoutWind.EvaluateFrame();
		bOk &= TestTrue(TEXT("対照: ModifyBones がある"), WithPublisher.Node.ModifyBones.Num() > 0);
		bOk &= TestTrue(FString::Printf(TEXT("Local の全ボーン位置はビット一致 %d"), FrameIndex),
			HasSameBonePositions(ReferenceOutput, PublishedOutput));
		if (FrameIndex == 59 && ReferenceOutput.Num() == StillOutput.Num())
		{
			for (int32 BoneIndex = 0; BoneIndex < ReferenceOutput.Num(); ++BoneIndex)
			{
				bWindMovedBone |= (ReferenceOutput[BoneIndex].Transform.GetLocation() -
					StillOutput[BoneIndex].Transform.GetLocation()).Size() > 0.01f;
			}
		}
	}
	bOk &= TestTrue(TEXT("対照: Local 風で位置が変わる"), bWindMovedBone);
	return bOk;
}

// TW-PW-23。3032: D と 10D を正規化しないと力が十倍となり、30 フレームの位置差が 1e-5 cm を超える。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecNodeDirectionMagnitudeTest,
	"KawaiiPhysics.WindSpec.Node.DirectionMagnitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecNodeDirectionMagnitudeTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture OriginalDirection(MakeConstantWindOptions(FVector(3.0f, 4.0f, 0.0f), 200.0f));
	KawaiiPhysicsNodeFixture::FFixture ScaledDirection(MakeConstantWindOptions(FVector(30.0f, 40.0f, 0.0f), 200.0f));
	KawaiiPhysicsNodeFixture::FFixture NoWind(MakeConstantWindOptions(FVector(3.0f, 4.0f, 0.0f), 0.0f));
	bool bOk = true;
	for (int32 FrameIndex = 0; FrameIndex < 30; ++FrameIndex)
	{
		OriginalDirection.EvaluateFrame();
		ScaledDirection.EvaluateFrame();
		NoWind.EvaluateFrame();
	}
	bOk &= TestTrue(TEXT("対照: ModifyBones がある"), OriginalDirection.Node.ModifyBones.Num() > 0);
	bOk &= TestTrue(TEXT("対照: 出力ボーンがある"), OriginalDirection.OutTransforms.Num() > 0 &&
		OriginalDirection.OutTransforms.Num() == NoWind.OutTransforms.Num());
	if (!bOk) return false;
	bool bWindMovedBone = false;
	for (int32 BoneIndex = 0; BoneIndex < OriginalDirection.OutTransforms.Num(); ++BoneIndex)
	{
		const FVector OriginalLocation = OriginalDirection.OutTransforms[BoneIndex].Transform.GetLocation();
		const FVector ScaledLocation = ScaledDirection.OutTransforms[BoneIndex].Transform.GetLocation();
		const FVector StillLocation = NoWind.OutTransforms[BoneIndex].Transform.GetLocation();
		// 同じ設定と積分順の float 丸めとして 1e-5 cm を許す。
		bOk &= TestTrue(TEXT("方向の長さは変位に影響しない"), OriginalLocation.Equals(ScaledLocation, 1e-5f));
		bWindMovedBone |= (OriginalLocation - StillLocation).Size() > 0.01f;
	}
	bOk &= TestTrue(TEXT("対照: 風の変位が非零"), bWindMovedBone);
	return bOk;
}

KawaiiPhysicsNodeFixture::FOptions MakeBoneForceOptions(
	const EKawaiiPhysicsSimulationSpace SimulationSpace, const bool bAssignBaseBone, const float Strength)
{
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	NodeOptions.SimulationSpace = SimulationSpace;
	NodeOptions.BaseBoneName = bAssignBaseBone ? FName(TEXT("root")) : NAME_None;
	NodeOptions.GetComponentTransform = [](const int32 FrameIndex)
	{
		return FTransform(FRotator(0.0f, 90.0f, 0.0f));
	};
	NodeOptions.SetInputPose = [](FCompactPose& LocalPose, const int32 FrameIndex)
	{
		LocalPose[FCompactPoseBoneIndex(1)].SetRotation(FQuat(FVector::UpVector, PI / 6.0));
	};
	NodeOptions.ConfigureNode = [Strength](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
		auto& AuthoredForce = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Basic>(AnimNode);
		AuthoredForce.ExternalForceSpace = EExternalForceSpace::BoneSpace;
		AuthoredForce.ForceDir = FVector(Strength, 0.0f, 0.0f);
		AuthoredForce.Interval = 0.0f;
	};
	return NodeOptions;
}

// TW-WS-16。生き残り mutant なし。BoneTM の二重適用では CS の差分が Yaw 60°、World の差分が Yaw 240° となり 1 回転の方向と一致しない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecNodeBoneSpaceOnceTest,
	"KawaiiPhysics.WindSpec.Node.BoneSpaceOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecNodeBoneSpaceOnceTest::RunTest(const FString& Parameters)
{
	struct FSpaceCase
	{
		EKawaiiPhysicsSimulationSpace SimulationSpace;
		bool bAssignBaseBone;
		FVector ExpectedDirection;
		FVector DoubleDirection;
	};
	const float Cos30 = FMath::Sqrt(3.0f) * 0.5f;
	// Bone の局所 +X を Yaw 30° で 1 回回す。CS は (cos30,sin30)、BaseBone は root が恒等回転なので同じ。
	// World は Component の Yaw 90° を加えて Yaw 120°。CS＋BaseBone 指定も SimulationSpace は CS のまま。
	const FSpaceCase SpaceCases[] = {
		{EKawaiiPhysicsSimulationSpace::ComponentSpace, false,
			FVector(Cos30, 0.5f, 0.0f), FVector(0.5f, Cos30, 0.0f)},
		{EKawaiiPhysicsSimulationSpace::BaseBoneSpace, true,
			FVector(Cos30, 0.5f, 0.0f), FVector(0.5f, Cos30, 0.0f)},
		{EKawaiiPhysicsSimulationSpace::WorldSpace, false,
			FVector(-0.5f, Cos30, 0.0f), FVector(-0.5f, -Cos30, 0.0f)},
		{EKawaiiPhysicsSimulationSpace::ComponentSpace, true,
			FVector(Cos30, 0.5f, 0.0f), FVector(0.5f, Cos30, 0.0f)}
	};
	bool bOk = true;
	for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(SpaceCases); ++CaseIndex)
	{
		const FSpaceCase& SpaceCase = SpaceCases[CaseIndex];
		KawaiiPhysicsNodeFixture::FFixture WithForce(MakeBoneForceOptions(
			SpaceCase.SimulationSpace, SpaceCase.bAssignBaseBone, 1.0f));
		KawaiiPhysicsNodeFixture::FFixture WithoutForce(MakeBoneForceOptions(
			SpaceCase.SimulationSpace, SpaceCase.bAssignBaseBone, 0.0f));
		WithForce.EvaluateFrame();
		WithoutForce.EvaluateFrame();
		bOk &= TestTrue(TEXT("対照: 実ボーンが構築された"), WithForce.Node.ModifyBones.Num() >= 2 &&
			!WithForce.Node.ModifyBones[1].bDummy);
		if (WithForce.Node.ModifyBones.Num() < 2 || WithoutForce.Node.ModifyBones.Num() < 2) return false;
		const FVector DeltaLocation = WithForce.Node.ModifyBones[1].Location -
			WithoutForce.Node.ModifyBones[1].Location;
		bOk &= TestTrue(TEXT("対照: BoneSpace 外力が変位を作る"), DeltaLocation.Size() > 0.001f);
		if (DeltaLocation.Size() <= 0.001f) continue;
		const FVector DeltaDirection = DeltaLocation.GetSafeNormal();
		const float OneRotationAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(DeltaDirection, SpaceCase.ExpectedDirection), -1.0f, 1.0f)));
		const float TwoRotationAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(DeltaDirection, SpaceCase.DoubleDirection), -1.0f, 1.0f)));
		// 単一フレームの float 積分と acos の丸めとして 0.1° を許す。
		bOk &= TestTrue(FString::Printf(TEXT("1 回の BoneTM 回転 %d"), CaseIndex), OneRotationAngle <= 0.1f);
		bOk &= TestTrue(FString::Printf(TEXT("2 回の BoneTM 回転ではない %d"), CaseIndex), TwoRotationAngle > 10.0f);
	}
	return bOk;
}

// TW-RI-05。生き残り mutant なし。ノードが追加外力の Initialize を飛ばして初期化済み印だけ付けると、風時計が 5 秒からリセットされず初回評価後の 1/60 秒で落ちる。
// ノード経路の Initialize 呼び出しを ProceduralWind で観測し、Curve 単体の Initialize が最大時刻 2 秒を決めることと合わせて、パッケージ版の実行時追加 Curve のループを押さえる。
// エディタでは PreApply が最大時刻を補うので、150 フレームの主張だけでは足りない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecNodeRuntimeCurveLoopTest,
	"KawaiiPhysics.WindSpec.Node.RuntimeCurveLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecNodeRuntimeCurveLoopTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	NodeOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
	};
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(NodeOptions);
	NodeFixture.EvaluateFrame();
	bool bOk = TestTrue(TEXT("対照: 初期化済みノードに ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
	auto& RuntimeCurve = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Curve>(NodeFixture.Node);
	RuntimeCurve.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	RuntimeCurve.CurveEvaluateType = EExternalForceCurveEvaluateType::Single;
	FRichCurve* XCurve = RuntimeCurve.ForceCurve.GetRichCurve(0);
	const FKeyHandle FirstKey = XCurve->AddKey(0.0f, 0.0f);
	const FKeyHandle MiddleKey = XCurve->AddKey(1.0f, 10.0f);
	const FKeyHandle LastKey = XCurve->AddKey(2.0f, 0.0f);
	XCurve->SetKeyInterpMode(FirstKey, RCIM_Linear);
	XCurve->SetKeyInterpMode(MiddleKey, RCIM_Linear);
	XCurve->SetKeyInterpMode(LastKey, RCIM_Linear);
	const FFloatProperty* MaximumTimeProperty = FindFProperty<FFloatProperty>(
		FKawaiiPhysics_ExternalForce_Curve::StaticStruct(), TEXT("MaxCurveTime"));
	if (!TestNotNull(TEXT("MaxCurveTime の反射プロパティ"), MaximumTimeProperty)) return false;
	FKawaiiPhysics_ExternalForce_Curve InitializationProbe;
	InitializationProbe.ForceCurve = RuntimeCurve.ForceCurve;
	bOk &= TestEqual(TEXT("対照: Initialize 前の最大時刻は零"),
		MaximumTimeProperty->GetPropertyValue_InContainer(&InitializationProbe), 0.0f);
	FAnimInstanceProxy InitializationProxy(NodeFixture.Anim);
	InitializationProbe.Initialize(FAnimationInitializeContext(&InitializationProxy));
	bOk &= TestEqual(TEXT("PreApply 前の Initialize で最大時刻は 2 秒"),
		MaximumTimeProperty->GetPropertyValue_InContainer(&InitializationProbe), 2.0f);
	bOk &= TestFalse(TEXT("対照: 追加直後は未初期化"), RuntimeCurve.IsRuntimeInitialized());
	auto& RuntimeWind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(NodeFixture.Node);
	RuntimeWind.TimeScale = 1.25f;
	RuntimeWind.ConstantForce = 100.0f;
	RuntimeWind.EnsureRuntimeState();
	RuntimeWind.RuntimeState->Time = 5.0f;
	bOk &= TestFalse(TEXT("対照: 追加した風も未初期化"), RuntimeWind.IsRuntimeInitialized());
	bOk &= TestEqual(TEXT("対照: 初回評価前の風時計は 5 秒"), RuntimeWind.RuntimeState->Time, 5.0f);
	NodeFixture.EvaluateFrame();
	const auto& InitializedWind = NodeFixture.Node.ExternalForces[1].Get<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	bOk &= TestTrue(TEXT("対照: 風もノード経路で初期化済み"), InitializedWind.IsRuntimeInitialized());
	// Initialize が 5 秒を零へ戻し、最初の評価で (1/60)×1.25 秒進む。float 丸めを 1e-5 秒許す。
	bOk &= TestTrue(TEXT("追加外力の Initialize が最初の使用前に走る"),
		FMath::IsNearlyEqual(InitializedWind.RuntimeState->Time, 1.25f / 60.0f, 1e-5f));
	for (int32 FrameIndex = 1; FrameIndex < 150; ++FrameIndex) NodeFixture.EvaluateFrame();
	const auto& InitializedCurve = NodeFixture.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce_Curve>();
	bOk &= TestTrue(TEXT("対照: Curve も最初の使用前に初期化"), InitializedCurve.IsRuntimeInitialized());
	bOk &= TestEqual(TEXT("最後のキー時刻は 2 秒"),
		MaximumTimeProperty->GetPropertyValue_InContainer(&InitializedCurve), 2.0f);
	// 150/60=2.5 秒、2 秒で先頭に戻ると 0.5 秒。0→1 秒で 0→10 の線形補間なので力は 5。
	// 150 回の float 加算と fmod の丸めを 0.01 だけ許す。
	const FVector AppliedForce = FKawaiiPhysicsTestAccessor::GetExternalForceVector(InitializedCurve);
	bOk &= TestTrue(TEXT("2.5 秒の力は 0.5 秒の値"), AppliedForce.Equals(FVector(5.0f, 0.0f, 0.0f), 0.01f));
	return bOk;
}

#if !UE_BUILD_SHIPPING
struct FScopedSharedWindWarningCounter : FOutputDevice
{
	FThreadSafeCounter WarningCount;
	// ログは非同期に出力先へ配られることがあるので、読む前に Flush して数え終わらせる。
	int32 ReadCount() { GLog->Flush(); return WarningCount.GetValue(); }
	FScopedSharedWindWarningCounter() { GLog->AddOutputDevice(this); }
	~FScopedSharedWindWarningCounter() override { GLog->RemoveOutputDevice(this); }
	bool CanBeUsedOnAnyThread() const override { return true; }
	void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		// テストの実行基盤がテスト中の警告を LogAutomationController でもう一度流すので、本番のカテゴリの行だけ数える。
			if (Category == FName(TEXT("LogKawaiiPhysics")) && Verbosity == ELogVerbosity::Warning &&
			FCString::Strstr(Message, TEXT("Shared Publisher for Shared Wind Tag")))
		{
			WarningCount.Increment();
		}
	}
};

// TW-SH-11・SH-5。生き残り mutant なし。警告閾値が 59 または 61 なら境界で落ち、毎回警告または Auto も警告なら追加 100・160 フレームで落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecNodeSharedMissingPublisherWarningTest,
	"KawaiiPhysics.WindSpec.Node.SharedMissingPublisherWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecNodeSharedMissingPublisherWarningTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FScopedCVar ResolveInterval(
		TEXT("a.AnimNode.KawaiiPhysics.SharedPublisher.AutoResolveInterval"), TEXT("1"));
	if (!TestNotNull(TEXT("解決間隔 CVar がある"), ResolveInterval.Variable)) return false;
	FScopedSharedWindWarningCounter WarningCounter;
	const auto MakeMissingPublisherOptions = [](const EKawaiiPhysicsProceduralWindSource Source)
	{
		KawaiiPhysicsNodeFixture::FOptions NodeOptions;
		NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
		NodeOptions.ConfigureNode = [Source](FAnimNode_KawaiiPhysics& AnimNode)
		{
			AnimNode.RootBone.BoneName = TEXT("root");
			AnimNode.WarmUpFrames = 0;
			AnimNode.Gravity = FVector::ZeroVector;
			auto& MissingPublisherWind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode);
			MissingPublisherWind.WindSource = Source;
			MissingPublisherWind.SharedWindTag = TAG_KawaiiPhysicsWindSpecMissing;
			MissingPublisherWind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
			MissingPublisherWind.ConstantForce = 200.0f;
			MissingPublisherWind.SwayForce = 0.0f;
			MissingPublisherWind.RippleForce = 0.0f;
			MissingPublisherWind.RandomForce = 0.0f;
		};
		return NodeOptions;
	};
	bool bOk = true;
	{
		KawaiiPhysicsNodeFixture::FFixture SharedFixture(MakeMissingPublisherOptions(EKawaiiPhysicsProceduralWindSource::Shared));
		KawaiiPhysicsNodeFixture::FFixture StillFixture(MakeConstantWindOptions(FVector::ForwardVector, 0.0f));
		const auto& SharedWind = SharedFixture.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce_ProceduralWind>();
		// 何フレーム目が何回目の解決かは初期化のフレームに左右されるので決め打ちしない。
		// 仕様（SH-5）は「解決の失敗が 60 回に達したら 1 回」なので、失敗回数を毎フレーム観測して境界を確かめる。
		// 間隔 1 では解決の失敗は毎フレームちょうど 1 回増える（数え方が壊れたらここで落ちる）。
		int32 PreviousFailures = 0;
		bool bFailuresStepByOne = true;
		bool bWarningBeforeThreshold = false;
		bool bMissingWarningAtThreshold = false;
		for (int32 FrameIndex = 1; FrameIndex <= 80; ++FrameIndex)
		{
			SharedFixture.EvaluateFrame();
			StillFixture.EvaluateFrame();
			const int32 ResolveFailures = SharedWind.RuntimeState->SharedResolveFailures;
			if (PreviousFailures > 0 && ResolveFailures != PreviousFailures + 1)
			{
				bFailuresStepByOne = false;
			}
			const int32 WarningsSoFar = WarningCounter.ReadCount();
			bWarningBeforeThreshold |= ResolveFailures < 60 && WarningsSoFar != 0;
			bMissingWarningAtThreshold |= ResolveFailures >= 60 && WarningsSoFar != 1;
			PreviousFailures = ResolveFailures;
		}
		bOk &= TestTrue(TEXT("対照: 失敗回数が 60 に達した"), PreviousFailures >= 60);
		bOk &= TestTrue(TEXT("間隔 1 では解決の失敗は毎フレーム 1 回"), bFailuresStepByOne);
		bOk &= TestFalse(TEXT("59 回目まで警告なし"), bWarningBeforeThreshold);
		bOk &= TestFalse(TEXT("60 回目以降は警告一回"), bMissingWarningAtThreshold);
		bOk &= TestTrue(TEXT("対照: ModifyBones がある"), SharedFixture.Node.ModifyBones.Num() > 0);
		bOk &= TestTrue(TEXT("対照: 有効な Tag がある"), SharedWind.SharedWindTag.IsValid());
		bOk &= TestTrue(TEXT("Publisher 不在では Local を選ぶ"),
			SharedWind.RuntimeState->ResolvedSource == EKawaiiPhysicsProceduralWindSource::Local);
		bOk &= TestTrue(TEXT("対照: Local 風の入力が非零"), SharedWind.RuntimeState->CachedSinesWithoutRipple > 100.0f);
		bOk &= TestTrue(TEXT("対照: Local 風で位置が変わる"),
			SharedFixture.Node.ModifyBones.Num() > 1 && StillFixture.Node.ModifyBones.Num() > 1 &&
			(SharedFixture.Node.ModifyBones[1].Location - StillFixture.Node.ModifyBones[1].Location).Size() > 0.01f);
		for (int32 FrameIndex = 0; FrameIndex < 100; ++FrameIndex) SharedFixture.EvaluateFrame();
		bOk &= TestEqual(TEXT("以後 100 フレームも一回だけ"), WarningCounter.ReadCount(), 1);
	}
	{
		KawaiiPhysicsNodeFixture::FFixture AutoFixture(MakeMissingPublisherOptions(EKawaiiPhysicsProceduralWindSource::Auto));
		for (int32 FrameIndex = 0; FrameIndex < 160; ++FrameIndex) AutoFixture.EvaluateFrame();
		bOk &= TestTrue(TEXT("Auto も Local を選ぶ"),
			AutoFixture.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce_ProceduralWind>().RuntimeState->ResolvedSource ==
			EKawaiiPhysicsProceduralWindSource::Local);
		bOk &= TestEqual(TEXT("Auto は 160 フレーム警告なし"), WarningCounter.ReadCount(), 1);
	}
	return bOk;
}
#endif

// TW-GR-06。生き残り mutant なし。倍率 2.5 が CVar 1 で反転または無視されると力 (0,0,-2.5) と異なる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecNodeGravityScaleDisabledTest,
	"KawaiiPhysics.WindSpec.Node.GravityScaleDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecNodeGravityScaleDisabledTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	FVector PreviousForce = FVector::ZeroVector;
	for (const TCHAR* CVarValue : {TEXT("0"), TEXT("1")})
	{
		KawaiiPhysicsNodeFixture::FOptions NodeOptions;
		NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
		NodeOptions.ActorKind = KawaiiPhysicsNodeFixture::EActorKind::Character;
		NodeOptions.SimulationSpace = EKawaiiPhysicsSimulationSpace::WorldSpace;
		NodeOptions.ConsoleVariables.Add({TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), CVarValue});
		NodeOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode)
		{
			AnimNode.RootBone.BoneName = TEXT("root");
			auto& AuthoredGravity = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Gravity>(AnimNode);
			AuthoredGravity.bUseCharacterGravityScale = false;
			AuthoredGravity.RandomForceScaleRange = FFloatInterval(2.5f, 2.5f);
		};
		KawaiiPhysicsNodeFixture::FFixture NodeFixture(NodeOptions);
		NodeFixture.EvaluateFrame();
		bOk &= TestTrue(TEXT("対照: ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
		const FVector AppliedForce = FKawaiiPhysicsTestAccessor::GetExternalForceVector(
			NodeFixture.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce_Gravity>());
		// 方向 -Z、既定の大きさ 1、倍率 2.5。float の乗算丸めを 1e-4 許す。
		bOk &= TestTrue(TEXT("倍率は符号 CVar と無関係"), AppliedForce.Equals(FVector(0.0f, 0.0f, -2.5f), 1e-4f));
		if (FCString::Strcmp(CVarValue, TEXT("1")) == 0)
		{
			bOk &= TestTrue(TEXT("CVar の二条件は一致"), AppliedForce.Equals(PreviousForce, 1e-4f));
		}
		PreviousForce = AppliedForce;
	}
	return bOk;
}

} // namespace KawaiiPhysicsWindSpecNodeTestPrivate

#endif
