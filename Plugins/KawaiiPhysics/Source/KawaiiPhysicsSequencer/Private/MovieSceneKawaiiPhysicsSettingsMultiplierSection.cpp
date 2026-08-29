// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.h"

#include "Channels/MovieSceneChannelEditorData.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierChannels.h"
#include "KawaiiPhysicsSequencerMultiplierRegistry.h"
#include "MovieSceneTrack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneKawaiiPhysicsSettingsMultiplierSection)

#define LOCTEXT_NAMESPACE "MovieSceneKawaiiPhysicsSettingsMultiplierSection"

UMovieSceneKawaiiPhysicsSettingsMultiplierSection::UMovieSceneKawaiiPhysicsSettingsMultiplierSection(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// SetBlendType は Outer Track の対応 BlendType を見るため、CDO でも確実に設定できるよう直接代入する
	BlendType = EMovieSceneBlendType::Absolute;

	// 外部駆動倍率は区間終了時に Stop するため、KeepState は意味を持たせない
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::RestoreState);
	EvalOptions.bCanEditCompletionMode = false;

	Weight.SetDefault(1.0f);
	Damping.SetDefault(1.0f);
	Stiffness.SetDefault(1.0f);
	WorldDampingLocation.SetDefault(1.0f);
	WorldDampingRotation.SetDefault(1.0f);
	Radius.SetDefault(1.0f);
	LimitAngle.SetDefault(1.0f);

	// ChannelProxy は非 UPROPERTY でメンバ参照を持つだけなので、PostLoad/Duplicate 後も同じメンバアドレスを参照し続けられる
	FMovieSceneChannelProxyData Channels;
#if WITH_EDITOR
	const auto AddChannel = [&Channels](
		FMovieSceneFloatChannel& Channel,
		const FName Name,
		const FText& DisplayText,
		const int32 SortOrder,
		const FText& GroupText)
	{
		FMovieSceneChannelMetaData MetaData;
		MetaData.SetIdentifiers(Name, DisplayText, GroupText);
		MetaData.SortOrder = SortOrder;
		MetaData.bCanCollapseToTrack = false;
		Channels.Add(Channel, MetaData, TMovieSceneExternalValue<float>());
	};

	const FText ScaleGroup = LOCTEXT("ScaleGroup", "Scale");
	AddChannel(Weight, FName(TEXT("Weight")), LOCTEXT("WeightChannel", "Weight"), 0, FText::GetEmpty());
	AddChannel(Damping, FName(TEXT("Damping")), LOCTEXT("DampingChannel", "Damping"), 1, ScaleGroup);
	AddChannel(Stiffness, FName(TEXT("Stiffness")), LOCTEXT("StiffnessChannel", "Stiffness"), 2, ScaleGroup);
	AddChannel(WorldDampingLocation, FName(TEXT("WorldDampingLocation")),
	           LOCTEXT("WorldDampingLocationChannel", "WorldDampingLocation"), 3, ScaleGroup);
	AddChannel(WorldDampingRotation, FName(TEXT("WorldDampingRotation")),
	           LOCTEXT("WorldDampingRotationChannel", "WorldDampingRotation"), 4, ScaleGroup);
	AddChannel(Radius, FName(TEXT("Radius")), LOCTEXT("RadiusChannel", "Radius"), 5, ScaleGroup);
	AddChannel(LimitAngle, FName(TEXT("LimitAngle")), LOCTEXT("LimitAngleChannel", "LimitAngle"), 6, ScaleGroup);
#else
	Channels.Add(Weight);
	Channels.Add(Damping);
	Channels.Add(Stiffness);
	Channels.Add(WorldDampingLocation);
	Channels.Add(WorldDampingRotation);
	Channels.Add(Radius);
	Channels.Add(LimitAngle);
#endif
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
}

float UMovieSceneKawaiiPhysicsSettingsMultiplierSection::EvaluateWeightAtTime(const FFrameTime InTime) const
{
	float WeightValue = 1.0f;
	Weight.Evaluate(InTime, WeightValue);
	return FMath::Clamp(WeightValue, 0.0f, 1.0f) * EvaluateEasing(InTime);
}

FKawaiiPhysicsSettingsMultiplier UMovieSceneKawaiiPhysicsSettingsMultiplierSection::EvaluateScaleAtTime(
	const FFrameTime InTime) const
{
	const FMovieSceneFloatChannel* const Channels[6] = {
		&Damping,
		&Stiffness,
		&WorldDampingLocation,
		&WorldDampingRotation,
		&Radius,
		&LimitAngle
	};
	return KawaiiPhysicsSequencer::EvaluateKawaiiPhysicsScaleChannels(Channels, InTime);
}

void UMovieSceneKawaiiPhysicsSettingsMultiplierSection::BeginDestroy()
{
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(this);
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UMovieSceneKawaiiPhysicsSettingsMultiplierSection::PostEditUndo()
{
	Super::PostEditUndo();

	const UMovieSceneTrack* Track = GetTypedOuter<UMovieSceneTrack>();
	if (!Track || !Track->GetAllSections().Contains(this))
	{
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(this);
	}
}
#endif

#undef LOCTEXT_NAMESPACE
