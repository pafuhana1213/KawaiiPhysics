// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "KawaiiPhysicsTypes.h"

namespace KawaiiPhysicsSequencer
{
inline FKawaiiPhysicsSettingsMultiplier EvaluateKawaiiPhysicsScaleChannels(
	const FMovieSceneFloatChannel* const (&Channels)[6],
	const FFrameTime InTime)
{
	const auto EvaluateChannel = [](const FMovieSceneFloatChannel* Channel, const FFrameTime Time)
	{
		float Value = 1.0f;
		if (Channel)
		{
			Channel->Evaluate(Time, Value);
		}
		return FMath::Max(0.0f, Value);
	};

	FKawaiiPhysicsSettingsMultiplier Scale;
	Scale.Damping = EvaluateChannel(Channels[0], InTime);
	Scale.Stiffness = EvaluateChannel(Channels[1], InTime);
	Scale.WorldDampingLocation = EvaluateChannel(Channels[2], InTime);
	Scale.WorldDampingRotation = EvaluateChannel(Channels[3], InTime);
	Scale.Radius = EvaluateChannel(Channels[4], InTime);
	Scale.LimitAngle = EvaluateChannel(Channels[5], InTime);
	return Scale;
}
}
