// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_Gravity)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Gravity_Apply"), STAT_KawaiiPhysics_ExternalForce_Gravity_Apply,
                   STATGROUP_Anim);

void FKawaiiPhysics_ExternalForce_Gravity::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                    const USkeletalMeshComponent* SkelComp)
{
	Super::PreApply(Node, SkelComp);

	Force = bUseOverrideGravityDirection ? OverrideGravityDirection : FVector(0, 0, -1.0f);

	// For Character's Custom Gravity Direction
	if (const ACharacter* Character = Cast<ACharacter>(SkelComp->GetOwner()))
	{
#if	ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
		if (bUseCharacterGravityDirection)
		{
			Force = Character->GetGravityDirection();
		}
#endif

		if (bUseCharacterGravityScale)
		{
			if (const UCharacterMovementComponent* CharacterMovementComponent = Character->GetCharacterMovement())
			{
				Force *= CharacterMovementComponent->GetGravityZ();
			}
		}
	}

	Force *= RandomizedForceScale;
	if (Node.SimulationSpace != EKawaiiPhysicsSimulationSpace::WorldSpace)
	{
		Force = ComponentTransform.InverseTransformVector(Force);
	}
}

void FKawaiiPhysics_ExternalForce_Gravity::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                 const FComponentSpacePoseContext& PoseContext,
                                                 const FTransform& BoneTM)
{
	if (!CanApply(Bone))
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_Gravity_Apply);

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot);
	}

	Bone.Location += 0.5f * Force * ForceRate * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, Force * ForceRate);
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}
