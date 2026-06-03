// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "KawaiiPhysicsTypes.generated.h"

UENUM(BlueprintType)
enum class EKawaiiPhysicsSimulationSpace : uint8
{
	/** Simulate in component space */
	ComponentSpace,
	/** Simulate in world space. This fixes the issues of root bones moving suddenly */
	WorldSpace,
	/** Simulate in another bone space */
	BaseBoneSpace,
};

/**
 * Enum representing the planar constraint axis in KawaiiPhysics.
 */
UENUM(meta=(ScriptName = "KP_PlanarConstraint"))
enum class EPlanarConstraint : uint8
{
	/** No planar constraint */
	None,
	/** Constrain to the X axis */
	X,
	/** Constrain to the Y axis */
	Y,
	/** Constrain to the Z axis */
	Z,
};

/**
 * Enum representing the forward axis of a bone in KawaiiPhysics.
 */
UENUM()
enum class EBoneForwardAxis : uint8
{
	X_Positive,
	X_Negative,
	Y_Positive,
	Y_Negative,
	Z_Positive,
	Z_Negative,
};

/**
 * Structure representing the root bone settings for KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsRootBoneSetting
{
	GENERATED_BODY()

	/** 
	* 指定ボーンとそれ以下のボーンを制御対象に
	* Control the specified bone and the bones below it
	*/
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference RootBone;

	/** 
	* 指定したボーンとそれ以下のボーンを制御対象から除去
	* Do NOT control the specified bone and the bones below it
	*/
	UPROPERTY(EditAnywhere, Category = "Bones", meta = (EditCondition = "bUseOverrideExcludeBones"))
	TArray<FBoneReference> OverrideExcludeBones;
	UPROPERTY(EditAnywhere, Category = "Bones", meta = (InlineEditConditionToggle))
	bool bUseOverrideExcludeBones = false;
};


/**
 * Structure representing the settings for KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSettings
{
	GENERATED_BODY()

	/** 
	* 減衰度：揺れの強さを制御。値が小さいほど、加速度を物理挙動に反映
	* Damping physical behavior. As the value is smaller, the acceleration is more reflected to the physical behavior
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float Damping = 0.1f;

	/** 
	* 剛性度：値が大きいほど、元の形状を維持
	* Stiffness of physical behavior.As the value is larger, pre-physics shape is more respected
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float Stiffness = 0.05f;

	/** 
	* ワールド座標系におけるSkeletal Mesh Componentの移動量の反映度
	* Influence from movement in world coordinate system of Skeletal Mesh Component
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float WorldDampingLocation = 0.8f;

	/** 
	* ワールド座標系におけるSkeletal Mesh Componentの回転量の反映度
	* Influence from rotation in world coordinate system of Skeletal Mesh Component
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float WorldDampingRotation = 0.8f;

	/** 
	* 各ボーンのコリジョン半径
	* Radius of bone's collision
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", DisplayName="Collision Radius"),
		category = "KawaiiPhysics")
	float Radius = 3.0f;

	/** 
	* 物理挙動による回転制限。適切に設定することで荒ぶりを抑制
	* Rotational limitations in physical behavior. Setting the value properly can prevent rampage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float LimitAngle = 0.0f;
};

/**
 * Structure representing a bone that can be modified by the KawaiiPhysics system.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsModifyBone
{
	GENERATED_USTRUCT_BODY()

	/** Reference to the bone */
	UPROPERTY()
	FBoneReference BoneRef;

	/** Index of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	int32 Index = -1;

	/** Index of the parent bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	int32 ParentIndex = -1;

	/** Indices of the child bones */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	TArray<int32> ChildIndices;

	/** Physics settings for the bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FKawaiiPhysicsSettings PhysicsSettings;

	/** Current location of the bone */
	UPROPERTY(BlueprintReadWrite, Category = "KawaiiPhysics|ModifyBone")
	FVector Location = FVector::ZeroVector;

	/** Previous location of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FVector PrevLocation = FVector::ZeroVector;

	/** Previous rotation of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FQuat PrevRotation = FQuat::Identity;

	/** Pose location of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FVector PoseLocation = FVector::ZeroVector;

	/** Pose rotation of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FQuat PoseRotation = FQuat::Identity;

	/** Pose scale of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	FVector PoseScale = FVector::OneVector;

	/** Length of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	float BoneLength = 0.0f;

	/** Length from the root bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	float LengthFromRoot = 0.0f;

	/** Length rate from the root bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	float LengthRateFromRoot = 0.0f;

	/** Flag indicating if this is a dummy bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	bool bDummy = false;

	/**
	* ボーン間ダミーボーンフラグ（2つの実ボーン間に挿入されたダミー）
	* Flag: this is an inter-bone dummy (inserted between two real bones)
	*/
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	bool bInterBoneDummy = false;

	/**
	* ボーン間ダミーの補間先（実子ボーン）のインデックス / Real child bone index for PoseLocation interpolation
	*/
	UPROPERTY()
	int32 InterBoneRealChildIndex = -1;

	/**
	* ボーン間ダミーの補間元（実親ボーン）のインデックス / Real parent bone index for PoseLocation interpolation
	*/
	UPROPERTY()
	int32 InterBoneRealParentIndex = -1;

	/**
	* ボーン間ダミーの補間アルファ（0.0=実親, 1.0=実子）/ Interpolation alpha between real parent and child
	*/
	UPROPERTY()
	float InterBoneAlpha = 0.0f;

	/** Flag indicating if simulation should be skipped for this bone */
	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics|ModifyBone")
	bool bSkipSimulate = false;

	/**
	 * Checks if the bone has a parent.
	 *
	 * @return True if the bone has a parent, false otherwise.
	 */
	bool HasParent() const { return ParentIndex >= 0; }

	/** Default constructor */
	FKawaiiPhysicsModifyBone()
	{
	}
};
