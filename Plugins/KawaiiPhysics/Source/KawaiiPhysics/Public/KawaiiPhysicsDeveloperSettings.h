// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "KawaiiPhysicsDeveloperSettings.generated.h"

/**
 * KawaiiPhysics のプロジェクト全体設定 / Project-wide settings for KawaiiPhysics.
 * Project Settings > Plugins > Kawaii Physics に表示される。
 */
UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "Kawaii Physics"))
class KAWAIIPHYSICS_API UKawaiiPhysicsDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	* シミュレーション全体を固定タイムステップ（FixedDt = 1/TargetFramerate）でサブステップ実行し、
	* 真にフレームレート非依存にする。既定OFF=従来挙動（後方互換）。
	* ONにすると、減衰・重力・Stiffness・コリジョン・拘束のすべてが異なるフレームレート
	* （15/30/60/120fps等）でほぼ一定の挙動になる（低fps時はサブステップ数が増え負荷が上がる）。
	* Run the whole simulation with fixed-timestep substepping (FixedDt = 1/TargetFramerate) for true
	* frame-rate independence. Default OFF keeps the legacy behavior (backward compatible). When ON,
	* damping/gravity/stiffness/collision/constraints all behave consistently across frame rates
	* (CPU cost rises at low fps as substep count grows).
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simulation",
		meta = (DisplayName = "Use Fixed Substepping"))
	bool bUseFixedSubstepping = false;

	/**
	* 1フレームあたりの最大サブステップ数。低fps/ヒッチ時の暴走（spiral of death）を防ぐ上限。
	* これを超える分の時間は破棄される。
	* Maximum substeps per frame. Caps catch-up at low fps / hitches (prevents spiral of death);
	* time beyond this is dropped.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simulation",
		meta = (DisplayName = "Max Substeps", ClampMin = "1", UIMin = "1", ClampMax = "16",
			EditCondition = "bUseFixedSubstepping"))
	int32 MaxSubsteps = 4;

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	//~ End of UDeveloperSettings interface
};
