#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimNode_KawaiiPhysics.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "EdGraph/EdGraphNodeUtils.h"

#include "AnimGraphNode_KawaiiPhysics.generated.h"

class FCompilerResultsLog;

UCLASS()
class UAnimGraphNode_KawaiiPhysics : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_UCLASS_BODY()
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_KawaiiPhysics Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

protected:
	// UAnimGraphNode_Base interface
	virtual FEditorModeID GetEditorMode() const override;
	virtual void ValidateAnimNodePostCompile(FCompilerResultsLog& MessageLog,
	                                         UAnimBlueprintGeneratedClass* CompiledClass,
	                                         int32 CompiledNodeIndex) override;
	virtual void CopyNodeDataToPreviewNode(FAnimNode_Base* AnimNode) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	// End of UAnimGraphNode_Base interface

	//virtual FText GetControllerDescription() const override;
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	// End of UAnimGraphNode_SkeletalControlBase interface

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;

	// End of UObject interface

	virtual void CustomizeDetailTools(IDetailLayoutBuilder& DetailBuilder);
	virtual void CustomizeDetailDebugVisualizations(IDetailLayoutBuilder& DetailBuilder);

private:
	/** Creates the export data asset path. */
	void CreateExportDataAssetPath(FString& PackageName, const FString& DefaultSuffix) const;

	/** Creates the data asset package. */
	UPackage* CreateDataAssetPackage(const FString& DialogTitle, const FString& DefaultSuffix,
									 FString& AssetName) const;
	
	/** Exports the limits data asset. */
	void ExportLimitsDataAsset();

	/** Exports the bone constraints data asset. */
	void ExportBoneConstraintsDataAsset();

public:
	/** Enables or disables debug drawing for bones. */
	UPROPERTY()
	bool bEnableDebugDrawBone = true;

	/** Enables or disables debug drawing for bone length rate. */
	UPROPERTY()
	bool bEnableDebugBoneLengthRate = true;

	/** Enables or disables debug drawing for limit angles. */
	UPROPERTY()
	bool bEnableDebugDrawLimitAngle = true;

	/** Enables or disables debug drawing for spherical limits. */
	UPROPERTY()
	bool bEnableDebugDrawSphereLimit = true;

	/** Enables or disables debug drawing for capsule limits. */
	UPROPERTY()
	bool bEnableDebugDrawCapsuleLimit = true;

	/** Enables or disables debug drawing for box limits. */
	UPROPERTY()
	bool bEnableDebugDrawBoxLimit = true;

	/** Enables or disables debug drawing for planar limits. */
	UPROPERTY()
	bool bEnableDebugDrawPlanerLimit = true;

	/** Enables or disables debug drawing for bone constraints. */
	UPROPERTY()
	bool bEnableDebugDrawBoneConstraint = true;

	/** Enables or disables debug drawing for external forces. */
	UPROPERTY()
	bool bEnableDebugDrawExternalForce = true;

private:
	/** Constructing FText strings can be costly, so we cache the node's title */
	FNodeTitleTextTable CachedNodeTitles;
};
