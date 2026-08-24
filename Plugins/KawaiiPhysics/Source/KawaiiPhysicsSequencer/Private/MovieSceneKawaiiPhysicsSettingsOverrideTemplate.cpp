// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "MovieSceneKawaiiPhysicsSettingsOverrideTemplate.h"

#include "AnimNode_KawaiiPhysics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Evaluation/MovieSceneExecutionTokens.h"
#include "GameFramework/Actor.h"
#include "IMovieScenePlayer.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsSequencerOverrideRegistry.h"
#include "MovieSceneExecutionToken.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneKawaiiPhysicsSettingsOverrideTemplate)

namespace
{
struct FSectionData : IPersistentEvaluationData
{
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TSharedRef<FKawaiiPhysicsSequencerOverrideEntry>> Entries;
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> PreAnimatedSavedComponents;
	TSharedRef<uint8> InstanceOwner = MakeShared<uint8>(0);

	virtual ~FSectionData() override
	{
		for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, TSharedRef<FKawaiiPhysicsSequencerOverrideEntry>>& Pair :
		     Entries)
		{
			Pair.Value->Stop();
		}
	}
};

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
	                const UMovieSceneSection* InSourceSection)
		: Scale(InScale)
		, Alpha(InAlpha)
		, FilterTags(InFilterTags)
		, bFilterExactMatch(bInFilterExactMatch)
		, BlendOutTime(InBlendOutTime)
		, AnimTypeID(InAnimTypeID)
		, SourceSection(InSourceSection)
	{
	}

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand,
	                     FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		FSectionData& Data = PersistentData.GetOrAddSectionData<FSectionData>();

		TArray<USkeletalMeshComponent*> Components;
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

			UKawaiiPhysicsLibrary::SetPhysicsSettingsOverrideOnComponent(Component, Entry->Handle, Scale, Alpha,
			                                                             FilterTags, bFilterExactMatch);
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
};
}

FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate::
FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate(
	const UMovieSceneKawaiiPhysicsSettingsOverrideSection& Section)
	: Scale(Section.Scale)
	, Weight(Section.Weight)
	, FilterTags(Section.FilterTags)
	, bFilterExactMatch(Section.bFilterExactMatch)
	, BlendOutTimeOnEnd(Section.BlendOutTimeOnEnd)
{
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

	ExecutionTokens.Add(FExecutionToken(Scale, Alpha, FilterTags, bFilterExactMatch, BlendOutTimeOnEnd, AnimTypeID,
	                                    GetSourceSection()));
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
	}
}
