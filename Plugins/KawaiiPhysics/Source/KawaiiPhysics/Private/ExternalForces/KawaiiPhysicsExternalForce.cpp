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
	// 非対応の外力（ProceduralWind等）ではグローバル乱数を消費せず1固定にする（乱数列への副作用も残さない）
	RandomizedForceScale = bSupportsRandomForceScaleRange
		                       ? FMath::RandRange(RandomForceScaleRange.Min, RandomForceScaleRange.Max)
		                       : 1.0f;
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
			return ExternalForcePtr && ExternalForcePtr == this;
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

FVector FKawaiiPhysics_ExternalForce::ConvertExternalForceToSimulationSpace(FAnimNode_KawaiiPhysics& Node,
                                                                            FComponentSpacePoseContext& PoseContext,
                                                                            const FVector& InForce) const
{
	// ExternalForceSpaceに対応する変換元のSimulationSpaceを決定
	EKawaiiPhysicsSimulationSpace From = EKawaiiPhysicsSimulationSpace::ComponentSpace;
	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
		From = EKawaiiPhysicsSimulationSpace::WorldSpace;
	}
	else if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		From = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
	}

	// 変換元からNode.SimulationSpaceへ変換して返す
	return Node.ConvertSimulationSpaceVector(PoseContext, From,
	                                         Node.SimulationSpace, InForce);
}

#if ENABLE_ANIM_DEBUG
void FKawaiiPhysics_ExternalForce::AnimDrawDebug(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                 const FComponentSpacePoseContext& PoseContext)
{
	if (IsDebugEnabled() && !Force.IsZero())
	{
		// BoneForceMapに該当ボーンが無い場合はnull参照を避ける（EditMode版と同様にガード）
		const FVector* ForcePtr = BoneForceMap.Find(Bone.BoneRef.BoneName);
		if (!ForcePtr)
		{
			return;
		}

		const auto AnimInstanceProxy = PoseContext.AnimInstanceProxy;
		const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
			Bone.Location);

		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ModifyRootBoneLocationWS + DebugArrowOffset,
			ModifyRootBoneLocationWS + DebugArrowOffset + ForcePtr->GetSafeNormal() *
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
