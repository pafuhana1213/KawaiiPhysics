// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "KawaiiPhysicsTypes.h"

/**
 * Kawaii Physics 設定倍率の Sequencer セクション表示用サマリを作成する
 * Creates a Sequencer section display summary for Kawaii Physics settings multipliers.
 */
FText MakeKawaiiPhysicsScaleSummaryText(const FKawaiiPhysicsSettingsMultiplier& Scale);

/**
 * ロケール非依存の略号+数値のみのサマリを作成する（全成分 1.0 なら空文字列）
 * Creates a locale-independent abbreviation+number summary; empty when all components are 1.0.
 */
FString MakeKawaiiPhysicsScaleSummaryString(const FKawaiiPhysicsSettingsMultiplier& Scale);
