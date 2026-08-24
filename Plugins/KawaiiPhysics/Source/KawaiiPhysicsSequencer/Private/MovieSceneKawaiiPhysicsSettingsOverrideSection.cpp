// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.h"

#include "Channels/MovieSceneChannelEditorData.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "KawaiiPhysicsSequencerOverrideRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneKawaiiPhysicsSettingsOverrideSection)

#define LOCTEXT_NAMESPACE "MovieSceneKawaiiPhysicsSettingsOverrideSection"

UMovieSceneKawaiiPhysicsSettingsOverrideSection::UMovieSceneKawaiiPhysicsSettingsOverrideSection(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// SetBlendType は Outer Track の対応 BlendType を見るため、CDO でも確実に設定できるよう直接代入する
	BlendType = EMovieSceneBlendType::Absolute;

	// 外部駆動オーバーライドは区間終了時に Stop するため、KeepState は意味を持たせない
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::RestoreState);
	EvalOptions.bCanEditCompletionMode = false;

	Weight.SetDefault(1.0f);

	// ChannelProxy は非 UPROPERTY でメンバ参照を持つだけなので、PostLoad/Duplicate 後も同じメンバアドレスを参照し続けられる
#if WITH_EDITOR
	FMovieSceneChannelMetaData MetaData("Weight", LOCTEXT("WeightChannel", "Weight"));
	MetaData.bCanCollapseToTrack = false;
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(Weight, MetaData, TMovieSceneExternalValue<float>());
#else
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(Weight);
#endif
}

float UMovieSceneKawaiiPhysicsSettingsOverrideSection::EvaluateWeightAtTime(const FFrameTime InTime) const
{
	float WeightValue = 1.0f;
	Weight.Evaluate(InTime, WeightValue);
	return FMath::Clamp(WeightValue, 0.0f, 1.0f) * EvaluateEasing(InTime);
}

void UMovieSceneKawaiiPhysicsSettingsOverrideSection::BeginDestroy()
{
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(this);
	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
