#include "KawaiiPhysicsExternalForce.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"


void FKawaiiPhysics_ExternalForce_Simple::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                   const USkeletalMeshComponent* SkelComp)
{
	Force = ForceDir * ForceScale;

	const FTransform ComponentTransform = SkelComp->GetComponentTransform();
	Force = ComponentTransform.InverseTransformVector(Force);
}

void FKawaiiPhysics_ExternalForce_Simple::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                const FComponentSpacePoseContext& PoseContext)
{
	if (!CanApply(Bone))
	{
		return;
	}

	Bone.Location += Force * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}

void FKawaiiPhysics_ExternalForce_Gravity::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                    const USkeletalMeshComponent* SkelComp)
{
	Force = bUseOverrideGravityDirection ? OverrideGravityDirection : FVector(0, 0, -1.0f);

	// For Character's Custom Gravity Direction
	if (const ACharacter* Character = Cast<ACharacter>(SkelComp->GetOwner()))
	{
		if (bUseCharacterGravityDirection)
		{
			Force = Character->GetGravityDirection();
		}

		if (bUseCharacterGravityScale)
		{
			if (const UCharacterMovementComponent* CharacterMovementComponent = Character->
				GetCharacterMovement())
			{
				Force *= CharacterMovementComponent->GetGravityZ();
			}
		}
	}

	Force *= GravityScale;

	const FTransform ComponentTransform = SkelComp->GetComponentTransform();
	Force = ComponentTransform.InverseTransformVector(Force);
}

void FKawaiiPhysics_ExternalForce_Gravity::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                 const FComponentSpacePoseContext& PoseContext)
{
	if (!CanApply(Bone))
	{
		return;
	}

	Bone.Location += 0.5f * Force * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}


void FKawaiiPhysics_ExternalForce_Curve::PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp)
{
	Time += Node.DeltaTime * TimeScale;

	float MaxTime = 0.0f;
	if (const FRichCurve* CurveX = ForceCurve.GetRichCurve(0); CurveX && !CurveX->IsEmpty())
	{
		MaxTime = FMath::Max(MaxTime, CurveX->GetLastKey().Time);
	}
	if (const FRichCurve* CurveY = ForceCurve.GetRichCurve(1); CurveY && !CurveY->IsEmpty())
	{
		MaxTime = FMath::Max(MaxTime, CurveY->GetLastKey().Time);
	}
	if (const FRichCurve* CurveZ = ForceCurve.GetRichCurve(2); CurveZ && !CurveZ->IsEmpty())
	{
		MaxTime = FMath::Max(MaxTime, CurveZ->GetLastKey().Time);
	}

	if (MaxTime > 0 && Time > MaxTime)
	{
		Time -= MaxTime;
	}

	Force = ForceCurve.GetValue(Time) * ForceScale;

	const FTransform ComponentTransform = SkelComp->GetComponentTransform();
	Force = ComponentTransform.InverseTransformVector(Force);
}

void FKawaiiPhysics_ExternalForce_Curve::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                               const FComponentSpacePoseContext& PoseContext)
{
	if (!CanApply(Bone))
	{
		return;
	}

	Bone.Location += Force * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}
