// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "AnimationRuntime.h"
#include "CoreMinimal.h"
#include "KawaiiPhysicsCollisionLimits.h"
#include "ReferenceSkeleton.h"
#include "Templates/Function.h"

namespace KawaiiPhysicsMirrorUtils
{
	namespace Private
	{
		// 端点ごとの属性を持たない形状では何もしない
		template <typename TLimit>
		void PreserveMirroredEndpointMeaning(
			const TLimit&, TLimit&, const FQuat&, const FQuat&, EAxis::Type)
		{
		}

		inline void PreserveMirroredEndpointMeaning(
			const FTaperedCapsuleLimit& Source,
			FTaperedCapsuleLimit& Mirrored,
			const FQuat& SourceBoneRefCS,
			const FQuat& TargetBoneRefCS,
			const EAxis::Type MirrorAxis)
		{
			// 鏡映は回転で表せず MirrorQuat は回転を返すため、反射軸によっては生成後の +Z 端が鏡像の -Z 端側に来る（端点の意味が反転）。
			// Radius0 = +Z 端 / Radius1 = -Z 端の規約で半径を元の物理端点に結び付けるため、期待する鏡像 +Z と生成後 +Z が逆向きなら半径を交換する。
			const FVector SourcePlusZCS =
				(SourceBoneRefCS * Source.OffsetRotation.Quaternion()).GetAxisZ();
			const FVector ExpectedMirroredPlusZCS =
				FAnimationRuntime::MirrorVector(SourcePlusZCS, MirrorAxis).GetSafeNormal();
			const FVector GeneratedPlusZCS =
				(TargetBoneRefCS * Mirrored.OffsetRotation.Quaternion()).GetAxisZ().GetSafeNormal();

			if (FVector::DotProduct(ExpectedMirroredPlusZCS, GeneratedPlusZCS) < 0.0f)
			{
				Swap(Mirrored.Radius0, Mirrored.Radius1);
			}
		}

		inline void PreserveMirroredEndpointMeaning(
			const FPlanarLimit& Source,
			FPlanarLimit& Mirrored,
			const FQuat& SourceBoneRefCS,
			const FQuat& TargetBoneRefCS,
			const EAxis::Type MirrorAxis)
		{
			// MirrorQuat は反射軸のローカル軸だけ鏡像と逆向きにするため、法線の向きを合わせる。
			const FVector SourceNormalCS =
				(SourceBoneRefCS * Source.OffsetRotation.Quaternion()).GetAxisZ();
			const FVector ExpectedNormalCS =
				FAnimationRuntime::MirrorVector(SourceNormalCS, MirrorAxis).GetSafeNormal();
			const FVector GeneratedNormalCS =
				(TargetBoneRefCS * Mirrored.OffsetRotation.Quaternion()).GetAxisZ().GetSafeNormal();
			if (FVector::DotProduct(ExpectedNormalCS, GeneratedNormalCS) < 0.0f)
			{
				const FQuat HalfTurnAroundLocalX(FVector::XAxisVector, PI);
				Mirrored.OffsetRotation = (Mirrored.OffsetRotation.Quaternion() * HalfTurnAroundLocalX).Rotator();
			}
		}

		inline void ResetMirroredPlanarHistory(FPlanarLimit& Mirrored)
		{
			// コピー構築で持ち込まれた元の物理ステップの平面はミラー先に適用しない。
			Mirrored.PreviousStepPlane = FPlane(0, 0, 0, 0);
			Mirrored.bHasPreviousStepPlane = false;
		}

		template <typename TLimit>
		void ResetMirroredPlanarHistory(TLimit&)
		{
		}
	}

	/**
	 * ボーンローカル空間のコリジョンオフセット位置をミラー先ボーンのローカル空間へ変換
	 * Converts a bone-local collision offset location into the mirrored target bone-local space.
	 * 位置はボーン基準: 先ボーンからの相対位置が元ボーンからの相対位置の鏡像になる。
	 * 対応ボーンの参照位置そのものが鏡像でない骨格では、CS での厳密な鏡像にはならない（仕様）。
	 * 負の参照ポーズスケールで左右を作るリグは対象外。
	 * Rigs that use negative reference-pose scale for left and right sides are unsupported.
	 */
	KAWAIIPHYSICS_API FVector MirrorOffsetLocation(const FVector& OffsetLocation, const FTransform& SourceBoneRefCS,
	                             const FTransform& TargetBoneRefCS, EAxis::Type MirrorAxis);

	/**
	 * ボーンローカル空間のコリジョンオフセット回転をミラー先ボーンのローカル空間へ変換
	 * Converts a bone-local collision offset rotation into the mirrored target bone-local space.
	 */
	KAWAIIPHYSICS_API FQuat MirrorOffsetRotation(const FQuat& OffsetRotation, const FQuat& SourceBoneRefCS,
	                           const FQuat& TargetBoneRefCS, EAxis::Type MirrorAxis);

	/**
	 * RefSkeletonの参照ポーズから全ボーンのコンポーネント空間回転を構築
	 * Builds component-space reference rotations for all bones from the RefSkeleton reference pose.
	 */
	KAWAIIPHYSICS_API void BuildComponentSpaceRefRotations(const FReferenceSkeleton& RefSkeleton, TArray<FQuat>& OutCSRotations);

	/**
	 * FTransform の親子合成と同じスケール規則で、全ボーンの参照 CS 変換を構築。
	 * Builds reference component-space transforms for all bones using FTransform parent-child scale composition.
	 */
	KAWAIIPHYSICS_API void BuildComponentSpaceRefTransforms(const FReferenceSkeleton& RefSkeleton,
	                                                       TArray<FTransform>& OutCSTransforms);

	/**
	 * ミラー平面の DrivingBone、OffsetLocation、OffsetRotation が一致する場合だけ前回ステップ平面を引き継ぐ。
	 * Restores the previous-step plane only when mirrored planes match in DrivingBone, OffsetLocation, and OffsetRotation.
	 */
	KAWAIIPHYSICS_API void RestoreMirroredPlanarHistory(const TArray<FPlanarLimit>& PreviousLimits,
	                                                    TArray<FPlanarLimit>& NewLimits);

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
		const TArray<FTransform>& CSRefTransforms,
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
				|| !CSRefTransforms.IsValidIndex(SourceBoneIndex) || !CSRefTransforms.IsValidIndex(TargetBoneIndex))
			{
				continue;
			}

			TLimit NewLimit = Source;
			NewLimit.DrivingBone = FBoneReference(TargetBoneName);
			Private::ResetMirroredPlanarHistory(NewLimit);
			NewLimit.OffsetLocation = MirrorOffsetLocation(Source.OffsetLocation, CSRefTransforms[SourceBoneIndex],
			                                               CSRefTransforms[TargetBoneIndex], MirrorAxis);
			NewLimit.OffsetRotation = MirrorOffsetRotation(Source.OffsetRotation.Quaternion(),
			                                               CSRefTransforms[SourceBoneIndex].GetRotation(),
			                                               CSRefTransforms[TargetBoneIndex].GetRotation(), MirrorAxis).Rotator();
			Private::PreserveMirroredEndpointMeaning(
				Source, NewLimit, CSRefTransforms[SourceBoneIndex].GetRotation(),
				CSRefTransforms[TargetBoneIndex].GetRotation(), MirrorAxis);
			NewLimit.SourceType = ECollisionSourceType::Mirror;
#if WITH_EDITORONLY_DATA
			NewLimit.Guid = FGuid::NewGuid();
#endif
			OutNewLimits.Add(NewLimit);
		}
	}
}
