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

	/**
	 * このConstraintをBoneConstraintSubdivisionの対象から除外する（構造/対角Constraint用のオプトアウト）
	 * Exclude this constraint from BoneConstraintSubdivision (opt-out for structural/diagonal constraints).
	 */
	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	bool bExcludeFromSubdivision = false;

	/**
	 * 実行時状態。InitBoneConstraints は MergedBoneConstraints 上で ModifyBoneIndex1/2 と Length を毎回再計算し、bIsDummy は動的生成の dummy constraint にのみ設定する。
	 * Lambda は SimulateOnce で毎ステップ 0 にリセットされる。
	 * これらは全て実行時に再構築される MergedBoneConstraints 経由で読まれ、保存された BoneConstraints 配列の値を読む経路はない。
	 * Runtime state. InitBoneConstraints recalculates ModifyBoneIndex1/2 and Length on MergedBoneConstraints each time, and bIsDummy is set only for dynamically generated dummy constraints.
	 * Lambda is reset to 0 on every SimulateOnce step.
	 * These values are read only through runtime-rebuilt MergedBoneConstraints, with no path that reads saved values from the BoneConstraints array.
	 */
	UPROPERTY(Transient)
	int32 ModifyBoneIndex1 = -1;

	/** Index of the second modify bone */
	UPROPERTY(Transient)
	int32 ModifyBoneIndex2 = -1;

	/** Length of the constraint */
	UPROPERTY(Transient)
	float Length = -1.0f;

	/** Flag indicating if this is a dummy constraint */
	UPROPERTY(Transient)
	bool bIsDummy = false;

	/** Lambda value for the constraint */
	UPROPERTY(Transient)
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
