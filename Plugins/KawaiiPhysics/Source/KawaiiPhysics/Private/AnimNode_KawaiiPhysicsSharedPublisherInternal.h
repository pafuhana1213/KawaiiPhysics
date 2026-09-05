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

	bool IsEffectiveEnabled() const { return bEffectiveEnabled; }
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
	bool bNeedsEntryReacquire = false;

#if !UE_BUILD_SHIPPING
	bool bProviderConflictWarningLogged = false;
#endif

#if WITH_DEV_AUTOMATION_TESTS
	int32 NumSetDescCalls = 0;
#endif
};
