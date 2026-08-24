// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSequencerOverrideRegistry.h"

#include "Components/SkeletalMeshComponent.h"
#include "KawaiiPhysicsLibrary.h"
#include "MovieSceneSection.h"

void FKawaiiPhysicsSequencerOverrideEntry::Stop()
{
	if (bStopped)
	{
		return;
	}

	bStopped = true;

	if (USkeletalMeshComponent* TargetComponent = Component.Get())
	{
		UKawaiiPhysicsLibrary::StopPhysicsSettingsOverridesOnComponent(
			TargetComponent, Handle, FilterTags, bFilterExactMatch, FMath::Max(BlendOutTime, 0.0f));
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
	if (!Section)
	{
		return;
	}

	const TWeakObjectPtr<const UMovieSceneSection> SectionKey(Section);
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		const bool bMatchesSection = It.Key() == SectionKey;
		if (bMatchesSection)
		{
			if (const TSharedPtr<FKawaiiPhysicsSequencerOverrideEntry> Entry = It.Value().Pin())
			{
				Entry->Stop();
			}
		}

		if (bMatchesSection || !It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
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
		if (Pair.Key == SectionKey && Pair.Value.IsValid())
		{
			++Count;
		}
	}
	return Count;
}
#endif
