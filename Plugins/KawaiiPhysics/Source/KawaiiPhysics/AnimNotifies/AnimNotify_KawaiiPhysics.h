// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotify.h"

#if	ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "AnimNotify_KawaiiPhysics.generated.h"

/**
 * UAnimNotify_KawaiiPhysicsAddExternalForce
 *
 * This class represents an animation notify that adds external forces to a skeletal mesh component
 * during an animation sequence. It inherits from UAnimNotify and provides functionality to add and remove
 * external forces when the notify is triggered.
 */
UCLASS(Blueprintable, meta = (DisplayName = "KawaiiPhyiscs: Add ExternalForce"))
class KAWAIIPHYSICS_API UAnimNotify_KawaiiPhysicsAddExternalForce : public UAnimNotify
{
	GENERATED_BODY()

public:
	/**
	 * Constructor for UAnimNotify_KawaiiPhysicsAddExternalForce.
	 *
	 * @param ObjectInitializer - The object initializer for this class.
	 */
	UAnimNotify_KawaiiPhysicsAddExternalForce(const FObjectInitializer& ObjectInitializer);

	/**
	 * Gets the name of the notify.
	 *
	 * @return The name of the notify as a string.
	 */
	virtual FString GetNotifyName_Implementation() const override;

	/**
	 * Called when the animation notify is triggered.
	 *
	 * @param MeshComp - The skeletal mesh component.
	 * @param Animation - The animation sequence.
	 * @param EventReference - The event reference.
	 */
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
