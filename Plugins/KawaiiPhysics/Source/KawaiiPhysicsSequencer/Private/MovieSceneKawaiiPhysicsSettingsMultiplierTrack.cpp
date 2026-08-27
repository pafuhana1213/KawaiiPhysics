// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "MovieSceneKawaiiPhysicsSettingsMultiplierTrack.h"

#include "KawaiiPhysicsSequencerMultiplierRegistry.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierTemplate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneKawaiiPhysicsSettingsMultiplierTrack)

#define LOCTEXT_NAMESPACE "MovieSceneKawaiiPhysicsSettingsMultiplierTrack"

UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::UMovieSceneKawaiiPhysicsSettingsMultiplierTrack(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);

#if WITH_EDITORONLY_DATA
	TrackTint = FColor(80, 200, 255, 65);
#endif
}

bool UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::SupportsType(
	const TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneKawaiiPhysicsSettingsMultiplierSection::StaticClass();
}

UMovieSceneSection* UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::CreateNewSection()
{
	return NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(this, NAME_None, RF_Transactional);
}

void UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::AddSection(UMovieSceneSection& Section)
{
	Sections.Add(&Section);
}

void UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::RemoveSection(UMovieSceneSection& Section)
{
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(&Section);
	Sections.Remove(&Section);
}

void UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::RemoveSectionAt(const int32 SectionIndex)
{
	if (Sections.IsValidIndex(SectionIndex))
	{
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Sections[SectionIndex]);
		Sections.RemoveAt(SectionIndex);
	}
}

bool UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.Contains(&Section);
}

const TArray<UMovieSceneSection*>& UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::GetAllSections() const
{
	return Sections;
}

bool UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}

void UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::RemoveAllAnimationData()
{
	for (UMovieSceneSection* Section : Sections)
	{
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	}
	Sections.Empty();
}

#if WITH_EDITOR
void UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::PostEditUndo()
{
	Super::PostEditUndo();
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSectionsNotIn(this, GetAllSections());
}
#endif

#if WITH_EDITORONLY_DATA
FText UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::GetDefaultDisplayName() const
{
	if (bIsRootTrack)
	{
		return LOCTEXT("RootTrackName", "Kawaii Physics Settings Multiplier (All)");
	}

	return LOCTEXT("TrackName", "Kawaii Physics Settings Multiplier");
}
#endif

FMovieSceneEvalTemplatePtr UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::CreateTemplateForSection(
	const UMovieSceneSection& InSection) const
{
	if (const UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section =
		Cast<const UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(&InSection))
	{
		return FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate(*Section);
	}

	return FMovieSceneEvalTemplatePtr();
}

#undef LOCTEXT_NAMESPACE
