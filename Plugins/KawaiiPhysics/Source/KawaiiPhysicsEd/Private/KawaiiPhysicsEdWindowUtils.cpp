// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEdWindowUtils.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/SWindow.h"

namespace
{
	// FVector2D を ini 保存用の "X,Y" 文字列へ変換する
	FString MakeVectorConfigString(const FVector2D& Value)
	{
		return FString::Printf(TEXT("%.0f,%.0f"), Value.X, Value.Y);
	}

	// MakeVectorConfigString で保存した "X,Y" 文字列を FVector2D へ復元する
	bool TryParseVectorConfigString(const FString& StringValue, FVector2D& OutValue)
	{
		FString Left;
		FString Right;
		if (!StringValue.Split(TEXT(","), &Left, &Right))
		{
			return false;
		}

		OutValue.X = FCString::Atof(*Left);
		OutValue.Y = FCString::Atof(*Right);
		return true;
	}
}

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

	void PersistWindowPlacement(const TSharedRef<SWindow>& Window,
	                            const TCHAR* ConfigSectionName,
	                            const TCHAR* WindowPosConfigKey,
	                            const TCHAR* WindowSizeConfigKey)
	{
		// commandlet等 GConfig 未初期化の環境では保存をスキップ
		if (!GConfig)
		{
			return;
		}

		const FVector2D Position = Window->GetPositionInScreen();
		const FVector2D Size = Window->GetSizeInScreen();
		GConfig->SetString(ConfigSectionName, WindowPosConfigKey, *MakeVectorConfigString(Position), GEditorPerProjectIni);
		GConfig->SetString(ConfigSectionName, WindowSizeConfigKey, *MakeVectorConfigString(Size), GEditorPerProjectIni);
		GConfig->Flush(false, GEditorPerProjectIni);
	}

	void RestoreWindowPlacement(const TSharedRef<SWindow>& Window,
	                            const TCHAR* ConfigSectionName,
	                            const TCHAR* WindowPosConfigKey,
	                            const TCHAR* WindowSizeConfigKey)
	{
		// PersistWindowPlacementと同様、GConfig未初期化ならスキップ
		if (!GConfig)
		{
			return;
		}

		// 位置・サイズは互いに独立して、パースに成功した場合のみ反映する
		FString StringValue;
		FVector2D ParsedValue;
		if (GConfig->GetString(ConfigSectionName, WindowPosConfigKey, StringValue, GEditorPerProjectIni) &&
			TryParseVectorConfigString(StringValue, ParsedValue))
		{
			Window->MoveWindowTo(ParsedValue);
		}

		if (GConfig->GetString(ConfigSectionName, WindowSizeConfigKey, StringValue, GEditorPerProjectIni) &&
			TryParseVectorConfigString(StringValue, ParsedValue))
		{
			Window->Resize(ParsedValue);
		}
	}
}
