// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"

#include "AnimNotify_KawaiiPhysicsSettingsMultiplier.generated.h"

/**
 * Notify 発火時点から Duration 秒間、KawaiiPhysics ノードの物理設定へ倍率（BlendIn→Hold→BlendOut の台形）を適用する。
 * Applies multipliers to KawaiiPhysics node physics settings for Duration seconds from notify fire time (BlendIn -> Hold -> BlendOut trapezoid).
 */
UCLASS(Blueprintable, meta = (DisplayName = "KawaiiPhysics: Settings Multiplier (Pulse)"))
class KAWAIIPHYSICS_API UAnimNotify_KawaiiPhysicsSettingsMultiplier : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_KawaiiPhysicsSettingsMultiplier(const FObjectInitializer& ObjectInitializer);

	/** Notify トラックに表示する名前を返す / Returns the name shown on the notify track. */
	virtual FString GetNotifyName_Implementation() const override;

	/** トリガー時に物理設定倍率を開始する / Starts physics settings multipliers when the notify fires. */
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                    const FAnimNotifyEventReference& EventReference) override;

public:
	/** 物理設定への倍率（全 1.0 で変更なし） / Multipliers for physics settings; all 1.0 means no change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings Multiplier")
	FKawaiiPhysicsSettingsMultiplier SettingsScale;

	/** BlendIn/BlendOut 込みの合計秒。0 以下は何もしない / Total seconds including BlendIn and BlendOut. 0 or less does nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings Multiplier", meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float Duration = 1.0f;

	/** 立ち上がり時間（秒） / Blend-in time, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings Multiplier", meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float BlendInTime = 0.2f;

	/** 減衰時間（秒） / Blend-out time, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings Multiplier", meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float BlendOutTime = 0.5f;

	/** 適用するノードを Tag でフィルタ（空なら全ノード対象） / Tags used to filter target nodes; empty targets all nodes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	FGameplayTagContainer FilterTags;

	/** Tag の完全一致でフィルタするか（false なら親 Tag も許容） / Whether to filter tags by exact match (false allows parent tags). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bFilterExactMatch = false;

#if WITH_EDITOR
	/** 関連アセットの設定を検証する / Validates settings on associated assets. */
	virtual void ValidateAssociatedAssets() override;
#endif
};
