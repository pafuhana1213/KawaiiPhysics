// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEdWindowUtils.h"

#include "Animation/AnimBlueprint.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"

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
}
