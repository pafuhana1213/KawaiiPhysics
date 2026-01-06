// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_Curve.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_Curve)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_Curve_Apply"), STAT_KawaiiPhysics_ExternalForce_Curve_Apply,
                   STATGROUP_Anim);

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

void FKawaiiPhysics_ExternalForce_Curve::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                  FComponentSpacePoseContext& PoseContext)
{
	Super::PreApply(Node, PoseContext);

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

	// TODO : Merge EExternalForceSpace and EKawaiiPhysicsSimulationSpace
	EKawaiiPhysicsSimulationSpace From = EKawaiiPhysicsSimulationSpace::ComponentSpace;
	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
		From = EKawaiiPhysicsSimulationSpace::WorldSpace;
	}
	else if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		From = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
	}

	Force = Node.ConvertSimulationSpaceVector(PoseContext, From,
	                                          Node.SimulationSpace, Force);
}

void FKawaiiPhysics_ExternalForce_Curve::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                               FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
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
