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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/**
	* Character側で設定されたCustomGravityDirectionを使用するフラグ(UE5.3以降)。OverrideがONならOverrideを優先する。
	* Flag to use CustomGravityDirection set on the Character side (UE5.3 and later). Override takes priority when enabled.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce", meta=(EditCondition="!bUseOverrideGravityDirection"))
	bool bUseCharacterGravityDirection = false;

	/**
	* Characterの重力加速度（ワールドの重力 × GravityScale）を力の大きさに使うフラグ。Characterが無ければワールドの重力で代用する。
	* Flag to use the Character's gravity acceleration (world gravity × GravityScale) as the force magnitude. Uses world gravity when no Character exists.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce", meta=(DisplayName="Use Character Gravity"))
	bool bUseCharacterGravityScale = false;

	/**
	 * 重力方向を上書きするベクトル。
	 * bUseOverrideGravityDirectionが有効な場合に使用。
	 * Direction to override the gravity.
	 * This direction is used when bUseOverrideGravityDirection is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta = (EditCondition = "bUseOverrideGravityDirection"), Category="Kawaii Physics|ExternalForce")
	FVector OverrideGravityDirection = FVector::Zero();

	/**
	 * 重力方向の上書きを使用するフラグ。
	 * 有効な場合、重力方向をOverrideGravityDirectionで上書きする。
	 * Flag to determine whether to use the override gravity direction.
	 * If true, the gravity direction will be overridden by OverrideGravityDirection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (InlineEditConditionToggle),
		Category="Kawaii Physics|ExternalForce")
	bool bUseOverrideGravityDirection = false;

private:
	FVector ForceInComponentSpace = FVector::ZeroVector;
	FVector ForceInSimSpaceAtPreApply = FVector::ZeroVector;
	// 現在の評価で組み込みの PreApply を通ったキャッシュだけを使う。0 は未準備。
	uint32 ForcePreApplySerial = 0;

	/**
	 * Initialize で取得し直す実行時の参照。保存しない（Transient）。一時外力の保存領域は GC の追跡対象外なので、
	 * 強参照ではなく弱参照で持つ
	 * Runtime reference re-acquired in Initialize; not saved (Transient). The transient force store is not tracked
	 * by GC, so this is a weak reference instead of a strong one
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACharacter> OwnerCharacter;

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
