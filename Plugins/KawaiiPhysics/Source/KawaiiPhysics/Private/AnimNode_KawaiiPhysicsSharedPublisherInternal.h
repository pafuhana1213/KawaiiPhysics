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

struct FKawaiiPhysicsSharedPublishHelper
{
	void SetSourceID(uint64 InSourceID);
	void SetDebugTag(FGameplayTag InDebugTag);
	void SetEntries(TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> InPublisherEntry,
	                TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> InSimpleWorldEntry,
	                TWeakObjectPtr<const USkeletalMeshComponent> InSkelComp);
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
	bool NeedsEntryReacquire() const { return bNeedsEntryReacquire || !PublisherEntry.IsValid() || !SimpleWorldEntry.IsValid(); }
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
	bool ApplyInputChanges(const FKawaiiPhysicsSharedPublishInputs& Inputs);
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
