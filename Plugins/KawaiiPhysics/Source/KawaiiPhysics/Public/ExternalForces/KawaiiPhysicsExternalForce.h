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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="Kawaii Physics|ExternalForce")
	bool bIsEnabled = true;

	/** Whether to draw debug information for the external force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="Kawaii Physics|ExternalForce")
	bool bDrawDebug = false;

	/** 
	* 外力を適用するボーンを指定（＝指定しなかったボーンには適用しない）
	* 空の場合、全ての物理対象のボーンに適用
	* Specify the bones to which the external force will be applied (= the force will not be applied to bones that are not specified)
	* If empty, it will be applied to all physical target bones
	*/
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1), Category="Kawaii Physics|ExternalForce")
	TArray<FBoneReference> ApplyBoneFilter;

	/** 
	* 外力を適用しないボーンを指定
	* Specify the bones to which the external force will NOT be applied
	*/
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1), Category="Kawaii Physics|ExternalForce")
	TArray<FBoneReference> IgnoreBoneFilter;

	/** The space in which the external force is simulated */
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1, EditCondition=bCanSelectForceSpace, EditConditionHides),
		Category="Kawaii Physics|ExternalForce")
	EExternalForceSpace ExternalForceSpace = EExternalForceSpace::WorldSpace;

	/** Range for randomizing the force scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1, EditCondition=bSupportsRandomForceScaleRange,
		EditConditionHides), Category="Kawaii Physics|ExternalForce")
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
	UPROPERTY(BlueprintReadWrite, Category="Kawaii Physics|ExternalForce")
	bool bCanSelectForceSpace = true;

	/** Whether RandomForceScaleRange is supported (controls both edit visibility and the runtime randomization; when false, RandomizedForceScale stays 1) */
	UPROPERTY(BlueprintReadWrite, Category="Kawaii Physics|ExternalForce")
	bool bSupportsRandomForceScaleRange = true;

public:
	virtual ~FKawaiiPhysics_ExternalForce() = default;

	virtual void Initialize(const FAnimationInitializeContext& Context);

	/** Prepares the external force before applying it */
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext);

	/**
	 * ApplyBoneFilter / IgnoreBoneFilter の判定結果をボーン index ごとに作り直す。
	 * 基底の PreApply の先頭と、ノードが各外力の PreApply を呼び終えた直後の 2 か所で呼ばれる。
	 * そのため派生の PreApply が Super の後でフィルタを変えても、同じフレームの Apply から反映される。
	 * 派生の PreApply 内でフィルタを変えた後に CanApply を使う場合は、その前にこれを呼ぶこと。
	 * フィルタの中身とボーン名の列を前回と比べ、どちらかが変わったときだけ再計算する（変わらなければ照合のみ）。
	 * Rebuilds the per-bone result of ApplyBoneFilter / IgnoreBoneFilter.
	 * It runs at the start of the base PreApply and again right after the node's call to each force's PreApply returns,
	 * so a filter a derived PreApply changes after Super applies from the same frame's Apply. A derived PreApply that
	 * changes the filter and then calls CanApply must call this first. It recomputes only when the filter contents or the
	 * bone names differ from the last build; otherwise it only compares them.
	 */
	void RefreshBoneFilterCache(const FAnimNode_KawaiiPhysics& Node);

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
	/**
	 * WorldSpace / ComponentSpace の外力を Node.SimulationSpace へ変換する。
	 * BoneSpace はボーンローカルのまま返し、Apply 側で BoneTM を1回だけ掛ける。
	 * Converts WorldSpace / ComponentSpace forces to Node.SimulationSpace.
	 * Returns BoneSpace forces in bone-local space; Apply transforms them once with BoneTM.
	 */
	FVector ConvertExternalForceToSimulationSpace(FAnimNode_KawaiiPhysics& Node,
	                                              FComponentSpacePoseContext& PoseContext,
	                                              const FVector& InForce) const;

	/** Checks if the external force can be applied to a bone (uses the cache built by RefreshBoneFilterCache) */
	bool CanApply(const FKawaiiPhysicsModifyBone& Bone) const;

	/**
	 * フィルタを線形探索で判定する。評価と別スレッドで動くエディタ描画は、作り直し中のキャッシュを読まないようこちらを使う。
	 * Evaluates the filters with a linear search. Editor drawing, which runs apart from the evaluation thread,
	 * uses this so it never reads a cache that is being rebuilt.
	 */
	bool EvaluateBoneFilters(const FKawaiiPhysicsModifyBone& Bone) const;

#if ENABLE_ANIM_DEBUG
	/**
	 * BoneForceMap のキーを返す。ダミーボーンは名前が無いため index で区別する。
	 * Returns the BoneForceMap key. Dummy bones have no name, so they are told apart by index.
	 */
	static FName GetBoneForceKey(const FKawaiiPhysicsModifyBone& Bone);
#endif

private:
	friend struct FKawaiiPhysicsExternalForcePostApply;
	friend struct FKawaiiPhysicsTestAccessor;

	// Runtime-only: the node destroys a one-shot after the complete virtual callback returns.
	bool bRemovalRequestedAfterPostApply = false;

	// 実行時のみのキャッシュ（シリアライズしない） / Runtime-only cache, not serialized
	TArray<FName> BoneFilterCacheBoneNames;
	TArray<FName> BoneFilterCacheApplyNames;
	TArray<FName> BoneFilterCacheIgnoreNames;
	TBitArray<> BoneFilterCacheResults;
	bool bBoneFilterCacheValid = false;
};
