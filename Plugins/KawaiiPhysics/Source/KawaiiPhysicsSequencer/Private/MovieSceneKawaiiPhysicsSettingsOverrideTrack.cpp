// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "MovieSceneKawaiiPhysicsSettingsOverrideTrack.h"

#include "KawaiiPhysicsSequencerOverrideRegistry.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideTemplate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneKawaiiPhysicsSettingsOverrideTrack)

#define LOCTEXT_NAMESPACE "MovieSceneKawaiiPhysicsSettingsOverrideTrack"

UMovieSceneKawaiiPhysicsSettingsOverrideTrack::UMovieSceneKawaiiPhysicsSettingsOverrideTrack(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);

#if WITH_EDITORONLY_DATA
	TrackTint = FColor(80, 200, 255, 65);
#endif
}

bool UMovieSceneKawaiiPhysicsSettingsOverrideTrack::SupportsType(
	const TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneKawaiiPhysicsSettingsOverrideSection::StaticClass();
}

UMovieSceneSection* UMovieSceneKawaiiPhysicsSettingsOverrideTrack::CreateNewSection()
{
	return NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(this, NAME_None, RF_Transactional);
}

void UMovieSceneKawaiiPhysicsSettingsOverrideTrack::AddSection(UMovieSceneSection& Section)
{
	Sections.Add(&Section);
}

void UMovieSceneKawaiiPhysicsSettingsOverrideTrack::RemoveSection(UMovieSceneSection& Section)
{
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(&Section);
	Sections.Remove(&Section);
}

void UMovieSceneKawaiiPhysicsSettingsOverrideTrack::RemoveSectionAt(const int32 SectionIndex)
{
	if (Sections.IsValidIndex(SectionIndex))
	{
		FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Sections[SectionIndex]);
		Sections.RemoveAt(SectionIndex);
	}
}

bool UMovieSceneKawaiiPhysicsSettingsOverrideTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.Contains(&Section);
}

const TArray<UMovieSceneSection*>& UMovieSceneKawaiiPhysicsSettingsOverrideTrack::GetAllSections() const
{
	return Sections;
}

bool UMovieSceneKawaiiPhysicsSettingsOverrideTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}

void UMovieSceneKawaiiPhysicsSettingsOverrideTrack::RemoveAllAnimationData()
{
	for (UMovieSceneSection* Section : Sections)
	{
		FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section);
	}
	Sections.Empty();
}

#if WITH_EDITOR
void UMovieSceneKawaiiPhysicsSettingsOverrideTrack::PostEditUndo()
{
	Super::PostEditUndo();
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSectionsNotIn(this, GetAllSections());
}
#endif

#if WITH_EDITORONLY_DATA
FText UMovieSceneKawaiiPhysicsSettingsOverrideTrack::GetDefaultDisplayName() const
{
	if (bIsRootTrack)
	{
		return LOCTEXT("RootTrackName", "Kawaii Physics Settings Override (All)");
	}

	return LOCTEXT("TrackName", "Kawaii Physics Settings Override");
}
#endif

FMovieSceneEvalTemplatePtr UMovieSceneKawaiiPhysicsSettingsOverrideTrack::CreateTemplateForSection(
	const UMovieSceneSection& InSection) const
{
	if (const UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section =
		Cast<const UMovieSceneKawaiiPhysicsSettingsOverrideSection>(&InSection))
	{
		return FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate(*Section);
	}

	return FMovieSceneEvalTemplatePtr();
}

#undef LOCTEXT_NAMESPACE
