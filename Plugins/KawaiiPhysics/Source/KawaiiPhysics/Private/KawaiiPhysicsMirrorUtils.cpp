// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsMirrorUtils.h"

namespace KawaiiPhysicsMirrorUtils
{
	FVector MirrorOffsetLocation(const FVector& OffsetLocation, const FQuat& SourceBoneRefCS,
	                             const FQuat& TargetBoneRefCS, EAxis::Type MirrorAxis)
	{
		const FVector SourceOffsetCS = SourceBoneRefCS.RotateVector(OffsetLocation);
		const FVector MirroredOffsetCS = FAnimationRuntime::MirrorVector(SourceOffsetCS, MirrorAxis);
		return TargetBoneRefCS.UnrotateVector(MirroredOffsetCS);
	}

	FQuat MirrorOffsetRotation(const FQuat& OffsetRotation, const FQuat& SourceBoneRefCS,
	                           const FQuat& TargetBoneRefCS, EAxis::Type MirrorAxis)
	{
		FQuat MirroredOffsetRotation =
			TargetBoneRefCS.Inverse()
			* FAnimationRuntime::MirrorQuat(SourceBoneRefCS * OffsetRotation, MirrorAxis)
			* FAnimationRuntime::MirrorQuat(SourceBoneRefCS, MirrorAxis).Inverse()
			* TargetBoneRefCS;
		MirroredOffsetRotation.Normalize();
		return MirroredOffsetRotation;
	}

	void BuildComponentSpaceRefRotations(const FReferenceSkeleton& RefSkeleton, TArray<FQuat>& OutCSRotations)
	{
		const TArray<FTransform>& RefBonePose = RefSkeleton.GetRefBonePose();
		OutCSRotations.SetNum(RefBonePose.Num());

		for (int32 BoneIndex = 0; BoneIndex < RefBonePose.Num(); ++BoneIndex)
		{
			const FQuat LocalRotation = RefBonePose[BoneIndex].GetRotation();
			const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE && OutCSRotations.IsValidIndex(ParentIndex))
			{
				OutCSRotations[BoneIndex] = OutCSRotations[ParentIndex] * LocalRotation;
			}
			else
			{
				OutCSRotations[BoneIndex] = LocalRotation;
			}
			OutCSRotations[BoneIndex].Normalize();
		}
	}
}
