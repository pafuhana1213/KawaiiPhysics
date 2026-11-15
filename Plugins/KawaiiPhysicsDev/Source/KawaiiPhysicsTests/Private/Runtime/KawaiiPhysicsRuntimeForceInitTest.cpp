// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

namespace KawaiiPhysicsRuntimeForceInitTestPrivate
{
constexpr float GRuntimeForceInitDt = 1.0f / 60.0f;

// Gravity 外力の、PreApply で計算した力を読むためだけの型（メンバーは追加しない）
struct FGravityForceView : FKawaiiPhysics_ExternalForce_Gravity
{
	using FKawaiiPhysics_ExternalForce_Gravity::Force;
};

FVector ReadGravityForce(FInstancedStruct& Item)
{
	return static_cast<FGravityForceView&>(Item.GetMutable<FKawaiiPhysics_ExternalForce_Gravity>()).Force;
}

struct FRuntimeForceInitProxy : FAnimInstanceProxy
{
	using FAnimInstanceProxy::FAnimInstanceProxy;
	using FAnimInstanceProxy::PreUpdate;
};

// Character が持つメッシュで、本番と同じ経路（UpdateInternal → EvaluateSkeletalControl_AnyThread）で評価する
struct FRuntimeForceInitFixture
{
	TStrongObjectPtr<USkeleton> Skeleton{NewObject<USkeleton>()};
	UWorld* World = nullptr;
	ACharacter* Character = nullptr;
	UAnimInstance* Anim = nullptr;
	TUniquePtr<FRuntimeForceInitProxy> Proxy;
	FKawaiiPhysicsTestAccessor A;
	TArray<FBoneTransform> OutTransforms;

	FRuntimeForceInitFixture()
	{
		TArray<FBoneIndexType> RequiredIndices;
		{
			FReferenceSkeletonModifier Modifier(Skeleton.Get());
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			for (int32 Link = 0; Link < 3; ++Link)
			{
				const FName Name(*FString::Printf(TEXT("chain_%d"), Link));
				Modifier.Add(FMeshBoneInfo(Name, Name.ToString(), Link), FTransform(FVector(0.0, 0.0, -10.0)));
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				RequiredIndices.Add(static_cast<FBoneIndexType>(Index));
			}
		}

		const UWorld::InitializationValues Values = UWorld::InitializationValues()
			.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
			.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
		World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
		Character = World->SpawnActor<ACharacter>();
		Anim = NewObject<UAnimInstance>(Character->GetMesh());
		Proxy = MakeUnique<FRuntimeForceInitProxy>(Anim);
		Proxy->PreUpdate(Anim, GRuntimeForceInitDt);
		Proxy->GetRequiredBones().InitializeTo(RequiredIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);

		FAnimNode_KawaiiPhysics& Node = A.Node;
		Node.RootBone.BoneName = TEXT("chain_0");
		Node.DummyBoneLength = 0.0f;
		Node.WarmUpFrames = 0;
		Node.bAllowWorldCollision = false;
		Node.bUseSimpleWorldCollision = false;

		Node.OnInitializeAnimInstance(Proxy.Get(), Anim);
		Node.Initialize_AnyThread(FAnimationInitializeContext(Proxy.Get()));
		Node.CacheBones_AnyThread(FAnimationCacheBonesContext(Proxy.Get()));
		Node.PreUpdate(Anim);
	}

	~FRuntimeForceInitFixture()
	{
		Proxy.Reset();
		World->DestroyWorld(false);
	}

	void Evaluate()
	{
		// ポーズのメモリは FMemStack から確保される
		FMemMark MemMark(FMemStack::Get());
		FComponentSpacePoseContext Context(Proxy.Get());
		Context.Pose.InitPose(&Proxy->GetRequiredBones());
		A.Node.UpdateInternal(FAnimationUpdateContext(Proxy.Get(), GRuntimeForceInitDt));
		OutTransforms.Reset();
		A.Node.EvaluateSkeletalControl_AnyThread(Context, OutTransforms);
	}

	float CharacterGravityZ() const { return Character->GetCharacterMovement()->GetGravityZ(); }
};

FInstancedStruct MakeCharacterGravityForce()
{
	FInstancedStruct Item = FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Gravity>();
	Item.GetMutable<FKawaiiPhysics_ExternalForce_Gravity>().bUseCharacterGravityScale = true;
	return Item;
}

// ノードの初期化より後に追加した外力も、最初に使う前に Initialize される（Gravity が Character を取得する）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeForceInitAddedForceTest,
                                 "KawaiiPhysics.ExternalForce.RuntimeInit.AddedForce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeForceInitAddedForceTest::RunTest(const FString& Parameters)
{
	FRuntimeForceInitFixture Fixture;
	Fixture.Evaluate();

	// AddExternalForce と同じく、初期化済みのノードへ後から追加する
	Fixture.A.Node.ExternalForces.Add(MakeCharacterGravityForce());
	bool bOk = TestFalse(TEXT("added force starts uninitialized"),
		Fixture.A.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce>().IsRuntimeInitialized());

	Fixture.Evaluate();
	bOk &= TestTrue(TEXT("added force is initialized before use"),
		Fixture.A.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce>().IsRuntimeInitialized());

	// Character を取得できていれば、力の大きさは Character の重力（約 980）。取得できていなければ 1
	const FVector Force = ReadGravityForce(Fixture.A.Node.ExternalForces[0]);
	bOk &= TestTrue(FString::Printf(TEXT("gravity uses the character (|force| %.1f, character gravity %.1f)"),
		Force.Size(), FMath::Abs(Fixture.CharacterGravityZ())),
		FMath::IsNearlyEqual(Force.Size(), FMath::Abs(Fixture.CharacterGravityZ()), 1.0));
	return bOk;
}

// 一時外力も、取り込んだ後の最初の使用前に Initialize される
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeForceInitTransientForceTest,
                                 "KawaiiPhysics.ExternalForce.RuntimeInit.TransientForce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeForceInitTransientForceTest::RunTest(const FString& Parameters)
{
	FRuntimeForceInitFixture Fixture;
	Fixture.Evaluate();

	// 別のノードで初期化済みの外力を渡されても、このノードで初期化し直す
	FInstancedStruct Item = MakeCharacterGravityForce();
	Item.GetMutable<FKawaiiPhysics_ExternalForce>().MarkRuntimeInitialized();
	Fixture.A.Node.RequestTransientExternalForce(MoveTemp(Item), 10.0f);
	Fixture.Evaluate();

	bool bOk = TestEqual(TEXT("transient force is consumed"), Fixture.A.GetTransientForceCount(), 1);
	if (Fixture.A.GetTransientForceCount() != 1)
	{
		return false;
	}
	bOk &= TestTrue(TEXT("transient force is initialized before use"),
		Fixture.A.GetTransientForce(0).Get<FKawaiiPhysics_ExternalForce>().IsRuntimeInitialized());
	const FVector Force = ReadGravityForce(Fixture.A.GetTransientForce(0));
	bOk &= TestTrue(FString::Printf(TEXT("transient gravity uses the character (|force| %.1f)"), Force.Size()),
		FMath::IsNearlyEqual(Force.Size(), FMath::Abs(Fixture.CharacterGravityZ()), 1.0));
	return bOk;
}

// 実行時に追加した ProceduralWind へ、最初の評価より前に送ったパラメータ変更が、使う前の初期化で消えない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeForceInitWindParamsTest,
                                 "KawaiiPhysics.ExternalForce.RuntimeInit.ProceduralWindParamsBeforeFirstUse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeForceInitWindParamsTest::RunTest(const FString& Parameters)
{
	FRuntimeForceInitFixture Fixture;
	Fixture.Evaluate();

	Fixture.A.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>());
	FKawaiiPhysics_ExternalForce_ProceduralWind& Wind =
		Fixture.A.Node.ExternalForces[0].GetMutable<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	FKawaiiProceduralWindDynamicParams Params;
	Params.bOverrideConstantForce = true;
	Params.ConstantForce = Wind.ConstantForce + 7.0f;
	const float Expected = Params.ConstantForce;
	Wind.RequestDynamicParams(Params);

	Fixture.Evaluate();
	const FKawaiiPhysics_ExternalForce_ProceduralWind& Used =
		Fixture.A.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	bool bOk = TestTrue(TEXT("wind is initialized before use"), Used.IsRuntimeInitialized());
	bOk &= TestTrue(FString::Printf(TEXT("the parameter change survives initialization (%.2f)"), Used.ConstantForce),
		FMath::IsNearlyEqual(Used.ConstantForce, Expected));
	return bOk;
}

// ノードが作る一時の突風が、使う前の初期化を通っても予約したまま発動する（本番の評価経路）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeForceInitTransientGustTest,
                                 "KawaiiPhysics.ExternalForce.RuntimeInit.TransientGust",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeForceInitTransientGustTest::RunTest(const FString& Parameters)
{
	FRuntimeForceInitFixture Fixture;
	Fixture.Evaluate();

	Fixture.A.Node.RequestTransientGust(1.0f, 0.2f, 0.2f, FVector(1.0, 0.0, 0.0));
	Fixture.Evaluate();

	bool bOk = TestEqual(TEXT("gust is consumed"), Fixture.A.GetTransientForceCount(), 1);
	if (Fixture.A.GetTransientForceCount() != 1)
	{
		return false;
	}
	const FKawaiiPhysics_ExternalForce_ProceduralWind& Gust =
		Fixture.A.GetTransientForce(0).Get<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	bOk &= TestTrue(TEXT("gust force is initialized before use"), Gust.IsRuntimeInitialized());
	bOk &= TestTrue(TEXT("the requested gust is active"),
		Gust.RuntimeState.IsValid() && Gust.RuntimeState->ActiveGust.bIsActive);
	return bOk;
}

// 突風を予約した ProceduralWind を一時外力として渡しても、使う前の初期化で予約が消えない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRuntimeForceInitRequestedGustTest,
                                 "KawaiiPhysics.ExternalForce.RuntimeInit.GustRequestedBeforeTransient",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRuntimeForceInitRequestedGustTest::RunTest(const FString& Parameters)
{
	FRuntimeForceInitFixture Fixture;
	Fixture.Evaluate();

	FInstancedStruct Item = FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	Item.GetMutable<FKawaiiPhysics_ExternalForce_ProceduralWind>().RequestGust(1.0f, 0.2f, 0.2f);
	Fixture.A.Node.RequestTransientExternalForce(MoveTemp(Item), 5.0f);
	Fixture.Evaluate();

	bool bOk = TestEqual(TEXT("transient wind is consumed"), Fixture.A.GetTransientForceCount(), 1);
	if (Fixture.A.GetTransientForceCount() != 1)
	{
		return false;
	}
	const FKawaiiPhysics_ExternalForce_ProceduralWind& Wind =
		Fixture.A.GetTransientForce(0).Get<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	bOk &= TestTrue(TEXT("the gust requested before handing over is active"),
		Wind.RuntimeState.IsValid() && Wind.RuntimeState->ActiveGust.bIsActive);
	return bOk;
}

} // namespace KawaiiPhysicsRuntimeForceInitTestPrivate

#endif
