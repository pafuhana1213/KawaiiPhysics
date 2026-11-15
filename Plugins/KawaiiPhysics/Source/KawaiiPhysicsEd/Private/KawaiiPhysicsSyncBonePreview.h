// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "KawaiiPhysicsSyncBone.h"

namespace KawaiiPhysicsSyncBonePreview
{
	// Array の Index 番目の要素と同じ名前を持つ要素のうち、何番目か（同名の重複を区別するため）
	template <typename ElementType, typename GetNameType>
	int32 GetSameNameOccurrence(const TArray<ElementType>& Array, const int32 Index, GetNameType GetName)
	{
		const FName Name = GetName(Array[Index]);
		int32 Occurrence = 0;
		for (int32 i = 0; i < Index; ++i)
		{
			if (GetName(Array[i]) == Name)
			{
				++Occurrence;
			}
		}
		return Occurrence;
	}

	// 同じ名前の要素のうち Occurrence 番目のものを探す
	template <typename ElementType, typename GetNameType>
	const ElementType* FindSameNameElement(const TArray<ElementType>& Array, const FName Name, int32 Occurrence,
	                                       GetNameType GetName)
	{
		for (const ElementType& Element : Array)
		{
			if (GetName(Element) == Name && Occurrence-- == 0)
			{
				return &Element;
			}
		}
		return nullptr;
	}

	inline void CopyTargetRootPreview(FKawaiiPhysicsSyncTargetRoot& Dest, const FKawaiiPhysicsSyncTargetRoot* Source)
	{
		const FKawaiiPhysicsSyncTargetRoot Cleared;
		const FKawaiiPhysicsSyncTargetRoot& From = Source ? *Source : Cleared;

		Dest.ChildTargets = From.ChildTargets;
		Dest.ScaleByLengthRateCurve = From.ScaleByLengthRateCurve;
		Dest.LengthRateFromSyncTargetRoot = From.LengthRateFromSyncTargetRoot;
		Dest.ModifyBoneIndex = From.ModifyBoneIndex;
#if WITH_EDITORONLY_DATA
		Dest.PreviewBone = From.PreviewBone;
		Dest.TranslationBySyncBone = From.TranslationBySyncBone;
#endif
	}

	inline void CopySyncBonePreview(FKawaiiPhysicsSyncBone& Dest, const FKawaiiPhysicsSyncBone* Source)
	{
		const FKawaiiPhysicsSyncBone Cleared;
		const FKawaiiPhysicsSyncBone& From = Source ? *Source : Cleared;

		Dest.InitialPoseLocation = From.InitialPoseLocation;
#if WITH_EDITORONLY_DATA
		Dest.DeltaDistance = From.DeltaDistance;
		Dest.ScaledDeltaDistance = From.ScaledDeltaDistance;
#endif

		const auto GetTargetRootName = [](const FKawaiiPhysicsSyncTargetRoot& TargetRoot)
		{
			return TargetRoot.Bone.BoneName;
		};
		for (int32 TargetIndex = 0; TargetIndex < Dest.TargetRoots.Num(); ++TargetIndex)
		{
			FKawaiiPhysicsSyncTargetRoot& DestTargetRoot = Dest.TargetRoots[TargetIndex];
			const FKawaiiPhysicsSyncTargetRoot* SourceTargetRoot = Source
				? FindSameNameElement(Source->TargetRoots, DestTargetRoot.Bone.BoneName,
				                      GetSameNameOccurrence(Dest.TargetRoots, TargetIndex, GetTargetRootName),
				                      GetTargetRootName)
				: nullptr;
			CopyTargetRootPreview(DestTargetRoot, SourceTargetRoot);
		}
	}

	/**
	 * 実行時のノードの SyncBones から、エディタのプレビュー表示用の値だけをグラフノード側へ写す。
	 * 実行時は現在のメッシュ・LOD で無効なターゲットを TargetRoots から取り除くため、配列をまるごと写すと
	 * ユーザーの設定が消える。ボーン名（同名の重複は出現順）で対応付け、設定値と要素の並びは変更しない。
	 * 対応する実行時の要素が無い場合は、プレビュー値をクリアする。
	 * Copies only the editor preview values from the runtime SyncBones to the graph node. The runtime removes
	 * TargetRoots that are invalid for the current mesh or LOD, so copying the whole array would delete user
	 * settings. Entries are matched by bone name (duplicates in order); settings and order are left unchanged.
	 * Preview values of entries with no runtime match are cleared.
	 */
	inline void CopyPreviewValues(TArray<FKawaiiPhysicsSyncBone>& Dest, const TArray<FKawaiiPhysicsSyncBone>& Source)
	{
		const auto GetSyncBoneName = [](const FKawaiiPhysicsSyncBone& SyncBone) { return SyncBone.Bone.BoneName; };
		for (int32 SyncIndex = 0; SyncIndex < Dest.Num(); ++SyncIndex)
		{
			FKawaiiPhysicsSyncBone& DestSyncBone = Dest[SyncIndex];
			const FKawaiiPhysicsSyncBone* SourceSyncBone = FindSameNameElement(
				Source, DestSyncBone.Bone.BoneName, GetSameNameOccurrence(Dest, SyncIndex, GetSyncBoneName),
				GetSyncBoneName);
			CopySyncBonePreview(DestSyncBone, SourceSyncBone);
		}
	}
} // namespace KawaiiPhysicsSyncBonePreview
