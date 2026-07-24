// Copyright 2019-2026 pafuhana1213. All Rights Reserved.


#include "KawaiiPhysicsBoneConstraintsDataAsset.h"

#include "KawaiiPhysics.h"
#include "KawaiiPhysicsBoneChainUtils.h"
#include "Animation/Skeleton.h"
#include "Internationalization/Regex.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsBoneConstraintsDataAsset)

struct FBoneConstraintDataCustomVersion
{
	enum Type
	{
		// FNameからFBoneReferenceに移行
		ChangeToBoneReference = 0,

		// ------------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FBoneConstraintDataCustomVersion()
	{
	}
};

const FGuid FBoneConstraintDataCustomVersion::GUID(0xA1C4D3F6, 0x5B2E7A8D, 0x9F6E4B3C, 0xD7E1A8B2);
FCustomVersionRegistration GRegisterBoneConstraintDataCustomVersion(FBoneConstraintDataCustomVersion::GUID,
                                                                    FBoneConstraintDataCustomVersion::LatestVersion,
                                                                    TEXT("BoneConstraintData"));

void FModifyBoneConstraintData::Update(const FModifyBoneConstraint& BoneConstraint)
{
	BoneReference1 = BoneConstraint.Bone1;
	BoneReference2 = BoneConstraint.Bone2;
	bOverrideCompliance = BoneConstraint.bOverrideCompliance;
	ComplianceType = BoneConstraint.ComplianceType;
	bExcludeFromSubdivision = BoneConstraint.bExcludeFromSubdivision;
}

TArray<FModifyBoneConstraint> UKawaiiPhysicsBoneConstraintsDataAsset::GenerateBoneConstraints()
{
	TArray<FModifyBoneConstraint> BoneConstraints;

	for (const FModifyBoneConstraintData& BoneConstraintData : BoneConstraintsData)
	{
		FModifyBoneConstraint BoneConstraint;
		BoneConstraint.Bone1 = BoneConstraintData.BoneReference1;
		BoneConstraint.Bone2 = BoneConstraintData.BoneReference2;
		BoneConstraint.bOverrideCompliance = BoneConstraintData.bOverrideCompliance;
		BoneConstraint.ComplianceType = BoneConstraintData.ComplianceType;
		BoneConstraint.bExcludeFromSubdivision = BoneConstraintData.bExcludeFromSubdivision;

		BoneConstraints.Add(BoneConstraint);
	}

	return BoneConstraints;
}

void UKawaiiPhysicsBoneConstraintsDataAsset::Serialize(FStructuredArchiveRecord Record)
{
	Super::Serialize(Record);

	Record.GetUnderlyingArchive().UsingCustomVersion(FBoneConstraintDataCustomVersion::GUID);
}

void UKawaiiPhysicsBoneConstraintsDataAsset::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FBoneConstraintDataCustomVersion::GUID) <
		FBoneConstraintDataCustomVersion::ChangeToBoneReference)
	{
		for (auto& Data : BoneConstraintsData)
		{
			Data.BoneReference1 = FBoneReference(Data.BoneName1);
			Data.BoneReference2 = FBoneReference(Data.BoneName2);
		}

#if WITH_EDITOR
		UpdatePreviewBoneList();
#endif
		UE_LOG(LogKawaiiPhysics, Log, TEXT("Update : BoneName -> BoneReference (%s)"), *this->GetName());
	}
}

USkeleton* UKawaiiPhysicsBoneConstraintsDataAsset::GetSkeleton(bool& bInvalidSkeletonIsError,
                                                               const IPropertyHandle* PropertyHandle)
{
#if WITH_EDITOR
	return PreviewSkeleton.LoadSynchronous();
#else
	return nullptr;
#endif
}

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "KawaiiPhysicsBoneConstraintsDataAsset"

namespace
{
	struct FGeneratedChainPairSettings
	{
		bool bOverrideCompliance = false;
		EXPBDComplianceType ComplianceType = EXPBDComplianceType::Leather;
		bool bExcludeFromSubdivision = false;
	};

	void ShowApplyChainsNotification(const FText& NotificationText, SNotificationItem::ECompletionState CompletionState)
	{
		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = 5.0f;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(
			NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(CompletionState);
		}
	}

	FString GetChainGroupLogName(const FKawaiiPhysicsBoneChainGroup& Group, int32 GroupIndex)
	{
		return Group.GroupName.IsEmpty()
			       ? FString::Printf(TEXT("Group %d"), GroupIndex)
			       : Group.GroupName;
	}

	bool IsValidBoneConstraintPair(const KawaiiPhysicsBoneChain::FBoneChainPair& Pair)
	{
		return Pair.BoneNameA != NAME_None && Pair.BoneNameB != NAME_None && Pair.BoneNameA != Pair.BoneNameB;
	}

	KawaiiPhysicsBoneChain::FBoneChainPair MakePairFromConstraintData(const FModifyBoneConstraintData& Data)
	{
		return KawaiiPhysicsBoneChain::MakeBoneChainPair(Data.BoneReference1.BoneName,
		                                                 Data.BoneReference2.BoneName);
	}

	void LogValidationIssue(const FString& GroupLogName,
	                        const KawaiiPhysicsBoneChain::FBoneChainValidationIssue& Issue)
	{
		switch (Issue.Type)
		{
		case KawaiiPhysicsBoneChain::EBoneChainValidationIssueType::DuplicateRoot:
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("ApplyChains: group '%s' has a duplicate chain root '%s'. Later entry was ignored."),
			       *GroupLogName, *Issue.BoneName.ToString());
			break;
		case KawaiiPhysicsBoneChain::EBoneChainValidationIssueType::NestedRoot:
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("ApplyChains: group '%s' has nested chain root '%s' under '%s'. Descendant entry was ignored."),
			       *GroupLogName, *Issue.BoneName.ToString(), *Issue.RelatedBoneName.ToString());
			break;
		case KawaiiPhysicsBoneChain::EBoneChainValidationIssueType::LowestCommonAncestorIsSkeletonRoot:
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("ApplyChains: group '%s' chain roots have skeleton root '%s' as their lowest common ancestor. Unrelated parts may be mixed."),
			       *GroupLogName, *Issue.BoneName.ToString());
			break;
		case KawaiiPhysicsBoneChain::EBoneChainValidationIssueType::InvalidRoot:
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("ApplyChains: group '%s' has an invalid chain root. Entry was ignored."),
			       *GroupLogName);
			break;
		default:
			break;
		}
	}
}

void UKawaiiPhysicsBoneConstraintsDataAsset::ApplyRegex()
{
	GEditor->BeginTransaction(FText::FromString("ApplyRegex"));
	Modify();

	UpdatePreviewBoneList();

	for (FRegexPatternBoneSet& Pattern : RegexPatternList)
	{
		const FRegexPattern Pattern1 = FRegexPattern(Pattern.RegexPatternBone1);
		const FRegexPattern Pattern2 = FRegexPattern(Pattern.RegexPatternBone2);

		FRegexMatcher Matcher1(Pattern1, PreviewBoneListString);
		FRegexMatcher Matcher2(Pattern2, PreviewBoneListString);

		while (Matcher1.FindNext() && Matcher2.FindNext())
		{
			FModifyBoneConstraintData BoneConstraintData;
			BoneConstraintData.BoneReference1 = FBoneReference(FName(*Matcher1.GetCaptureGroup(0)));
			BoneConstraintData.BoneReference2 = FBoneReference(FName(*Matcher2.GetCaptureGroup(0)));
			BoneConstraintData.SourceType = EBoneConstraintSourceType::Regex;
			BoneConstraintsData.Add(BoneConstraintData);
		}
	}

	GEditor->EndTransaction();
}

void UKawaiiPhysicsBoneConstraintsDataAsset::ApplyChains()
{
	GEditor->BeginTransaction(FText::FromString("ApplyChains"));
	Modify();

	UpdatePreviewBoneList();

	USkeleton* LoadedPreviewSkeleton = PreviewSkeleton.Get();
	if (LoadedPreviewSkeleton == nullptr)
	{
		GEditor->EndTransaction();
		ShowApplyChainsNotification(LOCTEXT("ApplyChainsNoPreviewSkeleton",
		                                    "ApplyChains failed: PreviewSkeleton is not set or could not be loaded."),
		                            SNotificationItem::CS_Fail);
		return;
	}

	const FReferenceSkeleton& RefSkeleton = LoadedPreviewSkeleton->GetReferenceSkeleton();
	TSet<KawaiiPhysicsBoneChain::FBoneChainPair> NewPairs;
	TMap<KawaiiPhysicsBoneChain::FBoneChainPair, FGeneratedChainPairSettings> NewPairSettings;
	int32 AppliedGroupCount = 0;

	for (int32 GroupIndex = 0; GroupIndex < ChainGroups.Num(); ++GroupIndex)
	{
		const FKawaiiPhysicsBoneChainGroup& Group = ChainGroups[GroupIndex];
		const FString GroupLogName = GetChainGroupLogName(Group, GroupIndex);

		TArray<int32> RootBoneIndices;
		RootBoneIndices.Reserve(Group.ChainRootBones.Num());
		for (const FBoneReference& ChainRootBone : Group.ChainRootBones)
		{
			if (ChainRootBone.BoneName == NAME_None)
			{
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("ApplyChains: group '%s' has a None chain root. Entry was ignored."),
				       *GroupLogName);
				continue;
			}

			const int32 RootBoneIndex = RefSkeleton.FindBoneIndex(ChainRootBone.BoneName);
			if (!RefSkeleton.IsValidIndex(RootBoneIndex))
			{
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("ApplyChains: group '%s' has a chain root '%s' that does not exist in PreviewSkeleton. Entry was ignored."),
				       *GroupLogName, *ChainRootBone.BoneName.ToString());
				continue;
			}

			RootBoneIndices.Add(RootBoneIndex);
		}

		TArray<int32> ValidRootBoneIndices;
		TArray<KawaiiPhysicsBoneChain::FBoneChainValidationIssue> ValidationIssues;
		KawaiiPhysicsBoneChain::ValidateChainRoots(RefSkeleton, RootBoneIndices, ValidRootBoneIndices,
		                                           ValidationIssues);
		for (const KawaiiPhysicsBoneChain::FBoneChainValidationIssue& Issue : ValidationIssues)
		{
			LogValidationIssue(GroupLogName, Issue);
		}

		if (ValidRootBoneIndices.Num() < 2)
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("ApplyChains: group '%s' has fewer than two valid chain roots. Group was skipped."),
			       *GroupLogName);
			continue;
		}

		if (Group.LateralConnectionCount == 0 && ValidRootBoneIndices.Num() < 3)
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("ApplyChains: group '%s' uses loop mode but has fewer than three valid chains. Closing connection was omitted."),
			       *GroupLogName);
		}

		TArray<KawaiiPhysicsBoneChain::FBoneChain> Chains;
		Chains.Reserve(ValidRootBoneIndices.Num());
		for (int32 RootBoneIndex : ValidRootBoneIndices)
		{
			KawaiiPhysicsBoneChain::FBoneChain Chain;
			TArray<int32> DiscardedBranchRootIndices;
			if (!KawaiiPhysicsBoneChain::BuildChainFromRoot(RefSkeleton, RootBoneIndex, Chain, nullptr,
			                                                &DiscardedBranchRootIndices))
			{
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("ApplyChains: group '%s' failed to build chain from root '%s'. Chain was skipped."),
				       *GroupLogName, *RefSkeleton.GetBoneName(RootBoneIndex).ToString());
				continue;
			}

			if (DiscardedBranchRootIndices.Num() > 0)
			{
				TArray<FString> DiscardedBranchRootNames;
				DiscardedBranchRootNames.Reserve(DiscardedBranchRootIndices.Num());
				for (int32 DiscardedBranchRootIndex : DiscardedBranchRootIndices)
				{
					if (RefSkeleton.IsValidIndex(DiscardedBranchRootIndex))
					{
						DiscardedBranchRootNames.Add(RefSkeleton.GetBoneName(DiscardedBranchRootIndex).ToString());
					}
				}

				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("ApplyChains: group '%s' discarded branch roots while building chain from '%s': %s"),
				       *GroupLogName, *RefSkeleton.GetBoneName(RootBoneIndex).ToString(),
				       *FString::Join(DiscardedBranchRootNames, TEXT(", ")));
			}

			Chains.Add(Chain);
		}

		if (Chains.Num() < 2)
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("ApplyChains: group '%s' has fewer than two built chains. Group was skipped."),
			       *GroupLogName);
			continue;
		}

		TArray<TArray<float>> ChainRatios;
		ChainRatios.SetNum(Chains.Num());
		for (int32 ChainIndex = 0; ChainIndex < Chains.Num(); ++ChainIndex)
		{
			if (ChainRatioMode == EBoneChainRatioMode::LengthRatio)
			{
				KawaiiPhysicsBoneChain::MakeLengthRatios(RefSkeleton, Chains[ChainIndex], ChainRatios[ChainIndex]);
			}
			else
			{
				KawaiiPhysicsBoneChain::MakeIndexRatios(Chains[ChainIndex].BoneIndices.Num(),
				                                        ChainRatios[ChainIndex]);
			}
		}

		TArray<TPair<int32, int32>> ConnectionPairs;
		KawaiiPhysicsBoneChain::EnumerateLateralConnectionPairs(Chains.Num(), Group.LateralConnectionCount,
		                                                        ConnectionPairs);
		if (ConnectionPairs.Num() == 0)
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("ApplyChains: group '%s' generated no lateral connection pairs. Group was skipped."),
			       *GroupLogName);
			continue;
		}

		bool bGroupGeneratedAnyPair = false;
		for (const TPair<int32, int32>& ConnectionPair : ConnectionPairs)
		{
			TArray<TPair<int32, int32>> MatchedIndexPairs;
			KawaiiPhysicsBoneChain::MatchChainsByRatio(ChainRatios[ConnectionPair.Key],
			                                           ChainRatios[ConnectionPair.Value],
			                                           MatchedIndexPairs);

			for (const TPair<int32, int32>& MatchedIndexPair : MatchedIndexPairs)
			{
				const int32 BoneIndexA = Chains[ConnectionPair.Key].BoneIndices[MatchedIndexPair.Key];
				const int32 BoneIndexB = Chains[ConnectionPair.Value].BoneIndices[MatchedIndexPair.Value];
				const KawaiiPhysicsBoneChain::FBoneChainPair Pair = KawaiiPhysicsBoneChain::MakeBoneChainPair(
					RefSkeleton.GetBoneName(BoneIndexA), RefSkeleton.GetBoneName(BoneIndexB));

				if (!IsValidBoneConstraintPair(Pair))
				{
					continue;
				}

				if (!NewPairs.Contains(Pair))
				{
					FGeneratedChainPairSettings Settings;
					Settings.bOverrideCompliance = Group.bOverrideCompliance;
					Settings.ComplianceType = Group.ComplianceType;
					Settings.bExcludeFromSubdivision = Group.bExcludeFromSubdivision;
					NewPairSettings.Add(Pair, Settings);
				}

				NewPairs.Add(Pair);
				bGroupGeneratedAnyPair = true;
			}
		}

		if (bGroupGeneratedAnyPair)
		{
			++AppliedGroupCount;
		}
	}

	if (AppliedGroupCount == 0)
	{
		GEditor->EndTransaction();
		ShowApplyChainsNotification(LOCTEXT("ApplyChainsNoValidGroups",
		                                    "ApplyChains failed: no valid chain groups generated constraints."),
		                            SNotificationItem::CS_Fail);
		return;
	}

	TSet<KawaiiPhysicsBoneChain::FBoneChainPair> ExistingChainGenPairs;
	TSet<KawaiiPhysicsBoneChain::FBoneChainPair> ExistingManualOrRegexPairs;
	for (const FModifyBoneConstraintData& BoneConstraintData : BoneConstraintsData)
	{
		const KawaiiPhysicsBoneChain::FBoneChainPair Pair = MakePairFromConstraintData(BoneConstraintData);
		if (!IsValidBoneConstraintPair(Pair))
		{
			continue;
		}

		if (BoneConstraintData.SourceType == EBoneConstraintSourceType::ChainGen)
		{
			ExistingChainGenPairs.Add(Pair);
		}
		else
		{
			ExistingManualOrRegexPairs.Add(Pair);
		}
	}

	for (const KawaiiPhysicsBoneChain::FBoneChainPair& ExistingManualOrRegexPair : ExistingManualOrRegexPairs)
	{
		NewPairs.Remove(ExistingManualOrRegexPair);
		NewPairSettings.Remove(ExistingManualOrRegexPair);
	}

	KawaiiPhysicsBoneChain::FBoneChainPairDiff PairDiff;
	KawaiiPhysicsBoneChain::DiffBoneChainPairs(ExistingChainGenPairs, NewPairs, PairDiff);

	TSet<KawaiiPhysicsBoneChain::FBoneChainPair> RemovedPairSet;
	for (const KawaiiPhysicsBoneChain::FBoneChainPair& RemovedPair : PairDiff.RemovedPairs)
	{
		RemovedPairSet.Add(RemovedPair);
	}

	int32 RemovedCount = 0;
	TSet<KawaiiPhysicsBoneChain::FBoneChainPair> KeptChainGenPairs;
	for (int32 DataIndex = 0; DataIndex < BoneConstraintsData.Num();)
	{
		const KawaiiPhysicsBoneChain::FBoneChainPair Pair = MakePairFromConstraintData(BoneConstraintsData[DataIndex]);
		if (BoneConstraintsData[DataIndex].SourceType == EBoneConstraintSourceType::ChainGen
			&& (!IsValidBoneConstraintPair(Pair) || RemovedPairSet.Contains(Pair) || KeptChainGenPairs.Contains(Pair)))
		{
			BoneConstraintsData.RemoveAt(DataIndex);
			++RemovedCount;
			continue;
		}
		else if (BoneConstraintsData[DataIndex].SourceType == EBoneConstraintSourceType::ChainGen)
		{
			KeptChainGenPairs.Add(Pair);
		}

		++DataIndex;
	}

	int32 KeptCount = 0;
	for (const FModifyBoneConstraintData& BoneConstraintData : BoneConstraintsData)
	{
		const KawaiiPhysicsBoneChain::FBoneChainPair Pair = MakePairFromConstraintData(BoneConstraintData);
		if (BoneConstraintData.SourceType == EBoneConstraintSourceType::ChainGen
			&& IsValidBoneConstraintPair(Pair)
			&& NewPairs.Contains(Pair))
		{
			++KeptCount;
		}
	}

	int32 AddedCount = 0;
	for (const KawaiiPhysicsBoneChain::FBoneChainPair& Pair : PairDiff.AddedPairs)
	{
		const FGeneratedChainPairSettings* Settings = NewPairSettings.Find(Pair);
		if (Settings == nullptr)
		{
			continue;
		}

		FModifyBoneConstraintData BoneConstraintData;
		BoneConstraintData.BoneReference1 = FBoneReference(Pair.BoneNameA);
		BoneConstraintData.BoneReference2 = FBoneReference(Pair.BoneNameB);
		BoneConstraintData.bOverrideCompliance = Settings->bOverrideCompliance;
		BoneConstraintData.ComplianceType = Settings->ComplianceType;
		BoneConstraintData.bExcludeFromSubdivision = Settings->bExcludeFromSubdivision;
		BoneConstraintData.SourceType = EBoneConstraintSourceType::ChainGen;
		BoneConstraintsData.Add(BoneConstraintData);
		++AddedCount;
	}

	GEditor->EndTransaction();

	ShowApplyChainsNotification(FText::Format(
		                            LOCTEXT("ApplyChainsResult",
		                                    "Added {0} / kept {1} / removed {2} constraints ({3} groups)"),
		                            FText::AsNumber(AddedCount),
		                            FText::AsNumber(KeptCount),
		                            FText::AsNumber(RemovedCount),
		                            FText::AsNumber(AppliedGroupCount)),
	                            SNotificationItem::CS_Success);
}

void UKawaiiPhysicsBoneConstraintsDataAsset::DetectChains()
{
	USkeleton* LoadedPreviewSkeleton = PreviewSkeleton.LoadSynchronous();
	if (LoadedPreviewSkeleton == nullptr)
	{
		ShowApplyChainsNotification(LOCTEXT("DetectChainsNoPreviewSkeleton",
		                                    "DetectChains failed: PreviewSkeleton is not set or could not be loaded."),
		                            SNotificationItem::CS_Fail);
		return;
	}

	TArray<int32> TargetGroupIndices;
	TargetGroupIndices.Reserve(ChainGroups.Num());
	bool bNeedsOverwriteConfirmation = false;
	for (int32 GroupIndex = 0; GroupIndex < ChainGroups.Num(); ++GroupIndex)
	{
		const FKawaiiPhysicsBoneChainGroup& Group = ChainGroups[GroupIndex];
		if (Group.DetectRootBone.BoneName == NAME_None)
		{
			continue;
		}

		TargetGroupIndices.Add(GroupIndex);
		bNeedsOverwriteConfirmation = bNeedsOverwriteConfirmation || Group.ChainRootBones.Num() > 0;
	}

	if (TargetGroupIndices.Num() == 0)
	{
		ShowApplyChainsNotification(LOCTEXT("DetectChainsNoDetectRootBone",
		                                    "DetectChains failed: set DetectRootBone in a chain group."),
		                            SNotificationItem::CS_Fail);
		return;
	}

	if (bNeedsOverwriteConfirmation)
	{
		const EAppReturnType::Type DialogResult = FMessageDialog::Open(
			EAppMsgType::YesNo,
			LOCTEXT("DetectChainsOverwriteChainRootBones",
			        "Overwrite existing ChainRootBones?"));
		if (DialogResult != EAppReturnType::Yes)
		{
			return;
		}
	}

	GEditor->BeginTransaction(FText::FromString("DetectChains"));
	Modify();
	UpdatePreviewBoneList();

	const FReferenceSkeleton& RefSkeleton = LoadedPreviewSkeleton->GetReferenceSkeleton();
	int32 TotalDetectedRootCount = 0;
	int32 DetectedGroupCount = 0;

	for (int32 GroupIndex : TargetGroupIndices)
	{
		FKawaiiPhysicsBoneChainGroup& Group = ChainGroups[GroupIndex];
		const FString GroupLogName = GetChainGroupLogName(Group, GroupIndex);
		const FName SearchRootBoneName = Group.DetectRootBone.BoneName;
		const int32 SearchRootIndex = RefSkeleton.FindBoneIndex(SearchRootBoneName);
		if (!RefSkeleton.IsValidIndex(SearchRootIndex))
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("DetectChains: group '%s' has a DetectRootBone '%s' that does not exist in PreviewSkeleton. Group was skipped."),
			       *GroupLogName, *SearchRootBoneName.ToString());
			continue;
		}

		TArray<int32> CandidateRootIndices;
		KawaiiPhysicsBoneChain::DetectChainRootCandidates(RefSkeleton, SearchRootIndex, CandidateRootIndices);

		Group.ChainRootBones.Reset();
		Group.ChainRootBones.Reserve(CandidateRootIndices.Num());

		TArray<FString> CandidateRootNames;
		CandidateRootNames.Reserve(CandidateRootIndices.Num());
		for (int32 CandidateRootIndex : CandidateRootIndices)
		{
			if (!RefSkeleton.IsValidIndex(CandidateRootIndex))
			{
				continue;
			}

			const FName CandidateRootBoneName = RefSkeleton.GetBoneName(CandidateRootIndex);
			Group.ChainRootBones.Add(FBoneReference(CandidateRootBoneName));
			CandidateRootNames.Add(CandidateRootBoneName.ToString());
		}

		const int32 DetectedRootCount = Group.ChainRootBones.Num();
		TotalDetectedRootCount += DetectedRootCount;
		++DetectedGroupCount;

		const FString CandidateRootNamesText = CandidateRootNames.Num() > 0
			                                       ? FString::Join(CandidateRootNames, TEXT(", "))
			                                       : FString(TEXT("(none)"));
		UE_LOG(LogKawaiiPhysics, Log,
		       TEXT("DetectChains: group '%s' detected %d chain roots under '%s': %s"),
		       *GroupLogName, DetectedRootCount, *SearchRootBoneName.ToString(),
		       *CandidateRootNamesText);

		if (DetectedRootCount < 2)
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("DetectChains: group '%s' detected fewer than two chain roots. Set ChainRootBones manually."),
			       *GroupLogName);
		}
	}

	GEditor->EndTransaction();

	if (DetectedGroupCount == 0)
	{
		ShowApplyChainsNotification(LOCTEXT("DetectChainsNoDetectedRoots",
		                                    "DetectChains failed: no chain roots were detected."),
		                            SNotificationItem::CS_Fail);
		return;
	}

	ShowApplyChainsNotification(FText::Format(
		                            LOCTEXT("DetectChainsResult",
		                                    "Detected {0} chain roots in {1} groups"),
		                            FText::AsNumber(TotalDetectedRootCount),
		                            FText::AsNumber(DetectedGroupCount)),
	                            SNotificationItem::CS_Success);
}

void UKawaiiPhysicsBoneConstraintsDataAsset::UpdatePreviewBoneList()
{
	PreviewBoneList.Empty();
	PreviewBoneListString.Empty();

	if (!PreviewSkeleton.IsValid())
	{
		PreviewSkeleton.LoadSynchronous();
	}

	if (PreviewSkeleton.IsValid())
	{
		const FReferenceSkeleton& RefSkeleton = PreviewSkeleton->GetReferenceSkeleton();
		const TArray<FMeshBoneInfo>& RefBoneInfo = RefSkeleton.GetRefBoneInfo();

		for (const FMeshBoneInfo& BoneInfo : RefBoneInfo)
		{
			PreviewBoneList.Add(BoneInfo.Name);
			PreviewBoneListString.Append(BoneInfo.Name.ToString());
			PreviewBoneListString.Append(TEXT(", "));
		}
	}
}

void UKawaiiPhysicsBoneConstraintsDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.MemberProperty
		                           ? PropertyChangedEvent.MemberProperty->GetFName()
		                           : NAME_None;

	if (PropertyName == FName(TEXT("PreviewSkeleton")))
	{
		UpdatePreviewBoneList();
	}

	const bool bSourceTypeChanged = PropertyChangedEvent.Property != nullptr
		&& PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FModifyBoneConstraintData, SourceType);

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsBoneConstraintsDataAsset, BoneConstraintsData)
		&& !bSourceTypeChanged)
	{
		const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(
			GET_MEMBER_NAME_STRING_CHECKED(UKawaiiPhysicsBoneConstraintsDataAsset, BoneConstraintsData));
		if (BoneConstraintsData.IsValidIndex(ArrayIndex))
		{
			BoneConstraintsData[ArrayIndex].SourceType = EBoneConstraintSourceType::Manual;
		}
	}
}

#undef LOCTEXT_NAMESPACE

#endif
