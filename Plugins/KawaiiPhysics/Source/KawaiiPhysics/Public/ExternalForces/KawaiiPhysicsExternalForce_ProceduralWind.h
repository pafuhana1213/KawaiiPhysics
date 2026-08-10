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
* RandomForceScaleRange が (1,1) 以外の場合、毎フレームのランダムスケールにより決定性が失われる。
* Parametric synthesized wind. This does not depend on UE WindDirectionalSource.
* If RandomForceScaleRange is not (1,1), the per-frame random scale makes the result non-deterministic.
*/
USTRUCT(BlueprintType, DisplayName = "Procedural Wind")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_ProceduralWind : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	FKawaiiPhysics_ExternalForce_ProceduralWind()
	{
		bCanSelectForceSpace = true;
		ExternalForceSpace = EExternalForceSpace::WorldSpace;
	}

	/**
	* 風方向。BP からは SetExternalForceRotatorProperty("WindDirection") で操作する。
	* Wind direction. Use SetExternalForceRotatorProperty("WindDirection") to control this from BP.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Common")
	FRotator WindDirection = FRotator::ZeroRotator;

	/**
	* 方向揺らぎの円錐半角
	* Cone half-angle for directional noise
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=2, Units="Degrees", ClampMin=0, UIMin=0,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|ProceduralWind|Common")
	float DirectionNoiseAngle = 0.0f;

	/**
	* 方向揺らぎの周期（秒）
	* Period for directional noise, in seconds
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=3, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|ProceduralWind|Common")
	float DirectionNoisePeriod = 1.0f;

	/**
	* 時間スケール
	* Time scale
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=4, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Common")
	float TimeScale = 1.0f;

	/**
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=5),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Common")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/**
	* 定常風力
	* Steady wind force
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Force")
	float SteadyForce = 0.0f;

	/**
	* 振動風力
	* Oscillating wind force
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=2, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Force")
	float OscillationForce = 0.0f;

	/**
	* 振動周期（秒）
	* Oscillation period, in seconds
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=3, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|ProceduralWind|Force")
	float OscillationPeriod = 1.0f;

	/**
	* 空間波の振幅
	* Spatial wave amplitude
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=4, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Force")
	float WaveAmplitude = 0.0f;

	/**
	* 空間波の周期（秒）
	* Spatial wave period, in seconds
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=5, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|ProceduralWind|Force")
	float WavePeriod = 1.0f;

	/**
	* 空間波の位相
	* Spatial wave phase
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=6, Units="Degrees", PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Force")
	float WavePhase = 0.0f;

	/**
	* 毛先 r=1 での位相遅れ量。正の値で根元から毛先へ波が伝播する。
	* Phase delay at tip r=1. Positive values make the wave propagate from root to tip.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=7, Units="Degrees", PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Force")
	float WaveSpatialOffset = 0.0f;

	/**
	* エンベロープ最大値
	* Maximum envelope value
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Envelope")
	float EnvelopeMax = 1.0f;

	/**
	* エンベロープ最小値
	* Minimum envelope value
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=2, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Envelope")
	float EnvelopeMin = 1.0f;

	/**
	* エンベロープ周波数（Hz）
	* Envelope frequency, in Hz
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=3, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Envelope")
	float EnvelopeFrequency = 0.1f;

	/**
	* エンベロープ位相
	* Envelope phase
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=4, Units="Degrees", PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Envelope")
	float EnvelopePhase = 0.0f;

	/**
	* ランダム風力
	* Random wind force
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1, ClampMin=0, UIMin=0, PinHiddenByDefault),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Random")
	float RandomForce = 0.0f;

	/**
	* ランダム風力の周期（秒）
	* Random wind period, in seconds
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=2, ClampMin=0.01, UIMin=0.01,
		PinHiddenByDefault), Category="KawaiiPhysics|ExternalForce|ProceduralWind|Random")
	float RandomPeriod = 0.5f;

	/**
	* ランダム風力のシード
	* Random wind seed
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=3),
		Category="KawaiiPhysics|ExternalForce|ProceduralWind|Random")
	int32 RandomSeed = 0;

	TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> RuntimeState;

	void ResetRuntimeState();
	void ApplyDynamicParams(const FKawaiiProceduralWindDynamicParams& Params);
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
