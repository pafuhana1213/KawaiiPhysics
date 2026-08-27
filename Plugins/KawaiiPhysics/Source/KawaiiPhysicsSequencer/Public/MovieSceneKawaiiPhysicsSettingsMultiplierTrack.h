// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "MovieSceneNameableTrack.h"

#include "MovieSceneKawaiiPhysicsSettingsMultiplierTrack.generated.h"

/**
 * Kawaii Physics 設定倍率を Sequencer から駆動するトラック。
 * 同一 SkeletalMeshComponent に複数セクション/トラックが重なる場合、倍率は成分ごとに乗算合成される / Overlapping sections or tracks on the same component multiply their scales per component
 */
UCLASS(MinimalAPI)
class UMovieSceneKawaiiPhysicsSettingsMultiplierTrack
	: public UMovieSceneNameableTrack
	, public IMovieSceneTrackTemplateProducer
{
	GENERATED_BODY()

public:
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack(const FObjectInitializer& ObjectInitializer);

	// バインディング無しのルートトラックとして追加された場合 true。再生コンテキストの World 内の全 SkeletalMeshComponent を対象にする / True when added as a root track without an object binding; targets every SkeletalMeshComponent in the playback world
	UPROPERTY()
	bool bIsRootTrack = false;

	// UMovieSceneTrack
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual bool IsEmpty() const override;
	virtual void RemoveAllAnimationData() override;
	virtual bool SupportsMultipleRows() const override { return true; }

#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif

	// IMovieSceneTrackTemplateProducer
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;

private:
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};
