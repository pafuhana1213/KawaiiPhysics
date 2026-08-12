// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Notifications/SNotificationList.h"

// 通知ユーティリティ / Notification utilities.
namespace KawaiiPhysicsEdWindowUtils
{
	/** Slate通知を表示する / Shows a Slate notification. */
	void ShowNotification(const FText& NotificationText,
	                      SNotificationItem::ECompletionState CompletionState,
	                      float ExpireDuration = 5.0f,
	                      const FSimpleDelegate& Hyperlink = FSimpleDelegate(),
	                      const FText& HyperlinkText = FText::GetEmpty());
}
