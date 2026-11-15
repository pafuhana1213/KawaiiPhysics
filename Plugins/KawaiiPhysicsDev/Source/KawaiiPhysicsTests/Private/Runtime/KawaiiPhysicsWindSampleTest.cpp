// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Wind.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Components/WindDirectionalSourceComponent.h"
#include "Engine/WindDirectionalSource.h"
#include "Engine/World.h"
#include "SceneInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

namespace KawaiiPhysicsWindSampleTestPrivate
{
constexpr float GWindSampleTestTol = 1.0e-3f;
constexpr float GWindSourceRadius = 1000.0f;
constexpr float GWindSourceSpeed = 100.0f;
// 点風源の位置（ワールド）と、ボーンを並べるコンポーネントの位置
const FVector GWindSourceLocation(0.0, 0.0, -50.0);
const FVector GComponentLocation(300.0, 0.0, 0.0);

struct FWindForceTestAccessor : FKawaiiPhysics_ExternalForce_Wind
{
	FWindForceTestAccessor()
	{
		bSupportsRandomForceScaleRange = false;
		RandomizedForceScale = 1.0f;
	}
};

// UE の点風源と同じ式で、位置 P での風を求める（風源は1つだけ）
void ExpectedPointWind(const FVector& Position, FVector& OutDirection, float& OutSpeed)
{
	const FVector Offset = Position - GWindSourceLocation;
	OutDirection = Offset.GetSafeNormal();
	OutSpeed = GWindSourceSpeed * FMath::Max(1.0f - static_cast<float>((Offset / GWindSourceRadius).SizeSquared()), 0.0f);
}

struct FWindSampleTestWorld
{
	UWorld* World = nullptr;

	FWindSampleTestWorld()
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
			.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
			ERHIFeatureLevel::Num, &Values);
		if (AWindDirectionalSource* Source = World->SpawnActor<AWindDirectionalSource>(
			GWindSourceLocation, FRotator::ZeroRotator))
		{
			UWindDirectionalSourceComponent* Wind = Source->GetComponent();
			Wind->SetWindType(EWindSourceType::Point);
			Wind->SetRadius(GWindSourceRadius);
			Wind->SetSpeed(GWindSourceSpeed);
			Wind->SetStrength(1.0f);
			Wind->SetMinimumGustAmount(0.0f);
			Wind->SetMaximumGustAmount(0.0f);
			// テスト用ワールドでは BeginPlay が走らず自動で有効化されないので、明示的に有効化して Scene に登録する
			// （Activate は UWindDirectionalSourceComponent では protected なので基底クラス経由で呼ぶ）
			static_cast<UActorComponent*>(Wind)->Activate(true);
		}
	}

	~FWindSampleTestWorld() { World->DestroyWorld(false); }

	// -nullrhi などでは Scene が描画しない実装（風は常にゼロ）になるため、風源を使うテストは実行できない
	bool HasRenderScene(FAutomationTestBase& Test) const
	{
		if (World->Scene && World->Scene->GetRenderScene())
		{
			return true;
		}
		Test.AddWarning(TEXT("Skipped: the world has no render scene (for example -nullrhi), so wind sources do not register."));
		return false;
	}
};

// 縦 3 本のチェーンを、コンポーネント空間で平行移動したコンポーネントに置く
void SetUpWindSampleChain(FKawaiiPhysicsTestAccessor& A)
{
	A.BuildVerticalChain(3, 50.0f);
	A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	A.SetWorldSpaceTransformForTest(FTransform(GComponentLocation));
	A.SetTimeState(1.0f / 60.0f, 1.0f / 60.0f);
}

FVector BoneWorldPosition(FKawaiiPhysicsTestAccessor& A, int32 BoneIndex)
{
	return A.Bone(BoneIndex).PoseLocation + GComponentLocation;
}

// 既定（NodeCenter）では、ボーン群の中心で1回だけ問い合わせて全ボーンで使う。
// 問い合わせ位置にはコンポーネントの平行移動が入る（Wind 外力が位置をベクトルとして変換していた PAF-366 の確認）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindSampleNodeCenterTest,
                                 "KawaiiPhysics.Wind.Sample.NodeCenter",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindSampleNodeCenterTest::RunTest(const FString& Parameters)
{
	FWindSampleTestWorld TestWorld;
	if (!TestWorld.HasRenderScene(*this))
	{
		return true;
	}

	FKawaiiPhysicsTestAccessor A;
	SetUpWindSampleChain(A);
	A.Node.bEnableWind = true;
	A.Node.WindSampleMode = EKawaiiPhysicsWindSampleMode::NodeCenter;

	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	A.CallStoreWindSamplePositions(Output);
	A.CallSampleWindOnGameThread(TestWorld.World);

	bool bOk = TestTrue(TEXT("center sample is taken"), A.HasWindCenterSample());
	bOk &= TestEqual(TEXT("no per-bone samples in NodeCenter mode"), A.GetWindBoneSampleCount(), 0);

	// 中心はボーン群のバウンディングボックスの中心（ワールド）
	FVector ExpectedDirection;
	float ExpectedSpeed;
	ExpectedPointWind((BoneWorldPosition(A, 0) + BoneWorldPosition(A, 2)) * 0.5, ExpectedDirection, ExpectedSpeed);
	for (int32 BoneIndex = 0; BoneIndex < 3; ++BoneIndex)
	{
		FVector Direction;
		float Speed;
		bOk &= TestTrue(TEXT("sample is available"), A.Node.GetWindSample(BoneIndex, Direction, Speed));
		bOk &= TestTrue(FString::Printf(TEXT("bone %d uses the center direction %s"), BoneIndex, *Direction.ToString()),
			Direction.Equals(ExpectedDirection, GWindSampleTestTol));
		bOk &= TestTrue(FString::Printf(TEXT("bone %d uses the center speed %.3f"), BoneIndex, Speed),
			FMath::IsNearlyEqual(Speed, ExpectedSpeed, GWindSampleTestTol));
	}

	// 従来の Wind も同じ向き（WindScale・突風倍率は大きさだけを変える）。風源の範囲内なので 0 にはならない
	bOk &= TestTrue(FString::Printf(TEXT("the center is inside the wind source (speed %.3f)"), ExpectedSpeed),
		ExpectedSpeed > 1.0f);
	const FVector Velocity = A.CallGetWindVelocity(Output, 2);
	bOk &= TestFalse(FString::Printf(TEXT("legacy wind is not zero %s"), *Velocity.ToString()), Velocity.IsNearlyZero());
	bOk &= TestTrue(FString::Printf(TEXT("legacy wind follows the sample %s"), *Velocity.ToString()),
		Velocity.GetSafeNormal().Equals(ExpectedDirection, GWindSampleTestTol));
	return bOk;
}

// PerBone ではボーンごとに問い合わせ、従来の Wind と Wind 外力の両方がその値を使う
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindSamplePerBoneTest,
                                 "KawaiiPhysics.Wind.Sample.PerBone",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindSamplePerBoneTest::RunTest(const FString& Parameters)
{
	FWindSampleTestWorld TestWorld;
	if (!TestWorld.HasRenderScene(*this))
	{
		return true;
	}

	FKawaiiPhysicsTestAccessor A;
	SetUpWindSampleChain(A);
	A.Node.WindSampleMode = EKawaiiPhysicsWindSampleMode::PerBone;
	// 従来の Wind は無効。有効な Wind 外力だけでも問い合わせる
	A.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Wind>());

	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	A.CallStoreWindSamplePositions(Output);
	A.CallSampleWindOnGameThread(TestWorld.World);

	bool bOk = TestEqual(TEXT("one sample per bone"), A.GetWindBoneSampleCount(), 3);

	FWindForceTestAccessor WindForce;
	WindForce.PreApply(A.Node, Output);
	for (const int32 BoneIndex : {0, 2})
	{
		FVector ExpectedDirection;
		float ExpectedSpeed;
		ExpectedPointWind(BoneWorldPosition(A, BoneIndex), ExpectedDirection, ExpectedSpeed);

		FVector Direction;
		float Speed;
		A.Node.GetWindSample(BoneIndex, Direction, Speed);
		bOk &= TestTrue(FString::Printf(TEXT("bone %d direction %s"), BoneIndex, *Direction.ToString()),
			Direction.Equals(ExpectedDirection, GWindSampleTestTol));
		bOk &= TestTrue(FString::Printf(TEXT("bone %d speed %.3f"), BoneIndex, Speed),
			FMath::IsNearlyEqual(Speed, ExpectedSpeed, GWindSampleTestTol));

		// Wind 外力は、そのボーンの風で 1 ステップ分だけ動かす
		const FVector Before = A.Bone(BoneIndex).Location;
		WindForce.Apply(A.Bone(BoneIndex), A.Node, Output);
		const FVector Displacement = A.Bone(BoneIndex).Location - Before;
		bOk &= TestTrue(FString::Printf(TEXT("wind force on bone %d moves %s"), BoneIndex, *Displacement.ToString()),
			Displacement.Equals(ExpectedDirection * ExpectedSpeed * A.Node.GetStepDeltaTime(), GWindSampleTestTol));
	}

	// 問い合わせたときとボーン数が変わったら、ボーンごとの値は使わず中心の値に戻る
	A.BuildVerticalChain(4, 50.0f);
	FVector Direction;
	float Speed;
	bOk &= TestTrue(TEXT("falls back to the center sample"), A.Node.GetWindSample(3, Direction, Speed));
	FVector CenterDirection;
	float CenterSpeed;
	ExpectedPointWind(GComponentLocation + FVector(0.0, 0.0, -50.0), CenterDirection, CenterSpeed);
	bOk &= TestTrue(FString::Printf(TEXT("fallback direction %s"), *Direction.ToString()),
		Direction.Equals(CenterDirection, GWindSampleTestTol));
	return bOk;
}

// ノード中心の風は、風を使わないノードでも取得する（一時外力の最初のフレームに備える）。
// ボーンごとの問い合わせ（PerBone）は、風を使うときだけ行う
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindSampleSkippedTest,
                                 "KawaiiPhysics.Wind.Sample.PerBoneSkippedWhenUnused",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindSampleSkippedTest::RunTest(const FString& Parameters)
{
	FWindSampleTestWorld TestWorld;
	FKawaiiPhysicsTestAccessor A;
	SetUpWindSampleChain(A);
	A.Node.bEnableWind = false;
	A.Node.WindSampleMode = EKawaiiPhysicsWindSampleMode::PerBone;

	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	A.CallStoreWindSamplePositions(Output);
	A.CallSampleWindOnGameThread(TestWorld.World);

	bool bOk = TestTrue(TEXT("the center wind is sampled even without wind users"), A.HasWindCenterSample());
	bOk &= TestEqual(TEXT("no per-bone queries without wind users"), A.GetWindBoneSampleCount(), 0);

	// 一時外力の Wind のように設定からは分からない利用でも、評価で読まれたら次のフレームからボーンごとに問い合わせる
	FVector Direction;
	float Speed;
	A.Node.GetWindSample(0, Direction, Speed);
	A.CallStoreWindSamplePositions(Output);
	A.CallSampleWindOnGameThread(TestWorld.World);
	bOk &= TestEqual(TEXT("a wind read during evaluation enables per-bone queries"), A.GetWindBoneSampleCount(), A.Num());
	return bOk;
}

struct FWindEvalProxy : FAnimInstanceProxy
{
	using FAnimInstanceProxy::FAnimInstanceProxy;
	using FAnimInstanceProxy::PreUpdate;
};

// 点風源のあるワールドで、本番と同じ PreUpdate → UpdateInternal → EvaluateSkeletalControl_AnyThread を回す
struct FWindEvalFixture
{
	FWindSampleTestWorld TestWorld;
	TStrongObjectPtr<USkeleton> Skeleton{NewObject<USkeleton>()};
	UAnimInstance* Anim = nullptr;
	USkeletalMeshComponent* Component = nullptr;
	TUniquePtr<FWindEvalProxy> Proxy;
	FKawaiiPhysicsTestAccessor A;
	TArray<FBoneTransform> OutTransforms;
	float FrameDt = 1.0f / 60.0f;

	FWindEvalFixture()
	{
		TArray<FBoneIndexType> RequiredIndices{0, 1, 2, 3};
		{
			FReferenceSkeletonModifier Modifier(Skeleton.Get());
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			for (int32 Link = 0; Link < 3; ++Link)
			{
				const FName Name(*FString::Printf(TEXT("chain_%d"), Link));
				Modifier.Add(FMeshBoneInfo(Name, Name.ToString(), Link), FTransform(FVector(0.0, 0.0, -20.0)));
			}
		}
		AActor* Actor = TestWorld.World->SpawnActor<AActor>(GComponentLocation, FRotator::ZeroRotator);
		Component = NewObject<USkeletalMeshComponent>(Actor);
		Actor->SetRootComponent(Component);
		Component->RegisterComponent();
		Component->SetWorldLocation(GComponentLocation);
		Anim = NewObject<UAnimInstance>(Component);
		Proxy = MakeUnique<FWindEvalProxy>(Anim);
		Proxy->PreUpdate(Anim, FrameDt);
		Proxy->GetRequiredBones().InitializeTo(RequiredIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);

		FAnimNode_KawaiiPhysics& Node = A.Node;
		Node.RootBone.BoneName = TEXT("chain_0");
		Node.DummyBoneLength = 0.0f;
		Node.WarmUpFrames = 0;
		Node.bAllowWorldCollision = false;
		Node.bUseSimpleWorldCollision = false;
		Node.Gravity = FVector::ZeroVector;
		Node.OnInitializeAnimInstance(Proxy.Get(), Anim);
		Node.Initialize_AnyThread(FAnimationInitializeContext(Proxy.Get()));
		Node.CacheBones_AnyThread(FAnimationCacheBonesContext(Proxy.Get()));
	}

	~FWindEvalFixture() { Proxy.Reset(); }

	void PreUpdate() { A.Node.PreUpdate(Anim); }

	void Evaluate()
	{
		FMemMark MemMark(FMemStack::Get());
		FComponentSpacePoseContext Context(Proxy.Get());
		Context.Pose.InitPose(&Proxy->GetRequiredBones());
		A.Node.UpdateInternal(FAnimationUpdateContext(Proxy.Get(), FrameDt));
		OutTransforms.Reset();
		A.Node.EvaluateSkeletalControl_AnyThread(Context, OutTransforms);
	}

	// 先端のボーンが、ポーズからどれだけ動いたか
	float TipDeviation() const
	{
		const FKawaiiPhysicsModifyBone& Tip = A.Node.ModifyBones.Last();
		return static_cast<float>((Tip.Location - Tip.PoseLocation).Size());
	}
};

// 寿命 1 フレームの一時 Wind 外力も、風源の風を受ける（要求のタイミングはアクターの Tick 相当と AnimBP 相当の両方）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindSampleShortTransientTest,
                                 "KawaiiPhysics.Wind.Sample.ShortLivedTransientWind",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindSampleShortTransientTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const bool bRequestBeforePreUpdate : {true, false})
	{
		FWindEvalFixture Fixture;
		if (!Fixture.TestWorld.HasRenderScene(*this))
		{
			return true;
		}
		// 風を使わない状態で数フレーム回す
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Fixture.PreUpdate();
			Fixture.Evaluate();
		}

		auto Request = [&Fixture]()
		{
			Fixture.A.Node.RequestTransientExternalForce(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Wind>(),
			                                             Fixture.FrameDt);
		};
		float MaxDeviation = 0.0f;
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			if (Frame == 0 && bRequestBeforePreUpdate)
			{
				Request();
			}
			Fixture.PreUpdate();
			if (Frame == 0 && !bRequestBeforePreUpdate)
			{
				Request();
			}
			Fixture.Evaluate();
			MaxDeviation = FMath::Max(MaxDeviation, Fixture.TipDeviation());
		}
		bOk &= TestTrue(FString::Printf(TEXT("short-lived transient wind moves the chain (requested %s PreUpdate): %.4f cm"),
			bRequestBeforePreUpdate ? TEXT("before") : TEXT("after"), MaxDeviation), MaxDeviation > 0.01f);
	}
	return bOk;
}

// コンポーネントが動いたら、次の PreUpdate は動いた後の位置で風を問い合わせる（前回の評価時のワールド位置ではない）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindSampleMovedComponentTest,
                                 "KawaiiPhysics.Wind.Sample.FollowsMovedComponent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindSampleMovedComponentTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const EKawaiiPhysicsWindSampleMode Mode : {EKawaiiPhysicsWindSampleMode::NodeCenter, EKawaiiPhysicsWindSampleMode::PerBone})
	{
		FWindEvalFixture Fixture;
		if (!Fixture.TestWorld.HasRenderScene(*this))
		{
			return true;
		}
		Fixture.A.Node.bEnableWind = true;
		Fixture.A.Node.WindSampleMode = Mode;
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Fixture.PreUpdate();
			Fixture.Evaluate();
		}

		// 点風源(0,0,-50)の反対側へ動かす。風は風源から外向きなので、向きの X の符号が反転するはず
		Fixture.Component->SetWorldLocation(FVector(-GComponentLocation.X, 0.0, 0.0));
		Fixture.PreUpdate();
		FVector Direction;
		float Speed;
		Fixture.A.Node.GetWindSample(1, Direction, Speed);
		bOk &= TestTrue(FString::Printf(TEXT("%s: wind is sampled at the moved component (%s)"),
			Mode == EKawaiiPhysicsWindSampleMode::PerBone ? TEXT("PerBone") : TEXT("NodeCenter"), *Direction.ToString()),
			Direction.X < 0.0);
	}
	return bOk;
}

} // namespace KawaiiPhysicsWindSampleTestPrivate

#endif
