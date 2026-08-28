// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Misc/EngineVersionComparison.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotify.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "AnimNotify_KawaiiPhysicsAddExternalForce.generated.h"

/**
 * 単発の AnimNotify で KawaiiPhysics に外力を追加する（タグでフィルタ可能）。
 * AnimNotify that adds external forces to KawaiiPhysics nodes when triggered (filterable by tag).
 */
UCLASS(Blueprintable, meta = (DisplayName = "KawaiiPhysics: Add ExternalForce (Pulse)"))
class KAWAIIPHYSICS_API UAnimNotify_KawaiiPhysicsAddExternalForce : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_KawaiiPhysicsAddExternalForce(const FObjectInitializer& ObjectInitializer);

	virtual FString GetNotifyName_Implementation() const override;

	/** トリガー時に外力を追加 / Adds the external forces when the notify fires. */
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                    const FAnimNotifyEventReference& EventReference) override;

public:
	/**
	 * Additional external forces to be applied to the skeletal mesh component.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (BaseStruct = "/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct))
	TArray<FInstancedStruct> AdditionalExternalForces;

	/**
	 * Tags used to filter which external forces are applied.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce")
	FGameplayTagContainer FilterTags;

	/**
	 * Whether to filter tags to exact matches (if False, parent tags will also be included).
	 * Tagのフィルタリングにて完全一致にするか否か（Falseの場合は親Tagも含めます）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce")
	bool bFilterExactMatch;

#if WITH_EDITOR
	/**
	 * Validates the associated assets in the editor.
	 */
	virtual void ValidateAssociatedAssets() override;
#endif
};
