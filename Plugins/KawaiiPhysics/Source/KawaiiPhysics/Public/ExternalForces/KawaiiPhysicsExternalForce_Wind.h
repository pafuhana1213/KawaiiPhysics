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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;
	
	/** 
* WindDirectionalSourceによる風方向に与えるノイズ（角度）
* Noise(Degree) of wind by WindDirectionalSource. For use with Cloth and SpeedTree
*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|ExternalForce",
		meta = (Units = "Degrees", ClampMin=0, PinHiddenByDefault))
	float WindDirectionNoiseAngle = 0.0f;

private:
	UPROPERTY()
	TObjectPtr<UWorld> World;

	/**
	* 風パラメータ（Scene 問い合わせ=game-thread 状態）をフレーム1回だけキャッシュし、ワーカースレッドから毎ステップ Scene を触らない（§7-E / FixedSubstepping.md）。
	* キーは ModifyBones の添字（ダミーボーンは BoneRef が空=NAME_None で衝突するため、ボーン名は使わない）。
	* Per-frame cache of wind params (a Scene query = game-thread state) so the worker thread never touches the Scene per substep (§7-E / FixedSubstepping.md).
	* Keyed by ModifyBones index (dummy bones have an empty BoneRef = NAME_None, which would collide, so bone name can't be the key).
	*/
	// SimulationSpace の風向き（VRandConeノイズ・風速乗算の前） / Wind direction in SimulationSpace (before VRandCone noise & speed multiply)
	TArray<FVector> CachedWindDirection;
	// 風速スカラー。負値＝このボーンには未適用（CanApply不可 / Scene無効） / Wind speed scalar; negative = not applicable to this bone (CanApply false / no Scene)
	TArray<float> CachedWindSpeed;

public:
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};
