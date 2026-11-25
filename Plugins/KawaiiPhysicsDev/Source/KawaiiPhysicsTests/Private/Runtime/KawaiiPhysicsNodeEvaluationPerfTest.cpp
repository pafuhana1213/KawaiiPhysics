// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/MemStack.h"
#include "UObject/StrongObjectPtr.h"

namespace KawaiiPhysicsNodeEvaluationPerfTestPrivate
{
	constexpr int32 GEvaluationCheckFrames = 32;
	constexpr int32 GEvaluationPerfBones = 64;
	constexpr float GEvaluationPerfDt = 1.0f / 60.0f;

	struct FNodeEvaluationFixture : KawaiiPhysicsNodeFixture::FFixture
	{
		static KawaiiPhysicsNodeFixture::FOptions MakeOptions()
		{
			KawaiiPhysicsNodeFixture::FOptions Options;
			Options.FrameDt = GEvaluationPerfDt;
			for (int32 Index = 0; Index < GEvaluationPerfBones; ++Index)
			{
				const FString Name = FString::Printf(TEXT("eval_bone_%d"), Index);
				Options.Bones.Add({FName(*Name), Index - 1,
					FTransform(FVector(Index == 0 ? 0.0 : 2.0, 0.0, Index == 0 ? 0.0 : -10.0))});
			}
			Options.ConfigureNode = [](FAnimNode_KawaiiPhysics& InNode)
			{
				InNode.RootBone.BoneName = TEXT("eval_bone_0");
				InNode.DummyBoneLength = 0.0f;
				InNode.WarmUpFrames = 0;
				InNode.bAllowWorldCollision = false;
				InNode.bUseSimpleWorldCollision = false;
				InNode.Gravity = FVector(0.0, 0.0, -980.0);
				InNode.SimpleExternalForce = FVector(20.0, 0.0, 0.0);
			};
			return Options;
		}

		FNodeEvaluationFixture() : KawaiiPhysicsNodeFixture::FFixture(MakeOptions()) {}

		void Evaluate(FComponentSpacePoseContext& Context, const bool bPushEveryFrame)
		{
			KawaiiPhysicsNodeFixture::FFixture::Evaluate(Context, [bPushEveryFrame](FAnimNode_KawaiiPhysics& InNode)
			{
				if (bPushEveryFrame)
				{
					FKawaiiPhysicsSettingsMultiplier Scale;
					Scale.Damping = 1.25f;
					InNode.RequestPushPhysicsSettingsMultiplier(Scale, 0.75f, 101);
				}
			});
		}
	};

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

} // namespace KawaiiPhysicsNodeEvaluationPerfTestPrivate

#endif
