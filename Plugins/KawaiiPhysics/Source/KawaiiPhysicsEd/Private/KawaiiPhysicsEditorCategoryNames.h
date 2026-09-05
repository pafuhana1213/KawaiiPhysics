// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace KawaiiPhysicsEditorCategoryNames
{
	struct FCategoryFilterGroup
	{
		FName GroupId;
		TArray<FName> CategoryNames;
	};

	inline const FName CategoryFilter(TEXT("Category Filter"));
	inline const FName KawaiiPhysicsTools(TEXT("Kawaii Physics Tools"));
	inline const FName DebugVisualization(TEXT("Debug Visualization"));
	inline const FName Functions(TEXT("Functions"));
	inline const FName Bones(TEXT("Bones"));
	inline const FName BonesBoneSubdivision(TEXT("Bones|Bone Subdivision"));
	inline const FName Physics(TEXT("Physics"));
	inline const FName PhysicsSettings(TEXT("Physics Settings"));
	inline const FName PhysicsSettingsCurves(TEXT("Physics Settings|Curves"));
	inline const FName Collision(TEXT("Collision"));
	inline const FName CollisionBoneConstraint(TEXT("Collision|Bone Constraint"));
	inline const FName CollisionSharedCollision(TEXT("Collision|Shared Collision"));
	inline const FName CollisionWorldCollision(TEXT("Collision|World Collision"));
	inline const FName CollisionSimpleWorldCollision(TEXT("Collision|Simple World Collision"));
	inline const FName Force(TEXT("Force"));
	inline const FName ForceExternalForce(TEXT("Force|External Force"));
	inline const FName ForceSyncBone(TEXT("Force|Sync Bone"));
	inline const FName Tag(TEXT("Tag"));
	inline const FName Alpha(TEXT("Alpha"));
	inline const FName Performance(TEXT("Performance"));
	inline const FName Bindings(TEXT("Bindings"));
	inline const FName PinOptions(TEXT("PinOptions"));
	inline const FName SharedPublisher(TEXT("Shared Publisher"));
	inline const FName SharedPublisherSimpleWorldCollision(TEXT("Shared Publisher|Simple World Collision"));
	inline const FName SharedPublisherWind(TEXT("Shared Publisher|Wind"));

	inline const TArray<FCategoryFilterGroup>& GetFilterGroups()
	{
		static const TArray<FCategoryFilterGroup> FilterGroups =
		{
			{Bones, {Bones, BonesBoneSubdivision}},
			{Physics, {PhysicsSettings, PhysicsSettingsCurves}},
			{Collision, {Collision, CollisionBoneConstraint, CollisionSharedCollision, CollisionWorldCollision, CollisionSimpleWorldCollision}},
			{Force, {Force, ForceExternalForce, ForceSyncBone}},
		};

		return FilterGroups;
	}

	inline const TArray<FName>& GetCategorySortOrderNames()
	{
		static const TArray<FName> CategoryNames =
		{
			CategoryFilter,
			KawaiiPhysicsTools,
			DebugVisualization,
			Functions,
			Bones,
			BonesBoneSubdivision,
			PhysicsSettings,
			PhysicsSettingsCurves,
			Collision,
			CollisionBoneConstraint,
			CollisionSharedCollision,
			CollisionWorldCollision,
			CollisionSimpleWorldCollision,
			Force,
			ForceExternalForce,
			ForceSyncBone,
			Tag,
			Alpha,
		};

		return CategoryNames;
	}

	inline const TArray<FName>& GetSharedPublisherCategorySortOrderNames()
	{
		static const TArray<FName> CategoryNames =
		{
			SharedPublisher,
			SharedPublisherSimpleWorldCollision,
			SharedPublisherWind,
		};

		return CategoryNames;
	}

	inline const TArray<FName>& GetFilterAdditionalHiddenNames()
	{
		// エンジン側生成カテゴリのため実在検証対象外。
		static const TArray<FName> CategoryNames =
		{
			Performance,
			Bindings,
			PinOptions,
		};

		return CategoryNames;
	}
}
