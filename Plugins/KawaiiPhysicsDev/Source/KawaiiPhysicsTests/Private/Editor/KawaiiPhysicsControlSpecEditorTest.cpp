// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "KawaiiPhysicsAnimBlueprintFixture.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "KawaiiPhysicsSequencerMultiplierRegistry.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.h"
#include "Misc/AutomationTest.h"

namespace KawaiiPhysicsControlSpecEditorTestPrivate
{
	KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsControlHair, "KawaiiPhysics.Test.Control.Hair");
	KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsControlHairFront, "KawaiiPhysics.Test.Control.Hair.Front");
	KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsControlSkirt, "KawaiiPhysics.Test.Control.Skirt");

	KawaiiPhysicsAnimBlueprintFixture::FOptions MakeControlOptions()
	{
		KawaiiPhysicsAnimBlueprintFixture::FOptions BlueprintOptions;
		BlueprintOptions.Bones = KawaiiPhysicsNodeFixture::BuildHair(3, 3);
		BlueprintOptions.NodeCount = 3;
		BlueprintOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode, const int32 NodeIndex)
		{
			const FGameplayTag NodeTags[3] = {TAG_KawaiiPhysicsControlHair, TAG_KawaiiPhysicsControlHairFront,
				TAG_KawaiiPhysicsControlSkirt};
			PhysicsNode.KawaiiPhysicsTag = NodeTags[NodeIndex];
			PhysicsNode.RootBone.BoneName = KawaiiPhysicsNodeFixture::GetHairBoneName(NodeIndex, 0);
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.WarmUpFrames = 0;
			PhysicsNode.bAllowWorldCollision = false;
			PhysicsNode.bUseSimpleWorldCollision = false;
			PhysicsNode.Gravity = FVector(980.0, 0.0, 0.0);
		};
		return BlueprintOptions;
	}

	// SM-38・SM-37。2137: 空でない FilterTags も全ノードを返し、親タグの 2 件と完全一致の 1 件で失敗する。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecEditorTagCollectionTest,
		"KawaiiPhysics.ControlSpecEditor.TagCollection",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecEditorTagCollectionTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture BlueprintFixture(MakeControlOptions());
		if (!TestTrue(TEXT("AnimInstance が初期化された"), BlueprintFixture.IsReady()))
		{
			return false;
		}
		BlueprintFixture.StepFrame();
		TArray<FKawaiiPhysicsReference> FoundNodes;
		bool bOk = TestTrue(TEXT("空フィルタで収集できる"), UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(
			FoundNodes, BlueprintFixture.GetComponent(), FGameplayTagContainer(), false));
		bOk &= TestEqual(TEXT("空フィルタは三件"), FoundNodes.Num(), 3);
		for (const FKawaiiPhysicsReference& NodeReference : FoundNodes)
		{
			bOk &= TestTrue(TEXT("各ノードに ModifyBones がある"),
				NodeReference.GetAnimNode<FAnimNode_KawaiiPhysics>().ModifyBones.Num() > 0);
		}
		FGameplayTagContainer HairFilter;
		HairFilter.AddTag(TAG_KawaiiPhysicsControlHair);
		FoundNodes.Reset();
		UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(FoundNodes, BlueprintFixture.GetComponent(), HairFilter, false);
		bOk &= TestEqual(TEXT("親タグは子を含めて二件"), FoundNodes.Num(), 2);
		FoundNodes.Reset();
		UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(FoundNodes, BlueprintFixture.GetComponent(), HairFilter, true);
		bOk &= TestEqual(TEXT("完全一致は一件"), FoundNodes.Num(), 1);
		return bOk;
	}

	// SQ-07。2052: 六引数 Push のリースが 4 評価になり、10 評価後の DrivenAlpha が 0.6 から下がる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecEditorNoLeasePushTest,
		"KawaiiPhysics.ControlSpecEditor.PushWithoutLease",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecEditorNoLeasePushTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture BlueprintFixture(MakeControlOptions());
		if (!TestTrue(TEXT("AnimInstance が初期化された"), BlueprintFixture.IsReady()))
		{
			return false;
		}
		BlueprintFixture.StepFrame();
		const FKawaiiPhysicsTransientHandle MultiplierHandle = UKawaiiPhysicsLibrary::GenerateTransientHandle();
		FKawaiiPhysicsSettingsMultiplier Multiplier;
		Multiplier.Damping = 0.5f;
		FGameplayTagContainer HairFilter;
		HairFilter.AddTag(TAG_KawaiiPhysicsControlHair);
		bool bOk = TestEqual(TEXT("対象二ノードへ Push が届く"),
			UKawaiiPhysicsLibrary::PushPhysicsSettingsMultiplierOnComponent(
				BlueprintFixture.GetComponent(), MultiplierHandle, Multiplier, 0.6f, HairFilter, false), 2);
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			bOk &= TestTrue(TEXT("一時停止相当の評価を続ける"), BlueprintFixture.StepFrame());
		}
		TArray<FKawaiiPhysicsReference> HairNodes;
		UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(HairNodes, BlueprintFixture.GetComponent(), HairFilter, false);
		bOk &= TestEqual(TEXT("Hair の二ノードが残る"), HairNodes.Num(), 2);
		for (const FKawaiiPhysicsReference& NodeReference : HairNodes)
		{
			const FAnimNode_KawaiiPhysics& PhysicsNode = NodeReference.GetAnimNode<FAnimNode_KawaiiPhysics>();
			bOk &= TestEqual(TEXT("Push は一項目だけ"), PhysicsNode.TransientForceStore.SettingsMultiplierItems.Num(), 1);
			if (PhysicsNode.TransientForceStore.SettingsMultiplierItems.Num() == 1)
			{
				// 物理時間 10/60 秒でも無期限リースは α=0.6。float の丸めに 1e-6。
				bOk &= TestTrue(TEXT("DrivenAlpha は保持される"),
					FMath::Abs(PhysicsNode.TransientForceStore.SettingsMultiplierItems[0].DrivenAlpha - 0.6f) <= 0.000001f);
			}
		}
		return bOk;
	}

	// SQ-06・SQ-08。2049: Registry の Stop が BlendOutTime=0.5 を 0 にし、次の Evaluate で項目が即時消去される。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecEditorSectionFadeTest,
		"KawaiiPhysics.ControlSpecEditor.SectionFade",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecEditorSectionFadeTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture BlueprintFixture(MakeControlOptions());
		if (!TestTrue(TEXT("AnimInstance が初期化された"), BlueprintFixture.IsReady()))
		{
			return false;
		}
		BlueprintFixture.StepFrame();
		FGameplayTagContainer ExactHairFilter;
		ExactHairFilter.AddTag(TAG_KawaiiPhysicsControlHair);
		TArray<FKawaiiPhysicsReference> HairNodes;
		UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(HairNodes, BlueprintFixture.GetComponent(), ExactHairFilter, true);
		if (!TestEqual(TEXT("対象は一ノード"), HairNodes.Num(), 1))
		{
			return false;
		}
		const FKawaiiPhysicsTransientHandle MultiplierHandle = UKawaiiPhysicsLibrary::GenerateTransientHandle();
		FKawaiiPhysicsSettingsMultiplier Multiplier;
		Multiplier.Damping = 0.5f;
		bool bOk = TestEqual(TEXT("ノードへ α=0.6 を Push"),
			UKawaiiPhysicsLibrary::PushPhysicsSettingsMultiplierOnComponent(BlueprintFixture.GetComponent(),
				MultiplierHandle, Multiplier, 0.6f, ExactHairFilter, true), 1);
		BlueprintFixture.StepFrame();
		bOk &= TestEqual(TEXT("停止前は一項目"),
			HairNodes[0].GetAnimNode<FAnimNode_KawaiiPhysics>().TransientForceStore.SettingsMultiplierItems.Num(), 1);
		UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section =
			NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(GetTransientPackage());
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> SectionEntry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		SectionEntry->Component = BlueprintFixture.GetComponent();
		SectionEntry->Handle = MultiplierHandle;
		SectionEntry->BlendOutTime = 0.5f;
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, SectionEntry);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
		bOk &= TestTrue(TEXT("Entry が停止した"), SectionEntry->bStopped);
		BlueprintFixture.StepFrame();
		bOk &= TestEqual(TEXT("0.5 秒フェードの最初の評価では残る"),
			HairNodes[0].GetAnimNode<FAnimNode_KawaiiPhysics>().TransientForceStore.SettingsMultiplierItems.Num(), 1);
		for (int32 Frame = 0; Frame < 35; ++Frame)
		{
			BlueprintFixture.StepFrame();
		}
		// 35/60 秒 > 0.5 秒。float の境界丸めを越えた時点で消える。
		bOk &= TestEqual(TEXT("物理時間 0.5 秒後に消える"),
			HairNodes[0].GetAnimNode<FAnimNode_KawaiiPhysics>().TransientForceStore.SettingsMultiplierItems.Num(), 0);
		return bOk;
	}
}

#endif
