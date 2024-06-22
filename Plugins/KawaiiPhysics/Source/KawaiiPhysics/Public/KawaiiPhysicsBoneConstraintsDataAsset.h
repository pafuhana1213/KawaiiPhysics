// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Engine/DataAsset.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.generated.h"

USTRUCT(BlueprintType)
struct FModifyBoneConstraintData
{
	GENERATED_BODY()

	// deprecated
	UPROPERTY()
	FName BoneName1;
	// deprecated
	UPROPERTY()
	FName BoneName2;

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FBoneReference BoneReference1;

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FBoneReference BoneReference2;

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics", meta=(InlineEditConditionToggle))
	bool bOverrideCompliance = false;

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics", meta=(EditCondition="bOverrideCompliance"))
	EXPBDComplianceType ComplianceType = EXPBDComplianceType::Leather;
};

USTRUCT(BlueprintType)
struct FRegexPatternBoneSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Helper")
	FString RegexPatternBone1;
	UPROPERTY(EditAnywhere, Category="Helper")
	FString RegexPatternBone2;
};


UCLASS(Blueprintable)
class KAWAIIPHYSICS_API UKawaiiPhysicsBoneConstraintsDataAsset : public UDataAsset,
                                                                 public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta=(TitleProperty="{BoneReference1} - {BoneReference2}"))
	TArray<FModifyBoneConstraintData> BoneConstraintsData;

#if WITH_EDITORONLY_DATA

	UPROPERTY(EditAnywhere, Category="Helper")
	TArray<FRegexPatternBoneSet> RegexPatternList;

	UPROPERTY(EditAnywhere, Category = "Skeleton")
	TSoftObjectPtr<USkeleton> PreviewSkeleton;
	UPROPERTY(VisibleAnywhere, Category = "Skeleton", meta= (EditCondition=false))
	TArray<FBoneReference> PreviewBoneList;
	UPROPERTY()
	FString PreviewBoneListString;
#endif

public:
	// Begin UObject Interface.
	virtual void Serialize(FStructuredArchiveRecord Record) override;
	virtual void PostLoad() override;
	// End UObject Interface.

	// IBoneReferenceSkeletonProvider interface
	virtual USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

	TArray<FModifyBoneConstraint> GenerateBoneConstraints();

#if WITH_EDITOR

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Helper")
	void ApplyRegex();

	void UpdatePreviewBoneList();
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
