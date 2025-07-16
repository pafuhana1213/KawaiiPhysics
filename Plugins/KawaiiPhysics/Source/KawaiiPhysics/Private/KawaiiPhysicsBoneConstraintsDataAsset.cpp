// Copyright 2019-2025 pafuhana1213. All Rights Reserved.


#include "KawaiiPhysicsBoneConstraintsDataAsset.h"

#include "KawaiiPhysics.h"
#include "Internationalization/Regex.h"

#if WITH_EDITOR
#include "Editor.h"
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

	if (PropertyName == FName(TEXT("PreviewSkeleton")))
	{
		UpdatePreviewBoneList();
	}
}

#undef LOCTEXT_NAMESPACE

#endif
