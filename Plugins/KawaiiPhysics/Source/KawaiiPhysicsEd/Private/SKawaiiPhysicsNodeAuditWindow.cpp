// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "SKawaiiPhysicsNodeAuditWindow.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformProcess.h"
#include "ISourceControlModule.h"
#include "KawaiiPhysicsAuditCommandlet.h"
#include "KawaiiPhysicsEdStyle.h"
#include "KawaiiPhysicsEdWindowUtils.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "KawaiiPhysicsPresetDiffSnapshot.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "SKawaiiPhysicsPresetDiffWindow.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SWindow.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWidget.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsNodeAuditWindow"

SLATE_IMPLEMENT_WIDGET(SKawaiiPhysicsNodeAuditWindow)

void SKawaiiPhysicsNodeAuditWindow::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
	(void)AttributeInitializer;
}

namespace
{
	const FName AnimBlueprintColumnName(TEXT("AnimBlueprint"));
	const FName GraphColumnName(TEXT("Graph"));
	const FName TagColumnName(TEXT("Tag"));
	const FName RootBoneColumnName(TEXT("RootBone"));
	const FName MatchesColumnName(TEXT("Matches"));
	const FName DiffPropertiesColumnName(TEXT("DiffProperties"));
	const FName ViewDiffColumnName(TEXT("ViewDiff"));

	TArray<TWeakPtr<SKawaiiPhysicsNodeAuditWindow>> LiveNodeAuditWindows;

	void RegisterLiveWindow(TSharedRef<SKawaiiPhysicsNodeAuditWindow> Window)
	{
		LiveNodeAuditWindows.RemoveAllSwap([](const TWeakPtr<SKawaiiPhysicsNodeAuditWindow>& ExistingWindow)
		{
			return !ExistingWindow.IsValid();
		});
		for (const TWeakPtr<SKawaiiPhysicsNodeAuditWindow>& ExistingWindow : LiveNodeAuditWindows)
		{
			if (ExistingWindow.Pin().Get() == &Window.Get())
			{
				return;
			}
		}
		LiveNodeAuditWindows.Add(Window);
	}

	void ShowAuditExportSucceededNotification(const FString& OutputPath)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ExportSucceeded", "Exported Kawaii Physics audit results."),
			SNotificationItem::CS_Success,
			5.0f,
			FSimpleDelegate::CreateLambda([OutputPath]()
			{
				FPlatformProcess::ExploreFolder(*FPaths::GetPath(OutputPath));
			}),
			LOCTEXT("ExportSucceededHyperlink", "Show in Folder"));
	}

	FText MakeAuditSummaryText(const TArray<TSharedPtr<FKawaiiPhysicsNodeAuditEntry>>& Entries)
	{
		int32 DiffCount = 0;
		for (const TSharedPtr<FKawaiiPhysicsNodeAuditEntry>& Entry : Entries)
		{
			if (Entry.IsValid() && !Entry->bMatchesPreset)
			{
				++DiffCount;
			}
		}

		return FText::Format(
			LOCTEXT("AuditSummaryFormat", "{0} nodes, {1} differ"),
			FText::AsNumber(Entries.Num()),
			FText::AsNumber(DiffCount));
	}

	FString EscapeCsvField(const FString& Value)
	{
		const bool bNeedsQuoting = Value.Contains(TEXT(",")) || Value.Contains(TEXT("\"")) ||
			Value.Contains(TEXT("\n")) || Value.Contains(TEXT("\r"));
		if (!bNeedsQuoting)
		{
			return Value;
		}

		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	FText GetMatchesCellText(const FKawaiiPhysicsNodeAuditEntry& Entry)
	{
		return Entry.bMatchesPreset
			       ? LOCTEXT("MatchesCheckMark", "✓")
			       : FText::AsNumber(Entry.DiffProperties.Num());
	}

	FString JoinDiffPropertyNames(const FKawaiiPhysicsNodeAuditEntry& Entry)
	{
		TArray<FString> Names;
		Names.Reserve(Entry.DiffProperties.Num());
		for (const FName& DiffProperty : Entry.DiffProperties)
		{
			Names.Add(DiffProperty.ToString());
		}
		return FString::Join(Names, TEXT(", "));
	}

	DECLARE_DELEGATE_OneParam(FOnAuditViewDiffClicked, TSharedPtr<FKawaiiPhysicsNodeAuditEntry>);

	class SKawaiiPhysicsNodeAuditRow : public SMultiColumnTableRow<TSharedPtr<FKawaiiPhysicsNodeAuditEntry>>
	{
	public:
		SLATE_BEGIN_ARGS(SKawaiiPhysicsNodeAuditRow)
			{
			}
			SLATE_ARGUMENT(TSharedPtr<FKawaiiPhysicsNodeAuditEntry>, Entry)
			SLATE_ARGUMENT(bool, ShowDiffColumns)
			SLATE_EVENT(FOnAuditViewDiffClicked, OnViewDiffClicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
		{
			Entry = InArgs._Entry;
			bShowDiffColumns = InArgs._ShowDiffColumns;
			OnViewDiffClicked = InArgs._OnViewDiffClicked;
			SMultiColumnTableRow<TSharedPtr<FKawaiiPhysicsNodeAuditEntry>>::Construct(
				FSuperRowType::FArguments().Padding(FMargin(2.0f, 1.0f)),
				OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (!Entry.IsValid())
			{
				return SNew(STextBlock);
			}

			if (ColumnName == AnimBlueprintColumnName)
			{
				return MakeTextCell(
					FText::FromString(Entry->AnimBlueprintPath.GetAssetName()),
					FText::FromString(Entry->AnimBlueprintPath.ToString()));
			}

			if (ColumnName == GraphColumnName)
			{
				return MakeTextCell(FText::FromName(Entry->GraphName), FText::FromName(Entry->GraphName));
			}

			if (ColumnName == TagColumnName)
			{
				const FText TagText = FText::FromString(Entry->KawaiiPhysicsTag.ToString());
				return MakeTextCell(TagText, TagText);
			}

			if (ColumnName == RootBoneColumnName)
			{
				return MakeTextCell(FText::FromName(Entry->RootBoneName), FText::FromName(Entry->RootBoneName));
			}

			if (bShowDiffColumns && ColumnName == MatchesColumnName)
			{
				const FText MatchesText = GetMatchesCellText(*Entry);
				return MakeTextCell(MatchesText, MatchesText);
			}

			if (bShowDiffColumns && ColumnName == DiffPropertiesColumnName)
			{
				const FString Joined = JoinDiffPropertyNames(*Entry);
				return MakeTextCell(FText::FromString(Joined), FText::FromString(Joined));
			}

			if (bShowDiffColumns && ColumnName == ViewDiffColumnName)
			{
				return SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("ViewDiffButton", "View Diff"))
						.ToolTipText(LOCTEXT("ViewDiffButtonToolTip", "このノードとマッチしたプリセットの差分を表示します / Shows the diff between this node and its matched preset."))
						.IsEnabled(Entry->MatchedPresetPath.IsValid())
						.OnClicked(this, &SKawaiiPhysicsNodeAuditRow::HandleViewDiffClicked)
					];
			}

			return SNew(STextBlock);
		}

	private:
		TSharedRef<SWidget> MakeTextCell(const FText& Text, const FText& ToolTipText) const
		{
			return SNew(STextBlock)
				.Text(Text)
				.ToolTipText(ToolTipText)
				.Clipping(EWidgetClipping::OnDemand);
		}

		FReply HandleViewDiffClicked()
		{
			OnViewDiffClicked.ExecuteIfBound(Entry);
			return FReply::Handled();
		}

		TSharedPtr<FKawaiiPhysicsNodeAuditEntry> Entry;
		bool bShowDiffColumns = false;
		FOnAuditViewDiffClicked OnViewDiffClicked;
	};
}

const FName SKawaiiPhysicsNodeAuditWindow::NodeAuditTabId(TEXT("KawaiiPhysicsNodeAudit"));

void SKawaiiPhysicsNodeAuditWindow::Construct(const FArguments& InArgs, FKawaiiPhysicsNodeAuditWindowArgs InitArgs)
{
	(void)InArgs;
	SetArgs(MoveTemp(InitArgs));
}

void SKawaiiPhysicsNodeAuditWindow::RegisterTabSpawner()
{
	const FSlateIcon KawaiiPhysicsIcon(
		FKawaiiPhysicsEdStyle::GetStyleSetName(),
		TEXT("KawaiiPhysics.TabIcon"));
	// DataAsset 詳細から開くため、グローバル側には非表示タブとして登録する
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			NodeAuditTabId,
			FOnSpawnTab::CreateStatic(&SKawaiiPhysicsNodeAuditWindow::SpawnNodeAuditTab))
		.SetDisplayName(LOCTEXT("NodeAuditMenuDisplayName", "Node Audit"))
		.SetTooltipText(LOCTEXT("NodeAuditMenuTooltip", "KawaiiPhysics のノード監査タブを開きます / Opens the KawaiiPhysics node audit tab."))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(KawaiiPhysicsIcon);
}

void SKawaiiPhysicsNodeAuditWindow::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(NodeAuditTabId);
}

TSharedRef<SDockTab> SKawaiiPhysicsNodeAuditWindow::SpawnNodeAuditTab(const FSpawnTabArgs& SpawnTabArgs)
{
	(void)SpawnTabArgs;

	TSharedRef<SKawaiiPhysicsNodeAuditWindow> AuditWidget = SNew(SKawaiiPhysicsNodeAuditWindow);
	TSharedRef<SDockTab> AuditTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("NodeAuditTabLabel", "Kawaii Node Audit"))
		[
			AuditWidget
		];

	AuditWidget->SetOwnerTab(AuditTab);
	return AuditTab;
}

void SKawaiiPhysicsNodeAuditWindow::OpenWindow(FKawaiiPhysicsNodeAuditWindowArgs Args)
{
	const FText TabLabel = Args.WindowTitle;

	// タブを呼び出してから、監査結果の引数を既存コンテンツへ注入する
	TSharedPtr<SDockTab> InvokedTab = FGlobalTabmanager::Get()->TryInvokeTab(NodeAuditTabId);
	if (!InvokedTab.IsValid())
	{
		return;
	}

	TSharedPtr<SWidget> TabContent = InvokedTab->GetContent();
	if (!TabContent.IsValid())
	{
		return;
	}

	if (!TabLabel.IsEmpty())
	{
		InvokedTab->SetLabel(TabLabel);
	}

	if (TabContent->GetType() == FName(TEXT("SKawaiiPhysicsNodeAuditWindow")))
	{
		TSharedPtr<SKawaiiPhysicsNodeAuditWindow> WindowWidget =
			StaticCastSharedPtr<SKawaiiPhysicsNodeAuditWindow>(TabContent);
		WindowWidget->SetOwnerTab(InvokedTab.ToSharedRef());
		WindowWidget->SetArgs(MoveTemp(Args));
		return;
	}

	KawaiiPhysicsEdWindowUtils::ShowNotification(
		LOCTEXT("NodeAuditTabContentInvalid", "Failed to update the Node Audit tab content."),
		SNotificationItem::CS_Fail);
}

void SKawaiiPhysicsNodeAuditWindow::CloseAllWindows()
{
	TArray<TSharedPtr<SDockTab>> TabsToClose;
	for (const TWeakPtr<SKawaiiPhysicsNodeAuditWindow>& WeakWindow : LiveNodeAuditWindows)
	{
		if (TSharedPtr<SKawaiiPhysicsNodeAuditWindow> Window = WeakWindow.Pin())
		{
			if (TSharedPtr<SDockTab> OwnerTab = Window->OwnerTabWeak.Pin())
			{
				TabsToClose.Add(OwnerTab);
			}
		}
	}

	for (const TSharedPtr<SDockTab>& Tab : TabsToClose)
	{
		Tab->RequestCloseTab();
	}
	LiveNodeAuditWindows.Reset();
}

void SKawaiiPhysicsNodeAuditWindow::SetOwnerTab(TSharedRef<SDockTab> InOwnerTab)
{
	OwnerTabWeak = InOwnerTab;
	RegisterLiveWindow(StaticCastSharedRef<SKawaiiPhysicsNodeAuditWindow>(AsShared()));
}

void SKawaiiPhysicsNodeAuditWindow::SetArgs(FKawaiiPhysicsNodeAuditWindowArgs Args)
{
	bShowDiffColumns = Args.bShowDiffColumns;
	PresetPath = Args.PresetPath;
	bShowDifferingOnly = false;
	bCheckOutFiles = false;
	SortColumnId = NAME_None;
	SortMode = EColumnSortMode::None;

	Entries.Reset();
	Entries.Reserve(Args.Entries.Num());
	for (const FKawaiiPhysicsNodeAuditEntry& Entry : Args.Entries)
	{
		Entries.Add(MakeShared<FKawaiiPhysicsNodeAuditEntry>(Entry));
	}

	if (Entries.IsEmpty() && Args.SummaryText.IsEmpty() && !PresetPath.IsValid())
	{
		SummaryText = LOCTEXT("NoAuditResultsGuidance", "監査結果なし: プリセット DataAsset 詳細の [Find Target Nodes] / [Apply To Project (Dry Run)] から開いてください / No audit results: open from the preset DataAsset details.");
	}
	else
	{
		SummaryText = Args.SummaryText.IsEmpty() ? MakeAuditSummaryText(Entries) : Args.SummaryText;
	}

	RebuildWidget();
}

void SKawaiiPhysicsNodeAuditWindow::RebuildWidget()
{
	TSharedRef<SHeaderRow> HeaderRowWidget =
		SNew(SHeaderRow)
		+ SHeaderRow::Column(AnimBlueprintColumnName)
		.FillWidth(0.22f)
		.DefaultLabel(LOCTEXT("AnimBlueprintColumnLabel", "AnimBlueprint"))
		.DefaultTooltip(LOCTEXT("AnimBlueprintColumnToolTip", "Anim Blueprint のアセット名を表示します。ツールチップにフルパスを表示します / Shows the Anim Blueprint asset name. The tooltip shows the full path."))
		.SortMode(this, &SKawaiiPhysicsNodeAuditWindow::GetColumnSortMode, AnimBlueprintColumnName)
		.OnSort(this, &SKawaiiPhysicsNodeAuditWindow::OnColumnSort)
		+ SHeaderRow::Column(GraphColumnName)
		.FillWidth(0.14f)
		.DefaultLabel(LOCTEXT("GraphColumnLabel", "Graph"))
		.DefaultTooltip(LOCTEXT("GraphColumnToolTip", "ノードを含むグラフ名を表示します / Shows the name of the graph that owns the node."))
		+ SHeaderRow::Column(TagColumnName)
		.FillWidth(0.16f)
		.DefaultLabel(LOCTEXT("TagColumnLabel", "Tag"))
		.DefaultTooltip(LOCTEXT("TagColumnToolTip", "KawaiiPhysicsTag を表示します / Shows the KawaiiPhysicsTag."))
		.SortMode(this, &SKawaiiPhysicsNodeAuditWindow::GetColumnSortMode, TagColumnName)
		.OnSort(this, &SKawaiiPhysicsNodeAuditWindow::OnColumnSort)
		+ SHeaderRow::Column(RootBoneColumnName)
		.FillWidth(0.14f)
		.DefaultLabel(LOCTEXT("RootBoneColumnLabel", "RootBone"))
		.DefaultTooltip(LOCTEXT("RootBoneColumnToolTip", "RootBone のボーン名を表示します / Shows the RootBone bone name."));

	if (bShowDiffColumns)
	{
		HeaderRowWidget->AddColumn(
			SHeaderRow::Column(MatchesColumnName)
			.FillWidth(0.10f)
			.DefaultLabel(LOCTEXT("MatchesColumnLabel", "Matches"))
			.DefaultTooltip(LOCTEXT("MatchesColumnToolTip", "プリセットと一致していればチェック、そうでなければ差分プロパティ数を表示します / Shows a check mark when the node matches the preset, otherwise the number of differing properties."))
			.SortMode(this, &SKawaiiPhysicsNodeAuditWindow::GetColumnSortMode, MatchesColumnName)
			.OnSort(this, &SKawaiiPhysicsNodeAuditWindow::OnColumnSort));

		HeaderRowWidget->AddColumn(
			SHeaderRow::Column(DiffPropertiesColumnName)
			.FillWidth(0.24f)
			.DefaultLabel(LOCTEXT("DiffPropertiesColumnLabel", "DiffProperties"))
			.DefaultTooltip(LOCTEXT("DiffPropertiesColumnToolTip", "差分のあるプロパティ名をカンマ区切りで表示します / Shows differing property names, comma-separated.")));

		HeaderRowWidget->AddColumn(
			SHeaderRow::Column(ViewDiffColumnName)
			.FixedWidth(90.0f)
			.DefaultLabel(FText::GetEmpty())
			.DefaultTooltip(LOCTEXT("ViewDiffColumnToolTip", "ノードとマッチしたプリセットの差分をウィンドウで表示します / Shows the diff between the node and its matched preset in a window.")));
	}

	SAssignNew(ListView, SListView<FEntryPtr>)
		.ListItemsSource(&FilteredRows)
		.OnGenerateRow(this, &SKawaiiPhysicsNodeAuditWindow::GenerateRow)
		.OnMouseButtonDoubleClick(this, &SKawaiiPhysicsNodeAuditWindow::OnRowDoubleClicked)
		.HeaderRow(HeaderRowWidget);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				return SummaryText;
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SCheckBox)
				.Visibility(bShowDiffColumns ? EVisibility::Visible : EVisibility::Collapsed)
				.IsChecked(this, &SKawaiiPhysicsNodeAuditWindow::GetShowDifferingOnlyState)
				.OnCheckStateChanged(this, &SKawaiiPhysicsNodeAuditWindow::OnShowDifferingOnlyChanged)
				.ToolTipText(LOCTEXT("ShowDifferingOnlyToolTip", "プリセットと一致していない行だけを表示します（表示のみのフィルタ） / Shows only rows that do not match the preset (display filter only)."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowDifferingOnlyLabel", "Show Differing Only"))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SCheckBox)
				.Visibility(bShowDiffColumns ? EVisibility::Visible : EVisibility::Collapsed)
				.IsEnabled(ISourceControlModule::Get().IsEnabled())
				.IsChecked(this, &SKawaiiPhysicsNodeAuditWindow::GetCheckOutFilesState)
				.OnCheckStateChanged(this, &SKawaiiPhysicsNodeAuditWindow::OnCheckOutFilesChanged)
				.ToolTipText(LOCTEXT("CheckOutFilesToolTip", "Apply to Project 実行時にソース管理でパッケージをチェックアウトします / Checks out packages via source control when running Apply to Project."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CheckOutFilesLabel", "Check out files"))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshButton", "Refresh"))
				.ToolTipText(LOCTEXT("RefreshButtonToolTip", "プリセットを対象ノードへドライラン適用し直し、一覧を再計算します / Re-runs a dry-run apply of the preset against the target nodes and rebuilds the list."))
				.IsEnabled_Lambda([this]()
				{
					return PresetPath.IsValid();
				})
				.OnClicked(this, &SKawaiiPhysicsNodeAuditWindow::OnRefreshClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ExportButton", "Export..."))
				.ToolTipText(LOCTEXT("ExportButtonToolTip", "監査結果を JSON または CSV ファイルへ書き出します / Exports the audit results to a JSON or CSV file."))
				.OnClicked(this, &SKawaiiPhysicsNodeAuditWindow::OnExportClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Visibility(bShowDiffColumns ? EVisibility::Visible : EVisibility::Collapsed)
				.Text(LOCTEXT("ApplyToProjectButton", "Apply to Project..."))
				.ToolTipText(LOCTEXT("ApplyToProjectButtonToolTip", "このプリセットを、対象タグに一致するプロジェクト内の全ノードへ適用します / Applies this preset to every node in the project that matches its target tags."))
				.IsEnabled_Lambda([this]()
				{
					return PresetPath.IsValid();
				})
				.OnClicked(this, &SKawaiiPhysicsNodeAuditWindow::OnApplyToProjectClicked)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 4.0f)
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			ListView.ToSharedRef()
		]
	];

	SortEntries();
	RefreshFilteredRows();
}

void SKawaiiPhysicsNodeAuditWindow::RefreshFilteredRows()
{
	FilteredRows.Reset();
	for (const FEntryPtr& Entry : Entries)
	{
		if (!Entry.IsValid())
		{
			continue;
		}

		if (bShowDiffColumns && bShowDifferingOnly && Entry->bMatchesPreset)
		{
			continue;
		}

		FilteredRows.Add(Entry);
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SKawaiiPhysicsNodeAuditWindow::ReplaceEntriesFromReport(const TArray<FKawaiiPhysicsNodeAuditEntry>& Report)
{
	Entries.Reset();
	Entries.Reserve(Report.Num());
	for (const FKawaiiPhysicsNodeAuditEntry& Entry : Report)
	{
		Entries.Add(MakeShared<FKawaiiPhysicsNodeAuditEntry>(Entry));
	}

	SortEntries();
	SummaryText = MakeAuditSummaryText(Entries);
	RefreshFilteredRows();
}

void SKawaiiPhysicsNodeAuditWindow::SortEntries()
{
	if (SortMode == EColumnSortMode::None || SortColumnId.IsNone())
	{
		return;
	}

	const bool bAscending = SortMode == EColumnSortMode::Ascending;

	if (SortColumnId == AnimBlueprintColumnName)
	{
		Entries.Sort([bAscending](const FEntryPtr& A, const FEntryPtr& B)
		{
			const FString NameA = A.IsValid() ? A->AnimBlueprintPath.GetAssetName() : FString();
			const FString NameB = B.IsValid() ? B->AnimBlueprintPath.GetAssetName() : FString();
			return bAscending ? (NameA < NameB) : (NameA > NameB);
		});
	}
	else if (SortColumnId == TagColumnName)
	{
		Entries.Sort([bAscending](const FEntryPtr& A, const FEntryPtr& B)
		{
			const FString TagA = A.IsValid() ? A->KawaiiPhysicsTag.ToString() : FString();
			const FString TagB = B.IsValid() ? B->KawaiiPhysicsTag.ToString() : FString();
			return bAscending ? (TagA < TagB) : (TagA > TagB);
		});
	}
	else if (SortColumnId == MatchesColumnName)
	{
		Entries.Sort([bAscending](const FEntryPtr& A, const FEntryPtr& B)
		{
			const int32 DiffA = A.IsValid() ? A->DiffProperties.Num() : 0;
			const int32 DiffB = B.IsValid() ? B->DiffProperties.Num() : 0;
			return bAscending ? (DiffA < DiffB) : (DiffA > DiffB);
		});
	}
}

TSharedRef<ITableRow> SKawaiiPhysicsNodeAuditWindow::GenerateRow(
	FEntryPtr Entry,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SKawaiiPhysicsNodeAuditRow, OwnerTable)
		.Entry(Entry)
		.ShowDiffColumns(bShowDiffColumns)
		.OnViewDiffClicked(this, &SKawaiiPhysicsNodeAuditWindow::OnViewDiffClicked);
}

void SKawaiiPhysicsNodeAuditWindow::OnRowDoubleClicked(FEntryPtr Entry)
{
	if (!Entry.IsValid())
	{
		return;
	}

	UObject* AnimBlueprintObject = Entry->AnimBlueprintPath.TryLoad();
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AnimBlueprintObject);
	if (!AnimBlueprint || !GEditor)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("OpenAnimBlueprintFailed", "Failed to open the AnimBlueprint."),
			SNotificationItem::CS_Fail);
		return;
	}

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(AnimBlueprint);
	}
}

void SKawaiiPhysicsNodeAuditWindow::OnViewDiffClicked(FEntryPtr Entry)
{
	if (!Entry.IsValid())
	{
		return;
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode =
		UKawaiiPhysicsEditorLibrary::FindGraphNodeByGuid(Entry->AnimBlueprintPath, Entry->NodeGuid);
	if (!GraphNode)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ViewDiffResolveFailed", "Failed to resolve the KawaiiPhysics graph node."),
			SNotificationItem::CS_Fail);
		return;
	}

	UKawaiiPhysicsPresetDataAsset* Preset =
		Cast<UKawaiiPhysicsPresetDataAsset>(Entry->MatchedPresetPath.TryLoad());
	if (!Preset)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ViewDiffPresetLoadFailed", "Failed to load the matched preset."),
			SNotificationItem::CS_Fail);
		return;
	}

	const FKawaiiPhysicsPresetApplyOptions Options;
	TArray<TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>> Snapshots;
	Snapshots.Add(KawaiiPhysicsPresetDiff::BuildSnapshot(GraphNode->Node, *Preset, Options));

	// 一覧行と同じプリセットを基準にするため、ここではノードにマッチする全プリセットの再収集は行わない。
	const UAnimBlueprint* AnimBlueprint = GraphNode->GetAnimBlueprint();
	const FText ContextLabel = FText::Format(
		LOCTEXT("ViewDiffContextLabel", "{0}  |  {1}  |  Tag: {2}"),
		GraphNode->GetNodeTitle(ENodeTitleType::ListView),
		AnimBlueprint
			? FText::FromString(AnimBlueprint->GetName())
			: LOCTEXT("ViewDiffUnknownAnimBlueprint", "(Unknown AnimBlueprint)"),
		FText::FromString(Entry->KawaiiPhysicsTag.ToString()));

	FKawaiiPhysicsPresetDiffWindowArgs DiffArgs;
	DiffArgs.ContextLabel = ContextLabel;
	DiffArgs.Snapshots = Snapshots;
	DiffArgs.AnimBlueprintPath = Entry->AnimBlueprintPath;
	DiffArgs.NodeGuid = Entry->NodeGuid;

	SKawaiiPhysicsPresetDiffWindow::OpenWindow(MoveTemp(DiffArgs));
}

ECheckBoxState SKawaiiPhysicsNodeAuditWindow::GetShowDifferingOnlyState() const
{
	return bShowDifferingOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SKawaiiPhysicsNodeAuditWindow::OnShowDifferingOnlyChanged(ECheckBoxState NewState)
{
	bShowDifferingOnly = NewState == ECheckBoxState::Checked;
	RefreshFilteredRows();
}

ECheckBoxState SKawaiiPhysicsNodeAuditWindow::GetCheckOutFilesState() const
{
	return bCheckOutFiles ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SKawaiiPhysicsNodeAuditWindow::OnCheckOutFilesChanged(ECheckBoxState NewState)
{
	bCheckOutFiles = NewState == ECheckBoxState::Checked;
}

EColumnSortMode::Type SKawaiiPhysicsNodeAuditWindow::GetColumnSortMode(FName ColumnId) const
{
	return SortColumnId == ColumnId ? SortMode : EColumnSortMode::None;
}

void SKawaiiPhysicsNodeAuditWindow::OnColumnSort(
	EColumnSortPriority::Type SortPriority,
	const FName& ColumnId,
	EColumnSortMode::Type NewSortMode)
{
	(void)SortPriority;
	SortColumnId = ColumnId;
	SortMode = NewSortMode;
	SortEntries();
	RefreshFilteredRows();
}

FReply SKawaiiPhysicsNodeAuditWindow::OnRefreshClicked()
{
	UKawaiiPhysicsPresetDataAsset* Preset = Cast<UKawaiiPhysicsPresetDataAsset>(PresetPath.TryLoad());
	if (!Preset)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("RefreshPresetLoadFailed", "Failed to load the preset for Refresh."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	TArray<FKawaiiPhysicsNodeAuditEntry> Report;
	UKawaiiPhysicsEditorLibrary::ApplyPresetToProject(Preset, true, false, Report);
	ReplaceEntriesFromReport(Report);
	return FReply::Handled();
}

FReply SKawaiiPhysicsNodeAuditWindow::OnApplyToProjectClicked()
{
	int32 DifferingCount = 0;
	for (const FEntryPtr& Entry : Entries)
	{
		if (Entry.IsValid() && !Entry->bMatchesPreset)
		{
			++DifferingCount;
		}
	}

	UKawaiiPhysicsPresetDataAsset* PreviewPreset = Cast<UKawaiiPhysicsPresetDataAsset>(PresetPath.TryLoad());
	const FText PresetNameText = PreviewPreset
		                             ? FText::FromString(PreviewPreset->GetName())
		                             : FText::FromString(PresetPath.ToString());

	const EAppReturnType::Type DialogResult = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::Format(
			LOCTEXT("ApplyToProjectConfirm",
			        "Apply preset '{0}' to the project?\n{1} node(s) currently differ from this preset."),
			PresetNameText,
			FText::AsNumber(DifferingCount)));
	if (DialogResult != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	UKawaiiPhysicsPresetDataAsset* Preset = Cast<UKawaiiPhysicsPresetDataAsset>(PresetPath.TryLoad());
	if (!Preset)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplyToProjectPresetLoadFailed", "Failed to load the preset for Apply to Project."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	TArray<FKawaiiPhysicsNodeAuditEntry> Report;
	const int32 AppliedCount =
		UKawaiiPhysicsEditorLibrary::ApplyPresetToProject(Preset, false, bCheckOutFiles, Report);
	ReplaceEntriesFromReport(Report);

	KawaiiPhysicsEdWindowUtils::ShowNotification(
		FText::Format(
			LOCTEXT("ApplyToProjectSucceeded", "Applied preset to {0} node(s)."),
			FText::AsNumber(AppliedCount)),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

FReply SKawaiiPhysicsNodeAuditWindow::OnExportClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	const void* ParentWindowHandle = nullptr;
	if (TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared()))
	{
		if (ParentWindow->GetNativeWindow().IsValid())
		{
			ParentWindowHandle = ParentWindow->GetNativeWindow()->GetOSWindowHandle();
		}
	}

	TArray<FString> OutFilenames;
	const bool bSaved = DesktopPlatform->SaveFileDialog(
		ParentWindowHandle,
		LOCTEXT("ExportDialogTitle", "Export Kawaii Physics Audit").ToString(),
		TEXT(""),
		TEXT("KawaiiPhysicsAudit"),
		TEXT("JSON (*.json)|*.json|CSV (*.csv)|*.csv"),
		EFileDialogFlags::None,
		OutFilenames);
	if (!bSaved || OutFilenames.IsEmpty())
	{
		return FReply::Handled();
	}

	const FString OutputPath = OutFilenames[0];
	const FString Extension = FPaths::GetExtension(OutputPath).ToLower();
	const bool bWriteOk = Extension == TEXT("csv") ? ExportEntriesToCsv(OutputPath) : ExportEntriesToJson(OutputPath);

	if (!bWriteOk)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ExportFailed", "Failed to export the Kawaii Physics audit results."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	ShowAuditExportSucceededNotification(OutputPath);
	return FReply::Handled();
}

bool SKawaiiPhysicsNodeAuditWindow::ExportEntriesToJson(const FString& OutputPath) const
{
	TArray<FKawaiiPhysicsNodeAuditEntry> FlatEntries;
	FlatEntries.Reserve(Entries.Num());
	TSet<FSoftObjectPath> DistinctAnimBlueprints;
	for (const FEntryPtr& Entry : Entries)
	{
		if (!Entry.IsValid())
		{
			continue;
		}
		FlatEntries.Add(*Entry);
		DistinctAnimBlueprints.Add(Entry->AnimBlueprintPath);
	}

	// コマンドレットのTotalAnimBlueprintsはContent走査母数だが、このウィンドウはプロジェクト全体を走査していないため、
	// 現在の一覧に含まれるAnimBlueprintの異なり数で代用する（スキーマのフィールド名は同一）。
	const TSharedPtr<FJsonObject> RootObject =
		KawaiiPhysicsAuditJson::MakeAuditJsonObject(FlatEntries, DistinctAnimBlueprints.Num());
	if (!RootObject.IsValid())
	{
		return false;
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
	{
		return false;
	}

	return FFileHelper::SaveStringToFile(OutputString, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool SKawaiiPhysicsNodeAuditWindow::ExportEntriesToCsv(const FString& OutputPath) const
{
	TArray<FString> Lines;
	Lines.Add(TEXT("AnimBlueprint,Graph,Tag,RootBone,Matches,DiffCount,DiffProperties"));

	for (const FEntryPtr& EntryPtr : Entries)
	{
		if (!EntryPtr.IsValid())
		{
			continue;
		}

		const FKawaiiPhysicsNodeAuditEntry& Entry = *EntryPtr;
		Lines.Add(FString::Printf(
			TEXT("%s,%s,%s,%s,%s,%d,%s"),
			*EscapeCsvField(Entry.AnimBlueprintPath.GetAssetName()),
			*EscapeCsvField(Entry.GraphName.ToString()),
			*EscapeCsvField(Entry.KawaiiPhysicsTag.ToString()),
			*EscapeCsvField(Entry.RootBoneName.ToString()),
			*EscapeCsvField(GetMatchesCellText(Entry).ToString()),
			Entry.DiffProperties.Num(),
			*EscapeCsvField(JoinDiffPropertyNames(Entry))));
	}

	const FString CsvString = FString::Join(Lines, TEXT("\r\n"));
	return FFileHelper::SaveStringToFile(CsvString, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

#undef LOCTEXT_NAMESPACE
