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
	TWeakObjectPtr<const USkeletalMeshComponent> CachedSkelComp;
	TWeakObjectPtr<AActor> CachedFamilyRoot;
	FGameplayTag ResolvedTag;
	std::atomic<bool> bReinitRequested{true};
	uint64 PreUpdateFrame = 0;
	uint64 ProviderMaxAgeFrames = 60;

#if !UE_BUILD_SHIPPING
	bool bInvalidTagWarningLogged = false;
#endif

#if WITH_EDITORONLY_DATA
	double LastUpdatedTime = 0.0;
#endif
};
