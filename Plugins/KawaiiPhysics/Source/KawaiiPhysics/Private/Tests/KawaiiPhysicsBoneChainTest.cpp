// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsBoneChainUtils.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"

namespace
{
	constexpr float GBoneChainRatioTol = 0.0001f;

	FReferenceSkeleton MakeLinearSkeleton(int32 NumBones, const FString& Prefix = TEXT("bone"))
	{
		FReferenceSkeleton RefSkeleton;
		FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
		for (int32 Index = 0; Index < NumBones; ++Index)
		{
			const FName BoneName(*FString::Printf(TEXT("%s_%d"), *Prefix, Index));
			Modifier.Add(FMeshBoneInfo(BoneName, BoneName.ToString(), Index - 1),
			             FTransform(FQuat::Identity, FVector(1.0f, 0.0f, 0.0f)));
		}
		return RefSkeleton;
	}

	bool ContainsPair(const TArray<TPair<int32, int32>>& Pairs, int32 A, int32 B)
	{
		return Pairs.ContainsByPredicate([A, B](const TPair<int32, int32>& Pair)
		{
			return Pair.Key == A && Pair.Value == B;
		});
	}

	bool ContainsBonePair(const TArray<KawaiiPhysicsBoneChain::FBoneChainPair>& Pairs, FName BoneNameA, FName BoneNameB)
	{
		const KawaiiPhysicsBoneChain::FBoneChainPair Expected =
			KawaiiPhysicsBoneChain::MakeBoneChainPair(BoneNameA, BoneNameB);
		return Pairs.ContainsByPredicate([Expected](const KawaiiPhysicsBoneChain::FBoneChainPair& Pair)
		{
			return Pair == Expected;
		});
	}

	bool IsMonotonicPairs(const TArray<TPair<int32, int32>>& Pairs)
	{
		for (int32 Index = 1; Index < Pairs.Num(); ++Index)
		{
			if (Pairs[Index - 1].Key > Pairs[Index].Key || Pairs[Index - 1].Value > Pairs[Index].Value)
			{
				return false;
			}
		}
		return true;
	}

	int32 CountUniquePairs(const TArray<TPair<int32, int32>>& Pairs)
	{
		TSet<uint64> PairKeys;
		for (const TPair<int32, int32>& Pair : Pairs)
		{
			const uint64 PairKey = (static_cast<uint64>(static_cast<uint32>(Pair.Key)) << 32)
				| static_cast<uint32>(Pair.Value);
			PairKeys.Add(PairKey);
		}
		return PairKeys.Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoneChainBuildTest,
                                 "KawaiiPhysics.BoneChain.BuildChain",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoneChainBuildTest::RunTest(const FString& Parameters)
{
	{
		const FReferenceSkeleton RefSkeleton = MakeLinearSkeleton(5);
		KawaiiPhysicsBoneChain::FBoneChain Chain;
		TestTrue(TEXT("BuildChain accepts a linear chain"), KawaiiPhysicsBoneChain::BuildChainFromRoot(RefSkeleton, 0, Chain));
		TestEqual(TEXT("Linear chain has 5 bones"), Chain.BoneIndices.Num(), 5);
		if (Chain.BoneIndices.Num() == 0)
		{
			AddError(TEXT("Linear chain does not contain enough bones"));
		}
		else
		{
			TestEqual(TEXT("Linear chain reaches the tip"), Chain.BoneIndices.Last(), 4);
		}
	}

	{
		FReferenceSkeleton RefSkeleton;
		{
			FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("short"), TEXT("short"), 0), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("long"), TEXT("long"), 0), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("long_tip"), TEXT("long_tip"), 2), FTransform::Identity);
		}

		KawaiiPhysicsBoneChain::FBoneChain Chain;
		TArray<int32> Discarded;
		TestTrue(TEXT("BuildChain accepts a branched chain"),
		         KawaiiPhysicsBoneChain::BuildChainFromRoot(RefSkeleton, 0, Chain, nullptr, &Discarded));
		if (Chain.BoneIndices.Num() <= 1)
		{
			AddError(TEXT("Branched chain does not contain enough bones"));
		}
		else
		{
			TestEqual(TEXT("Longest branch is selected"), Chain.BoneIndices[1], 2);
			TestTrue(TEXT("Discarded branch is reported"), Discarded.Contains(1));
		}
	}

	{
		FReferenceSkeleton RefSkeleton;
		{
			FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("tie_a"), TEXT("tie_a"), 0), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("tie_b"), TEXT("tie_b"), 0), FTransform::Identity);
		}

		KawaiiPhysicsBoneChain::FBoneChain Chain;
		TArray<int32> Discarded;
		KawaiiPhysicsBoneChain::BuildChainFromRoot(RefSkeleton, 0, Chain, nullptr, &Discarded);
		if (Chain.BoneIndices.Num() <= 1)
		{
			AddError(TEXT("Tie-break chain does not contain enough bones"));
		}
		else
		{
			TestEqual(TEXT("Tie break selects the smallest bone index"), Chain.BoneIndices[1], 1);
			TestTrue(TEXT("Tie loser is reported"), Discarded.Contains(2));
		}
	}

	{
		const FReferenceSkeleton RefSkeleton = MakeLinearSkeleton(1);
		KawaiiPhysicsBoneChain::FBoneChain Chain;
		TestTrue(TEXT("Single root chain is valid"), KawaiiPhysicsBoneChain::BuildChainFromRoot(RefSkeleton, 0, Chain));
		TestEqual(TEXT("Single root chain length is 1"), Chain.BoneIndices.Num(), 1);
	}

	{
		const FReferenceSkeleton RefSkeleton = MakeLinearSkeleton(5);
		TSet<int32> Excluded;
		Excluded.Add(3);

		KawaiiPhysicsBoneChain::FBoneChain Chain;
		KawaiiPhysicsBoneChain::BuildChainFromRoot(RefSkeleton, 0, Chain, &Excluded);
		TestEqual(TEXT("Excluded bone truncates the chain"), Chain.BoneIndices.Num(), 3);
		if (Chain.BoneIndices.Num() == 0)
		{
			AddError(TEXT("Excluded chain does not contain enough bones"));
		}
		else
		{
			TestEqual(TEXT("Chain stops before excluded bone"), Chain.BoneIndices.Last(), 2);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoneChainMatchRatioTest,
                                 "KawaiiPhysics.BoneChain.MatchChainsByRatio",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoneChainMatchRatioTest::RunTest(const FString& Parameters)
{
	TArray<float> RatiosA;
	TArray<float> RatiosB;
	TArray<TPair<int32, int32>> Pairs;

	KawaiiPhysicsBoneChain::MakeIndexRatios(4, RatiosA);
	KawaiiPhysicsBoneChain::MakeIndexRatios(4, RatiosB);
	KawaiiPhysicsBoneChain::MatchChainsByRatio(RatiosA, RatiosB, Pairs);
	TestEqual(TEXT("Equal counts are identity matched"), Pairs.Num(), 4);
	TestTrue(TEXT("Identity pair exists"), ContainsPair(Pairs, 3, 3));

	KawaiiPhysicsBoneChain::MakeIndexRatios(5, RatiosA);
	KawaiiPhysicsBoneChain::MakeIndexRatios(3, RatiosB);
	KawaiiPhysicsBoneChain::MatchChainsByRatio(RatiosA, RatiosB, Pairs);
	TestEqual(TEXT("5vs3 maps each longer-chain bone"), Pairs.Num(), 5);
	TestTrue(TEXT("5vs3 keeps first endpoint"), ContainsPair(Pairs, 0, 0));
	TestTrue(TEXT("5vs3 keeps last endpoint"), ContainsPair(Pairs, 4, 2));
	TestTrue(TEXT("5vs3 is monotonic"), IsMonotonicPairs(Pairs));

	KawaiiPhysicsBoneChain::MakeIndexRatios(2, RatiosA);
	KawaiiPhysicsBoneChain::MakeIndexRatios(5, RatiosB);
	KawaiiPhysicsBoneChain::MatchChainsByRatio(RatiosA, RatiosB, Pairs);
	TestEqual(TEXT("2vs5 maps each longer-chain bone"), Pairs.Num(), 5);
	TestTrue(TEXT("2vs5 keeps first endpoint"), ContainsPair(Pairs, 0, 0));
	TestTrue(TEXT("2vs5 keeps last endpoint"), ContainsPair(Pairs, 1, 4));
	TestTrue(TEXT("2vs5 is monotonic"), IsMonotonicPairs(Pairs));

	KawaiiPhysicsBoneChain::MakeIndexRatios(1, RatiosA);
	KawaiiPhysicsBoneChain::MakeIndexRatios(4, RatiosB);
	KawaiiPhysicsBoneChain::MatchChainsByRatio(RatiosA, RatiosB, Pairs);
	TestEqual(TEXT("1vsN fans in to the single bone"), Pairs.Num(), 4);
	TestTrue(TEXT("1vsN contains the tip connection"), ContainsPair(Pairs, 0, 3));

	RatiosA = {0.0f, 0.5f, 0.5f, 1.0f};
	RatiosB = {0.0f, 0.5f, 1.0f};
	KawaiiPhysicsBoneChain::MatchChainsByRatio(RatiosA, RatiosB, Pairs);
	TestEqual(TEXT("Duplicate pair keys are removed"), CountUniquePairs(Pairs), Pairs.Num());
	TestTrue(TEXT("Duplicate-ratio case remains monotonic"), IsMonotonicPairs(Pairs));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoneChainLengthRatioTest,
                                 "KawaiiPhysics.BoneChain.LengthRatio",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoneChainLengthRatioTest::RunTest(const FString& Parameters)
{
	{
		FReferenceSkeleton RefSkeleton;
		{
			FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE),
			             FTransform(FQuat::Identity, FVector(0.0f, 0.0f, 0.0f)));
			Modifier.Add(FMeshBoneInfo(TEXT("near"), TEXT("near"), 0),
			             FTransform(FQuat::Identity, FVector(1.0f, 0.0f, 0.0f)));
			Modifier.Add(FMeshBoneInfo(TEXT("far"), TEXT("far"), 1),
			             FTransform(FQuat::Identity, FVector(9.0f, 0.0f, 0.0f)));
		}

		KawaiiPhysicsBoneChain::FBoneChain Chain;
		Chain.BoneIndices = {0, 1, 2};

		TArray<float> LengthRatios;
		TArray<float> IndexRatios;
		KawaiiPhysicsBoneChain::MakeLengthRatios(RefSkeleton, Chain, LengthRatios);
		KawaiiPhysicsBoneChain::MakeIndexRatios(3, IndexRatios);

		if (LengthRatios.Num() <= 1 || IndexRatios.Num() <= 1)
		{
			AddError(TEXT("Uneven-spacing ratios do not contain enough elements"));
		}
		else
		{
			TestTrue(TEXT("Length ratio differs from index ratio on uneven spacing"),
			         FMath::Abs(LengthRatios[1] - IndexRatios[1]) > GBoneChainRatioTol);
			TestTrue(TEXT("Length ratio middle value reflects cumulative distance"),
			         FMath::IsNearlyEqual(LengthRatios[1], 0.1f, GBoneChainRatioTol));
		}
	}

	{
		FReferenceSkeleton RefSkeleton;
		{
			FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("zero_a"), TEXT("zero_a"), 0), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("zero_b"), TEXT("zero_b"), 1), FTransform::Identity);
		}

		KawaiiPhysicsBoneChain::FBoneChain Chain;
		Chain.BoneIndices = {0, 1, 2};

		TArray<float> LengthRatios;
		KawaiiPhysicsBoneChain::MakeLengthRatios(RefSkeleton, Chain, LengthRatios);
		if (LengthRatios.Num() <= 1)
		{
			AddError(TEXT("Zero-length ratios do not contain enough elements"));
		}
		else
		{
			TestTrue(TEXT("Zero length falls back to index ratio"),
			         FMath::IsNearlyEqual(LengthRatios[1], 0.5f, GBoneChainRatioTol));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoneChainDetectRootsTest,
                                 "KawaiiPhysics.BoneChain.DetectChainRoots",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoneChainDetectRootsTest::RunTest(const FString& Parameters)
{
	{
		FReferenceSkeleton RefSkeleton;
		{
			FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			for (int32 Index = 0; Index < 8; ++Index)
			{
				const FName BoneName(*FString::Printf(TEXT("skirt_%02d"), Index));
				Modifier.Add(FMeshBoneInfo(BoneName, BoneName.ToString(), 0), FTransform::Identity);
			}
		}

		TArray<int32> Candidates;
		KawaiiPhysicsBoneChain::DetectChainRootCandidates(RefSkeleton, 0, Candidates);
		TestEqual(TEXT("Direct children are detected"), Candidates.Num(), 8);
		if (Candidates.Num() == 0)
		{
			AddError(TEXT("Direct-child candidates do not contain enough elements"));
		}
		else
		{
			TestEqual(TEXT("Numeric order starts at skirt_00"), Candidates[0], 1);
		}
	}

	{
		FReferenceSkeleton RefSkeleton;
		{
			FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("middle"), TEXT("middle"), 0), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("skirt_10"), TEXT("skirt_10"), 1), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("skirt_2"), TEXT("skirt_2"), 1), FTransform::Identity);
		}

		TArray<int32> Candidates;
		KawaiiPhysicsBoneChain::DetectChainRootCandidates(RefSkeleton, 0, Candidates);
		TestEqual(TEXT("Single middle bone is skipped"), Candidates.Num(), 2);
		if (Candidates.Num() <= 1)
		{
			AddError(TEXT("Middle-skipped candidates do not contain enough elements"));
		}
		else
		{
			TestEqual(TEXT("Numeric sort treats 10 as greater than 2"), Candidates[0], 3);
			TestEqual(TEXT("Numeric sort places 10 after 2"), Candidates[1], 2);
		}
	}

	{
		FReferenceSkeleton RefSkeleton;
		{
			FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("right"), TEXT("right"), 0),
			             FTransform(FQuat::Identity, FVector(1.0f, 0.0f, 0.0f)));
			Modifier.Add(FMeshBoneInfo(TEXT("up"), TEXT("up"), 0),
			             FTransform(FQuat::Identity, FVector(0.0f, 1.0f, 0.0f)));
			Modifier.Add(FMeshBoneInfo(TEXT("left"), TEXT("left"), 0),
			             FTransform(FQuat::Identity, FVector(-1.0f, 0.0f, 0.0f)));
			Modifier.Add(FMeshBoneInfo(TEXT("down"), TEXT("down"), 0),
			             FTransform(FQuat::Identity, FVector(0.0f, -1.0f, 0.0f)));
		}

		TArray<int32> Candidates = {1, 2, 3, 4};
		TestFalse(TEXT("Names without numeric tokens do not numeric-sort"),
		          KawaiiPhysicsBoneChain::SortByNumericTokens(RefSkeleton, Candidates));
		KawaiiPhysicsBoneChain::SortByRefPoseAngle(RefSkeleton, 0, Candidates);
		if (Candidates.Num() == 0)
		{
			AddError(TEXT("Angle-sorted candidates do not contain enough elements"));
		}
		else
		{
			TestEqual(TEXT("Angle sort starts at -PI/2"), Candidates[0], 4);
			TestEqual(TEXT("Angle sort ends at PI"), Candidates.Last(), 3);
		}
	}

	{
		FReferenceSkeleton RefSkeleton;
		{
			FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("skirt_00"), TEXT("skirt_00"), 0), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("skirt_01"), TEXT("skirt_01"), 0), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("skirt_02"), TEXT("skirt_02"), 0), FTransform::Identity);
		}

		TSet<int32> Excluded;
		Excluded.Add(2);

		TArray<int32> Candidates;
		KawaiiPhysicsBoneChain::DetectChainRootCandidates(RefSkeleton, 0, Candidates, &Excluded);
		TestEqual(TEXT("Excluded roots are omitted"), Candidates.Num(), 2);
		TestFalse(TEXT("Excluded index is absent"), Candidates.Contains(2));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoneChainValidationTest,
                                 "KawaiiPhysics.BoneChain.Validation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoneChainValidationTest::RunTest(const FString& Parameters)
{
	FReferenceSkeleton RefSkeleton;
	{
		FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("part_a"), TEXT("part_a"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("part_a_child"), TEXT("part_a_child"), 1), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("part_b"), TEXT("part_b"), 0), FTransform::Identity);
	}

	TArray<int32> ValidRoots;
	TArray<KawaiiPhysicsBoneChain::FBoneChainValidationIssue> Issues;
	TArray<int32> RootIndices;
	RootIndices.Add(1);
	RootIndices.Add(1);
	RootIndices.Add(2);
	RootIndices.Add(3);
	KawaiiPhysicsBoneChain::ValidateChainRoots(RefSkeleton, RootIndices, ValidRoots, Issues);

	TestTrue(TEXT("Duplicate root is reported"), Issues.ContainsByPredicate(
		[](const KawaiiPhysicsBoneChain::FBoneChainValidationIssue& Issue)
		{
			return Issue.Type == KawaiiPhysicsBoneChain::EBoneChainValidationIssueType::DuplicateRoot
				&& Issue.BoneName == FName(TEXT("part_a"));
		}));
	TestTrue(TEXT("Nested root is reported"), Issues.ContainsByPredicate(
		[](const KawaiiPhysicsBoneChain::FBoneChainValidationIssue& Issue)
		{
			return Issue.Type == KawaiiPhysicsBoneChain::EBoneChainValidationIssueType::NestedRoot
				&& Issue.BoneName == FName(TEXT("part_a_child"))
				&& Issue.RelatedBoneName == FName(TEXT("part_a"));
		}));
	TestTrue(TEXT("Skeleton-root LCA is reported"), Issues.ContainsByPredicate(
		[](const KawaiiPhysicsBoneChain::FBoneChainValidationIssue& Issue)
		{
			return Issue.Type == KawaiiPhysicsBoneChain::EBoneChainValidationIssueType::LowestCommonAncestorIsSkeletonRoot
				&& Issue.BoneName == FName(TEXT("root"));
		}));
	TestTrue(TEXT("Nested descendant is removed from valid roots"), !ValidRoots.Contains(2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoneChainDiffTest,
                                 "KawaiiPhysics.BoneChain.PairDiff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoneChainDiffTest::RunTest(const FString& Parameters)
{
	TSet<KawaiiPhysicsBoneChain::FBoneChainPair> ExistingPairs;
	ExistingPairs.Add(KawaiiPhysicsBoneChain::MakeBoneChainPair(FName(TEXT("a")), FName(TEXT("b"))));
	ExistingPairs.Add(KawaiiPhysicsBoneChain::MakeBoneChainPair(FName(TEXT("c")), FName(TEXT("d"))));

	TSet<KawaiiPhysicsBoneChain::FBoneChainPair> NewPairs;
	NewPairs.Add(KawaiiPhysicsBoneChain::MakeBoneChainPair(FName(TEXT("b")), FName(TEXT("a"))));
	NewPairs.Add(KawaiiPhysicsBoneChain::MakeBoneChainPair(FName(TEXT("e")), FName(TEXT("f"))));

	KawaiiPhysicsBoneChain::FBoneChainPairDiff Diff;
	KawaiiPhysicsBoneChain::DiffBoneChainPairs(ExistingPairs, NewPairs, Diff);

	TestEqual(TEXT("One pair is kept"), Diff.KeptPairs.Num(), 1);
	TestEqual(TEXT("One pair is removed"), Diff.RemovedPairs.Num(), 1);
	TestEqual(TEXT("One pair is added"), Diff.AddedPairs.Num(), 1);
	TestTrue(TEXT("Kept pair is classified"), ContainsBonePair(Diff.KeptPairs, FName(TEXT("a")), FName(TEXT("b"))));
	TestTrue(TEXT("Removed pair is classified"), ContainsBonePair(Diff.RemovedPairs, FName(TEXT("c")), FName(TEXT("d"))));
	TestTrue(TEXT("Added pair is classified"), ContainsBonePair(Diff.AddedPairs, FName(TEXT("e")), FName(TEXT("f"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoneChainLateralConnectionTest,
                                 "KawaiiPhysics.BoneChain.LateralConnectionCount",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoneChainLateralConnectionTest::RunTest(const FString& Parameters)
{
	TArray<TPair<int32, int32>> Pairs;

	KawaiiPhysicsBoneChain::EnumerateLateralConnectionPairs(4, 0, Pairs);
	TestEqual(TEXT("Loop mode creates all open adjacencies plus close edge"), Pairs.Num(), 4);
	TestTrue(TEXT("Loop close edge exists"), ContainsPair(Pairs, 3, 0));

	KawaiiPhysicsBoneChain::EnumerateLateralConnectionPairs(4, 2, Pairs);
	TestEqual(TEXT("Partial mode creates only the requested leading connections"), Pairs.Num(), 2);
	TestTrue(TEXT("First leading connection exists"), ContainsPair(Pairs, 0, 1));
	TestTrue(TEXT("Second leading connection exists"), ContainsPair(Pairs, 1, 2));
	TestFalse(TEXT("Partial mode has no close edge"), ContainsPair(Pairs, 3, 0));

	KawaiiPhysicsBoneChain::EnumerateLateralConnectionPairs(4, 10, Pairs);
	TestEqual(TEXT("Large partial count creates open adjacency only"), Pairs.Num(), 3);
	TestTrue(TEXT("Open final adjacency exists"), ContainsPair(Pairs, 2, 3));
	TestFalse(TEXT("Large partial count has no close edge"), ContainsPair(Pairs, 3, 0));

	return true;
}

#endif // 開発用自動テスト
