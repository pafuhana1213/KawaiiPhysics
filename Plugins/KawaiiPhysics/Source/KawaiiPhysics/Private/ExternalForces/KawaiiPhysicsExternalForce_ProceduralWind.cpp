// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"

#include "HAL/CriticalSection.h"
#include "Math/RotationMatrix.h"
#include "Misc/ScopeLock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_ProceduralWind)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_ProceduralWind_Apply"),
                   STAT_KawaiiPhysics_ExternalForce_ProceduralWind_Apply, STATGROUP_Anim);

namespace
{
constexpr float TwoPi = UE_PI * 2.0f;

// WindScope プレビュー用のリングバッファサイズ。SKawaiiPhysicsWindScopeWindow の描画解像度と対応する
constexpr int32 ScopeBufferSize = 512;

// 正規化に失敗した場合は前方ベクトルへフォールバックする（GetSafeNormal の2引数版は UE バージョン間で挙動差があるため不使用）
FVector SafeDirectionOrForward(const FVector& InVector)
{
	const FVector Normalized = InVector.GetSafeNormal();
	return Normalized.IsNearlyZero() ? FVector::ForwardVector : Normalized;
}

// アクティブな gust envelope を経過時間から評価する（0 → Strength → 0 の線形 rise/decay）。
// Strength/RiseTime/DecayTime は TriggerProceduralWindGust API 経由で ActiveGust にセットされる
float EvaluateActiveGust(const FKawaiiProceduralWindActiveGust& ActiveGust, const float InTime)
{
	if (!ActiveGust.bIsActive)
	{
		return 0.0f;
	}

	const float ElapsedTime = InTime - ActiveGust.StartTime;
	if (ElapsedTime < 0.0f)
	{
		return 0.0f;
	}

	const float RiseTime = FMath::Max(ActiveGust.RiseTime, 0.0f);
	const float DecayTime = FMath::Max(ActiveGust.DecayTime, 0.0f);
	// rise フェーズ: 0 から Strength へ線形に立ち上がる
	if (RiseTime > KINDA_SMALL_NUMBER && ElapsedTime < RiseTime)
	{
		return ActiveGust.Strength * (ElapsedTime / RiseTime);
	}

	// DecayTime が実質ゼロならここで即終了（ゼロ除算防止）
	if (DecayTime <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// decay フェーズ: Strength から 0 へ線形に収束
	const float DecayElapsedTime = ElapsedTime - RiseTime;
	if (DecayElapsedTime < DecayTime)
	{
		return ActiveGust.Strength * (1.0f - DecayElapsedTime / DecayTime);
	}

	return 0.0f;
}

// 基準方向 InDirection を軸とする円錐内で、(NoiseX, NoiseY) の向きへ ConeHalfAngleDegrees を上限にぶれさせる
FVector ApplyConeNoiseToDirection(const FVector& InDirection, const float NoiseX, const float NoiseY,
                                  const float ConeHalfAngleDegrees)
{
	// 半角がほぼ0なら揺らぎ無効。基準方向をそのまま返す
	const float ConeHalfAngleRadians = FMath::DegreesToRadians(FMath::Max(ConeHalfAngleDegrees, 0.0f));
	if (ConeHalfAngleRadians <= KINDA_SMALL_NUMBER)
	{
		return SafeDirectionOrForward(InDirection);
	}

	// (NoiseX, NoiseY) を単位円板上のオフセットとみなす
	FVector UnitDisk = FVector(NoiseX, NoiseY, 0.0f);
	const float DiskLength = UnitDisk.Size2D();
	if (DiskLength <= KINDA_SMALL_NUMBER)
	{
		return SafeDirectionOrForward(InDirection);
	}

	// 単位円の外に出た場合は円周上へクランプ
	if (DiskLength > 1.0f)
	{
		UnitDisk /= DiskLength;
	}

	// 基準方向に直交する2軸を求め、その平面内でオフセット方向を構成する
	const FVector BaseDirection = SafeDirectionOrForward(InDirection);
	FVector AxisX;
	FVector AxisY;
	BaseDirection.FindBestAxisVectors(AxisX, AxisY);

	// 円錐の半角とディスク上のオフセット長から、基準方向を軸とした円錐内のベクトルを合成する
	const FVector OffsetDirection = (AxisX * UnitDisk.X + AxisY * UnitDisk.Y).GetSafeNormal();
	const float NoiseAngle = ConeHalfAngleRadians * FMath::Min(DiskLength, 1.0f);
	return (BaseDirection * FMath::Cos(NoiseAngle) + OffsetDirection * FMath::Sin(NoiseAngle)).GetSafeNormal();
}

// EditMode のデバッグ矢印の長さを Total の相対的な大きさで正規化する（0〜1目安の比率を返す）
float ComputeDebugTotalRate(const FKawaiiPhysics_ExternalForce_ProceduralWind& Force, const float Total)
{
	// パラメータ設定から起こりうる最大振幅を基準値として概算する
	const float MaxSine = FMath::Abs(Force.SteadyForce) + FMath::Abs(Force.PulseForce) +
		FMath::Abs(Force.WaveAmplitude);
	const float MaxBreathing = FMath::Max(FMath::Abs(Force.BreathingMin), FMath::Abs(Force.BreathingMax));
	const float MaxRandom = FMath::Abs(Force.RandomForce);
	// 基準値が0にならないよう下限1.0を保証（ゼロ除算防止）
	const float Reference = FMath::Max(MaxSine * MaxBreathing + MaxRandom, 1.0f);
	return FMath::Abs(Total) / Reference;
}

void InitializeRuntimeStateContents(FKawaiiProceduralWindRuntimeState& State)
{
	State.PendingParams.Reset();
	State.PendingGust.Reset();
	State.Time = 0.0f;
	State.ActiveGust = FKawaiiProceduralWindActiveGust();
	State.CachedSinesWithoutWave = 0.0f;
	State.CachedBreathing = 1.0f;
	State.CachedRandom = 0.0f;
	State.CachedGust = 0.0f;
	State.CachedWindVector = FVector::ZeroVector;

#if WITH_EDITOR
	State.ScopeBuffer.Empty(FMath::Max(ScopeBufferSize, 1));
	State.ScopeBuffer.SetNum(FMath::Max(ScopeBufferSize, 1));
	State.ScopeWriteIndex = 0;
	State.ScopeSampleCount = 0;
#endif
}

void MergePendingDynamicParams(FKawaiiProceduralWindDynamicParams& PendingParams,
                               const FKawaiiProceduralWindDynamicParams& IncomingParams)
{
	// 単一スロットの PendingParams を上書きすると、consume 前に届いた別項目のリクエストが失われるため、項目単位でマージする。同一項目は後勝ち
	if (IncomingParams.bOverrideIsEnabled)
	{
		PendingParams.bOverrideIsEnabled = true;
		PendingParams.bIsEnabled = IncomingParams.bIsEnabled;
	}
	if (IncomingParams.bOverrideWindDirection)
	{
		PendingParams.bOverrideWindDirection = true;
		PendingParams.WindDirection = IncomingParams.WindDirection;
	}
	if (IncomingParams.bOverrideSteadyForce)
	{
		PendingParams.bOverrideSteadyForce = true;
		PendingParams.SteadyForce = IncomingParams.SteadyForce;
	}
	if (IncomingParams.bOverridePulseForce)
	{
		PendingParams.bOverridePulseForce = true;
		PendingParams.PulseForce = IncomingParams.PulseForce;
	}
	if (IncomingParams.bOverridePulsePeriod)
	{
		PendingParams.bOverridePulsePeriod = true;
		PendingParams.PulsePeriod = IncomingParams.PulsePeriod;
	}
	if (IncomingParams.bOverrideWaveAmplitude)
	{
		PendingParams.bOverrideWaveAmplitude = true;
		PendingParams.WaveAmplitude = IncomingParams.WaveAmplitude;
	}
	if (IncomingParams.bOverrideWavePeriod)
	{
		PendingParams.bOverrideWavePeriod = true;
		PendingParams.WavePeriod = IncomingParams.WavePeriod;
	}
	if (IncomingParams.bOverrideWavePhase)
	{
		PendingParams.bOverrideWavePhase = true;
		PendingParams.WavePhase = IncomingParams.WavePhase;
	}
	if (IncomingParams.bOverrideWaveSpatialOffset)
	{
		PendingParams.bOverrideWaveSpatialOffset = true;
		PendingParams.WaveSpatialOffset = IncomingParams.WaveSpatialOffset;
	}
	if (IncomingParams.bOverrideBreathingMax)
	{
		PendingParams.bOverrideBreathingMax = true;
		PendingParams.BreathingMax = IncomingParams.BreathingMax;
	}
	if (IncomingParams.bOverrideBreathingMin)
	{
		PendingParams.bOverrideBreathingMin = true;
		PendingParams.BreathingMin = IncomingParams.BreathingMin;
	}
	if (IncomingParams.bOverrideBreathingPeriod)
	{
		PendingParams.bOverrideBreathingPeriod = true;
		PendingParams.BreathingPeriod = IncomingParams.BreathingPeriod;
	}
	if (IncomingParams.bOverrideBreathingPhase)
	{
		PendingParams.bOverrideBreathingPhase = true;
		PendingParams.BreathingPhase = IncomingParams.BreathingPhase;
	}
	if (IncomingParams.bOverrideRandomForce)
	{
		PendingParams.bOverrideRandomForce = true;
		PendingParams.RandomForce = IncomingParams.RandomForce;
	}
	if (IncomingParams.bOverrideRandomPeriod)
	{
		PendingParams.bOverrideRandomPeriod = true;
		PendingParams.RandomPeriod = IncomingParams.RandomPeriod;
	}
	if (IncomingParams.bOverrideDirectionNoiseAngle)
	{
		PendingParams.bOverrideDirectionNoiseAngle = true;
		PendingParams.DirectionNoiseAngle = IncomingParams.DirectionNoiseAngle;
	}
	if (IncomingParams.bOverrideDirectionNoisePeriod)
	{
		PendingParams.bOverrideDirectionNoisePeriod = true;
		PendingParams.DirectionNoisePeriod = IncomingParams.DirectionNoisePeriod;
	}
	if (IncomingParams.bOverrideTimeScale)
	{
		PendingParams.bOverrideTimeScale = true;
		PendingParams.TimeScale = IncomingParams.TimeScale;
	}
}
}

FKawaiiPhysics_ExternalForce_ProceduralWind::FKawaiiPhysics_ExternalForce_ProceduralWind()
{
	bCanSelectForceSpace = true;
	ExternalForceSpace = EExternalForceSpace::WorldSpace;
	RuntimeState = MakeShared<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>();
	InitializeRuntimeStateContents(*RuntimeState);
}

FKawaiiPhysics_ExternalForce_ProceduralWind::FKawaiiPhysics_ExternalForce_ProceduralWind(
	const FKawaiiPhysics_ExternalForce_ProceduralWind& Other)
	: FKawaiiPhysics_ExternalForce_ProceduralWind()
{
	*this = Other;
}

FKawaiiPhysics_ExternalForce_ProceduralWind& FKawaiiPhysics_ExternalForce_ProceduralWind::operator=(
	const FKawaiiPhysics_ExternalForce_ProceduralWind& Other)
{
	if (this == &Other)
	{
		return *this;
	}

	static_cast<FKawaiiPhysics_ExternalForce&>(*this) = static_cast<const FKawaiiPhysics_ExternalForce&>(Other);

	WindDirection = Other.WindDirection;
	DirectionNoiseAngle = Other.DirectionNoiseAngle;
	DirectionNoisePeriod = Other.DirectionNoisePeriod;
	TimeScale = Other.TimeScale;
	ForceRateByBoneLengthRate = Other.ForceRateByBoneLengthRate;
	SteadyForce = Other.SteadyForce;
	PulseForce = Other.PulseForce;
	PulsePeriod = Other.PulsePeriod;
	WaveAmplitude = Other.WaveAmplitude;
	WavePeriod = Other.WavePeriod;
	WavePhase = Other.WavePhase;
	WaveSpatialOffset = Other.WaveSpatialOffset;
	BreathingMax = Other.BreathingMax;
	BreathingMin = Other.BreathingMin;
	BreathingPeriod = Other.BreathingPeriod;
	BreathingPhase = Other.BreathingPhase;
	RandomForce = Other.RandomForce;
	RandomPeriod = Other.RandomPeriod;
	RandomSeed = Other.RandomSeed;

	// RuntimeState はインスタンス間でコピー・共有しない。代入先が有効な RuntimeState を持つ場合はポインタと中身（Time/ActiveGust/PendingParams）を保持し、
	// Persona の CopyNodeDataToPreviewNode などのインプレース同期でシミュレーション時刻をリセットしない。無効な代入先だけ新規生成する。
	EnsureRuntimeState();
	return *this;
}

// RuntimeState のポインタは維持し、中身だけを初期状態へ戻す
void FKawaiiPhysics_ExternalForce_ProceduralWind::ResetRuntimeState()
{
	const auto State = EnsureRuntimeState();
	FScopeLock Lock(&State->Mutex);
	InitializeRuntimeStateContents(*State);
}

TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> FKawaiiPhysics_ExternalForce_ProceduralWind::EnsureRuntimeState()
{
	// この関数内の RuntimeState 参照はすべてロックで直列化し、invalid→valid 遷移時の未同期な読み書きを避ける。
	// 他のコードがロックなしで参照できる主保証は、RuntimeState を常に有効に保つ不変条件。
	static FCriticalSection RuntimeStateCreationMutex;
	FScopeLock Lock(&RuntimeStateCreationMutex);
	if (!RuntimeState.IsValid())
	{
		RuntimeState = MakeShared<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>();
		InitializeRuntimeStateContents(*RuntimeState);
	}

	return RuntimeState;
}

// ランタイムAPI（BP/C++）経由で送られた上書きパラメータを反映する。bOverride が立っている項目のみ適用し、
// 各値は下限（周期系は0.01などゼロ除算回避のためのクランプ）を通す
void FKawaiiPhysics_ExternalForce_ProceduralWind::ApplyDynamicParams(
	const FKawaiiProceduralWindDynamicParams& Params)
{
	if (Params.bOverrideIsEnabled)
	{
		bIsEnabled = Params.bIsEnabled;
	}
	if (Params.bOverrideWindDirection)
	{
		WindDirection = Params.WindDirection;
	}
	if (Params.bOverrideSteadyForce)
	{
		SteadyForce = FMath::Max(Params.SteadyForce, 0.0f);
	}
	if (Params.bOverridePulseForce)
	{
		PulseForce = FMath::Max(Params.PulseForce, 0.0f);
	}
	if (Params.bOverridePulsePeriod)
	{
		PulsePeriod = FMath::Max(Params.PulsePeriod, 0.01f);
	}
	if (Params.bOverrideWaveAmplitude)
	{
		WaveAmplitude = FMath::Max(Params.WaveAmplitude, 0.0f);
	}
	if (Params.bOverrideWavePeriod)
	{
		WavePeriod = FMath::Max(Params.WavePeriod, 0.01f);
	}
	if (Params.bOverrideWavePhase)
	{
		WavePhase = Params.WavePhase;
	}
	if (Params.bOverrideWaveSpatialOffset)
	{
		WaveSpatialOffset = Params.WaveSpatialOffset;
	}
	if (Params.bOverrideBreathingMax)
	{
		BreathingMax = FMath::Max(Params.BreathingMax, 0.0f);
	}
	if (Params.bOverrideBreathingMin)
	{
		BreathingMin = FMath::Max(Params.BreathingMin, 0.0f);
	}
	if (Params.bOverrideBreathingPeriod)
	{
		BreathingPeriod = FMath::Max(Params.BreathingPeriod, 0.01f);
	}
	if (Params.bOverrideBreathingPhase)
	{
		BreathingPhase = Params.BreathingPhase;
	}
	if (Params.bOverrideRandomForce)
	{
		RandomForce = FMath::Max(Params.RandomForce, 0.0f);
	}
	if (Params.bOverrideRandomPeriod)
	{
		RandomPeriod = FMath::Max(Params.RandomPeriod, 0.01f);
	}
	if (Params.bOverrideDirectionNoiseAngle)
	{
		DirectionNoiseAngle = FMath::Max(Params.DirectionNoiseAngle, 0.0f);
	}
	if (Params.bOverrideDirectionNoisePeriod)
	{
		DirectionNoisePeriod = FMath::Max(Params.DirectionNoisePeriod, 0.01f);
	}
	if (Params.bOverrideTimeScale)
	{
		TimeScale = FMath::Max(Params.TimeScale, 0.0f);
	}
}

// プロパティ名から単一項目だけを上書きする DynamicParams を組み立てる
bool FKawaiiPhysics_ExternalForce_ProceduralWind::BuildDynamicParamsForProperty(
	const FName PropertyName, FKawaiiProceduralWindDynamicParams& OutParams) const
{
	OutParams = FKawaiiProceduralWindDynamicParams();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce, bIsEnabled))
	{
		OutParams.bOverrideIsEnabled = true;
		OutParams.bIsEnabled = bIsEnabled;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirection))
	{
		OutParams.bOverrideWindDirection = true;
		OutParams.WindDirection = WindDirection;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SteadyForce))
	{
		OutParams.bOverrideSteadyForce = true;
		OutParams.SteadyForce = SteadyForce;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, PulseForce))
	{
		OutParams.bOverridePulseForce = true;
		OutParams.PulseForce = PulseForce;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, PulsePeriod))
	{
		OutParams.bOverridePulsePeriod = true;
		OutParams.PulsePeriod = PulsePeriod;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WaveAmplitude))
	{
		OutParams.bOverrideWaveAmplitude = true;
		OutParams.WaveAmplitude = WaveAmplitude;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WavePeriod))
	{
		OutParams.bOverrideWavePeriod = true;
		OutParams.WavePeriod = WavePeriod;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WavePhase))
	{
		OutParams.bOverrideWavePhase = true;
		OutParams.WavePhase = WavePhase;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WaveSpatialOffset))
	{
		OutParams.bOverrideWaveSpatialOffset = true;
		OutParams.WaveSpatialOffset = WaveSpatialOffset;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, BreathingMax))
	{
		OutParams.bOverrideBreathingMax = true;
		OutParams.BreathingMax = BreathingMax;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, BreathingMin))
	{
		OutParams.bOverrideBreathingMin = true;
		OutParams.BreathingMin = BreathingMin;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, BreathingPeriod))
	{
		OutParams.bOverrideBreathingPeriod = true;
		OutParams.BreathingPeriod = BreathingPeriod;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, BreathingPhase))
	{
		OutParams.bOverrideBreathingPhase = true;
		OutParams.BreathingPhase = BreathingPhase;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForce))
	{
		OutParams.bOverrideRandomForce = true;
		OutParams.RandomForce = RandomForce;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomPeriod))
	{
		OutParams.bOverrideRandomPeriod = true;
		OutParams.RandomPeriod = RandomPeriod;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, DirectionNoiseAngle))
	{
		OutParams.bOverrideDirectionNoiseAngle = true;
		OutParams.DirectionNoiseAngle = DirectionNoiseAngle;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, DirectionNoisePeriod))
	{
		OutParams.bOverrideDirectionNoisePeriod = true;
		OutParams.DirectionNoisePeriod = DirectionNoisePeriod;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, TimeScale))
	{
		OutParams.bOverrideTimeScale = true;
		OutParams.TimeScale = TimeScale;
		return true;
	}

	return false;
}

// 現在値を全項目上書きの DynamicParams として組み立てる
FKawaiiProceduralWindDynamicParams FKawaiiPhysics_ExternalForce_ProceduralWind::BuildDynamicParamsSnapshot() const
{
	FKawaiiProceduralWindDynamicParams Params;
	Params.bOverrideIsEnabled = true;
	Params.bIsEnabled = bIsEnabled;
	Params.bOverrideWindDirection = true;
	Params.WindDirection = WindDirection;
	Params.bOverrideSteadyForce = true;
	Params.SteadyForce = SteadyForce;
	Params.bOverridePulseForce = true;
	Params.PulseForce = PulseForce;
	Params.bOverridePulsePeriod = true;
	Params.PulsePeriod = PulsePeriod;
	Params.bOverrideWaveAmplitude = true;
	Params.WaveAmplitude = WaveAmplitude;
	Params.bOverrideWavePeriod = true;
	Params.WavePeriod = WavePeriod;
	Params.bOverrideWavePhase = true;
	Params.WavePhase = WavePhase;
	Params.bOverrideWaveSpatialOffset = true;
	Params.WaveSpatialOffset = WaveSpatialOffset;
	Params.bOverrideBreathingMax = true;
	Params.BreathingMax = BreathingMax;
	Params.bOverrideBreathingMin = true;
	Params.BreathingMin = BreathingMin;
	Params.bOverrideBreathingPeriod = true;
	Params.BreathingPeriod = BreathingPeriod;
	Params.bOverrideBreathingPhase = true;
	Params.BreathingPhase = BreathingPhase;
	Params.bOverrideRandomForce = true;
	Params.RandomForce = RandomForce;
	Params.bOverrideRandomPeriod = true;
	Params.RandomPeriod = RandomPeriod;
	Params.bOverrideDirectionNoiseAngle = true;
	Params.DirectionNoiseAngle = DirectionNoiseAngle;
	Params.bOverrideDirectionNoisePeriod = true;
	Params.DirectionNoisePeriod = DirectionNoisePeriod;
	Params.bOverrideTimeScale = true;
	Params.TimeScale = TimeScale;
	return Params;
}

// 動的パラメータ更新を PendingParams として積み、次回 PreApply で反映する
void FKawaiiPhysics_ExternalForce_ProceduralWind::RequestDynamicParams(
	const FKawaiiProceduralWindDynamicParams& Params)
{
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = EnsureRuntimeState();
	FScopeLock Lock(&LocalRuntimeState->Mutex);
	if (LocalRuntimeState->PendingParams.IsSet())
	{
		MergePendingDynamicParams(LocalRuntimeState->PendingParams.GetValue(), Params);
	}
	else
	{
		LocalRuntimeState->PendingParams = Params;
	}
}

// 突風要求を PendingGust として積み、次回 PreApply で反映する
void FKawaiiPhysics_ExternalForce_ProceduralWind::RequestGust(
	const float Strength, const float RiseTime, const float DecayTime)
{
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = EnsureRuntimeState();
	FScopeLock Lock(&LocalRuntimeState->Mutex);
	LocalRuntimeState->PendingGust = FKawaiiProceduralWindGustRequest{
		Strength,
		RiseTime,
		DecayTime
	};
}

// 他スレッド（Game Thread の BP API など）から積まれた Pending 要求を Worker 側の PreApply で取り込む。
// Mutex で RuntimeState を保護し、書き込み側（Set*/TriggerGust API）とのスレッドセーフ性を確保する
void FKawaiiPhysics_ExternalForce_ProceduralWind::ConsumePendingRequests()
{
	if (!RuntimeState.IsValid())
	{
		return;
	}

	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = RuntimeState;
	FScopeLock Lock(&LocalRuntimeState->Mutex);
	// パラメータ上書き要求を適用
	if (LocalRuntimeState->PendingParams.IsSet())
	{
		ApplyDynamicParams(LocalRuntimeState->PendingParams.GetValue());
		LocalRuntimeState->PendingParams.Reset();
	}

	// gust 要求を ActiveGust として起動する（StartTime は現在のシミュレーション内時間を基準にする）
	if (LocalRuntimeState->PendingGust.IsSet())
	{
		const FKawaiiProceduralWindGustRequest& PendingGust = LocalRuntimeState->PendingGust.GetValue();
		LocalRuntimeState->ActiveGust.StartTime = LocalRuntimeState->Time;
		LocalRuntimeState->ActiveGust.Strength = PendingGust.Strength;
		LocalRuntimeState->ActiveGust.RiseTime = PendingGust.RiseTime;
		LocalRuntimeState->ActiveGust.DecayTime = PendingGust.DecayTime;
		LocalRuntimeState->ActiveGust.bIsActive = true;
		LocalRuntimeState->PendingGust.Reset();
	}
}

// 時刻 InTime とボーンの長さ率 InLengthRate から風力の各成分を計算する。
// PreApply（フレーム単位のキャッシュ生成）と SKawaiiPhysicsWindScopeWindow（理論波形プレビュー）の
// 両方から呼ばれる共有関数のため、ここでの合成式を変更する場合は両者の見た目が一致することを確認する
FKawaiiPhysicsProceduralWindSample FKawaiiPhysics_ExternalForce_ProceduralWind::ComputeWindSample(
	const float InTime, const float InLengthRate) const
{
	FKawaiiPhysicsProceduralWindSample Sample;

	// 周期パラメータのゼロ除算防止クランプ
	const float SafePulsePeriod = FMath::Max(PulsePeriod, 0.01f);
	const float SafeWavePeriod = FMath::Max(WavePeriod, 0.01f);
	const float SafeRandomPeriod = FMath::Max(RandomPeriod, 0.01f);
	const float SafeBreathingPeriod = FMath::Max(BreathingPeriod, 0.01f);

	// 定常成分
	Sample.Steady = SteadyForce;
	// パルス成分（周期的な押し引き）
	Sample.Pulse = PulseForce * FMath::Sin(TwoPi * InTime / SafePulsePeriod);
	// ボーン列に沿って伝わる空間波。InLengthRate（毛先方向の距離率）ぶんだけ位相をずらし、根元から毛先へ
	// 波が伝播しているように見せる
	Sample.Wave = WaveAmplitude * FMath::Sin(TwoPi * InTime / SafeWavePeriod -
		FMath::DegreesToRadians(InLengthRate * WaveSpatialOffset) + FMath::DegreesToRadians(WavePhase));
	// 低周波の呼吸変調。sin を [0,1] に正規化してから BreathingMin-Max 間を補間する
	Sample.Breathing = FMath::Lerp(BreathingMin, BreathingMax,
		0.5f * (1.0f + FMath::Sin(TwoPi * InTime / SafeBreathingPeriod + FMath::DegreesToRadians(BreathingPhase))));
	// seeded smooth noise によるランダム成分。同一 RandomSeed なら実行のたびに同じ揺らぎを再現する
	Sample.Random = RandomForce * SampleSmoothNoise(InTime / SafeRandomPeriod, RandomSeed, 0);

	// アクティブな gust があれば加算
	if (RuntimeState.IsValid())
	{
		Sample.Gust = EvaluateActiveGust(RuntimeState->ActiveGust, InTime);
	}

	// 最終合成: (定常 + パルス + 波) × breathing + random + gust
	Sample.Total = (Sample.Steady + Sample.Pulse + Sample.Wave) * Sample.Breathing +
		Sample.Random + Sample.Gust;
	return Sample;
}

// (Seed, GridIndex, Channel) から決定論的なハッシュ値を作る（FNV-1aベースのミックス + fmix32相当の追加撹拌）。
// RandomStream の内部状態を跨いで持ち回さず、都度この値から種を作ることで実行順序やスレッドに依存しない
// 再現性を持たせている
uint32 FKawaiiPhysics_ExternalForce_ProceduralWind::StableHash(const int32 Seed, const int32 GridIndex,
                                                               const int32 Channel)
{
	// FNV-1a
	uint32 Hash = 0x811C9DC5u;
	Hash ^= static_cast<uint32>(Seed);
	Hash *= 0x01000193u;
	Hash ^= static_cast<uint32>(GridIndex);
	Hash *= 0x01000193u;
	Hash ^= static_cast<uint32>(Channel);
	Hash *= 0x01000193u;

	// 追加の bit mixing（アバランシェ効果を高める）
	Hash ^= Hash >> 16;
	Hash *= 0x7FEB352Du;
	Hash ^= Hash >> 15;
	Hash *= 0x846CA68Bu;
	Hash ^= Hash >> 16;
	return Hash;
}

// グリッド点 GridIndex における疑似ランダム値 [-1, 1] を返す。StableHash を種にすることで、
// 呼び出し順序に関係なく同じ GridIndex なら常に同じ値になる
float FKawaiiPhysics_ExternalForce_ProceduralWind::NoiseValueAt(const int32 GridIndex, const int32 Seed,
                                                                const int32 Channel)
{
	FRandomStream RandomStream(StableHash(Seed, GridIndex, Channel));
	return RandomStream.FRandRange(-1.0f, 1.0f);
}

// 1次元の smooth value noise。整数グリッド点のランダム値をスムーズステップで補間して滑らかな連続波形にする
float FKawaiiPhysics_ExternalForce_ProceduralWind::SampleSmoothNoise(const float U, const int32 Seed,
                                                                     const int32 Channel)
{
	// U の整数部をグリッドインデックス、小数部を補間係数にする
	const float GridFloat = FMath::FloorToFloat(U);
	const int32 GridIndex = static_cast<int32>(GridFloat);
	const float Alpha = U - GridFloat;
	// 3α²-2α³ のスムーズステップ補間（両端で微分が0になり、線形補間のような折れ目が出ない）
	const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);

	// 隣接グリッド点のランダム値を補間して返す
	return FMath::Lerp(NoiseValueAt(GridIndex, Seed, Channel),
	                   NoiseValueAt(GridIndex + 1, Seed, Channel), SmoothAlpha);
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::Initialize(const FAnimationInitializeContext& Context)
{
	Super::Initialize(Context);

	// ノード初期化時に RuntimeState の中身を初期化し、前回の再生状態（Time/ActiveGust等）を引き継がない
	ResetRuntimeState();
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                           FComponentSpacePoseContext& PoseContext)
{
	Super::PreApply(Node, PoseContext);

	// RuntimeState未生成なら保険として初期化する
	if (!RuntimeState.IsValid())
	{
		ResetRuntimeState();
	}

	// 他スレッドからのランタイム上書き・gust要求を取り込む
	ConsumePendingRequests();

	// TimeScale を考慮したシミュレーション内時間を進める
	RuntimeState->Time += Node.GetStepDeltaTime() * TimeScale;

	// ボーンに依存しない成分（Steady/Pulse/Breathing/Random/Gust）はここで1回だけ計算してキャッシュする。
	// Apply は全ボーンで呼ばれるため、毎ボーン再計算しないための最適化（Waveのみボーン依存で Apply 側が再計算する）
	const FKawaiiPhysicsProceduralWindSample Sample = ComputeWindSample(RuntimeState->Time, 0.0f);
	RuntimeState->CachedSinesWithoutWave = Sample.Steady + Sample.Pulse;
	RuntimeState->CachedBreathing = Sample.Breathing;
	RuntimeState->CachedRandom = Sample.Random;
	RuntimeState->CachedGust = Sample.Gust;

	// 風向きに円錐状の揺らぎを加える（DirectionNoiseAngle>0のときのみ）。X/Y で異なる Channel を使い、
	// 独立した2軸のノイズ系列にする
	const FVector BaseWindDirection = SafeDirectionOrForward(WindDirection);
	FVector NoisyWindDirection = BaseWindDirection;
	if (DirectionNoiseAngle > 0.0f)
	{
		const float SafeDirectionNoisePeriod = FMath::Max(DirectionNoisePeriod, 0.01f);
		const float DirectionNoiseU = RuntimeState->Time / SafeDirectionNoisePeriod;
		const float NoiseX = SampleSmoothNoise(DirectionNoiseU, RandomSeed, 1);
		const float NoiseY = SampleSmoothNoise(DirectionNoiseU, RandomSeed, 2);
		NoisyWindDirection = ApplyConeNoiseToDirection(BaseWindDirection, NoiseX, NoiseY, DirectionNoiseAngle);
	}
	// シミュレーション空間へ変換してフレーム単位でキャッシュ（BoneSpace指定時は Apply 側で更にボーンのTMを掛ける）
	RuntimeState->CachedWindVector = ConvertExternalForceToSimulationSpace(Node, PoseContext, NoisyWindDirection);

#if WITH_EDITOR
	{
		// WindScope の「live」表示用にサンプルをリングバッファへ記録する。Mutex は描画側スレッドとの競合を防ぐため
		FScopeLock Lock(&RuntimeState->Mutex);
		if (RuntimeState->ScopeBuffer.Num() != ScopeBufferSize)
		{
			RuntimeState->ScopeBuffer.SetNum(ScopeBufferSize);
			RuntimeState->ScopeWriteIndex = 0;
			RuntimeState->ScopeSampleCount = 0;
		}

		RuntimeState->ScopeBuffer[RuntimeState->ScopeWriteIndex] = {RuntimeState->Time, Sample};
		RuntimeState->ScopeWriteIndex = (RuntimeState->ScopeWriteIndex + 1) % ScopeBufferSize;
		++RuntimeState->ScopeSampleCount;
	}
#endif
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                        FComponentSpacePoseContext& PoseContext,
                                                        const FTransform& BoneTM)
{
	if (!CanApply(Bone) || !RuntimeState.IsValid())
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_ProceduralWind_Apply);

	// Wave はボーンごとの LengthRateFromRoot に依存するためここで個別に計算し、PreApply でキャッシュした
	// 他成分と合算する。式は ComputeWindSample の Sample.Wave 計算と一致させること
	const float Wave = WaveAmplitude * FMath::Sin(TwoPi * RuntimeState->Time / FMath::Max(WavePeriod, 0.01f) -
		FMath::DegreesToRadians(Bone.LengthRateFromRoot * WaveSpatialOffset) + FMath::DegreesToRadians(WavePhase));
	const float Total = (RuntimeState->CachedSinesWithoutWave + Wave) * RuntimeState->CachedBreathing +
		RuntimeState->CachedRandom + RuntimeState->CachedGust;

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot);
	}

	// RandomizedForceScale は _Wind と同じく Apply 側でスカラーにだけ掛け、方向キャッシュとの二重掛けを避ける。
	// BoneSpace 指定時はキャッシュ済みの風ベクトルに各ボーンのTMを掛けて向きをボーンローカルへ変換する
	if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		const FVector BoneForce = BoneTM.TransformVector(RuntimeState->CachedWindVector);
		Bone.Location += BoneForce * Total * ForceRate * RandomizedForceScale * Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce * Total * ForceRate * RandomizedForceScale);
#endif
	}
	else
	{
		Bone.Location += RuntimeState->CachedWindVector * Total * ForceRate * RandomizedForceScale *
			Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName,
		                 RuntimeState->CachedWindVector * Total * ForceRate * RandomizedForceScale);
#endif
	}

#if ENABLE_ANIM_DEBUG
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}

#if WITH_EDITOR
// EditMode（Persona）でボーンごとの風向き・強さを矢印で可視化する
void FKawaiiPhysics_ExternalForce_ProceduralWind::AnimDrawDebugForEditMode(
	const FKawaiiPhysicsModifyBone& ModifyBone, const FAnimNode_KawaiiPhysics& Node, FPrimitiveDrawInterface* PDI)
{
	if (!IsDebugEnabled(true) || !CanApply(ModifyBone) || !RuntimeState.IsValid() ||
		RuntimeState->CachedWindVector.IsNearlyZero())
	{
		return;
	}

	// Wave/Total の再計算は Apply と同じ式（ComputeWindSample の Sample.Wave と一致させること）
	const float Wave = WaveAmplitude * FMath::Sin(TwoPi * RuntimeState->Time / FMath::Max(WavePeriod, 0.01f) -
		FMath::DegreesToRadians(ModifyBone.LengthRateFromRoot * WaveSpatialOffset) +
		FMath::DegreesToRadians(WavePhase));
	const float Total = (RuntimeState->CachedSinesWithoutWave + Wave) * RuntimeState->CachedBreathing +
		RuntimeState->CachedRandom + RuntimeState->CachedGust;

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(ModifyBone.LengthRateFromRoot);
	}

	// 風向きの矢印を該当ボーン位置に描画。BaseBoneSpace の場合はコンポーネント空間へ変換してから配置する
	FVector ArrowLocation = ModifyBone.Location + DebugArrowOffset;
	FQuat ArrowRotation = RuntimeState->CachedWindVector.GetSafeNormal().ToOrientationQuat();
	if (Node.SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
	{
		const FTransform& BaseBoneSpace2ComponentSpace = Node.GetBaseBoneSpace2ComponentSpace();
		ArrowLocation = BaseBoneSpace2ComponentSpace.TransformPosition(ArrowLocation);
		ArrowRotation = BaseBoneSpace2ComponentSpace.TransformRotation(ArrowRotation);
	}

	// 矢印長は ForceRate と TotalRate（実際の力の相対的な大きさ）で見た目のスケールを調整する
	const float TotalRate = ComputeDebugTotalRate(*this, Total);
	const FTransform ArrowTransform(ArrowRotation, ArrowLocation);
	DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Cyan,
	                     DebugArrowLength * ForceRate * TotalRate, DebugArrowSize, SDPG_Foreground);

	// ルートボーンにはさらに大きめの矢印を重ねて風全体の向き・強さの目安を分かりやすくする
	if (ModifyBone.Index == 0)
	{
		FVector RootArrowLocation = ModifyBone.Location + DebugArrowOffset * 2.0f;
		FQuat RootArrowRotation = RuntimeState->CachedWindVector.GetSafeNormal().ToOrientationQuat();
		if (Node.SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
		{
			const FTransform& BaseBoneSpace2ComponentSpace = Node.GetBaseBoneSpace2ComponentSpace();
			RootArrowLocation = BaseBoneSpace2ComponentSpace.TransformPosition(RootArrowLocation);
			RootArrowRotation = BaseBoneSpace2ComponentSpace.TransformRotation(RootArrowRotation);
		}

		const FTransform RootArrowTransform(RootArrowRotation, RootArrowLocation);
		DrawDirectionalArrow(PDI, RootArrowTransform.ToMatrixNoScale(), FColor::Cyan,
		                     DebugArrowLength * TotalRate, DebugArrowSize * 2.0f, SDPG_Foreground);
	}
}
#endif
