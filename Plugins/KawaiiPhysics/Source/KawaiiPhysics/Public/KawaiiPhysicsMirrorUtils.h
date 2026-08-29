// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "AnimationRuntime.h"
#include "CoreMinimal.h"
#include "KawaiiPhysicsCollisionLimits.h"
#include "ReferenceSkeleton.h"
#include "Templates/Function.h"

namespace KawaiiPhysicsMirrorUtils
{
	/**
	 * ボーンローカル空間のコリジョンオフセット位置をミラー先ボーンのローカル空間へ変換
	 * Converts a bone-local collision offset location into the mirrored target bone-local space.
	 */
	FVector MirrorOffsetLocation(const FVector& OffsetLocation, const FQuat& SourceBoneRefCS,
	                             const FQuat& TargetBoneRefCS, EAxis::Type MirrorAxis);

	/**
	 * ボーンローカル空間のコリジョンオフセット回転をミラー先ボーンのローカル空間へ変換
	 * Converts a bone-local collision offset rotation into the mirrored target bone-local space.
	 */
	FQuat MirrorOffsetRotation(const FQuat& OffsetRotation, const FQuat& SourceBoneRefCS,
	                           const FQuat& TargetBoneRefCS, EAxis::Type MirrorAxis);

	/**
	 * RefSkeletonの参照ポーズから全ボーンのコンポーネント空間回転を構築
	 * Builds component-space reference rotations for all bones from the RefSkeleton reference pose.
	 */
	void BuildComponentSpaceRefRotations(const FReferenceSkeleton& RefSkeleton, TArray<FQuat>& OutCSRotations);

	/**
	 * 既存コリジョンをミラー先ボーンへ複製し、生成結果をOutNewLimitsへ追加。DrivingBone.Initializeは呼び出し側で行うこと
	 * Duplicates existing collisions onto mirrored bones and appends generated results to OutNewLimits. DrivingBone.Initialize is the caller's responsibility.
	 */
	template <typename TLimit>
	void AppendMirroredLimits(
		const TArray<TLimit>& SourceLimits,
		const TArray<TLimit>& ExistingA,
		const TArray<TLimit>& ExistingB,
		bool bSkipExisting,
		TFunctionRef<FName(FName)> ResolveMirrorBoneName,
		TFunctionRef<int32(FName)> FindBoneIndex,
		const TArray<FQuat>& CSRefRotations,
		EAxis::Type MirrorAxis,
		TArray<TLimit>& OutNewLimits)
	{
		auto HasExistingNonMirrorLimit = [](const TArray<TLimit>& ExistingLimits, FName TargetBoneName)
		{
			return ExistingLimits.ContainsByPredicate([TargetBoneName](const TLimit& ExistingLimit)
			{
				return ExistingLimit.SourceType != ECollisionSourceType::Mirror
					&& ExistingLimit.DrivingBone.BoneName == TargetBoneName;
			});
		};

		for (const TLimit& Source : SourceLimits)
		{
			const FName TargetBoneName = ResolveMirrorBoneName(Source.DrivingBone.BoneName);
			if (TargetBoneName.IsNone())
			{
				continue;
			}

			if (bSkipExisting
				&& (HasExistingNonMirrorLimit(ExistingA, TargetBoneName)
					|| HasExistingNonMirrorLimit(ExistingB, TargetBoneName)))
			{
				continue;
			}

			const int32 SourceBoneIndex = FindBoneIndex(Source.DrivingBone.BoneName);
			const int32 TargetBoneIndex = FindBoneIndex(TargetBoneName);
			if (SourceBoneIndex == INDEX_NONE || TargetBoneIndex == INDEX_NONE
				|| !CSRefRotations.IsValidIndex(SourceBoneIndex) || !CSRefRotations.IsValidIndex(TargetBoneIndex))
			{
				continue;
			}

			TLimit NewLimit = Source;
			NewLimit.DrivingBone = FBoneReference(TargetBoneName);
			NewLimit.OffsetLocation = MirrorOffsetLocation(Source.OffsetLocation, CSRefRotations[SourceBoneIndex],
			                                               CSRefRotations[TargetBoneIndex], MirrorAxis);
			NewLimit.OffsetRotation = MirrorOffsetRotation(Source.OffsetRotation.Quaternion(),
			                                               CSRefRotations[SourceBoneIndex],
			                                               CSRefRotations[TargetBoneIndex], MirrorAxis).Rotator();
			NewLimit.SourceType = ECollisionSourceType::Mirror;
#if WITH_EDITORONLY_DATA
			NewLimit.Guid = FGuid::NewGuid();
#endif
			OutNewLimits.Add(NewLimit);
		}
	}
}
