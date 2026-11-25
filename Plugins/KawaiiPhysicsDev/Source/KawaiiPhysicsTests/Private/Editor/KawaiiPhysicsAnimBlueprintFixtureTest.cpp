// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsAnimBlueprintFixture.h"
#include "Misc/AutomationTest.h"

namespace KawaiiPhysicsAnimBlueprintFixtureTestPrivate
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimBlueprintFixtureComponentTest, "KawaiiPhysics.Fixture.AnimBlueprint.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimBlueprintFixtureComponentTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FScopedDeveloperSettings Settings([](UKawaiiPhysicsDeveloperSettings& Value)
	{
		Value.bUseFixedSubstepping = true;
		Value.MaxSubsteps = 4;
	});
	KawaiiPhysicsAnimBlueprintFixture::FOptions Options;
	Options.Bones = KawaiiPhysicsNodeFixture::BuildHair(2, 3);
	Options.NodeCount = 2;
	Options.ConfigureNode = [](FAnimNode_KawaiiPhysics& Node, const int32 NodeIndex)
	{
		Node.KawaiiPhysicsTag = FGameplayTag::RequestGameplayTag(FName(NodeIndex == 0
			? TEXT("KawaiiPhysics.Test.PresetSource") : TEXT("KawaiiPhysics.Test.PresetTarget")));
		Node.RootBone.BoneName = KawaiiPhysicsNodeFixture::GetHairBoneName(NodeIndex, 0);
		Node.DummyBoneLength = 0.0f;
		Node.WarmUpFrames = 0;
		Node.bAllowWorldCollision = false;
		Node.bUseSimpleWorldCollision = false;
		Node.Gravity = FVector(980.0, 0.0, 0.0);
	};
	Options.GetComponentTransform = [](const int32 Frame)
	{
		return FTransform(FRotator(0.0, Frame * 0.5, 0.0), FVector(Frame * 0.3, 0.0, 0.0));
	};
	bool bOk = true;
	// 一回目の World や生成クラスを、二回目が再利用しないことも確かめる。
	for (int32 RunIndex = 0; RunIndex < 2; ++RunIndex)
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture Fixture(Options);
		for (const FString& Message : Fixture.GetMessages())
		{
			AddInfo(Message);
		}
		bOk &= TestEqual(TEXT("AnimBlueprint compiles without errors"), Fixture.CompileErrorCount, 0);
		if (!TestTrue(TEXT("Component fixture initializes"), Fixture.IsReady()))
		{
			return false;
		}
		bOk &= TestNotNull(TEXT("Component owns an AnimInstance"), Fixture.GetAnimInstance());
		TArray<FKawaiiPhysicsReference> Nodes = Fixture.GetNodes();
		bOk &= TestEqual(TEXT("Component enumeration finds both connected nodes"), Nodes.Num(), 2);
		if (Nodes.Num() != 2)
		{
			return false;
		}
		bOk &= TestTrue(TEXT("Nodes retain distinct tags"),
			Nodes[0].GetAnimNode<FAnimNode_KawaiiPhysics>().KawaiiPhysicsTag !=
			Nodes[1].GetAnimNode<FAnimNode_KawaiiPhysics>().KawaiiPhysicsTag);
		for (const FKawaiiPhysicsReference& Reference : Nodes)
		{
			bOk &= TestTrue(TEXT("Node has a valid tag"), Reference.GetAnimNode<FAnimNode_KawaiiPhysics>().KawaiiPhysicsTag.IsValid());
		}
		const FName TipName = KawaiiPhysicsNodeFixture::GetHairBoneName(0, 2);
		FTransform ReferenceTransform;
		if (!TestTrue(TEXT("Reference component pose is available"), Fixture.GetBoneComponentTransform(TipName, ReferenceTransform)))
		{
			return false;
		}
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			bOk &= TestTrue(TEXT("Synchronous component frame completes"), Fixture.StepFrame());
			FTransform Transform;
			bOk &= TestTrue(TEXT("Evaluated component pose is available"), Fixture.GetBoneComponentTransform(TipName, Transform));
			bOk &= TestFalse(TEXT("Evaluated component pose is finite"), Transform.ContainsNaN());
		}
		FTransform FinalTransform;
		bOk &= TestTrue(TEXT("Final component pose is available"), Fixture.GetBoneComponentTransform(TipName, FinalTransform));
		bOk &= TestFalse(TEXT("Simulated bone moves from the reference pose"),
			FinalTransform.GetLocation().Equals(ReferenceTransform.GetLocation()));
		for (const FKawaiiPhysicsReference& Reference : Nodes)
		{
			bOk &= TestTrue(TEXT("Engine evaluation initializes simulated bones"),
				!Reference.GetAnimNode<FAnimNode_KawaiiPhysics>().ModifyBones.IsEmpty());
		}
	}
	return bOk;
}

}

#endif
