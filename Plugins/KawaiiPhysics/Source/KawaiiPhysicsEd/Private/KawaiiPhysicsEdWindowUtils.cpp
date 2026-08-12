// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEdWindowUtils.h"

#include "Framework/Notifications/NotificationManager.h"

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
}
