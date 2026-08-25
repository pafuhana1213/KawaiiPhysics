// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Sequencer/KawaiiPhysicsSettingsOverrideTrackEditor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "KawaiiPhysicsEdStyle.h"
#include "MovieScene.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideTrack.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequence.h"
#include "ScopedTransaction.h"
#include "Sequencer/KawaiiPhysicsSettingsOverrideSectionSummary.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

#define LOCTEXT_NAMESPACE "FKawaiiPhysicsSettingsOverrideTrackEditor"

namespace
{
bool KawaiiPhysicsBindingHasSettingsOverrideTrack(UMovieScene* MovieScene, const FGuid& ObjectBinding)
{
	if (!MovieScene || !ObjectBinding.IsValid())
	{
		return false;
	}

	return MovieScene->FindTrack(UMovieSceneKawaiiPhysicsSettingsOverrideTrack::StaticClass(), ObjectBinding) !=
		nullptr;
}

bool KawaiiPhysicsAnyParentBindingHasSettingsOverrideTrack(
	UMovieScene* MovieScene,
	const TArray<FGuid>& ObjectBindings)
{
	if (!MovieScene)
	{
		return false;
	}

	for (const FGuid& ObjectBinding : ObjectBindings)
	{
		const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectBinding);
		if (Possessable &&
			KawaiiPhysicsBindingHasSettingsOverrideTrack(MovieScene, Possessable->GetParent()))
		{
			return true;
		}
	}

	return false;
}

bool KawaiiPhysicsAnyChildComponentBindingHasSettingsOverrideTrack(
	UMovieScene* MovieScene,
	const TArray<FGuid>& ObjectBindings)
{
	if (!MovieScene)
	{
		return false;
	}

	TSet<FGuid> ParentBindings;
	for (const FGuid& ObjectBinding : ObjectBindings)
	{
		ParentBindings.Add(ObjectBinding);
	}

	for (int32 PossessableIndex = 0; PossessableIndex < MovieScene->GetPossessableCount(); ++PossessableIndex)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(PossessableIndex);
		const UClass* PossessedObjectClass = Possessable.GetPossessedObjectClass();
		if (ParentBindings.Contains(Possessable.GetParent()) &&
			PossessedObjectClass &&
			PossessedObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass()) &&
			KawaiiPhysicsBindingHasSettingsOverrideTrack(MovieScene, Possessable.GetGuid()))
		{
			return true;
		}
	}

	return false;
}

FText KawaiiPhysicsAppendTrackWarning(const FText& ToolTip, const FText& Warning)
{
	return FText::Format(LOCTEXT("AddKawaiiPhysicsSettingsOverrideTrackTooltipWithWarning", "{0}{1}"),
	                     ToolTip, Warning);
}
}

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

	FText ToolTip = LOCTEXT(
		"AddKawaiiPhysicsSettingsOverrideTrackTooltip",
		"Adds a track that drives Kawaii Physics settings multiplier overrides on the bound skeletal mesh components.");

	UMovieScene* MovieScene = GetSequencer().IsValid() ? GetFocusedMovieScene() : nullptr;
	if (ObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass()) &&
		KawaiiPhysicsAnyParentBindingHasSettingsOverrideTrack(MovieScene, ObjectBindings))
	{
		ToolTip = KawaiiPhysicsAppendTrackWarning(
			ToolTip,
			LOCTEXT(
				"AddKawaiiPhysicsSettingsOverrideTrackParentWarning",
				"\nNote: the parent actor binding already has this track. Overlapping sections on the actor and this component multiply together."));
	}
	else if (ObjectClass->IsChildOf(AActor::StaticClass()) &&
	         KawaiiPhysicsAnyChildComponentBindingHasSettingsOverrideTrack(MovieScene, ObjectBindings))
	{
		ToolTip = KawaiiPhysicsAppendTrackWarning(
			ToolTip,
			LOCTEXT(
				"AddKawaiiPhysicsSettingsOverrideTrackChildWarning",
				"\nNote: a child component binding already has this track. Overlapping sections on the actor and this component multiply together."));
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddKawaiiPhysicsSettingsOverrideTrack", "Kawaii Physics Settings Override"),
		ToolTip,
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

	// 既存トラックへ 2 回目以降に追加した場合もセクションを作る（無反応にしない）
	if (ensure(TrackResult.Track))
	{
		// 既存トラックの Sections を書き換えるため、トランザクションに事前状態を積む（Undo で追加分が戻るように）
		TrackResult.Track->Modify();
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
