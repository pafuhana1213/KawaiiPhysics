// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "AnimNode_KawaiiPhysics.h"

// SharedCollision CVars（AnimNode_KawaiiPhysics.cpp で定義）
// SharedCollision CVars (defined in AnimNode_KawaiiPhysics.cpp)
extern TAutoConsoleVariable<int32> CVarSharedCollisionReadMaxAge;
extern TAutoConsoleVariable<int32> CVarSharedCollisionCleanupMaxAge;
extern TAutoConsoleVariable<float> CVarSharedCollisionCleanupInterval;

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

	// フレーム番号を記録（鮮度チェック用） / Record frame number for expiration detection
	LastPublishFrame.store(GFrameCounter, std::memory_order_release);
}

bool FKawaiiPhysicsSharedCollisionSourceSlot::IsExpired(uint64 CurrentFrame, uint64 MaxAge) const
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
	check(IsInGameThread());
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_GetOrCreateSlot);

	// Findは読み取りのみ — ロック不要（GameThread専用、AnimThreadとは読み取り同士で競合しない）
	// Find is read-only — no lock needed (GameThread only, no conflict with AnimThread reads)
	if (TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>* Existing = Slots.Find(SourceID))
	{
		return *Existing;
	}

	// Addは構造変更 — 書き込みロック必要 / Add mutates the TMap — write lock required
	FWriteScopeLock WriteLock(SlotsLock);
	TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> NewSlot = MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>();
	Slots.Add(SourceID, NewSlot);
	return NewSlot;
}

void FKawaiiPhysicsSharedCollisionEntry::ReadMerged(FKawaiiPhysicsSharedCollisionData& OutData) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_ReadMerged);
	OutData.Reset();

	const uint64 CurrentFrame = GFrameCounter;

	FReadScopeLock ReadLock(SlotsLock);
	for (const auto& Pair : Slots)
	{
		// 期限切れスロットをスキップ（Publishが停止したSourceのデータを除外）
		// Skip expired slots (exclude data from sources that stopped publishing)
		if (Pair.Value->IsExpired(CurrentFrame, CVarSharedCollisionReadMaxAge.GetValueOnAnyThread()))
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

void FKawaiiPhysicsSharedCollisionEntry::RemoveExpiredSlots(uint64 CurrentFrame, uint64 MaxAge)
{
	check(IsInGameThread());
	FWriteScopeLock WriteLock(SlotsLock);
	for (auto SlotIt = Slots.CreateIterator(); SlotIt; ++SlotIt)
	{
		if (SlotIt->Value->IsExpired(CurrentFrame, MaxAge))
		{
			SlotIt.RemoveCurrent();
		}
	}
}

int32 FKawaiiPhysicsSharedCollisionEntry::GetSlotCount() const
{
	FReadScopeLock ReadLock(SlotsLock);
	return Slots.Num();
}

bool FKawaiiPhysicsSharedCollisionEntry::IsEmpty() const
{
	FReadScopeLock ReadLock(SlotsLock);
	return Slots.IsEmpty();
}

// -------------------------------------------------------------------
// UKawaiiPhysicsSharedCollisionSubsystem
// -------------------------------------------------------------------

TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindOrCreateEntry(
	AActor* Actor, const FGameplayTag& Tag)
{
	check(IsInGameThread());
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
	check(IsInGameThread());
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
	if (CleanupAccumulator < CVarSharedCollisionCleanupInterval.GetValueOnGameThread())
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

		// 期限切れスロットを除去 / Remove expired slots
		FKawaiiPhysicsSharedCollisionEntry& Entry = *It->Value;
		Entry.RemoveExpiredSlots(CurrentFrame, CVarSharedCollisionCleanupMaxAge.GetValueOnGameThread());

		// スロットが空になったエントリも除去 / Remove entry if all slots are gone
		if (Entry.IsEmpty())
		{
			It.RemoveCurrent();
		}
	}

	// 整数カウンタ更新 / Update integer counters
	int32 TotalSlots = 0;
	for (const auto& Pair : Registry)
	{
		TotalSlots += Pair.Value->GetSlotCount();
	}
	SET_DWORD_STAT(STAT_KawaiiPhysics_SharedCollision_NumEntries, Registry.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_SharedCollision_NumSlots, TotalSlots);
}

TStatId UKawaiiPhysicsSharedCollisionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKawaiiPhysicsSharedCollisionSubsystem, STATGROUP_Tickables);
}
