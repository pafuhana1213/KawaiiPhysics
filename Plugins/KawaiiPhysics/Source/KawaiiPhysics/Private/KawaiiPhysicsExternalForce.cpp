#include "KawaiiPhysicsExternalForce.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"


void FKawaiiPhysics_ExternalForce_Basic::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                  const USkeletalMeshComponent* SkelComp)
{
	Force = ForceDir * ForceScale;

	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
		const FTransform ComponentTransform = SkelComp->GetComponentTransform();
		Force = ComponentTransform.InverseTransformVector(Force);
	}
}

void FKawaiiPhysics_ExternalForce_Basic::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                               const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	if (!CanApply(Bone))
	{
		return;
	}

	if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		const FVector BoneForce = BoneTM.TransformVector(Force);
		Bone.Location += BoneForce * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce);
#endif
	}
	else
	{
		Bone.Location += Force * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, Force);
#endif
	}
}

void FKawaiiPhysics_ExternalForce_Gravity::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                    const USkeletalMeshComponent* SkelComp)
{
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
			if (const UCharacterMovementComponent* CharacterMovementComponent = Character->
				GetCharacterMovement())
			{
				Force *= CharacterMovementComponent->GetGravityZ();
			}
		}
	}

	Force *= GravityScale;

	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
		const FTransform ComponentTransform = SkelComp->GetComponentTransform();
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

	if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		const FVector BoneForce = BoneTM.TransformVector(Force);
		Bone.Location += 0.5f * BoneForce * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce);
#endif
	}
	else
	{
		Bone.Location += 0.5f * Force * Node.DeltaTime * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, Force);
#endif
	}

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, Force);
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}


void FKawaiiPhysics_ExternalForce_Curve::InitMaxCurveTime()
{
	if (const FRichCurve* CurveX = ForceCurve.GetRichCurve(0); CurveX && !CurveX->IsEmpty())
	{
		MaxCurveTime = FMath::Max(MaxCurveTime, CurveX->GetLastKey().Time);
	}
	if (const FRichCurve* CurveY = ForceCurve.GetRichCurve(1); CurveY && !CurveY->IsEmpty())
	{
		MaxCurveTime = FMath::Max(MaxCurveTime, CurveY->GetLastKey().Time);
	}
	if (const FRichCurve* CurveZ = ForceCurve.GetRichCurve(2); CurveZ && !CurveZ->IsEmpty())
	{
		MaxCurveTime = FMath::Max(MaxCurveTime, CurveZ->GetLastKey().Time);
	}
}

void FKawaiiPhysics_ExternalForce_Curve::Initialize(const FAnimationInitializeContext& Context)
{
	FKawaiiPhysics_ExternalForce::Initialize(Context);

	InitMaxCurveTime();
}

void FKawaiiPhysics_ExternalForce_Curve::PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp)
{
#if WITH_EDITOR
	InitMaxCurveTime();
#endif

	PrevTime = Time;

	if (CurveEvaluateType == EExternalForceCurveEvaluateType::Single)
	{
		Time += Node.DeltaTime * TimeScale;
		if (MaxCurveTime > 0 && Time > MaxCurveTime)
		{
			Time = FMath::Fmod(Time, MaxCurveTime);
		}
		Force = ForceCurve.GetValue(Time) * ForceScale;
	}
	else
	{
		TArray<FVector> CurveValues;
		const float SubStep = Node.DeltaTime * TimeScale / 10.0f;

		for (int i = 0; i < 10; ++i)
		{
			Time += SubStep;
			if (MaxCurveTime > 0 && Time > MaxCurveTime)
			{
				Time = FMath::Fmod(Time, MaxCurveTime);
			}
			CurveValues.Add(ForceCurve.GetValue(Time));
		}

		switch (CurveEvaluateType)
		{
		case EExternalForceCurveEvaluateType::Average:
			for (const auto& CurveValue : CurveValues)
			{
				Force += CurveValue;
			}
			Force /= 10.0f;
			break;

		case EExternalForceCurveEvaluateType::Max:
			Force = FVector(FLT_MIN);
			for (const auto& CurveValue : CurveValues)
			{
				Force = FVector::Max(Force, CurveValue);
			}
			break;
		case EExternalForceCurveEvaluateType::Min:
			Force = FVector(FLT_MAX);
			for (const auto& CurveValue : CurveValues)
			{
				Force = FVector::Min(Force, CurveValue);
			}
			break;
		default:
			break;
		}

		Force *= ForceScale;
	}

	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
		const FTransform ComponentTransform = SkelComp->GetComponentTransform();
		Force = ComponentTransform.InverseTransformVector(Force);
	}
}

void FKawaiiPhysics_ExternalForce_Curve::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                               const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
	if (!CanApply(Bone))
	{
		return;
	}


	if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		const FVector BoneForce = BoneTM.TransformVector(Force);
		Bone.Location += BoneForce * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce);
#endif
	}
	else
	{
		Bone.Location += Force * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, Force);
#endif
	}

#if ENABLE_ANIM_DEBUG
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}
