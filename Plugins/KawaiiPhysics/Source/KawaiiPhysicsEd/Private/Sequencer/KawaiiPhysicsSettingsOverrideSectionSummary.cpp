// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Sequencer/KawaiiPhysicsSettingsOverrideSectionSummary.h"

#include "Internationalization/Text.h"
#include "Math/UnrealMathUtility.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsSettingsOverrideSectionSummary"

namespace
{
	bool ShouldShowScaleValue(const float Value)
	{
		return !FMath::IsNearlyEqual(Value, 1.0f, KINDA_SMALL_NUMBER);
	}

	FText MakeScaleEntryText(const TCHAR* Abbreviation, const float Value, const FNumberFormattingOptions& FormatOptions)
	{
		return FText::Format(
			LOCTEXT("ScaleEntryFormat", "{0}×{1}"),
			FText::FromString(FString(Abbreviation)),
			FText::AsNumber(Value, &FormatOptions));
	}

	void AddScaleEntry(
		TArray<FText>& Entries,
		const TCHAR* Abbreviation,
		const float Value,
		const FNumberFormattingOptions& FormatOptions)
	{
		if (ShouldShowScaleValue(Value))
		{
			Entries.Add(MakeScaleEntryText(Abbreviation, Value, FormatOptions));
		}
	}
}

FText MakeKawaiiPhysicsScaleSummaryText(const FKawaiiPhysicsSettingsScale& Scale)
{
	FNumberFormattingOptions FormatOptions;
	FormatOptions.SetUseGrouping(false);
	FormatOptions.SetMinimumFractionalDigits(2);
	FormatOptions.SetMaximumFractionalDigits(2);

	TArray<FText> Entries;
	AddScaleEntry(Entries, TEXT("D"), Scale.Damping, FormatOptions);
	AddScaleEntry(Entries, TEXT("S"), Scale.Stiffness, FormatOptions);
	AddScaleEntry(Entries, TEXT("WL"), Scale.WorldDampingLocation, FormatOptions);
	AddScaleEntry(Entries, TEXT("WR"), Scale.WorldDampingRotation, FormatOptions);
	AddScaleEntry(Entries, TEXT("R"), Scale.Radius, FormatOptions);
	AddScaleEntry(Entries, TEXT("LA"), Scale.LimitAngle, FormatOptions);

	if (Entries.IsEmpty())
	{
		return LOCTEXT("NoScaleChange", "×1.0 (no change)");
	}

	return FText::Join(FText::FromString(TEXT("  ")), Entries);
}

#undef LOCTEXT_NAMESPACE
