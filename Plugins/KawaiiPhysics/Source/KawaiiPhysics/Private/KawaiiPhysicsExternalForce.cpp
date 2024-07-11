#include "KawaiiPhysicsExternalForce.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"


void FKawaiiPhysics_ExternalForce_Gravity::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                    const USkeletalMeshComponent* SkelComp)
{
	Gravity = bUseOverrideGravityDirection ? OverrideGravityDirection : FVector(0, 0, -1.0f);

	// For Character's Custom Gravity Direction
	if (const ACharacter* Character = Cast<ACharacter>(SkelComp->GetOwner()))
	{
		if (bUseCharacterGravityDirection)
		{
			Gravity = Character->GetGravityDirection();
		}

		if (bUseCharacterGravityScale)
		{
			if (const UCharacterMovementComponent* CharacterMovementComponent = Character->
				GetCharacterMovement())
			{
				Gravity *= CharacterMovementComponent->GetGravityZ();
			}
		}
	}

	Gravity *= GravityScale;

	const FTransform ComponentTransform = SkelComp->GetComponentTransform();
	Gravity = ComponentTransform.InverseTransformVector(Gravity);
}

void FKawaiiPhysics_ExternalForce_Gravity::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                 const FComponentSpacePoseContext& PoseContext)
{
	Bone.Location += 0.5 * Gravity * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	if (IsDebugEnabled())
	{
		const auto AnimInstanceProxy = PoseContext.AnimInstanceProxy;
		const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
			Bone.Location);

		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ModifyRootBoneLocationWS + DebugArrowOffset,
			ModifyRootBoneLocationWS + DebugArrowOffset + Gravity.GetSafeNormal() * DebugArrowLength,
			DebugArrowSize, FColor::Red, false, 0.f, 2);
	}
#endif
}

#if WITH_EDITOR
void FKawaiiPhysics_ExternalForce_Gravity::AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
                                                                    const FAnimNode_KawaiiPhysics& Node,
                                                                    FPrimitiveDrawInterface* PDI)
{
	if (bDrawDebug && bIsEnabled)
	{
		const FTransform ArrowTransform = FTransform(Gravity.GetSafeNormal().ToOrientationRotator(),
		                                             ModifyBone.Location + DebugArrowOffset);
		DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
		                     SDPG_Foreground, 1.0f);
	}
}
#endif
