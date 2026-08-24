// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Sequencer/KawaiiPhysicsSettingsOverrideTrackEditor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "KawaiiPhysicsEdStyle.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideTrack.h"
#include "ScopedTransaction.h"
#include "Sequencer/KawaiiPhysicsSettingsOverrideSectionSummary.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

#define LOCTEXT_NAMESPACE "FKawaiiPhysicsSettingsOverrideTrackEditor"

TSharedRef<ISequencerTrackEditor> FKawaiiPhysicsSettingsOverrideTrackEditor::CreateTrackEditor(
	TSharedRef<ISequencer> InSequencer)
{
	return MakeShared<FKawaiiPhysicsSettingsOverrideTrackEditor>(InSequencer);
}

FKawaiiPhysicsSettingsOverrideTrackEditor::FKawaiiPhysicsSettingsOverrideTrackEditor(
	TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

bool FKawaiiPhysicsSettingsOverrideTrackEditor::SupportsType(const TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UMovieSceneKawaiiPhysicsSettingsOverrideTrack::StaticClass();
}

bool FKawaiiPhysicsSettingsOverrideTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return InSequence &&
		InSequence->IsTrackSupported(UMovieSceneKawaiiPhysicsSettingsOverrideTrack::StaticClass()) !=
		ETrackSupport::NotSupported;
}

void FKawaiiPhysicsSettingsOverrideTrackEditor::BuildObjectBindingTrackMenu(
	FMenuBuilder& MenuBuilder,
	const TArray<FGuid>& ObjectBindings,
	const UClass* ObjectClass)
{
	if (!ObjectClass ||
		(!ObjectClass->IsChildOf(AActor::StaticClass()) &&
		 !ObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass())))
	{
		return;
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddKawaiiPhysicsSettingsOverrideTrack", "Kawaii Physics Settings Override"),
		LOCTEXT(
			"AddKawaiiPhysicsSettingsOverrideTrackTooltip",
			"Adds a track that drives Kawaii Physics settings multiplier overrides on the bound skeletal mesh components."),
		FSlateIcon(FKawaiiPhysicsEdStyle::GetStyleSetName(), TEXT("KawaiiPhysics.TabIcon")),
		FUIAction(FExecuteAction::CreateSP(
			this,
			&FKawaiiPhysicsSettingsOverrideTrackEditor::HandleAddTrack,
			ObjectBindings)));
}

TSharedRef<ISequencerSection> FKawaiiPhysicsSettingsOverrideTrackEditor::MakeSectionInterface(
	UMovieSceneSection& SectionObject,
	UMovieSceneTrack& Track,
	FGuid ObjectBinding)
{
	(void)ObjectBinding;
	check(SupportsType(Track.GetClass()));
	return MakeShared<FKawaiiPhysicsSettingsOverrideSectionInterface>(SectionObject);
}

const FSlateBrush* FKawaiiPhysicsSettingsOverrideTrackEditor::GetIconBrush() const
{
	if (const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(FKawaiiPhysicsEdStyle::GetStyleSetName()))
	{
		return Style->GetBrush(TEXT("KawaiiPhysics.TabIcon"));
	}

	return nullptr;
}

void FKawaiiPhysicsSettingsOverrideTrackEditor::HandleAddTrack(TArray<FGuid> ObjectBindings)
{
	const FScopedTransaction Transaction(
		LOCTEXT("AddKawaiiPhysicsSettingsOverrideTrackTransaction", "Add Kawaii Physics Settings Override Track"));

	const TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return;
	}

	for (const FGuid& ObjectBinding : ObjectBindings)
	{
		UObject* Object = SequencerPtr->FindSpawnedObjectOrTemplate(ObjectBinding);
		if (Object)
		{
			AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(
				this,
				&FKawaiiPhysicsSettingsOverrideTrackEditor::AddTrackInternal,
				Object));
		}
	}
}

FKeyPropertyResult FKawaiiPhysicsSettingsOverrideTrackEditor::AddTrackInternal(
	const FFrameNumber KeyTime,
	UObject* Object)
{
	FKeyPropertyResult KeyPropertyResult;

	if (!Object)
	{
		return KeyPropertyResult;
	}

	const TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return KeyPropertyResult;
	}

	const FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToObject(Object);
	KeyPropertyResult.bHandleCreated |= HandleResult.bWasCreated;

	if (!HandleResult.Handle.IsValid())
	{
		return KeyPropertyResult;
	}

	FFindOrCreateTrackResult TrackResult = FindOrCreateTrackForObject(
		HandleResult.Handle,
		UMovieSceneKawaiiPhysicsSettingsOverrideTrack::StaticClass());
	KeyPropertyResult.bTrackCreated |= TrackResult.bWasCreated;

	if (TrackResult.bWasCreated && ensure(TrackResult.Track))
	{
		UMovieSceneSection* Section = TrackResult.Track->CreateNewSection();
		if (ensure(Section))
		{
			const FFrameNumber DurationFrames = SequencerPtr->GetFocusedTickResolution().AsFrameNumber(1.0);
			Section->InitialPlacement(
				TrackResult.Track->GetAllSections(),
				KeyTime,
				FMath::Max(1, DurationFrames.Value),
				TrackResult.Track->SupportsMultipleRows());
			TrackResult.Track->AddSection(*Section);

			KeyPropertyResult.bTrackModified = true;
			KeyPropertyResult.SectionsCreated.Add(Section);
		}
	}

	return KeyPropertyResult;
}

FKawaiiPhysicsSettingsOverrideSectionInterface::FKawaiiPhysicsSettingsOverrideSectionInterface(
	UMovieSceneSection& InSection)
	: FSequencerSection(InSection)
{
}

FText FKawaiiPhysicsSettingsOverrideSectionInterface::GetSectionTitle() const
{
	if (const UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section =
		Cast<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(WeakSection.Get()))
	{
		return MakeKawaiiPhysicsScaleSummaryText(Section->Scale);
	}

	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
