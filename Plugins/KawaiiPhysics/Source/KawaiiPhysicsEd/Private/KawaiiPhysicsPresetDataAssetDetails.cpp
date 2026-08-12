// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsPresetDataAssetDetails.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "Misc/App.h"
#include "Misc/ScopedSlowTask.h"
#include "SKawaiiPhysicsNodeAuditWindow.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsPresetDataAssetDetails"

namespace
{
	void ShowPresetDetailsNotification(const FText& NotificationText,
	                                   const SNotificationItem::ECompletionState CompletionState)
	{
		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = 5.0f;

		TSharedPtr<SNotificationItem> NotificationItem =
			FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(CompletionState);
		}
	}

	FString JoinPresetPropertyNames(const TArray<FName>& PropertyNames)
	{
		TArray<FString> Strings;
		Strings.Reserve(PropertyNames.Num());
		for (const FName& PropertyName : PropertyNames)
		{
			Strings.Add(PropertyName.ToString());
		}
		return FString::Join(Strings, TEXT(", "));
	}

	void FindTargetNodes(UKawaiiPhysicsPresetDataAsset* Preset)
	{
		if (!Preset)
		{
			return;
		}

		if (Preset->TargetTags.IsEmpty())
		{
			ShowPresetDetailsNotification(
				LOCTEXT("TargetTagsEmpty", "TargetTags is empty - no nodes will be targeted."),
				SNotificationItem::CS_Fail);
			return;
		}

		TArray<FAssetData> AnimBlueprintAssets;
		UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssetsReferencingTags(
			Preset->TargetTags,
			Preset->bTargetTagsExactMatch,
			TArray<FString>(),
			AnimBlueprintAssets);
		UE_LOG(LogKawaiiPhysics, Display,
		       TEXT("FindTargetNodes: SearchableName prefilter kept %d AnimBlueprint asset(s)."),
		       AnimBlueprintAssets.Num());

		// アセット数が多いとロード＋走査に時間がかかるため、進捗ダイアログを表示する（キャンセル非対応）。
		FScopedSlowTask SlowTask(static_cast<float>(AnimBlueprintAssets.Num()),
		                          LOCTEXT("FindTargetNodesProgress", "Searching AnimBlueprints for KawaiiPhysics nodes..."));
		if (!IsRunningCommandlet() && !FApp::IsUnattended() && FSlateApplication::IsInitialized())
		{
			SlowTask.MakeDialog();
		}

		TArray<FKawaiiPhysicsNodeAuditEntry> Entries;
		int32 MatchingNodeCount = 0;
		int32 MatchingAnimBlueprintCount = 0;
		for (const FAssetData& AssetData : AnimBlueprintAssets)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::FromName(AssetData.AssetName));

			UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
			if (!AnimBlueprint)
			{
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("FindTargetNodes: Failed to load AnimBlueprint asset '%s'."),
				       *AssetData.GetSoftObjectPath().ToString());
				continue;
			}

			const TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
				UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsGraphNodes(
					AnimBlueprint, Preset->TargetTags, Preset->bTargetTagsExactMatch);
			if (Handles.IsEmpty())
			{
				continue;
			}

			++MatchingAnimBlueprintCount;
			for (const FKawaiiPhysicsGraphNodeHandle& Handle : Handles)
			{
				if (UAnimGraphNode_KawaiiPhysics* GraphNode = Handle.Node.Get())
				{
					++MatchingNodeCount;
					UE_LOG(LogKawaiiPhysics, Display,
					       TEXT("FindTargetNodes: AnimBlueprint=%s NodeTag=%s Graph=%s NodeGuid=%s"),
					       *AnimBlueprint->GetName(),
					       *GraphNode->Node.KawaiiPhysicsTag.ToString(),
					       *(GraphNode->GetGraph() ? GraphNode->GetGraph()->GetName() : FString(TEXT("None"))),
					       *GraphNode->NodeGuid.ToString());

					// FindTargetNodesはプリセットとの差分比較を行わないため、bMatchesPreset/DiffProperties等は既定値のまま積む。
					FKawaiiPhysicsNodeAuditEntry Entry;
					Entry.AnimBlueprintPath = FSoftObjectPath(AnimBlueprint);
					Entry.GraphName = GraphNode->GetGraph() ? GraphNode->GetGraph()->GetFName() : NAME_None;
					Entry.NodeGuid = GraphNode->NodeGuid;
					Entry.RootBoneName = GraphNode->Node.RootBone.BoneName;
					Entry.KawaiiPhysicsTag = GraphNode->Node.KawaiiPhysicsTag;
					Entry.BoneSubdivisionCount = GraphNode->Node.BoneSubdivisionCount;
					Entry.BoneConstraintSubdivisionCount = GraphNode->Node.BoneConstraintSubdivisionCount;
					Entry.bAllowWorldCollision = GraphNode->Node.bAllowWorldCollision;
					Entry.bUseSharedCollision = GraphNode->Node.bUseSharedCollision;
					Entry.bSharedCollisionSource = GraphNode->Node.bSharedCollisionSource;
					Entry.bEnableWind = GraphNode->Node.bEnableWind;
					Entry.ExternalForceCount =
						GraphNode->Node.ExternalForces.Num() + GraphNode->Node.CustomExternalForces.Num();
					Entry.WarmUpFrames = GraphNode->Node.WarmUpFrames;
					Entries.Add(MoveTemp(Entry));
				}
			}
		}

		FKawaiiPhysicsNodeAuditWindowArgs WindowArgs;
		WindowArgs.WindowTitle = FText::Format(
			LOCTEXT("FindTargetNodesWindowTitle", "Find Target Nodes: {0}"),
			FText::FromString(Preset->GetName()));
		WindowArgs.SummaryText = FText::Format(
			LOCTEXT("FindTargetNodesResult", "{0} nodes in {1} AnimBlueprints match TargetTags."),
			FText::AsNumber(MatchingNodeCount),
			FText::AsNumber(MatchingAnimBlueprintCount));
		WindowArgs.Entries = MoveTemp(Entries);
		WindowArgs.bShowDiffColumns = false;
		WindowArgs.PresetPath = FSoftObjectPath(Preset);
		SKawaiiPhysicsNodeAuditWindow::OpenWindow(MoveTemp(WindowArgs));
	}

	void ApplyToProjectDryRun(UKawaiiPhysicsPresetDataAsset* Preset)
	{
		if (!Preset)
		{
			return;
		}

		TArray<FKawaiiPhysicsNodeAuditEntry> Report;
		UKawaiiPhysicsEditorLibrary::ReapplyPresetToProject(
			Preset, true, false, Report);

		int32 DiffNodeCount = 0;
		for (const FKawaiiPhysicsNodeAuditEntry& Entry : Report)
		{
			if (Entry.bMatchesPreset)
			{
				continue;
			}

			++DiffNodeCount;
			UE_LOG(LogKawaiiPhysics, Display,
			       TEXT("ApplyToProjectDryRun: AnimBlueprint=%s NodeTag=%s DiffProperties=%s"),
			       *Entry.AnimBlueprintPath.GetAssetName(),
			       *Entry.KawaiiPhysicsTag.ToString(),
			       *JoinPresetPropertyNames(Entry.DiffProperties));
		}

		FKawaiiPhysicsNodeAuditWindowArgs WindowArgs;
		WindowArgs.WindowTitle = FText::Format(
			LOCTEXT("ApplyToProjectDryRunWindowTitle", "Apply To Project (Dry Run): {0}"),
			FText::FromString(Preset->GetName()));
		WindowArgs.SummaryText = FText::Format(
			LOCTEXT("ApplyToProjectDryRunResult", "Dry run: {0} nodes matched, {1} differ from preset."),
			FText::AsNumber(Report.Num()),
			FText::AsNumber(DiffNodeCount));
		WindowArgs.Entries = MoveTemp(Report);
		WindowArgs.bShowDiffColumns = true;
		WindowArgs.PresetPath = FSoftObjectPath(Preset);
		SKawaiiPhysicsNodeAuditWindow::OpenWindow(MoveTemp(WindowArgs));
	}
}

TSharedRef<IDetailCustomization> FKawaiiPhysicsPresetDataAssetDetails::MakeInstance()
{
	return MakeShareable(new FKawaiiPhysicsPresetDataAssetDetails);
}

void FKawaiiPhysicsPresetDataAssetDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideCategory(TEXT("Links"));
	DetailBuilder.HideCategory(TEXT("Alpha"));
	DetailBuilder.HideCategory(TEXT("Performance"));

	// カテゴリの作成順が表示順になるため、DescriptionをDetails上部（Targetより前）に移動する。
	DetailBuilder.EditCategory(TEXT("Description"));

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	TWeakObjectPtr<UKawaiiPhysicsPresetDataAsset> WeakPreset;
	for (const TWeakObjectPtr<UObject>& Object : ObjectsBeingCustomized)
	{
		if (UKawaiiPhysicsPresetDataAsset* Preset = Cast<UKawaiiPhysicsPresetDataAsset>(Object.Get()))
		{
			WeakPreset = Preset;
			break;
		}
	}

	IDetailCategoryBuilder& TargetCategory = DetailBuilder.EditCategory(TEXT("Target"));
	FDetailWidgetRow& WidgetRow = TargetCategory.AddCustomRow(
		LOCTEXT("KawaiiPhysicsPresetTargetTools", "KawaiiPhysicsPresetTargetTools"));
	WidgetRow
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(2, 0, 2, 0))
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([WeakPreset]()
			{
				FindTargetNodes(WeakPreset.Get());
				return FReply::Handled();
			})
			.ToolTipText(LOCTEXT("FindTargetNodesToolTip", "TargetTags に一致する KawaiiPhysics ノードをプロジェクト内の全 AnimBlueprint から検索し、結果をタブで一覧表示します（詳細は Output Log にも出力） / Searches all AnimBlueprints in the project for KawaiiPhysics nodes matching TargetTags and lists the results in a tab (details are also logged to the Output Log)."))
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Find Target Nodes")))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([WeakPreset]()
			{
				ApplyToProjectDryRun(WeakPreset.Get());
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Apply to Project (Dry Run)")))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 9))
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
