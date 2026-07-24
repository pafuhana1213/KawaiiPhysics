// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReferenceSkeleton.h"

namespace KawaiiPhysicsBoneChain
{
	/**
	 * 参照スケルトン上の1本のボーンチェーン
	 * A single bone chain on a reference skeleton.
	 */
	struct FBoneChain
	{
		TArray<int32> BoneIndices;
	};

	/**
	 * チェーンルート検証の問題種別
	 * Issue type reported by chain-root validation.
	 */
	enum class EBoneChainValidationIssueType : uint8
	{
		InvalidRoot,
		DuplicateRoot,
		NestedRoot,
		LowestCommonAncestorIsSkeletonRoot,
	};

	/**
	 * チェーンルート検証で見つかった問題
	 * A validation issue found in chain roots.
	 */
	struct FBoneChainValidationIssue
	{
		EBoneChainValidationIssueType Type = EBoneChainValidationIssueType::InvalidRoot;
		FName BoneName = NAME_None;
		FName RelatedBoneName = NAME_None;
	};

	/**
	 * 順序を持たないボーンペアキー
	 * Order-independent bone pair key.
	 */
	struct FBoneChainPair
	{
		FName BoneNameA = NAME_None;
		FName BoneNameB = NAME_None;

		bool operator==(const FBoneChainPair& Other) const
		{
			return BoneNameA == Other.BoneNameA && BoneNameB == Other.BoneNameB;
		}
	};

	/**
	 * ChainGenペア差分の分類結果
	 * Classified diff result for ChainGen pairs.
	 */
	struct FBoneChainPairDiff
	{
		TArray<FBoneChainPair> KeptPairs;
		TArray<FBoneChainPair> RemovedPairs;
		TArray<FBoneChainPair> AddedPairs;
	};

	inline uint32 GetTypeHash(const FBoneChainPair& Pair)
	{
		return HashCombine(GetTypeHash(Pair.BoneNameA), GetTypeHash(Pair.BoneNameB));
	}

	/**
	 * ボーン名ペアを順序に依存しないキーへ正規化
	 * Normalizes a bone-name pair into an order-independent key.
	 */
	KAWAIIPHYSICS_API FBoneChainPair MakeBoneChainPair(FName BoneNameA, FName BoneNameB);

	/**
	 * GetParentIndexの一括走査で子ボーンテーブルを構築
	 * Builds a child-bone table by scanning GetParentIndex once.
	 */
	KAWAIIPHYSICS_API void BuildChildBoneTable(const FReferenceSkeleton& RefSkeleton, TArray<TArray<int32>>& OutChildrenByBone,
	                         const TSet<int32>* ExcludedBoneIndices = nullptr);

	/**
	 * ルートから最長子孫パスのチェーンを構築
	 * Builds a chain from a root by choosing the longest descendant path.
	 */
	KAWAIIPHYSICS_API bool BuildChainFromRoot(const FReferenceSkeleton& RefSkeleton, int32 RootBoneIndex, FBoneChain& OutChain,
	                        const TSet<int32>* ExcludedBoneIndices = nullptr,
	                        TArray<int32>* OutDiscardedBranchRootIndices = nullptr);

	/**
	 * インデックス比の相対位置列を作成
	 * Creates index-ratio relative positions.
	 */
	KAWAIIPHYSICS_API void MakeIndexRatios(int32 NumBones, TArray<float>& OutRatios);

	/**
	 * 参照ポーズ長比の相対位置列を作成。全長ゼロ時はIndexRatioへフォールバック
	 * Creates reference-pose length-ratio relative positions, falling back to IndexRatio for zero total length.
	 */
	KAWAIIPHYSICS_API void MakeLengthRatios(const FReferenceSkeleton& RefSkeleton, const FBoneChain& Chain, TArray<float>& OutRatios);

	/**
	 * 相対位置に基づいて2本のチェーンのインデックスを対応付け
	 * Matches two chains by relative positions.
	 */
	KAWAIIPHYSICS_API void MatchChainsByRatio(const TArray<float>& RatiosA, const TArray<float>& RatiosB,
	                        TArray<TPair<int32, int32>>& OutIndexPairs);

	/**
	 * 検索ルート配下からチェーンルート候補を検出
	 * Detects chain-root candidates under a search root.
	 */
	KAWAIIPHYSICS_API void DetectChainRootCandidates(const FReferenceSkeleton& RefSkeleton, int32 SearchRootIndex,
	                               TArray<int32>& OutChainRootIndices,
	                               const TSet<int32>* ExcludedBoneIndices = nullptr);

	/**
	 * ボーン名内の全数値トークンで候補をソート。全候補にユニークな数値キーがある場合のみ成功
	 * Sorts candidates by all numeric tokens in bone names, succeeding only when every candidate has a unique numeric key.
	 */
	KAWAIIPHYSICS_API bool SortByNumericTokens(const FReferenceSkeleton& RefSkeleton, TArray<int32>& InOutBoneIndices);

	/**
	 * 参照ポーズ位置の検索ルートローカルXY角度で候補をソート
	 * Sorts candidates by reference-pose angle in the search-root local XY plane.
	 */
	KAWAIIPHYSICS_API void SortByRefPoseAngle(const FReferenceSkeleton& RefSkeleton, int32 SearchRootIndex,
	                        TArray<int32>& InOutBoneIndices);

	/**
	 * チェーンルート配列を検証し、有効ルートと問題一覧を返す
	 * Validates chain roots and returns valid roots plus reported issues.
	 */
	KAWAIIPHYSICS_API void ValidateChainRoots(const FReferenceSkeleton& RefSkeleton, const TArray<int32>& RootBoneIndices,
	                        TArray<int32>& OutValidRootBoneIndices,
	                        TArray<FBoneChainValidationIssue>& OutIssues);

	/**
	 * LateralConnectionCountに従って隣接チェーン組を列挙
	 * Enumerates adjacent chain pairs according to LateralConnectionCount.
	 */
	KAWAIIPHYSICS_API void EnumerateLateralConnectionPairs(int32 NumChains, int32 LateralConnectionCount,
	                                     TArray<TPair<int32, int32>>& OutConnectionPairs);

	/**
	 * 既存ChainGenペア集合と新ペア集合の差分を温存・削除・追加へ分類
	 * Classifies the diff between existing ChainGen pairs and newly generated pairs into kept, removed, and added.
	 */
	KAWAIIPHYSICS_API void DiffBoneChainPairs(const TSet<FBoneChainPair>& ExistingChainGenPairs,
	                        const TSet<FBoneChainPair>& NewPairs,
	                        FBoneChainPairDiff& OutDiff);
}
