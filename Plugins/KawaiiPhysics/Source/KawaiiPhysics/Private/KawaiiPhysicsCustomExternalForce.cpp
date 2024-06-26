#include "KawaiiPhysicsCustomExternalForce.h"

void UKawaiiPhysics_CustomExternalForce_Simple::Apply_Implementation(FAnimNode_KawaiiPhysics& Node,
                                                                     const USkeletalMeshComponent* SkelComp)
{
	if (!bIsEnabled)
	{
		return;
	}

	for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
	{
		Bone.Location += Force * Node.DeltaTime;
	}
}

#if ENABLE_ANIM_DEBUG
void UKawaiiPhysics_CustomExternalForce_Simple::AnimDrawDebug(const FAnimNode_KawaiiPhysics& Node,
                                                              const FComponentSpacePoseContext& PoseContext)
{
	if (IsDebugEnabled())
	{
		if (FAnimInstanceProxy* AnimInstanceProxy = PoseContext.AnimInstanceProxy)
		{
			const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
				Node.ModifyBones[0].Location);

			AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
				ModifyRootBoneLocationWS + DebugArrowOffset,
				ModifyRootBoneLocationWS + DebugArrowOffset + Force.GetSafeNormal() * DebugArrowLength,
				DebugArrowSize, FColor::Red, false, 0.f, 2);
		}
	}
}
#endif

#if WITH_EDITOR
void UKawaiiPhysics_CustomExternalForce_Simple::AnimDrawDebugForEditMode(const FAnimNode_KawaiiPhysics& Node,
                                                                         FPrimitiveDrawInterface* PDI)
{
	if (bIsEnabled && bDrawDebug)
	{
		const FTransform ArrowTransform = FTransform(Force.GetSafeNormal().ToOrientationRotator(),
		                                             Node.ModifyBones[0].Location + DebugArrowOffset);
		DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
		                     SDPG_Foreground, 1.0f);
	}
}
#endif

void UKawaiiPhysics_CustomExternalForce_Gravity::Apply_Implementation(FAnimNode_KawaiiPhysics& Node,
                                                                      const USkeletalMeshComponent* SkelComp)
{
	if (!bIsEnabled)
	{
		return;
	}

	Gravity = bOverrideGravityDirection ? OverrideGravityDirection : FVector(0, 0, -1.0f);

	if (!bOverrideGravityDirection)
	{
		// For Character's Custom Gravity Direction
		if (const ACharacter* Character = Cast<ACharacter>(SkelComp->GetOwner()))
		{
			Gravity = Character->GetGravityDirection();

			if (const UCharacterMovementComponent* CharacterMovementComponent = Character->GetCharacterMovement())
			{
				Gravity *= CharacterMovementComponent->GetGravityZ();
			}
		}
	}
	Gravity *= GravityScale;

	const FTransform ComponentTransform = SkelComp->GetComponentTransform();
	Gravity = ComponentTransform.InverseTransformVector(Gravity);
	for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
	{
		if (Bone.bSkipSimulate)
		{
			continue;
		}

		Bone.Location += 0.5 * Gravity * Node.DeltaTime * Node.DeltaTime;
	}
}

#if ENABLE_ANIM_DEBUG
void UKawaiiPhysics_CustomExternalForce_Gravity::AnimDrawDebug(const FAnimNode_KawaiiPhysics& Node,
                                                               const FComponentSpacePoseContext& PoseContext)
{
	if (IsDebugEnabled())
	{
		if (FAnimInstanceProxy* AnimInstanceProxy = PoseContext.AnimInstanceProxy)
		{
			const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
				Node.ModifyBones[0].Location);

			AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
				ModifyRootBoneLocationWS + DebugArrowOffset,
				ModifyRootBoneLocationWS + DebugArrowOffset + Gravity.GetSafeNormal() * DebugArrowLength,
				DebugArrowSize, FColor::Red, false, 0.f, 2);
		}
	}
}
#endif

#if WITH_EDITOR
void UKawaiiPhysics_CustomExternalForce_Gravity::AnimDrawDebugForEditMode(const FAnimNode_KawaiiPhysics& Node,
                                                                          FPrimitiveDrawInterface* PDI)
{
	if (bDrawDebug && bIsEnabled)
	{
		const FTransform ArrowTransform = FTransform(Gravity.GetSafeNormal().ToOrientationRotator(),
		                                             Node.ModifyBones[0].Location + DebugArrowOffset);
		DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
		                     SDPG_Foreground, 1.0f);
	}
}
#endif
