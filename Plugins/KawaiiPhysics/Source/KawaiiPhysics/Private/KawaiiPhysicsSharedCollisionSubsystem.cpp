// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "AnimNode_KawaiiPhysics.h"

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_Publish"), STAT_KawaiiPhysics_SharedCollision_Publish, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_GetOrCreateSlot"), STAT_KawaiiPhysics_SharedCollision_GetOrCreateSlot, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_ReadMerged"), STAT_KawaiiPhysics_SharedCollision_ReadMerged, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_FindOrCreateEntry"), STAT_KawaiiPhysics_SharedCollision_FindOrCreateEntry, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_FindEntry"), STAT_KawaiiPhysics_SharedCollision_FindEntry, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_Tick"), STAT_KawaiiPhysics_SharedCollision_Tick, STATGROUP_Anim);
DECLARE_DWORD_COUNTER_STAT(TEXT("KawaiiPhysics_SharedCollision_NumEntries"), STAT_KawaiiPhysics_SharedCollision_NumEntries, STATGROUP_Anim);
DECLARE_DWORD_COUNTER_STAT(TEXT("KawaiiPhysics_SharedCollision_NumSlots"), STAT_KawaiiPhysics_SharedCollision_NumSlots, STATGROUP_Anim);

// -------------------------------------------------------------------
// FKawaiiPhysicsSharedCollisionSourceSlot
// -------------------------------------------------------------------

void FKawaiiPhysicsSharedCollisionSourceSlot::Publish(const FKawaiiPhysicsSharedCollisionData& Data)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_Publish);
	// 非アクティブバッファに書き込み / Write to inactive buffer
	const int32 WriteIndex = 1 - ReadBufferIndex.load(std::memory_order_acquire);
	Buffers[WriteIndex] = Data;

	// アトミックスワップ：読み取り側に公開 / Atomic swap: expose to readers
	ReadBufferIndex.store(WriteIndex, std::memory_order_release);

	// フレーム番号を記録（鮮度チェック用） / Record frame number for staleness detection
	LastPublishFrame.store(GFrameCounter, std::memory_order_release);
}

bool FKawaiiPhysicsSharedCollisionSourceSlot::IsStale(uint64 CurrentFrame, uint64 MaxAge) const
{
	const uint64 LastFrame = LastPublishFrame.load(std::memory_order_acquire);
	return (LastFrame == 0) || (CurrentFrame - LastFrame > MaxAge);
}

const FKawaiiPhysicsSharedCollisionData& FKawaiiPhysicsSharedCollisionSourceSlot::Read() const
{
	return Buffers[ReadBufferIndex.load(std::memory_order_acquire)];
}

// -------------------------------------------------------------------
// FKawaiiPhysicsSharedCollisionEntry
// -------------------------------------------------------------------

TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> FKawaiiPhysicsSharedCollisionEntry::GetOrCreateSlot(uint64 SourceID)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_GetOrCreateSlot);
	if (TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>* Existing = Slots.Find(SourceID))
	{
		return *Existing;
	}

	TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> NewSlot = MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>();
	Slots.Add(SourceID, NewSlot);
	return NewSlot;
}

void FKawaiiPhysicsSharedCollisionEntry::ReadMerged(FKawaiiPhysicsSharedCollisionData& OutData) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_ReadMerged);
	OutData.Reset();

	const uint64 CurrentFrame = GFrameCounter;

	for (const auto& Pair : Slots)
	{
		// staleスロットをスキップ（Publishが停止したSourceのデータを除外）
		// Skip stale slots (exclude data from sources that stopped publishing)
		if (Pair.Value->IsStale(CurrentFrame))
		{
			continue;
		}

		const FKawaiiPhysicsSharedCollisionData& SlotData = Pair.Value->Read();

		OutData.SphericalLimits.Append(SlotData.SphericalLimits);
		OutData.CapsuleLimits.Append(SlotData.CapsuleLimits);
		OutData.BoxLimits.Append(SlotData.BoxLimits);
		OutData.PlanarLimits.Append(SlotData.PlanarLimits);
	}
}

// -------------------------------------------------------------------
// UKawaiiPhysicsSharedCollisionSubsystem
// -------------------------------------------------------------------

TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindOrCreateEntry(
	AActor* Actor, const FGameplayTag& Tag)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_FindOrCreateEntry);
	if (!Actor || !Tag.IsValid())
	{
		return nullptr;
	}

	const TPair<TWeakObjectPtr<AActor>, FGameplayTag> Key(Actor, Tag);

	if (TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>* Existing = Registry.Find(Key))
	{
		return *Existing;
	}

	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> NewEntry = MakeShared<FKawaiiPhysicsSharedCollisionEntry>();
	Registry.Add(Key, NewEntry);
	return NewEntry;
}

TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindEntry(
	AActor* Actor, const FGameplayTag& Tag) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_FindEntry);
	if (!Actor || !Tag.IsValid())
	{
		return nullptr;
	}

	// Actor→親Actorを辿って検索 / Traverse Actor attachment hierarchy
	AActor* Current = Actor;
	while (Current)
	{
		const TPair<TWeakObjectPtr<AActor>, FGameplayTag> Key(Current, Tag);

		if (const TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>* Found = Registry.Find(Key))
		{
			// Actorが無効ならスキップ（Tick()で定期的にクリーンアップ）
			// Skip if actor is invalid (cleaned up periodically in Tick())
			if (Key.Key.IsValid())
			{
				return *Found;
			}
		}

		Current = Current->GetAttachParentActor();
	}

	return nullptr;
}

void UKawaiiPhysicsSharedCollisionSubsystem::Deinitialize()
{
	Registry.Empty();
	Super::Deinitialize();
}

void UKawaiiPhysicsSharedCollisionSubsystem::Tick(float DeltaTime)
{
	CleanupAccumulator += DeltaTime;
	if (CleanupAccumulator < CleanupIntervalSeconds)
	{
		return;
	}
	CleanupAccumulator = 0.0f;
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_Tick);

	const uint64 CurrentFrame = GFrameCounter;

	for (auto It = Registry.CreateIterator(); It; ++It)
	{
		// Actorが無効 → エントリ除去 / Remove entry if actor is invalid
		if (!It->Key.Key.IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		// staleスロットを除去（猶予60フレーム ≈ 1秒@60fps）
		// Remove stale slots (60 frame grace period ≈ 1s at 60fps)
		FKawaiiPhysicsSharedCollisionEntry& Entry = *It->Value;
		for (auto SlotIt = Entry.Slots.CreateIterator(); SlotIt; ++SlotIt)
		{
			if (SlotIt->Value->IsStale(CurrentFrame, 60))
			{
				SlotIt.RemoveCurrent();
			}
		}

		// スロットが空になったエントリも除去 / Remove entry if all slots are gone
		if (Entry.Slots.IsEmpty())
		{
			It.RemoveCurrent();
		}
	}

	// 整数カウンタ更新 / Update integer counters
	int32 TotalSlots = 0;
	for (const auto& Pair : Registry)
	{
		TotalSlots += Pair.Value->Slots.Num();
	}
	SET_DWORD_STAT(STAT_KawaiiPhysics_SharedCollision_NumEntries, Registry.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_SharedCollision_NumSlots, TotalSlots);
}

TStatId UKawaiiPhysicsSharedCollisionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKawaiiPhysicsSharedCollisionSubsystem, STATGROUP_Tickables);
}
