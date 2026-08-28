// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimNode_KawaiiPhysics.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "EdGraph/EdGraphNodeUtils.h"

#include "AnimGraphNode_KawaiiPhysics.generated.h"

class FCompilerResultsLog;
class UToolMenu;
class UGraphNodeContextMenuContext;

UCLASS()
class UAnimGraphNode_KawaiiPhysics : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_UCLASS_BODY()
	UPROPERTY(EditAnywhere, Category = "Settings")
	FAnimNode_KawaiiPhysics Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	/** ノード右クリックメニューにKawaii Physics専用項目（Apply Preset / Check Preset Diff）を追加します / Adds Kawaii Physics specific entries (Apply Preset / Check Preset Diff) to the node's right-click context menu. */
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;

protected:
	// UAnimGraphNode_Base interface
	virtual FEditorModeID GetEditorMode() const override;
	virtual void ValidateAnimNodePostCompile(FCompilerResultsLog& MessageLog,
	                                         UAnimBlueprintGeneratedClass* CompiledClass,
	                                         int32 CompiledNodeIndex) override;
	virtual void CopyNodeDataToPreviewNode(FAnimNode_Base* AnimNode) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	// End of UAnimGraphNode_Base interface

	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	// End of UAnimGraphNode_SkeletalControlBase interface

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;

	// End of UObject interface

	virtual void CustomizeDetailTools(IDetailLayoutBuilder& DetailBuilder);
	virtual void CustomizeDetailDebugVisualizations(IDetailLayoutBuilder& DetailBuilder);

private:
	/** コリジョン配列のGuidを一意化する（複製/貼り付け/旧データの重複Guidを再発番） */
	void EnsureUniqueCollisionGuids();

	/** ProceduralWind の Details 編集を実行中ノードへ DynamicParams として送る */
	void PushProceduralWindEditToLiveInstance(const struct FPropertyChangedChainEvent& PropertyChangedEvent);

	/** Creates the export data asset path. */
	void CreateExportDataAssetPath(FString& PackageName, const FString& DefaultSuffix) const;

	/** Creates the data asset package. */
	UPackage* CreateDataAssetPackage(const FText& DialogTitle, const FString& DefaultSuffix,
	                                 FString& AssetName) const;

	/** Shows the export asset notification. */
	void ShowExportAssetNotification(UObject* NewAsset, FText NotificationText);

	/** Exports the limits data asset. */
	void ExportLimitsDataAsset();

	/** Exports the bone constraints data asset. */
	void ExportBoneConstraintsDataAsset();

	/** プリセットDataAssetを書き出します / Exports the preset data asset. */
	void ExportPresetDataAsset();

	/** プリセットDataAssetをこのノードへ適用します / Applies a preset data asset to this node. */
	void ApplyPresetDataAsset();

	/** このノードと対象プリセットとの差分を確認します / Checks the diff between this node and its target preset. */
	void CheckPresetDiff();

	/** 指定したProcedural Windの波形プレビューウィンドウを開きます / Opens the waveform preview window for the specified Procedural Wind. */
	void OpenWindScopeWindow(int32 ExternalForceIndex = INDEX_NONE);

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

	/** Enables or disables debug drawing for sync bones. */
	UPROPERTY()
	bool bEnableDebugDrawSyncBone = true;

	/** Enables or disables debug drawing for spherical limits. */
	UPROPERTY()
	bool bEnableDebugDrawSphereLimit = true;

	/** Enables or disables debug drawing for capsule limits. */
	UPROPERTY()
	bool bEnableDebugDrawCapsuleLimit = true;

	/** テーパードカプセルリミットのデバッグ描画を有効/無効にします / Enables or disables debug drawing for tapered capsule limits. */
	UPROPERTY()
	bool bEnableDebugDrawTaperedCapsuleLimit = true;

	/** Enables or disables debug drawing for box limits. */
	UPROPERTY()
	bool bEnableDebugDrawBoxLimit = true;

	/** Enables or disables debug drawing for planar limits. */
	UPROPERTY()
	bool bEnableDebugDrawPlanarLimit = true;

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
