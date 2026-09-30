// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsLibrary.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPropertyAccessRoundTripTest,
                                 "KawaiiPhysics.PropertyAccess.RoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPropertyAccessRoundTripTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	bool bOk = true;

	bOk &= TestTrue(TEXT("Set float"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValue<float, FFloatProperty>(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WindScale), 3.5f));
	float WindScale = 0.0f;
	bOk &= TestTrue(TEXT("Get float"),
	                UKawaiiPhysicsLibrary::GetNodePropertyValue<float, FFloatProperty>(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WindScale), WindScale));
	bOk &= TestTrue(TEXT("Float round-trips"), FMath::IsNearlyEqual(WindScale, 3.5f));

	const FVector Gravity(10.0f, 20.0f, -30.0f);
	bOk &= TestTrue(TEXT("Set vector"),
	                UKawaiiPhysicsLibrary::SetNodeStructPropertyValue<FVector>(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, Gravity), Gravity));
	FVector OutGravity = FVector::ZeroVector;
	bOk &= TestTrue(TEXT("Get vector"),
	                UKawaiiPhysicsLibrary::GetNodeStructPropertyValue<FVector>(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, Gravity), OutGravity));
	bOk &= TestTrue(TEXT("Vector round-trips"), OutGravity.Equals(Gravity));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPropertyAccessDenyListTest,
                                 "KawaiiPhysics.PropertyAccess.DenyList",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPropertyAccessDenyListTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	bool bOk = true;

	bOk &= TestFalse(TEXT("Transient ModifyBones is rejected"),
	                 UKawaiiPhysicsLibrary::IsNodePropertyAccessible(
		                 GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ModifyBones)));
	bOk &= TestFalse(TEXT("ExternalForces is rejected"),
	                 UKawaiiPhysicsLibrary::IsNodePropertyAccessible(
		                 GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces)));
	bOk &= TestFalse(TEXT("Base Alpha is rejected"),
	                 UKawaiiPhysicsLibrary::SetNodePropertyValue<float, FFloatProperty>(
		                 Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, Alpha), 0.5f));
	bOk &= TestFalse(TEXT("DeltaTime deny-list is rejected"),
	                 UKawaiiPhysicsLibrary::SetNodePropertyValue<float, FFloatProperty>(
		                 Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DeltaTime), 0.5f));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPropertyAccessReinitTest,
                                 "KawaiiPhysics.PropertyAccess.Reinit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPropertyAccessReinitTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	bool bOk = true;

	bOk &= TestTrue(TEXT("DummyBoneLength is in the modify bones reinit set"),
	                UKawaiiPhysicsLibrary::DoesNodePropertyRequireModifyBonesReinit(
		                GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DummyBoneLength)));

	bOk &= TestTrue(TEXT("Set topology-affecting property"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValue<float, FFloatProperty>(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DummyBoneLength), 12.0f));
	bOk &= TestTrue(TEXT("Topology-affecting member is updated"), FMath::IsNearlyEqual(Node.DummyBoneLength, 12.0f));
	bOk &= TestFalse(TEXT("Runtime scalar is not in the reinit set"),
	                 UKawaiiPhysicsLibrary::DoesNodePropertyRequireModifyBonesReinit(
		                 GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WindScale)));

	bOk &= TestTrue(TEXT("bUseSimpleWorldCollision is in the simple world collision reinit set"),
	                UKawaiiPhysicsLibrary::DoesNodePropertyRequireSimpleWorldCollisionReinit(
		                GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSimpleWorldCollision)));
	bOk &= TestFalse(TEXT("SimpleWorldCollisionGatherInterval is not in the simple world collision reinit set"),
	                 UKawaiiPhysicsLibrary::DoesNodePropertyRequireSimpleWorldCollisionReinit(
		                 GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionGatherInterval)));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPropertyAccessWildcardSameTypeTest,
                                 "KawaiiPhysics.PropertyAccess.WildcardSameType",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPropertyAccessWildcardSameTypeTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	float FloatValue = 9.0f;

	const FProperty* FloatProperty = FindFProperty<FProperty>(
		FAnimNode_KawaiiPhysics::StaticStruct(), GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WindScale));

	bool bOk = true;
	bOk &= TestFalse(TEXT("Wildcard SameType mismatch is rejected"),
	                 UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(
		                 Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bEnableWind),
		                 FloatProperty, &FloatValue));
	bOk &= TestFalse(TEXT("Mismatched wildcard did not overwrite bool"), Node.bEnableWind);

	return bOk;
}

#endif // WITH_DEV_AUTOMATION_TESTS
