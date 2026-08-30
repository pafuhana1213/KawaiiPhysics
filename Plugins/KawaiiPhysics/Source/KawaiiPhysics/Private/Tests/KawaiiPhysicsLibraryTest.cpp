// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsLibraryPropertyStringRoundTripTest,
                                 "KawaiiPhysics.Library.PropertyStringRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsLibraryPropertyStringRoundTripTest::RunTest(const FString& Parameters)
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

	bOk &= TestTrue(TEXT("Set float"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionGatherInterval),
		                TEXT("0.125")));
	bOk &= TestTrue(TEXT("Get float"),
	                UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionGatherInterval),
		                ValueText));
	FAnimNode_KawaiiPhysics FloatRoundTripNode;
	bOk &= TestTrue(TEXT("Import float round-trip string"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		                FloatRoundTripNode,
		                GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionGatherInterval),
		                ValueText));
	bOk &= TestTrue(TEXT("Float string round-trips"),
	                FMath::IsNearlyEqual(FloatRoundTripNode.SimpleWorldCollisionGatherInterval,
	                                     Node.SimpleWorldCollisionGatherInterval));

	bOk &= TestTrue(TEXT("Set FName array"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, IgnoreBoneNamePrefix),
		                TEXT("(KP_Left,KP_Right)")));
	bOk &= TestTrue(TEXT("Get FName array"),
	                UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(
		                Node, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, IgnoreBoneNamePrefix),
		                ValueText));
	FAnimNode_KawaiiPhysics NameRoundTripNode;
	bOk &= TestTrue(TEXT("Import FName round-trip string"),
	                UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(
		                NameRoundTripNode, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, IgnoreBoneNamePrefix),
		                ValueText));
	bOk &= TestEqual(TEXT("FName array count round-trips"),
	                 NameRoundTripNode.IgnoreBoneNamePrefix.Num(),
	                 Node.IgnoreBoneNamePrefix.Num());
	for (int32 NameIndex = 0;
	     NameIndex < NameRoundTripNode.IgnoreBoneNamePrefix.Num() && NameIndex < Node.IgnoreBoneNamePrefix.Num();
	     ++NameIndex)
	{
		bOk &= TestEqual(FString::Printf(TEXT("FName array element %d round-trips"), NameIndex),
		                 NameRoundTripNode.IgnoreBoneNamePrefix[NameIndex],
		                 Node.IgnoreBoneNamePrefix[NameIndex]);
	}

	return bOk;
}

#endif // WITH_DEV_AUTOMATION_TESTS
