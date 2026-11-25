// Copyright 2019-2026 pafuhana1213. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Wind.h"

namespace KawaiiPhysicsWindSampleTestPrivate
{
const FVector WindDirection(1.0, 0.0, 0.0);
const FVector ComponentLocation(300.0, 40.0, 70.0);

struct FWindForce : FKawaiiPhysics_ExternalForce_Wind
{
	FWindForce() { bSupportsRandomForceScaleRange = false; RandomizedForceScale = 1.0f; }
};

void SetUp(FKawaiiPhysicsTestAccessor& Accessor, EKawaiiPhysicsWindSampleMode Mode)
{
	Accessor.BuildVerticalChain(3, 50.0f);
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetWorldSpaceTransformForTest(FTransform(ComponentLocation));
	Accessor.SetTimeState(1.0f / 60.0f, 1.0f / 60.0f);
	Accessor.Node.WindSampleMode = Mode;
}

void Store(FKawaiiPhysicsTestAccessor& Accessor)
{
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	Accessor.CallStoreWindSamplePositions(Output);
}

FTransform QueryTransform()
{
	return FTransform(FRotator(0.0, 90.0, 0.0), ComponentLocation, FVector(2.0, 1.5, 0.5));
}

KawaiiPhysicsNodeFixture::FOptions Options(EKawaiiPhysicsWindSampleMode Mode)
{
	KawaiiPhysicsNodeFixture::FOptions Result;
	Result.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(4, FVector(0.0, 0.0, -20.0));
	Result.ConfigureNode = [Mode](FAnimNode_KawaiiPhysics& Node)
	{
		Node.RootBone.BoneName = TEXT("chain_1");
		Node.DummyBoneLength = 0.0f;
		Node.WarmUpFrames = 0;
		Node.bAllowWorldCollision = false;
		Node.bUseSimpleWorldCollision = false;
		Node.Gravity = FVector::ZeroVector;
		Node.WindSampleMode = Mode;
	};
	return Result;
}

void SetUniform(FKawaiiPhysicsTestAccessor& Accessor, EKawaiiPhysicsWindSampleMode Mode)
{
	Accessor.SetWindCenterSample(WindDirection, 100.0f);
	if (Mode == EKawaiiPhysicsWindSampleMode::PerBone)
	{
		TArray<FVector> Directions;
		TArray<float> Speeds;
		Directions.Init(WindDirection, Accessor.Num());
		Speeds.Init(100.0f, Accessor.Num());
		Accessor.SetWindBoneSamples(Directions, Speeds);
	}
}

// TW-WS-04, TW-WS-06: ボーン群の境界と位置変換を独立に求める。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeCenterTest, "KawaiiPhysics.WindSample.NodeCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNodeCenterTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetUp(Accessor, EKawaiiPhysicsWindSampleMode::NodeCenter);
	Accessor.Node.bEnableWind = true;
	// 初回は保存位置が無いため、問い合わせ位置はコンポーネントの平行移動成分。
	bool bOk = TestEqual(TEXT("first center query uses component location"),
		Accessor.ComputeWindSampleCenterPosition(QueryTransform()), ComponentLocation);
	Store(Accessor);
	FBox Bounds(ForceInit);
	for (int32 BoneIndex = 0; BoneIndex < 3; ++BoneIndex) { Bounds += Accessor.Bone(BoneIndex).PoseLocation; }
	const FTransform Transform = QueryTransform();
	bOk &= TestTrue(TEXT("center world position"), Accessor.ComputeWindSampleCenterPosition(Transform).Equals(
		Transform.TransformPosition(Bounds.GetCenter()), 0.001));
	bOk &= TestFalse(TEXT("not vector transform"), Accessor.ComputeWindSampleCenterPosition(Transform).Equals(
		Transform.TransformVector(Bounds.GetCenter()), 0.001));
	bOk &= TestFalse(TEXT("no per-bone query"), Accessor.ShouldSampleWindPerBone());
	bOk &= TestEqual(TEXT("missing component uses evaluated transform"),
		Accessor.ResolveWindSampleComponentTransform().GetLocation(), ComponentLocation);
	Accessor.SetWindCenterSample(WindDirection, 80.0f);
	for (int32 BoneIndex = 0; BoneIndex < 3; ++BoneIndex)
	{
		FVector Direction;
		float Speed = 0.0f;
		bOk &= TestTrue(TEXT("sample exists"), Accessor.Node.GetWindSample(BoneIndex, Direction, Speed));
		bOk &= TestEqual(TEXT("same direction"), Direction, WindDirection);
		bOk &= TestEqual(TEXT("same speed"), Speed, 80.0f);
	}
	return bOk;
}

// TW-WS-05, TW-WS-11: 各ボーンの値を渡し、外力と従来 Wind の使用を調べる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerBoneTest, "KawaiiPhysics.WindSample.PerBone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerBoneTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetUp(Accessor, EKawaiiPhysicsWindSampleMode::PerBone);
	Accessor.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Wind>());
	Store(Accessor);
	bool bOk = TestTrue(TEXT("per-bone query enabled"), Accessor.ShouldSampleWindPerBone());
	const FTransform Transform = QueryTransform();
	for (int32 BoneIndex = 0; BoneIndex < 3; ++BoneIndex)
	{
		bOk &= TestTrue(TEXT("bone world position"), Accessor.ComputeWindSampleBonePosition(Transform, BoneIndex).Equals(
			Transform.TransformPosition(Accessor.Bone(BoneIndex).PoseLocation), 0.001));
	}
	Accessor.SetWindCenterSample(WindDirection, 5.0f);
	Accessor.SetWindBoneSamples({FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1)}, {20.0f, 40.0f, 60.0f});
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	FWindForce Force;
	Force.PreApply(Accessor.Node, Output);
	for (int32 BoneIndex = 0; BoneIndex < 3; ++BoneIndex)
	{
		const FVector ExpectedDirection = BoneIndex == 0 ? FVector(1, 0, 0) :
			BoneIndex == 1 ? FVector(0, 1, 0) : FVector(0, 0, 1);
		const float ExpectedSpeed = 20.0f * (BoneIndex + 1);
		FVector Direction;
		float Speed = 0.0f;
		bOk &= TestTrue(TEXT("bone sample exists"), Accessor.Node.GetWindSample(BoneIndex, Direction, Speed));
		bOk &= TestEqual(TEXT("bone direction"), Direction, ExpectedDirection);
		bOk &= TestEqual(TEXT("bone speed"), Speed, ExpectedSpeed);
		const FVector Before = Accessor.Bone(BoneIndex).Location;
		Force.Apply(Accessor.Bone(BoneIndex), Accessor.Node, Output);
		bOk &= TestTrue(TEXT("force displacement"), (Accessor.Bone(BoneIndex).Location - Before).Equals(
			ExpectedDirection * ExpectedSpeed * Accessor.Node.GetStepDeltaTime(), 0.001));
		Accessor.Node.bEnableWind = true;
		bOk &= TestTrue(TEXT("legacy wind direction"), Accessor.CallGetWindVelocity(Output, BoneIndex).
			GetSafeNormal().Equals(ExpectedDirection, 0.001));
	}
	Accessor.BuildVerticalChain(4, 50.0f);
	FVector Direction;
	float Speed = 0.0f;
	bOk &= TestTrue(TEXT("changed count falls back to center"), Accessor.Node.GetWindSample(3, Direction, Speed));
	bOk &= TestEqual(TEXT("fallback direction"), Direction, WindDirection);
	bOk &= TestEqual(TEXT("fallback speed"), Speed, 5.0f);
	// 設定でも直前の評価でも使わないときだけ、保存済み位置があっても問い合わせない。
	Accessor.Node.ExternalForces.Reset();
	Accessor.Node.bEnableWind = false;
	Accessor.SetWindBoneSamples({}, {});
	bOk &= TestFalse(TEXT("unused mode skips per-bone query"), Accessor.ShouldSampleWindPerBone());
	return bOk;
}

// 未使用時はボーン別問い合わせを省き、直前の評価で読んだら有効にする。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkippedTest, "KawaiiPhysics.WindSample.PerBoneSkippedWhenUnused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkippedTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetUp(Accessor, EKawaiiPhysicsWindSampleMode::PerBone);
	Store(Accessor);
	bool bOk = TestFalse(TEXT("unused wind skips bones"), Accessor.ShouldSampleWindPerBone());
	Accessor.SetWindCenterSample(WindDirection, 30.0f);
	FVector Direction;
	float Speed = 0.0f;
	bOk &= TestTrue(TEXT("center available without user"), Accessor.Node.GetWindSample(0, Direction, Speed));
	bOk &= TestEqual(TEXT("center speed"), Speed, 30.0f);
	Store(Accessor);
	bOk &= TestTrue(TEXT("prior read enables bones"), Accessor.ShouldSampleWindPerBone());
	return bOk;
}

// TW-WS-02: 同一の方向性の風を渡した両モードのボーン位置をビット比較する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectionalModesTest, "KawaiiPhysics.WindSample.DirectionalModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDirectionalModesTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture Center(Options(EKawaiiPhysicsWindSampleMode::NodeCenter));
	KawaiiPhysicsNodeFixture::FFixture PerBone(Options(EKawaiiPhysicsWindSampleMode::PerBone));
	Center.Node.bEnableWind = true;
	PerBone.Node.bEnableWind = true;
	bool bOk = true;
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		SetUniform(Center.Accessor, EKawaiiPhysicsWindSampleMode::NodeCenter);
		SetUniform(PerBone.Accessor, EKawaiiPhysicsWindSampleMode::PerBone);
		// 従来の Wind は突風と向きの揺らぎをグローバルの乱数からノードごとに引くので、両ノードに同じ乱数を引かせる
		FMath::RandInit(1000 + Frame);
		Center.EvaluateFrame();
		FMath::RandInit(1000 + Frame);
		PerBone.EvaluateFrame();
		bOk &= TestEqual(TEXT("same bone count"), Center.Accessor.Num(), PerBone.Accessor.Num());
		for (int32 BoneIndex = 0; BoneIndex < Center.Accessor.Num(); ++BoneIndex)
		{
			bOk &= TestTrue(TEXT("bit-identical bone position"),
				Center.Accessor.Bone(BoneIndex).Location == PerBone.Accessor.Bone(BoneIndex).Location);
		}
	}
	return bOk;
}

// 一時 Wind 外力は PreUpdate の前後どちらで要求しても、中心の風があれば最初の評価で使う。
// 風を使っていないノードでも PreUpdate で毎フレーム中心の風を取っておくこと自体は Scene に問い合わせるので PIE で確かめる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShortTransientTest, "KawaiiPhysics.WindSample.ShortLivedTransientWind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShortTransientTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const bool bBeforePreUpdate : {true, false})
	{
		KawaiiPhysicsNodeFixture::FFixture Fixture(Options(EKawaiiPhysicsWindSampleMode::NodeCenter));
		for (int32 Frame = 0; Frame < 3; ++Frame) { Fixture.EvaluateFrame(); }
		if (bBeforePreUpdate)
		{
			Fixture.Node.RequestTransientExternalForce(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Wind>(), Fixture.FrameDt);
		}
		Fixture.Node.PreUpdate(Fixture.Anim);
		if (!bBeforePreUpdate)
		{
			Fixture.Node.RequestTransientExternalForce(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Wind>(), Fixture.FrameDt);
		}
		Fixture.Accessor.SetWindCenterSample(WindDirection, 100.0f);
		Fixture.EvaluateFrame();
		const auto& Tip = Fixture.Accessor.Bone(Fixture.Accessor.Num() - 1);
		bOk &= TestTrue(TEXT("short-lived wind moves the chain"), (Tip.Location - Tip.PoseLocation).Size() > 0.01);
	}
	return bOk;
}

// TW-WS-07: 保存後に動いたコンポーネントの現在位置を使う。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovedComponentTest, "KawaiiPhysics.WindSample.FollowsMovedComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMovedComponentTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const EKawaiiPhysicsWindSampleMode Mode : {EKawaiiPhysicsWindSampleMode::NodeCenter, EKawaiiPhysicsWindSampleMode::PerBone})
	{
		FKawaiiPhysicsTestAccessor Accessor;
		SetUp(Accessor, Mode);
		Accessor.Node.bEnableWind = true;
		Store(Accessor);
		KawaiiPhysicsNodeFixture::FFixture Fixture(Options(Mode));
		// 評価で位置を保存した後、原点をはさんだ反対側へ回転とスケールつきで動かす
		Fixture.Component->SetWorldTransform(
			FTransform(FRotator(0.0, -90.0, 0.0), -ComponentLocation, FVector(2.0, 1.5, 0.5)));
		const FTransform Current = Accessor.ResolveWindSampleComponentTransform(Fixture.Component);
		bOk &= TestFalse(TEXT("current transform differs from evaluated transform"),
			Current.GetLocation().Equals(Accessor.ResolveWindSampleComponentTransform().GetLocation(), 0.001));
		FBox Bounds(ForceInit);
		for (int32 BoneIndex = 0; BoneIndex < 3; ++BoneIndex) { Bounds += Accessor.Bone(BoneIndex).PoseLocation; }
		bOk &= TestTrue(TEXT("center follows component"), Accessor.ComputeWindSampleCenterPosition(Current).Equals(
			Fixture.Component->GetComponentTransform().TransformPosition(Bounds.GetCenter()), 0.001));
		if (Mode == EKawaiiPhysicsWindSampleMode::PerBone)
		{
			bOk &= TestTrue(TEXT("bone follows component"), Accessor.ComputeWindSampleBonePosition(Current, 2).Equals(
				Fixture.Component->GetComponentTransform().TransformPosition(Accessor.Bone(2).PoseLocation), 0.001));
		}
	}
	return bOk;
}
} // namespace KawaiiPhysicsWindSampleTestPrivate
#endif
