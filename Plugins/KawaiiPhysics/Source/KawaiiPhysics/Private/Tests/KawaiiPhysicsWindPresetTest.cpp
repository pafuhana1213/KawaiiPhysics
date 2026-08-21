// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsWindPresetDataAsset.h"
#include "KawaiiPhysicsWindPresetTags.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysics_WindPreset_TestUnregistered,
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

FKawaiiProceduralWindPreset MakeExpectedPreset(
	const FGameplayTag& PresetTag,
	const float ConstantForce,
	const float SwayForce,
	const float SwayPeriod,
	const float RippleForce,
	const float RipplePeriod,
	const float RippleTipPhaseDelay,
	const float StrengthCycleRangeMin,
	const float StrengthCycleRangeMax,
	const float StrengthCyclePeriod,
	const float RandomForce,
	const float RandomForcePeriod,
	const float WindDirectionNoiseAngle)
{
	FKawaiiProceduralWindPreset Preset;
	Preset.PresetTag = PresetTag;
	Preset.ConstantForce = ConstantForce;
	Preset.SwayForce = SwayForce;
	Preset.SwayPeriod = SwayPeriod;
	Preset.RippleForce = RippleForce;
	Preset.RipplePeriod = RipplePeriod;
	Preset.RippleTipPhaseDelay = RippleTipPhaseDelay;
	Preset.StrengthCycleRange = FFloatInterval(StrengthCycleRangeMin, StrengthCycleRangeMax);
	Preset.StrengthCyclePeriod = StrengthCyclePeriod;
	Preset.RandomForce = RandomForce;
	Preset.RandomForcePeriod = RandomForcePeriod;
	Preset.WindDirectionNoiseAngle = WindDirectionNoiseAngle;
	return Preset;
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

bool TestPresetValues(FAutomationTestBase& Test, const FString& Prefix,
                      const FKawaiiProceduralWindPreset& Actual,
                      const FKawaiiProceduralWindPreset& Expected)
{
	bool bOk = true;
	bOk &= Test.TestTrue(FString::Printf(TEXT("%s PresetTag"), *Prefix),
	                     Actual.PresetTag.MatchesTagExact(Expected.PresetTag));
	for (const FWindPresetParamMapping& Mapping : GWindPresetParamMappings)
	{
		bOk &= TestWindPresetFloatNear(Test,
		                      FString::Printf(TEXT("%s %s"), *Prefix, Mapping.Name),
		                      Actual.*(Mapping.PresetValue),
		                      Expected.*(Mapping.PresetValue));
	}
	bOk &= TestIntervalNear(Test,
	                        FString::Printf(TEXT("%s StrengthCycleRange"), *Prefix),
	                        Actual.StrengthCycleRange,
	                        Expected.StrengthCycleRange);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetToDynamicParamsTest,
                                 "KawaiiPhysics.WindPreset.ToDynamicParams",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetToDynamicParamsTest::RunTest(const FString& Parameters)
{
	FKawaiiProceduralWindPreset Preset;
	Preset.ConstantForce = 3.25f;
	Preset.SwayForce = 2.5f;
	Preset.SwayPeriod = 1.75f;
	Preset.RippleForce = 4.5f;
	Preset.RipplePeriod = 0.65f;
	Preset.RippleTipPhaseDelay = 135.0f;
	Preset.StrengthCycleRange = FFloatInterval(0.35f, 1.45f);
	Preset.StrengthCyclePeriod = 8.33f;
	Preset.RandomForce = 1.25f;
	Preset.RandomForcePeriod = 0.42f;
	Preset.WindDirectionNoiseAngle = 12.5f;

	const FKawaiiProceduralWindDynamicParams Params = Preset.ToDynamicParams();

	bool bOk = true;
	bOk &= TestDynamicParamsMatchPreset(*this, TEXT("ToDynamicParams"), Params, Preset);
	bOk &= TestFalse(TEXT("WindDirection override remains false"), Params.bOverrideWindDirection);
	bOk &= TestFalse(TEXT("RipplePhaseOffset override remains false"), Params.bOverrideRipplePhaseOffset);
	bOk &= TestFalse(TEXT("StrengthCyclePhaseOffset override remains false"), Params.bOverrideStrengthCyclePhaseOffset);
	bOk &= TestFalse(TEXT("WindDirectionNoisePeriod override remains false"), Params.bOverrideWindDirectionNoisePeriod);
	bOk &= TestFalse(TEXT("TimeScale override remains false"), Params.bOverrideTimeScale);
	bOk &= TestFalse(TEXT("IsEnabled override remains false"), Params.bOverrideIsEnabled);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetDefaultsTest,
                                 "KawaiiPhysics.WindPreset.Defaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetDefaultsTest::RunTest(const FString& Parameters)
{
	const TArray<FKawaiiProceduralWindPreset> Defaults = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();

	bool bOk = true;
	bOk &= TestEqual(TEXT("Default preset count"), Defaults.Num(), 3);
	if (!bOk)
	{
		return false;
	}

	const FKawaiiProceduralWindPreset ExpectedBreeze = MakeExpectedPreset(
		TAG_KawaiiPhysics_WindPreset_Breeze,
		2.0f, 1.0f, 3.0f, 1.0f, 1.2f, 120.0f, 0.6f, 1.0f, 20.0f, 0.5f, 0.8f, 5.0f);
	const FKawaiiProceduralWindPreset ExpectedStrong = MakeExpectedPreset(
		TAG_KawaiiPhysics_WindPreset_Strong,
		8.0f, 4.0f, 1.6f, 3.0f, 0.5f, 120.0f, 0.7f, 1.3f, 12.0f, 2.0f, 0.5f, 10.0f);
	const FKawaiiProceduralWindPreset ExpectedStorm = MakeExpectedPreset(
		TAG_KawaiiPhysics_WindPreset_Storm,
		15.0f, 10.0f, 0.9f, 8.0f, 0.3f, 180.0f, 0.5f, 1.6f, 7.0f, 6.0f, 0.3f, 20.0f);

	bOk &= TestPresetValues(*this, TEXT("Breeze"), Defaults[0], ExpectedBreeze);
	bOk &= TestPresetValues(*this, TEXT("Strong"), Defaults[1], ExpectedStrong);
	bOk &= TestPresetValues(*this, TEXT("Storm"), Defaults[2], ExpectedStorm);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetApplyRoundTripTest,
                                 "KawaiiPhysics.WindPreset.ApplyRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetApplyRoundTripTest::RunTest(const FString& Parameters)
{
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

	Wind.ApplyDynamicParams(Preset.ToDynamicParams());

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetApplyEnabledOverrideTest,
                                 "KawaiiPhysics.WindPreset.ApplyEnabledOverride",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetApplyEnabledOverrideTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.bIsEnabled = false;

	FKawaiiProceduralWindDynamicParams Params;
	Params.bIsEnabled = true;
	Wind.ApplyDynamicParams(Params);

	bool bOk = true;
	bOk &= TestFalse(TEXT("bIsEnabled unchanged without override"), Wind.bIsEnabled);

	Params.bOverrideIsEnabled = true;
	Wind.ApplyDynamicParams(Params);
	bOk &= TestTrue(TEXT("bIsEnabled changes with override"), Wind.bIsEnabled);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetRequestDynamicParamsStateTest,
                                 "KawaiiPhysics.WindPreset.RequestDynamicParamsState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetRequestDynamicParamsStateTest::RunTest(const FString& Parameters)
{
	const TArray<FKawaiiProceduralWindPreset> Defaults = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();
	bool bOk = TestEqual(TEXT("Default preset count"), Defaults.Num(), 3);
	if (!bOk)
	{
		return false;
	}

	const FKawaiiProceduralWindPreset& Preset = Defaults[2];
	FKawaiiProceduralWindDynamicParams Params = Preset.ToDynamicParams();
	Params.bOverrideIsEnabled = true;
	Params.bIsEnabled = true;
	Params.bOverrideTimeScale = true;
	Params.TimeScale = 1.0f;

	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.bIsEnabled = false;
	Wind.TimeScale = 2.0f;
	Wind.ResetRuntimeState();
	Wind.RuntimeState->Time = 5.0f;
	Wind.RuntimeState->ActiveGust.StartTime = 4.5f;
	Wind.RuntimeState->ActiveGust.Strength = 7.0f;
	Wind.RuntimeState->ActiveGust.RiseTime = 0.2f;
	Wind.RuntimeState->ActiveGust.DecayTime = 0.7f;
	Wind.RuntimeState->ActiveGust.bIsActive = true;

	Wind.RequestDynamicParams(Params);
	bOk &= TestTrue(TEXT("PendingParams queued"), Wind.RuntimeState->PendingParams.IsSet());
	Wind.ConsumePendingRequests();

	bOk &= TestWindValuesMatchPreset(*this, TEXT("Requested preset applied"), Wind, Preset);
	bOk &= TestTrue(TEXT("bIsEnabled applied"), Wind.bIsEnabled);
	bOk &= TestWindPresetFloatNear(*this, TEXT("TimeScale applied"), Wind.TimeScale, 1.0f);
	bOk &= TestWindPresetFloatNear(*this, TEXT("Time preserved"), Wind.RuntimeState->Time, 5.0f);
	bOk &= TestWindPresetFloatNear(*this, TEXT("ActiveGust StartTime preserved"), Wind.RuntimeState->ActiveGust.StartTime, 4.5f);
	bOk &= TestWindPresetFloatNear(*this, TEXT("ActiveGust Strength preserved"), Wind.RuntimeState->ActiveGust.Strength, 7.0f);
	bOk &= TestWindPresetFloatNear(*this, TEXT("ActiveGust RiseTime preserved"), Wind.RuntimeState->ActiveGust.RiseTime, 0.2f);
	bOk &= TestWindPresetFloatNear(*this, TEXT("ActiveGust DecayTime preserved"), Wind.RuntimeState->ActiveGust.DecayTime, 0.7f);
	bOk &= TestTrue(TEXT("ActiveGust active preserved"), Wind.RuntimeState->ActiveGust.bIsActive);
	bOk &= TestFalse(TEXT("PendingParams reset"), Wind.RuntimeState->PendingParams.IsSet());

	FKawaiiPhysics_ExternalForce_ProceduralWind LazyWind;
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LazyInitialRuntimeState =
		LazyWind.RuntimeState;
	bOk &= TestTrue(TEXT("RuntimeState starts valid"), LazyInitialRuntimeState.IsValid());
	LazyWind.RequestDynamicParams(Params);
	bOk &= TestTrue(TEXT("RequestDynamicParams preserves RuntimeState"),
	                LazyWind.RuntimeState.Get() == LazyInitialRuntimeState.Get());
	bOk &= TestTrue(TEXT("Lazy PendingParams queued"), LazyWind.RuntimeState->PendingParams.IsSet());
	LazyWind.ConsumePendingRequests();
	bOk &= TestWindValuesMatchPreset(*this, TEXT("Lazy requested preset applied"), LazyWind, Preset);
	bOk &= TestTrue(TEXT("Lazy bIsEnabled applied"), LazyWind.bIsEnabled);
	bOk &= TestWindPresetFloatNear(*this, TEXT("Lazy TimeScale applied"), LazyWind.TimeScale, 1.0f);
	bOk &= TestFalse(TEXT("Lazy PendingParams reset"), LazyWind.RuntimeState->PendingParams.IsSet());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetRequestGustTest,
                                 "KawaiiPhysics.WindPreset.RequestGust",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetRequestGustTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.ResetRuntimeState();
	Wind.RuntimeState->Time = 3.25f;

	Wind.RequestGust(6.0f, 0.1f, 0.5f);

	bool bOk = true;
	bOk &= TestTrue(TEXT("PendingGust queued"), Wind.RuntimeState->PendingGust.IsSet());
	bOk &= TestFalse(TEXT("PendingParams unchanged before consume"), Wind.RuntimeState->PendingParams.IsSet());
	Wind.ConsumePendingRequests();
	bOk &= TestWindPresetFloatNear(*this, TEXT("Time unchanged"), Wind.RuntimeState->Time, 3.25f);
	bOk &= TestWindPresetFloatNear(*this, TEXT("ActiveGust StartTime"), Wind.RuntimeState->ActiveGust.StartTime, 3.25f);
	bOk &= TestWindPresetFloatNear(*this, TEXT("ActiveGust Strength"), Wind.RuntimeState->ActiveGust.Strength, 6.0f);
	bOk &= TestWindPresetFloatNear(*this, TEXT("ActiveGust RiseTime"), Wind.RuntimeState->ActiveGust.RiseTime, 0.1f);
	bOk &= TestWindPresetFloatNear(*this, TEXT("ActiveGust DecayTime"), Wind.RuntimeState->ActiveGust.DecayTime, 0.5f);
	bOk &= TestTrue(TEXT("ActiveGust active"), Wind.RuntimeState->ActiveGust.bIsActive);
	bOk &= TestFalse(TEXT("PendingGust reset"), Wind.RuntimeState->PendingGust.IsSet());
	bOk &= TestFalse(TEXT("PendingParams unchanged after consume"), Wind.RuntimeState->PendingParams.IsSet());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetFindPresetByTagTest,
                                 "KawaiiPhysics.WindPreset.FindPresetByTag",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetFindPresetByTagTest::RunTest(const FString& Parameters)
{
	UKawaiiPhysicsWindPresetDataAsset* DataAsset =
		NewObject<UKawaiiPhysicsWindPresetDataAsset>(GetTransientPackage());
	DataAsset->Presets = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();

	bool bOk = TestEqual(TEXT("Default preset count"), DataAsset->Presets.Num(), 3);
	if (!bOk)
	{
		return false;
	}

	const FKawaiiProceduralWindPreset* StrongPreset =
		DataAsset->FindPresetByTag(TAG_KawaiiPhysics_WindPreset_Strong);
	bOk &= TestTrue(TEXT("Strong preset is found"), StrongPreset != nullptr);
	if (StrongPreset)
	{
		bOk &= TestTrue(TEXT("Strong preset tag matches"),
		                StrongPreset->PresetTag.MatchesTagExact(TAG_KawaiiPhysics_WindPreset_Strong));
		bOk &= TestWindPresetFloatNear(*this, TEXT("Strong preset value"), StrongPreset->ConstantForce, 8.0f);
	}

	bOk &= TestTrue(TEXT("Empty tag returns null"), DataAsset->FindPresetByTag(FGameplayTag()) == nullptr);
	bOk &= TestTrue(TEXT("Unregistered preset tag returns null"),
	                DataAsset->FindPresetByTag(TAG_KawaiiPhysics_WindPreset_TestUnregistered) == nullptr);

	FKawaiiProceduralWindPreset DuplicateStrong = DataAsset->Presets[1];
	DuplicateStrong.ConstantForce = 123.0f;
	DataAsset->Presets.Add(DuplicateStrong);

	const FKawaiiProceduralWindPreset* FirstStrongPreset =
		DataAsset->FindPresetByTag(TAG_KawaiiPhysics_WindPreset_Strong);
	bOk &= TestTrue(TEXT("Duplicate tag returns first preset"), FirstStrongPreset == &DataAsset->Presets[1]);
	if (FirstStrongPreset)
	{
		bOk &= TestWindPresetFloatNear(*this, TEXT("Duplicate tag keeps first value"), FirstStrongPreset->ConstantForce, 8.0f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetResolveFallbackNullAssetStormTest,
                                 "KawaiiPhysics.WindPreset.ResolveFallback.NullAssetStorm",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetResolveFallbackNullAssetStormTest::RunTest(const FString& Parameters)
{
	const TArray<FKawaiiProceduralWindPreset> Defaults = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();
	const FKawaiiProceduralWindPreset* StormPreset = Defaults.FindByPredicate([](const FKawaiiProceduralWindPreset& Preset)
	{
		return Preset.PresetTag.MatchesTagExact(TAG_KawaiiPhysics_WindPreset_Storm);
	});

	bool bOk = TestTrue(TEXT("Storm default preset exists"), StormPreset != nullptr);
	if (!StormPreset)
	{
		return false;
	}

	FKawaiiProceduralWindDynamicParams Params;
	bOk &= TestTrue(TEXT("Null asset resolves Storm default"),
	                UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(
		                nullptr,
		                TAG_KawaiiPhysics_WindPreset_Storm,
		                Params));
	bOk &= TestDynamicParamsMatchPreset(*this, TEXT("Storm fallback params"), Params, *StormPreset);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetResolveFallbackInvalidTagTest,
                                 "KawaiiPhysics.WindPreset.ResolveFallback.InvalidTag",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetResolveFallbackInvalidTagTest::RunTest(const FString& Parameters)
{
	FKawaiiProceduralWindDynamicParams Params;
	return TestFalse(TEXT("Null asset with empty tag does not resolve"),
	                 UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(
		                 nullptr,
		                 FGameplayTag(),
		                 Params));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetResolveFallbackNonDefaultTagTest,
                                 "KawaiiPhysics.WindPreset.ResolveFallback.NonDefaultTag",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetResolveFallbackNonDefaultTagTest::RunTest(const FString& Parameters)
{
	FKawaiiProceduralWindDynamicParams Params;
	return TestFalse(TEXT("Null asset with non-default tag does not resolve"),
	                 UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(
		                 nullptr,
		                 TAG_KawaiiPhysics_WindPreset_TestUnregistered,
		                 Params));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetResolveFallbackEmptyAssetStormTest,
                                 "KawaiiPhysics.WindPreset.ResolveFallback.EmptyAssetStorm",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetResolveFallbackEmptyAssetStormTest::RunTest(const FString& Parameters)
{
	const TArray<FKawaiiProceduralWindPreset> Defaults = UKawaiiPhysicsWindPresetDataAsset::GetDefaultPresets();
	const FKawaiiProceduralWindPreset* StormPreset = Defaults.FindByPredicate([](const FKawaiiProceduralWindPreset& Preset)
	{
		return Preset.PresetTag.MatchesTagExact(TAG_KawaiiPhysics_WindPreset_Storm);
	});

	bool bOk = TestTrue(TEXT("Storm default preset exists"), StormPreset != nullptr);
	if (!StormPreset)
	{
		return false;
	}

	UKawaiiPhysicsWindPresetDataAsset* DataAsset =
		NewObject<UKawaiiPhysicsWindPresetDataAsset>(GetTransientPackage());
	DataAsset->Presets.Empty();

	FKawaiiProceduralWindDynamicParams Params;
	bOk &= TestTrue(TEXT("Empty asset resolves Storm default"),
	                UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(
		                DataAsset,
		                TAG_KawaiiPhysics_WindPreset_Storm,
		                Params));
	bOk &= TestDynamicParamsMatchPreset(*this, TEXT("Empty asset Storm fallback params"), Params, *StormPreset);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindPresetResolveFallbackConfiguredAssetMissTest,
                                 "KawaiiPhysics.WindPreset.ResolveFallback.ConfiguredAssetMiss",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindPresetResolveFallbackConfiguredAssetMissTest::RunTest(const FString& Parameters)
{
	UKawaiiPhysicsWindPresetDataAsset* DataAsset =
		NewObject<UKawaiiPhysicsWindPresetDataAsset>(GetTransientPackage());

	FKawaiiProceduralWindPreset CustomPreset;
	CustomPreset.PresetTag = TAG_KawaiiPhysics_WindPreset_TestUnregistered;
	CustomPreset.ConstantForce = 123.0f;
	DataAsset->Presets.Add(CustomPreset);

	FKawaiiProceduralWindDynamicParams Params;
	return TestFalse(TEXT("Configured asset miss does not fall back to defaults"),
	                 UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(
		                 DataAsset,
		                 TAG_KawaiiPhysics_WindPreset_Storm,
		                 Params));
}

#endif
