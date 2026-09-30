// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsWindPresetDataAsset.h"
#include "KawaiiPhysicsWindPresetTags.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysics_WindPreset_TestUnregistered,
                                   "KawaiiPhysics.WindPreset.TestUnregistered");

namespace
{
// 既存の手続き風テストと同じく、浮動小数点の丸め程度を既定許容誤差にする
constexpr float GWindPresetTol = KINDA_SMALL_NUMBER;

struct FWindPresetParamMapping
{
	const TCHAR* Name;
	bool FKawaiiProceduralWindDynamicParams::* DynamicOverride;
	float FKawaiiProceduralWindDynamicParams::* DynamicValue;
	float FKawaiiProceduralWindPreset::* PresetValue;
	float FKawaiiPhysics_ExternalForce_ProceduralWind::* WindValue;
};

const FWindPresetParamMapping GWindPresetParamMappings[] = {
	{
		TEXT("ConstantForce"),
		&FKawaiiProceduralWindDynamicParams::bOverrideConstantForce,
		&FKawaiiProceduralWindDynamicParams::ConstantForce,
		&FKawaiiProceduralWindPreset::ConstantForce,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::ConstantForce
	},
	{
		TEXT("SwayForce"),
		&FKawaiiProceduralWindDynamicParams::bOverrideSwayForce,
		&FKawaiiProceduralWindDynamicParams::SwayForce,
		&FKawaiiProceduralWindPreset::SwayForce,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::SwayForce
	},
	{
		TEXT("SwayPeriod"),
		&FKawaiiProceduralWindDynamicParams::bOverrideSwayPeriod,
		&FKawaiiProceduralWindDynamicParams::SwayPeriod,
		&FKawaiiProceduralWindPreset::SwayPeriod,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::SwayPeriod
	},
	{
		TEXT("RippleForce"),
		&FKawaiiProceduralWindDynamicParams::bOverrideRippleForce,
		&FKawaiiProceduralWindDynamicParams::RippleForce,
		&FKawaiiProceduralWindPreset::RippleForce,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::RippleForce
	},
	{
		TEXT("RipplePeriod"),
		&FKawaiiProceduralWindDynamicParams::bOverrideRipplePeriod,
		&FKawaiiProceduralWindDynamicParams::RipplePeriod,
		&FKawaiiProceduralWindPreset::RipplePeriod,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::RipplePeriod
	},
	{
		TEXT("RippleTipPhaseDelay"),
		&FKawaiiProceduralWindDynamicParams::bOverrideRippleTipPhaseDelay,
		&FKawaiiProceduralWindDynamicParams::RippleTipPhaseDelay,
		&FKawaiiProceduralWindPreset::RippleTipPhaseDelay,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::RippleTipPhaseDelay
	},
	{
		TEXT("StrengthCyclePeriod"),
		&FKawaiiProceduralWindDynamicParams::bOverrideStrengthCyclePeriod,
		&FKawaiiProceduralWindDynamicParams::StrengthCyclePeriod,
		&FKawaiiProceduralWindPreset::StrengthCyclePeriod,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::StrengthCyclePeriod
	},
	{
		TEXT("RandomForce"),
		&FKawaiiProceduralWindDynamicParams::bOverrideRandomForce,
		&FKawaiiProceduralWindDynamicParams::RandomForce,
		&FKawaiiProceduralWindPreset::RandomForce,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::RandomForce
	},
	{
		TEXT("RandomForcePeriod"),
		&FKawaiiProceduralWindDynamicParams::bOverrideRandomForcePeriod,
		&FKawaiiProceduralWindDynamicParams::RandomForcePeriod,
		&FKawaiiProceduralWindPreset::RandomForcePeriod,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::RandomForcePeriod
	},
	{
		TEXT("WindDirectionNoiseAngle"),
		&FKawaiiProceduralWindDynamicParams::bOverrideWindDirectionNoiseAngle,
		&FKawaiiProceduralWindDynamicParams::WindDirectionNoiseAngle,
		&FKawaiiProceduralWindPreset::WindDirectionNoiseAngle,
		&FKawaiiPhysics_ExternalForce_ProceduralWind::WindDirectionNoiseAngle
	}
};

bool TestWindPresetFloatNear(FAutomationTestBase& Test, const FString& Name, const float Actual, const float Expected,
                   const float Tol = GWindPresetTol)
{
	return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), *Name, Actual, Expected),
	                     FMath::IsNearlyEqual(Actual, Expected, Tol));
}

bool TestIntervalNear(FAutomationTestBase& Test, const FString& Name, const FFloatInterval& Actual, const FFloatInterval& Expected)
{
	bool bOk = true;
	bOk &= TestWindPresetFloatNear(Test, FString::Printf(TEXT("%s Min"), *Name), Actual.Min, Expected.Min);
	bOk &= TestWindPresetFloatNear(Test, FString::Printf(TEXT("%s Max"), *Name), Actual.Max, Expected.Max);
	return bOk;
}

bool TestDynamicParamsMatchPreset(FAutomationTestBase& Test, const FString& Prefix,
                                  const FKawaiiProceduralWindDynamicParams& Params,
                                  const FKawaiiProceduralWindPreset& Preset)
{
	bool bOk = true;
	for (const FWindPresetParamMapping& Mapping : GWindPresetParamMappings)
	{
		bOk &= Test.TestTrue(FString::Printf(TEXT("%s %s override"), *Prefix, Mapping.Name),
		                     Params.*(Mapping.DynamicOverride));
		bOk &= TestWindPresetFloatNear(Test,
		                      FString::Printf(TEXT("%s %s value"), *Prefix, Mapping.Name),
		                      Params.*(Mapping.DynamicValue),
		                      Preset.*(Mapping.PresetValue));
	}
	bOk &= Test.TestTrue(FString::Printf(TEXT("%s StrengthCycleRange override"), *Prefix),
	                     Params.bOverrideStrengthCycleRange);
	bOk &= TestIntervalNear(Test,
	                        FString::Printf(TEXT("%s StrengthCycleRange value"), *Prefix),
	                        Params.StrengthCycleRange,
	                        Preset.StrengthCycleRange);
	return bOk;
}

bool TestWindValuesMatchPreset(FAutomationTestBase& Test, const FString& Prefix,
                               const FKawaiiPhysics_ExternalForce_ProceduralWind& Wind,
                               const FKawaiiProceduralWindPreset& Preset)
{
	bool bOk = true;
	for (const FWindPresetParamMapping& Mapping : GWindPresetParamMappings)
	{
		bOk &= TestWindPresetFloatNear(Test,
		                      FString::Printf(TEXT("%s %s"), *Prefix, Mapping.Name),
		                      Wind.*(Mapping.WindValue),
		                      Preset.*(Mapping.PresetValue));
	}
	bOk &= TestIntervalNear(Test,
	                        FString::Printf(TEXT("%s StrengthCycleRange"), *Prefix),
	                        Wind.StrengthCycleRange,
	                        Preset.StrengthCycleRange);
	return bOk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetApplyRoundTripTest,
                                 "KawaiiPhysics.WindPreset.ApplyRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetApplyRoundTripTest::RunTest(const FString& Parameters)
{
	// プリセットから動的パラメータへの変換と適用、上書きゲートを確認する。
	const TArray<FKawaiiProceduralWindPreset> Defaults = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();
	bool bOk = TestEqual(TEXT("Default preset count"), Defaults.Num(), 3);
	if (!bOk)
	{
		return false;
	}

	const FKawaiiProceduralWindPreset& Preset = Defaults[1];
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	const float BeforeTimeScale = Wind.TimeScale;
	const FVector BeforeWindDirection = Wind.WindDirection;
	const float BeforeRipplePhaseOffset = Wind.RipplePhaseOffset;
	const float BeforeStrengthCyclePhaseOffset = Wind.StrengthCyclePhaseOffset;
	const float BeforeWindDirectionNoisePeriod = Wind.WindDirectionNoisePeriod;

	const FKawaiiProceduralWindDynamicParams Params = Preset.ToDynamicParams();
	bOk &= TestDynamicParamsMatchPreset(*this, TEXT("Strong params"), Params, Preset);
	bOk &= TestFalse(TEXT("WindDirection override remains false"), Params.bOverrideWindDirection);
	bOk &= TestFalse(TEXT("RipplePhaseOffset override remains false"), Params.bOverrideRipplePhaseOffset);
	bOk &= TestFalse(TEXT("StrengthCyclePhaseOffset override remains false"), Params.bOverrideStrengthCyclePhaseOffset);
	bOk &= TestFalse(TEXT("WindDirectionNoisePeriod override remains false"), Params.bOverrideWindDirectionNoisePeriod);
	bOk &= TestFalse(TEXT("TimeScale override remains false"), Params.bOverrideTimeScale);
	bOk &= TestFalse(TEXT("IsEnabled override remains false"), Params.bOverrideIsEnabled);
	Wind.bIsEnabled = false;
	Wind.ApplyDynamicParams(Params);
	bOk &= TestFalse(TEXT("bIsEnabled unchanged without override"), Wind.bIsEnabled);
	FKawaiiProceduralWindDynamicParams EnabledParams;
	EnabledParams.bIsEnabled = true;
	EnabledParams.bOverrideIsEnabled = true;
	Wind.ApplyDynamicParams(EnabledParams);
	bOk &= TestTrue(TEXT("bIsEnabled changes with override"), Wind.bIsEnabled);

	bOk &= TestWindValuesMatchPreset(*this, TEXT("Strong applied"), Wind, Preset);
	bOk &= TestWindPresetFloatNear(*this, TEXT("TimeScale unchanged"), Wind.TimeScale, BeforeTimeScale);
	bOk &= TestTrue(TEXT("WindDirection unchanged"), Wind.WindDirection.Equals(BeforeWindDirection));
	bOk &= TestWindPresetFloatNear(*this, TEXT("RipplePhaseOffset unchanged"), Wind.RipplePhaseOffset, BeforeRipplePhaseOffset);
	bOk &= TestWindPresetFloatNear(*this, TEXT("StrengthCyclePhaseOffset unchanged"), Wind.StrengthCyclePhaseOffset, BeforeStrengthCyclePhaseOffset);
	bOk &= TestWindPresetFloatNear(*this, TEXT("WindDirectionNoisePeriod unchanged"),
	                     Wind.WindDirectionNoisePeriod,
	                     BeforeWindDirectionNoisePeriod);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetResolvePresetParamsByTagTest,
                                 "KawaiiPhysics.WindPreset.ResolvePresetParamsByTag",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetResolvePresetParamsByTagTest::RunTest(const FString& Parameters)
{
	// アセットの有無、タグ不一致、重複タグで解決結果と先勝ちの値を確認する。
	UKawaiiPhysicsWindPresetDataAsset* EmptyAsset =
		NewObject<UKawaiiPhysicsWindPresetDataAsset>(GetTransientPackage());
	EmptyAsset->Presets.Empty();
	UKawaiiPhysicsWindPresetDataAsset* ConfiguredAsset =
		NewObject<UKawaiiPhysicsWindPresetDataAsset>(GetTransientPackage());
	FKawaiiProceduralWindPreset CustomPreset;
	CustomPreset.PresetTag = TAG_KawaiiPhysics_WindPreset_TestUnregistered;
	CustomPreset.ConstantForce = 123.0f;
	ConfiguredAsset->Presets.Add(CustomPreset);
	UKawaiiPhysicsWindPresetDataAsset* DuplicateAsset =
		NewObject<UKawaiiPhysicsWindPresetDataAsset>(GetTransientPackage());
	DuplicateAsset->Presets = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();
	if (!TestTrue(TEXT("Default presets include Strong"), DuplicateAsset->Presets.Num() > 1))
	{
		return false;
	}
	FKawaiiProceduralWindPreset DuplicateStrong = DuplicateAsset->Presets[1];
	DuplicateStrong.ConstantForce = 123.0f;
	DuplicateAsset->Presets.Add(DuplicateStrong);

	struct FResolveCase
	{
		const TCHAR* Name;
		UKawaiiPhysicsWindPresetDataAsset* Asset;
		FGameplayTag Tag;
		bool bExpected;
		float ExpectedConstantForce;
	};
	const FResolveCase Cases[] = {
		{TEXT("Null Storm"), nullptr, TAG_KawaiiPhysics_WindPreset_Storm, true, 15.0f},
		{TEXT("Empty Storm"), EmptyAsset, TAG_KawaiiPhysics_WindPreset_Storm, true, 15.0f},
		{TEXT("Empty tag"), nullptr, FGameplayTag(), false, 0.0f},
		{TEXT("Unregistered tag"), nullptr, TAG_KawaiiPhysics_WindPreset_TestUnregistered, false, 0.0f},
		{TEXT("Configured miss"), ConfiguredAsset, TAG_KawaiiPhysics_WindPreset_Storm, false, 0.0f},
		{TEXT("Duplicate Strong"), DuplicateAsset, TAG_KawaiiPhysics_WindPreset_Strong, true, 8.0f}
	};
	bool bOk = true;
	for (const FResolveCase& Case : Cases)
	{
		FKawaiiProceduralWindDynamicParams Params;
		const bool bResolved = UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(
			Case.Asset, Case.Tag, Params);
		bOk &= TestEqual(FString::Printf(TEXT("%s resolves"), Case.Name), bResolved, Case.bExpected);
		if (bResolved)
		{
			bOk &= TestWindPresetFloatNear(*this,
				FString::Printf(TEXT("%s ConstantForce"), Case.Name), Params.ConstantForce, Case.ExpectedConstantForce);
		}
	}
	const FKawaiiProceduralWindPreset* FirstStrong =
		DuplicateAsset->FindPresetByTag(TAG_KawaiiPhysics_WindPreset_Strong);
	bOk &= TestTrue(TEXT("Duplicate tag returns first preset"),
		FirstStrong == &DuplicateAsset->Presets[1]);
	bOk &= TestTrue(TEXT("Empty tag returns null"), DuplicateAsset->FindPresetByTag(FGameplayTag()) == nullptr);
	bOk &= TestTrue(TEXT("Unregistered preset tag returns null"),
		DuplicateAsset->FindPresetByTag(TAG_KawaiiPhysics_WindPreset_TestUnregistered) == nullptr);
	return bOk;
}

#endif
