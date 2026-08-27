// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Evaluation/MovieSceneAnimTypeID.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsTypes.h"

#include "MovieSceneKawaiiPhysicsSettingsMultiplierTemplate.generated.h"

class UMovieSceneKawaiiPhysicsSettingsMultiplierSection;

USTRUCT()
struct FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()

	FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate() = default;
	explicit FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate(
		const UMovieSceneKawaiiPhysicsSettingsMultiplierSection& Section);

	UPROPERTY()
	FMovieSceneFloatChannel Damping;

	UPROPERTY()
	FMovieSceneFloatChannel Stiffness;

	UPROPERTY()
	FMovieSceneFloatChannel WorldDampingLocation;

	UPROPERTY()
	FMovieSceneFloatChannel WorldDampingRotation;

	UPROPERTY()
	FMovieSceneFloatChannel Radius;

	UPROPERTY()
	FMovieSceneFloatChannel LimitAngle;

	UPROPERTY()
	FMovieSceneFloatChannel Weight;

	UPROPERTY()
	FGameplayTagContainer FilterTags;

	UPROPERTY()
	bool bFilterExactMatch = false;

	UPROPERTY()
	float BlendOutTimeOnEnd = 0.2f;

	UPROPERTY()
	bool bIsRootTrack = false;

private:
	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void SetupOverrides() override { EnableOverrides(RequiresTearDownFlag); }
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context,
	                      const FPersistentEvaluationData& PersistentData,
	                      FMovieSceneExecutionTokens& ExecutionTokens) const override;
	virtual void TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;

	// PreAnimated storage は (Object, AnimTypeID) キーなので、同一 Component に複数セクションが載っても別々に復元されるよう
	// セクション（テンプレート実体）ごとに一意な ID を持つ。非 UPROPERTY で再コンパイル時に再生成されるが、
	// Entry の寿命は評価インスタンス内に閉じるため問題ない
	FMovieSceneAnimTypeID AnimTypeID = FMovieSceneAnimTypeID::Unique();
};
