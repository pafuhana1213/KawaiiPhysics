// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Sequencer/KawaiiPhysicsSettingsMultiplierSectionSummary.h"

#include "Internationalization/Text.h"
#include "Math/UnrealMathUtility.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsSettingsMultiplierSectionSummary"

namespace
{
	bool ShouldShowScaleValue(const float Value)
	{
		return !FMath::IsNearlyEqual(Value, 1.0f, KINDA_SMALL_NUMBER);
	}

	void AddScaleEntry(TArray<FString>& Entries, const TCHAR* Abbreviation, const float Value)
	{
		if (ShouldShowScaleValue(Value))
		{
			// カルチャ依存の FText::AsNumber を使わず Printf で組み立てることで小数点表記を常に '.' に固定する
			Entries.Add(FString::Printf(TEXT("%s×%.2f"), Abbreviation, Value));
		}
	}
}

FString MakeKawaiiPhysicsScaleSummaryString(const FKawaiiPhysicsSettingsMultiplier& Scale)
{
	TArray<FString> Entries;
	AddScaleEntry(Entries, TEXT("D"), Scale.Damping);
	AddScaleEntry(Entries, TEXT("S"), Scale.Stiffness);
	AddScaleEntry(Entries, TEXT("WL"), Scale.WorldDampingLocation);
	AddScaleEntry(Entries, TEXT("WR"), Scale.WorldDampingRotation);
	AddScaleEntry(Entries, TEXT("R"), Scale.Radius);
	AddScaleEntry(Entries, TEXT("LA"), Scale.LimitAngle);

	return FString::Join(Entries, TEXT("  "));
}

FText MakeKawaiiPhysicsScaleSummaryText(const FKawaiiPhysicsSettingsMultiplier& Scale)
{
	const FString Summary = MakeKawaiiPhysicsScaleSummaryString(Scale);
	if (Summary.IsEmpty())
	{
		return LOCTEXT("NoScaleChange", "×1.0 (no change)");
	}

	return FText::FromString(Summary);
}

#undef LOCTEXT_NAMESPACE
