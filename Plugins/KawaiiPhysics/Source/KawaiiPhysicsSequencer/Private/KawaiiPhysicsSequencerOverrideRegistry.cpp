// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSequencerOverrideRegistry.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "KawaiiPhysicsLibrary.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "UObject/UObjectGlobals.h"

void FKawaiiPhysicsSequencerOverrideEntry::Stop(const float OverrideBlendOutTime)
{
	if (bStopped)
	{
		return;
	}

	bStopped = true;

	if (USkeletalMeshComponent* TargetComponent = Component.Get())
	{
		const float EffectiveBlendOutTime =
			OverrideBlendOutTime >= 0.0f ? OverrideBlendOutTime : FMath::Max(BlendOutTime, 0.0f);
		UKawaiiPhysicsLibrary::StopPhysicsSettingsOverridesOnComponent(
			TargetComponent, Handle, FilterTags, bFilterExactMatch, EffectiveBlendOutTime);
	}
}

FKawaiiPhysicsSequencerOverrideRegistry& FKawaiiPhysicsSequencerOverrideRegistry::Get()
{
	static FKawaiiPhysicsSequencerOverrideRegistry Registry;
	return Registry;
}

void FKawaiiPhysicsSequencerOverrideRegistry::Register(
	const UMovieSceneSection* Section,
	const TSharedRef<FKawaiiPhysicsSequencerOverrideEntry>& Entry)
{
	if (!Section)
	{
		return;
	}

	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		if (It.Key().Get() == Section && It.Value().Pin().Get() == &Entry.Get())
		{
			return;
		}
	}

	Entries.Add(TWeakObjectPtr<const UMovieSceneSection>(Section), Entry);
}

void FKawaiiPhysicsSequencerOverrideRegistry::StopForSection(const UMovieSceneSection* Section)
{
	StopForSectionInternal(Section, nullptr, nullptr);
}

void FKawaiiPhysicsSequencerOverrideRegistry::StopForSection(
	const UMovieSceneSection* Section,
	const TWeakPtr<uint8>& OwnerFilter)
{
	StopForSectionInternal(Section, &OwnerFilter, nullptr);
}

void FKawaiiPhysicsSequencerOverrideRegistry::StopForSection(
	const UMovieSceneSection* Section,
	const TWeakPtr<uint8>& OwnerFilter,
	const USkeletalMeshComponent* ComponentFilter)
{
	StopForSectionInternal(Section, &OwnerFilter, ComponentFilter);
}

void FKawaiiPhysicsSequencerOverrideRegistry::StopForSectionInternal(
	const UMovieSceneSection* Section,
	const TWeakPtr<uint8>* OptionalOwnerFilter,
	const USkeletalMeshComponent* OptionalComponentFilter)
{
	if (!Section)
	{
		return;
	}

	TSharedPtr<uint8> OwnerFilterPin;
	if (OptionalOwnerFilter)
	{
		OwnerFilterPin = OptionalOwnerFilter->Pin();
		if (!OwnerFilterPin.IsValid())
		{
			return;
		}
	}

	const TWeakObjectPtr<const UMovieSceneSection> SectionKey(Section);
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		const bool bMatchesSection = It.Key() == SectionKey;
		bool bStopAndRemove = bMatchesSection;
		if (bMatchesSection && OptionalOwnerFilter)
		{
			bStopAndRemove = false;
			if (const TSharedPtr<FKawaiiPhysicsSequencerOverrideEntry> Entry = It.Value().Pin())
			{
				const TSharedPtr<uint8> EntryOwner = Entry->Owner.Pin();
				bStopAndRemove = EntryOwner.IsValid() && EntryOwner.Get() == OwnerFilterPin.Get();

				// Component まで指定されている場合は、その Component の Entry だけに絞る（無効化済み Entry はフィルタ指定時は対象外）
				if (bStopAndRemove && OptionalComponentFilter)
				{
					bStopAndRemove = Entry->Component.Get() == OptionalComponentFilter;
				}
			}
		}

		if (bStopAndRemove)
		{
			if (const TSharedPtr<FKawaiiPhysicsSequencerOverrideEntry> Entry = It.Value().Pin())
			{
				Entry->Stop();
			}
		}

		if (bStopAndRemove || !It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void FKawaiiPhysicsSequencerOverrideRegistry::StopForWorld(const UWorld* World)
{
	if (!World)
	{
		return;
	}

	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		bool bRemove = !It.Key().IsValid();
		if (const TSharedPtr<FKawaiiPhysicsSequencerOverrideEntry> Entry = It.Value().Pin())
		{
			if (USkeletalMeshComponent* Component = Entry->Component.Get())
			{
				if (Component->GetWorld() == World)
				{
					Entry->Stop(0.0f);
					bRemove = true;
				}
			}
		}
		else
		{
			bRemove = true;
		}

		if (bRemove)
		{
			It.RemoveCurrent();
		}
	}
}

void FKawaiiPhysicsSequencerOverrideRegistry::StopForSectionsNotIn(
	const UMovieSceneTrack* Track,
	TConstArrayView<UMovieSceneSection*> LiveSections)
{
	if (!Track)
	{
		return;
	}

	TArray<const UMovieSceneSection*> SectionsToStop;
	for (const TPair<TWeakObjectPtr<const UMovieSceneSection>, TWeakPtr<FKawaiiPhysicsSequencerOverrideEntry>>& Pair :
	     Entries)
	{
		const UMovieSceneSection* Section = Pair.Key.Get();
		if (!Section || Section->GetOuter() != Track)
		{
			continue;
		}

		bool bIsLive = false;
		for (const UMovieSceneSection* LiveSection : LiveSections)
		{
			if (LiveSection == Section)
			{
				bIsLive = true;
				break;
			}
		}

		if (!bIsLive)
		{
			SectionsToStop.AddUnique(Section);
		}
	}

	for (const UMovieSceneSection* Section : SectionsToStop)
	{
		StopForSection(Section);
	}
}

void FKawaiiPhysicsSequencerOverrideRegistry::PruneInvalidEntries()
{
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		bool bRemove = !It.Key().IsValid();
		if (const TSharedPtr<FKawaiiPhysicsSequencerOverrideEntry> Entry = It.Value().Pin())
		{
			USkeletalMeshComponent* Component = Entry->Component.Get();
			bRemove |= !IsValid(Component) || !IsValid(Component->GetWorld());
		}
		else
		{
			bRemove = true;
		}

		if (bRemove)
		{
			It.RemoveCurrent();
		}
	}
}

int32 FKawaiiPhysicsSequencerOverrideRegistry::GetQueuedNodeCount(
	const UMovieSceneSection* Section,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch,
	bool& bOutHasLiveEntry) const
{
	bOutHasLiveEntry = false;

	if (!Section)
	{
		return 0;
	}

	const TWeakObjectPtr<const UMovieSceneSection> SectionKey(Section);
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<const UMovieSceneSection>, TWeakPtr<FKawaiiPhysicsSequencerOverrideEntry>>& Pair :
	     Entries)
	{
		if (Pair.Key != SectionKey)
		{
			continue;
		}

		const TSharedPtr<FKawaiiPhysicsSequencerOverrideEntry> Entry = Pair.Value.Pin();
		if (!Entry.IsValid() || Entry->bStopped || !IsValid(Entry->Component.Get()))
		{
			continue;
		}

		// フィルタが異なる Entry（過去のフィルタ設定で残っているもの等）は現在の表示に無関係なので合算から除外する
		if (Entry->FilterTags != FilterTags || Entry->bFilterExactMatch != bFilterExactMatch)
		{
			continue;
		}

		bOutHasLiveEntry = true;
		Count += Entry->LastQueuedNodeCount;
	}
	return Count;
}

void FKawaiiPhysicsSequencerOverrideRegistry::StopAll()
{
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		if (const TSharedPtr<FKawaiiPhysicsSequencerOverrideEntry> Entry = It.Value().Pin())
		{
			Entry->Stop(0.0f);
		}
	}

	Entries.Empty();
}

void FKawaiiPhysicsSequencerOverrideRegistry::RegisterDelegates()
{
	if (!WorldCleanupDelegateHandle.IsValid())
	{
		WorldCleanupDelegateHandle = FWorldDelegates::OnWorldCleanup.AddRaw(
			this, &FKawaiiPhysicsSequencerOverrideRegistry::HandleWorldCleanup);
	}

	if (!PostGarbageCollectDelegateHandle.IsValid())
	{
		PostGarbageCollectDelegateHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(
			this, &FKawaiiPhysicsSequencerOverrideRegistry::HandlePostGarbageCollect);
	}
}

void FKawaiiPhysicsSequencerOverrideRegistry::UnregisterDelegates()
{
	if (WorldCleanupDelegateHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupDelegateHandle);
		WorldCleanupDelegateHandle.Reset();
	}

	if (PostGarbageCollectDelegateHandle.IsValid())
	{
		FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGarbageCollectDelegateHandle);
		PostGarbageCollectDelegateHandle.Reset();
	}
}

void FKawaiiPhysicsSequencerOverrideRegistry::HandleWorldCleanup(
	UWorld* World,
	bool bSessionEnded,
	bool bCleanupResources)
{
	(void)bSessionEnded;
	(void)bCleanupResources;

	// World cleanup デリゲートは GameThread 前提で呼ばれるため、Registry 側も既存方針どおり GameThread 限定で扱う
	StopForWorld(World);
}

void FKawaiiPhysicsSequencerOverrideRegistry::HandlePostGarbageCollect()
{
	// GC 後デリゲートは GameThread 前提で呼ばれるため、Registry 側も既存方針どおり GameThread 限定で扱う
	PruneInvalidEntries();
}

#if WITH_DEV_AUTOMATION_TESTS
int32 FKawaiiPhysicsSequencerOverrideRegistry::CountEntriesForSectionForTesting(
	const UMovieSceneSection* Section) const
{
	if (!Section)
	{
		return 0;
	}

	const TWeakObjectPtr<const UMovieSceneSection> SectionKey(Section);
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<const UMovieSceneSection>, TWeakPtr<FKawaiiPhysicsSequencerOverrideEntry>>& Pair :
	     Entries)
	{
		if (Pair.Key == SectionKey)
		{
			++Count;
		}
	}
	return Count;
}
#endif
