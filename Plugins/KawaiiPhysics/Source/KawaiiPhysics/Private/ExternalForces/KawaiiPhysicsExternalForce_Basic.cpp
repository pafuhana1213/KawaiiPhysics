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
	Time += Node.DeltaTime;
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

	// TODO : Merge EExternalForceSpace and EKawaiiPhysicsSimulationSpace
	EKawaiiPhysicsSimulationSpace From = EKawaiiPhysicsSimulationSpace::ComponentSpace;
	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
		From = EKawaiiPhysicsSimulationSpace::WorldSpace;
	}
	else if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		From = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
	}

	Force = Node.ConvertSimulationSpaceVector(PoseContext, From,
	                                          Node.SimulationSpace, Force);
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
		Bone.Location += BoneForce * ForceRate * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce);
#endif
	}
	else
	{
		Bone.Location += Force * ForceRate * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, Force * ForceRate);
#endif
	}
}
