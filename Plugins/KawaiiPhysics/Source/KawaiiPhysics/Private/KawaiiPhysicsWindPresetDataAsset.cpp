// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsWindPresetDataAsset.h"

#include "KawaiiPhysicsWindPresetTags.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "KawaiiPhysicsWindPresetDataAsset"

FKawaiiProceduralWindDynamicParams FKawaiiProceduralWindPreset::ToDynamicParams() const
{
	FKawaiiProceduralWindDynamicParams Params;

	Params.bOverrideConstantForce = true;
	Params.ConstantForce = ConstantForce;
	Params.bOverrideSwayForce = true;
	Params.SwayForce = SwayForce;
	Params.bOverrideSwayPeriod = true;
	Params.SwayPeriod = SwayPeriod;
	Params.bOverrideRippleForce = true;
	Params.RippleForce = RippleForce;
	Params.bOverrideRipplePeriod = true;
	Params.RipplePeriod = RipplePeriod;
	Params.bOverrideRippleTipPhaseDelay = true;
	Params.RippleTipPhaseDelay = RippleTipPhaseDelay;
	Params.bOverrideStrengthCycleRange = true;
	Params.StrengthCycleRange = StrengthCycleRange;
	Params.bOverrideStrengthCyclePeriod = true;
	Params.StrengthCyclePeriod = StrengthCyclePeriod;
	Params.bOverrideRandomForce = true;
	Params.RandomForce = RandomForce;
	Params.bOverrideRandomForcePeriod = true;
	Params.RandomForcePeriod = RandomForcePeriod;
	Params.bOverrideWindDirectionNoiseAngle = true;
	Params.WindDirectionNoiseAngle = WindDirectionNoiseAngle;

	return Params;
}

const FKawaiiProceduralWindPreset* UKawaiiPhysicsWindPresetDataAsset::FindPresetByTag(FGameplayTag PresetTag) const
{
	if (!PresetTag.IsValid())
	{
		return nullptr;
	}

	for (const FKawaiiProceduralWindPreset& Preset : Presets)
	{
		if (Preset.PresetTag.IsValid() && Preset.PresetTag.MatchesTagExact(PresetTag))
		{
			return &Preset;
		}
	}

	return nullptr;
}

bool UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(
	const UKawaiiPhysicsWindPresetDataAsset* PresetDataAsset,
	FGameplayTag PresetTag,
	FKawaiiProceduralWindDynamicParams& OutParams)
{
	// 無効タグは DataAsset と組み込み既定のどちらも照合せず失敗扱いにする。
	if (!PresetTag.IsValid())
	{
		return false;
	}

	// 明示的にプリセットを持つ DataAsset は、その DataAsset 内だけを照合対象にする。
	// 見つからない場合も組み込み既定へはフォールバックしない。
	if (PresetDataAsset && PresetDataAsset->Presets.Num() > 0)
	{
		if (const FKawaiiProceduralWindPreset* Preset = PresetDataAsset->FindPresetByTag(PresetTag))
		{
			OutParams = Preset->ToDynamicParams();
			return true;
		}

		return false;
	}

	// DataAsset が未指定、または空配列の場合のみ組み込み既定の3件から照合する。
	const TArray<FKawaiiProceduralWindPreset> DefaultPresets = GetDefaultPresets();
	for (const FKawaiiProceduralWindPreset& Preset : DefaultPresets)
	{
		if (Preset.PresetTag.IsValid() && Preset.PresetTag.MatchesTagExact(PresetTag))
		{
			OutParams = Preset.ToDynamicParams();
			return true;
		}
	}

	return false;
}

namespace
{
	FKawaiiProceduralWindPreset MakeWindPreset(
		const FGameplayTag& PresetTag,
		const FText& PresetName,
		float ConstantForce,
		float SwayForce,
		float SwayPeriod,
		float RippleForce,
		float RipplePeriod,
		float RippleTipPhaseDelay,
		const FFloatInterval& StrengthCycleRange,
		float StrengthCyclePeriod,
		float RandomForce,
		float RandomForcePeriod,
		float WindDirectionNoiseAngle)
	{
		FKawaiiProceduralWindPreset Preset;
		Preset.PresetTag = PresetTag;
		Preset.PresetName = PresetName;
		Preset.ConstantForce = ConstantForce;
		Preset.SwayForce = SwayForce;
		Preset.SwayPeriod = SwayPeriod;
		Preset.RippleForce = RippleForce;
		Preset.RipplePeriod = RipplePeriod;
		Preset.RippleTipPhaseDelay = RippleTipPhaseDelay;
		Preset.StrengthCycleRange = StrengthCycleRange;
		Preset.StrengthCyclePeriod = StrengthCyclePeriod;
		Preset.RandomForce = RandomForce;
		Preset.RandomForcePeriod = RandomForcePeriod;
		Preset.WindDirectionNoiseAngle = WindDirectionNoiseAngle;
		return Preset;
	}

#if WITH_EDITOR
	void ForEachWindPresetValidationWarning(
		const TArray<FKawaiiProceduralWindPreset>& Presets,
		const TFunctionRef<void(const FText&)>& AddWarning)
	{
		TSet<FGameplayTag> SeenTags;
		for (int32 Index = 0; Index < Presets.Num(); ++Index)
		{
			const FKawaiiProceduralWindPreset& Preset = Presets[Index];
			if (!Preset.PresetTag.IsValid())
			{
				AddWarning(FText::Format(
					LOCTEXT("InvalidPresetTagWarning", "Wind preset at index {0} has an invalid PresetTag."),
					FText::AsNumber(Index)));
				continue;
			}

			if (SeenTags.Contains(Preset.PresetTag))
			{
				AddWarning(FText::Format(
					LOCTEXT("DuplicatePresetTagWarning", "Wind preset at index {0} has a duplicate PresetTag: {1}."),
					FText::AsNumber(Index),
					FText::FromName(Preset.PresetTag.GetTagName())));
				continue;
			}

			SeenTags.Add(Preset.PresetTag);
		}
	}
#endif
}

#if WITH_EDITOR
#if UE_VERSION_OLDER_THAN(5, 3, 0)
EDataValidationResult UKawaiiPhysicsWindPresetDataAsset::IsDataValid(TArray<FText>& ValidationErrors)
{
	ForEachWindPresetValidationWarning(Presets, [&ValidationErrors](const FText& Warning)
	{
		ValidationErrors.Add(Warning);
	});

	return EDataValidationResult::Valid;
}
#else
EDataValidationResult UKawaiiPhysicsWindPresetDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	ForEachWindPresetValidationWarning(Presets, [&Context](const FText& Warning)
	{
		Context.AddWarning(Warning);
	});

	return EDataValidationResult::Valid;
}
#endif
#endif

TArray<FKawaiiProceduralWindPreset> UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets()
{
	TArray<FKawaiiProceduralWindPreset> DefaultPresets;
	DefaultPresets.Reserve(3);

	DefaultPresets.Add(MakeWindPreset(
		TAG_KawaiiPhysics_WindPreset_Breeze,
		NSLOCTEXT("KawaiiPhysicsWindPresetDataAsset", "BreezePresetName", "Breeze"),
		2.0f,
		1.0f,
		3.0f,
		1.0f,
		1.2f,
		120.0f,
		FFloatInterval(0.6f, 1.0f),
		20.0f,
		0.5f,
		0.8f,
		5.0f));

	DefaultPresets.Add(MakeWindPreset(
		TAG_KawaiiPhysics_WindPreset_Strong,
		NSLOCTEXT("KawaiiPhysicsWindPresetDataAsset", "StrongPresetName", "Strong"),
		8.0f,
		4.0f,
		1.6f,
		3.0f,
		0.5f,
		120.0f,
		FFloatInterval(0.7f, 1.3f),
		12.0f,
		2.0f,
		0.5f,
		10.0f));

	DefaultPresets.Add(MakeWindPreset(
		TAG_KawaiiPhysics_WindPreset_Storm,
		NSLOCTEXT("KawaiiPhysicsWindPresetDataAsset", "StormPresetName", "Storm"),
		15.0f,
		10.0f,
		0.9f,
		8.0f,
		0.3f,
		180.0f,
		FFloatInterval(0.5f, 1.6f),
		7.0f,
		6.0f,
		0.3f,
		20.0f));

	return DefaultPresets;
}

#undef LOCTEXT_NAMESPACE
