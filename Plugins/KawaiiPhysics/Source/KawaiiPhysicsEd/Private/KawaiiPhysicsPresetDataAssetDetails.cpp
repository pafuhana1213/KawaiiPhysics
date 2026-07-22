// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsPresetDataAssetDetails.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "KawaiiPhysicsPresetDataAsset.h"
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

	FString JoinPropertyNames(const TArray<FName>& PropertyNames)
	{
		TArray<FString> Strings;
		Strings.Reserve(PropertyNames.Num());
		for (const FName& PropertyName : PropertyNames)
		{
			Strings.Add(PropertyName.ToString());
		}
		return FString::Join(Strings, TEXT(", "));
	}

	void ShowTargetNodes(UKawaiiPhysicsPresetDataAsset* Preset)
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
		UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssets(TArray<FString>(), AnimBlueprintAssets);

		int32 MatchingNodeCount = 0;
		int32 MatchingAnimBlueprintCount = 0;
		for (const FAssetData& AssetData : AnimBlueprintAssets)
		{
			UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
			if (!AnimBlueprint)
			{
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("ShowTargetNodes: Failed to load AnimBlueprint asset '%s'."),
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
					       TEXT("ShowTargetNodes: AnimBlueprint=%s NodeTag=%s Graph=%s NodeGuid=%s"),
					       *AnimBlueprint->GetName(),
					       *GraphNode->Node.KawaiiPhysicsTag.ToString(),
					       *(GraphNode->GetGraph() ? GraphNode->GetGraph()->GetName() : FString(TEXT("None"))),
					       *GraphNode->NodeGuid.ToString());
				}
			}
		}

		ShowPresetDetailsNotification(
			FText::Format(
				LOCTEXT("ShowTargetNodesResult", "{0} nodes in {1} AnimBlueprints match TargetTags."),
				FText::AsNumber(MatchingNodeCount),
				FText::AsNumber(MatchingAnimBlueprintCount)),
			SNotificationItem::CS_Success);
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
			       *JoinPropertyNames(Entry.DiffProperties));
		}

		ShowPresetDetailsNotification(
			FText::Format(
				LOCTEXT("ApplyToProjectDryRunResult",
				        "Dry run: {0} nodes matched, {1} differ from preset.\nApply through the ReapplyPresetToProject API (Python/BP)."),
				FText::AsNumber(Report.Num()),
				FText::AsNumber(DiffNodeCount)),
			DiffNodeCount > 0 ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
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
				ShowTargetNodes(WeakPreset.Get());
				return FReply::Handled();
			})
			.Content()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Show Target Nodes")))
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
