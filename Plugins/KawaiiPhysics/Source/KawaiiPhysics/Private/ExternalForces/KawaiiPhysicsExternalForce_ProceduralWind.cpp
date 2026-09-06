// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"

#include "AnimNode_KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsInternal.h"
#include "HAL/CriticalSection.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "KawaiiPhysicsSharedTags.h"
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

// アクティブな gust envelope を経過時間から評価する（線形 rise → hold → 線形 decay の台形エンベロープ）。
// Strength/RiseTime/DecayTime/HoldTime は StartProceduralWindGust API 経由で ActiveGust にセットされる
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
	const float HoldTime = FMath::Max(ActiveGust.HoldTime, 0.0f);
	const float DecayTime = FMath::Max(ActiveGust.DecayTime, 0.0f);
	// rise フェーズ: 0 から Strength へ線形に立ち上がる
	if (RiseTime > KINDA_SMALL_NUMBER && ElapsedTime < RiseTime)
	{
		return ActiveGust.Strength * (ElapsedTime / RiseTime);
	}

	// hold フェーズ: Strength を維持する
	if (ElapsedTime < RiseTime + HoldTime)
	{
		return ActiveGust.Strength;
	}

	// DecayTime が実質ゼロならここで即終了（ゼロ除算防止）
	if (DecayTime <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// decay フェーズ: Strength から 0 へ線形に収束
	const float DecayElapsedTime = ElapsedTime - RiseTime - HoldTime;
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
	const float MaxSine = FMath::Abs(Force.ConstantForce) + FMath::Abs(Force.SwayForce) +
		FMath::Abs(Force.RippleForce);
	const float MaxStrengthCycle = FMath::Max(FMath::Abs(Force.StrengthCycleRange.Min), FMath::Abs(Force.StrengthCycleRange.Max));
	const float MaxRandom = FMath::Abs(Force.RandomForce);
	// 基準値が0にならないよう下限1.0を保証（ゼロ除算防止）
	const float Reference = FMath::Max(MaxSine * MaxStrengthCycle + MaxRandom, 1.0f);
	return FMath::Abs(Total) / Reference;
}

void InitializeRuntimeStateContents(FKawaiiProceduralWindRuntimeState& State)
{
	State.PendingParams.Reset();
	State.PendingGust.Reset();
	State.PendingGustStop.Reset();
	State.Time = 0.0f;
	State.ActiveGust = FKawaiiProceduralWindActiveGust();
	State.CachedSinesWithoutRipple = 0.0f;
	State.CachedStrengthCycle = 1.0f;
	State.CachedRandom = 0.0f;
	State.CachedGust = 0.0f;
	State.CachedWindVector = FVector::ZeroVector;
	State.SharedPublisherEntry.Reset();
	State.ResolvedSharedTag = FGameplayTag();
	State.LastAppliedSharedSerial = 0;
	State.CachedPublisherTimeScale = 1.0f;
	State.bPublisherWindDisabled = false;
	State.ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
	State.SharedResolveCountdown = 0;
	State.SharedResolveFailures = 0;
	// 復元は呼び出し側（ResetRuntimeState）が先に済ませる。ここでは退避を捨てるだけ
	State.LocalSharedParamsBackup.Reset();
#if !UE_BUILD_SHIPPING
	State.bSharedResolveWarningLogged = false;
#endif

#if WITH_EDITOR
	State.ScopeBuffer.Empty(FMath::Max(ScopeBufferSize, 1));
	State.ScopeBuffer.SetNum(FMath::Max(ScopeBufferSize, 1));
	State.ScopeWriteIndex = 0;
	State.ScopeSampleCount = 0;
#endif
}

}

// bOverride 単位のマージ実装は公開ヘッダ（KawaiiPhysicsExternalForce_ProceduralWind.h）宣言の関数として無名namespaceの外に置く。
// SharedCollisionSubsystem 側の RequestWindParams からも同じマージ処理を呼べるようにするため
void MergeProceduralWindDynamicParams(FKawaiiProceduralWindDynamicParams& Target,
                                       const FKawaiiProceduralWindDynamicParams& Source)
{
	// 単一スロットの Pending 値を上書きすると、consume 前に届いた別項目のリクエストが失われるため、項目単位でマージする。同一項目は後勝ち
	if (Source.bOverrideIsEnabled)
	{
		Target.bOverrideIsEnabled = true;
		Target.bIsEnabled = Source.bIsEnabled;
	}
	if (Source.bOverrideWindDirection)
	{
		Target.bOverrideWindDirection = true;
		Target.WindDirection = Source.WindDirection;
	}
	if (Source.bOverrideConstantForce)
	{
		Target.bOverrideConstantForce = true;
		Target.ConstantForce = Source.ConstantForce;
	}
	if (Source.bOverrideSwayForce)
	{
		Target.bOverrideSwayForce = true;
		Target.SwayForce = Source.SwayForce;
	}
	if (Source.bOverrideSwayPeriod)
	{
		Target.bOverrideSwayPeriod = true;
		Target.SwayPeriod = Source.SwayPeriod;
	}
	if (Source.bOverrideSwayPhaseOffset)
	{
		Target.bOverrideSwayPhaseOffset = true;
		Target.SwayPhaseOffset = Source.SwayPhaseOffset;
	}
	if (Source.bOverrideRippleForce)
	{
		Target.bOverrideRippleForce = true;
		Target.RippleForce = Source.RippleForce;
	}
	if (Source.bOverrideRipplePeriod)
	{
		Target.bOverrideRipplePeriod = true;
		Target.RipplePeriod = Source.RipplePeriod;
	}
	if (Source.bOverrideRipplePhaseOffset)
	{
		Target.bOverrideRipplePhaseOffset = true;
		Target.RipplePhaseOffset = Source.RipplePhaseOffset;
	}
	if (Source.bOverrideRippleTipPhaseDelay)
	{
		Target.bOverrideRippleTipPhaseDelay = true;
		Target.RippleTipPhaseDelay = Source.RippleTipPhaseDelay;
	}
	if (Source.bOverrideStrengthCycleRange)
	{
		Target.bOverrideStrengthCycleRange = true;
		Target.StrengthCycleRange = Source.StrengthCycleRange;
	}
	if (Source.bOverrideStrengthCyclePeriod)
	{
		Target.bOverrideStrengthCyclePeriod = true;
		Target.StrengthCyclePeriod = Source.StrengthCyclePeriod;
	}
	if (Source.bOverrideStrengthCyclePhaseOffset)
	{
		Target.bOverrideStrengthCyclePhaseOffset = true;
		Target.StrengthCyclePhaseOffset = Source.StrengthCyclePhaseOffset;
	}
	if (Source.bOverrideRandomForce)
	{
		Target.bOverrideRandomForce = true;
		Target.RandomForce = Source.RandomForce;
	}
	if (Source.bOverrideRandomForcePeriod)
	{
		Target.bOverrideRandomForcePeriod = true;
		Target.RandomForcePeriod = Source.RandomForcePeriod;
	}
	if (Source.bOverrideWindDirectionNoiseAngle)
	{
		Target.bOverrideWindDirectionNoiseAngle = true;
		Target.WindDirectionNoiseAngle = Source.WindDirectionNoiseAngle;
	}
	if (Source.bOverrideWindDirectionNoisePeriod)
	{
		Target.bOverrideWindDirectionNoisePeriod = true;
		Target.WindDirectionNoisePeriod = Source.WindDirectionNoisePeriod;
	}
	if (Source.bOverrideTimeScale)
	{
		Target.bOverrideTimeScale = true;
		Target.TimeScale = Source.TimeScale;
	}
}

FKawaiiPhysics_ExternalForce_ProceduralWind::FKawaiiPhysics_ExternalForce_ProceduralWind()
{
	bCanSelectForceSpace = true;
	// 固有の Random 系列と紛らわしく、使用すると Seed の再現性も壊れるため基底の RandomForceScaleRange は非表示にする
	bSupportsRandomForceScaleRange = false;
	ExternalForceSpace = EExternalForceSpace::WorldSpace;
	SharedWindTag = TAG_KawaiiPhysics_Shared_Default;
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

	const bool bSharedKeyChanged = WindSource != Other.WindSource || !(SharedWindTag == Other.SharedWindTag);
	WindSource = Other.WindSource;
	SharedWindTag = Other.SharedWindTag;
	ParameterMode = Other.ParameterMode;
	WindDirection = Other.WindDirection;
	WindDirectionNoiseAngle = Other.WindDirectionNoiseAngle;
	WindDirectionNoisePeriod = Other.WindDirectionNoisePeriod;
	TimeScale = Other.TimeScale;
	ForceRateByBoneLengthRate = Other.ForceRateByBoneLengthRate;
	ConstantForce = Other.ConstantForce;
	SwayForce = Other.SwayForce;
	SwayPeriod = Other.SwayPeriod;
	SwayPhaseOffset = Other.SwayPhaseOffset;
	RippleForce = Other.RippleForce;
	RipplePeriod = Other.RipplePeriod;
	RipplePhaseOffset = Other.RipplePhaseOffset;
	RippleTipPhaseDelay = Other.RippleTipPhaseDelay;
	StrengthCycleRange = Other.StrengthCycleRange;
	StrengthCyclePeriod = Other.StrengthCyclePeriod;
	StrengthCyclePhaseOffset = Other.StrengthCyclePhaseOffset;
	RandomForce = Other.RandomForce;
	RandomForcePeriod = Other.RandomForcePeriod;
	Seed = Other.Seed;

	// RuntimeState はインスタンス間でコピー・共有しない。代入先が有効な RuntimeState を持つ場合はポインタと中身（Time/ActiveGust/PendingParams）を保持し、
	// Persona の CopyNodeDataToPreviewNode などのインプレース同期でシミュレーション時刻をリセットしない。無効な代入先だけ新規生成する。
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = EnsureRuntimeState();
	if (bSharedKeyChanged)
	{
		// 共有 13 項目は上のコピーで Other の値に置き換わっているため、退避値は書き戻さず捨てる
		//（Source / Tag が同じ代入＝Persona のプレビュー同期は Shared 継続なので退避を維持する）
		LocalRuntimeState->LocalSharedParamsBackup.Reset();
		LocalRuntimeState->SharedPublisherEntry.Reset();
		LocalRuntimeState->ResolvedSharedTag = FGameplayTag();
		LocalRuntimeState->LastAppliedSharedSerial = 0;
		LocalRuntimeState->ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
		LocalRuntimeState->SharedResolveCountdown = 0;
		LocalRuntimeState->SharedResolveFailures = 0;
		LocalRuntimeState->bPublisherWindDisabled = false;
#if !UE_BUILD_SHIPPING
		LocalRuntimeState->bSharedResolveWarningLogged = false;
#endif
	}
	return *this;
}

// RuntimeState のポインタは維持し、中身だけを初期状態へ戻す
void FKawaiiPhysics_ExternalForce_ProceduralWind::ResetRuntimeState()
{
	const auto State = EnsureRuntimeState();
	// Shared を採用したまま初期化すると共有値が authored 値として焼き付くため、中身を消す前にローカル値へ戻す
	RestoreLocalSharedParams();
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
	if (Params.bOverrideConstantForce)
	{
		ConstantForce = FMath::Max(Params.ConstantForce, 0.0f);
	}
	if (Params.bOverrideSwayForce)
	{
		SwayForce = FMath::Max(Params.SwayForce, 0.0f);
	}
	if (Params.bOverrideSwayPeriod)
	{
		SwayPeriod = FMath::Max(Params.SwayPeriod, 0.01f);
	}
	if (Params.bOverrideSwayPhaseOffset)
	{
		SwayPhaseOffset = Params.SwayPhaseOffset;
	}
	if (Params.bOverrideRippleForce)
	{
		RippleForce = FMath::Max(Params.RippleForce, 0.0f);
	}
	if (Params.bOverrideRipplePeriod)
	{
		RipplePeriod = FMath::Max(Params.RipplePeriod, 0.01f);
	}
	if (Params.bOverrideRipplePhaseOffset)
	{
		RipplePhaseOffset = Params.RipplePhaseOffset;
	}
	if (Params.bOverrideRippleTipPhaseDelay)
	{
		RippleTipPhaseDelay = Params.RippleTipPhaseDelay;
	}
	if (Params.bOverrideStrengthCycleRange)
	{
		StrengthCycleRange.Min = FMath::Max(Params.StrengthCycleRange.Min, 0.0f);
		StrengthCycleRange.Max = FMath::Max(Params.StrengthCycleRange.Max, 0.0f);
	}
	if (Params.bOverrideStrengthCyclePeriod)
	{
		StrengthCyclePeriod = FMath::Max(Params.StrengthCyclePeriod, 0.01f);
	}
	if (Params.bOverrideStrengthCyclePhaseOffset)
	{
		StrengthCyclePhaseOffset = Params.StrengthCyclePhaseOffset;
	}
	if (Params.bOverrideRandomForce)
	{
		RandomForce = FMath::Max(Params.RandomForce, 0.0f);
	}
	if (Params.bOverrideRandomForcePeriod)
	{
		RandomForcePeriod = FMath::Max(Params.RandomForcePeriod, 0.01f);
	}
	if (Params.bOverrideWindDirectionNoiseAngle)
	{
		WindDirectionNoiseAngle = FMath::Max(Params.WindDirectionNoiseAngle, 0.0f);
	}
	if (Params.bOverrideWindDirectionNoisePeriod)
	{
		WindDirectionNoisePeriod = FMath::Max(Params.WindDirectionNoisePeriod, 0.01f);
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
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, ConstantForce))
	{
		OutParams.bOverrideConstantForce = true;
		OutParams.ConstantForce = ConstantForce;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayForce))
	{
		OutParams.bOverrideSwayForce = true;
		OutParams.SwayForce = SwayForce;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayPeriod))
	{
		OutParams.bOverrideSwayPeriod = true;
		OutParams.SwayPeriod = SwayPeriod;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, SwayPhaseOffset))
	{
		OutParams.bOverrideSwayPhaseOffset = true;
		OutParams.SwayPhaseOffset = SwayPhaseOffset;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RippleForce))
	{
		OutParams.bOverrideRippleForce = true;
		OutParams.RippleForce = RippleForce;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RipplePeriod))
	{
		OutParams.bOverrideRipplePeriod = true;
		OutParams.RipplePeriod = RipplePeriod;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RipplePhaseOffset))
	{
		OutParams.bOverrideRipplePhaseOffset = true;
		OutParams.RipplePhaseOffset = RipplePhaseOffset;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RippleTipPhaseDelay))
	{
		OutParams.bOverrideRippleTipPhaseDelay = true;
		OutParams.RippleTipPhaseDelay = RippleTipPhaseDelay;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCycleRange))
	{
		OutParams.bOverrideStrengthCycleRange = true;
		OutParams.StrengthCycleRange = StrengthCycleRange;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCyclePeriod))
	{
		OutParams.bOverrideStrengthCyclePeriod = true;
		OutParams.StrengthCyclePeriod = StrengthCyclePeriod;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, StrengthCyclePhaseOffset))
	{
		OutParams.bOverrideStrengthCyclePhaseOffset = true;
		OutParams.StrengthCyclePhaseOffset = StrengthCyclePhaseOffset;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForce))
	{
		OutParams.bOverrideRandomForce = true;
		OutParams.RandomForce = RandomForce;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, RandomForcePeriod))
	{
		OutParams.bOverrideRandomForcePeriod = true;
		OutParams.RandomForcePeriod = RandomForcePeriod;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirectionNoiseAngle))
	{
		OutParams.bOverrideWindDirectionNoiseAngle = true;
		OutParams.WindDirectionNoiseAngle = WindDirectionNoiseAngle;
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FKawaiiPhysics_ExternalForce_ProceduralWind, WindDirectionNoisePeriod))
	{
		OutParams.bOverrideWindDirectionNoisePeriod = true;
		OutParams.WindDirectionNoisePeriod = WindDirectionNoisePeriod;
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
	Params.bOverrideConstantForce = true;
	Params.ConstantForce = ConstantForce;
	Params.bOverrideSwayForce = true;
	Params.SwayForce = SwayForce;
	Params.bOverrideSwayPeriod = true;
	Params.SwayPeriod = SwayPeriod;
	Params.bOverrideSwayPhaseOffset = true;
	Params.SwayPhaseOffset = SwayPhaseOffset;
	Params.bOverrideRippleForce = true;
	Params.RippleForce = RippleForce;
	Params.bOverrideRipplePeriod = true;
	Params.RipplePeriod = RipplePeriod;
	Params.bOverrideRipplePhaseOffset = true;
	Params.RipplePhaseOffset = RipplePhaseOffset;
	Params.bOverrideRippleTipPhaseDelay = true;
	Params.RippleTipPhaseDelay = RippleTipPhaseDelay;
	Params.bOverrideStrengthCycleRange = true;
	Params.StrengthCycleRange = StrengthCycleRange;
	Params.bOverrideStrengthCyclePeriod = true;
	Params.StrengthCyclePeriod = StrengthCyclePeriod;
	Params.bOverrideStrengthCyclePhaseOffset = true;
	Params.StrengthCyclePhaseOffset = StrengthCyclePhaseOffset;
	Params.bOverrideRandomForce = true;
	Params.RandomForce = RandomForce;
	Params.bOverrideRandomForcePeriod = true;
	Params.RandomForcePeriod = RandomForcePeriod;
	Params.bOverrideWindDirectionNoiseAngle = true;
	Params.WindDirectionNoiseAngle = WindDirectionNoiseAngle;
	Params.bOverrideWindDirectionNoisePeriod = true;
	Params.WindDirectionNoisePeriod = WindDirectionNoisePeriod;
	Params.bOverrideTimeScale = true;
	Params.TimeScale = TimeScale;

	// consume前のPendingParamsがあれば上乗せし、Set直後・PreApply前のGetでも指定済みの値を返す（read-your-writes）。
	// PendingParamsはここでは消費しない（Resetしない）
	if (RuntimeState.IsValid())
	{
		FScopeLock Lock(&RuntimeState->Mutex);
		if (RuntimeState->PendingParams.IsSet())
		{
			MergeProceduralWindDynamicParams(Params, RuntimeState->PendingParams.GetValue());
		}
	}

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
		MergeProceduralWindDynamicParams(LocalRuntimeState->PendingParams.GetValue(), Params);
	}
	else
	{
		LocalRuntimeState->PendingParams = Params;
	}
}

// TimeScale を考慮したシミュレーション内時間を進める
void FKawaiiPhysics_ExternalForce_ProceduralWind::AdvanceWindTime(const float DeltaTime)
{
	EnsureRuntimeState()->Time += FMath::Max(DeltaTime, 0.0f) * TimeScale;
}

// 現在時刻のサンプルを計算して記録する（PreApply 以外の呼び出し元用の薄いラッパ）
void FKawaiiPhysics_ExternalForce_ProceduralWind::RecordScopeSample()
{
#if WITH_EDITOR
	if (!RuntimeState.IsValid())
	{
		return;
	}

	RecordScopeSample(RuntimeState->bPublisherWindDisabled
		                  ? FKawaiiPhysicsProceduralWindSample()
		                  : ComputeWindSample(RuntimeState->Time, 0.0f));
#endif
}

// 計算済みサンプルを記録する。PreApply は同フレームに求めた Sample を渡し、ComputeWindSample の二重計算を避ける
void FKawaiiPhysics_ExternalForce_ProceduralWind::RecordScopeSample(const FKawaiiPhysicsProceduralWindSample& Sample)
{
#if WITH_EDITOR
	if (!RuntimeState.IsValid())
	{
		return;
	}

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
#endif
}

// Publisher が共有する 13 項目だけ bOverride を残し、ノード個別の 5 項目は共有から外す
FKawaiiProceduralWindDynamicParams FKawaiiPhysics_ExternalForce_ProceduralWind::BuildSharedWindParams() const
{
	FKawaiiProceduralWindDynamicParams Params = BuildDynamicParamsSnapshot();
	Params.bOverrideIsEnabled = false;
	Params.bOverrideSwayPhaseOffset = false;
	Params.bOverrideRipplePhaseOffset = false;
	Params.bOverrideStrengthCyclePhaseOffset = false;
	Params.bOverrideTimeScale = false;
	return Params;
}

// Serial が更新された Publisher 状態を採用する
void FKawaiiPhysics_ExternalForce_ProceduralWind::ApplySharedWindState(
	const FKawaiiPhysicsSharedWindState& State,
	const uint64 Serial)
{
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = EnsureRuntimeState();
	// 初回採用時だけローカルの共有 13 項目を退避する。Shared を離れたときはこの値へ戻し、
	// Auto の Local フォールバックが「最後に受け取った共有値」ではなく authored 値で動くようにする
	if (!LocalRuntimeState->LocalSharedParamsBackup.IsSet())
	{
		LocalRuntimeState->LocalSharedParamsBackup = BuildSharedWindParams();
	}
	ApplyDynamicParams(State.Params);
	LocalRuntimeState->Time = State.Time;
	LocalRuntimeState->ActiveGust = State.ActiveGust;
	LocalRuntimeState->bPublisherWindDisabled = !State.bPublisherWindEnabled;
	LocalRuntimeState->CachedPublisherTimeScale = State.PublisherTimeScale;
	LocalRuntimeState->LastAppliedSharedSerial = Serial;
}

// Shared を離れるとき、ApplySharedWindState で退避しておいたローカルの共有 13 項目を書き戻す。
// 退避が無い（Shared を一度も採用していない）ときは何もしないので、Local 経路の値は変わらない
void FKawaiiPhysics_ExternalForce_ProceduralWind::RestoreLocalSharedParams()
{
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = EnsureRuntimeState();
	if (!LocalRuntimeState->LocalSharedParamsBackup.IsSet())
	{
		return;
	}

	ApplyDynamicParams(LocalRuntimeState->LocalSharedParamsBackup.GetValue());
	LocalRuntimeState->LocalSharedParamsBackup.Reset();
}

// 突風要求を PendingGust として積み、次回 PreApply で反映する。
// Shared の消費側でも呼び出しスレッドからは Entry を触らず、Worker の PreApply から
// SendPendingGustRequestsToSharedEntry() で転送する（ResolvedSource / SharedPublisherEntry は Worker 専用の領域のため）
void FKawaiiPhysics_ExternalForce_ProceduralWind::RequestGust(
	const float Strength, const float RiseTime, const float DecayTime, const float HoldTime)
{
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = EnsureRuntimeState();
	FScopeLock Lock(&LocalRuntimeState->Mutex);
	LocalRuntimeState->PendingGust = FKawaiiProceduralWindGustRequest{
		Strength,
		RiseTime,
		DecayTime,
		HoldTime
	};
}

// transient 用の突風はコピー元が Shared でも必ずローカルキューへ積む
void FKawaiiPhysics_ExternalForce_ProceduralWind::RequestLocalGust(
	const float Strength, const float RiseTime, const float DecayTime, const float HoldTime)
{
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = EnsureRuntimeState();
	WindSource = EKawaiiPhysicsProceduralWindSource::Local;
	// 突風はその時点の風に乗せるものなので、共有値をローカル値へ戻さず実効値のまま残して退避だけ捨てる
	LocalRuntimeState->LocalSharedParamsBackup.Reset();
	LocalRuntimeState->SharedPublisherEntry.Reset();
	LocalRuntimeState->ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
	LocalRuntimeState->ResolvedSharedTag = FGameplayTag();
	LocalRuntimeState->LastAppliedSharedSerial = 0;
	LocalRuntimeState->bPublisherWindDisabled = false;
	RequestGust(Strength, RiseTime, DecayTime, HoldTime);
}

// アクティブな突風の早期停止要求を PendingGustStop として積み、次回 PreApply で反映する
//（Shared の消費側での転送は RequestGust と同じく Worker の PreApply が行う）
void FKawaiiPhysics_ExternalForce_ProceduralWind::RequestGustStop(const float BlendOutTime)
{
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = EnsureRuntimeState();
	FScopeLock Lock(&LocalRuntimeState->Mutex);
	LocalRuntimeState->PendingGustStop = BlendOutTime;
}

// 溜まった突風要求を Shared Publisher Entry へ転送する。Worker（PreApply）からのみ呼ぶこと。
// Publisher が次の Update で消費して publish するため、Shared の全消費者へは 1〜2 フレーム後に届く
void FKawaiiPhysics_ExternalForce_ProceduralWind::SendPendingGustRequestsToSharedEntry()
{
	if (!RuntimeState.IsValid() || !RuntimeState->SharedPublisherEntry.IsValid())
	{
		return;
	}

	TOptional<FKawaiiProceduralWindGustRequest> Gust;
	TOptional<float> GustStop;
	{
		FScopeLock Lock(&RuntimeState->Mutex);
		Gust = MoveTemp(RuntimeState->PendingGust);
		GustStop = MoveTemp(RuntimeState->PendingGustStop);
		RuntimeState->PendingGust.Reset();
		RuntimeState->PendingGustStop.Reset();
	}

	// RuntimeState の Mutex を抜けてから Entry を触り、ロックの入れ子を作らない。
	// 起動 → 停止の順に積み、同フレームに両方来たときの停止優先をローカル経路と揃える
	if (Gust.IsSet())
	{
		const FKawaiiProceduralWindGustRequest& Request = Gust.GetValue();
		RuntimeState->SharedPublisherEntry->RequestGust(
			Request.Strength, Request.RiseTime, Request.DecayTime, Request.HoldTime);
	}
	if (GustStop.IsSet())
	{
		RuntimeState->SharedPublisherEntry->RequestGustStop(GustStop.GetValue());
	}
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
		LocalRuntimeState->ActiveGust.HoldTime = PendingGust.HoldTime;
		LocalRuntimeState->ActiveGust.bIsActive = true;
		LocalRuntimeState->PendingGust.Reset();
	}

	// gust 停止要求は起動要求の後に処理し、同フレームでは停止を優先する
	if (LocalRuntimeState->PendingGustStop.IsSet())
	{
		const float BlendOutTime = LocalRuntimeState->PendingGustStop.GetValue();
		if (LocalRuntimeState->ActiveGust.bIsActive)
		{
			if (BlendOutTime <= KINDA_SMALL_NUMBER)
			{
				LocalRuntimeState->ActiveGust.bIsActive = false;
			}
			else
			{
				const float CurrentGust = EvaluateActiveGust(LocalRuntimeState->ActiveGust, LocalRuntimeState->Time);
				LocalRuntimeState->ActiveGust.StartTime = LocalRuntimeState->Time;
				LocalRuntimeState->ActiveGust.Strength = CurrentGust;
				LocalRuntimeState->ActiveGust.RiseTime = 0.0f;
				LocalRuntimeState->ActiveGust.DecayTime = BlendOutTime;
				LocalRuntimeState->ActiveGust.HoldTime = 0.0f;
				LocalRuntimeState->ActiveGust.bIsActive = true;
			}
		}
		LocalRuntimeState->PendingGustStop.Reset();
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
	const float SafeSwayPeriod = FMath::Max(SwayPeriod, 0.01f);
	const float SafeRipplePeriod = FMath::Max(RipplePeriod, 0.01f);
	const float SafeRandomForcePeriod = FMath::Max(RandomForcePeriod, 0.01f);
	const float SafeStrengthCyclePeriod = FMath::Max(StrengthCyclePeriod, 0.01f);

	// 定常(Constant)成分
	Sample.Constant = ConstantForce;
	// 一斉揺れ(Sway)成分（周期的な押し引き）。SwayPhaseOffset で開始位相をずらせる（既定0で従来と同一）
	Sample.Sway = SwayForce * FMath::Sin(TwoPi * InTime / SafeSwayPeriod + FMath::DegreesToRadians(SwayPhaseOffset));
	// ボーン列に沿って伝わる波揺れ(Ripple)。InLengthRate（毛先方向の距離率）ぶんだけ位相をずらし、根元から毛先へ
	// 波が伝播しているように見せる
	Sample.Ripple = RippleForce * FMath::Sin(TwoPi * InTime / SafeRipplePeriod -
		FMath::DegreesToRadians(InLengthRate * RippleTipPhaseDelay) + FMath::DegreesToRadians(RipplePhaseOffset));
	// 低周波の強弱サイクル(StrengthCycle)変調。sin を [0,1] に正規化してから StrengthCycleRange の Min-Max 間を補間する
	Sample.StrengthCycle = FMath::Lerp(StrengthCycleRange.Min, StrengthCycleRange.Max,
		0.5f * (1.0f + FMath::Sin(TwoPi * InTime / SafeStrengthCyclePeriod + FMath::DegreesToRadians(StrengthCyclePhaseOffset))));
	// seeded smooth noise によるランダム成分。同一 Seed なら実行のたびに同じ揺らぎを再現する
	Sample.Random = RandomForce * SampleSmoothNoise(InTime / SafeRandomForcePeriod, Seed, 0);

	// アクティブな gust があれば加算
	if (RuntimeState.IsValid())
	{
		Sample.Gust = EvaluateActiveGust(RuntimeState->ActiveGust, InTime);
	}

	// 最終合成: (定常 + 一斉揺れ + 波揺れ) × 強弱サイクル + random + gust
	Sample.Total = (Sample.Constant + Sample.Sway + Sample.Ripple) * Sample.StrengthCycle +
		Sample.Random + Sample.Gust;
	return Sample;
}

// (Seed, GridIndex, Channel) から決定論的なハッシュ値を作る（FNV-1aベースのミックス + fmix32相当の追加撹拌）。
// RandomStream の内部状態を跨いで持ち回さず、都度この値から種を作ることで実行順序やスレッドに依存しない
// 再現性を持たせている
uint32 FKawaiiPhysics_ExternalForce_ProceduralWind::ComputeStableHash(const int32 Seed, const int32 GridIndex,
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

// グリッド点 GridIndex における疑似ランダム値 [-1, 1] を返す。ComputeStableHash を種にすることで、
// 呼び出し順序に関係なく同じ GridIndex なら常に同じ値になる
float FKawaiiPhysics_ExternalForce_ProceduralWind::SampleNoiseAt(const int32 GridIndex, const int32 Seed,
                                                                 const int32 Channel)
{
	FRandomStream RandomStream(ComputeStableHash(Seed, GridIndex, Channel));
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
	return FMath::Lerp(SampleNoiseAt(GridIndex, Seed, Channel),
	                   SampleNoiseAt(GridIndex + 1, Seed, Channel), SmoothAlpha);
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::Initialize(const FAnimationInitializeContext& Context)
{
	Super::Initialize(Context);

	// ノード初期化時に RuntimeState の中身を初期化し、前回の再生状態（Time/ActiveGust等）を引き継がない
	ResetRuntimeState();
}

namespace KawaiiPhysicsProceduralWindInternal
{
	bool IsSharedPublisherEntryLive(const FKawaiiPhysicsSharedPublisherEntry& Entry,
	                                const uint64 CurrentFrame, const uint64 MaxAgeFrames)
	{
		// claim 前の Entry（ProviderID 0）は LastPublishFrame も 0 なので、起動直後の MaxAge フレームの間だけ
		// 期限切れに見えない。無主 Entry を採用しないよう、provider が居ることも要求する
		const FKawaiiPhysicsSharedPublisherEntry::FProviderSnapshot Snapshot =
			Entry.ReadProviderSnapshot(CurrentFrame, MaxAgeFrames);
		return Snapshot.ProviderID != 0 && !Snapshot.bExpired;
	}
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::ResolveSharedWindSource(
	const FAnimNode_KawaiiPhysics& Node,
	const uint64 CurrentFrame)
{
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> LocalRuntimeState = EnsureRuntimeState();
	if (WindSource == EKawaiiPhysicsProceduralWindSource::Local)
	{
		LocalRuntimeState->ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
		LocalRuntimeState->SharedPublisherEntry.Reset();
		LocalRuntimeState->ResolvedSharedTag = FGameplayTag();
		return;
	}

	if (!SharedWindTag.IsValid())
	{
		// Shared を離れる経路なので、退避済みのローカル値があれば書き戻す
		RestoreLocalSharedParams();
		LocalRuntimeState->ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
		LocalRuntimeState->SharedPublisherEntry.Reset();
		LocalRuntimeState->ResolvedSharedTag = FGameplayTag();
		LocalRuntimeState->SharedResolveCountdown =
			FMath::Max(1, GetKawaiiPhysicsSharedPublisherAutoResolveInterval());
#if !UE_BUILD_SHIPPING
		if (!LocalRuntimeState->bSharedResolveWarningLogged)
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("ProceduralWind: Shared Wind Tag is None (Source: %s). Falling back to Local source."),
			       *UEnum::GetValueAsString(WindSource));
			LocalRuntimeState->bSharedResolveWarningLogged = true;
		}
#endif
		return;
	}

	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem = Node.GetSharedCollisionSubsystem();
	AActor* Owner = Node.GetSharedCollisionOwnerActor();
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> Entry;
	if (Subsystem && Owner)
	{
		Entry = Subsystem->FindSharedPublisherEntry(Owner, SharedWindTag);
	}

	const uint64 ProviderMaxAge =
		static_cast<uint64>(FMath::Max(0, GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge()));
	// 期限切れでないことに加えて claim 済み（ProviderID != 0）も要求する。作られただけの Entry は
	// 起動直後の MaxAge フレームの間だけ期限切れに見えないため、そこへ bind すると突風要求を捨ててしまう
	const bool bProviderAlive = Entry.IsValid() &&
		KawaiiPhysicsProceduralWindInternal::IsSharedPublisherEntryLive(*Entry, CurrentFrame, ProviderMaxAge);
	if (bProviderAlive)
	{
		LocalRuntimeState->SharedPublisherEntry = Entry;
		LocalRuntimeState->ResolvedSource = EKawaiiPhysicsProceduralWindSource::Shared;
		LocalRuntimeState->ResolvedSharedTag = SharedWindTag;
		LocalRuntimeState->SharedResolveFailures = 0;
		LocalRuntimeState->LastAppliedSharedSerial = 0;
		LocalRuntimeState->bPublisherWindDisabled = false;
#if !UE_BUILD_SHIPPING
		LocalRuntimeState->bSharedResolveWarningLogged = false;
#endif
		return;
	}

	// 解決に失敗して Local へ落ちる経路。ここも退避済みのローカル値があれば書き戻す
	RestoreLocalSharedParams();
	LocalRuntimeState->ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
	LocalRuntimeState->SharedPublisherEntry.Reset();
	LocalRuntimeState->ResolvedSharedTag = FGameplayTag();
	LocalRuntimeState->SharedResolveCountdown =
		FMath::Max(1, GetKawaiiPhysicsSharedPublisherAutoResolveInterval());
	++LocalRuntimeState->SharedResolveFailures;
	LocalRuntimeState->bPublisherWindDisabled = false;

#if !UE_BUILD_SHIPPING
	if (WindSource == EKawaiiPhysicsProceduralWindSource::Shared &&
		LocalRuntimeState->SharedResolveFailures >= 60 &&
		!LocalRuntimeState->bSharedResolveWarningLogged)
	{
		UE_LOG(LogKawaiiPhysics, Warning,
		       TEXT("ProceduralWind: Shared Publisher for Shared Wind Tag '%s' was not found. Falling back to Local source."),
		       *SharedWindTag.ToString());
		LocalRuntimeState->bSharedResolveWarningLogged = true;
	}
#endif
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

	if (WindSource == EKawaiiPhysicsProceduralWindSource::Local)
	{
		// Shared → Local へ切り替えた直後のフレームでローカル値へ戻す。
		// 退避が無い通常の Local フレームでは関数呼び出し自体を省き、ConsumePendingRequests より先に呼んで
		// 同フレームの BP 上書きが復元値を上書きできるようにする
		if (RuntimeState->LocalSharedParamsBackup.IsSet())
		{
			RestoreLocalSharedParams();
		}
		// 他スレッドからのランタイム上書き・gust要求を取り込む
		ConsumePendingRequests();
		RuntimeState->ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
		RuntimeState->SharedPublisherEntry.Reset();
		RuntimeState->bPublisherWindDisabled = false;
		AdvanceWindTime(Node.GetStepDeltaTime());
	}
	else
	{
		// Persona の in-place 同期（CopyScriptStruct）では operator= が走らず Tag だけが差し替わるため、
		// 解決に使った Tag と食い違ったら Entry を捨てて即座に解決し直す
		if (RuntimeState->SharedPublisherEntry.IsValid() && !(RuntimeState->ResolvedSharedTag == SharedWindTag))
		{
			// Tag が変わった＝別の Publisher を読み直すので、まず今の Shared を離れてローカル値へ戻す
			RestoreLocalSharedParams();
			RuntimeState->SharedPublisherEntry.Reset();
			RuntimeState->ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
			RuntimeState->ResolvedSharedTag = FGameplayTag();
			RuntimeState->LastAppliedSharedSerial = 0;
			RuntimeState->SharedResolveCountdown = 0;
		}

		if (!RuntimeState->SharedPublisherEntry.IsValid())
		{
			if (--RuntimeState->SharedResolveCountdown <= 0)
			{
				ResolveSharedWindSource(Node, GFrameCounter);
			}
		}

		if (RuntimeState->SharedPublisherEntry.IsValid())
		{
			const uint64 ProviderMaxAge =
				static_cast<uint64>(FMath::Max(0, GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge()));
			if (RuntimeState->SharedPublisherEntry->IsExpired(GFrameCounter, ProviderMaxAge))
			{
				// 期限切れ: Entry を手放し、このフレームは下の Entry 無し経路で Local と同じ扱い（突風もローカル起動）にする。
				// 共有 13 項目も採用前のローカル値へ戻し、フォールバックが authored 値で動くようにする
				RestoreLocalSharedParams();
				RuntimeState->SharedPublisherEntry.Reset();
				RuntimeState->ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
				RuntimeState->ResolvedSharedTag = FGameplayTag();
				RuntimeState->SharedResolveCountdown =
					FMath::Max(1, GetKawaiiPhysicsSharedPublisherAutoResolveInterval());
				++RuntimeState->SharedResolveFailures;
				RuntimeState->bPublisherWindDisabled = false;
			}
		}

		if (RuntimeState->SharedPublisherEntry.IsValid())
		{
			// 突風要求は Worker のここでだけ Entry へ転送し、PendingParams はローカルへ取り込む
			SendPendingGustRequestsToSharedEntry();
			ConsumePendingRequests();

			if (RuntimeState->SharedPublisherEntry->GetPublishSerial() != RuntimeState->LastAppliedSharedSerial)
			{
				// Serial と State は同じロック区間で読む（別々に読むと間に publish が入って State と Serial がずれる）
				FKawaiiPhysicsSharedWindState State;
				const uint64 ReadSerial = RuntimeState->SharedPublisherEntry->ReadWindState(State);
				ApplySharedWindState(State, ReadSerial);
			}
			else
			{
				// Publisher は無効の間クロックを止めて publish するので、外挿も同じ規則で止める
				//（無効の Publisher が停止→再開したときに消費側だけ先へ進んで巻き戻らないため）
				// warm-up は 1 回の評価内で PreApply を WarmUpFrames 回呼ぶが Publisher は 1 フレーム分しか
				// 進まないので、共有クロックは warm-up 中は止めておく（進めると次の serial 採用で巻き戻る）。
				// warm-up 後の通常の 1 回で通常どおり外挿する
				if (!RuntimeState->bPublisherWindDisabled && !Node.IsWarmingUp())
				{
					RuntimeState->Time += Node.GetStepDeltaTime() * RuntimeState->CachedPublisherTimeScale;
				}
			}
		}
		else
		{
			// Entry が無い間は Local と同じ扱いで、突風もローカルで起動する
			ConsumePendingRequests();
			if (RuntimeState->ResolvedSource == EKawaiiPhysicsProceduralWindSource::Local)
			{
				AdvanceWindTime(Node.GetStepDeltaTime());
			}
		}
	}

	// ボーンに依存しない成分（Constant/Sway/StrengthCycle/Random/Gust）はここで1回だけ計算してキャッシュする。
	// Apply は全ボーンで呼ばれるため、毎ボーン再計算しないための最適化（Rippleのみボーン依存で Apply 側が再計算する）
	const FKawaiiPhysicsProceduralWindSample Sample =
		RuntimeState->bPublisherWindDisabled
			? FKawaiiPhysicsProceduralWindSample()
			: ComputeWindSample(RuntimeState->Time, 0.0f);
	RuntimeState->CachedSinesWithoutRipple = Sample.Constant + Sample.Sway;
	RuntimeState->CachedStrengthCycle = Sample.StrengthCycle;
	RuntimeState->CachedRandom = Sample.Random;
	RuntimeState->CachedGust = Sample.Gust;

	// 風向きに円錐状の揺らぎを加える（WindDirectionNoiseAngle>0のときのみ）。X/Y で異なる Channel を使い、
	// 独立した2軸のノイズ系列にする
	if (RuntimeState->bPublisherWindDisabled)
	{
		RuntimeState->CachedWindVector = FVector::ZeroVector;
	}
	else
	{
		const FVector BaseWindDirection = SafeDirectionOrForward(WindDirection);
		FVector NoisyWindDirection = BaseWindDirection;
		if (WindDirectionNoiseAngle > 0.0f)
		{
			const float SafeWindDirectionNoisePeriod = FMath::Max(WindDirectionNoisePeriod, 0.01f);
			const float DirectionNoiseU = RuntimeState->Time / SafeWindDirectionNoisePeriod;
			const float NoiseX = SampleSmoothNoise(DirectionNoiseU, Seed, 1);
			const float NoiseY = SampleSmoothNoise(DirectionNoiseU, Seed, 2);
			NoisyWindDirection = ApplyConeNoiseToDirection(BaseWindDirection, NoiseX, NoiseY, WindDirectionNoiseAngle);
		}
		// シミュレーション空間へ変換してフレーム単位でキャッシュ（BoneSpace指定時は Apply 側で更にボーンのTMを掛ける）
		RuntimeState->CachedWindVector = ConvertExternalForceToSimulationSpace(Node, PoseContext, NoisyWindDirection);
	}

	// 上で求めた Sample をそのまま渡し、Wind Scope 記録での再計算を避ける
	RecordScopeSample(Sample);
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                        FComponentSpacePoseContext& PoseContext,
                                                        const FTransform& BoneTM)
{
	if (!CanApply(Bone) || !RuntimeState.IsValid() || RuntimeState->bPublisherWindDisabled)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_ProceduralWind_Apply);

	// Ripple はボーンごとの LengthRateFromRoot に依存するためここで個別に計算し、PreApply でキャッシュした
	// 他成分と合算する。式は ComputeWindSample の Sample.Ripple 計算と一致させること
	const float Ripple = RippleForce * FMath::Sin(TwoPi * RuntimeState->Time / FMath::Max(RipplePeriod, 0.01f) -
		FMath::DegreesToRadians(Bone.LengthRateFromRoot * RippleTipPhaseDelay) + FMath::DegreesToRadians(RipplePhaseOffset));
	const float Total = (RuntimeState->CachedSinesWithoutRipple + Ripple) * RuntimeState->CachedStrengthCycle +
		RuntimeState->CachedRandom + RuntimeState->CachedGust;

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot);
	}

	// 基底の RandomForceScaleRange / RandomizedForceScale は本外力では意図的に無視する
	// （ランダム性は Seed 管理の Random 系列に一本化。bSupportsRandomForceScaleRange=false により非表示かつ PreApply の乱数化も無効）。
	// BoneSpace 指定時はキャッシュ済みの風ベクトルに各ボーンのTMを掛けて向きをボーンローカルへ変換する
	if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		const FVector BoneForce = BoneTM.TransformVector(RuntimeState->CachedWindVector);
		Bone.Location += BoneForce * Total * ForceRate * Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce * Total * ForceRate);
#endif
	}
	else
	{
		Bone.Location += RuntimeState->CachedWindVector * Total * ForceRate *
			Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName,
		                 RuntimeState->CachedWindVector * Total * ForceRate);
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

	// Ripple/Total の再計算は Apply と同じ式（ComputeWindSample の Sample.Ripple と一致させること）
	const float Ripple = RippleForce * FMath::Sin(TwoPi * RuntimeState->Time / FMath::Max(RipplePeriod, 0.01f) -
		FMath::DegreesToRadians(ModifyBone.LengthRateFromRoot * RippleTipPhaseDelay) +
		FMath::DegreesToRadians(RipplePhaseOffset));
	const float Total = (RuntimeState->CachedSinesWithoutRipple + Ripple) * RuntimeState->CachedStrengthCycle +
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
