// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "ExternalForces/KawaiiPhysicsExternalForce.h"

#include "HAL/CriticalSection.h"
#include "Math/RandomStream.h"
#include "Misc/Optional.h"
#include "Templates/SharedPointer.h"

#include "KawaiiPhysicsExternalForce_ProceduralWind.generated.h"

struct FKawaiiProceduralWindGustRequest
{
	float Strength = 0.0f;
	float RiseTime = 0.0f;
	float DecayTime = 0.0f;
};

struct FKawaiiProceduralWindActiveGust
{
	float StartTime = 0.0f;
	float Strength = 0.0f;
	float RiseTime = 0.0f;
	float DecayTime = 0.0f;
	bool bIsActive = false;
};

struct FKawaiiPhysicsProceduralWindSample
{
	float Steady = 0.0f;
	float Oscillation = 0.0f;
	float Wave = 0.0f;
	float Envelope = 1.0f;
	float Random = 0.0f;
	float Gust = 0.0f;
	float Total = 0.0f;
};

struct FKawaiiProceduralWindScopeSample
{
	float Time = 0.0f;
	FKawaiiPhysicsProceduralWindSample Sample;
};

/**
* ProceduralWind のランタイム更新パラメータ。bOverride が true の項目だけ次回 PreApply で反映される。
* Runtime update parameters for ProceduralWind. Only entries whose bOverride flag is true are applied on the next PreApply.
*/
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiProceduralWindDynamicParams
{
	GENERATED_BODY()

	/** bIsEnabled を上書きする / Override bIsEnabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideIsEnabled = false;

	/** 外力の有効状態 / External force enabled state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bIsEnabled = true;

	/** WindDirection を上書きする / Override WindDirection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideWindDirection = false;

	/** 風方向 / Wind direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	FRotator WindDirection = FRotator::ZeroRotator;

	/** SteadyForce を上書きする / Override SteadyForce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideSteadyForce = false;

	/** 定常風力 / Steady wind force. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float SteadyForce = 0.0f;

	/** OscillationForce を上書きする / Override OscillationForce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideOscillationForce = false;

	/** 振動風力 / Oscillating wind force. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float OscillationForce = 0.0f;

	/** OscillationPeriod を上書きする / Override OscillationPeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideOscillationPeriod = false;

	/** 振動周期（秒） / Oscillation period, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float OscillationPeriod = 1.0f;

	/** WaveAmplitude を上書きする / Override WaveAmplitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideWaveAmplitude = false;

	/** 空間波の振幅 / Spatial wave amplitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float WaveAmplitude = 0.0f;

	/** WavePeriod を上書きする / Override WavePeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideWavePeriod = false;

	/** 空間波の周期（秒） / Spatial wave period, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float WavePeriod = 1.0f;

	/** WavePhase を上書きする / Override WavePhase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideWavePhase = false;

	/** 空間波の位相 / Spatial wave phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float WavePhase = 0.0f;

	/** WaveSpatialOffset を上書きする / Override WaveSpatialOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideWaveSpatialOffset = false;

	/** 毛先 r=1 での位相遅れ量 / Phase delay at tip r=1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float WaveSpatialOffset = 0.0f;

	/** EnvelopeMax を上書きする / Override EnvelopeMax. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideEnvelopeMax = false;

	/** エンベロープ最大値 / Maximum envelope value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float EnvelopeMax = 1.0f;

	/** EnvelopeMin を上書きする / Override EnvelopeMin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideEnvelopeMin = false;

	/** エンベロープ最小値 / Minimum envelope value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float EnvelopeMin = 1.0f;

	/** EnvelopeFrequency を上書きする / Override EnvelopeFrequency. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideEnvelopeFrequency = false;

	/** エンベロープ周波数（Hz） / Envelope frequency, in Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float EnvelopeFrequency = 0.1f;

	/** EnvelopePhase を上書きする / Override EnvelopePhase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideEnvelopePhase = false;

	/** エンベロープ位相 / Envelope phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float EnvelopePhase = 0.0f;

	/** RandomForce を上書きする / Override RandomForce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideRandomForce = false;

	/** ランダム風力 / Random wind force. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float RandomForce = 0.0f;

	/** RandomPeriod を上書きする / Override RandomPeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideRandomPeriod = false;

	/** ランダム風力の周期（秒） / Random wind period, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float RandomPeriod = 0.5f;

	/** DirectionNoiseAngle を上書きする / Override DirectionNoiseAngle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideDirectionNoiseAngle = false;

	/** 方向揺らぎの円錐半角 / Cone half-angle for directional noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float DirectionNoiseAngle = 0.0f;

	/** DirectionNoisePeriod を上書きする / Override DirectionNoisePeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideDirectionNoisePeriod = false;

	/** 方向揺らぎの周期（秒） / Period for directional noise, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float DirectionNoisePeriod = 1.0f;

	/** TimeScale を上書きする / Override TimeScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	bool bOverrideTimeScale = false;

	/** 時間スケール / Time scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce|ProceduralWind|DynamicParams")
	float TimeScale = 1.0f;
};

struct FKawaiiProceduralWindRuntimeState
{
	FCriticalSection Mutex;
	TOptional<FKawaiiProceduralWindDynamicParams> PendingParams;
	TOptional<FKawaiiProceduralWindGustRequest> PendingGust;

	float Time = 0.0f;
	FKawaiiProceduralWindActiveGust ActiveGust;

	float CachedSinesWithoutWave = 0.0f;
	float CachedEnvelope = 1.0f;
	float CachedRandom = 0.0f;
	float CachedGust = 0.0f;
	FVector CachedWindVector = FVector::ZeroVector;

#if WITH_EDITOR
	TArray<FKawaiiProceduralWindScopeSample> ScopeBuffer;
	int32 ScopeWriteIndex = 0;
	uint64 ScopeSampleCount = 0;
#endif
};

///
/// Procedural Wind
///
/**
* パラメトリック合成風。UE の WindDirectionalSource に依存しない。
* 合成式: Total = (SteadyForce + Oscillation + Wave) × Envelope + Random + Gust（Gust は TriggerProceduralWindGust API から発生）
* Parametric synthesized wind. This does not depend on UE WindDirectionalSource.
* Composition: Total = (SteadyForce + Oscillation + Wave) x Envelope + Random + Gust (Gust is triggered via the TriggerProceduralWindGust API).
*/
USTRUCT(BlueprintType, DisplayName = "Procedural Wind")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_ProceduralWind : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	FKawaiiPhysics_ExternalForce_ProceduralWind();
	// コピーは RuntimeState を共有しない。代入先の RuntimeState が有効ならポインタと中身を保持する（メンバ追加時は operator= のコピー処理にも追加すること）
	FKawaiiPhysics_ExternalForce_ProceduralWind(const FKawaiiPhysics_ExternalForce_ProceduralWind& Other);
	FKawaiiPhysics_ExternalForce_ProceduralWind& operator=(const FKawaiiPhysics_ExternalForce_ProceduralWind& Other);

	/**
	* 風の吹く方向。BP からは SetExternalForceRotatorProperty("WindDirection") で変更可能
	* Direction the wind blows. Controllable from BP via SetExternalForceRotatorProperty("WindDirection").
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=3, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	FRotator WindDirection = FRotator::ZeroRotator;

	/**
	* 風向き自体の揺らぎの円錐半角。向きの単調さを消す
	* Cone half-angle of the wind direction wander. Breaks directional monotony.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=4, Units="Degrees", ClampMin=0, UIMin=0,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float DirectionNoiseAngle = 0.0f;

	/**
	* 向きの揺らぎの周期（秒）
	* Period of the direction wander, in seconds.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=5, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float DirectionNoisePeriod = 1.0f;

	/**
	* この外力内の時間の進み。0で凍結、2で倍速
	* Time scale of this force. 0 freezes, 2 doubles speed.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=20, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float TimeScale = 1.0f;

	/**
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=2),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/**
	* 常に一定で掛かる風の強さ。上げるとボーンが風下へ流されたままになる
	* Constant wind strength. Higher values keep bones pushed downwind.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=6, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float SteadyForce = 0.0f;

	/**
	* サイン波で周期的に強弱する成分の振幅。上げると規則的な揺れが強くなる
	* Amplitude of the sine-based oscillation. Higher values strengthen the rhythmic sway.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=7, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float OscillationForce = 0.0f;

	/**
	* 振動の周期（秒）。短いほど速く揺れる
	* Oscillation period in seconds. Shorter values sway faster.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=8, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float OscillationPeriod = 1.0f;

	/**
	* ボーン列に沿って伝わる波成分の振幅。WaveSpatialOffset と組で使う
	* Amplitude of the traveling wave along the bone chain. Use together with WaveSpatialOffset.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=9, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float WaveAmplitude = 0.0f;

	/**
	* 波の周期（秒）
	* Wave period in seconds.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=10, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float WavePeriod = 1.0f;

	/**
	* 開始位相のオフセット。複数キャラや複数外力の揺れタイミングをずらす用途
	* Initial phase offset. Use to desynchronize multiple characters or forces.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=11, Units="Degrees", PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float WavePhase = 0.0f;

	/**
	* 毛先（LengthRate=1）での位相遅れ量。正の値で根元から毛先へ波が走って見える。0なら全ボーン同時に揺れる
	* Phase delay at the tip. Positive values make the wave travel from root to tip; 0 sways all bones together.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=12, Units="Degrees", PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float WaveSpatialOffset = 0.0f;

	/**
	* 風全体の強弱のうねり（低周波変調）の上限倍率。Min を下げると凪↔強風の緩急が生まれる。Min=Max=1 で無効
	* Upper multiplier of the slow global intensity swell. Lower Min creates calm-to-gust dynamics. Disabled when Min=Max=1.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=13, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float EnvelopeMax = 1.0f;

	/**
	* 風全体の強弱のうねり（低周波変調）の下限倍率。Min を下げると凪↔強風の緩急が生まれる。Min=Max=1 で無効
	* Lower multiplier of the slow global intensity swell. Lower Min creates calm-to-gust dynamics. Disabled when Min=Max=1.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=14, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float EnvelopeMin = 1.0f;

	/**
	* うねりの速さ（Hz）。低いほどゆったり
	* Swell speed in Hz. Lower is slower.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=15, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float EnvelopeFrequency = 0.1f;

	/**
	* 開始位相のオフセット。複数キャラや複数外力の揺れタイミングをずらす用途
	* Initial phase offset. Use to desynchronize multiple characters or forces.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=16, Units="Degrees", PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float EnvelopePhase = 0.0f;

	/**
	* 不規則な揺らぎ成分の強さ。自然なランダム感を足す
	* Strength of the irregular fluctuation. Adds natural randomness.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=17, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float RandomForce = 0.0f;

	/**
	* 揺らぎが変化する速さ（秒）。短いほど細かく震える
	* How fast the fluctuation changes, in seconds. Shorter values jitter faster.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=18, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	float RandomPeriod = 0.5f;

	/**
	* ランダム系列のシード。同じ値なら同じ揺らぎを再現できる。基底の RandomForceScaleRange が (1,1) 以外の場合は再現性が失われる
	* Seed of the random sequence. Same seed reproduces the same fluctuation. Reproducibility is lost if the base RandomForceScaleRange is not (1,1).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=19),
		Category="KawaiiPhysics|ExternalForce|Procedural Wind")
	int32 RandomSeed = 0;

	TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> RuntimeState;

	void ResetRuntimeState();
	TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> EnsureRuntimeState();
	void ApplyDynamicParams(const FKawaiiProceduralWindDynamicParams& Params);
	void RequestDynamicParams(const FKawaiiProceduralWindDynamicParams& Params);
	void RequestGust(float Strength, float RiseTime, float DecayTime);
	void ConsumePendingRequests();

	FKawaiiPhysicsProceduralWindSample ComputeWindSample(float InTime, float InLengthRate = 0.0f) const;
	static uint32 StableHash(int32 Seed, int32 GridIndex, int32 Channel);
	static float NoiseValueAt(int32 GridIndex, int32 Seed, int32 Channel);
	static float SampleSmoothNoise(float U, int32 Seed, int32 Channel = 0);

	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;

#if WITH_EDITOR
	virtual void AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
	                                      const FAnimNode_KawaiiPhysics& Node, FPrimitiveDrawInterface* PDI) override;
#endif
};

template<>
struct TStructOpsTypeTraits<FKawaiiPhysics_ExternalForce_ProceduralWind> : public TStructOpsTypeTraitsBase2<FKawaiiPhysics_ExternalForce_ProceduralWind>
{
	enum
	{
		WithCopy = true,
	};
};
