// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsTypes.h"
#include "MovieSceneSection.h"

#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.generated.h"

UCLASS(MinimalAPI)
class UMovieSceneKawaiiPhysicsSettingsMultiplierSection : public UMovieSceneSection
{
	GENERATED_BODY()

public:
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection(const FObjectInitializer& ObjectInitializer);

	/** Dampingへの倍率。1未満で減衰が弱まり、揺れが大きくなる / Multiplier for Damping. Below 1 the damping weakens and the sway grows. */
	UPROPERTY()
	FMovieSceneFloatChannel Damping;

	/** Stiffnessへの倍率。1未満で元の形状への引き戻しが弱まり、揺れが大きくなる / Multiplier for Stiffness. Below 1 the pull back to the pre-physics shape weakens and the sway grows. */
	UPROPERTY()
	FMovieSceneFloatChannel Stiffness;

	/** WorldDampingLocationへの倍率。実際の反映率は (1 - WorldDampingLocation) のため意味が反転し、1未満の倍率ではコンポーネントの移動量がより強く反映されて揺れが大きくなる / Multiplier for WorldDampingLocation. The semantics are inverted because the actual reflection factor is (1 - WorldDampingLocation): a multiplier below 1 reflects more of the component movement and increases the sway. */
	UPROPERTY()
	FMovieSceneFloatChannel WorldDampingLocation;

	/** WorldDampingRotationへの倍率。実際の反映率は (1 - WorldDampingRotation) のため意味が反転し、1未満の倍率ではコンポーネントの回転量がより強く反映されて揺れが大きくなる / Multiplier for WorldDampingRotation. The semantics are inverted because the actual reflection factor is (1 - WorldDampingRotation): a multiplier below 1 reflects more of the component rotation and increases the sway. */
	UPROPERTY()
	FMovieSceneFloatChannel WorldDampingRotation;

	/**
	 * コリジョン半径への倍率。0にするとワールドコリジョンのスイープと押し出しが実質無効になる。
	 * 半径によるダミーボーンの本数はベースの半径から決まるため、1未満ではコリジョンの被覆に隙間が生じうる
	 * Multiplier for the collision radius. At 0 the world sweep and push-out are effectively disabled.
	 * The radius-based dummy bone count comes from the base radius, so below 1 the collision coverage can leave gaps.
	 */
	UPROPERTY()
	FMovieSceneFloatChannel Radius;

	/**
	 * LimitAngleへの倍率。ベースが0（無制限）のボーンは倍率に関わらず無制限のまま。ベースが0より大きいボーンは
	 * 極小値で下限クランプされ、倍率0でも無制限へは反転せず、ほぼ完全にポーズへ追従する
	 * Multiplier for LimitAngle. Bones whose base value is 0 (unlimited) stay unlimited whatever the multiplier is.
	 * Bones with a base above 0 are clamped to a tiny positive minimum, so even a multiplier of 0 never flips them back to
	 * unlimited; they follow the pose almost exactly instead.
	 */
	UPROPERTY()
	FMovieSceneFloatChannel LimitAngle;

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

	/** 指定時刻の物理設定倍率（負値は0にクランプ） / Physics settings multipliers at the time (negative values are clamped to 0) */
	KAWAIIPHYSICSSEQUENCER_API FKawaiiPhysicsSettingsMultiplier EvaluateScaleAtTime(FFrameTime InTime) const;

	/** 破棄時に、このセクション由来の外部駆動を停止する / Stops externally driven overrides created by this section on destruction */
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
};
