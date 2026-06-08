// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_Wind.h"

#include "SceneInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_Wind)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Wind_Apply"), STAT_KawaiiPhysics_ExternalForce_Wind_Apply,
                   STATGROUP_Anim);

void FKawaiiPhysics_ExternalForce_Wind::PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext)
{
	Super::PreApply(Node, PoseContext);

	// SkeletalMeshComponentがnullの場合のクラッシュを回避 / Avoid crash when the component is null
	const USkeletalMeshComponent* SkelComp = PoseContext.AnimInstanceProxy->GetSkelMeshComponent();
	World = SkelComp ? SkelComp->GetWorld() : nullptr;

	// 風パラメータ（Scene問い合わせ）はフレーム1回だけ取得してキャッシュする。固定サブステップ時に
	// ワーカースレッドから毎ステップ Scene を触らないため（§7-E）。サブステップ間は同じ風入力を使い回す。
	// Fetch wind parameters (a Scene query) once per frame and cache them, so the worker thread does not
	// touch the Scene every substep (§7-E). The same wind input is reused across substeps.
	const int32 NumBones = Node.ModifyBones.Num();
	CachedWindDirection.Reset();
	CachedWindSpeed.Reset();

	const FSceneInterface* Scene = World && World->Scene ? World->Scene : nullptr;
	if (!Scene)
	{
		return;
	}

	// 添字でアクセスするため ModifyBones と同数で確保。未適用ボーンは Speed=負値のままにする。
	// Sized to ModifyBones for index access; non-applicable bones keep Speed = negative.
	CachedWindDirection.SetNumZeroed(NumBones);
	CachedWindSpeed.Init(-1.0f, NumBones);

	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FKawaiiPhysicsModifyBone& Bone = Node.ModifyBones[BoneIndex];
		if (!CanApply(Bone))
		{
			continue;
		}

		FVector WindDirection = FVector::ZeroVector;
		float WindSpeed = 0.0f, WindMinGust = 0.0f, WindMaxGust = 0.0f;
		Scene->GetWindParameters(Node.ConvertSimulationSpaceVector(PoseContext, Node.SimulationSpace,
			                         EKawaiiPhysicsSimulationSpace::WorldSpace, Bone.PoseLocation),
		                         WindDirection, WindSpeed, WindMinGust, WindMaxGust);

		// SimulationSpace の風向きと風速を分けて保存（ノイズ・ForceRate・dt は Apply で毎ステップ適用）。
		// 方向と速度を分離することで、元の VRandCone(dir)*speed をサブステップ毎に忠実に再現する。
		// Store the SimulationSpace direction and the speed separately (noise / ForceRate / dt are applied per step in Apply).
		// Keeping them separate reproduces the original VRandCone(dir)*speed faithfully each substep.
		CachedWindDirection[BoneIndex] = Node.ConvertSimulationSpaceVector(PoseContext,
			EKawaiiPhysicsSimulationSpace::WorldSpace, Node.SimulationSpace, WindDirection);
		CachedWindSpeed[BoneIndex] = WindSpeed;
	}
}

void FKawaiiPhysics_ExternalForce_Wind::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                              FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	// PreApply でキャッシュ済みの風入力を Bone.Index で参照（Scene には触らない / §7-E）。
	// 負の風速＝このボーンには未適用（CanApply不可 / Scene無効）、または風速ゼロ → スキップ。
	// Look up the wind input cached in PreApply by Bone.Index (do not touch the Scene here / §7-E).
	// Negative speed = not applicable to this bone (CanApply false / no Scene), or zero wind → skip.
	if (!CachedWindSpeed.IsValidIndex(Bone.Index))
	{
		return;
	}
	const float WindSpeed = CachedWindSpeed[Bone.Index];
	if (WindSpeed <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_Wind_Apply);

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot);
	}

	// 方向ノイズはサブステップ毎に適用し、その後に風速を乗算（元コードの VRandCone(dir)*speed と同一）。
	// Scene 問い合わせは PreApply で済ませてある。
	// Apply directional noise per substep, then multiply by speed (identical to the original VRandCone(dir)*speed).
	// The Scene query was already done in PreApply.
	FVector WindDirection = FMath::VRandCone(CachedWindDirection[Bone.Index],
	                                         FMath::DegreesToRadians(WindDirectionNoiseAngle));
	WindDirection *= WindSpeed;
	Bone.Location += WindDirection * ForceRate * RandomizedForceScale * Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, WindDirection * ForceRate * RandomizedForceScale);
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}
