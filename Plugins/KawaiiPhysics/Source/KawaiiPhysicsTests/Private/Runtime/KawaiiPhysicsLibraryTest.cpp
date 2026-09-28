// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsTestHarness.h"

// 真偽値と列挙値のエクスポート書式を守る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsLibraryPropertyStringFormatTest,
                                 "KawaiiPhysics.Library.PropertyStringFormat",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsLibraryPropertyStringFormatTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	FString ValueText;

	FAnimNode_KawaiiPhysics Node;
	bOk &= TestTrue(TEXT("Set bool false"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSimpleWorldCollision),
		                TEXT("False")));
	bOk &= TestTrue(TEXT("Get bool false"),
	                UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSimpleWorldCollision),
		                ValueText));
	bOk &= TestEqual(TEXT("Bool false string"), ValueText, FString(TEXT("False")));

	bOk &= TestTrue(TEXT("Set bool true"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSimpleWorldCollision),
		                TEXT("True")));
	bOk &= TestTrue(TEXT("Get bool true"),
	                UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSimpleWorldCollision),
		                ValueText));
	bOk &= TestEqual(TEXT("Bool true string"), ValueText, FString(TEXT("True")));

	bOk &= TestTrue(TEXT("Set skeletal mesh collision enum"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSkeletalMeshCollision),
		                TEXT("None")));
	bOk &= TestTrue(TEXT("Get skeletal mesh collision enum"),
	                UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSkeletalMeshCollision),
		                ValueText));
	bOk &= TestEqual(TEXT("Enum None string"), ValueText, FString(TEXT("None")));

	bOk &= TestTrue(TEXT("Set convex fallback enum"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionConvexFallbackShape),
		                TEXT("BoundingBox")));
	bOk &= TestTrue(TEXT("Get convex fallback enum"),
	                UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionConvexFallbackShape),
		                ValueText));
	bOk &= TestEqual(TEXT("Enum BoundingBox string"), ValueText, FString(TEXT("BoundingBox")));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsLibraryRuntimeNodeInfoNullComponentTest,
	"KawaiiPhysics.Library.RuntimeNodeInfo.NullComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsLibraryRuntimeNodeInfoNullComponentTest::RunTest(const FString& Parameters)
{
	TArray<FKawaiiPhysicsRuntimeNodeInfo> Infos;
	Infos.AddDefaulted();
	FString Error = TEXT("old error");
	const int32 Count = UKawaiiPhysicsLibrary::GetRuntimeNodeInfosOnComponent(
		nullptr, FGameplayTagContainer(), false, Infos, Error);
	bool bOk = TestEqual(TEXT("Null component result"), Count, -1);
	bOk &= TestFalse(TEXT("Error is populated"), Error.IsEmpty());
	bOk &= TestTrue(TEXT("Infos are cleared"), Infos.IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsLibraryRuntimeNodeInfoUnevaluatedNodeTest,
	"KawaiiPhysics.Library.RuntimeNodeInfo.UnevaluatedNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsLibraryRuntimeNodeInfoUnevaluatedNodeTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	bool bEvaluated = true;
	const FTransform SimulationToComponent = Node.GetSimulationSpace2ComponentSpace(bEvaluated);
	bool bOk = TestFalse(TEXT("Node has not been evaluated"), bEvaluated);
	bOk &= TestTrue(TEXT("Unevaluated transform is identity"), SimulationToComponent.Equals(FTransform::Identity));

	FKawaiiPhysicsRuntimeNodeInfo Info;
	KawaiiPhysics::BuildRuntimeNodeInfo(Node, Info);
	bOk &= TestFalse(TEXT("Info has not been evaluated"), Info.bEvaluated);
	bOk &= TestTrue(TEXT("Info bones are empty"), Info.Bones.IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsLibraryRuntimeNodeInfoSharedAndGroundSourcesTest,
	"KawaiiPhysics.Library.RuntimeNodeInfo.SharedAndGroundSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsLibraryRuntimeNodeInfoSharedAndGroundSourcesTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 5.0f, FVector(10.0f, 0.0f, 0.0f));
	Accessor.Node.ModifyBones[0].BoneRef.BoneName = TEXT("SkirtRoot");
	Accessor.Node.ModifyBones[0].PhysicsSettings.Radius = 3.0f;
	Accessor.Node.ModifyBones[0].bSkipSimulate = true;
	Accessor.Node.ModifyBones[1].bDummy = true;
	Accessor.Node.ModifyBones[1].BoneRef.BoneName = TEXT("IgnoredDummyName");

	FSphericalLimit Sphere;
	Sphere.Location = FVector(1.0f, 2.0f, 3.0f);
	Sphere.Radius = 4.0f;
	Accessor.Node.SphericalLimits.Add(Sphere);
	FSphericalLimit ZeroRadiusSphere;
	ZeroRadiusSphere.Radius = 0.0f;
	Accessor.Node.SphericalLimits.Add(ZeroRadiusSphere);

	FCapsuleLimit SharedCapsule;
	SharedCapsule.Location = FVector(5.0f, 6.0f, 7.0f);
	SharedCapsule.Radius = 2.0f;
	SharedCapsule.Length = 8.0f;
	SharedCapsule.DrivingBone.BoneName = TEXT("IgnoredSharedBone");
	TArray<FCapsuleLimit> SharedCapsules;
	SharedCapsules.Add(SharedCapsule);
	Accessor.SetSharedCapsuleLimits(SharedCapsules);
	Accessor.Node.bUseSharedCollision = true;

	FBoxLimit GroundBox;
	GroundBox.Location = FVector(0.0f, 0.0f, -10.0f);
	TArray<FBoxLimit> GroundBoxes;
	GroundBoxes.Add(GroundBox);
	Accessor.SetSimpleWorldLimits({}, {}, {}, {}, {}, GroundBoxes);

	FTaperedCapsuleLimit Tapered;
	Tapered.Location = FVector(0.0f, 0.0f, 20.0f);
	Tapered.Radius0 = 8.0f;
	Tapered.Radius1 = 2.0f;
	Tapered.Length = 4.0f;
	Accessor.Node.TaperedCapsuleLimits.Add(Tapered);

	FKawaiiPhysicsRuntimeNodeInfo Info;
	KawaiiPhysics::BuildRuntimeNodeInfo(Accessor.Node, Info);
	bool bOk = TestEqual(TEXT("Bone count"), Info.Bones.Num(), 2);
	bOk &= TestEqual(TEXT("Limit count"), Info.Limits.Num(), 5);
	if (Info.Bones.Num() == 2)
	{
		bOk &= TestEqual(TEXT("Root radius"), Info.Bones[0].Radius, 3.0f);
		bOk &= TestTrue(TEXT("Root skips simulation and collision"), Info.Bones[0].bSkipSimulate);
		bOk &= TestFalse(TEXT("Tip is simulated"), Info.Bones[1].bSkipSimulate);
		bOk &= TestTrue(TEXT("Tip dummy kind"), Info.Bones[1].DummyType == EKawaiiPhysicsDummyBoneType::Tip);
		bOk &= TestEqual(TEXT("Tip dummy name"), Info.Bones[1].BoneName, NAME_None);
		bOk &= TestTrue(TEXT("Component-space bone location"), Info.Bones[0].Location.Equals(FVector(10.0f, 0.0f, 0.0f)));
	}
	if (Info.Limits.Num() == 5)
	{
		bOk &= TestTrue(TEXT("Component-space sphere location"), Info.Limits[0].Location.Equals(Sphere.Location));
		bOk &= TestTrue(TEXT("Positive-radius sphere enabled"), Info.Limits[0].bEnabled);
		bOk &= TestFalse(TEXT("Zero-radius sphere disabled"), Info.Limits[1].bEnabled);
		bOk &= TestTrue(TEXT("Tapered type"), Info.Limits[2].LimitType == ECollisionLimitType::TaperedCapsule);
		bOk &= TestTrue(TEXT("Tapered fallback endpoints"), Info.Limits[2].Start.Equals(Info.Limits[2].End));
		bOk &= TestTrue(TEXT("Tapered fallback center"), Info.Limits[2].Start.Equals(FVector(0.0f, 0.0f, 22.0f)));
		bOk &= TestTrue(TEXT("Shared source"), Info.Limits[3].SourceType == ECollisionSourceType::Shared);
		bOk &= TestEqual(TEXT("Shared array"), Info.Limits[3].SourceArrayName, FName(TEXT("SharedCapsuleLimits")));
		bOk &= TestEqual(TEXT("Shared driving bone"), Info.Limits[3].DrivingBone, NAME_None);
		bOk &= TestTrue(TEXT("Shared capsule enabled"), Info.Limits[3].bEnabled);
		bOk &= TestEqual(TEXT("Ground box array"), Info.Limits[4].SourceArrayName, FName(TEXT("SimpleWorldGroundBoxLimits")));
		bOk &= TestTrue(TEXT("Ground box source"), Info.Limits[4].SourceType == ECollisionSourceType::SimpleWorld);
		bOk &= TestTrue(TEXT("Ground box enabled"), Info.Limits[4].bEnabled);
	}
	Accessor.Node.bUseSharedCollision = false;
	Accessor.Node.bUseSimpleWorldCollision = false;
	KawaiiPhysics::BuildRuntimeNodeInfo(Accessor.Node, Info);
	if (Info.Limits.Num() == 5)
	{
		bOk &= TestFalse(TEXT("Shared capsule disabled by node setting"), Info.Limits[3].bEnabled);
		bOk &= TestFalse(TEXT("Ground box disabled by node setting"), Info.Limits[4].bEnabled);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsLibraryRuntimeNodeInfoConstraintSourcesTest,
	"KawaiiPhysics.Library.RuntimeNodeInfo.ConstraintSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsLibraryRuntimeNodeInfoConstraintSourcesTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 5.0f);
	Accessor.Node.ModifyBones[0].BoneRef.BoneName = TEXT("Root");
	Accessor.Node.ModifyBones[1].BoneRef.BoneName = TEXT("Tip");
	Accessor.Node.BoneConstraints.AddDefaulted();

	const auto AddMergedConstraint = [&Accessor](int32 BoneIndex1, int32 BoneIndex2, bool bIsDummy, float Length)
	{
		FModifyBoneConstraint& Constraint = Accessor.Node.MergedBoneConstraints.AddDefaulted_GetRef();
		Constraint.ModifyBoneIndex1 = BoneIndex1;
		Constraint.ModifyBoneIndex2 = BoneIndex2;
		Constraint.bIsDummy = bIsDummy;
		Constraint.Length = Length;
	};
	AddMergedConstraint(0, 1, false, 5.0f);
	AddMergedConstraint(0, 1, false, 5.0f);
	AddMergedConstraint(0, 1, true, 5.0f);
	AddMergedConstraint(0, 2, false, 5.0f);
	AddMergedConstraint(0, 1, false, 0.0f);

	FKawaiiPhysicsRuntimeNodeInfo Info;
	KawaiiPhysics::BuildRuntimeNodeInfo(Accessor.Node, Info);
	bool bOk = TestEqual(TEXT("Valid constraint count"), Info.Constraints.Num(), 3);
	if (Info.Constraints.Num() == 3)
	{
		bOk &= TestTrue(TEXT("Node constraint source"), Info.Constraints[0].SourceType == EKawaiiPhysicsConstraintSourceType::AnimNode);
		bOk &= TestTrue(TEXT("Asset constraint source"), Info.Constraints[1].SourceType == EKawaiiPhysicsConstraintSourceType::DataAsset);
		bOk &= TestTrue(TEXT("Dummy constraint source"), Info.Constraints[2].SourceType == EKawaiiPhysicsConstraintSourceType::AutoDummy);
		bOk &= TestEqual(TEXT("First constraint bone name"), Info.Constraints[0].BoneName1, FName(TEXT("Root")));
		bOk &= TestEqual(TEXT("Second constraint bone name"), Info.Constraints[0].BoneName2, FName(TEXT("Tip")));
	}
	return bOk;
}

#endif // WITH_DEV_AUTOMATION_TESTS
