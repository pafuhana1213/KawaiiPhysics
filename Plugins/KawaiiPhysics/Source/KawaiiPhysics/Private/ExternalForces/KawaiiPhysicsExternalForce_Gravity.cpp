// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_Gravity)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Gravity_Apply"), STAT_KawaiiPhysics_ExternalForce_Gravity_Apply,
                   STATGROUP_Anim);

void FKawaiiPhysics_ExternalForce_Gravity::Initialize(const FAnimationInitializeContext& Context)
{
	Super::Initialize(Context);

	OwnerCharacter = Cast<ACharacter>(Context.AnimInstanceProxy->GetSkelMeshComponent()->GetOwner());
}

void FKawaiiPhysics_ExternalForce_Gravity::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                    FComponentSpacePoseContext& PoseContext)
{
	Super::PreApply(Node, PoseContext);

	Force = bUseOverrideGravityDirection ? OverrideGravityDirection.GetSafeNormal() : FVector(0, 0, -1.0f);

	// For Character's Custom Gravity Direction
	if (OwnerCharacter)
	{
#if	ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
		if (bUseCharacterGravityDirection)
		{
			Force = OwnerCharacter->GetGravityDirection();
		}
#endif

		if (bUseCharacterGravityScale)
		{
			if (const UCharacterMovementComponent* CharacterMovementComponent = OwnerCharacter->GetCharacterMovement())
			{
				Force *= CharacterMovementComponent->GetGravityZ();
			}
		}
	}

	Force *= RandomizedForceScale;
	Force = Node.ConvertSimulationSpaceVector(PoseContext, EKawaiiPhysicsSimulationSpace::WorldSpace,
	                                          Node.SimulationSpace, Force);
}

void FKawaiiPhysics_ExternalForce_Gravity::ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone,
                                                           FAnimNode_KawaiiPhysics& Node,
                                                           FComponentSpacePoseContext& PoseContext,
                                                           FVector& InOutVelocity)
{
	if (!CanApply(Bone))
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_Gravity_Apply);

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot, 1.0f);
	}

	InOutVelocity += Force * ForceRate * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, Force * ForceRate);
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}

void FKawaiiPhysics_ExternalForce_Gravity::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                 FComponentSpacePoseContext& PoseContext,
                                                 const FTransform& BoneTM)
{
}
