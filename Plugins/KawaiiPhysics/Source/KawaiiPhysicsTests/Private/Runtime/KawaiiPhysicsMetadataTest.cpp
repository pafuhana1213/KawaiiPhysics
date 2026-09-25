// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimNode_KawaiiPhysics.h"
#include "Misc/AutomationTest.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCollisionCategoryMetadataTest,
                                 "KawaiiPhysics.Metadata.CollisionCategory",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCollisionCategoryMetadataTest::RunTest(const FString& Parameters)
{
#if WITH_EDITORONLY_DATA
	const FName CategoryKey(TEXT("Category"));
	const FName DisplayNameKey(TEXT("DisplayName"));
	const UScriptStruct* NodeStruct = FAnimNode_KawaiiPhysics::StaticStruct();
	bool bOk = true;

	for (TFieldIterator<FProperty> PropertyIt(NodeStruct); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		if (!Property->HasMetaData(CategoryKey))
		{
			continue;
		}

		const FString Category = Property->GetMetaData(CategoryKey);
		const bool bUsesOldLimitsCategory = Category == TEXT("Limits") || Category.StartsWith(TEXT("Limits|"));
		bOk &= TestFalse(
			FString::Printf(TEXT("%s の Category に旧カテゴリが残っています: %s"),
			                *Property->GetName(), *Category),
			bUsesOldLimitsCategory);
	}

	struct FExpectedMeta
	{
		FName PropertyName;
		const TCHAR* ExpectedValue;
	};

	const FExpectedMeta ExpectedCategories[] = {
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SphericalLimits), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CapsuleLimits), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TaperedCapsuleLimits), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoxLimits), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PlanarLimits), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsAssetForLimits), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, MirrorDataTableForLimits), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bSkipMirroredBoneWithExistingCollision), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SphericalLimitsData), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CapsuleLimitsData), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TaperedCapsuleLimitsData), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoxLimitsData), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PlanarLimitsData), TEXT("Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bSharedCollisionSource), TEXT("Collision|Shared Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSharedCollision), TEXT("Collision|Shared Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SharedCollisionGroupTag), TEXT("Collision|Shared Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintGlobalComplianceType), TEXT("Collision|Bone Constraint")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintIterationCountBeforeCollision), TEXT("Collision|Bone Constraint")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintIterationCountAfterCollision), TEXT("Collision|Bone Constraint")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bAutoAddChildDummyBoneConstraint), TEXT("Collision|Bone Constraint")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraints), TEXT("Collision|Bone Constraint")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintsDataAsset), TEXT("Collision|Bone Constraint")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintsData), TEXT("Collision|Bone Constraint")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bAllowWorldCollision), TEXT("Collision|World Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bOverrideCollisionParams), TEXT("Collision|World Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CollisionChannelSettings), TEXT("Collision|World Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bIgnoreSelfComponent), TEXT("Collision|World Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, IgnoreBones), TEXT("Collision|World Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, IgnoreBoneNamePrefix), TEXT("Collision|World Collision")},
	};

	for (const FExpectedMeta& ExpectedCategory : ExpectedCategories)
	{
		const FProperty* Property = FindFProperty<FProperty>(NodeStruct, ExpectedCategory.PropertyName);
		if (!TestNotNull(
			    FString::Printf(TEXT("%s のプロパティが見つかること"), *ExpectedCategory.PropertyName.ToString()),
			    Property))
		{
			bOk = false;
			continue;
		}

		const FString Category = Property->GetMetaData(CategoryKey);
		bOk &= TestEqual(
			FString::Printf(TEXT("%s の Category が一致すること"), *ExpectedCategory.PropertyName.ToString()),
			Category, FString(ExpectedCategory.ExpectedValue));
	}

	const FExpectedMeta ExpectedDisplayNames[] = {
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SphericalLimits), TEXT("Spherical Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CapsuleLimits), TEXT("Capsule Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TaperedCapsuleLimits), TEXT("Tapered Capsule Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoxLimits), TEXT("Box Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PlanarLimits), TEXT("Planar Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset), TEXT("Collision Data Asset")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsAssetForLimits), TEXT("Physics Asset for Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, MirrorDataTableForLimits), TEXT("Mirror Data Table for Collision")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SphericalLimitsData), TEXT("Spherical Collision Data")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CapsuleLimitsData), TEXT("Capsule Collision Data")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TaperedCapsuleLimitsData), TEXT("Tapered Capsule Collision Data")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoxLimitsData), TEXT("Box Collision Data")},
		{GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PlanarLimitsData), TEXT("Planar Collision Data")},
	};

	for (const FExpectedMeta& ExpectedDisplayName : ExpectedDisplayNames)
	{
		const FProperty* Property = FindFProperty<FProperty>(NodeStruct, ExpectedDisplayName.PropertyName);
		if (!TestNotNull(
			    FString::Printf(TEXT("%s のプロパティが見つかること"), *ExpectedDisplayName.PropertyName.ToString()),
			    Property))
		{
			bOk = false;
			continue;
		}

		const FString DisplayName = Property->GetMetaData(DisplayNameKey);
		bOk &= TestEqual(
			FString::Printf(TEXT("%s の DisplayName が一致すること"), *ExpectedDisplayName.PropertyName.ToString()),
			DisplayName, FString(ExpectedDisplayName.ExpectedValue));
	}

	return bOk;
#else
	return true;
#endif
}

#endif
