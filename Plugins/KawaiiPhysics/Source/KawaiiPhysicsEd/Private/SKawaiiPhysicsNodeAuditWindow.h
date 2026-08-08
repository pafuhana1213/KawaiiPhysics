// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class SWindow;
class STableViewBase;

struct FKawaiiPhysicsNodeAuditWindowArgs
{
	/** ウィンドウタイトル / Window title. */
	FText WindowTitle;

	/** サマリ表示テキスト（初期表示用。以降は内部で再計算される） / Summary text for the initial display (recalculated internally afterwards). */
	FText SummaryText;

	/** 表示する監査結果（値コピー） / Audit entries to display (copied by value). */
	TArray<FKawaiiPhysicsNodeAuditEntry> Entries;

	/** Matches / DiffProperties / ViewDiff 列と Refresh 系操作を表示するか / Whether to show the Matches / DiffProperties / ViewDiff columns and preset-driven operations. */
	bool bShowDiffColumns = false;

	/** Refresh / Apply to Project で使用する対象プリセット / Target preset used by Refresh and Apply to Project. */
	FSoftObjectPath PresetPath;
};

class SKawaiiPhysicsNodeAuditWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKawaiiPhysicsNodeAuditWindow)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FKawaiiPhysicsNodeAuditWindowArgs InitArgs);

	/** 監査ウィンドウを開くか既存ウィンドウを更新する / Opens the audit window or updates the existing one. */
	static void OpenWindow(FKawaiiPhysicsNodeAuditWindowArgs Args);

	/** 開いている監査ウィンドウをすべて閉じる / Closes all open audit windows. */
	static void CloseAllWindows();

	/** 現在の引数でウィジェット状態を置き換える / Replaces the widget state with the current arguments. */
	void SetArgs(FKawaiiPhysicsNodeAuditWindowArgs Args);

private:
	using FEntryPtr = TSharedPtr<FKawaiiPhysicsNodeAuditEntry>;

	void RebuildWidget();
	void RefreshFilteredRows();
	void ReplaceEntriesFromReport(const TArray<FKawaiiPhysicsNodeAuditEntry>& Report);
	void SortEntries();

	TSharedRef<ITableRow> GenerateRow(FEntryPtr Entry, const TSharedRef<STableViewBase>& OwnerTable);
	void OnRowDoubleClicked(FEntryPtr Entry);
	void OnViewDiffClicked(FEntryPtr Entry);

	ECheckBoxState GetShowDifferingOnlyState() const;
	void OnShowDifferingOnlyChanged(ECheckBoxState NewState);
	ECheckBoxState GetCheckOutFilesState() const;
	void OnCheckOutFilesChanged(ECheckBoxState NewState);

	EColumnSortMode::Type GetColumnSortMode(FName ColumnId) const;
	void OnColumnSort(EColumnSortPriority::Type SortPriority, const FName& ColumnId, EColumnSortMode::Type NewSortMode);

	FReply OnRefreshClicked();
	FReply OnExportClicked();
	FReply OnApplyToProjectClicked();

	bool ExportEntriesToJson(const FString& OutputPath) const;
	bool ExportEntriesToCsv(const FString& OutputPath) const;

	FText SummaryText;
	TArray<FEntryPtr> Entries;
	TArray<FEntryPtr> FilteredRows;
	bool bShowDiffColumns = false;
	FSoftObjectPath PresetPath;
	bool bShowDifferingOnly = false;
	bool bCheckOutFiles = false;
	FName SortColumnId;
	EColumnSortMode::Type SortMode = EColumnSortMode::None;

	TSharedPtr<SListView<FEntryPtr>> ListView;
};
