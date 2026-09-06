// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "ExternalForces/KawaiiPhysicsExternalForce.h"

#include "GameplayTagContainer.h"
#include "HAL/CriticalSection.h"
#include "Math/Interval.h"
#include "Math/RandomStream.h"
#include "Misc/Optional.h"
#include "Templates/SharedPointer.h"

#include "KawaiiPhysicsExternalForce_ProceduralWind.generated.h"

struct FKawaiiProceduralWindGustRequest
{
	float Strength = 0.0f;
	float RiseTime = 0.0f;
	float DecayTime = 0.0f;
	float HoldTime = 0.0f;
};

struct FKawaiiProceduralWindActiveGust
{
	float StartTime = 0.0f;
	float Strength = 0.0f;
	float RiseTime = 0.0f;
	float DecayTime = 0.0f;
	float HoldTime = 0.0f;
	bool bIsActive = false;
};

struct FKawaiiPhysicsProceduralWindSample
{
	float Constant = 0.0f;
	float Sway = 0.0f;
	float Ripple = 0.0f;
	float StrengthCycle = 1.0f;
	float Random = 0.0f;
	float Gust = 0.0f;
	float Total = 0.0f;
};

struct FKawaiiProceduralWindScopeSample
{
	float Time = 0.0f;
	FKawaiiPhysicsProceduralWindSample Sample;
};

UENUM(BlueprintType)
enum class EKawaiiProceduralWindParameterMode : uint8
{
	Simple,
	Advanced,
};

/**
 * ProceduralWind の風ソース / Wind source for ProceduralWind
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsProceduralWindSource : uint8
{
	/** このノードの ProceduralWind 自身が風を計算する（従来どおり） / This ProceduralWind computes the wind on its own (legacy behavior). */
	Local UMETA(DisplayName = "Local"),
	/** 同じ Actor ファミリーの Kawaii Physics Shared Publisher（同じ Shared Wind Tag）が配る風パラメータ・位相・突風を使う。Publisher が無い間はローカル積算で動き続ける / Uses the wind parameters, phase and gusts published by the Shared Publisher with the same Shared Wind Tag in this actor family. Keeps running locally while no publisher exists. */
	Shared UMETA(DisplayName = "Shared"),
	/** Publisher があれば Shared、無ければ Local として動く。Publisher の出現・消失に追従する / Shared while a publisher exists, otherwise Local. Follows the publisher appearing or disappearing. */
	Auto UMETA(DisplayName = "Auto"),
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideIsEnabled = false;

	/** 外力の有効状態 / External force enabled state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bIsEnabled = true;

	/** WindDirection を上書きする / Override WindDirection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideWindDirection = false;

	/** 風方向。内部で正規化するため非正規化でも可 / Wind direction. Non-normalized values are allowed because this is normalized internally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	FVector WindDirection = FVector::ForwardVector;

	/** ConstantForce を上書きする / Override ConstantForce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideConstantForce = false;

	/** 定常(Constant)風力 / Constant wind force. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float ConstantForce = 0.0f;

	/** SwayForce を上書きする / Override SwayForce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideSwayForce = false;

	/** 一斉揺れ(Sway)風力 / Sway wind force. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float SwayForce = 0.0f;

	/** SwayPeriod を上書きする / Override SwayPeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideSwayPeriod = false;

	/** 一斉揺れ(Sway)周期（秒） / Sway period, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float SwayPeriod = 1.0f;

	/** SwayPhaseOffset を上書きする / Override SwayPhaseOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideSwayPhaseOffset = false;

	/** 一斉揺れ(Sway)の開始位相 / Sway start phase offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="Degrees"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float SwayPhaseOffset = 0.0f;

	/** RippleForce を上書きする / Override RippleForce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideRippleForce = false;

	/** 波揺れ(Ripple)の振幅 / Spatial wave amplitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float RippleForce = 0.0f;

	/** RipplePeriod を上書きする / Override RipplePeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideRipplePeriod = false;

	/** 波揺れ(Ripple)の周期（秒） / Spatial wave period, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float RipplePeriod = 1.0f;

	/** RipplePhaseOffset を上書きする / Override RipplePhaseOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideRipplePhaseOffset = false;

	/** 波揺れ(Ripple)の位相 / Spatial wave phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="Degrees"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float RipplePhaseOffset = 0.0f;

	/** RippleTipPhaseDelay を上書きする / Override RippleTipPhaseDelay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideRippleTipPhaseDelay = false;

	/** 毛先 r=1 での位相遅れ量 / Phase delay at tip r=1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="Degrees"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float RippleTipPhaseDelay = 180.0f;

	/** StrengthCycleRange を上書きする / Override StrengthCycleRange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideStrengthCycleRange = false;

	/** 強弱サイクル(StrengthCycle)倍率範囲 / StrengthCycle multiplier range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	FFloatInterval StrengthCycleRange = FFloatInterval(1.0f, 1.0f);

	/** StrengthCyclePeriod を上書きする / Override StrengthCyclePeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideStrengthCyclePeriod = false;

	/** 強弱サイクル(StrengthCycle)周期（秒） / StrengthCycle period, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float StrengthCyclePeriod = 10.0f;

	/** StrengthCyclePhaseOffset を上書きする / Override StrengthCyclePhaseOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideStrengthCyclePhaseOffset = false;

	/** 強弱サイクル(StrengthCycle)位相 / StrengthCycle phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="Degrees"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float StrengthCyclePhaseOffset = 0.0f;

	/** RandomForce を上書きする / Override RandomForce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideRandomForce = false;

	/** ランダム風力 / Random wind force. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float RandomForce = 0.0f;

	/** RandomForcePeriod を上書きする / Override RandomForcePeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideRandomForcePeriod = false;

	/** ランダム風力の周期（秒） / Random wind period, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float RandomForcePeriod = 0.5f;

	/** WindDirectionNoiseAngle を上書きする / Override WindDirectionNoiseAngle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideWindDirectionNoiseAngle = false;

	/** 方向揺らぎの円錐半角 / Cone half-angle for directional noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="Degrees"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float WindDirectionNoiseAngle = 0.0f;

	/** WindDirectionNoisePeriod を上書きする / Override WindDirectionNoisePeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideWindDirectionNoisePeriod = false;

	/** 方向揺らぎの周期（秒） / Period for directional noise, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float WindDirectionNoisePeriod = 1.0f;

	/** TimeScale を上書きする / Override TimeScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	bool bOverrideTimeScale = false;

	/** 時間スケール / Time scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kawaii Physics|ExternalForce|Procedural Wind|DynamicParams")
	float TimeScale = 1.0f;
};

/**
* Source の bOverride が true の項目だけを Target に上書きする。
* Copies only the fields whose bOverride is true from Source into Target.
*/
KAWAIIPHYSICS_API void MergeProceduralWindDynamicParams(FKawaiiProceduralWindDynamicParams& Target,
                                                         const FKawaiiProceduralWindDynamicParams& Source);

struct FKawaiiPhysicsSharedPublisherEntry;
struct FKawaiiPhysicsSharedWindState;

namespace KawaiiPhysicsProceduralWindInternal
{
	/**
	 * 消費側が Shared Publisher Entry を採用してよいか（provider が claim 済みで、かつ期限切れでない）を判定します。
	 * Returns whether a consumer may adopt a Shared Publisher entry (claimed by a provider and not expired).
	 */
	KAWAIIPHYSICS_API bool IsSharedPublisherEntryLive(const FKawaiiPhysicsSharedPublisherEntry& Entry,
	                                                  uint64 CurrentFrame, uint64 MaxAgeFrames);
}

struct FKawaiiProceduralWindRuntimeState
{
	FCriticalSection Mutex;
	TOptional<FKawaiiProceduralWindDynamicParams> PendingParams;
	TOptional<FKawaiiProceduralWindGustRequest> PendingGust;
	TOptional<float> PendingGustStop;

	float Time = 0.0f;
	FKawaiiProceduralWindActiveGust ActiveGust;

	float CachedSinesWithoutRipple = 0.0f;
	float CachedStrengthCycle = 1.0f;
	float CachedRandom = 0.0f;
	float CachedGust = 0.0f;
	FVector CachedWindVector = FVector::ZeroVector;

	// 以下の共有風状態は PreApply を回す Worker だけが読み書きする。Mutex の保護対象には含めない。
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> SharedPublisherEntry;
	// SharedPublisherEntry の解決に使った Shared Wind Tag（Worker 専用） / Shared Wind Tag used to resolve SharedPublisherEntry (worker only)
	FGameplayTag ResolvedSharedTag;
	uint64 LastAppliedSharedSerial = 0;
	// Publisher が止まっていた間の時間は Publisher が再開時にまとめて進めるため、採用時に Time は巻き戻らない
	// The publisher advances the time it spent stalled in one step when it resumes, so adopting its Time never rewinds this clock.
	float CachedPublisherTimeScale = 1.0f;
	// true の間は Publisher と同じ規則で Time の外挿も止める（無効の Publisher が停止→再開しても位相が巻き戻らない）
	// While true the consumer also stops extrapolating Time, matching the publisher, so the phase never rewinds when a disabled publisher stalls and resumes.
	bool bPublisherWindDisabled = false;
	EKawaiiPhysicsProceduralWindSource ResolvedSource = EKawaiiPhysicsProceduralWindSource::Local;
	int32 SharedResolveCountdown = 0;
	int32 SharedResolveFailures = 0;
	// Shared 採用前のローカル共有 13 項目。Shared を離れるときに復元する。Worker 専用
	// Local values of the 13 shared fields captured before adopting Shared. Restored when leaving Shared. Worker only.
	TOptional<FKawaiiProceduralWindDynamicParams> LocalSharedParamsBackup;
#if !UE_BUILD_SHIPPING
	bool bSharedResolveWarningLogged = false;
#endif

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
* 合成式: Total = (Constant + Sway + Ripple) × StrengthCycle + Random + Gust（Gust は StartProceduralWindGust API から発生）
* Parametric synthesized wind. This does not depend on UE WindDirectionalSource.
* Composition: Total = (Constant + Sway + Ripple) x StrengthCycle + Random + Gust (Gust is triggered via the StartProceduralWindGust API).
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
	* 風の計算元。Shared のとき、共有 13 項目（Wind Direction 〜 Random Force Period）は Publisher の値で毎フレーム上書きされ、このノードの値は無視される。Phase Offset / Seed / Time Scale / Enabled / ボーンフィルタ / 空間はこのノードの値のまま。Auto は Local フォールバック時にだけこのノードの共有 13 項目を使う
	* Wind source. In Shared mode, the 13 shared fields (Wind Direction through Random Force Period) are overwritten every frame from the Publisher and this node's values are ignored. Phase Offset / Seed / Time Scale / Enabled / bone filters / space stay on this node. Auto uses this node's 13 shared fields only during Local fallback.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|ExternalForce|Procedural Wind", meta = (DisplayName = "Wind Source", DisplayPriority = 1))
	EKawaiiPhysicsProceduralWindSource WindSource = EKawaiiPhysicsProceduralWindSource::Local;

	/**
	* Shared / Auto のときに参照する Shared Publisher の Shared Group Tag。Publisher 側の Shared Group Tag と一致させる
	* Shared Group Tag of the Shared Publisher used in Shared / Auto mode. Must match the publisher's Shared Group Tag.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|ExternalForce|Procedural Wind", meta = (DisplayName = "Shared Wind Tag", DisplayPriority = 1, EditCondition = "WindSource != EKawaiiPhysicsProceduralWindSource::Local"))
	FGameplayTag SharedWindTag;

	/**
	* パラメータの表示モード。Simple は入門用の最小セットのみ表示し、Advanced で全パラメータを表示。非表示のパラメータも値は有効なまま
	* Parameter display mode. Simple shows only the starter set, while Advanced shows every parameter. Hidden parameters remain active.
	*/
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=2), Category="Kawaii Physics|ExternalForce|Procedural Wind")
	EKawaiiProceduralWindParameterMode ParameterMode = EKawaiiProceduralWindParameterMode::Simple;

	/**
	* 風の吹く方向。BP からは SetExternalForceVectorProperty("WindDirection") で変更可能。内部で正規化するため非正規化でも可
	* Direction the wind blows. Controllable from BP via SetExternalForceVectorProperty("WindDirection"). Non-normalized values are allowed because this is normalized internally.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=3, PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	FVector WindDirection = FVector::ForwardVector;

	/**
	* WindDirection 自体をこの円錐半角の範囲内で揺らし、向きの単調さを消す
	* Wobbles WindDirection itself within this cone half-angle, breaking directional monotony.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=4, Units="Degrees", ClampMin=0, UIMin=0, UIMax=90,
		PinHiddenByDefault, EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float WindDirectionNoiseAngle = 0.0f;

	/**
	* WindDirection の揺らぎが変化する周期（秒）
	* Period of the wind direction wander, in seconds.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=5, ClampMin=0.01, UIMin=0.01, UIMax=10,
		PinHiddenByDefault, Units="s", EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float WindDirectionNoisePeriod = 1.0f;

	/**
	* この外力内の時間の進み。0で凍結、2で倍速
	* Time scale of this force. 0 freezes, 2 doubles speed.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=21, ClampMin=0, UIMin=0, UIMax=3, PinHiddenByDefault,
		EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float TimeScale = 1.0f;

	/**
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=22,
		EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/**
	* 常に一定で掛かる風の強さ。上げるとボーンが風下へ流されたままになる。Wind Scope の Constant 系列に対応
	* Constant wind strength that always applies. Higher values keep bones pushed downwind. Corresponds to the Constant series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=6, ClampMin=0, UIMin=0, UIMax=50, PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float ConstantForce = 0.0f;

	/**
	* 全ボーンが一斉に同位相で揺れる sin 波の振幅。Ripple との違いは波が伝播しないこと。Wind Scope の Sway 系列に対応
	* Amplitude of the sine wave that sways all bones together in the same phase. Unlike Ripple, the wave does not propagate. Corresponds to the Sway series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=7, ClampMin=0, UIMin=0, UIMax=50, PinHiddenByDefault,
		EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float SwayForce = 0.0f;

	/**
	* 一斉揺れ(Sway)の周期（秒）。短いほど速く揺れる。Wind Scope の Sway 系列に対応
	* Period of the synchronized sway, in seconds. Shorter values sway faster. Corresponds to the Sway series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=8, ClampMin=0.01, UIMin=0.01, UIMax=10,
		PinHiddenByDefault, Units="s", EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float SwayPeriod = 1.0f;

	/**
	* 一斉揺れ(Sway)の開始位相オフセット。複数キャラや複数外力で揺れのタイミングを分散する用途。Wind Scope の Sway 系列に対応
	* Start phase offset of the synchronized sway. Use to stagger sway timing across multiple characters or external forces. Corresponds to the Sway series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=9, Units="Degrees", UIMin=-360, UIMax=360,
		PinHiddenByDefault, EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float SwayPhaseOffset = 0.0f;

	/**
	* 根元から毛先へ伝播する波揺れ(Ripple)の振幅。伝播の要は RippleTipPhaseDelay で、0 だと Sway と同じ動きになる。Wind Scope の Ripple 系列に対応
	* Amplitude of the wave that travels from root to tip. RippleTipPhaseDelay drives the propagation; at 0 the motion matches Sway. Corresponds to the Ripple series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=10, ClampMin=0, UIMin=0, UIMax=50, PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RippleForce = 0.0f;

	/**
	* 波揺れ(Ripple)の周期（秒）。Wind Scope の Ripple 系列に対応
	* Period of the traveling wave, in seconds. Corresponds to the Ripple series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=11, ClampMin=0.01, UIMin=0.01, UIMax=10,
		PinHiddenByDefault, Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RipplePeriod = 1.0f;

	/**
	* 波揺れ(Ripple)の開始位相オフセット。複数キャラや複数外力で波のタイミングを分散する用途。Wind Scope の Ripple 系列に対応
	* Start phase offset of the traveling wave. Use to stagger wave timing across multiple characters or external forces. Corresponds to the Ripple series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=12, Units="Degrees", UIMin=-360, UIMax=360,
		PinHiddenByDefault, EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RipplePhaseOffset = 0.0f;

	/**
	* 毛先（LengthRate=1）での位相遅れ角。伝播の要で、0 だと Sway と同じ動きになる。波が根元から毛先へ走り抜ける時間 = RipplePeriod × (TipPhaseDelay/360°)。負値で毛先→根元へ逆走。Wind Scope の Ripple 系列に対応
	* Phase delay at the tip (LengthRate=1). This drives the propagation; at 0 the motion matches Sway. Travel time from root to tip = RipplePeriod x (TipPhaseDelay/360deg). Negative values make the wave travel from tip to root. Corresponds to the Ripple series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=13, Units="Degrees", UIMin=0, UIMax=720,
		PinHiddenByDefault), Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RippleTipPhaseDelay = 180.0f;

	/**
	* 風の基本波形 (Constant+Sway+Ripple) に掛かる倍率の下限(Min)〜上限(Max)。周期的にこの範囲をうねり凪↔強風の緩急を作る。Random/Gust には適用されない。Min=Max=1 で無効
	* Min-to-Max multiplier range applied to the base waveform (Constant+Sway+Ripple). It periodically swells within this range to create calm-to-strong dynamics. Not applied to Random/Gust. Disabled when Min=Max=1.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=14, ClampMin=0, UIMin=0, UIMax=3, PinHiddenByDefault,
		EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	FFloatInterval StrengthCycleRange = FFloatInterval(1.0f, 1.0f);

	/**
	* 強弱サイクル(StrengthCycle)の周期（秒）。長いほどゆったりうねる。Wind Scope の StrengthCycle 系列に対応
	* Period of the StrengthCycle modulation, in seconds. Longer is slower. Corresponds to the StrengthCycle series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=16, ClampMin=0.01, UIMin=0.01, UIMax=60,
		PinHiddenByDefault, Units="s", EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float StrengthCyclePeriod = 10.0f;

	/**
	* 強弱サイクル(StrengthCycle)の開始位相オフセット。複数キャラや複数外力でうねりのタイミングを分散する用途。Wind Scope の StrengthCycle 系列に対応
	* Start phase offset of the StrengthCycle modulation. Use to stagger modulation timing across multiple characters or external forces. Corresponds to the StrengthCycle series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=17, Units="Degrees", UIMin=-360, UIMax=360,
		PinHiddenByDefault, EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float StrengthCyclePhaseOffset = 0.0f;

	/**
	* 不規則な強さの揺らぎ（滑らかなノイズ）の強さ。自然なランダム感を足す。Wind Scope の Random 系列に対応
	* Strength of the irregular fluctuation (smooth noise). Adds natural randomness. Corresponds to the Random series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=18, ClampMin=0, UIMin=0, UIMax=50, PinHiddenByDefault),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RandomForce = 0.0f;

	/**
	* 揺らぎが変化する周期（秒）。短いほど細かく震える。Wind Scope の Random 系列に対応
	* Period at which the fluctuation changes, in seconds. Shorter values jitter faster. Corresponds to the Random series in Wind Scope.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=19, ClampMin=0.01, UIMin=0.01, UIMax=5,
		PinHiddenByDefault, Units="s"), Category="Kawaii Physics|ExternalForce|Procedural Wind")
	float RandomForcePeriod = 0.5f;

	/**
	* この外力内の全ランダム要素（Random 系列と WindDirectionNoise）に共通のシード。同じ値なら同じ揺らぎを再現できる。PIE中のライブ反映対象外（次回再生から反映）
	* Seed shared by every random element in this force (the Random series and WindDirectionNoise). Same seed reproduces the same fluctuation. Not live-updated during PIE; takes effect from the next play session.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=20,
		EditCondition="ParameterMode == EKawaiiProceduralWindParameterMode::Advanced", EditConditionHides),
		Category="Kawaii Physics|ExternalForce|Procedural Wind")
	int32 Seed = 0;

	TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> RuntimeState;

	void ResetRuntimeState();
	TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> EnsureRuntimeState();
	void ApplyDynamicParams(const FKawaiiProceduralWindDynamicParams& Params);
	void RequestDynamicParams(const FKawaiiProceduralWindDynamicParams& Params);
	// 指定プロパティ名に対応する項目だけ bOverride を立てた DynamicParams を作る（未対応名なら false） / Builds DynamicParams overriding only the named property (false if unmapped)
	bool BuildDynamicParamsForProperty(FName PropertyName, FKawaiiProceduralWindDynamicParams& OutParams) const;
	// 全項目の bOverride を立てた現在値スナップショットを作る / Builds a snapshot of current values with every override flag set
	FKawaiiProceduralWindDynamicParams BuildDynamicParamsSnapshot() const;
	// TimeScale を使って ProceduralWind の時刻を進める / Advances ProceduralWind time using TimeScale
	void AdvanceWindTime(float DeltaTime);
	// Wind Scope 用の現在サンプルを計算して記録する / Computes and records the current sample for Wind Scope
	void RecordScopeSample();
	// 計算済みサンプルを Wind Scope 用に記録する（PreApply での二重計算を避ける） / Records an already computed sample for Wind Scope (avoids recomputing it in PreApply)
	void RecordScopeSample(const FKawaiiPhysicsProceduralWindSample& Sample);
	// 共有 13 項目だけを上書き対象にしたスナップショットを作る / Builds a snapshot overriding only the 13 shared fields
	FKawaiiProceduralWindDynamicParams BuildSharedWindParams() const;
	// Publisher から読んだ共有風状態を Serial 単位で適用する / Applies shared wind state read from the Publisher for a Serial
	void ApplySharedWindState(const FKawaiiPhysicsSharedWindState& State, uint64 Serial);
	void RequestGust(float Strength, float RiseTime, float DecayTime, float HoldTime = 0.0f);
	// transient 突風用に Local へ戻してから突風を要求する / Forces Local source and requests a gust for transient gusts
	void RequestLocalGust(float Strength, float RiseTime, float DecayTime, float HoldTime = 0.0f);
	// 現在のガストを指定時間でフェードアウト停止する
	void RequestGustStop(float BlendOutTime);
	void ConsumePendingRequests();

	FKawaiiPhysicsProceduralWindSample ComputeWindSample(float InTime, float InLengthRate = 0.0f) const;
	static uint32 ComputeStableHash(int32 Seed, int32 GridIndex, int32 Channel);
	static float SampleNoiseAt(int32 GridIndex, int32 Seed, int32 Channel);
	static float SampleSmoothNoise(float U, int32 Seed, int32 Channel = 0);

	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;

private:
	// Shared / Auto の共有風 Entry を解決する / Resolves the shared wind Entry for Shared / Auto
	void ResolveSharedWindSource(const FAnimNode_KawaiiPhysics& Node, uint64 CurrentFrame);
	// Shared を離れるときに、退避しておいたローカルの共有 13 項目を書き戻す / Restores the backed-up local values of the 13 shared fields when leaving Shared
	void RestoreLocalSharedParams();
	// 溜まった突風要求を Worker から Shared Publisher Entry へ転送する / Sends the queued gust requests to the Shared Publisher Entry from the worker
	void SendPendingGustRequestsToSharedEntry();

#if WITH_EDITOR
public:
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
