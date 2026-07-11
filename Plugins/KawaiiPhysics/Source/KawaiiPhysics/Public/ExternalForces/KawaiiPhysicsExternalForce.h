// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Engine/World.h"
#include "AnimNode_KawaiiPhysics.h"
#include "SceneManagement.h"
#include "Curves/CurveVector.h"

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Components/SkeletalMeshComponent.h"
#endif

#include "KawaiiPhysicsExternalForce.generated.h"

/**
 * Enum representing the space in which external forces are simulated.
 */
UENUM(BlueprintType)
enum class EExternalForceSpace : uint8
{
	/** Simulate in component space. Moving the entire skeletal mesh will have no effect on velocities */
	ComponentSpace,
	/** Simulate in world space. Moving the skeletal mesh will generate velocity changes */
	WorldSpace,
	/** Simulate in another bone space. Moving the entire skeletal mesh and individually modifying the base bone will have no effect on velocities */
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
	* 外力を適用するボーンを指定（＝指定しなかったボーンには適用しない）
	* 空の場合、全ての物理対象のボーンに適用
	* Specify the bones to which the external force will be applied (= the force will not be applied to bones that are not specified)
	* If empty, it will be applied to all physical target bones
	*/
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	TArray<FBoneReference> ApplyBoneFilter;

	/** 
	* 外力を適用しないボーンを指定
	* Specify the bones to which the external force will NOT be applied
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
	/**
	 * 実行時状態。RandomizedForceScale と ComponentTransform は基底 PreApply、Force は各サブクラス PreApply で毎フレーム必ず上書きされる。
	 * 保存された値は次の PreApply で破棄され、ユーザー編集用の乱数範囲は RandomForceScaleRange に保存する。
	 * 副作用は初回評価前のエディタデバッグ描画が保存値ではなくゼロから始まることのみ。
	 * Runtime state. Base PreApply always overwrites RandomizedForceScale and ComponentTransform, and subclass PreApply overwrites Force every frame.
	 * Saved values are discarded by the next PreApply, while the user-editable random range is stored in RandomForceScaleRange.
	 * The only side effect is that editor debug drawing before the first evaluation starts from zero instead of saved values.
	 */
	UPROPERTY(Transient)
	float RandomizedForceScale = 0.0f;

	/** The force vector */
	UPROPERTY(Transient)
	FVector Force = FVector::Zero();

	/** Transform of the component */
	UPROPERTY(Transient)
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
