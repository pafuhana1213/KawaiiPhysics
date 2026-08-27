// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISequencerSection.h"
#include "MovieSceneTrackEditor.h"

/**
 * Sequencer に Kawaii Physics Settings Multiplier トラックを追加する TrackEditor
 * Track editor that adds Kawaii Physics Settings Multiplier tracks to Sequencer.
 * 同一 SkeletalMeshComponent に複数セクション/トラックが重なる場合、倍率は成分ごとに乗算合成される
 * Overlapping sections or tracks on the same component multiply their scales per component.
 */
class FKawaiiPhysicsSettingsMultiplierTrackEditor : public FMovieSceneTrackEditor
{
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer);
	explicit FKawaiiPhysicsSettingsMultiplierTrackEditor(TSharedRef<ISequencer> InSequencer);

	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
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
	void HandleAddRootTrack();
	FKeyPropertyResult AddTrackInternal(FFrameNumber KeyTime, UObject* Object);
};

/**
 * Kawaii Physics Settings Multiplier セクションの表示 UI
 * Display UI for Kawaii Physics Settings Multiplier sections.
 */
class FKawaiiPhysicsSettingsMultiplierSectionInterface : public FSequencerSection
{
public:
	FKawaiiPhysicsSettingsMultiplierSectionInterface(
		UMovieSceneSection& InSection,
		TWeakPtr<ISequencer> InSequencer);

	virtual FText GetSectionTitle() const override;
	virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override;
	virtual void BuildSectionContextMenu(FMenuBuilder& MenuBuilder, const FGuid& ObjectBinding) override;

private:
	TWeakPtr<ISequencer> WeakSequencer;
};
