// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "AnimNode_KawaiiPhysics.h"

// -------------------------------------------------------------------
// FKawaiiPhysicsSharedCollisionSourceSlot
// -------------------------------------------------------------------

void FKawaiiPhysicsSharedCollisionSourceSlot::Publish(const FKawaiiPhysicsSharedCollisionData& Data)
{
	// 非アクティブバッファに書き込み / Write to inactive buffer
	const int32 WriteIndex = 1 - ReadBufferIndex.load(std::memory_order_acquire);
	Buffers[WriteIndex] = Data;

	// アトミックスワップ：読み取り側に公開 / Atomic swap: expose to readers
	ReadBufferIndex.store(WriteIndex, std::memory_order_release);
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
	OutData.Reset();

	for (const auto& Pair : Slots)
	{
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
			// WeakObjectPtr遅延クリーンアップ: Actorが無効なら除去
			// Lazy cleanup: skip if actor is invalid (will be cleaned up later)
			if (Key.Key.IsValid())
			{
				return *Found;
			}
		}

		Current = Current->GetAttachParentActor();
	}

	return nullptr;
}
