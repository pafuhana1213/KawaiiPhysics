// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsMemoryTraceRegion.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/MemStack.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	constexpr int32 GEvaluationPerfWarmup = 100;
	constexpr int32 GEvaluationPerfFrames = 2000;
	constexpr int32 GEvaluationPerfTrials = 5;
	constexpr int32 GEvaluationPerfBones = 64;
	constexpr float GEvaluationPerfDt = 1.0f / 60.0f;

	struct FNodeEvaluationProxy : FAnimInstanceProxy
	{
		using FAnimInstanceProxy::FAnimInstanceProxy;
		using FAnimInstanceProxy::PreUpdate;
	};

	// Uses only baseline public APIs. The real pose and animation proxy drive the production
	// evaluator, including limits, request consumption, simulation and output bone transforms.
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

		double Evaluate(FComponentSpacePoseContext& Context, bool bPushEveryFrame)
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
			const double Start = FPlatformTime::Seconds();
			Node.EvaluateSkeletalControl_AnyThread(Context, OutTransforms);
			return FPlatformTime::Seconds() - Start;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProductionNodeEvaluationPerfTest,
	"KawaiiPhysics.Perf.ProductionNodeEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProductionNodeEvaluationPerfTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const bool bPushEveryFrame : {false, true})
	{
		for (int32 Trial = 0; Trial < GEvaluationPerfTrials; ++Trial)
		{
			FKawaiiPhysicsMemoryTraceRegion MemoryRegion(bPushEveryFrame ? TEXT("NodeEvaluation.PushEveryFrame")
				: TEXT("NodeEvaluation.NoRequests"), Trial + 1);
			FMemMark MemMark(FMemStack::Get());
			FNodeEvaluationFixture Fixture;
			FComponentSpacePoseContext Context(Fixture.Proxy.Get());
			for (int32 Frame = 0; Frame < GEvaluationPerfWarmup; ++Frame)
			{
				Fixture.Evaluate(Context, bPushEveryFrame);
			}
			double Seconds = 0.0;
			MemoryRegion.Warmup();
			for (int32 Frame = 0; Frame < GEvaluationPerfFrames; ++Frame)
			{
				Seconds += Fixture.Evaluate(Context, bPushEveryFrame);
			}
			MemoryRegion.End();
			bOk &= TestEqual(TEXT("Production evaluation outputs all simulated bones"),
				Fixture.OutTransforms.Num(), GEvaluationPerfBones);
			bool bFinite = true;
			for (const FBoneTransform& Bone : Fixture.OutTransforms)
			{
				bFinite &= !Bone.Transform.ContainsNaN();
			}
			bOk &= TestTrue(TEXT("Production evaluation output remains finite"), bFinite);
			AddInfo(FString::Printf(TEXT("PRODUCTION_EVAL scenario=%s trial=%d frames=%d bones=%d eval_us=%.6f output_bones=%d node_bytes=%d"),
				bPushEveryFrame ? TEXT("PushEveryFrame") : TEXT("NoRequests"), Trial + 1,
				GEvaluationPerfFrames, GEvaluationPerfBones, Seconds * 1000000.0 / GEvaluationPerfFrames,
				Fixture.OutTransforms.Num(), static_cast<int32>(sizeof(FAnimNode_KawaiiPhysics))));
		}
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsProductionRequestQueuePerfTest,
	"KawaiiPhysics.Perf.ProductionRequestQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsProductionRequestQueuePerfTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (int32 Trial = 0; Trial < GEvaluationPerfTrials; ++Trial)
	{
		FKawaiiPhysicsMemoryTraceRegion MemoryRegion(TEXT("RequestQueue.PushEveryFrame"), Trial + 1);
		FAnimNode_KawaiiPhysics Node;
		FKawaiiPhysicsSettingsMultiplier Scale;
		Scale.Damping = 1.25f;
		const auto Step = [&]()
		{
			Node.RequestPushPhysicsSettingsMultiplier(Scale, 0.75f, 101);
			Node.ConsumeAndAdvancePhysicsSettingsMultipliers(GEvaluationPerfDt);
		};
		for (int32 Frame = 0; Frame < GEvaluationPerfWarmup; ++Frame)
		{
			Step();
		}
		int32 QueueCapacityGrowths = 0;
		MemoryRegion.Warmup();
		const double Start = FPlatformTime::Seconds();
		for (int32 Frame = 0; Frame < GEvaluationPerfFrames; ++Frame)
		{
			// This node has one producer on this thread. One queued push with no capacity
			// requires one array allocation; do not substitute process-wide allocator stats.
			QueueCapacityGrowths += Node.TransientForceStore.Queue->PendingSettingsMultiplierPushes.Max() == 0 ? 1 : 0;
			Step();
		}
		const double Seconds = FPlatformTime::Seconds() - Start;
		MemoryRegion.End();
		bOk &= TestEqual(TEXT("Repeated pushes update one active handle"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
		AddInfo(FString::Printf(TEXT("PRODUCTION_QUEUE trial=%d frames=%d request_consume_us=%.6f producer_array_allocations=%d producer_retained_bytes=%llu store_bytes=%d"),
			Trial + 1, GEvaluationPerfFrames, Seconds * 1000000.0 / GEvaluationPerfFrames,
			QueueCapacityGrowths, static_cast<uint64>(Node.TransientForceStore.Queue->PendingSettingsMultiplierPushes.GetAllocatedSize()),
			static_cast<int32>(sizeof(FKawaiiPhysicsTransientForceStore))));
	}
	return bOk;
}

#endif
