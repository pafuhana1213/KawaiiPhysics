// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Notifications/SNotificationList.h"

class SWindow;

namespace KawaiiPhysicsEdWindowUtils
{
	/** Slate通知を表示する / Shows a Slate notification. */
	void ShowNotification(const FText& NotificationText,
	                      SNotificationItem::ECompletionState CompletionState,
	                      float ExpireDuration = 5.0f,
	                      const FSimpleDelegate& Hyperlink = FSimpleDelegate(),
	                      const FText& HyperlinkText = FText::GetEmpty());

	/** ウィンドウ位置とサイズを設定ファイルへ保存する / Persists window position and size to config. */
	void PersistWindowPlacement(const TSharedRef<SWindow>& Window,
	                            const TCHAR* ConfigSectionName,
	                            const TCHAR* WindowPosConfigKey,
	                            const TCHAR* WindowSizeConfigKey);

	/** 設定ファイルからウィンドウ位置とサイズを復元する / Restores window position and size from config. */
	void RestoreWindowPlacement(const TSharedRef<SWindow>& Window,
	                            const TCHAR* ConfigSectionName,
	                            const TCHAR* WindowPosConfigKey,
	                            const TCHAR* WindowSizeConfigKey);
}
