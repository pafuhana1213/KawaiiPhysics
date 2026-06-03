// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "KawaiiPhysicsBoneConstraintTypes.generated.h"

UENUM()
enum class EXPBDComplianceType : uint8
{
	Concrete UMETA(DisplayName = "Concrete"),
	Wood UMETA(DisplayName = "Wood"),
	Leather UMETA(DisplayName = "Leather"),
	Tendon UMETA(DisplayName = "Tendon"),
	Rubber UMETA(DisplayName = "Rubber"),
	Muscle UMETA(DisplayName = "Muscle"),
	Fat UMETA(DisplayName = "Fat"),
};

/**
 * Structure representing a constraint between two bones for the KawaiiPhysics system.
 */
USTRUCT()
struct FModifyBoneConstraint
{
	GENERATED_BODY()

	FModifyBoneConstraint()
	{
	}

	/** The first bone reference in the constraint */
	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FBoneReference Bone1;

	/** The second bone reference in the constraint */
	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FBoneReference Bone2;

	/** Flag to override the compliance type */
	UPROPERTY(EditAnywhere, category = "KawaiiPhysics", meta=(InlineEditConditionToggle))
	bool bOverrideCompliance = false;

	/** The compliance type to use if overridden */
	UPROPERTY(EditAnywhere, category = "KawaiiPhysics", meta=(EditCondition="bOverrideCompliance"))
	EXPBDComplianceType ComplianceType = EXPBDComplianceType::Leather;

	/** Index of the first modify bone */
	UPROPERTY()
	int32 ModifyBoneIndex1 = -1;

	/** Index of the second modify bone */
	UPROPERTY()
	int32 ModifyBoneIndex2 = -1;

	/** Length of the constraint */
	UPROPERTY()
	float Length = -1.0f;

	/** Flag indicating if this is a dummy constraint */
	UPROPERTY()
	bool bIsDummy = false;

	/** Lambda value for the constraint */
	UPROPERTY()
	float Lambda = 0.0f;

	/** Equality operator to compare two constraints */
	FORCEINLINE bool operator ==(const FModifyBoneConstraint& Other) const
	{
		return ((Bone1 == Other.Bone1 && Bone2 == Other.Bone2) || (Bone1 == Other.Bone2 && Bone2 == Other.Bone1)) &&
			ComplianceType == Other.ComplianceType;
	}

	/** Initializes the bone references with the required bones */
	void InitializeBone(const FBoneContainer& RequiredBones)
	{
		Bone1.Initialize(RequiredBones);
		Bone2.Initialize(RequiredBones);
	}

	/** Checks if the bone references are valid */
	bool IsBoneReferenceValid() const
	{
		return ModifyBoneIndex1 >= 0 && ModifyBoneIndex2 >= 0;
	}

	/** Checks if the constraint is valid */
	bool IsValid() const
	{
		return Length > 0.0f;
	}
};
