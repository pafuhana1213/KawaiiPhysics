// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_Wind.h"

#include "SceneInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_Wind)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Wind_Apply"), STAT_KawaiiPhysics_ExternalForce_Wind_Apply,
                   STATGROUP_Anim);

void FKawaiiPhysics_ExternalForce_Wind::PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp)
{
	Super::PreApply(Node, SkelComp);

	World = SkelComp ? SkelComp->GetWorld() : nullptr;
}

void FKawaiiPhysics_ExternalForce_Wind::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                              const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	const FSceneInterface* Scene = World && World->Scene ? World->Scene : nullptr;
	if (!CanApply(Bone) || !Scene)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_Wind_Apply);

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot);
	}

	FVector WindDirection = FVector::ZeroVector;
	float WindSpeed, WindMinGust, WindMaxGust = 0.0f;
	Scene->GetWindParameters(ComponentTransform.TransformPosition(Bone.PoseLocation), WindDirection,
	                         WindSpeed, WindMinGust, WindMaxGust);

	if (Node.SimulationSpace != EKawaiiPhysicsSimulationSpace::WorldSpace)
	{
		WindDirection = ComponentTransform.InverseTransformVector(WindDirection);
	}
	WindDirection *= WindSpeed;

	Bone.Location += WindDirection * ForceRate * RandomizedForceScale * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, WindDirection * ForceRate * RandomizedForceScale);
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}
