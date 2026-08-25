// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "MovieSceneKawaiiPhysicsSettingsOverrideTemplate.h"

#include "AnimNode_KawaiiPhysics.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Evaluation/MovieSceneExecutionTokens.h"
#include "GameFramework/Actor.h"
#include "IMovieScenePlayer.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsSequencerOverrideRegistry.h"
#include "MovieSceneExecutionToken.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideChannels.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideTrack.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneKawaiiPhysicsSettingsOverrideTemplate)

namespace
{
struct FSectionData : IPersistentEvaluationData
{
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TSharedRef<FKawaiiPhysicsSequencerOverrideEntry>> Entries;
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> PreAnimatedSavedComponents;
	TSharedRef<uint8> InstanceOwner = MakeShared<uint8>(0);
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> RootScanCache;
	double RootScanCacheTime = -1.0;
	TWeakObjectPtr<UWorld> RootScanWorld;

	virtual ~FSectionData() override
	{
		for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, TSharedRef<FKawaiiPhysicsSequencerOverrideEntry>>& Pair :
		     Entries)
		{
			Pair.Value->Stop();
		}
	}
};

bool KawaiiPhysicsIsRootTrackPlaybackWorld(const UWorld* World)
{
	return World &&
		(World->WorldType == EWorldType::Game ||
		 World->WorldType == EWorldType::PIE ||
		 World->WorldType == EWorldType::Editor);
}

double KawaiiPhysicsGetRootScanTime(const UWorld& World)
{
	// World 時間はポーズ中に止まりキャッシュが永続してしまうため、常に実時間を使う
	(void)World;
	return FPlatformTime::Seconds();
}

bool KawaiiPhysicsIsRootScanCacheValid(const FSectionData& Data, const UWorld& World, const double CurrentTime)
{
	if (Data.RootScanWorld.Get() != &World ||
		Data.RootScanCacheTime < 0.0 ||
		CurrentTime < Data.RootScanCacheTime ||
		CurrentTime - Data.RootScanCacheTime > 0.5)
	{
		return false;
	}

	for (const TWeakObjectPtr<USkeletalMeshComponent>& CachedComponent : Data.RootScanCache)
	{
		const USkeletalMeshComponent* Component = CachedComponent.Get();
		if (!IsValid(Component) || Component->IsTemplate() || Component->GetWorld() != &World)
		{
			return false;
		}
	}

	return true;
}

void KawaiiPhysicsGatherRootTrackComponents(UWorld& World, FSectionData& Data,
                                            TArray<USkeletalMeshComponent*>& Components)
{
	const double CurrentTime = KawaiiPhysicsGetRootScanTime(World);
	if (KawaiiPhysicsIsRootScanCacheValid(Data, World, CurrentTime))
	{
		for (const TWeakObjectPtr<USkeletalMeshComponent>& CachedComponent : Data.RootScanCache)
		{
			Components.Add(CachedComponent.Get());
		}
		return;
	}

	Data.RootScanCache.Reset();
	Data.RootScanWorld = &World;
	Data.RootScanCacheTime = CurrentTime;

	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Component = *It;
		if (Component &&
			Component->GetWorld() == &World &&
			IsValid(Component) &&
			!Component->IsTemplate())
		{
			Components.Add(Component);
			Data.RootScanCache.Add(Component);
		}
	}
}

struct FPreAnimatedToken : IMovieScenePreAnimatedToken
{
	FPreAnimatedToken(const UMovieSceneSection* InSourceSection, const TWeakPtr<uint8>& InOwner)
		: SourceSection(InSourceSection)
		, Owner(InOwner)
	{
	}

	virtual void RestoreState(UObject& Object, const UE::MovieScene::FRestoreStateParams& Params) override
	{
		if (const UMovieSceneSection* Section = SourceSection.Get())
		{
			// SavePreAnimatedState は Component 単位なので、復元対象の Component の Entry だけ止める（兄弟 Component を巻き込まない）
			const USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(&Object);
			if (Component)
			{
				FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section, Owner, Component);
			}
			else
			{
				FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section, Owner);
			}
		}
	}

	TWeakObjectPtr<const UMovieSceneSection> SourceSection;
	TWeakPtr<uint8> Owner;
};

struct FPreAnimatedTokenProducer : IMovieScenePreAnimatedTokenProducer
{
	FPreAnimatedTokenProducer(const UMovieSceneSection* InSourceSection, const TSharedRef<uint8>& InOwner)
		: SourceSection(InSourceSection)
		, Owner(InOwner)
	{
	}

	virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
	{
		return FPreAnimatedToken(SourceSection.Get(), Owner);
	}

	TWeakObjectPtr<const UMovieSceneSection> SourceSection;
	TWeakPtr<uint8> Owner;
};

struct FExecutionToken : IMovieSceneExecutionToken
{
	FExecutionToken(const FKawaiiPhysicsSettingsScale& InScale, const float InAlpha,
	                const FGameplayTagContainer& InFilterTags, const bool bInFilterExactMatch,
	                const float InBlendOutTime, const FMovieSceneAnimTypeID InAnimTypeID,
	                const UMovieSceneSection* InSourceSection, const bool bInIsRootTrack)
		: Scale(InScale)
		, Alpha(InAlpha)
		, FilterTags(InFilterTags)
		, bFilterExactMatch(bInFilterExactMatch)
		, BlendOutTime(InBlendOutTime)
		, AnimTypeID(InAnimTypeID)
		, SourceSection(InSourceSection)
		, bIsRootTrack(bInIsRootTrack)
	{
	}

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand,
	                     FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		FSectionData& Data = PersistentData.GetOrAddSectionData<FSectionData>();

		TArray<USkeletalMeshComponent*> Components;
		if (bIsRootTrack)
		{
			UObject* ContextObject = Player.GetPlaybackContext();
			UWorld* World = ContextObject ? ContextObject->GetWorld() : nullptr;
			// FilterTags が空（安全装置で無効）／World が取れない／対象外のいずれでも、以前に適用した Entry を止めるため
			// 早期 return はせず、キャッシュを捨てて空の Components で後段へ流す
			if (FilterTags.IsEmpty() || !KawaiiPhysicsIsRootTrackPlaybackWorld(World))
			{
				Data.RootScanCache.Reset();
				Data.RootScanWorld = nullptr;
				Data.RootScanCacheTime = -1.0;
			}
			else
			{
				KawaiiPhysicsGatherRootTrackComponents(*World, Data, Components);
			}
		}
		else
		{
			for (const TWeakObjectPtr<> BoundObject : Player.FindBoundObjects(Operand))
			{
				UObject* Object = BoundObject.Get();
				if (USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(Object))
				{
					Components.AddUnique(Component);
					continue;
				}

				if (AActor* Actor = Cast<AActor>(Object))
				{
					TArray<USkeletalMeshComponent*> ActorComponents;
					Actor->GetComponents<USkeletalMeshComponent>(ActorComponents);
					for (USkeletalMeshComponent* ActorComponent : ActorComponents)
					{
						Components.AddUnique(ActorComponent);
					}
				}
			}
		}

		TSet<TWeakObjectPtr<USkeletalMeshComponent>> SeenComponents;
		for (USkeletalMeshComponent* Component : Components)
		{
			if (!IsValid(Component))
			{
				continue;
			}

			const TWeakObjectPtr<USkeletalMeshComponent> ComponentKey(Component);
			TSharedRef<FKawaiiPhysicsSequencerOverrideEntry>* ExistingEntry = Data.Entries.Find(ComponentKey);
			if (ExistingEntry && (*ExistingEntry)->bStopped)
			{
				// 停止済み Entry を再駆動すると以後の Stop が no-op になるため破棄して作り直す
				Data.Entries.Remove(ComponentKey);
				ExistingEntry = nullptr;
			}
			if (ExistingEntry &&
				(!((*ExistingEntry)->FilterTags == FilterTags) ||
					(*ExistingEntry)->bFilterExactMatch != bFilterExactMatch ||
					!FMath::IsNearlyEqual((*ExistingEntry)->BlendOutTime, BlendOutTime)))
			{
				// フィルタ変更後に旧フィルタで Stop すると新しく駆動したノードを取りこぼすため、旧 Entry を旧フィルタで止めてから作り直す
				(*ExistingEntry)->Stop();
				Data.Entries.Remove(ComponentKey);
				ExistingEntry = nullptr;
			}

			TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry =
				ExistingEntry ? *ExistingEntry : MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
			if (!ExistingEntry)
			{
				Entry->Component = Component;
				Entry->Owner = Data.InstanceOwner;
				Entry->Handle.Id = FAnimNode_KawaiiPhysics::GenerateTransientForceHandleId();
				Entry->FilterTags = FilterTags;
				Entry->bFilterExactMatch = bFilterExactMatch;
				Entry->BlendOutTime = BlendOutTime;
				Data.Entries.Add(ComponentKey, Entry);

				if (const UMovieSceneSection* Section = SourceSection.Get())
				{
					FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, Entry);
				}
			}

			if (!Data.PreAnimatedSavedComponents.Contains(ComponentKey))
			{
				if (const UMovieSceneSection* Section = SourceSection.Get())
				{
					Player.SavePreAnimatedState(*Component, AnimTypeID,
					                            FPreAnimatedTokenProducer(Section, Data.InstanceOwner));
					Data.PreAnimatedSavedComponents.Add(ComponentKey);
				}
			}

			const int32 PreviousQueuedNodeCount = Entry->LastQueuedNodeCount;
			const int32 NewQueuedNodeCount =
				UKawaiiPhysicsLibrary::SetPhysicsSettingsOverrideOnComponent(Component, Entry->Handle, Scale, Alpha,
				                                                             FilterTags, bFilterExactMatch);
			Entry->LastQueuedNodeCount = NewQueuedNodeCount;
			if (PreviousQueuedNodeCount > 0 && NewQueuedNodeCount == 0)
			{
				// 駆動済みノードがセクション中にフィルタ外へ出た場合は全ノードに Stop を届かせて Entry を破棄する
				Entry->Stop();
				Data.Entries.Remove(ComponentKey);
				continue;
			}
			SeenComponents.Add(ComponentKey);
		}

		for (auto It = Data.Entries.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || !SeenComponents.Contains(It.Key()))
			{
				It.Value()->Stop();
				It.RemoveCurrent();
			}
		}
	}

	FKawaiiPhysicsSettingsScale Scale;
	float Alpha = 1.0f;
	FGameplayTagContainer FilterTags;
	bool bFilterExactMatch = false;
	float BlendOutTime = 0.2f;
	FMovieSceneAnimTypeID AnimTypeID;
	TWeakObjectPtr<const UMovieSceneSection> SourceSection;
	bool bIsRootTrack = false;
};
}

FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate::
FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate(
	const UMovieSceneKawaiiPhysicsSettingsOverrideSection& Section)
	: Damping(Section.Damping)
	, Stiffness(Section.Stiffness)
	, WorldDampingLocation(Section.WorldDampingLocation)
	, WorldDampingRotation(Section.WorldDampingRotation)
	, Radius(Section.Radius)
	, LimitAngle(Section.LimitAngle)
	, Weight(Section.Weight)
	, FilterTags(Section.FilterTags)
	, bFilterExactMatch(Section.bFilterExactMatch)
	, BlendOutTimeOnEnd(Section.BlendOutTimeOnEnd)
	, bIsRootTrack(false)
{
	if (const UMovieSceneKawaiiPhysicsSettingsOverrideTrack* Track =
		Section.GetTypedOuter<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>())
	{
		bIsRootTrack = Track->bIsRootTrack;
	}

	SetSourceSection(&Section);
	SetCompletionMode(Section.GetCompletionMode());
}

void FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate::Evaluate(
	const FMovieSceneEvaluationOperand& Operand,
	const FMovieSceneContext& Context,
	const FPersistentEvaluationData& PersistentData,
	FMovieSceneExecutionTokens& ExecutionTokens) const
{
	float WeightValue = 1.0f;
	Weight.Evaluate(Context.GetTime(), WeightValue);
	const float Alpha = FMath::Clamp(WeightValue, 0.0f, 1.0f) * EvaluateEasing(Context.GetTime());
	const FMovieSceneFloatChannel* const Channels[6] = {
		&Damping,
		&Stiffness,
		&WorldDampingLocation,
		&WorldDampingRotation,
		&Radius,
		&LimitAngle
	};
	const FKawaiiPhysicsSettingsScale Scale =
		KawaiiPhysicsSequencer::EvaluateKawaiiPhysicsScaleChannels(Channels, Context.GetTime());

	ExecutionTokens.Add(FExecutionToken(Scale, Alpha, FilterTags, bFilterExactMatch, BlendOutTimeOnEnd, AnimTypeID,
	                                    GetSourceSection(), bIsRootTrack));
}

void FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate::TearDown(
	FPersistentEvaluationData& PersistentData,
	IMovieScenePlayer& Player) const
{
	if (FSectionData* Data = PersistentData.FindSectionData<FSectionData>())
	{
		for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, TSharedRef<FKawaiiPhysicsSequencerOverrideEntry>>& Pair :
		     Data->Entries)
		{
			Pair.Value->Stop();
		}
		Data->Entries.Empty();
		Data->PreAnimatedSavedComponents.Empty();
		Data->RootScanCache.Empty();
		Data->RootScanWorld.Reset();
		Data->RootScanCacheTime = -1.0;
	}
}
