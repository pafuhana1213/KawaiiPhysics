// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "SKawaiiPhysicsPresetDiffWindow.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "BlueprintEditorModule.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "KawaiiPhysicsEdWindowUtils.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsPresetDiffWindow"

SLATE_IMPLEMENT_WIDGET(SKawaiiPhysicsPresetDiffWindow)

void SKawaiiPhysicsPresetDiffWindow::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
	(void)AttributeInitializer;
}

namespace
{
	const FName SelectionColumnName(TEXT("Selection"));
	const FName CategoryColumnName(TEXT("Category"));
	const FName PropertyColumnName(TEXT("Property"));
	const FName NodeValueColumnName(TEXT("NodeValue"));
	const FName PresetValueColumnName(TEXT("PresetValue"));

	TArray<TWeakPtr<SKawaiiPhysicsPresetDiffWindow>> LivePresetDiffWindows;

	void RegisterLiveWindow(TSharedRef<SKawaiiPhysicsPresetDiffWindow> Window)
	{
		LivePresetDiffWindows.RemoveAllSwap([](const TWeakPtr<SKawaiiPhysicsPresetDiffWindow>& ExistingWindow)
		{
			return !ExistingWindow.IsValid();
		});
		for (const TWeakPtr<SKawaiiPhysicsPresetDiffWindow>& ExistingWindow : LivePresetDiffWindows)
		{
			if (ExistingWindow.Pin().Get() == &Window.Get())
			{
				return;
			}
		}
		LivePresetDiffWindows.Add(Window);
	}

	FKawaiiPhysicsGraphNodeHandle MakePresetDiffHandle(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		FKawaiiPhysicsGraphNodeHandle Handle;
		Handle.Node = GraphNode;
		return Handle;
	}

	bool CopyPropertiesByName(const TArray<FName>& Names,
	                          bool bPresetToNode,
	                          FAnimNode_KawaiiPhysics& NodeStruct,
	                          UKawaiiPhysicsPresetDataAsset& Preset,
	                          UObject* CustomExternalForceOuter)
	{
		for (const FName& Name : Names)
		{
			const FProperty* Property = FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), Name);
			if (!Property)
			{
				return false;
			}

			if (Name == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CustomExternalForces))
			{
				if (!CustomExternalForceOuter)
				{
					return false;
				}

				if (bPresetToNode)
				{
					UKawaiiPhysicsPresetDataAsset::DuplicateCustomExternalForces(
						Preset.Node.CustomExternalForces,
						NodeStruct.CustomExternalForces,
						CustomExternalForceOuter);
				}
				else
				{
					UKawaiiPhysicsPresetDataAsset::DuplicateCustomExternalForces(
						NodeStruct.CustomExternalForces,
						Preset.Node.CustomExternalForces,
						CustomExternalForceOuter);
				}
				continue;
			}

			void* DstContainer = bPresetToNode ? static_cast<void*>(&NodeStruct) : static_cast<void*>(&Preset.Node);
			const void* SrcContainer = bPresetToNode
				                           ? static_cast<const void*>(&Preset.Node)
				                           : static_cast<const void*>(&NodeStruct);
			Property->CopyCompleteValue_InContainer(DstContainer, SrcContainer);
		}
		return true;
	}

	bool SnapshotRowsMatch(const FKawaiiPhysicsPresetDiffSnapshot& CurrentSnapshot,
	                       const FKawaiiPhysicsPresetDiffSnapshot& FreshSnapshot,
	                       const TSet<FName>* PropertyFilter)
	{
		TMap<FName, TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>> FreshRowsByName;
		for (const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>& FreshRow : FreshSnapshot.Rows)
		{
			if (FreshRow.IsValid())
			{
				FreshRowsByName.Add(FreshRow->PropertyName, FreshRow);
			}
		}

		for (const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>& CurrentRow : CurrentSnapshot.Rows)
		{
			if (!CurrentRow.IsValid())
			{
				continue;
			}

			if (PropertyFilter && !PropertyFilter->Contains(CurrentRow->PropertyName))
			{
				continue;
			}

			const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>* FreshRowPtr =
				FreshRowsByName.Find(CurrentRow->PropertyName);
			if (!FreshRowPtr || !FreshRowPtr->IsValid())
			{
				return false;
			}

			const FKawaiiPhysicsPresetDiffPropertyRow& FreshRow = **FreshRowPtr;
			if (CurrentRow->NodeValue != FreshRow.NodeValue ||
				CurrentRow->PresetValue != FreshRow.PresetValue)
			{
				return false;
			}
		}

		if (!PropertyFilter)
		{
			for (const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>& FreshRow : FreshSnapshot.Rows)
			{
				if (FreshRow.IsValid() && !CurrentSnapshot.Rows.ContainsByPredicate(
					    [&FreshRow](const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>& CurrentRow)
					    {
						    return CurrentRow.IsValid() && CurrentRow->PropertyName == FreshRow->PropertyName;
					    }))
				{
					return false;
				}
			}
		}

		return true;
	}

	TArray<FName> GetDifferingPropertyNames(const FKawaiiPhysicsPresetDiffSnapshot& Snapshot)
	{
		TArray<FName> Names;
		for (const TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>& Row : Snapshot.Rows)
		{
			if (Row.IsValid() && Row->bDiffers)
			{
				Names.Add(Row->PropertyName);
			}
		}
		return Names;
	}

	void MarkPresetDiffGraphNodeBlueprintModified(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		if (GraphNode)
		{
			if (UAnimBlueprint* AnimBlueprint = GraphNode->GetAnimBlueprint())
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
			}
		}
	}

	class SKawaiiPhysicsPresetDiffRow : public SMultiColumnTableRow<TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>>
	{
	public:
		SLATE_BEGIN_ARGS(SKawaiiPhysicsPresetDiffRow)
			{
			}
			SLATE_ARGUMENT(TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>, Row)
			SLATE_ARGUMENT(TSet<FName>*, SelectedPropertyNames)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
		{
			Row = InArgs._Row;
			SelectedPropertyNames = InArgs._SelectedPropertyNames;
			SMultiColumnTableRow<TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>>::Construct(
				FSuperRowType::FArguments().Padding(FMargin(2.0f, 1.0f)),
				OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (!Row.IsValid())
			{
				return SNew(STextBlock);
			}

			if (ColumnName == SelectionColumnName)
			{
				return SNew(SCheckBox)
					.IsChecked(this, &SKawaiiPhysicsPresetDiffRow::IsChecked)
					.OnCheckStateChanged(this, &SKawaiiPhysicsPresetDiffRow::OnCheckStateChanged)
					.ToolTipText(LOCTEXT("RowSelectionToolTip", "このプロパティを Apply Selected / Update Preset from Node の対象にします / Selects this property for Apply Selected or Update Preset from Node."));
			}

			if (ColumnName == CategoryColumnName)
			{
				return MakeTextCell(FText::FromString(Row->Category), FText::FromString(Row->Category));
			}

			if (ColumnName == PropertyColumnName)
			{
				return MakeTextCell(Row->DisplayName, FText::FromName(Row->PropertyName));
			}

			if (ColumnName == NodeValueColumnName)
			{
				return MakeTextCell(FText::FromString(Row->NodeValue), FText::FromString(Row->NodeValue));
			}

			if (ColumnName == PresetValueColumnName)
			{
				return MakeTextCell(FText::FromString(Row->PresetValue), FText::FromString(Row->PresetValue));
			}

			return SNew(STextBlock);
		}

	private:
		TSharedRef<SWidget> MakeTextCell(const FText& Text, const FText& ToolTipText) const
		{
			return SNew(STextBlock)
				.Text(Text)
				.ToolTipText(ToolTipText)
				.ColorAndOpacity(Row->bDiffers
					                 ? FAppStyle::Get().GetSlateColor(TEXT("Colors.AccentYellow"))
					                 : FSlateColor::UseForeground())
				.Clipping(EWidgetClipping::OnDemand);
		}

		ECheckBoxState IsChecked() const
		{
			return Row.IsValid() && SelectedPropertyNames && SelectedPropertyNames->Contains(Row->PropertyName)
				       ? ECheckBoxState::Checked
				       : ECheckBoxState::Unchecked;
		}

		void OnCheckStateChanged(ECheckBoxState NewState)
		{
			if (!Row.IsValid() || !SelectedPropertyNames)
			{
				return;
			}

			if (NewState == ECheckBoxState::Checked)
			{
				SelectedPropertyNames->Add(Row->PropertyName);
			}
			else
			{
				SelectedPropertyNames->Remove(Row->PropertyName);
			}
		}

		TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow> Row;
		TSet<FName>* SelectedPropertyNames = nullptr;
	};
}

const FName SKawaiiPhysicsPresetDiffWindow::PresetDiffTabId(TEXT("KawaiiPhysicsPresetDiff"));

void SKawaiiPhysicsPresetDiffWindow::Construct(const FArguments& InArgs, FKawaiiPhysicsPresetDiffWindowArgs DiffArgs)
{
	(void)InArgs;
	SetArgs(MoveTemp(DiffArgs));

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
				if (Snapshots.IsEmpty() && !NodeGuid.IsValid())
				{
					return LOCTEXT("NoDiffSelectedGuidance", "差分未選択: KawaiiPhysics ノードの [Check Preset Diff] から開いてください / No diff selected: open from a KawaiiPhysics node's [Check Preset Diff].");
				}
				return ContextLabel;
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SComboBox<FSnapshotPtr>)
				.OptionsSource(&Snapshots)
				.InitiallySelectedItem(SelectedSnapshot)
				.OnGenerateWidget(this, &SKawaiiPhysicsPresetDiffWindow::GeneratePresetComboWidget)
				.OnSelectionChanged(this, &SKawaiiPhysicsPresetDiffWindow::OnPresetSelectionChanged)
				.ToolTipText(LOCTEXT("PresetComboToolTip", "表示するプリセット差分を選択します / Selects the preset diff to display."))
				[
					SNew(STextBlock)
					.Text(this, &SKawaiiPhysicsPresetDiffWindow::GetSelectedPresetText)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("OpenPresetButtonToolTip", "選択中のプリセットアセットを開きます / Opens the selected preset asset."))
				.OnClicked(this, &SKawaiiPhysicsPresetDiffWindow::OnOpenPresetClicked)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.BrowseContent"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("RefreshButtonToolTip", "ノードを再解決して差分を再計算します / Re-resolves the node and rebuilds the diffs."))
				.OnClicked(this, &SKawaiiPhysicsPresetDiffWindow::OnRefreshClicked)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Refresh"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SKawaiiPhysicsPresetDiffWindow::GetSummaryText)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
				{
					return bShowAllProperties ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged(this, &SKawaiiPhysicsPresetDiffWindow::OnShowAllChanged)
				.ToolTipText(LOCTEXT("ShowAllPropertiesToolTip", "一致しているプロパティも表示します / Shows properties that already match too."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowAllPropertiesLabel", "Show All Properties"))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSearchBox)
				.OnTextChanged(this, &SKawaiiPhysicsPresetDiffWindow::OnSearchTextChanged)
				.ToolTipText(LOCTEXT("SearchToolTip", "カテゴリ、表示名、内部プロパティ名で絞り込みます / Filters by category, display name, or internal property name."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("CopyButtonToolTip", "差分をタブ区切りテキストとしてクリップボードへコピーします / Copies the diff as tab-separated text to the clipboard."))
				.OnClicked(this, &SKawaiiPhysicsPresetDiffWindow::OnCopyClicked)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Clipboard"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 4.0f)
		[
			SAssignNew(DiffListView, SListView<FRowPtr>)
			.ListItemsSource(&FilteredRows)
			.OnGenerateRow(this, &SKawaiiPhysicsPresetDiffWindow::GenerateDiffRow)
			.OnMouseButtonDoubleClick(this, &SKawaiiPhysicsPresetDiffWindow::OnRowDoubleClicked)
			.HeaderRow
			(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(SelectionColumnName)
				.FixedWidth(32.0f)
				.DefaultLabel(FText::GetEmpty())
				.DefaultTooltip(LOCTEXT("SelectionColumnToolTip", "プロパティを操作対象として選択します / Selects a property for operations."))
				+ SHeaderRow::Column(CategoryColumnName)
				.FillWidth(0.18f)
				.DefaultLabel(LOCTEXT("CategoryColumnLabel", "Category"))
				.DefaultTooltip(LOCTEXT("CategoryColumnToolTip", "プロパティカテゴリを表示します / Shows the property category."))
				+ SHeaderRow::Column(PropertyColumnName)
				.FillWidth(0.22f)
				.DefaultLabel(LOCTEXT("PropertyColumnLabel", "Property"))
				.DefaultTooltip(LOCTEXT("PropertyColumnToolTip", "表示名を表示し、ツールチップで内部名を表示します / Shows the display name, with the internal name in the tooltip."))
				+ SHeaderRow::Column(NodeValueColumnName)
				.FillWidth(0.30f)
				.DefaultLabel(LOCTEXT("NodeValueColumnLabel", "Node Value"))
				.DefaultTooltip(LOCTEXT("NodeValueColumnToolTip", "現在のノード側の値を表示します / Shows the current value on the node."))
				+ SHeaderRow::Column(PresetValueColumnName)
				.FillWidth(0.30f)
				.DefaultLabel(LOCTEXT("PresetValueColumnLabel", "Preset Value"))
				.DefaultTooltip(LOCTEXT("PresetValueColumnToolTip", "プリセット側の値を表示します / Shows the value stored in the preset."))
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(4.0f, 4.0f))
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplySelectedButton", "Apply Selected"))
					.ToolTipText(LOCTEXT("ApplySelectedButtonToolTip", "チェックしたプロパティだけをプリセットからノードへ適用します / Applies only checked properties from the preset to the node."))
					.IsEnabled(this, &SKawaiiPhysicsPresetDiffWindow::CanApplySelected)
					.OnClicked(this, &SKawaiiPhysicsPresetDiffWindow::OnApplySelectedClicked)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyPresetToNodeButton", "Apply Preset to Node"))
					.ToolTipText(LOCTEXT("ApplyPresetToNodeButtonToolTip", "選択中のプリセット全体をノードへ適用します / Applies the whole selected preset to the node."))
					.OnClicked(this, &SKawaiiPhysicsPresetDiffWindow::OnApplyPresetToNodeClicked)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("UpdatePresetFromNodeButton", "Update Preset from Node"))
					.ToolTipText(LOCTEXT("UpdatePresetFromNodeButtonToolTip", "チェックした差分、または未チェック時は全差分をノードからプリセットへ反映します / Updates checked diffs, or all diffs when none are checked, from the node to the preset."))
					.OnClicked(this, &SKawaiiPhysicsPresetDiffWindow::OnUpdatePresetFromNodeClicked)
				]
			]
		]
	];

	RefreshFilteredRows();
}

void SKawaiiPhysicsPresetDiffWindow::OpenWindow(FKawaiiPhysicsPresetDiffWindowArgs Args)
{
	TSharedPtr<SDockTab> InvokedTab = KawaiiPhysicsEdWindowUtils::InvokeAnimBlueprintEditorTab(
		Args.AnimBlueprintPath,
		PresetDiffTabId,
		LOCTEXT("PresetDiffOpenEditorFailed", "Failed to open the Animation Blueprint editor for Preset Diff."));
	if (!InvokedTab.IsValid())
	{
		return;
	}

	TSharedPtr<SWidget> TabContent = InvokedTab->GetContent();
	if (!TabContent.IsValid())
	{
		return;
	}

	if (TabContent->GetType() == FName(TEXT("SKawaiiPhysicsPresetDiffWindow")))
	{
		TSharedPtr<SKawaiiPhysicsPresetDiffWindow> WindowWidget =
			StaticCastSharedPtr<SKawaiiPhysicsPresetDiffWindow>(TabContent);
		WindowWidget->SetOwnerTab(InvokedTab.ToSharedRef());
		WindowWidget->SetArgs(MoveTemp(Args));
		return;
	}

	KawaiiPhysicsEdWindowUtils::ShowNotification(
		LOCTEXT("PresetDiffTabContentInvalid", "Failed to update the Preset Diff tab content."),
		SNotificationItem::CS_Fail);
}

void SKawaiiPhysicsPresetDiffWindow::CloseAllWindows()
{
	TArray<TSharedPtr<SDockTab>> TabsToClose;
	for (const TWeakPtr<SKawaiiPhysicsPresetDiffWindow>& WeakWindow : LivePresetDiffWindows)
	{
		if (TSharedPtr<SKawaiiPhysicsPresetDiffWindow> Window = WeakWindow.Pin())
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
	LivePresetDiffWindows.Reset();
}

void SKawaiiPhysicsPresetDiffWindow::SetOwnerTab(TSharedRef<SDockTab> InOwnerTab)
{
	OwnerTabWeak = InOwnerTab;
	RegisterLiveWindow(StaticCastSharedRef<SKawaiiPhysicsPresetDiffWindow>(AsShared()));
}

void SKawaiiPhysicsPresetDiffWindow::SetArgs(FKawaiiPhysicsPresetDiffWindowArgs Args)
{
	ContextLabel = MoveTemp(Args.ContextLabel);
	AnimBlueprintPath = Args.AnimBlueprintPath;
	NodeGuid = Args.NodeGuid;
	Snapshots.Reset();
	Snapshots.Reserve(Args.Snapshots.Num());
	for (const TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>& Snapshot : Args.Snapshots)
	{
		Snapshots.Add(Snapshot);
	}

	SelectedSnapshot = Snapshots.Num() > 0 ? Snapshots[0] : nullptr;
	SelectedPropertyNames.Reset();
	RefreshFilteredRows();
}

TSharedRef<SWidget> SKawaiiPhysicsPresetDiffWindow::GeneratePresetComboWidget(FSnapshotPtr Snapshot)
{
	return SNew(STextBlock)
		.Text(Snapshot.IsValid() ? Snapshot->PresetDisplayName : FText::GetEmpty());
}

TSharedRef<ITableRow> SKawaiiPhysicsPresetDiffWindow::GenerateDiffRow(
	FRowPtr Row,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SKawaiiPhysicsPresetDiffRow, OwnerTable)
		.Row(Row)
		.SelectedPropertyNames(&SelectedPropertyNames);
}

void SKawaiiPhysicsPresetDiffWindow::OnPresetSelectionChanged(FSnapshotPtr Snapshot, ESelectInfo::Type SelectInfo)
{
	SelectedSnapshot = Snapshot;
	SelectedPropertyNames.Reset();
	RefreshFilteredRows();
}

void SKawaiiPhysicsPresetDiffWindow::OnShowAllChanged(ECheckBoxState NewState)
{
	bShowAllProperties = NewState == ECheckBoxState::Checked;
	RefreshFilteredRows();
}

void SKawaiiPhysicsPresetDiffWindow::OnSearchTextChanged(const FText& NewText)
{
	SearchText = NewText;
	RefreshFilteredRows();
}

void SKawaiiPhysicsPresetDiffWindow::OnRowDoubleClicked(FRowPtr Row)
{
	UObject* AnimBlueprintObject = AnimBlueprintPath.TryLoad();
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AnimBlueprintObject);
	if (!AnimBlueprint || !GEditor)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("OpenAnimBlueprintFailed", "Failed to open the AnimBlueprint."),
			SNotificationItem::CS_Fail);
		return;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return;
	}

	AssetEditorSubsystem->OpenEditorForAsset(AnimBlueprint);

	UAnimGraphNode_KawaiiPhysics* GraphNode =
		UKawaiiPhysicsEditorLibrary::FindGraphNodeByGuid(AnimBlueprintPath, NodeGuid);
	if (!GraphNode)
	{
		return;
	}

	if (IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(AnimBlueprint, true))
	{
		IBlueprintEditor* BlueprintEditor = static_cast<IBlueprintEditor*>(EditorInstance);
		if (BlueprintEditor)
		{
			BlueprintEditor->JumpToHyperlink(GraphNode, false);
		}
	}
}

FText SKawaiiPhysicsPresetDiffWindow::GetSelectedPresetText() const
{
	return SelectedSnapshot.IsValid() ? SelectedSnapshot->PresetDisplayName : LOCTEXT("NoPresetSelected", "No Preset");
}

FText SKawaiiPhysicsPresetDiffWindow::GetSummaryText() const
{
	if (!SelectedSnapshot.IsValid())
	{
		return LOCTEXT("NoSnapshotSummary", "No preset selected");
	}

	return SelectedSnapshot->bMatches
		       ? LOCTEXT("MatchesPresetSummary", "Matches preset")
		       : FText::Format(
			       LOCTEXT("DiffCountSummary", "{0} properties differ"),
			       FText::AsNumber(SelectedSnapshot->DiffCount));
}

ECheckBoxState SKawaiiPhysicsPresetDiffWindow::IsPropertySelected(FName PropertyName) const
{
	return SelectedPropertyNames.Contains(PropertyName) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SKawaiiPhysicsPresetDiffWindow::SetPropertySelected(ECheckBoxState NewState, FName PropertyName)
{
	if (NewState == ECheckBoxState::Checked)
	{
		SelectedPropertyNames.Add(PropertyName);
	}
	else
	{
		SelectedPropertyNames.Remove(PropertyName);
	}
}

bool SKawaiiPhysicsPresetDiffWindow::CanApplySelected() const
{
	return SelectedPropertyNames.Num() > 0;
}

FReply SKawaiiPhysicsPresetDiffWindow::OnOpenPresetClicked()
{
	if (!SelectedSnapshot.IsValid() || !GEditor)
	{
		return FReply::Handled();
	}

	UObject* PresetObject = SelectedSnapshot->PresetPath.TryLoad();
	if (!PresetObject)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("OpenPresetFailed", "Failed to load the selected preset."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(PresetObject);
	}
	return FReply::Handled();
}

FReply SKawaiiPhysicsPresetDiffWindow::OnCopyClicked()
{
	if (!SelectedSnapshot.IsValid())
	{
		return FReply::Handled();
	}

	const FString ClipboardText = KawaiiPhysicsPresetDiff::MakeClipboardTextFromSnapshot(*SelectedSnapshot, ContextLabel);
	FPlatformApplicationMisc::ClipboardCopy(*ClipboardText);
	KawaiiPhysicsEdWindowUtils::ShowNotification(
		LOCTEXT("CopySucceeded", "Copied preset diff to clipboard."),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

FReply SKawaiiPhysicsPresetDiffWindow::OnRefreshClicked()
{
	if (!NodeGuid.IsValid())
	{
		return FReply::Handled();
	}

	if (!RebuildAllSnapshotsKeepingPreset())
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("RefreshResolveFailed", "Failed to resolve the KawaiiPhysics node."),
			SNotificationItem::CS_Fail);
	}
	return FReply::Handled();
}

FReply SKawaiiPhysicsPresetDiffWindow::OnApplySelectedClicked()
{
	if (!SelectedSnapshot.IsValid() || SelectedPropertyNames.Num() == 0)
	{
		return FReply::Handled();
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode =
		UKawaiiPhysicsEditorLibrary::FindGraphNodeByGuid(AnimBlueprintPath, NodeGuid);
	if (!GraphNode || !GraphNode->GetGraph() || !GraphNode->GetGraph()->Nodes.Contains(GraphNode))
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplySelectedResolveFailed", "Failed to resolve the KawaiiPhysics graph node."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	UKawaiiPhysicsPresetDataAsset* Preset =
		Cast<UKawaiiPhysicsPresetDataAsset>(SelectedSnapshot->PresetPath.TryLoad());
	if (!Preset)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplySelectedPresetLoadFailed", "Failed to load the selected preset."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	const FKawaiiPhysicsPresetApplyOptions Options;
	TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> FreshSnapshot =
		KawaiiPhysicsPresetDiff::BuildSnapshot(GraphNode->Node, *Preset, Options);
	if (!SnapshotRowsMatch(*SelectedSnapshot, *FreshSnapshot, &SelectedPropertyNames))
	{
		ReplaceSelectedSnapshot(FreshSnapshot);
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplySelectedPresetChanged", "The preset or node changed. Review the refreshed diff before applying."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	TArray<FName> Names = SelectedPropertyNames.Array();
	FScopedTransaction Transaction(
		LOCTEXT("ApplySelectedTransaction", "Apply Selected Kawaii Physics Preset Properties"));
	GraphNode->Modify();
	if (!CopyPropertiesByName(Names, true, GraphNode->Node, *Preset, GraphNode))
	{
		Transaction.Cancel();
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplySelectedCopyFailed", "Failed to copy one or more selected properties."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	GraphNode->Node.ModifyBones.Empty();
	GraphNode->ReconstructNode();
	MarkPresetDiffGraphNodeBlueprintModified(GraphNode);

	ReplaceSelectedSnapshot(KawaiiPhysicsPresetDiff::BuildSnapshot(GraphNode->Node, *Preset, Options));
	SelectedPropertyNames.Reset();
	KawaiiPhysicsEdWindowUtils::ShowNotification(
		LOCTEXT("ApplySelectedSucceeded", "Applied selected properties to the node."),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

FReply SKawaiiPhysicsPresetDiffWindow::OnApplyPresetToNodeClicked()
{
	if (!SelectedSnapshot.IsValid())
	{
		return FReply::Handled();
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode =
		UKawaiiPhysicsEditorLibrary::FindGraphNodeByGuid(AnimBlueprintPath, NodeGuid);
	if (!GraphNode || !GraphNode->GetGraph() || !GraphNode->GetGraph()->Nodes.Contains(GraphNode))
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplyPresetResolveFailed", "Failed to resolve the KawaiiPhysics graph node."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	UKawaiiPhysicsPresetDataAsset* Preset =
		Cast<UKawaiiPhysicsPresetDataAsset>(SelectedSnapshot->PresetPath.TryLoad());
	if (!Preset)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplyPresetLoadFailed", "Failed to load the selected preset."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	const FKawaiiPhysicsPresetApplyOptions Options;
	TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> FreshSnapshot =
		KawaiiPhysicsPresetDiff::BuildSnapshot(GraphNode->Node, *Preset, Options);
	if (!SnapshotRowsMatch(*SelectedSnapshot, *FreshSnapshot, nullptr))
	{
		ReplaceSelectedSnapshot(FreshSnapshot);
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplyPresetChanged", "The preset or node changed. Review the refreshed diff before applying."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	if (!UKawaiiPhysicsEditorLibrary::ApplyPresetToGraphNode(MakePresetDiffHandle(GraphNode), Preset, Options))
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("ApplyPresetFailed", "Failed to apply the preset to the node."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	ReplaceSelectedSnapshot(KawaiiPhysicsPresetDiff::BuildSnapshot(GraphNode->Node, *Preset, Options));
	SelectedPropertyNames.Reset();
	KawaiiPhysicsEdWindowUtils::ShowNotification(
		LOCTEXT("ApplyPresetSucceeded", "Applied the preset to the node."),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

FReply SKawaiiPhysicsPresetDiffWindow::OnUpdatePresetFromNodeClicked()
{
	if (!SelectedSnapshot.IsValid())
	{
		return FReply::Handled();
	}

	TArray<FName> Names = SelectedPropertyNames.Num() > 0
		                      ? SelectedPropertyNames.Array()
		                      : GetDifferingPropertyNames(*SelectedSnapshot);
	if (Names.IsEmpty())
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("NoPresetPropertiesToUpdate", "No differing properties to update."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	const EAppReturnType::Type DialogResult = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::Format(
			LOCTEXT("UpdatePresetConfirm", "Update preset '{0}' from the node and overwrite {1} properties?"),
			SelectedSnapshot->PresetDisplayName,
			FText::AsNumber(Names.Num())));
	if (DialogResult != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode =
		UKawaiiPhysicsEditorLibrary::FindGraphNodeByGuid(AnimBlueprintPath, NodeGuid);
	if (!GraphNode)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("UpdatePresetResolveFailed", "Failed to resolve the KawaiiPhysics graph node."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	UKawaiiPhysicsPresetDataAsset* Preset =
		Cast<UKawaiiPhysicsPresetDataAsset>(SelectedSnapshot->PresetPath.TryLoad());
	if (!Preset)
	{
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("UpdatePresetLoadFailed", "Failed to load the selected preset."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	FScopedTransaction Transaction(
		LOCTEXT("UpdatePresetTransaction", "Update Kawaii Physics Preset From Node"));
	Preset->Modify();
	if (!CopyPropertiesByName(Names, false, GraphNode->Node, *Preset, Preset))
	{
		Transaction.Cancel();
		KawaiiPhysicsEdWindowUtils::ShowNotification(
			LOCTEXT("UpdatePresetCopyFailed", "Failed to copy one or more properties to the preset."),
			SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	Preset->MarkPackageDirty();
	const FKawaiiPhysicsPresetApplyOptions Options;
	ReplaceSelectedSnapshot(KawaiiPhysicsPresetDiff::BuildSnapshot(GraphNode->Node, *Preset, Options));
	SelectedPropertyNames.Reset();
	KawaiiPhysicsEdWindowUtils::ShowNotification(
		LOCTEXT("UpdatePresetSucceeded", "Updated the preset from the node."),
		SNotificationItem::CS_Success);
	return FReply::Handled();
}

void SKawaiiPhysicsPresetDiffWindow::RefreshFilteredRows()
{
	FilteredRows.Reset();
	if (SelectedSnapshot.IsValid())
	{
		const FString FilterString = SearchText.ToString();
		for (const FRowPtr& Row : SelectedSnapshot->Rows)
		{
			if (!Row.IsValid())
			{
				continue;
			}

			if (!bShowAllProperties && !Row->bDiffers)
			{
				continue;
			}

			if (!FilterString.IsEmpty() &&
				!Row->DisplayName.ToString().Contains(FilterString, ESearchCase::IgnoreCase) &&
				!Row->PropertyName.ToString().Contains(FilterString, ESearchCase::IgnoreCase) &&
				!Row->Category.Contains(FilterString, ESearchCase::IgnoreCase))
			{
				continue;
			}

			FilteredRows.Add(Row);
		}
	}

	if (DiffListView.IsValid())
	{
		DiffListView->RequestListRefresh();
	}
}

void SKawaiiPhysicsPresetDiffWindow::ReplaceSelectedSnapshot(
	TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> NewSnapshot)
{
	for (FSnapshotPtr& Snapshot : Snapshots)
	{
		if (Snapshot.IsValid() && Snapshot->PresetPath == NewSnapshot->PresetPath)
		{
			Snapshot = NewSnapshot;
			SelectedSnapshot = Snapshot;
			RefreshFilteredRows();
			return;
		}
	}

	FSnapshotPtr NewSnapshotPtr = NewSnapshot;
	Snapshots.Add(NewSnapshotPtr);
	SelectedSnapshot = NewSnapshotPtr;
	RefreshFilteredRows();
}

bool SKawaiiPhysicsPresetDiffWindow::RebuildAllSnapshotsKeepingPreset()
{
	if (!NodeGuid.IsValid())
	{
		return false;
	}

	UAnimGraphNode_KawaiiPhysics* GraphNode =
		UKawaiiPhysicsEditorLibrary::FindGraphNodeByGuid(AnimBlueprintPath, NodeGuid);
	if (!GraphNode)
	{
		return false;
	}

	const FSoftObjectPath PreviousPresetPath = SelectedSnapshot.IsValid()
		                                          ? SelectedSnapshot->PresetPath
		                                          : FSoftObjectPath();
	const FKawaiiPhysicsPresetApplyOptions Options;
	const TArray<TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>> NewSnapshots =
		KawaiiPhysicsPresetDiff::BuildSnapshotsForNode(GraphNode->Node, Options);

	Snapshots.Reset();
	Snapshots.Reserve(NewSnapshots.Num());
	for (const TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>& Snapshot : NewSnapshots)
	{
		Snapshots.Add(Snapshot);
	}

	SelectedSnapshot.Reset();
	for (const FSnapshotPtr& Snapshot : Snapshots)
	{
		if (Snapshot.IsValid() && Snapshot->PresetPath == PreviousPresetPath)
		{
			SelectedSnapshot = Snapshot;
			break;
		}
	}
	if (!SelectedSnapshot.IsValid() && Snapshots.Num() > 0)
	{
		SelectedSnapshot = Snapshots[0];
	}

	SelectedPropertyNames.Reset();
	RefreshFilteredRows();
	return true;
}

#undef LOCTEXT_NAMESPACE
