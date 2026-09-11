// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "GameplayTagContainer.h"
#include "HAL/PlatformTime.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "KawaiiPhysicsSharedTags.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"

#include <atomic>

#include "AnimNode_KawaiiPhysicsSharedPublisher.generated.h"

class UKawaiiPhysicsSharedCollisionSubsystem;
class UKawaiiPhysicsWindPresetDataAsset;
struct FKawaiiPhysicsSharedPublisherEntry;
struct FKawaiiPhysicsSimpleWorldCollisionEntry;
struct FKawaiiPhysicsSharedPublishHelper;

/**
 * Kawaii Physics ノード群が共有する Simple World Collision の収集設定と風の状態を、同じ Shared Group Tag を持つノードへ配る pass-through ノード。1 キャラ（Actor ファミリー）に 1 個、本体メッシュの Post Process AnimBP または Output Pose 直前の幹（Blend で weight 0 にならない位置）に置く。
 * A pass-through node that publishes Simple World Collision gather settings and wind state to every Kawaii Physics node sharing the same Shared Group Tag. Place one per character (actor family) in the body mesh's Post Process AnimBP or on the trunk right before Output Pose (a branch whose blend weight never reaches zero).
 */
USTRUCT(BlueprintInternalUseOnly)
struct KAWAIIPHYSICS_API FAnimNode_KawaiiPhysicsSharedPublisher : public FAnimNode_Base
{
	GENERATED_BODY()

public:
	FAnimNode_KawaiiPhysicsSharedPublisher();
	FAnimNode_KawaiiPhysicsSharedPublisher(const FAnimNode_KawaiiPhysicsSharedPublisher& Other);
	FAnimNode_KawaiiPhysicsSharedPublisher& operator=(const FAnimNode_KawaiiPhysicsSharedPublisher& Other);
	virtual ~FAnimNode_KawaiiPhysicsSharedPublisher() override;

	/**
	 * 入力ポーズ。変更せずにそのまま出力する。このノードは Update で publish するため、weight 0 の枝や非アクティブ State に置くと publish が止まる。
	 * Input pose, passed through unchanged. Publishing happens in Update, so a branch with zero weight or an inactive state stops publishing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Links")
	FPoseLink Source;

	/**
	 * false でも Entry への heartbeat は続け、消費側には「無効」を配る（消費側は押し出し無し・風 0 になる）。
	 * When false the node keeps its heartbeat and publishes a disabled state (consumers get no push-out and zero wind).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher",
		meta = (PinHiddenByDefault, DisplayName = "Enabled"))
	bool bEnabled = true;

	/**
	 * 消費側ノードの Shared Tag と一致させる。同じ Actor ファミリー内で同じ Tag の Publisher は 1 個だけ。
	 * Must match the consumers' Shared Tag. Only one publisher per tag per actor family.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher",
		meta = (DisplayName = "Shared Group Tag"))
	FGameplayTag SharedGroupTag;

	/**
	 * Simple World Collision の収集設定。bGatherFamilyMembers を使う場合は ObjectTypes に Pawn を含める。
	 * Gather settings for Simple World Collision. Include Pawn in ObjectTypes when using bGatherFamilyMembers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision",
		meta = (PinHiddenByDefault, ShowOnlyInnerProperties, DisplayName = "Simple World Collision"))
	FKawaiiPhysicsSimpleWorldCollisionSettings SimpleWorldCollision;

	/**
	 * 共有する ProceduralWind。本ノードでは風の位相クロック（Time）と有効フラグだけを配る。風のパラメータと突風の共有は後続の更新で有効になる。
	 * ProceduralWind shared with consumers. This node currently publishes the wind clock (Time) and enabled flag; parameter and gust sharing arrives in a later change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Wind",
		meta = (PinHiddenByDefault, DisplayName = "Shared Wind"))
	FKawaiiPhysics_ExternalForce_ProceduralWind SharedWind;

	/**
	 * 設定すると、初期化・reinit のたびに Wind Preset Tag のプリセットを Shared Wind に適用する（Shared Wind の 12 項目が上書きされ、Enabled=true / Time Scale=1 になる）。null なら Shared Wind の値をそのまま使う。
	 * When set, the preset selected by Wind Preset Tag is applied to Shared Wind on every initialize / reinit (12 fields overwritten, Enabled=true, Time Scale=1). Leave null to use the Shared Wind values as authored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Wind",
		meta = (PinHiddenByDefault, DisplayName = "Wind Preset Data Asset"))
	TObjectPtr<UKawaiiPhysicsWindPresetDataAsset> WindPresetDataAsset = nullptr;

	/** Shared Wind へ適用する Wind Preset の Tag / Wind Preset tag applied to Shared Wind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Wind",
		meta = (PinHiddenByDefault, DisplayName = "Wind Preset Tag", EditCondition = "WindPresetDataAsset != nullptr"))
	FGameplayTag WindPresetTag;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual bool NeedsDynamicReset() const override { return true; }
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

	void RequestSharedPublisherReinit();
	const FKawaiiPhysicsSharedPublishHelper& GetPublishHelper() const;
	bool IsEffectiveEnabled() const;
	const FKawaiiPhysicsSimpleWorldCollisionSettings& GetEffectiveSimpleWorldCollisionSettings() const;
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> GetSharedPublisherEntry() const;
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> GetSimpleWorldEntry() const;
	FGameplayTag GetResolvedTag() const { return ResolvedTag; }
	/** Wind Preset Data Asset が設定されていれば Wind Preset Tag のプリセットを Shared Wind へ適用する。GameThread 専用。/ Applies the Wind Preset Tag preset to Shared Wind when Wind Preset Data Asset is set. GameThread only. */
	void ApplySharedWindPreset();
	/**
	 * 外部から SharedWind を authored 値で置き換えたときに呼ぶ。適用前の退避（SharedWindBeforePreset）を捨て、次の ApplySharedWindPreset が新しい authored 値を退避してプリセットを適用し直せるようにする。GameThread 専用。
	 * Call after SharedWind was replaced with authored values from outside. Drops the pre-preset snapshot (SharedWindBeforePreset) so the next ApplySharedWindPreset snapshots the new authored values and applies the preset again. GameThread only.
	 */
	void ResetSharedWindPresetSnapshot();

	// Shipping/Test（WITH_DEV_AUTOMATION_TESTS==0）では宣言ごと除外し、出荷ビルドにテスト表面を残さない。
	// Stripped in Shipping/Test (WITH_DEV_AUTOMATION_TESTS==0); leaves no test surface in shipping builds.
#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * プリセット適用前の Shared Wind の退避値を保持しているか（プリセット適用中なら true）。
	 * Whether the pre-preset Shared Wind snapshot is currently held (true while a preset is applied).
	 */
	bool HasSharedWindBeforePreset() const { return SharedWindBeforePreset.IsSet(); }

	/**
	 * 次の PreUpdate で reinit を走らせる要求が立っているか。
	 * Whether a reinit request is pending for the next PreUpdate.
	 */
	bool IsReinitRequestedForTest() const { return bReinitRequested.load(std::memory_order_acquire); }

	/**
	 * reinit 要求を降ろす（「この操作が新たに要求したか」をテストが見分けるための前準備）。
	 * Clears the pending reinit request so a test can tell whether a specific operation raises it again.
	 */
	void ClearReinitRequestForTest() { bReinitRequested.store(false, std::memory_order_release); }
#endif

#if WITH_EDITORONLY_DATA
	bool IsRecentlyUpdated() const
	{
		return (FPlatformTime::Seconds() - LastUpdatedTime) < 0.1;
	}
#endif

private:
	void InitializeHelper();
	uint64 GetSourceID() const { return reinterpret_cast<uint64>(this); }

	TUniquePtr<FKawaiiPhysicsSharedPublishHelper> Helper;
	TWeakObjectPtr<UKawaiiPhysicsSharedCollisionSubsystem> CachedSubsystem;
	TWeakObjectPtr<UWorld> CachedWindWorld;
	TOptional<double> CachedWindGameTimeSeconds;
	TWeakObjectPtr<const USkeletalMeshComponent> CachedSkelComp;
	TWeakObjectPtr<AActor> CachedFamilyRoot;
	TWeakObjectPtr<UKawaiiPhysicsWindPresetDataAsset> CachedWindPresetDataAsset;
	FGameplayTag ResolvedTag;
	FGameplayTag CachedWindPresetTag;
	/**
	 * Wind Preset を最初に適用する直前の SharedWind の値（18 項目の snapshot）。プリセットが None / 無効になったときに書き戻す。GameThread 専用（PreUpdate から呼ばれる ApplySharedWindPreset だけが読み書きする）／保存されない transient 値。
	 * Snapshot of SharedWind (all 18 fields) taken just before the first Wind Preset application, written back when the preset becomes None or unresolvable. GameThread only (touched solely by ApplySharedWindPreset, called from PreUpdate) and never serialized.
	 */
	TOptional<FKawaiiProceduralWindDynamicParams> SharedWindBeforePreset;
	std::atomic<bool> bReinitRequested{true};
	uint64 PreUpdateFrame = 0;
	uint64 ProviderMaxAgeFrames = 60;

#if !UE_BUILD_SHIPPING
	bool bInvalidTagWarningLogged = false;
	bool bInvalidWindPresetWarningLogged = false;
#endif

#if WITH_EDITORONLY_DATA
	double LastUpdatedTime = 0.0;
#endif
};
