// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISequencerSection.h"
#include "MovieSceneTrackEditor.h"

/**
 * Sequencer に Kawaii Physics Settings Override トラックを追加する TrackEditor
 * Track editor that adds Kawaii Physics Settings Override tracks to Sequencer.
 */
class FKawaiiPhysicsSettingsOverrideTrackEditor : public FMovieSceneTrackEditor
{
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer);
	explicit FKawaiiPhysicsSettingsOverrideTrackEditor(TSharedRef<ISequencer> InSequencer);

	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	virtual void BuildObjectBindingTrackMenu(
		FMenuBuilder& MenuBuilder,
		const TArray<FGuid>& ObjectBindings,
		const UClass* ObjectClass) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(
		UMovieSceneSection& SectionObject,
		UMovieSceneTrack& Track,
		FGuid ObjectBinding) override;
	virtual const FSlateBrush* GetIconBrush() const override;

private:
	void HandleAddTrack(TArray<FGuid> ObjectBindings);
	FKeyPropertyResult AddTrackInternal(FFrameNumber KeyTime, UObject* Object);
};

/**
 * Kawaii Physics Settings Override セクションの表示 UI
 * Display UI for Kawaii Physics Settings Override sections.
 */
class FKawaiiPhysicsSettingsOverrideSectionInterface : public FSequencerSection
{
public:
	explicit FKawaiiPhysicsSettingsOverrideSectionInterface(UMovieSceneSection& InSection);

	virtual FText GetSectionTitle() const override;
};
