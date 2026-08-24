// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsTypes.h"
#include "MovieSceneSection.h"

#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.generated.h"

UCLASS(MinimalAPI)
class UMovieSceneKawaiiPhysicsSettingsOverrideSection : public UMovieSceneSection
{
	GENERATED_BODY()

public:
	UMovieSceneKawaiiPhysicsSettingsOverrideSection(const FObjectInitializer& ObjectInitializer);

	/** 物理設定へ適用する倍率（全項目1.0で変更なし） / Multipliers applied to the physics settings (all 1.0 means no change) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics")
	FKawaiiPhysicsSettingsScale Scale;

	/** 倍率の適用率（0..1）。セクションの Ease In/Out と乗算される / Applied ratio (0..1) of the multipliers, multiplied with the section easing */
	UPROPERTY()
	FMovieSceneFloatChannel Weight;

	/** ノードの KawaiiPhysicsTag に対するフィルタ（空なら全ノード対象） / Filter against each node KawaiiPhysicsTag; empty targets all nodes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Filter")
	FGameplayTagContainer FilterTags;

	/** タグを完全一致で比較するか / Whether tags must match exactly */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Filter")
	bool bFilterExactMatch = false;

	/** セクションの評価終了時（範囲外へのスクラブ・停止・削除）に、現在の適用率から 0 へフェードする秒数 / Blend-out seconds from the current applied ratio to 0 when the section stops evaluating (scrubbed out, stopped, or removed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics", meta = (ClampMin = "0.0"))
	float BlendOutTimeOnEnd = 0.2f;

	/** 指定時刻の実効重み = Clamp(Weight,0,1) x EvaluateEasing / Effective weight at the time = Clamp(Weight,0,1) x EvaluateEasing (exposed for tests) */
	KAWAIIPHYSICSSEQUENCER_API float EvaluateWeightAtTime(FFrameTime InTime) const;

	/** 破棄時に、このセクション由来の外部駆動を停止する / Stops externally driven overrides created by this section on destruction */
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
};
