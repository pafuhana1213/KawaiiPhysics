// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "ExternalForces/KawaiiPhysicsExternalForce.h"

#include "KawaiiPhysicsExternalForce_Wind.generated.h"

///
/// Wind
///
USTRUCT(BlueprintType, DisplayName = "Wind")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Wind : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	FKawaiiPhysics_ExternalForce_Wind()
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
* WindDirectionalSourceによる風方向に与えるノイズ（角度）
* Noise(Degree) of wind by WindDirectionalSource. For use with Cloth and SpeedTree
*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KawaiiPhysics|ExternalForce",
		meta = (Units = "Degrees", ClampMin=0, PinHiddenByDefault))
	float WindDirectionNoiseAngle = 0.0f;

private:
	UPROPERTY()
	TObjectPtr<UWorld> World;

public:
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};
