#include "KawaiiPhysicsExternalForce.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Basic_Apply"), STAT_KawaiiPhysics_ExternalForce_Basic_Apply,
                   STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Gravity_Apply"), STAT_KawaiiPhysics_ExternalForce_Gravity_Apply,
                   STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Curve_Apply"), STAT_KawaiiPhysics_ExternalForce_Curve_Apply,
                   STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Wind_Apply"), STAT_KawaiiPhysics_ExternalForce_Wind_Apply,
                   STATGROUP_Anim);

///
/// Basic
///
void FKawaiiPhysics_ExternalForce_Basic::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                  const USkeletalMeshComponent* SkelComp)
{
	Super::PreApply(Node, SkelComp);

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

	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
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

///
/// Gravity
///
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
			if (const UCharacterMovementComponent* CharacterMovementComponent = Character->
				GetCharacterMovement())
			{
				Force *= CharacterMovementComponent->GetGravityZ();
			}
		}
	}

	Force *= RandomizedForceScale;
	Force = ComponentTransform.InverseTransformVector(Force);
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

///
/// Curve
///
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
	Super::PreApply(Node, SkelComp);

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
		Force = ForceCurve.GetValue(Time) * RandomizedForceScale;
	}
	else
	{
		TArray<FVector> CurveValues;
		const float SubStep = Node.DeltaTime * TimeScale / SubstepCount;

		for (int i = 0; i < SubstepCount; ++i)
		{
			Time += SubStep;
			if (MaxCurveTime > 0 && Time > MaxCurveTime)
			{
				Time = FMath::Fmod(Time, MaxCurveTime);
			}
			CurveValues.Add(ForceCurve.GetValue(Time));
		}

		Force = FVector::ZeroVector;
		switch (CurveEvaluateType)
		{
		case EExternalForceCurveEvaluateType::Average:
			for (const auto& CurveValue : CurveValues)
			{
				Force += CurveValue;
			}
			Force /= static_cast<float>(SubstepCount);

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

		Force *= RandomizedForceScale;
	}

	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
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

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_Curve_Apply);

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
		BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce * ForceRate);
#endif
	}
	else
	{
		Bone.Location += Force * ForceRate * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, Force * ForceRate);
#endif
	}

#if ENABLE_ANIM_DEBUG
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}

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
	WindDirection = ComponentTransform.InverseTransformVector(WindDirection);
	WindDirection *= WindSpeed;

	Bone.Location += WindDirection * ForceRate * RandomizedForceScale * Node.DeltaTime;

#if ENABLE_ANIM_DEBUG
	BoneForceMap.Add(Bone.BoneRef.BoneName, WindDirection * ForceRate * RandomizedForceScale);
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}
