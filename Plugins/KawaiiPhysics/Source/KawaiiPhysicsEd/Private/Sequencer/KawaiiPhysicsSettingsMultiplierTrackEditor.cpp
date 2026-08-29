// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Sequencer/KawaiiPhysicsSettingsMultiplierTrackEditor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "KawaiiPhysicsEdStyle.h"
#include "KawaiiPhysicsSequencerMultiplierRegistry.h"
#include "MovieScene.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierTrack.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequence.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Sequencer/KawaiiPhysicsSettingsMultiplierSectionPresets.h"
#include "Sequencer/KawaiiPhysicsSettingsMultiplierSectionSummary.h"
#include "SequencerSectionPainter.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "TimeToPixel.h"

#define LOCTEXT_NAMESPACE "FKawaiiPhysicsSettingsMultiplierTrackEditor"

namespace
{
struct FScalePreset
{
	FText Label;
	FKawaiiPhysicsSettingsMultiplier Scale;
};

FKawaiiPhysicsSettingsMultiplier MakeScalePreset(const float Stiffness, const float Damping)
{
	FKawaiiPhysicsSettingsMultiplier Scale;
	Scale.Damping = Damping;
	Scale.Stiffness = Stiffness;
	return Scale;
}

const FScalePreset* GetKawaiiPhysicsScalePresets(int32& OutNumPresets)
{
	static const FScalePreset ScalePresets[] = {
		{LOCTEXT("PresetStiff", "Stiff"), MakeScalePreset(2.0f, 1.5f)},
		{LOCTEXT("PresetLoose", "Loose"), MakeScalePreset(0.5f, 0.7f)},
		{LOCTEXT("PresetFreeze", "Freeze"), MakeScalePreset(10.0f, 10.0f)}
	};
	OutNumPresets = UE_ARRAY_COUNT(ScalePresets);
	return ScalePresets;
}

FString KawaiiPhysicsMakeShortTagName(const FGameplayTag& Tag)
{
	FString TagName = Tag.GetTagName().ToString();
	int32 DotIndex = INDEX_NONE;
	if (TagName.FindLastChar(TEXT('.'), DotIndex))
	{
		// RightChopInline の第 2 引数は 5.4 で bool→EAllowShrinking に変わったため、版差の無い RightChop を使う
		TagName = TagName.RightChop(DotIndex + 1);
	}
	return TagName;
}

FText KawaiiPhysicsMakeFilterTagsText(const FGameplayTagContainer& FilterTags)
{
	TArray<FGameplayTag> Tags;
	FilterTags.GetGameplayTagArray(Tags);

	constexpr int32 MaxVisibleTags = 3;
	TArray<FString> ShortNames;
	for (int32 TagIndex = 0; TagIndex < FMath::Min(Tags.Num(), MaxVisibleTags); ++TagIndex)
	{
		ShortNames.Add(KawaiiPhysicsMakeShortTagName(Tags[TagIndex]));
	}

	FText TagListText = FText::FromString(FString::Join(ShortNames, TEXT(", ")));
	if (Tags.Num() > MaxVisibleTags)
	{
		const FText MoreText = FText::Format(
			LOCTEXT("MoreTagsFormat", " +{0}"),
			FText::AsNumber(Tags.Num() - MaxVisibleTags));
		TagListText = FText::Format(
			LOCTEXT("SectionTagsWithMore", "{0}{1}"),
			TagListText,
			MoreText);
	}

	return FText::Format(LOCTEXT("SectionTagsFormat", "[{0}]"), TagListText);
}

bool KawaiiPhysicsBindingHasSettingsMultiplierTrack(UMovieScene* MovieScene, const FGuid& ObjectBinding)
{
	if (!MovieScene || !ObjectBinding.IsValid())
	{
		return false;
	}

	return MovieScene->FindTrack(UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::StaticClass(), ObjectBinding) !=
		nullptr;
}

bool KawaiiPhysicsAnyParentBindingHasSettingsMultiplierTrack(
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
			KawaiiPhysicsBindingHasSettingsMultiplierTrack(MovieScene, Possessable->GetParent()))
		{
			return true;
		}
	}

	return false;
}

bool KawaiiPhysicsAnyChildComponentBindingHasSettingsMultiplierTrack(
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
			KawaiiPhysicsBindingHasSettingsMultiplierTrack(MovieScene, Possessable.GetGuid()))
		{
			return true;
		}
	}

	return false;
}

FText KawaiiPhysicsAppendTrackWarning(const FText& ToolTip, const FText& Warning)
{
	return FText::Format(LOCTEXT("AddKawaiiPhysicsSettingsMultiplierTrackTooltipWithWarning", "{0}{1}"),
	                     ToolTip, Warning);
}

// FSequencerSection はセクション UI の再構築で破棄されうるため、メニューアクションは this を捕捉せず弱参照だけを持つ
void KawaiiPhysicsApplyScaleToSectionWithTransaction(
	TWeakObjectPtr<UMovieSceneKawaiiPhysicsSettingsMultiplierSection> WeakSection,
	TWeakPtr<ISequencer> WeakSequencer,
	const FText& TransactionText,
	const FKawaiiPhysicsSettingsMultiplier& Scale)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = WeakSection.Get();
	if (!Section)
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	Section->Modify();
	ApplyKawaiiPhysicsScalePresetToSection(*Section, Scale);

	if (const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin())
	{
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
}
}

TSharedRef<ISequencerTrackEditor> FKawaiiPhysicsSettingsMultiplierTrackEditor::CreateTrackEditor(
	TSharedRef<ISequencer> InSequencer)
{
	return MakeShared<FKawaiiPhysicsSettingsMultiplierTrackEditor>(InSequencer);
}

FKawaiiPhysicsSettingsMultiplierTrackEditor::FKawaiiPhysicsSettingsMultiplierTrackEditor(
	TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

bool FKawaiiPhysicsSettingsMultiplierTrackEditor::SupportsType(const TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::StaticClass();
}

bool FKawaiiPhysicsSettingsMultiplierTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return InSequence &&
		InSequence->IsTrackSupported(UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::StaticClass()) !=
		ETrackSupport::NotSupported;
}

void FKawaiiPhysicsSettingsMultiplierTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddKawaiiPhysicsSettingsMultiplierRootTrack", "Kawaii Physics Settings Multiplier (All)"),
		LOCTEXT(
			"AddKawaiiPhysicsSettingsMultiplierRootTrackTooltip",
			"Adds a root track that drives Kawaii Physics settings multiplier overrides on every skeletal mesh component in the playback world. Filter Tags are required; an empty filter does nothing."),
		FSlateIcon(FKawaiiPhysicsEdStyle::GetStyleSetName(), TEXT("KawaiiPhysics.TabIcon")),
		FUIAction(FExecuteAction::CreateSP(
			this,
			&FKawaiiPhysicsSettingsMultiplierTrackEditor::HandleAddRootTrack)));
}

void FKawaiiPhysicsSettingsMultiplierTrackEditor::BuildObjectBindingTrackMenu(
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
		"AddKawaiiPhysicsSettingsMultiplierTrackTooltip",
		"Adds a track that drives Kawaii Physics settings multiplier overrides on the bound skeletal mesh components.");

	UMovieScene* MovieScene = GetSequencer().IsValid() ? GetFocusedMovieScene() : nullptr;
	if (ObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass()) &&
		KawaiiPhysicsAnyParentBindingHasSettingsMultiplierTrack(MovieScene, ObjectBindings))
	{
		ToolTip = KawaiiPhysicsAppendTrackWarning(
			ToolTip,
			LOCTEXT(
				"AddKawaiiPhysicsSettingsMultiplierTrackParentWarning",
				"\nNote: the parent actor binding already has this track. Overlapping sections on the actor and this component multiply together."));
	}
	else if (ObjectClass->IsChildOf(AActor::StaticClass()) &&
	         KawaiiPhysicsAnyChildComponentBindingHasSettingsMultiplierTrack(MovieScene, ObjectBindings))
	{
		ToolTip = KawaiiPhysicsAppendTrackWarning(
			ToolTip,
			LOCTEXT(
				"AddKawaiiPhysicsSettingsMultiplierTrackChildWarning",
				"\nNote: a child component binding already has this track. Overlapping sections on the actor and this component multiply together."));
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddKawaiiPhysicsSettingsMultiplierTrack", "Kawaii Physics Settings Multiplier"),
		ToolTip,
		FSlateIcon(FKawaiiPhysicsEdStyle::GetStyleSetName(), TEXT("KawaiiPhysics.TabIcon")),
		FUIAction(FExecuteAction::CreateSP(
			this,
			&FKawaiiPhysicsSettingsMultiplierTrackEditor::HandleAddTrack,
			ObjectBindings)));
}

TSharedRef<ISequencerSection> FKawaiiPhysicsSettingsMultiplierTrackEditor::MakeSectionInterface(
	UMovieSceneSection& SectionObject,
	UMovieSceneTrack& Track,
	FGuid ObjectBinding)
{
	(void)ObjectBinding;
	check(SupportsType(Track.GetClass()));
	return MakeShared<FKawaiiPhysicsSettingsMultiplierSectionInterface>(SectionObject, GetSequencer());
}

const FSlateBrush* FKawaiiPhysicsSettingsMultiplierTrackEditor::GetIconBrush() const
{
	if (const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(FKawaiiPhysicsEdStyle::GetStyleSetName()))
	{
		return Style->GetBrush(TEXT("KawaiiPhysics.TabIcon"));
	}

	return nullptr;
}

void FKawaiiPhysicsSettingsMultiplierTrackEditor::HandleAddTrack(TArray<FGuid> ObjectBindings)
{
	const FScopedTransaction Transaction(
		LOCTEXT("AddKawaiiPhysicsSettingsMultiplierTrackTransaction", "Add Kawaii Physics Settings Multiplier Track"));

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
				&FKawaiiPhysicsSettingsMultiplierTrackEditor::AddTrackInternal,
				Object));
		}
	}
}

void FKawaiiPhysicsSettingsMultiplierTrackEditor::HandleAddRootTrack()
{
	const TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid())
	{
		return;
	}

	UMovieScene* MovieScene = GetFocusedMovieScene();
	if (!MovieScene)
	{
		return;
	}

	const FScopedTransaction Transaction(
		LOCTEXT("AddKawaiiPhysicsSettingsMultiplierRootTrackTransaction",
		        "Add Kawaii Physics Settings Multiplier Root Track"));

	// FindOrCreateRootTrack はクラス一致のみで既存トラックを拾ってしまい、バインディング付きで同クラスの
	// トラックが root Tracks 配列に紛れ込んでいる場合に誤って早期 return してしまう。
	// bIsRootTrack フラグが立っている本物の root track だけを探す。
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track = nullptr;
	for (UMovieSceneTrack* ExistingTrack : MovieScene->GetTracks())
	{
		UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* ExistingRootTrack =
			Cast<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(ExistingTrack);
		if (ExistingRootTrack && ExistingRootTrack->bIsRootTrack)
		{
			Track = ExistingRootTrack;
			break;
		}
	}

	if (!Track)
	{
		MovieScene->Modify();
		Track = MovieScene->AddTrack<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>();
		if (!Track)
		{
			return;
		}

		Track->Modify();
		Track->bIsRootTrack = true;
	}

	Track->Modify();
	UMovieSceneSection* Section = Track->CreateNewSection();
	if (!ensure(Section))
	{
		return;
	}

	const FFrameNumber KeyTime = SequencerPtr->GetLocalTime().Time.FrameNumber;
	const FFrameNumber DurationFrames = SequencerPtr->GetFocusedTickResolution().AsFrameNumber(1.0);
	Section->InitialPlacement(
		Track->GetAllSections(),
		KeyTime,
		FMath::Max(1, DurationFrames.Value),
		Track->SupportsMultipleRows());
	Track->AddSection(*Section);

	SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
}

FKeyPropertyResult FKawaiiPhysicsSettingsMultiplierTrackEditor::AddTrackInternal(
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
		UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::StaticClass());
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

FKawaiiPhysicsSettingsMultiplierSectionInterface::FKawaiiPhysicsSettingsMultiplierSectionInterface(
	UMovieSceneSection& InSection,
	TWeakPtr<ISequencer> InSequencer)
	: FSequencerSection(InSection)
	, WeakSequencer(InSequencer)
{
}

FText FKawaiiPhysicsSettingsMultiplierSectionInterface::GetSectionTitle() const
{
	if (const UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section =
		Cast<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(WeakSection.Get()))
	{
		const FFrameTime StartTime = Section->HasStartFrame()
			                             ? FFrameTime(Section->GetInclusiveStartFrame())
			                             : FFrameTime(0);
		const FText Summary = MakeKawaiiPhysicsScaleSummaryText(Section->EvaluateScaleAtTime(StartTime));
		const UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track =
			Section->GetTypedOuter<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>();
		const bool bNeedsRootFilterWarning = Track && Track->bIsRootTrack && Section->FilterTags.IsEmpty();
		const auto AddRootFilterWarning = [bNeedsRootFilterWarning](const FText& Title)
		{
			if (!bNeedsRootFilterWarning)
			{
				return Title;
			}

			return FText::Format(
				LOCTEXT("RootTrackNoFilterWarningFormat", "{0}{1}"),
				LOCTEXT("RootTrackNoFilterWarning", "[Filter Tags required] "),
				Title);
		};

		bool bHasLiveEntry = false;
		const int32 QueuedNodeCount = FKawaiiPhysicsSequencerMultiplierRegistry::Get().GetQueuedNodeCount(
			Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
		FText Suffix = FText::GetEmpty();
		// 生存 Entry があって 0 件なら、フィルタ無し（全ノード対象）でも「一致なし」＝対象 Component に KawaiiPhysics ノードが無い
		if (bHasLiveEntry && QueuedNodeCount == 0)
		{
			Suffix = LOCTEXT("NoMatchSuffix", " (no match)");
		}
		else if (bHasLiveEntry && QueuedNodeCount > 0)
		{
			Suffix = FText::Format(LOCTEXT("NodeCountSuffix", " ({0} nodes)"), QueuedNodeCount);
		}

		if (!Suffix.IsEmpty())
		{
			const FText SummaryWithSuffix = FText::Format(
				LOCTEXT("SectionTitleWithNodeCountSuffix", "{0}{1}"),
				Summary,
				Suffix);
			if (!Section->FilterTags.IsEmpty())
			{
				return AddRootFilterWarning(FText::Format(
					LOCTEXT("SectionTitleWithTags", "{0} {1}"),
					KawaiiPhysicsMakeFilterTagsText(Section->FilterTags),
					SummaryWithSuffix));
			}
			return AddRootFilterWarning(SummaryWithSuffix);
		}

		if (!Section->FilterTags.IsEmpty())
		{
			return AddRootFilterWarning(FText::Format(
				LOCTEXT("SectionTitleWithTags", "{0} {1}"),
				KawaiiPhysicsMakeFilterTagsText(Section->FilterTags),
				Summary));
		}

		return AddRootFilterWarning(Summary);
	}

	return FText::GetEmpty();
}

int32 FKawaiiPhysicsSettingsMultiplierSectionInterface::OnPaintSection(FSequencerSectionPainter& Painter) const
{
	const int32 LayerId = Painter.PaintSectionBackground();

	const UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section =
		Cast<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(WeakSection.Get());
	if (!Section || !Section->HasStartFrame() || !Section->HasEndFrame())
	{
		return LayerId + 1;
	}

	const FFrameNumber StartFrame = Section->GetInclusiveStartFrame();
	const FFrameNumber EndFrame = Section->GetExclusiveEndFrame();
	if (EndFrame <= StartFrame)
	{
		return LayerId + 1;
	}

	constexpr int32 SampleSegments = 32;
	constexpr float PaddingY = 2.0f;
	const FVector2D SectionSize = Painter.SectionGeometry.GetLocalSize();
	const float TopY = PaddingY;
	const float BottomY = FMath::Max(TopY, SectionSize.Y - PaddingY);
	const FTimeToPixel& TimeToPixel = Painter.GetTimeConverter();

	TArray<FVector2D> Points;
	Points.Reserve(SampleSegments + 1);
	for (int32 SampleIndex = 0; SampleIndex <= SampleSegments; ++SampleIndex)
	{
		const double Alpha = static_cast<double>(SampleIndex) / static_cast<double>(SampleSegments);
		const double FrameValue = FMath::Lerp(
			static_cast<double>(StartFrame.Value),
			static_cast<double>(EndFrame.Value),
			Alpha);
		const int32 WholeFrame = FMath::FloorToInt(FrameValue);
		const FFrameTime SampleTime(FFrameNumber(WholeFrame), static_cast<float>(FrameValue - WholeFrame));
		const float Weight = FMath::Clamp(Section->EvaluateWeightAtTime(SampleTime), 0.0f, 1.0f);
		const float X = TimeToPixel.FrameToPixel(SampleTime);
		const float Y = FMath::Lerp(BottomY, TopY, Weight);
		Points.Add(FVector2D(X, Y));
	}

	FSlateDrawElement::MakeLines(
		Painter.DrawElements,
		LayerId + 1,
		Painter.SectionGeometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		FLinearColor(1.0f, 1.0f, 1.0f, 0.35f),
		true,
		1.0f);

	return LayerId + 1;
}

void FKawaiiPhysicsSettingsMultiplierSectionInterface::BuildSectionContextMenu(
	FMenuBuilder& MenuBuilder,
	const FGuid& ObjectBinding)
{
	(void)ObjectBinding;

	// FSequencerSection はセクション UI の再構築で破棄されうるため、メニューアクションは this を捕捉せず弱参照だけを持つ
	const TWeakObjectPtr<UMovieSceneKawaiiPhysicsSettingsMultiplierSection> WeakSectionObj =
		Cast<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(WeakSection.Get());
	const TWeakPtr<ISequencer> WeakSequencerCopy = WeakSequencer;

	MenuBuilder.BeginSection(
		FName(TEXT("KawaiiPhysicsSettingsMultiplier")),
		LOCTEXT("SectionMenuHeader", "Kawaii Physics"));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ResetScale", "Reset Scale to 1.0"),
		LOCTEXT(
			"ResetScaleTooltip",
			"Removes all keys from the six scale channels and resets their defaults to 1.0."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakSectionObj, WeakSequencerCopy]()
		{
			KawaiiPhysicsApplyScaleToSectionWithTransaction(
				WeakSectionObj,
				WeakSequencerCopy,
				LOCTEXT("ResetScaleTransaction", "Reset Kawaii Physics Scale"),
				FKawaiiPhysicsSettingsMultiplier());
		})));

	MenuBuilder.AddSubMenu(
		LOCTEXT("ApplyPreset", "Apply Preset"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateLambda([WeakSectionObj, WeakSequencerCopy](FMenuBuilder& SubMenuBuilder)
		{
			int32 NumPresets = 0;
			const FScalePreset* ScalePresets = GetKawaiiPhysicsScalePresets(NumPresets);
			for (int32 PresetIndex = 0; PresetIndex < NumPresets; ++PresetIndex)
			{
				const FScalePreset Preset = ScalePresets[PresetIndex];
				SubMenuBuilder.AddMenuEntry(
					Preset.Label,
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([WeakSectionObj, WeakSequencerCopy, Preset]()
					{
						KawaiiPhysicsApplyScaleToSectionWithTransaction(
							WeakSectionObj,
							WeakSequencerCopy,
							LOCTEXT("ApplyPresetTransaction", "Apply Kawaii Physics Scale Preset"),
							Preset.Scale);
					})));
			}
		}));
	MenuBuilder.EndSection();
}

#undef LOCTEXT_NAMESPACE
