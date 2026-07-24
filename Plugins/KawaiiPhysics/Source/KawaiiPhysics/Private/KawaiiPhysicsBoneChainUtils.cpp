// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsBoneChainUtils.h"

#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"

namespace KawaiiPhysicsBoneChain
{
	namespace
	{
		bool IsExcluded(int32 BoneIndex, const TSet<int32>* ExcludedBoneIndices)
		{
			return ExcludedBoneIndices != nullptr && ExcludedBoneIndices->Contains(BoneIndex);
		}

		FName GetBoneNameSafe(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex)
		{
			return RefSkeleton.IsValidIndex(BoneIndex) ? RefSkeleton.GetBoneName(BoneIndex) : NAME_None;
		}

		bool IsAncestorOf(const FReferenceSkeleton& RefSkeleton, int32 AncestorIndex, int32 BoneIndex)
		{
			for (int32 CurrentIndex = BoneIndex; CurrentIndex != INDEX_NONE;
			     CurrentIndex = RefSkeleton.GetParentIndex(CurrentIndex))
			{
				if (CurrentIndex == AncestorIndex)
				{
					return true;
				}
			}
			return false;
		}

		int32 FindLowestCommonAncestor(const FReferenceSkeleton& RefSkeleton, const TArray<int32>& BoneIndices)
		{
			if (BoneIndices.Num() == 0)
			{
				return INDEX_NONE;
			}

			TSet<int32> CommonAncestors;
			for (int32 CurrentIndex = BoneIndices[0]; CurrentIndex != INDEX_NONE;
			     CurrentIndex = RefSkeleton.GetParentIndex(CurrentIndex))
			{
				CommonAncestors.Add(CurrentIndex);
			}

			for (int32 BoneIndexIndex = 1; BoneIndexIndex < BoneIndices.Num(); ++BoneIndexIndex)
			{
				TSet<int32> Ancestors;
				for (int32 CurrentIndex = BoneIndices[BoneIndexIndex]; CurrentIndex != INDEX_NONE;
				     CurrentIndex = RefSkeleton.GetParentIndex(CurrentIndex))
				{
					Ancestors.Add(CurrentIndex);
				}

				for (auto It = CommonAncestors.CreateIterator(); It; ++It)
				{
					if (!Ancestors.Contains(*It))
					{
						It.RemoveCurrent();
					}
				}
			}

			for (int32 CurrentIndex = BoneIndices[0]; CurrentIndex != INDEX_NONE;
			     CurrentIndex = RefSkeleton.GetParentIndex(CurrentIndex))
			{
				if (CommonAncestors.Contains(CurrentIndex))
				{
					return CurrentIndex;
				}
			}

			return INDEX_NONE;
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
			}
		}

		void ExtractNumericTokens(const FString& Name, TArray<int32>& OutTokens)
		{
			OutTokens.Reset();

			int32 Index = 0;
			while (Index < Name.Len())
			{
				if (!FChar::IsDigit(Name[Index]))
				{
					++Index;
					continue;
				}

				int64 Value = 0;
				while (Index < Name.Len() && FChar::IsDigit(Name[Index]))
				{
					Value = FMath::Min<int64>(Value * 10 + (Name[Index] - TEXT('0')), MAX_int32);
					++Index;
				}
				OutTokens.Add(static_cast<int32>(Value));
			}
		}

		int32 CompareNumericTokens(const TArray<int32>& A, const TArray<int32>& B)
		{
			const int32 CommonNum = FMath::Min(A.Num(), B.Num());
			for (int32 Index = 0; Index < CommonNum; ++Index)
			{
				if (A[Index] != B[Index])
				{
					return A[Index] < B[Index] ? -1 : 1;
				}
			}

			if (A.Num() == B.Num())
			{
				return 0;
			}
			return A.Num() < B.Num() ? -1 : 1;
		}

		TSet<FBoneChainPair> CanonicalizePairs(const TSet<FBoneChainPair>& Pairs)
		{
			TSet<FBoneChainPair> Result;
			for (const FBoneChainPair& Pair : Pairs)
			{
				Result.Add(MakeBoneChainPair(Pair.BoneNameA, Pair.BoneNameB));
			}
			return Result;
		}

		uint64 MakeIndexPairKey(int32 IndexA, int32 IndexB)
		{
			return (static_cast<uint64>(static_cast<uint32>(IndexA)) << 32)
				| static_cast<uint32>(IndexB);
		}
	}

	FBoneChainPair MakeBoneChainPair(FName BoneNameA, FName BoneNameB)
	{
		FBoneChainPair Pair;
		if (BoneNameA.ToString() <= BoneNameB.ToString())
		{
			Pair.BoneNameA = BoneNameA;
			Pair.BoneNameB = BoneNameB;
		}
		else
		{
			Pair.BoneNameA = BoneNameB;
			Pair.BoneNameB = BoneNameA;
		}
		return Pair;
	}

	void BuildChildBoneTable(const FReferenceSkeleton& RefSkeleton, TArray<TArray<int32>>& OutChildrenByBone,
	                         const TSet<int32>* ExcludedBoneIndices)
	{
		const int32 NumBones = RefSkeleton.GetNum();
		OutChildrenByBone.Reset();
		OutChildrenByBone.SetNum(NumBones);

		for (int32 ChildIndex = 0; ChildIndex < NumBones; ++ChildIndex)
		{
			if (IsExcluded(ChildIndex, ExcludedBoneIndices))
			{
				continue;
			}

			const int32 ParentIndex = RefSkeleton.GetParentIndex(ChildIndex);
			if (ParentIndex != INDEX_NONE && OutChildrenByBone.IsValidIndex(ParentIndex)
				&& !IsExcluded(ParentIndex, ExcludedBoneIndices))
			{
				OutChildrenByBone[ParentIndex].Add(ChildIndex);
			}
		}
	}

	bool BuildChainFromRoot(const FReferenceSkeleton& RefSkeleton, int32 RootBoneIndex, FBoneChain& OutChain,
	                        const TSet<int32>* ExcludedBoneIndices,
	                        TArray<int32>* OutDiscardedBranchRootIndices)
	{
		OutChain.BoneIndices.Reset();
		if (OutDiscardedBranchRootIndices != nullptr)
		{
			OutDiscardedBranchRootIndices->Reset();
		}

		if (!RefSkeleton.IsValidIndex(RootBoneIndex) || IsExcluded(RootBoneIndex, ExcludedBoneIndices))
		{
			return false;
		}

		TArray<TArray<int32>> ChildrenByBone;
		BuildChildBoneTable(RefSkeleton, ChildrenByBone, ExcludedBoneIndices);

		TArray<int32> DepthToLeaf;
		TArray<int32> BestChild;
		DepthToLeaf.Init(0, RefSkeleton.GetNum());
		BestChild.Init(INDEX_NONE, RefSkeleton.GetNum());

		for (int32 BoneIndex = RefSkeleton.GetNum() - 1; BoneIndex >= 0; --BoneIndex)
		{
			if (IsExcluded(BoneIndex, ExcludedBoneIndices))
			{
				continue;
			}

			int32 BestDepth = 0;
			int32 BestChildIndex = INDEX_NONE;
			for (int32 ChildIndex : ChildrenByBone[BoneIndex])
			{
				const int32 ChildDepth = DepthToLeaf[ChildIndex] + 1;
				if (ChildDepth > BestDepth || (ChildDepth == BestDepth
					&& (BestChildIndex == INDEX_NONE || ChildIndex < BestChildIndex)))
				{
					BestDepth = ChildDepth;
					BestChildIndex = ChildIndex;
				}
			}
			DepthToLeaf[BoneIndex] = BestDepth;
			BestChild[BoneIndex] = BestChildIndex;
		}

		for (int32 CurrentIndex = RootBoneIndex; CurrentIndex != INDEX_NONE; CurrentIndex = BestChild[CurrentIndex])
		{
			OutChain.BoneIndices.Add(CurrentIndex);

			if (OutDiscardedBranchRootIndices != nullptr)
			{
				for (int32 ChildIndex : ChildrenByBone[CurrentIndex])
				{
					if (ChildIndex != BestChild[CurrentIndex])
					{
						OutDiscardedBranchRootIndices->Add(ChildIndex);
					}
				}
			}
		}

		return true;
	}

	void MakeIndexRatios(int32 NumBones, TArray<float>& OutRatios)
	{
		OutRatios.Reset();
		if (NumBones <= 0)
		{
			return;
		}

		OutRatios.SetNum(NumBones);
		if (NumBones == 1)
		{
			OutRatios[0] = 0.0f;
			return;
		}

		const float Denominator = static_cast<float>(NumBones - 1);
		for (int32 Index = 0; Index < NumBones; ++Index)
		{
			OutRatios[Index] = static_cast<float>(Index) / Denominator;
		}
	}

	void MakeLengthRatios(const FReferenceSkeleton& RefSkeleton, const FBoneChain& Chain, TArray<float>& OutRatios)
	{
		OutRatios.Reset();
		if (Chain.BoneIndices.Num() <= 0)
		{
			return;
		}

		TArray<FTransform> CSTransforms;
		BuildComponentSpaceRefTransforms(RefSkeleton, CSTransforms);

		TArray<float> CumulativeLengths;
		CumulativeLengths.Init(0.0f, Chain.BoneIndices.Num());

		float TotalLength = 0.0f;
		for (int32 ChainIndex = 1; ChainIndex < Chain.BoneIndices.Num(); ++ChainIndex)
		{
			const int32 PrevBoneIndex = Chain.BoneIndices[ChainIndex - 1];
			const int32 BoneIndex = Chain.BoneIndices[ChainIndex];
			if (!CSTransforms.IsValidIndex(PrevBoneIndex) || !CSTransforms.IsValidIndex(BoneIndex))
			{
				MakeIndexRatios(Chain.BoneIndices.Num(), OutRatios);
				return;
			}

			TotalLength += FVector::Distance(CSTransforms[PrevBoneIndex].GetLocation(),
			                                 CSTransforms[BoneIndex].GetLocation());
			CumulativeLengths[ChainIndex] = TotalLength;
		}

		if (TotalLength < KINDA_SMALL_NUMBER)
		{
			MakeIndexRatios(Chain.BoneIndices.Num(), OutRatios);
			return;
		}

		OutRatios.SetNum(Chain.BoneIndices.Num());
		for (int32 ChainIndex = 0; ChainIndex < Chain.BoneIndices.Num(); ++ChainIndex)
		{
			OutRatios[ChainIndex] = CumulativeLengths[ChainIndex] / TotalLength;
		}
	}

	void MatchChainsByRatio(const TArray<float>& RatiosA, const TArray<float>& RatiosB,
	                        TArray<TPair<int32, int32>>& OutIndexPairs)
	{
		OutIndexPairs.Reset();
		if (RatiosA.Num() == 0 || RatiosB.Num() == 0)
		{
			return;
		}

		if (RatiosA.Num() == RatiosB.Num())
		{
			for (int32 Index = 0; Index < RatiosA.Num(); ++Index)
			{
				OutIndexPairs.Add(TPair<int32, int32>(Index, Index));
			}
			return;
		}

		const bool bAIsLonger = RatiosA.Num() > RatiosB.Num();
		const TArray<float>& LongerRatios = bAIsLonger ? RatiosA : RatiosB;
		const TArray<float>& ShorterRatios = bAIsLonger ? RatiosB : RatiosA;

		TSet<uint64> UniquePairKeys;
		for (int32 LongerIndex = 0; LongerIndex < LongerRatios.Num(); ++LongerIndex)
		{
			const float Ratio = LongerRatios[LongerIndex];
			int32 UpperIndex = Algo::LowerBound(ShorterRatios, Ratio);
			UpperIndex = FMath::Clamp(UpperIndex, 0, ShorterRatios.Num() - 1);

			int32 NearestShorterIndex = UpperIndex;
			if (UpperIndex > 0)
			{
				const float PrevDistance = FMath::Abs(Ratio - ShorterRatios[UpperIndex - 1]);
				const float UpperDistance = FMath::Abs(Ratio - ShorterRatios[UpperIndex]);
				if (PrevDistance <= UpperDistance)
				{
					NearestShorterIndex = UpperIndex - 1;
				}
			}

			const TPair<int32, int32> Pair = bAIsLonger
				                                ? TPair<int32, int32>(LongerIndex, NearestShorterIndex)
				                                : TPair<int32, int32>(NearestShorterIndex, LongerIndex);
			const uint64 PairKey = MakeIndexPairKey(Pair.Key, Pair.Value);
			if (!UniquePairKeys.Contains(PairKey))
			{
				UniquePairKeys.Add(PairKey);
				OutIndexPairs.Add(Pair);
			}
		}
	}

	void DetectChainRootCandidates(const FReferenceSkeleton& RefSkeleton, int32 SearchRootIndex,
	                               TArray<int32>& OutChainRootIndices,
	                               const TSet<int32>* ExcludedBoneIndices)
	{
		OutChainRootIndices.Reset();
		if (!RefSkeleton.IsValidIndex(SearchRootIndex) || IsExcluded(SearchRootIndex, ExcludedBoneIndices))
		{
			return;
		}

		TArray<TArray<int32>> ChildrenByBone;
		BuildChildBoneTable(RefSkeleton, ChildrenByBone, ExcludedBoneIndices);

		int32 BranchRootIndex = SearchRootIndex;
		while (ChildrenByBone.IsValidIndex(BranchRootIndex) && ChildrenByBone[BranchRootIndex].Num() == 1)
		{
			BranchRootIndex = ChildrenByBone[BranchRootIndex][0];
		}

		if (!ChildrenByBone.IsValidIndex(BranchRootIndex))
		{
			return;
		}

		OutChainRootIndices = ChildrenByBone[BranchRootIndex];
		if (!SortByNumericTokens(RefSkeleton, OutChainRootIndices))
		{
			SortByRefPoseAngle(RefSkeleton, SearchRootIndex, OutChainRootIndices);
		}
	}

	bool SortByNumericTokens(const FReferenceSkeleton& RefSkeleton, TArray<int32>& InOutBoneIndices)
	{
		struct FCandidateKey
		{
			int32 BoneIndex = INDEX_NONE;
			TArray<int32> Tokens;
		};

		TArray<FCandidateKey> Keys;
		Keys.Reserve(InOutBoneIndices.Num());

		for (int32 BoneIndex : InOutBoneIndices)
		{
			if (!RefSkeleton.IsValidIndex(BoneIndex))
			{
				return false;
			}

			FCandidateKey Key;
			Key.BoneIndex = BoneIndex;
			ExtractNumericTokens(RefSkeleton.GetBoneName(BoneIndex).ToString(), Key.Tokens);
			if (Key.Tokens.Num() == 0)
			{
				return false;
			}
			Keys.Add(Key);
		}

		for (int32 IndexA = 0; IndexA < Keys.Num(); ++IndexA)
		{
			for (int32 IndexB = IndexA + 1; IndexB < Keys.Num(); ++IndexB)
			{
				if (CompareNumericTokens(Keys[IndexA].Tokens, Keys[IndexB].Tokens) == 0)
				{
					return false;
				}
			}
		}

		Algo::Sort(Keys, [](const FCandidateKey& A, const FCandidateKey& B)
		{
			const int32 CompareResult = CompareNumericTokens(A.Tokens, B.Tokens);
			return CompareResult < 0 || (CompareResult == 0 && A.BoneIndex < B.BoneIndex);
		});

		InOutBoneIndices.Reset();
		for (const FCandidateKey& Key : Keys)
		{
			InOutBoneIndices.Add(Key.BoneIndex);
		}

		return true;
	}

	void SortByRefPoseAngle(const FReferenceSkeleton& RefSkeleton, int32 SearchRootIndex,
	                        TArray<int32>& InOutBoneIndices)
	{
		if (!RefSkeleton.IsValidIndex(SearchRootIndex))
		{
			InOutBoneIndices.Sort();
			return;
		}

		TArray<FTransform> CSTransforms;
		BuildComponentSpaceRefTransforms(RefSkeleton, CSTransforms);
		if (!CSTransforms.IsValidIndex(SearchRootIndex))
		{
			InOutBoneIndices.Sort();
			return;
		}

		struct FAngleCandidate
		{
			int32 BoneIndex = INDEX_NONE;
			float Angle = 0.0f;
			float RadiusSquared = 0.0f;
		};

		TArray<FAngleCandidate> Candidates;
		Candidates.Reserve(InOutBoneIndices.Num());

		bool bHasUsableRadius = false;
		for (int32 BoneIndex : InOutBoneIndices)
		{
			if (!CSTransforms.IsValidIndex(BoneIndex))
			{
				InOutBoneIndices.Sort();
				return;
			}

			const FVector LocalLocation = CSTransforms[SearchRootIndex].InverseTransformPosition(
				CSTransforms[BoneIndex].GetLocation());

			FAngleCandidate Candidate;
			Candidate.BoneIndex = BoneIndex;
			Candidate.Angle = FMath::Atan2(LocalLocation.Y, LocalLocation.X);
			Candidate.RadiusSquared = LocalLocation.X * LocalLocation.X + LocalLocation.Y * LocalLocation.Y;
			bHasUsableRadius = bHasUsableRadius || Candidate.RadiusSquared > KINDA_SMALL_NUMBER;
			Candidates.Add(Candidate);
		}

		if (!bHasUsableRadius)
		{
			InOutBoneIndices.Sort();
			return;
		}

		Algo::Sort(Candidates, [](const FAngleCandidate& A, const FAngleCandidate& B)
		{
			if (!FMath::IsNearlyEqual(A.Angle, B.Angle, KINDA_SMALL_NUMBER))
			{
				return A.Angle < B.Angle;
			}
			return A.BoneIndex < B.BoneIndex;
		});

		InOutBoneIndices.Reset();
		for (const FAngleCandidate& Candidate : Candidates)
		{
			InOutBoneIndices.Add(Candidate.BoneIndex);
		}
	}

	void ValidateChainRoots(const FReferenceSkeleton& RefSkeleton, const TArray<int32>& RootBoneIndices,
	                        TArray<int32>& OutValidRootBoneIndices,
	                        TArray<FBoneChainValidationIssue>& OutIssues)
	{
		OutValidRootBoneIndices.Reset();
		OutIssues.Reset();

		TSet<int32> SeenRootIndices;
		for (int32 RootBoneIndex : RootBoneIndices)
		{
			if (!RefSkeleton.IsValidIndex(RootBoneIndex))
			{
				FBoneChainValidationIssue Issue;
				Issue.Type = EBoneChainValidationIssueType::InvalidRoot;
				Issue.BoneName = NAME_None;
				OutIssues.Add(Issue);
				continue;
			}

			if (SeenRootIndices.Contains(RootBoneIndex))
			{
				FBoneChainValidationIssue Issue;
				Issue.Type = EBoneChainValidationIssueType::DuplicateRoot;
				Issue.BoneName = GetBoneNameSafe(RefSkeleton, RootBoneIndex);
				OutIssues.Add(Issue);
				continue;
			}

			SeenRootIndices.Add(RootBoneIndex);
			OutValidRootBoneIndices.Add(RootBoneIndex);
		}

		TSet<int32> NestedRootIndices;
		for (int32 IndexA = 0; IndexA < OutValidRootBoneIndices.Num(); ++IndexA)
		{
			for (int32 IndexB = 0; IndexB < OutValidRootBoneIndices.Num(); ++IndexB)
			{
				if (IndexA == IndexB)
				{
					continue;
				}

				const int32 AncestorIndex = OutValidRootBoneIndices[IndexA];
				const int32 DescendantIndex = OutValidRootBoneIndices[IndexB];
				if (IsAncestorOf(RefSkeleton, AncestorIndex, DescendantIndex))
				{
					if (!NestedRootIndices.Contains(DescendantIndex))
					{
						FBoneChainValidationIssue Issue;
						Issue.Type = EBoneChainValidationIssueType::NestedRoot;
						Issue.BoneName = GetBoneNameSafe(RefSkeleton, DescendantIndex);
						Issue.RelatedBoneName = GetBoneNameSafe(RefSkeleton, AncestorIndex);
						OutIssues.Add(Issue);
						NestedRootIndices.Add(DescendantIndex);
					}
				}
			}
		}

		OutValidRootBoneIndices.RemoveAll([&NestedRootIndices](int32 RootBoneIndex)
		{
			return NestedRootIndices.Contains(RootBoneIndex);
		});

		if (OutValidRootBoneIndices.Num() >= 2)
		{
			const int32 LCAIndex = FindLowestCommonAncestor(RefSkeleton, OutValidRootBoneIndices);
			if (LCAIndex == 0)
			{
				FBoneChainValidationIssue Issue;
				Issue.Type = EBoneChainValidationIssueType::LowestCommonAncestorIsSkeletonRoot;
				Issue.BoneName = GetBoneNameSafe(RefSkeleton, LCAIndex);
				OutIssues.Add(Issue);
			}
		}
	}

	void EnumerateLateralConnectionPairs(int32 NumChains, int32 LateralConnectionCount,
	                                     TArray<TPair<int32, int32>>& OutConnectionPairs)
	{
		OutConnectionPairs.Reset();
		if (NumChains < 2)
		{
			return;
		}

		const int32 OpenConnectionCount = LateralConnectionCount == 0
			                                  ? NumChains - 1
			                                  : FMath::Min(LateralConnectionCount, NumChains - 1);
		for (int32 ChainIndex = 0; ChainIndex < OpenConnectionCount; ++ChainIndex)
		{
			OutConnectionPairs.Add(TPair<int32, int32>(ChainIndex, ChainIndex + 1));
		}

		if (LateralConnectionCount == 0 && NumChains >= 3)
		{
			OutConnectionPairs.Add(TPair<int32, int32>(NumChains - 1, 0));
		}
	}

	void DiffBoneChainPairs(const TSet<FBoneChainPair>& ExistingChainGenPairs,
	                        const TSet<FBoneChainPair>& NewPairs,
	                        FBoneChainPairDiff& OutDiff)
	{
		OutDiff.KeptPairs.Reset();
		OutDiff.RemovedPairs.Reset();
		OutDiff.AddedPairs.Reset();

		const TSet<FBoneChainPair> CanonicalExistingPairs = CanonicalizePairs(ExistingChainGenPairs);
		const TSet<FBoneChainPair> CanonicalNewPairs = CanonicalizePairs(NewPairs);

		for (const FBoneChainPair& ExistingPair : CanonicalExistingPairs)
		{
			if (CanonicalNewPairs.Contains(ExistingPair))
			{
				OutDiff.KeptPairs.Add(ExistingPair);
			}
			else
			{
				OutDiff.RemovedPairs.Add(ExistingPair);
			}
		}

		for (const FBoneChainPair& NewPair : CanonicalNewPairs)
		{
			if (!CanonicalExistingPairs.Contains(NewPair))
			{
				OutDiff.AddedPairs.Add(NewPair);
			}
		}
	}
}
