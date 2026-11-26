// Copyright 2019-2026 pafuhana1213. All Rights Reserved.


#include "ExternalForces/KawaiiPhysicsExternalForce_Basic.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_Basic)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Basic_Apply"), STAT_KawaiiPhysics_ExternalForce_Basic_Apply,
                   STATGROUP_Anim);

void FKawaiiPhysics_ExternalForce_Basic::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                  FComponentSpacePoseContext& PoseContext)
{
	Super::PreApply(Node, PoseContext);

	PrevTime = Time;
	Time += Node.GetStepDeltaTime();
	if (Interval > 0.0f)
	{
		if (Time > Interval)
		{
			Force = ForceDir * RandomizedForceScale;
			Time = FMath::Fmod(Time, Interval);
		}
		else
		{
			Force = FVector::ZeroVector;
		}
	}
	else
	{
		Force = ForceDir * RandomizedForceScale;
	}

	// BoneSpace はボーンローカルの生ベクトルをキャッシュし、Apply 側で BoneTM を1回だけ掛ける
	ForceInComponentSpace = ConvertExternalForceToComponentSpace(Node, PoseContext, Force);
	Force = ConvertExternalForceToSimulationSpace(Node, PoseContext, Force);
	ForceInSimSpaceAtPreApply = Force;
	ForcePreApplySerial = Node.GetExternalForcePreApplySerial();
}

void FKawaiiPhysics_ExternalForce_Basic::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                               FComponentSpacePoseContext& PoseContext,
                                               const FTransform& BoneTM)
{
	if (!CanApply(Bone))
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_Basic_Apply);

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot);
	}

	if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		const FVector BoneForce = BoneTM.TransformVector(Force);
		Bone.Location += BoneForce * ForceRate * Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(GetBoneForceKey(Bone), BoneForce);
#endif
	}
	else
	{
		// この評価で未準備、または派生が Force を置き換えた場合は、従来の sim 空間の値を使う。
		const FVector StepForce = Node.IsUsingBaseBoneStepTransforms() && ForcePreApplySerial == Node.GetExternalForcePreApplySerial()
			&& FMemory::Memcmp(&Force, &ForceInSimSpaceAtPreApply, sizeof(FVector)) == 0
			? Node.ConvertComponentVectorToSimulationSpace(PoseContext, ForceInComponentSpace) : Force;
		Bone.Location += StepForce * ForceRate * Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(GetBoneForceKey(Bone), StepForce * ForceRate);
#endif
	}
}
