// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsMirrorUtils.h"

namespace KawaiiPhysicsMirrorUtils
{
	FVector MirrorOffsetLocation(const FVector& OffsetLocation, const FTransform& SourceBoneRefCS,
	                             const FTransform& TargetBoneRefCS, EAxis::Type MirrorAxis)
	{
		const FVector SourceOffsetCS = SourceBoneRefCS.TransformVector(OffsetLocation);
		const FVector MirroredOffsetCS = FAnimationRuntime::MirrorVector(SourceOffsetCS, MirrorAxis);
		return TargetBoneRefCS.InverseTransformVector(MirroredOffsetCS);
	}

	FQuat MirrorOffsetRotation(const FQuat& OffsetRotation, const FQuat& SourceBoneRefCS,
	                           const FQuat& TargetBoneRefCS, EAxis::Type MirrorAxis)
	{
		// 実行時の CS 回転は Target * Offset'。Offset' = Target⁻¹ * MirrorQuat(Source * Offset) とすれば
		// CS で MirrorQuat(Source * Offset) になり、Target の向きに依存しない。
		FQuat MirroredOffsetRotation =
			TargetBoneRefCS.Inverse()
			* FAnimationRuntime::MirrorQuat(SourceBoneRefCS * OffsetRotation, MirrorAxis);
		MirroredOffsetRotation.Normalize();
		return MirroredOffsetRotation;
	}

	void BuildComponentSpaceRefRotations(const FReferenceSkeleton& RefSkeleton, TArray<FQuat>& OutCSRotations)
	{
		TArray<FTransform> CSTransforms;
		BuildComponentSpaceRefTransforms(RefSkeleton, CSTransforms);
		OutCSRotations.SetNum(CSTransforms.Num());
		for (int32 BoneIndex = 0; BoneIndex < CSTransforms.Num(); ++BoneIndex)
		{
			OutCSRotations[BoneIndex] = CSTransforms[BoneIndex].GetRotation();
		}
	}

	void BuildComponentSpaceRefTransforms(const FReferenceSkeleton& RefSkeleton, TArray<FTransform>& OutCSTransforms)
	{
		const TArray<FTransform>& RefBonePose = RefSkeleton.GetRefBonePose();
		OutCSTransforms.SetNum(RefBonePose.Num());

		for (int32 BoneIndex = 0; BoneIndex < RefBonePose.Num(); ++BoneIndex)
		{
			const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE && OutCSTransforms.IsValidIndex(ParentIndex))
			{
				OutCSTransforms[BoneIndex] = RefBonePose[BoneIndex] * OutCSTransforms[ParentIndex];
			}
			else
			{
				OutCSTransforms[BoneIndex] = RefBonePose[BoneIndex];
			}
			OutCSTransforms[BoneIndex].NormalizeRotation();
		}
	}

	void RestoreMirroredPlanarHistory(const TArray<FPlanarLimit>& PreviousLimits, TArray<FPlanarLimit>& NewLimits)
	{
		const auto Matches = [](const FPlanarLimit& A, const FPlanarLimit& B)
		{
			return A.SourceType == ECollisionSourceType::Mirror
				&& B.SourceType == ECollisionSourceType::Mirror
				&& A.DrivingBone.BoneName == B.DrivingBone.BoneName
				&& A.OffsetLocation == B.OffsetLocation
				&& A.OffsetRotation == B.OffsetRotation;
		};
		for (int32 NewIndex = 0; NewIndex < NewLimits.Num(); ++NewIndex)
		{
			FPlanarLimit& NewLimit = NewLimits[NewIndex];
			if (NewLimit.SourceType != ECollisionSourceType::Mirror)
			{
				continue;
			}
			Private::ResetMirroredPlanarHistory(NewLimit);
			// 同じ内容の平面は新旧それぞれの出現順で一対一に対応づける。
			int32 MatchingOrdinal = 0;
			for (int32 EarlierIndex = 0; EarlierIndex < NewIndex; ++EarlierIndex)
			{
				if (Matches(NewLimits[EarlierIndex], NewLimit))
				{
					++MatchingOrdinal;
				}
			}
			int32 PreviousOrdinal = 0;
			for (const FPlanarLimit& Previous : PreviousLimits)
			{
				if (Matches(Previous, NewLimit))
				{
					if (PreviousOrdinal == MatchingOrdinal)
					{
						NewLimit.PreviousStepPlane = Previous.PreviousStepPlane;
						NewLimit.bHasPreviousStepPlane = Previous.bHasPreviousStepPlane;
						break;
					}
					++PreviousOrdinal;
				}
			}
		}
	}
}
