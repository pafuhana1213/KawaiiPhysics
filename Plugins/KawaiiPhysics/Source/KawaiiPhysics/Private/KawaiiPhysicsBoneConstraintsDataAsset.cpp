// Fill out your copyright notice in the Description page of Project Settings.


#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "Internationalization/Regex.h"

TArray<FModifyBoneConstraint> UKawaiiPhysicsBoneConstraintsDataAsset::GenerateBoneConstraints()
{
	TArray<FModifyBoneConstraint> BoneConstraints;

	for (const FModifyBoneConstraintData BoneConstraintData : BoneConstraintsData)
	{
		FModifyBoneConstraint BoneConstraint;
		BoneConstraint.Bone1.BoneName = BoneConstraintData.BoneName1;
		BoneConstraint.Bone2.BoneName = BoneConstraintData.BoneName2;
		BoneConstraint.bOverrideCompliance = BoneConstraintData.bOverrideCompliance;
		BoneConstraint.ComplianceType = BoneConstraintData.ComplianceType;

		BoneConstraints.Add(BoneConstraint);
	}

	return BoneConstraints;
}

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "KawaiiPhysicsBoneConstraintsDataAsset"

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
			BoneConstraintData.BoneName1 = FName(*Matcher1.GetCaptureGroup(0));
			BoneConstraintData.BoneName2 = FName(*Matcher2.GetCaptureGroup(0));
			BoneConstraintsData.Add(BoneConstraintData);
		}
	}

	GEditor->EndTransaction();
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

	if (PropertyName == FName(TEXT("PrewviewSkeleton")))
	{
		UpdatePreviewBoneList();
	}
}

#undef LOCTEXT_NAMESPACE

#endif
