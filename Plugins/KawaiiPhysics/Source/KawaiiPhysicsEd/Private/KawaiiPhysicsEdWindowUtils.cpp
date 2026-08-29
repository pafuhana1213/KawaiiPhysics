// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEdWindowUtils.h"

#include "Animation/AnimBlueprint.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "KawaiiPhysicsEdStyle.h"
#include "Misc/EngineVersionComparison.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsEdWindowUtils"

namespace KawaiiPhysicsEdWindowUtils
{
	void ShowNotification(const FText& NotificationText,
	                      SNotificationItem::ECompletionState CompletionState,
	                      float ExpireDuration,
	                      const FSimpleDelegate& Hyperlink,
	                      const FText& HyperlinkText)
	{
		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = ExpireDuration;
		// Hyperlinkが設定されていれば通知に付与する
		if (Hyperlink.IsBound())
		{
			NotificationInfo.Hyperlink = Hyperlink;
			NotificationInfo.HyperlinkText = HyperlinkText;
		}

		// 通知を表示し、完了状態を設定する
		TSharedPtr<SNotificationItem> NotificationItem =
			FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetCompletionState(CompletionState);
		}
	}

	TSharedPtr<SDockTab> InvokeAnimBlueprintEditorTab(
		const FSoftObjectPath& AnimBlueprintPath,
		FName TabId,
		const FText& ResolveFailedMessage)
	{
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AnimBlueprintPath.TryLoad());
		if (!AnimBlueprint || !GEditor)
		{
			ShowNotification(ResolveFailedMessage, SNotificationItem::CS_Fail);
			return nullptr;
		}

		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!AssetEditorSubsystem)
		{
			ShowNotification(ResolveFailedMessage, SNotificationItem::CS_Fail);
			return nullptr;
		}

		// エディタが未オープンなら開いてから再取得する
		IAssetEditorInstance* AssetEditorInstance = AssetEditorSubsystem->FindEditorForAsset(AnimBlueprint, true);
		if (!AssetEditorInstance)
		{
			AssetEditorSubsystem->OpenEditorForAsset(AnimBlueprint);
			AssetEditorInstance = AssetEditorSubsystem->FindEditorForAsset(AnimBlueprint, true);
		}

		FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(AssetEditorInstance);
		if (!Toolkit || !Toolkit->GetTabManager().IsValid())
		{
			ShowNotification(ResolveFailedMessage, SNotificationItem::CS_Fail);
			return nullptr;
		}

		TSharedPtr<SDockTab> InvokedTab = Toolkit->GetTabManager()->TryInvokeTab(TabId);
		if (!InvokedTab.IsValid())
		{
			ShowNotification(ResolveFailedMessage, SNotificationItem::CS_Fail);
		}
		return InvokedTab;
	}

	TSharedRef<FWorkspaceItem> FindOrAddKawaiiPhysicsMenuGroup(const TSharedRef<FWorkspaceItem>& Parent)
	{
		const FText GroupDisplayName = LOCTEXT("KawaiiPhysicsMenuGroup", "Kawaii Physics");
		const FSlateIcon Icon(
			FKawaiiPhysicsEdStyle::GetStyleSetName(),
			TEXT("KawaiiPhysics.TabIcon"));

#if UE_VERSION_OLDER_THAN(5, 4, 0)
		for (const TSharedRef<FWorkspaceItem>& Child : Parent->GetChildItems())
		{
			if (!Child->AsSpawnerEntry().IsValid() && Child->GetDisplayName().EqualTo(GroupDisplayName))
			{
				return Child;
			}
		}
		return Parent->AddGroup(GroupDisplayName, Icon, false);
#else
		const FName GroupName(TEXT("KawaiiPhysics"));
		for (const TSharedRef<FWorkspaceItem>& Child : Parent->GetChildItems())
		{
			if (Child->GetFName() == GroupName)
			{
				return Child;
			}
		}
		return Parent->AddGroup(
			GroupName,
			GroupDisplayName,
			LOCTEXT("KawaiiPhysicsMenuGroupTooltip", "KawaiiPhysics のツール群 / KawaiiPhysics tools"),
			Icon,
			false);
#endif
	}

	void RemoveStaleSpawnerChildren(const TSharedRef<FWorkspaceItem>& Group, FName TabId)
	{
		const TArray<TSharedRef<FWorkspaceItem>> Children = Group->GetChildItems();
		for (const TSharedRef<FWorkspaceItem>& Child : Children)
		{
			const TSharedPtr<FTabSpawnerEntry> Spawner = Child->AsSpawnerEntry();
			if (Spawner.IsValid() && Spawner->GetTabType() == TabId)
			{
				Group->RemoveItem(Child);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
