// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSyncBone.h"
#include "AnimNode_KawaiiPhysics.h"

#if WITH_EDITOR
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneManagement.h" // DrawDirectionalArrow, DrawSphere
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsSyncBone)

void FKawaiiPhysicsSyncTarget::UpdateScaleByLengthRate(const FRichCurve* ScaleCurveByBoneLengthRate)
{
	if (!ScaleCurveByBoneLengthRate)
	{
		return;
	}

	ScaleByLengthRateCurve = ScaleCurveByBoneLengthRate->Eval(LengthRateFromSyncTargetRoot);
}

void FKawaiiPhysicsSyncTarget::Apply(TArray<FKawaiiPhysicsModifyBone>& ModifyBones, const FVector& Translation)
{
	if (ModifyBoneIndex < 0 || !ModifyBones.IsValidIndex(ModifyBoneIndex))
	{
		return;;
	}

	FKawaiiPhysicsModifyBone& Bone = ModifyBones[ModifyBoneIndex];
	if (Bone.bSkipSimulate)
	{
		return;;
	}

	// // Apply Alpha per target
	const FVector ScaledTranslation = Translation * ScaleByLengthRateCurve;

#if WITH_EDITORONLY_DATA
	TranslationBySyncBone = ScaledTranslation;
#endif

	if (Bone.ParentIndex >= 0)
	{
		// Maintain bone length relative to parent
		const auto& ParentBone = ModifyBones[Bone.ParentIndex];
		const FVector NewPoseLocation = Bone.PoseLocation + ScaledTranslation;
		Bone.PoseLocation = (NewPoseLocation - ParentBone.PoseLocation).GetSafeNormal() * Bone.BoneLength +
			ParentBone.PoseLocation;
	}
	else
	{
		Bone.PoseLocation += ScaledTranslation;
	}
}

#if WITH_EDITOR
void FKawaiiPhysicsSyncTarget::DebugDraw(FPrimitiveDrawInterface* PDI, const FAnimNode_KawaiiPhysics* Node) const
{
	auto DrawForceArrow = [&](const FVector& Force, const FVector& Location)
	{
		const FRotator Rotation = FRotationMatrix::MakeFromX(Force.GetSafeNormal()).Rotator();
		const FMatrix TransformMatrix = FRotationMatrix(Rotation) * FTranslationMatrix(Location);
		DrawDirectionalArrow(PDI, TransformMatrix, FLinearColor::Green, Force.Length(), 2.0f, SDPG_Foreground);
	};

	if (ModifyBoneIndex >= 0 && Node->ModifyBones.IsValidIndex(ModifyBoneIndex))
	{
		// Target Bone Location
		FVector TargetBoneLocation = Node->ModifyBones[ModifyBoneIndex].Location;
		if (Node->SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
		{
			const FTransform& BaseBoneSpace2ComponentSpace = Node->GetBaseBoneSpace2ComponentSpace();
			TargetBoneLocation = BaseBoneSpace2ComponentSpace.TransformPosition(TargetBoneLocation);
		}
		DrawSphere(PDI, TargetBoneLocation, FRotator::ZeroRotator,
		           FVector(1.0f), 12, 6,
		           GEngine->ConstraintLimitMaterialY->GetRenderProxy(), SDPG_World);

		// Force by SyncBone
		DrawForceArrow(TranslationBySyncBone, TargetBoneLocation);
	}
}
#endif
