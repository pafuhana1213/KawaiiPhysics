// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEdWindowUtils.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/SWindow.h"

namespace
{
	FString MakeVectorConfigString(const FVector2D& Value)
	{
		return FString::Printf(TEXT("%.0f,%.0f"), Value.X, Value.Y);
	}

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
		if (Hyperlink.IsBound())
		{
			NotificationInfo.Hyperlink = Hyperlink;
			NotificationInfo.HyperlinkText = HyperlinkText;
		}

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
		if (!GConfig)
		{
			return;
		}

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
