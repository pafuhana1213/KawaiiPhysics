// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Engine/DataAsset.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.generated.h"

USTRUCT(BlueprintType)
struct FModifyBoneConstraintData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FName BoneName1;

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FName BoneName2;

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
class KAWAIIPHYSICS_API UKawaiiPhysicsBoneConstraintsDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta=(TitleProperty="{BoneName1} - {BoneName2}"))
	TArray<FModifyBoneConstraintData> BoneConstraintsData;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category="Helper")
	TArray<FRegexPatternBoneSet> RegexPatternList;

	UPROPERTY(EditAnywhere, Category = "Preview")
	TSoftObjectPtr<USkeleton> PreviewSkeleton;
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TArray<FName> PreviewBoneList;
	UPROPERTY()
	FString PreviewBoneListString;
#endif

public:
	TArray<FModifyBoneConstraint> GenerateBoneConstraints();

#if WITH_EDITOR

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Helper")
	void ApplyRegex();

	void UpdatePreviewBoneList();
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
