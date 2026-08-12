// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEdStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Math/Vector2D.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FKawaiiPhysicsEdStyle::StyleSet;

void FKawaiiPhysicsEdStyle::Initialize()
{
	const FName StyleSetName = GetStyleSetName();
	if (FSlateStyleRegistry::FindSlateStyle(StyleSetName))
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(StyleSetName);
	}

	if (StyleSet.IsValid())
	{
		StyleSet.Reset();
	}

	const TSharedPtr<IPlugin> KawaiiPhysicsPlugin = IPluginManager::Get().FindPlugin(TEXT("KawaiiPhysics"));
	if (!KawaiiPhysicsPlugin.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(StyleSetName);
	StyleSet->SetContentRoot(FPaths::Combine(KawaiiPhysicsPlugin->GetBaseDir(), TEXT("Resources")));
	StyleSet->Set(
		TEXT("KawaiiPhysics.TabIcon"),
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("Icon128"), TEXT(".png")), FVector2D(16.0f, 16.0f)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FKawaiiPhysicsEdStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

const ISlateStyle& FKawaiiPhysicsEdStyle::Get()
{
	check(StyleSet.IsValid());
	return *StyleSet.Get();
}

FName FKawaiiPhysicsEdStyle::GetStyleSetName()
{
	static const FName StyleSetName(TEXT("KawaiiPhysicsEdStyle"));
	return StyleSetName;
}
