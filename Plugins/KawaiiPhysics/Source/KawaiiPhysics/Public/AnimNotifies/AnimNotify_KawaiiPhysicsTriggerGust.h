// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotify.h"

#include "AnimNotify_KawaiiPhysicsTriggerGust.generated.h"

/**
 * 単発の AnimNotify で ProceduralWind の突風をトリガーする（タグでフィルタ可能）。
 * AnimNotify that triggers ProceduralWind gusts when fired (filterable by tag).
 */
UCLASS(Blueprintable, meta = (DisplayName = "KawaiiPhysics: Trigger Gust"))
class KAWAIIPHYSICS_API UAnimNotify_KawaiiPhysicsTriggerGust : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_KawaiiPhysicsTriggerGust(const FObjectInitializer& ObjectInitializer);

	virtual FString GetNotifyName_Implementation() const override;

	/** トリガー時に ProceduralWind の突風をリクエストする / Requests ProceduralWind gusts when the notify fires. */
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                    const FAnimNotifyEventReference& EventReference) override;

public:
	/** 突風の強さ / Gust strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Wind")
	float Strength = 0.0f;

	/** 立ち上がり時間（秒） / Rise time, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Wind", meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float RiseTime = 0.0f;

	/** ピーク強度を保持する時間（秒）。0で従来の三角形エンベロープ / Hold time at peak strength, in seconds. 0 keeps the legacy triangular envelope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Wind", meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float HoldTime = 0.0f;

	/** 減衰時間（秒） / Decay time, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Wind", meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float DecayTime = 0.0f;

	/** 突風の方向（ワールド空間・非正規化可）。ゼロなら既存 ProceduralWind の風向き等を継承 / Gust direction (world space; may be non-normalized). Zero inherits from an authored ProceduralWind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Wind")
	FVector GustDirection = FVector::ZeroVector;

	/** 適用するノードを Tag でフィルタ（空なら全ノード対象） / Tags used to filter target nodes; empty targets all nodes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	FGameplayTagContainer FilterTags;

	/** Tag の完全一致でフィルタするか / Whether to filter tags by exact match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bFilterExactMatch = false;
};
