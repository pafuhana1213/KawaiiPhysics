// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce.h"

#include "AnimNode_KawaiiPhysics.h"

#include "Math/UnrealMathUtility.h"
#include "Misc/AssertionMacros.h"

#if WITH_EDITOR
#include "SceneManagement.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce)

void FKawaiiPhysics_ExternalForce::Initialize(const FAnimationInitializeContext& Context)
{
}

void FKawaiiPhysics_ExternalForce::PreApply(FAnimNode_KawaiiPhysics& Node,
                                            FComponentSpacePoseContext& PoseContext)
{
	ComponentTransform = PoseContext.AnimInstanceProxy->GetComponentTransform();
	RandomizedForceScale = FMath::RandRange(RandomForceScaleRange.Min, RandomForceScaleRange.Max);
}

void FKawaiiPhysics_ExternalForce::ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                   FComponentSpacePoseContext& PoseContext,
                                                   FVector& InOutVelocity)
{
}

void FKawaiiPhysics_ExternalForce::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                         FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
}

void FKawaiiPhysics_ExternalForce::PostApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext)
{
	if (bIsOneShot)
	{
		Node.ExternalForces.RemoveAll([&](FInstancedStruct& InstancedStruct)
		{
			const auto* ExternalForcePtr = InstancedStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>();
			return ExternalForcePtr == this;
		});
	}
}

bool FKawaiiPhysics_ExternalForce::IsDebugEnabled(bool bInPersona)
{
	if (bInPersona)
	{
		return bDrawDebug && bIsEnabled;
	}

#if ENABLE_ANIM_DEBUG
	if (CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
	{
		return bDrawDebug && bIsEnabled;
	}
#endif

	return false;
}

#if ENABLE_ANIM_DEBUG
void FKawaiiPhysics_ExternalForce::AnimDrawDebug(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                 const FComponentSpacePoseContext& PoseContext)
{
	if (IsDebugEnabled() && !Force.IsZero())
	{
		const auto AnimInstanceProxy = PoseContext.AnimInstanceProxy;
		const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
			Bone.Location);

		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ModifyRootBoneLocationWS + DebugArrowOffset,
			ModifyRootBoneLocationWS + DebugArrowOffset + BoneForceMap.Find(Bone.BoneRef.BoneName)->GetSafeNormal() *
			DebugArrowLength,
			DebugArrowSize, FColor::Red, false, 0.f, 2);
	}
}
#endif

#if WITH_EDITOR
void FKawaiiPhysics_ExternalForce::AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
                                                            const FAnimNode_KawaiiPhysics& Node,
                                                            FPrimitiveDrawInterface* PDI)
{
	if (IsDebugEnabled(true) && CanApply(ModifyBone) && !Force.IsNearlyZero() &&
		BoneForceMap.Contains(ModifyBone.BoneRef.BoneName))
	{
		const FTransform ArrowTransform = FTransform(
			BoneForceMap.Find(ModifyBone.BoneRef.BoneName)->GetSafeNormal().ToOrientationRotator(),
			ModifyBone.Location + DebugArrowOffset);
		DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
		                     SDPG_Foreground, 1.0f);
	}
}
#endif

bool FKawaiiPhysics_ExternalForce::CanApply(const FKawaiiPhysicsModifyBone& Bone) const
{
	if (!ApplyBoneFilter.IsEmpty() && !ApplyBoneFilter.Contains(Bone.BoneRef))
	{
		return false;
	}

	if (!IgnoreBoneFilter.IsEmpty() && IgnoreBoneFilter.Contains(Bone.BoneRef))
	{
		return false;
	}

	return true;
}
