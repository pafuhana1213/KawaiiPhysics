// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "ExternalForces/KawaiiPhysicsExternalForce.h"

#include "KawaiiPhysicsExternalForce_Gravity.generated.h"

///
/// Gravity
///
USTRUCT(BlueprintType, DisplayName = "Gravity")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Gravity : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	FKawaiiPhysics_ExternalForce_Gravity()
	{
		bCanSelectForceSpace = false;
		ExternalForceSpace = EExternalForceSpace::WorldSpace;
	}

	/**
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/**
	* Character側で設定されたCustomGravityDirectionを使用するフラグ(UE5.4以降)
	* Flag to use CustomGravityDirection set on the Character side (UE5.4 and later)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	bool bUseCharacterGravityDirection = false;

	/**
	* Character側で設定されたGravityScaleを使用するフラグ
	* Flag to use GravityScale set on the Character side
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	bool bUseCharacterGravityScale = false;

	/**
	 * Direction to override the gravity.
	 * This direction is used when bUseOverrideGravityDirection is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta = (EditCondition = "bUseOverrideGravityDirection"), Category="KawaiiPhysics|ExternalForce")
	FVector OverrideGravityDirection = FVector::Zero();

	/**
	 * Flag to determine whether to use the override gravity direction.
	 * If true, the gravity direction will be overridden by OverrideGravityDirection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (InlineEditConditionToggle),
		Category="KawaiiPhysics|ExternalForce")
	bool bUseOverrideGravityDirection = false;

private:
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

protected:
	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override;
	virtual void ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                             FComponentSpacePoseContext& PoseContext,
	                             FVector& InOutVelocity) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};
