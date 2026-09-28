// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsLibrary.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
