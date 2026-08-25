// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.h"

/**
 * Kawaii Physics Settings Override セクションへ設定倍率プリセットを適用する
 * Applies a settings multiplier preset to a Kawaii Physics Settings Override section.
 */
inline void ApplyKawaiiPhysicsScalePresetToSection(
	UMovieSceneKawaiiPhysicsSettingsOverrideSection& Section,
	const FKawaiiPhysicsSettingsScale& Scale)
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
