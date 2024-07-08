#include "KawaiiPhysicsCustomExternalForce.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UKawaiiPhysics_CustomExternalForce_Gravity::PreApply_Implementation(FAnimNode_KawaiiPhysics& Node,
                                                                         const USkeletalMeshComponent* SkelComp)
{
	if (!bIsEnabled)
	{
		return;
	}

	Gravity = bUseOverrideGravityDirection ? OverrideGravityDirection : FVector(0, 0, -1.0f);

	if (bUseCharacterGravity)
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
}

bool UKawaiiPhysics_CustomExternalForce_Gravity::Apply_Implementation(int32 ModifyBoneIndex,
                                                                      FAnimNode_KawaiiPhysics& Node,
                                                                      const USkeletalMeshComponent* SkelComp)
{
	Node.ModifyBones[ModifyBoneIndex].Location += 0.5 * Gravity * Node.DeltaTime * Node.
		DeltaTime;

	return true;
}

#if ENABLE_ANIM_DEBUG
void UKawaiiPhysics_CustomExternalForce_Gravity::AnimDrawDebug(const FKawaiiPhysicsModifyBone& ModifyBone,
                                                               const FAnimNode_KawaiiPhysics& Node,
                                                               const FComponentSpacePoseContext& PoseContext)
{
	if (IsDebugEnabled())
	{
		if (FAnimInstanceProxy* AnimInstanceProxy = PoseContext.AnimInstanceProxy)
		{
			const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
				ModifyBone.Location);

			AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
				ModifyRootBoneLocationWS + DebugArrowOffset,
				ModifyRootBoneLocationWS + DebugArrowOffset + Gravity.GetSafeNormal() * DebugArrowLength,
				DebugArrowSize, FColor::Red, false, 0.f, 2);
		}
	}
}
#endif

#if WITH_EDITOR
void UKawaiiPhysics_CustomExternalForce_Gravity::AnimDrawDebugForEditMode(
	const FKawaiiPhysicsModifyBone& ModifyBone, const FAnimNode_KawaiiPhysics& Node,
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

void UKawaiiPhysics_CustomExternalForce_Curve::PreApply_Implementation(FAnimNode_KawaiiPhysics& Node,
                                                                       const USkeletalMeshComponent* SkelComp)
{
	Time += Node.DeltaTime * TimeScale;

	float MaxTime = 0.0f;
	if (const FRichCurve* CurveX = ForceCurve.GetRichCurve(0))
	{
		MaxTime = FMath::Max(MaxTime, CurveX->GetLastKey().Time);
	}
	if (const FRichCurve* CurveY = ForceCurve.GetRichCurve(1))
	{
		MaxTime = FMath::Max(MaxTime, CurveY->GetLastKey().Time);
	}
	if (const FRichCurve* CurveZ = ForceCurve.GetRichCurve(2))
	{
		MaxTime = FMath::Max(MaxTime, CurveZ->GetLastKey().Time);
	}

	if (MaxTime > 0 && Time > MaxTime)
	{
		Time -= MaxTime;
	}
}

bool UKawaiiPhysics_CustomExternalForce_Curve::Apply_Implementation(int32 ModifyBoneIndex,
                                                                    FAnimNode_KawaiiPhysics& Node,
                                                                    const USkeletalMeshComponent* SkelComp)
{
	float Rate = 1.0f;
	if (!ForceRateCurve.GetRichCurve()->IsEmpty())
	{
		Rate = ForceRateCurve.GetRichCurve()->Eval(
			Node.ModifyBones[ModifyBoneIndex].LengthFromRoot / Node.GetTotalBoneLength());
	}

	Node.ModifyBones[ModifyBoneIndex].Location += ForceCurve.GetValue(Time) * Rate * ForceScale * Node.DeltaTime;

	return true;
}

#if ENABLE_ANIM_DEBUG
void UKawaiiPhysics_CustomExternalForce_Curve::AnimDrawDebug(const FKawaiiPhysicsModifyBone& ModifyBone,
                                                             const FAnimNode_KawaiiPhysics& Node,
                                                             const FComponentSpacePoseContext& PoseContext)
{
	if (IsDebugEnabled())
	{
		if (FAnimInstanceProxy* AnimInstanceProxy = PoseContext.AnimInstanceProxy)
		{
			float Rate = 1.0f;
			if (!ForceRateCurve.GetRichCurve()->IsEmpty())
			{
				Rate = ForceRateCurve.GetRichCurve()->Eval(ModifyBone.LengthFromRoot / Node.GetTotalBoneLength());
			}

			FVector Force = ForceCurve.GetValue(Time) * Rate * ForceScale * Node.DeltaTime;
			if (Force.Length() > 0)
			{
				const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().
				                                                            TransformPosition(
					                                                            ModifyBone.Location);

				AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
					ModifyRootBoneLocationWS + DebugArrowOffset,
					ModifyRootBoneLocationWS + DebugArrowOffset + Force * DebugArrowLength,
					DebugArrowSize, FColor::Red, false, 0.f, 2);
			}
		}
	}
}
#endif


#if WITH_EDITOR
void UKawaiiPhysics_CustomExternalForce_Curve::AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
                                                                        const FAnimNode_KawaiiPhysics& Node,
                                                                        FPrimitiveDrawInterface* PDI)
{
	if (bDrawDebug && bIsEnabled)
	{
		float Rate = 1.0f;
		if (!ForceRateCurve.GetRichCurve()->IsEmpty())
		{
			Rate = ForceRateCurve.GetRichCurve()->Eval(ModifyBone.LengthFromRoot / Node.GetTotalBoneLength());
		}

		FVector Force = ForceCurve.GetValue(Time) * Rate * ForceScale * Node.DeltaTime;
		if (Force.Length() > 0)
		{
			const FTransform ArrowTransform = FTransform(Force.GetSafeNormal().ToOrientationRotator(),
			                                             ModifyBone.Location + DebugArrowOffset);
			DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red,
			                     Force.Length() * DebugArrowLength,
			                     DebugArrowSize,
			                     SDPG_Foreground, 1.0f);
		}
	}
}
#endif
