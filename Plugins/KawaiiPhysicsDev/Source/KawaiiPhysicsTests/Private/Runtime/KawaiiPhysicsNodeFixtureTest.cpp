// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsNodeFixture.h"
#include "Misc/AutomationTest.h"

namespace KawaiiPhysicsNodeFixtureTestPrivate
{

KawaiiPhysicsNodeFixture::FOptions MakeOptions()
{
	KawaiiPhysicsNodeFixture::FOptions Options;
	Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(5);
	Options.FrameDt = 1.0f / 60.0f;
	Options.ConfigureDeveloperSettings = [](UKawaiiPhysicsDeveloperSettings& Settings)
	{
		Settings.bUseFixedSubstepping = true;
		Settings.MaxSubsteps = 4;
	};
	Options.ConfigureNode = [](FAnimNode_KawaiiPhysics& Node)
	{
		Node.RootBone.BoneName = TEXT("root");
		Node.DummyBoneLength = 0.0f;
		Node.WarmUpFrames = 0;
		Node.bAllowWorldCollision = false;
		Node.bUseSimpleWorldCollision = false;
		Node.Gravity = FVector(980.0, 0.0, 0.0);
		Node.PhysicsSettings.WorldDampingLocation = 0.0f;
	};
	return Options;
}

bool GetOutputsFinite(const TArray<FBoneTransform>& Transforms)
{
	if (Transforms.IsEmpty())
	{
		return false;
	}
	for (const FBoneTransform& Bone : Transforms)
	{
		if (Bone.Transform.ContainsNaN())
		{
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeFixtureBaseBoneSpaceTest, "KawaiiPhysics.Fixture.Node.BaseBoneSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNodeFixtureBaseBoneSpaceTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FOptions Options = MakeOptions();
	Options.SimulationSpace = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
	Options.BaseBoneName = TEXT("chain_1");
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	bool bOk = true;
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		bOk &= TestTrue(TEXT("BaseBoneSpace evaluation is finite"), GetOutputsFinite(Fixture.EvaluateFrame()));
	}
	Fixture.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::WorldSpace, TEXT("chain_1"));
	bOk &= TestTrue(TEXT("Space switch evaluation is finite"), GetOutputsFinite(Fixture.EvaluateFrame()));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeFixtureRequiredBonesTest, "KawaiiPhysics.Fixture.Node.RequiredBones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNodeFixtureRequiredBonesTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeOptions());
	const TArray<FBoneIndexType> FullIndices = Fixture.Proxy->GetRequiredBones().GetBoneIndicesArray();
	bool bOk = TestTrue(TEXT("Full LOD evaluation is finite"), GetOutputsFinite(Fixture.EvaluateFrame()));
	const TArray<FBoneIndexType> ReducedIndices{0, 1, 2};
	Fixture.SetRequiredBones(ReducedIndices);
	bOk &= TestEqual(TEXT("Reduced LOD installs its RequiredBones"),
		Fixture.Proxy->GetRequiredBones().GetBoneIndicesArray().Num(), ReducedIndices.Num());
	bOk &= TestTrue(TEXT("Reduced LOD evaluation is finite"), GetOutputsFinite(Fixture.EvaluateFrame()));
	for (const FBoneTransform& Bone : Fixture.OutTransforms)
	{
		const int32 CompactIndex = Bone.BoneIndex.GetInt();
		const bool bValidCompactIndex = CompactIndex >= 0 && CompactIndex < ReducedIndices.Num();
		bOk &= TestTrue(TEXT("Output uses a required compact bone index"), bValidCompactIndex);
		if (bValidCompactIndex)
		{
			const int32 SkeletonIndex = Fixture.Proxy->GetRequiredBones().GetSkeletonIndex(Bone.BoneIndex);
			bOk &= TestTrue(TEXT("Output excludes removed skeleton bones"),
				ReducedIndices.Contains(static_cast<FBoneIndexType>(SkeletonIndex)));
		}
	}
	Fixture.SetRequiredBones(FullIndices);
	bOk &= TestTrue(TEXT("Restored LOD evaluation is finite"), GetOutputsFinite(Fixture.EvaluateFrame()));
	bOk &= TestEqual(TEXT("Restored LOD outputs every bone"), Fixture.OutTransforms.Num(), FullIndices.Num());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeFixtureScopedSettingsTest, "KawaiiPhysics.Fixture.Node.ScopedSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNodeFixtureScopedSettingsTest::RunTest(const FString& Parameters)
{
	UKawaiiPhysicsDeveloperSettings* Settings = GetMutableDefault<UKawaiiPhysicsDeveloperSettings>();
	const bool bPreviousFixed = Settings->bUseFixedSubstepping;
	const int32 PreviousMax = Settings->MaxSubsteps;
	const float PreviousRadiusScale = Settings->SimpleWorldCollisionAutoGatherRadiusScale;
	bool bOk = true;
	{
		KawaiiPhysicsNodeFixture::FScopedDeveloperSettings Scoped([](UKawaiiPhysicsDeveloperSettings& Value)
		{
			Value.bUseFixedSubstepping = !Value.bUseFixedSubstepping;
			Value.MaxSubsteps = 9;
			Value.SimpleWorldCollisionAutoGatherRadiusScale = 7.0f;
		});
		bOk &= TestEqual(TEXT("Settings override is applied"), Settings->bUseFixedSubstepping, !bPreviousFixed);
		bOk &= TestEqual(TEXT("MaxSubsteps override is applied"), Settings->MaxSubsteps, 9);
		bOk &= TestEqual(TEXT("Additional setting override is applied"), Settings->SimpleWorldCollisionAutoGatherRadiusScale, 7.0f);
	}
	bOk &= TestEqual(TEXT("Fixed stepping is restored"), Settings->bUseFixedSubstepping, bPreviousFixed);
	bOk &= TestEqual(TEXT("MaxSubsteps is restored"), Settings->MaxSubsteps, PreviousMax);
	bOk &= TestEqual(TEXT("Additional setting is restored"), Settings->SimpleWorldCollisionAutoGatherRadiusScale, PreviousRadiusScale);
	const FString Name = TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign");
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!TestNotNull(TEXT("Fixture CVar exists"), Variable))
	{
		return false;
	}
	const int32 PreviousValue = Variable->GetInt();
	const EConsoleVariableFlags PreviousFlags = Variable->GetFlags();
	const int32 NewValue = PreviousValue == 0 ? 1 : 0;
	{
		KawaiiPhysicsNodeFixture::FScopedCVar Scoped(Name, FString::FromInt(NewValue));
		bOk &= TestEqual(TEXT("CVar override is applied"), Variable->GetInt(), NewValue);
	}
	bOk &= TestEqual(TEXT("CVar value is restored"), Variable->GetInt(), PreviousValue);
	bOk &= TestEqual(TEXT("CVar flags are restored"), static_cast<uint32>(Variable->GetFlags()), static_cast<uint32>(PreviousFlags));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeFixtureMotionTest, "KawaiiPhysics.Fixture.Node.Motion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNodeFixtureMotionTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FOptions PoseOptions = MakeOptions();
	PoseOptions.SetInputPose = [](FCompactPose& Pose, const int32 Frame)
	{
		Pose[FCompactPoseBoneIndex(0)].SetTranslation(FVector(20.0 + Frame, 0.0, 0.0));
	};
	KawaiiPhysicsNodeFixture::FOptions ComponentOptions = MakeOptions();
	ComponentOptions.GetComponentTransform = [](const int32 Frame)
	{
		return FTransform(FVector(Frame * 5.0, 0.0, 0.0));
	};
	KawaiiPhysicsNodeFixture::FFixture Stationary(MakeOptions());
	KawaiiPhysicsNodeFixture::FFixture PoseMotion(PoseOptions);
	KawaiiPhysicsNodeFixture::FFixture ComponentMotion(ComponentOptions);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Stationary.EvaluateFrame();
		PoseMotion.EvaluateFrame();
		ComponentMotion.EvaluateFrame();
	}
	bool bOk = TestTrue(TEXT("Stationary output is finite"), GetOutputsFinite(Stationary.OutTransforms));
	bOk &= TestTrue(TEXT("Pose motion output is finite"), GetOutputsFinite(PoseMotion.OutTransforms));
	bOk &= TestTrue(TEXT("Component motion output is finite"), GetOutputsFinite(ComponentMotion.OutTransforms));
	if (Stationary.OutTransforms.IsEmpty() || PoseMotion.OutTransforms.IsEmpty() || ComponentMotion.OutTransforms.IsEmpty())
	{
		return false;
	}
	const FVector ReferenceLocation = Stationary.OutTransforms.Last().Transform.GetLocation();
	bOk &= TestFalse(TEXT("Input pose motion changes output"),
		PoseMotion.OutTransforms.Last().Transform.GetLocation().Equals(ReferenceLocation));
	bOk &= TestFalse(TEXT("Component motion changes output"),
		ComponentMotion.OutTransforms.Last().Transform.GetLocation().Equals(ReferenceLocation));
	return bOk;
}

}

#endif
