// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Engine/World.h"
#include "AnimNode_KawaiiPhysics.h"
#include "SceneManagement.h"
#include "Curves/CurveVector.h"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
#include "Components/SkeletalMeshComponent.h"
#endif

#include "KawaiiPhysicsExternalForce.generated.h"

/**
 * Enum representing the space in which external forces are simulated.
 */
UENUM(BlueprintType)
enum class EExternalForceSpace : uint8
{
	/** Simulate in component space. Moving the entire skeletal mesh will have no affect on velocities */
	ComponentSpace,
	/** Simulate in world space. Moving the skeletal mesh will generate velocity changes */
	WorldSpace,
	/** Simulate in another bone space. Moving the entire skeletal mesh and individually modifying the base bone will have no affect on velocities */
	BoneSpace,
};

/**
 * Enum representing the evaluation type for external force curves.
 */
UENUM(BlueprintType)
enum class EExternalForceCurveEvaluateType : uint8
{
	/** Evaluate the curve at a single point */
	Single,
	/** Evaluate the curve by averaging multiple points */
	Average,
	/** Evaluate the curve by taking the maximum value from multiple points */
	Max,
	/** Evaluate the curve by taking the minimum value from multiple points */
	Min
};

///
/// Base
///
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	/** Whether the external force is enabled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	bool bIsEnabled = true;

	/** Whether to draw debug information for the external force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	bool bDrawDebug = false;

	/** 
	* 外力を適応するボーンを指定（＝指定しなかったボーンには適応しない）
	* 空の場合、全ての物理対象のボーンに適応
	* Specify the bones to which the external force will be applied (= the force will not be applied to bones that are not specified)
	* If empty, it will be applied to all physical target bones
	*/
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	TArray<FBoneReference> ApplyBoneFilter;

	/** 
	* 外力を適応しないボーンを指定
	* Specify the bones to which the external force will be NOT applied
	*/
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	TArray<FBoneReference> IgnoreBoneFilter;

	/** The space in which the external force is simulated */
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1, EditCondition=bCanSelectForceSpace, EditConditionHides),
		Category="KawaiiPhysics|ExternalForce")
	EExternalForceSpace ExternalForceSpace = EExternalForceSpace::WorldSpace;

	/** Range for randomizing the force scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	FFloatInterval RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);

	/** Owner of the external force */
	UPROPERTY()
	TObjectPtr<UObject> ExternalOwner;

	/** Whether the external force is applied only once */
	UPROPERTY()
	bool bIsOneShot = false;

#if ENABLE_ANIM_DEBUG
	/** Length of the debug arrow */
	float DebugArrowLength = 5.0f;

	/** Size of the debug arrow */
	float DebugArrowSize = 1.0f;

	/** Offset for the debug arrow */
	FVector DebugArrowOffset = FVector::Zero();

	/** Map of bone names to forces for debugging */
	TMap<FName, FVector> BoneForceMap;
#endif

protected:
	/** Randomized scale of the force */
	UPROPERTY()
	float RandomizedForceScale = 0.0f;

	/** The force vector */
	UPROPERTY()
	FVector Force = FVector::Zero();

	/** Transform of the component */
	UPROPERTY()
	FTransform ComponentTransform;

	/** Whether the force space can be selected */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	bool bCanSelectForceSpace = true;

public:
	virtual ~FKawaiiPhysics_ExternalForce() = default;

	virtual void Initialize(const FAnimationInitializeContext& Context);

	/** Prepares the external force before applying it */
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext);

	// Applies the external force to the bone's velocity
	virtual void ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                             FComponentSpacePoseContext& PoseContext, FVector& InOutVelocity);
	

	/** Applies the external force to a bone */
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM = FTransform::Identity);

	/** Finalizes the external force after applying it */
	virtual void PostApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext);

	/** Checks if debug information should be drawn */
	virtual bool IsDebugEnabled(bool bInPersona = false);

#if ENABLE_ANIM_DEBUG
	/** Draws debug information for the external force */
	virtual void AnimDrawDebug(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                           const FComponentSpacePoseContext& PoseContext);
#endif

#if WITH_EDITOR
	/** Draws debug information for the external force in edit mode */
	virtual void AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
	                                      const FAnimNode_KawaiiPhysics& Node, FPrimitiveDrawInterface* PDI);
#endif

protected:
	/** Checks if the external force can be applied to a bone */
	bool CanApply(const FKawaiiPhysicsModifyBone& Bone) const;
};
