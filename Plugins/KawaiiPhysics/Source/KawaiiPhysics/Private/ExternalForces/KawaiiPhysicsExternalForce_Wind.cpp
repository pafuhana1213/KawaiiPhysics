// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_Wind.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_Wind)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Wind_Apply"), STAT_KawaiiPhysics_ExternalForce_Wind_Apply,
                   STATGROUP_Anim);

void FKawaiiPhysics_ExternalForce_Wind::PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext)
{
	Super::PreApply(Node, PoseContext);

	// 風パラメータはフレーム1回だけ取得してキャッシュし、サブステップ間は同じ風入力を使い回す。
	// Scene の風源はワーカースレッドから読まず、ノードが PreUpdate（GameThread）で取得した値を使う。
	const int32 NumBones = Node.ModifyBones.Num();
	CachedWindDirection.Reset();
	CachedWindSpeed.Reset();

	// 添字でアクセスするため ModifyBones と同数で確保。未適用ボーンは Speed=負値のままにする。
	CachedWindDirection.SetNumZeroed(NumBones);
	CachedWindSpeed.Init(-1.0f, NumBones);

	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FKawaiiPhysicsModifyBone& Bone = Node.ModifyBones[BoneIndex];
		if (!CanApply(Bone))
		{
			continue;
		}

		// ノードの WindSampleMode に従い、ボーンごと、またはノード中心で取得した風を使う
		FVector WindDirection = FVector::ZeroVector;
		float WindSpeed = 0.0f;
		if (!Node.GetWindSample(BoneIndex, WindDirection, WindSpeed))
		{
			continue;
		}

		// SimulationSpace の風向きと風速を分けて保存。ForceRate・dt は Apply で毎ステップ適用。
		// 方向ノイズ(VRandCone)はフレーム頭で1回だけここで適用し、サブステップ間で同一値を使う
		// （ノイズ分散が NumStep＝フレームレートに依存しないようにする。
		const FVector SimSpaceDir = Node.ConvertSimulationSpaceVector(PoseContext,
			EKawaiiPhysicsSimulationSpace::WorldSpace, Node.SimulationSpace, WindDirection);
		CachedWindDirection[BoneIndex] = (WindDirectionNoiseAngle > 0)
			                                 ? FMath::VRandCone(
				                                 SimSpaceDir, FMath::DegreesToRadians(WindDirectionNoiseAngle))
			                                 : SimSpaceDir;
		CachedWindSpeed[BoneIndex] = WindSpeed;
	}
}

void FKawaiiPhysics_ExternalForce_Wind::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                              FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	// PreApply でキャッシュ済みの風入力を Bone.Index で参照（Scene には触らない / §7-E）。
	// 負の風速＝このボーンには未適用（CanApply不可 / Scene無効）、または風速ゼロ → スキップ。
	const float WindSpeed = CachedWindSpeed.IsValidIndex(Bone.Index) ? CachedWindSpeed[Bone.Index] : 0.0f;
	if (WindSpeed <= KINDA_SMALL_NUMBER)
	{
#if ENABLE_ANIM_DEBUG
		// 風が止まった後に古い矢印を残さない
		BoneForceMap.Remove(GetBoneForceKey(Bone));
#endif
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_Wind_Apply);

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot);
	}

	// 方向ノイズは PreApply でフレーム単位に適用済み。ここでは風速のみ乗算する。
	// Scene 問い合わせ・乱数をサブステップに依存させない（フレームレート非依存）。
	const FVector WindDirection = CachedWindDirection[Bone.Index] * WindSpeed;
	Bone.Location += WindDirection * ForceRate * RandomizedForceScale * Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(GetBoneForceKey(Bone), WindDirection * ForceRate * RandomizedForceScale);
#endif
}
