// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Notifications/SNotificationList.h"

class SDockTab;
class FWorkspaceItem;

// 通知ユーティリティ / Notification utilities.
namespace KawaiiPhysicsEdWindowUtils
{
	/** Slate通知を表示する / Shows a Slate notification. */
	void ShowNotification(const FText& NotificationText,
	                      SNotificationItem::ECompletionState CompletionState,
	                      float ExpireDuration = 5.0f,
	                      const FSimpleDelegate& Hyperlink = FSimpleDelegate(),
	                      const FText& HyperlinkText = FText::GetEmpty());

	/** AnimBlueprint のアセットエディタを（必要なら開いて）取得し、指定タブを呼び出す。失敗時は通知を表示して nullptr を返す / Finds (or opens) the asset editor for the AnimBlueprint and invokes the given tab. Shows a notification and returns nullptr on failure. */
	TSharedPtr<SDockTab> InvokeAnimBlueprintEditorTab(const FSoftObjectPath& AnimBlueprintPath, FName TabId, const FText& ResolveFailedMessage);

	/** 親 workspace item 配下の Kawaii Physics メニューグループを検索し、無ければ作成して返す / Finds the Kawaii Physics menu group under the parent workspace item, creating it if missing. */
	TSharedRef<FWorkspaceItem> FindOrAddKawaiiPhysicsMenuGroup(const TSharedRef<FWorkspaceItem>& Parent);

	/** グループ配下から指定 TabId の古い spawner 子要素を除去する / Removes stale spawner children with the given TabId from the group. */
	void RemoveStaleSpawnerChildren(const TSharedRef<FWorkspaceItem>& Group, FName TabId);
}
