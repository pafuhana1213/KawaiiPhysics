// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "Components/SkeletalMeshComponent.h"

struct FKawaiiPhysicsSharedPublishInputs
{
	bool bEnabled = true;
	FKawaiiPhysicsSimpleWorldCollisionSettings SimpleWorld;
	bool bWindEnabled = true;
	float WindTimeScale = 1.0f;
	FKawaiiPhysics_ExternalForce_ProceduralWind* SharedWind = nullptr;
	TOptional<double> GameTimeSeconds;
};

struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedPublishHelper
{
	void SetSourceID(uint64 InSourceID);
	void SetDebugTag(FGameplayTag InDebugTag);
	/**
	 * Entry を差し替える。bInProviderDescRegistered は PreUpdate の FindOrCreate（provider）で provider Desc を登録済みのときに true を渡す。
	 * Replaces the entries. Pass bInProviderDescRegistered as true when PreUpdate already registered a provider Desc through the provider FindOrCreate.
	 */
	void SetEntries(TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> InPublisherEntry,
	                TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> InSimpleWorldEntry,
	                TWeakObjectPtr<const USkeletalMeshComponent> InSkelComp,
	                bool bInProviderDescRegistered = false);
	/** SimpleWorld Entry だけを差し替え、実効値と風クロックを保持する / Replaces only the SimpleWorld entry, preserving effective values and the wind clock. */
	void SetSimpleWorldEntry(TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> InSimpleWorldEntry,
	                         bool bInProviderDescRegistered = false);
	void ReleaseEntries();
	void ResetEffectiveValues(const FKawaiiPhysicsSharedPublishInputs& Defaults);
	bool Update(const FKawaiiPhysicsSharedPublishInputs& Inputs,
	            const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>& WindRuntimeState,
	            float DeltaTime, uint64 CurrentFrame, uint64 ProviderMaxAgeFrames);
	/**
	 * World を持たない呼び出し元向けに、未消費の DeltaSeconds を累積する。
	 * Accumulates unconsumed delta seconds for callers without a world clock.
	 */
	void AccumulatePendingDeltaTime(float DeltaSeconds);
	void ResetWindClock();

	bool IsEffectiveEnabled() const { return bEffectiveEnabled; }
	float GetPendingDeltaTime() const { return PendingDeltaTime; }
	const FKawaiiPhysicsSimpleWorldCollisionSettings& GetEffectiveSimpleWorldSettings() const
	{
		return EffectiveSimpleWorldSettings;
	}
	uint64 GetLastPublishSerial() const { return LastPublishSerial; }
	bool NeedsEntryReacquire() const { return bNeedsEntryReacquire || !PublisherEntry.IsValid(); }
	/** SimpleWorld Entry の再取得が必要かを返す / Returns whether the SimpleWorld entry needs rebinding. */
	bool NeedsSimpleWorldEntryReacquire() const { return bNeedsSimpleWorldEntryReacquire || !SimpleWorldEntry.IsValid(); }
	const FKawaiiPhysicsSharedPublisherState& GetLastPublishedState() const { return LastPublishedState; }
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> GetSharedPublisherEntry() const { return PublisherEntry; }
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> GetSimpleWorldEntry() const { return SimpleWorldEntry; }

#if WITH_DEV_AUTOMATION_TESTS
	int32 GetNumSetDescCalls() const { return NumSetDescCalls; }
	/**
	 * claim フレームの最初の publish（受理判定）に載せた風の Time。当フレーム分までの追いつきが publish 前に済んでいるかの確認用。
	 * The wind Time carried by the first publish of a claim frame (the acceptance test), used to verify that the clock is caught up through the current frame before publishing.
	 */
	float GetLastClaimPublishedWindTime() const { return LastClaimPublishedWindTime; }
	/**
	 * 直近の受理された Update が claim フレームだったか（定常フレームなら false）。
	 * Whether the most recently accepted Update was a claim frame (false for a steady frame).
	 */
	bool WasLastUpdateClaim() const { return bLastUpdateWasClaim; }
#endif

private:
	/**
	 * 当フレームに変化した UPROPERTY 入力の種別。Pending 消費後の再適用に使い、フレームを跨いで持たない。
	 * Which UPROPERTY inputs changed this frame. Used to re-apply them after pending requests are consumed; never kept across frames.
	 */
	struct FInputChangeFlags
	{
		bool bEnabledChanged = false;
		bool bSimpleWorldSettingsChanged = false;
	};

	FInputChangeFlags ApplyInputChanges(const FKawaiiPhysicsSharedPublishInputs& Inputs);
	/**
	 * SimpleWorld Entry へ登録済みの provider Desc を登録解除する。
	 * Unregisters this source's provider Desc from the SimpleWorld entry.
	 */
	void UnregisterProviderDesc();

	uint64 SourceID = 0;
	FGameplayTag DebugTag;
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry;
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry;
	TWeakObjectPtr<const USkeletalMeshComponent> SkelComp;

	bool bEffectiveEnabled = true;
	FKawaiiPhysicsSimpleWorldCollisionSettings EffectiveSimpleWorldSettings;
	TOptional<FKawaiiPhysicsSharedPublishInputs> LastInputs;
	TOptional<FKawaiiPhysicsSimpleWorldCollisionDesc> LastSentDesc;
	FKawaiiPhysicsSharedPublisherState LastPublishedState;
	uint64 LastPublishSerial = 0;
	/**
	 * 現在保持している Entry へ publish 済みか。LastPublishSerial は Entry を取り直しても 0 に戻らないので、LastPublishedState を「消費側が受け取った値」として使ってよいかの判定はこちらで行う。
	 * Whether this helper has published to the entry it currently holds. LastPublishSerial is not reset when entries are re-acquired, so this flag decides whether LastPublishedState still represents what consumers received.
	 */
	bool bHasPublishedToCurrentEntry = false;
	/**
	 * World のゲーム内時刻が無い場合だけ使う、未消費の DeltaSeconds。
	 * Unconsumed delta seconds used only when world game time is unavailable.
	 */
	float PendingDeltaTime = 0.0f;
	TOptional<double> LastWindGameTimeSeconds;
	bool bNeedsEntryReacquire = false;
	bool bNeedsSimpleWorldEntryReacquire = false;
	/**
	 * この SourceID の provider Desc が SimpleWorld Entry に登録済みか。PreUpdate の FindOrCreate（provider）成功時と SetDesc 成功時に立て、RemoveDesc で下ろす。
	 * Whether this source's provider Desc is registered with the SimpleWorld entry. Set when PreUpdate's provider FindOrCreate succeeds and when SetDesc succeeds; cleared by RemoveDesc.
	 */
	bool bProviderDescRegistered = false;
	TArray<FKawaiiPhysicsSharedPublisherGustRequest> PendingGustBuffer;

#if !UE_BUILD_SHIPPING
	bool bProviderConflictWarningLogged = false;
#endif

#if WITH_DEV_AUTOMATION_TESTS
	int32 NumSetDescCalls = 0;
	float LastClaimPublishedWindTime = 0.0f;
	bool bLastUpdateWasClaim = false;
#endif
};
