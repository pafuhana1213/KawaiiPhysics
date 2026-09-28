// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/MemStack.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	constexpr int32 GEvaluationCheckFrames = 32;
	constexpr int32 GEvaluationPerfBones = 64;
	constexpr float GEvaluationPerfDt = 1.0f / 60.0f;

	struct FNodeEvaluationProxy : FAnimInstanceProxy
	{
		using FAnimInstanceProxy::FAnimInstanceProxy;
		using FAnimInstanceProxy::PreUpdate;
	};

	// 実際のポーズとアニメーションプロキシで評価し、出力ボーンを確認する。
	struct FNodeEvaluationFixture
	{
		TStrongObjectPtr<USkeleton> Skeleton{NewObject<USkeleton>()};
		UWorld* World = nullptr;
		UAnimInstance* Anim = nullptr;
		TUniquePtr<FNodeEvaluationProxy> Proxy;
		FAnimNode_KawaiiPhysics Node;
		TArray<FBoneTransform> OutTransforms;

		FNodeEvaluationFixture()
		{
			TArray<FBoneIndexType> RequiredIndices;
			{
				FReferenceSkeletonModifier Modifier(Skeleton.Get());
				for (int32 Index = 0; Index < GEvaluationPerfBones; ++Index)
				{
					const FString Name = FString::Printf(TEXT("eval_bone_%d"), Index);
					Modifier.Add(FMeshBoneInfo(FName(*Name), Name, Index - 1),
						FTransform(FVector(Index == 0 ? 0.0 : 2.0, 0.0, Index == 0 ? 0.0 : -10.0)));
					RequiredIndices.Add(static_cast<FBoneIndexType>(Index));
				}
			}

			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &Values);
			AActor* Actor = World->SpawnActor<AActor>();
			USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Actor);
			Actor->AddInstanceComponent(Component);
			Actor->SetRootComponent(Component);
			Anim = NewObject<UAnimInstance>(Component);
			Proxy = MakeUnique<FNodeEvaluationProxy>(Anim);
			Proxy->PreUpdate(Anim, GEvaluationPerfDt);
			Proxy->GetRequiredBones().InitializeTo(RequiredIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);

			Node.RootBone.BoneName = TEXT("eval_bone_0");
			Node.DummyBoneLength = 0.0f;
			Node.WarmUpFrames = 0;
			Node.bAllowWorldCollision = false;
			Node.bUseSimpleWorldCollision = false;
			Node.Gravity = FVector(0.0, 0.0, -980.0);
			Node.SimpleExternalForce = FVector(20.0, 0.0, 0.0);
			Node.OnInitializeAnimInstance(Proxy.Get(), Anim);
			Node.Initialize_AnyThread(FAnimationInitializeContext(Proxy.Get()));
			Node.CacheBones_AnyThread(FAnimationCacheBonesContext(Proxy.Get()));
			Node.PreUpdate(Anim);
			OutTransforms.Reserve(GEvaluationPerfBones);
		}

		~FNodeEvaluationFixture()
		{
			Proxy.Reset();
			World->DestroyWorld(false);
		}

		void Evaluate(FComponentSpacePoseContext& Context, bool bPushEveryFrame)
		{
			Context.Pose.InitPose(&Proxy->GetRequiredBones());
			Node.UpdateInternal(FAnimationUpdateContext(Proxy.Get(), GEvaluationPerfDt));
			OutTransforms.Reset();
			if (bPushEveryFrame)
			{
				FKawaiiPhysicsSettingsMultiplier Scale;
				Scale.Damping = 1.25f;
				Node.RequestPushPhysicsSettingsMultiplier(Scale, 0.75f, 101);
			}
			Node.EvaluateSkeletalControl_AnyThread(Context, OutTransforms);
		}
	};
}

// リクエストの有無それぞれで、本番のノード評価が全ボーンを有限値で出力する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProductionNodeEvaluationTest,
	"KawaiiPhysics.Simulation.ProductionNodeEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProductionNodeEvaluationTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const bool bPushEveryFrame : {false, true})
	{
		FMemMark MemMark(FMemStack::Get());
		FNodeEvaluationFixture Fixture;
		FComponentSpacePoseContext Context(Fixture.Proxy.Get());
		for (int32 Frame = 0; Frame < GEvaluationCheckFrames; ++Frame)
		{
			Fixture.Evaluate(Context, bPushEveryFrame);
		}
		const TCHAR* Scenario = bPushEveryFrame ? TEXT("PushEveryFrame") : TEXT("NoRequests");
		bOk &= TestEqual(FString::Printf(TEXT("%s: production evaluation outputs all simulated bones"), Scenario),
			Fixture.OutTransforms.Num(), GEvaluationPerfBones);
		bool bFinite = true;
		for (const FBoneTransform& Bone : Fixture.OutTransforms)
		{
			bFinite &= !Bone.Transform.ContainsNaN();
		}
		bOk &= TestTrue(FString::Printf(TEXT("%s: production evaluation output remains finite"), Scenario), bFinite);
	}
	return bOk;
}

#endif
