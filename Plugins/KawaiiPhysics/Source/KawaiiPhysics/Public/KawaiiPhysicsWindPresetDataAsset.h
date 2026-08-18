// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Misc/EngineVersionComparison.h"
#include "KawaiiPhysicsWindPresetDataAsset.generated.h"

#if WITH_EDITOR
class FDataValidationContext;
#endif

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiProceduralWindPreset
{
	GENERATED_BODY()

	/**
	 * ランタイム API でプリセットを照合するためのキー
	 * Key used by runtime APIs to match a wind preset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Preset")
	FGameplayTag PresetTag;

	/** 表示名 / Display name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Preset")
	FText PresetName;

	/** 常に一定で掛かる風の強さ / Constant wind strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=6, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float ConstantForce = 2.0f;

	/** サイン波で周期的に強弱する成分の振幅 / Amplitude of the sine-based pulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=7, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float SwayForce = 1.0f;

	/** 一斉揺れ(Sway)の周期（秒） / Sway period in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=8, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault, Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float SwayPeriod = 2.0f;

	/** ボーン列に沿って伝わる波揺れ(Ripple)成分の振幅 / Amplitude of the traveling wave along the bone chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=9, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RippleForce = 1.0f;

	/** 波揺れ(Ripple)の周期（秒） / Ripple period in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=10, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault, Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RipplePeriod = 1.5f;

	/** 毛先（LengthRate=1）での位相遅れ量 / Phase delay at the tip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=12, Units="Degrees", PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RippleTipPhaseDelay = 90.0f;

	/** 風全体の強弱のうねりの倍率範囲 / Multiplier range of the slow global intensity swell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=14, ClampMin=0, UIMin=0, UIMax=3, PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	FFloatInterval StrengthCycleRange = FFloatInterval(0.6f, 1.0f);

	/** 強弱サイクル(StrengthCycle)周期（秒） / StrengthCycle period in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=15, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault, Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float StrengthCyclePeriod = 20.0f;

	/** 不規則な揺らぎ成分の強さ / Strength of the irregular fluctuation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=17, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RandomForce = 0.5f;

	/** 揺らぎが変化する速さ（秒） / How fast the fluctuation changes, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=18, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault, Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RandomForcePeriod = 0.8f;

	/** 風向き自体の揺らぎの円錐半角 / Cone half-angle of the wind direction wander. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=4, Units="Degrees", ClampMin=0, UIMin=0,
		PinHiddenByDefault), Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float WindDirectionNoiseAngle = 5.0f;

	/** 動的パラメータへ変換する / Convert to dynamic parameters. */
	FKawaiiProceduralWindDynamicParams ToDynamicParams() const;
};

/**
 * Wind Scope の風プリセットを保存する DataAsset。
 * DataAsset that stores Wind Scope wind presets.
 */
UCLASS(Blueprintable)
class KAWAIIPHYSICS_API UKawaiiPhysicsWindPresetDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	/** 説明 / Description. */
	UPROPERTY(EditAnywhere, Category = "Description", meta = (MultiLine = true))
	FText Description;
#endif

	/** Wind Scope に表示するプリセット一覧 / Presets shown in Wind Scope. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind Preset", meta=(TitleProperty="PresetName"))
	TArray<FKawaiiProceduralWindPreset> Presets;

	/** タグ完全一致で先頭のプリセットを探す / Find the first preset by exact tag match. */
	const FKawaiiProceduralWindPreset* FindPresetByTag(FGameplayTag PresetTag) const;

	/** DataAsset（null可）または組み込み既定からタグ照合して DynamicParams を解決 / Resolve params from the asset (nullable) or built-in defaults. */
	static bool ResolvePresetParamsByTag(const UKawaiiPhysicsWindPresetDataAsset* PresetDataAsset, FGameplayTag PresetTag, FKawaiiProceduralWindDynamicParams& OutParams);

#if WITH_EDITOR
	// UObject インターフェース開始 / Begin UObject Interface.
#if UE_VERSION_OLDER_THAN(5, 3, 0)
	virtual EDataValidationResult IsDataValid(TArray<FText>& ValidationErrors) override;
#else
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	// UObject インターフェース終了 / End UObject Interface.
#endif

	/** 組み込みの Wind Scope プリセットを返す / Return built-in Wind Scope presets. */
	static TArray<FKawaiiProceduralWindPreset> GetDefaultPresets();
};
