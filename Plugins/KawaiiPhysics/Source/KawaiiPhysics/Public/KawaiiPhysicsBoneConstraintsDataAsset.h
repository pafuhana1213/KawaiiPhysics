// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Engine/DataAsset.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.generated.h"

/**
 * Struct representing the data for modifying bone constraints in KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FModifyBoneConstraintData
{
	GENERATED_BODY()

	/** Name of the first bone (deprecated) */
	UPROPERTY()
	FName BoneName1;

	/** Name of the second bone (deprecated) */
	UPROPERTY()
	FName BoneName2;

	/** Reference to the first bone */
	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FBoneReference BoneReference1;

	/** Reference to the second bone */
	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FBoneReference BoneReference2;

	/** Whether to override the compliance type */
	UPROPERTY(EditAnywhere, category = "KawaiiPhysics", meta=(InlineEditConditionToggle))
	bool bOverrideCompliance = false;

	/** The compliance type to use if overriding */
	UPROPERTY(EditAnywhere, category = "KawaiiPhysics", meta=(EditCondition="bOverrideCompliance"))
	EXPBDComplianceType ComplianceType = EXPBDComplianceType::Leather;

	/**
	 * Updates the bone constraint data with the given constraint.
	 * @param BoneConstraint The bone constraint to update from.
	 */
	void Update(const FModifyBoneConstraint& BoneConstraint);
};

/**
 * Struct representing a set of regex patterns for bones in KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct FRegexPatternBoneSet
{
	GENERATED_BODY()

	/** Regex pattern for the first bone */
	UPROPERTY(EditAnywhere, Category="Helper")
	FString RegexPatternBone1;

	/** Regex pattern for the second bone */
	UPROPERTY(EditAnywhere, Category="Helper")
	FString RegexPatternBone2;
};


/**
 * Data asset for managing bone constraints in KawaiiPhysics.
 */
UCLASS(Blueprintable)
class KAWAIIPHYSICS_API UKawaiiPhysicsBoneConstraintsDataAsset : public UDataAsset,
                                                                 public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:
	/** Array of bone constraint data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta=(TitleProperty="{BoneReference1} - {BoneReference2}"))
	TArray<FModifyBoneConstraintData> BoneConstraintsData;

#if WITH_EDITORONLY_DATA

	/** List of regex patterns for bones */
	UPROPERTY(EditAnywhere, Category="Helper")
	TArray<FRegexPatternBoneSet> RegexPatternList;

	/** Preview skeleton for editor */
	UPROPERTY(EditAnywhere, Category = "Skeleton")
	TSoftObjectPtr<USkeleton> PreviewSkeleton;

	/** List of preview bones */
	UPROPERTY(VisibleAnywhere, Category = "Skeleton", meta= (EditCondition=false))
	TArray<FBoneReference> PreviewBoneList;

	/** String representation of the preview bone list */
	UPROPERTY()
	FString PreviewBoneListString;
#endif

	// Begin UObject Interface.
	virtual void Serialize(FStructuredArchiveRecord Record) override;
	virtual void PostLoad() override;
	// End UObject Interface.

	// IBoneReferenceSkeletonProvider interface
	virtual USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

	/** Generates bone constraints based on the current data */
	TArray<FModifyBoneConstraint> GenerateBoneConstraints();

#if WITH_EDITOR

	/** Applies regex patterns to the bone constraints */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Helper")
	void ApplyRegex();

	/** Updates the preview bone list */
	void UpdatePreviewBoneList();

	/** Handles property changes in the editor */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
