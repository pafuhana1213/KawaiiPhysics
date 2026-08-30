// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsTypes.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Misc/EngineVersionComparison.h"

#include "AnimNotifyState_KawaiiPhysicsSettingsMultiplier.generated.h"

UENUM(BlueprintType)
enum class EKawaiiPhysicsSettingsMultiplierWeightSource : uint8
{
	/** BlendIn/BlendOut の台形。NotifyTick の実時間を累積するため PlayRate≠1・逆再生・スクラブでは区間とズレる / BlendIn/BlendOut trapezoid. Accumulates NotifyTick real time, so it can drift from the section with PlayRate != 1, reverse playback, or scrubbing. */
	Envelope,
	/** アニメカーブ値を重みにする。PlayRate/スクラブに正確 / Uses an animation curve value as weight. Accurate for PlayRate and scrubbing. */
	Curve,
};

/**
 * NotifyState 区間中、KawaiiPhysics ノードの物理設定へ倍率を適用する。重みは Envelope（区間長に対する BlendIn/BlendOut の台形、実時間ベース）またはアニメカーブで決める。
 * 5.7 以前では Component のみで再生対象を識別し、同一イベントの重なりは ActiveCount で統合して全ての End が来るまで保持する。
 * Applies multipliers to KawaiiPhysics node physics settings during the NotifyState section. Weight is driven by Envelope (BlendIn/BlendOut trapezoid over section length, real-time based) or an animation curve.
 * On 5.7 and earlier, only Component identifies the play target; overlaps of the same event are merged by ActiveCount until all Ends arrive.
 */
UCLASS(Blueprintable, meta = (DisplayName = "KawaiiPhysics: Settings Multiplier"))
class KAWAIIPHYSICS_API UAnimNotifyState_KawaiiPhysicsSettingsMultiplier : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UAnimNotifyState_KawaiiPhysicsSettingsMultiplier(const FObjectInitializer& ObjectInitializer);

	/** Notify トラックに表示する名前を返す / Returns the name shown on the notify track. */
	virtual FString GetNotifyName_Implementation() const override;

	/** 区間開始時に物理設定倍率を開始する / Starts physics settings multipliers when the state begins. */
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	                         const FAnimNotifyEventReference& EventReference) override;

	/** 区間中に重みを更新する / Updates the multiplier weight while the state is active. */
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime,
	                        const FAnimNotifyEventReference& EventReference) override;

	/** 区間終了時に物理設定倍率を停止する / Stops physics settings multipliers when the state ends. */
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                       const FAnimNotifyEventReference& EventReference) override;

public:
	/** 物理設定への倍率（全 1.0 で変更なし） / Multipliers for physics settings; all 1.0 means no change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings Multiplier")
	FKawaiiPhysicsSettingsMultiplier SettingsScale;

	/** 重みの取得元 / Source used to resolve weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight")
	EKawaiiPhysicsSettingsMultiplierWeightSource WeightSource = EKawaiiPhysicsSettingsMultiplierWeightSource::Envelope;

	/** 区間先頭からの立ち上がり秒 / Rise time from the start of the section, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight",
		meta=(ClampMin="0.0", UIMin="0.0",
			EditCondition="WeightSource == EKawaiiPhysicsSettingsMultiplierWeightSource::Envelope", EditConditionHides, Units="s"))
	float BlendInTime = 0.2f;

	/** Envelope では区間末尾の減衰秒。区間が途中で終了した場合（Montage 中断など）と Curve モードでは、NotifyEnd 時にこの秒数で現在の重みから 0 へフェードする / In Envelope mode, decay seconds at the end of the section. If the section ends early (for example, Montage interruption) and in Curve mode, NotifyEnd fades from the current weight to 0 over this duration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight", meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float BlendOutTime = 0.2f;

	/** WeightSource=Curve の時に参照するカーブ名 / Curve name used when WeightSource is Curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight",
		meta=(EditCondition="WeightSource == EKawaiiPhysicsSettingsMultiplierWeightSource::Curve", EditConditionHides))
	FName CurveName = NAME_None;

	/** カーブが無い/取得できない時のフォールバック重み / Fallback weight when the curve is missing or cannot be read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weight",
		meta=(EditCondition="WeightSource == EKawaiiPhysicsSettingsMultiplierWeightSource::Curve", EditConditionHides,
			ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float DefaultWeightIfNoCurve = 1.0f;

	/** 適用するノードを Tag でフィルタ（空なら全ノード対象） / Tags used to filter target nodes; empty targets all nodes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	FGameplayTagContainer FilterTags;

	/** Tag の完全一致でフィルタするか（false なら親 Tag も許容） / Whether to filter tags by exact match (false allows parent tags). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bFilterExactMatch = false;

#if WITH_EDITOR
	/** 関連アセットの設定を検証する / Validates settings on associated assets. */
	virtual void ValidateAssociatedAssets() override;
#endif

private:
	// 5.8 以降は NotifyInstanceID で再生インスタンスを分ける。5.7 以前はエンジンが Begin と Tick/End で別アドレスのイベントコピーを渡すためポインタは識別子にならない。NotifyStateClass は Instanced で this がイベントと 1:1 なので Component のみで識別し、同一イベントの重なりは ActiveCount で統合する。
	// On 5.8 and later, NotifyInstanceID separates play instances. On 5.7 and earlier, the engine passes event copies at different addresses between Begin and Tick/End, so the pointer cannot identify the state. NotifyStateClass is Instanced, so this maps 1:1 to the event; Component alone identifies the play target, and overlaps of the same event are merged by ActiveCount.
	struct FActiveStateKey
	{
		TWeakObjectPtr<USkeletalMeshComponent> Component;
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
		int32 NotifyInstanceID = 0;
#endif

		bool operator==(const FActiveStateKey& Other) const;

		// private ネスト型のため、名前空間スコープでは型名を参照できない。ADL で見つかるよう friend をクラス内で定義する
		friend uint32 GetTypeHash(const FActiveStateKey& Key)
		{
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
			return HashCombine(GetTypeHash(Key.Component), GetTypeHash(Key.NotifyInstanceID));
#else
			return GetTypeHash(Key.Component);
#endif
		}
	};

	struct FActiveState
	{
		FKawaiiPhysicsTransientHandle Handle;
		float ElapsedTime = 0.0f;
		float TotalDuration = 0.0f;
		KawaiiPhysics::FWindGustEnvelope Envelope;
		uint64 LastTouchedFrame = 0;
		// 同一キーで重なった Begin の数。5.7 以前は同一 Notify イベントの同時再生がエンジン側で区別されないため、End が全て来るまで override を保持する / Number of overlapping Begins for the same key. On 5.7 and earlier, concurrent plays of the same notify event are not distinguished by the engine, so the override is retained until all Ends arrive.
		int32 ActiveCount = 1;
	};

	TMap<FActiveStateKey, FActiveState> ActiveStates;

	/**
	 * エディタの再インスタンス化中は NotifyEnd がスキップされる（UAnimInstance::UninitializeAnimation）ため、この回数の評価で Push が来なければノード側が自動でフェードする保険。
	 * Safety lease count: if Push is not received for this many evaluations, the node side auto-fades because NotifyEnd can be skipped during editor reinstancing (UAnimInstance::UninitializeAnimation).
	 */
	static constexpr int32 LeaseEvaluations = 4;

	/**
	 * 有効な NotifyState はアニメーション更新ごとに毎フレーム Tick されるため、このフレーム数だけ触られていないエントリは NotifyEnd を失って（例: エディタ再インスタンス化）Notify 側の帳簿だけが残っている。ノード側の倍率は Lease で既にフェード済み。
	 * 既知の制限: 掃除はこの Notify の次の NotifyBegin/NotifyEnd（どの Component 上でも可）で走るため、それまでは弱参照キーと数値だけの小さなエントリが残る。アセット共有オブジェクトに Ticker を持たせる寿命リスクの方が大きいため意図的にこの設計にしている。
	 * A live NotifyState ticks every frame the animation updates, so an entry untouched for this many frames has lost NotifyEnd (for example, editor reinstancing) and only notify-side bookkeeping remains; the node-side multiplier has already faded via the lease.
	 * Known limitation: the cleanup runs on the next NotifyBegin/NotifyEnd of this notify (on any component), so a small weak-keyed, numeric-only entry can linger until then. This is intentional: a ticker on an asset-shared object would carry a bigger lifetime risk.
	 */
	static constexpr uint64 StaleStateFrameThreshold = 300;

	static FActiveStateKey MakeStateKey(USkeletalMeshComponent* MeshComp,
	                                    const FAnimNotifyEventReference& EventReference);
	void CleanupStaleStates();
	float ResolveWeight(USkeletalMeshComponent* MeshComp, const FActiveState& State) const;
};
