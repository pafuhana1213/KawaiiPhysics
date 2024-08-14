// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "InstancedStruct.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_KawaiiPhysics.generated.h"

/**
 * UAnimNotifyState_KawaiiPhysicsAddExternalForce
 * 
 * This class represents an animation notify state that adds external forces to a skeletal mesh component
 * during an animation sequence. It inherits from UAnimNotifyState and provides functionality to add and remove
 * external forces at the beginning and end of the animation notify state.
 */
UCLASS(Blueprintable, meta = (DisplayName = "KawaiiPhyiscs: Add ExternalForce"))
class KAWAIIPHYSICS_API UAnimNotifyState_KawaiiPhysicsAddExternalForce : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/**
	 * Constructor for UAnimNotifyState_KawaiiPhysicsAddExternalForce.
	 * 
	 * @param ObjectInitializer - The object initializer for this class.
	 */
	UAnimNotifyState_KawaiiPhysicsAddExternalForce(const FObjectInitializer& ObjectInitializer);

	/**
	 * Gets the name of the notify state.
	 * 
	 * @return The name of the notify state as a string.
	 */
	virtual FString GetNotifyName_Implementation() const override;

	/**
	 * Called when the animation notify state begins.
	 * 
	 * @param MeshComp - The skeletal mesh component.
	 * @param Animation - The animation sequence.
	 * @param TotalDuration - The total duration of the notify state.
	 * @param EventReference - The event reference.
	 */
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	                         const FAnimNotifyEventReference& EventReference) override;

	/**
	 * Called when the animation notify state ends.
	 * 
	 * @param MeshComp - The skeletal mesh component.
	 * @param Animation - The animation sequence.
	 * @param EventReference - The event reference.
	 */
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                       const FAnimNotifyEventReference& EventReference) override;

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
