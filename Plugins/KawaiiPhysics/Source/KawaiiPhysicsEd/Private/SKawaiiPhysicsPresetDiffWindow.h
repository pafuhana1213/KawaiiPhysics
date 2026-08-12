// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KawaiiPhysicsPresetDiffSnapshot.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class FSpawnTabArgs;
class FWorkspaceItem;
class SDockTab;
class STableViewBase;

struct FKawaiiPhysicsPresetDiffWindowArgs
{
	/** 表示対象の説明ラベル / Description label for the compared context. */
	FText ContextLabel;

	/** 表示するプリセット差分スナップショット / Preset diff snapshots to display. */
	TArray<TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>> Snapshots;

	/** ノードを再解決する AnimBlueprint パス / AnimBlueprint path used to re-resolve the node. */
	FSoftObjectPath AnimBlueprintPath;

	/** ノードを再解決する NodeGuid / NodeGuid used to re-resolve the node. */
	FGuid NodeGuid;
};

class SKawaiiPhysicsPresetDiffWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKawaiiPhysicsPresetDiffWindow)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FKawaiiPhysicsPresetDiffWindowArgs DiffArgs = FKawaiiPhysicsPresetDiffWindowArgs());

	static const FName PresetDiffTabId;

	/** 差分タブスポナーを登録する / Registers the diff tab spawner. */
	static void RegisterTabSpawner(const TSharedRef<FWorkspaceItem>& InMenuGroup);

	/** 差分タブスポナーを解除する / Unregisters the diff tab spawner. */
	static void UnregisterTabSpawner();

	/** 差分タブを生成する / Spawns the diff tab. */
	static TSharedRef<SDockTab> SpawnPresetDiffTab(const FSpawnTabArgs& SpawnTabArgs);

	/** 差分タブを開くか既存タブを更新する / Opens the diff tab or updates the existing one. */
	static void OpenWindow(FKawaiiPhysicsPresetDiffWindowArgs Args);

	/** 開いている差分タブをすべて閉じる / Closes all open diff tabs. */
	static void CloseAllWindows();

	/** 現在の引数でウィジェット状態を置き換える / Replaces the widget state with the current arguments. */
	void SetArgs(FKawaiiPhysicsPresetDiffWindowArgs Args);

private:
	using FSnapshotPtr = TSharedPtr<FKawaiiPhysicsPresetDiffSnapshot>;
	using FRowPtr = TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>;

	TSharedRef<SWidget> GeneratePresetComboWidget(FSnapshotPtr Snapshot);
	TSharedRef<ITableRow> GenerateDiffRow(FRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable);
	void OnPresetSelectionChanged(FSnapshotPtr Snapshot, ESelectInfo::Type SelectInfo);
	void OnShowAllChanged(ECheckBoxState NewState);
	void OnSearchTextChanged(const FText& NewText);
	void OnRowDoubleClicked(FRowPtr Row);

	FText GetSelectedPresetText() const;
	FText GetSummaryText() const;
	ECheckBoxState IsPropertySelected(FName PropertyName) const;
	void SetPropertySelected(ECheckBoxState NewState, FName PropertyName);
	bool CanApplySelected() const;

	FReply OnOpenPresetClicked();
	FReply OnCopyClicked();
	FReply OnRefreshClicked();
	FReply OnApplySelectedClicked();
	FReply OnApplyPresetToNodeClicked();
	FReply OnUpdatePresetFromNodeClicked();

	void RefreshFilteredRows();
	void ReplaceSelectedSnapshot(TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> NewSnapshot);
	bool RebuildAllSnapshotsKeepingPreset();

	FText ContextLabel;
	TArray<FSnapshotPtr> Snapshots;
	FSnapshotPtr SelectedSnapshot;
	FSoftObjectPath AnimBlueprintPath;
	FGuid NodeGuid;
	bool bShowAllProperties = false;
	FText SearchText;
	TArray<FRowPtr> FilteredRows;
	TSet<FName> SelectedPropertyNames;

	TSharedPtr<SListView<FRowPtr>> DiffListView;
};
