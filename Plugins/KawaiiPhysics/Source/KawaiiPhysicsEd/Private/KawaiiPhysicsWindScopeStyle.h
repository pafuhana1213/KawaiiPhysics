// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

// Wind Scope の系列表示スタイルを共有する private ヘッダ。

#include "SKawaiiPhysicsWindScopeWindow.h"

struct FKawaiiWindScopeComponentStyle
{
	EKawaiiPhysicsWindScopeComponent Component;
	FText Label;
	FLinearColor Color;
	float Thickness = 1.0f;
	bool bDashed = false;
};

// 各波形成分の表示スタイル（色・凡例ラベル・線種）を定義する
inline const TArray<FKawaiiWindScopeComponentStyle>& GetWindScopeComponentStyles()
{
	static const TArray<FKawaiiWindScopeComponentStyle> Styles =
	{
		{EKawaiiPhysicsWindScopeComponent::Total, NSLOCTEXT("KawaiiPhysicsWindScopeWindow", "TotalLabel", "Total"), FLinearColor::White, 2.0f, false},
		{EKawaiiPhysicsWindScopeComponent::Steady, NSLOCTEXT("KawaiiPhysicsWindScopeWindow", "SteadyLabel", "Steady"), FLinearColor(1.0f, 0.48f, 0.08f), 1.0f, false},
		{EKawaiiPhysicsWindScopeComponent::Pulse, NSLOCTEXT("KawaiiPhysicsWindScopeWindow", "PulseLabel", "Pulse"), FLinearColor(1.0f, 0.86f, 0.05f), 1.0f, false},
		{EKawaiiPhysicsWindScopeComponent::Wave, NSLOCTEXT("KawaiiPhysicsWindScopeWindow", "WaveLabel", "Wave"), FLinearColor(0.0f, 0.85f, 1.0f), 1.0f, false},
		{EKawaiiPhysicsWindScopeComponent::Breathing, NSLOCTEXT("KawaiiPhysicsWindScopeWindow", "BreathingLabel", "Breathing"), FLinearColor(0.2f, 0.42f, 1.0f), 1.0f, true},
		{EKawaiiPhysicsWindScopeComponent::Random, NSLOCTEXT("KawaiiPhysicsWindScopeWindow", "RandomLabel", "Random"), FLinearColor(1.0f, 0.25f, 0.78f), 1.0f, false},
		{EKawaiiPhysicsWindScopeComponent::Gust, NSLOCTEXT("KawaiiPhysicsWindScopeWindow", "GustLabel", "Gust"), FLinearColor(1.0f, 0.12f, 0.08f), 1.0f, false},
	};
	return Styles;
}
