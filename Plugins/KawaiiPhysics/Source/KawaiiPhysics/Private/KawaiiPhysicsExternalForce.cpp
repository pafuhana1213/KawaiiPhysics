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
	if (IsDebugEnabled() && !Force.IsZero())
	{
		const auto AnimInstanceProxy = PoseContext.AnimInstanceProxy;
		const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
			Bone.Location);

		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ModifyRootBoneLocationWS + DebugArrowOffset,
			ModifyRootBoneLocationWS + DebugArrowOffset + Force.GetSafeNormal() * DebugArrowLength,
			DebugArrowSize, FColor::Red, false, 0.f, 2);
	}
#endif
}

void FKawaiiPhysics_ExternalForce_Simple::AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
                                                                   const FAnimNode_KawaiiPhysics& Node,
                                                                   FPrimitiveDrawInterface* PDI)
{
	if (bDrawDebug && bIsEnabled && CanApply(ModifyBone) && !Force.IsZero())
	{
		const FTransform ArrowTransform = FTransform(Force.GetSafeNormal().ToOrientationRotator(),
		                                             ModifyBone.Location + DebugArrowOffset);
		DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
		                     SDPG_Foreground, 1.0f);
	}
}

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
	if (!CanApply(Bone))
	{
		return;
	}

	Bone.Location += 0.5f * Gravity * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	if (IsDebugEnabled() && !Gravity.IsZero())
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
	if (bDrawDebug && bIsEnabled && CanApply(ModifyBone) & !Gravity.IsZero())
	{
		const FTransform ArrowTransform = FTransform(Gravity.GetSafeNormal().ToOrientationRotator(),
		                                             ModifyBone.Location + DebugArrowOffset);
		DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
		                     SDPG_Foreground, 1.0f);
	}
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
	if (IsDebugEnabled() && !Force.IsZero())
	{
		const auto AnimInstanceProxy = PoseContext.AnimInstanceProxy;
		const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
			Bone.Location);

		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ModifyRootBoneLocationWS + DebugArrowOffset,
			ModifyRootBoneLocationWS + DebugArrowOffset + Force.GetSafeNormal() * DebugArrowLength,
			DebugArrowSize, FColor::Red, false, 0.f, 2);
	}
#endif
}

void FKawaiiPhysics_ExternalForce_Curve::AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
                                                                  const FAnimNode_KawaiiPhysics& Node,
                                                                  FPrimitiveDrawInterface* PDI)
{
	if (bDrawDebug && bIsEnabled && CanApply(ModifyBone) && !Force.IsZero())
	{
		const FTransform ArrowTransform = FTransform(Force.GetSafeNormal().ToOrientationRotator(),
		                                             ModifyBone.Location + DebugArrowOffset);
		DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
		                     SDPG_Foreground, 1.0f);
	}
}
#endif
