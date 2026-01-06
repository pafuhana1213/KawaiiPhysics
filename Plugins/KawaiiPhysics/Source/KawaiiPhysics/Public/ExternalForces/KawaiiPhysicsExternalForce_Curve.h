// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "ExternalForces/KawaiiPhysicsExternalForce.h"

#include "KawaiiPhysicsExternalForce_Curve.generated.h"

///
/// Curve
///
USTRUCT(BlueprintType, DisplayName = "Curve")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Curve : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	/**
	* 時間に応じて変化する外力をカーブで設定。X軸:Time Y軸:Force
	* Set the external force that changes over time using a curve. X-axis: Time Y-axis: Force
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (XAxisName="Time", YAxisName="Force"),
		Category="KawaiiPhysics|ExternalForce")
	FRuntimeVectorCurve ForceCurve;

	/**
	* カーブの評価方式。
	* Single以外に設定した場合：前フレームからの経過時間をSubstepCountで分割し、
	* 分割後の各時間におけるカーブの値の平均・最大値・最小値を外力として使用
	* Curve evaluation method
	* If set to anything other than Single: The time elapsed from the previous frame is divided by SubstepCount,
	* and the Average, Maximum, or Minimum values of the curve at each time point after division are used as external forces.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	EExternalForceCurveEvaluateType CurveEvaluateType = EExternalForceCurveEvaluateType::Single;

	/**
	* 経過時間の分割数
	* Number of divisions of elapsed time
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta=(EditCondition="CurveEvaluateType != EExternalForceCurveEvaluateType::Single"),
		Category="KawaiiPhysics|ExternalForce")
	int SubstepCount = 10;

	/**
	 * Scale factor for the time.
	 * This value is used to scale the time for the external force.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category ="KawaiiPhysics|ExternalForce")
	float TimeScale = 1.0f;

	/**
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

private:
	UPROPERTY()
	float Time = 0.0f;

	UPROPERTY()
	float PrevTime = 0.0f;

	UPROPERTY()
	float MaxCurveTime = 0.0f;

public:
	void InitMaxCurveTime();

	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};
