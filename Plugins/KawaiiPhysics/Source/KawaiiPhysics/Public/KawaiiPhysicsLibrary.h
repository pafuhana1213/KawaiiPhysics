// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsExternalForce.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KawaiiPhysicsLibrary.generated.h"

class UKawaiiPhysics_ExternalForce;

/**
 * Kawaii Physics Blueprint Library (UE4.27 Compatible Version)
 * Note: The FAnimNodeReference-based API is not available in UE4.27.
 * This provides direct component-based functions instead.
 */
UCLASS()
class KAWAIIPHYSICS_API UKawaiiPhysicsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Collect KawaiiPhysics Node References from AnimInstance(ABP)  */
	static void CollectKawaiiPhysicsNodes(TArray<FAnimNode_KawaiiPhysics*>& OutNodes,
										  UAnimInstance* AnimInstance, const FGameplayTagContainer& FilterTags,
										  bool bFilterExactMatch);

	/** Collect KawaiiPhysics Node References from SkeletalMeshComponent  */
	static void CollectKawaiiPhysicsNodes(TArray<FAnimNode_KawaiiPhysics*>& OutNodes,
										  USkeletalMeshComponent* MeshComp, const FGameplayTagContainer& FilterTags,
										  bool bFilterExactMatch);
	
	/** Add ExternalForces to SkeletalMeshComponent */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool AddExternalForcesToComponent(USkeletalMeshComponent* MeshComp,
	                                         UPARAM(ref) TArray<UKawaiiPhysics_ExternalForce*>& ExternalForces, UObject* Owner,
	                                         UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                         bool bFilterExactMatch = false,
	                                         bool bIsOneShot = false);

	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics", meta=(BlueprintThreadSafe))
	static bool RemoveExternalForcesFromComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
	                                              UPARAM(ref) FGameplayTagContainer& FilterTags,
	                                              bool bFilterExactMatch = false);
};
