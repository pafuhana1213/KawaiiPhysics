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
	void CreateExportDataAssetPath(FString& PackageName, const FString& DefaultSuffix) const;
	UPackage* CreateDataAssetPackage(const FString& DialogTitle, const FString& DefaultSuffix,
	                                 FString& AssetName) const;
	void ExportLimitsDataAsset();
	void ExportBoneConstraintsDataAsset();

public:
	UPROPERTY()
	bool bEnableDebugDrawBone = true;
	UPROPERTY()
	bool bEnableDebugBoneLengthRate = true;
	UPROPERTY()
	bool bEnableDebugDrawLimitAngle = true;
	UPROPERTY()
	bool bEnableDebugDrawSphereLimit = true;
	UPROPERTY()
	bool bEnableDebugDrawCapsuleLimit = true;
	UPROPERTY()
	bool bEnableDebugDrawBoxLimit = true;
	UPROPERTY()
	bool bEnableDebugDrawPlanerLimit = true;
	UPROPERTY()
	bool bEnableDebugDrawBoneConstraint = true;
	UPROPERTY()
	bool bEnableDebugDrawExternalForce = true;

private:
	/** Constructing FText strings can be costly, so we cache the node's title */
	FNodeTitleTextTable CachedNodeTitles;
};
