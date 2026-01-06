// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "ExternalForces/KawaiiPhysicsExternalForce.h"

#include "KawaiiPhysicsExternalForce_Basic.generated.h"

///
/// Basic
///
USTRUCT(BlueprintType, DisplayName = "Basic")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Basic : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	/** Direction of the force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	FVector ForceDir = FVector::Zero();

	/**
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/** Interval for applying the force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	float Interval = 0.0f;

	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;

private:
	/** Current time */
	UPROPERTY()
	float Time = 0.0f;

	/** Previous time */
	UPROPERTY()
	float PrevTime = 0.0f;
};
