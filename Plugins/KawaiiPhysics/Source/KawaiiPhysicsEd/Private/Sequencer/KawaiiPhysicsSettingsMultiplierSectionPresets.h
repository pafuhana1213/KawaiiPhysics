// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.h"

/**
 * Kawaii Physics Settings Multiplier セクションへ設定倍率プリセットを適用する
 * Applies a settings multiplier preset to a Kawaii Physics Settings Multiplier section.
 */
inline void ApplyKawaiiPhysicsScalePresetToSection(
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection& Section,
	const FKawaiiPhysicsSettingsMultiplier& Scale)
{
	Section.Damping.Reset();
	Section.Damping.SetDefault(Scale.Damping);
	Section.Stiffness.Reset();
	Section.Stiffness.SetDefault(Scale.Stiffness);
	Section.WorldDampingLocation.Reset();
	Section.WorldDampingLocation.SetDefault(Scale.WorldDampingLocation);
	Section.WorldDampingRotation.Reset();
	Section.WorldDampingRotation.SetDefault(Scale.WorldDampingRotation);
	Section.Radius.Reset();
	Section.Radius.SetDefault(Scale.Radius);
	Section.LimitAngle.Reset();
	Section.LimitAngle.SetDefault(Scale.LimitAngle);
}
