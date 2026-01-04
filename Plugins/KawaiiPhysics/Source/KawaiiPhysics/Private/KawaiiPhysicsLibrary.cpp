// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "KawaiiPhysicsLibrary.h"

#include "AnimNode_KawaiiPhysics.h"
#include "BlueprintGameplayTagLibrary.h"
#include "KawaiiPhysicsExternalForce.h"
#include "Animation/AnimClassInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogKawaiiPhysicsLibrary, Verbose, All);

void UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(TArray<FAnimNode_KawaiiPhysics*>& OutNodes,
                                                      UAnimInstance* AnimInstance,
                                                      const FGameplayTagContainer& FilterTags,
                                                      bool bFilterExactMatch)
{
	if (!AnimInstance || !AnimInstance->GetClass())
	{
		return;
	}

	if (const IAnimClassInterface* AnimClassInterface =
		IAnimClassInterface::GetFromClass(AnimInstance->GetClass()))
	{
		const TArray<FStructProperty*>& AnimNodeProperties = AnimClassInterface->GetAnimNodeProperties();
		for (int i = 0; i < AnimNodeProperties.Num(); ++i)
		{
			if (AnimNodeProperties[i]->Struct->IsChildOf(FAnimNode_KawaiiPhysics::StaticStruct()))
			{
				FAnimNode_KawaiiPhysics* Node = AnimNodeProperties[i]->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(AnimInstance);
				if (Node)
				{
					// Check tag filter
					if (FilterTags.IsEmpty() || UBlueprintGameplayTagLibrary::MatchesAnyTags(
						Node->KawaiiPhysicsTag, FilterTags, bFilterExactMatch))
					{
						OutNodes.Add(Node);
					}
				}
			}
		}
	}
}

void UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(TArray<FAnimNode_KawaiiPhysics*>& OutNodes,
                                                      USkeletalMeshComponent* MeshComp,
                                                      const FGameplayTagContainer& FilterTags,
                                                      bool bFilterExactMatch)
{
	if (!MeshComp)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	{
		CollectKawaiiPhysicsNodes(OutNodes, AnimInstance, FilterTags, bFilterExactMatch);
	}

	const TArray<UAnimInstance*>& LinkedInstances =
		const_cast<const USkeletalMeshComponent*>(MeshComp)->GetLinkedAnimInstances();
	for (UAnimInstance* LinkedInstance : LinkedInstances)
	{
		CollectKawaiiPhysicsNodes(OutNodes, LinkedInstance, FilterTags, bFilterExactMatch);
	}

	if (UAnimInstance* PostProcessAnimInstance = MeshComp->GetPostProcessInstance())
	{
		CollectKawaiiPhysicsNodes(OutNodes, PostProcessAnimInstance, FilterTags, bFilterExactMatch);
	}
}

bool UKawaiiPhysicsLibrary::AddExternalForcesToComponent(USkeletalMeshComponent* MeshComp,
                                                         TArray<UKawaiiPhysics_ExternalForce*>& ExternalForces,
                                                         UObject* Owner,
                                                         FGameplayTagContainer& FilterTags,
                                                         bool bFilterExactMatch, bool bIsOneShot)
{
	bool bResult = false;

	TArray<FAnimNode_KawaiiPhysics*> KawaiiPhysicsNodes;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsNodes, MeshComp, FilterTags, bFilterExactMatch);

	for (FAnimNode_KawaiiPhysics* Node : KawaiiPhysicsNodes)
	{
		for (UKawaiiPhysics_ExternalForce* ExternalForce : ExternalForces)
		{
			if (ExternalForce != nullptr)
			{
				ExternalForce->ExternalOwner = Owner;
				ExternalForce->bIsOneShot = bIsOneShot;
				Node->ExternalForces.Add(ExternalForce);
				bResult = true;
			}
		}
	}

	return bResult;
}

bool UKawaiiPhysicsLibrary::RemoveExternalForcesFromComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
                                                              FGameplayTagContainer& FilterTags, bool bFilterExactMatch)
{
	bool bResult = false;

	TArray<FAnimNode_KawaiiPhysics*> KawaiiPhysicsNodes;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsNodes, MeshComp, FilterTags, bFilterExactMatch);

	for (FAnimNode_KawaiiPhysics* Node : KawaiiPhysicsNodes)
	{
		const int32 NumRemoved = Node->ExternalForces.RemoveAll([&](UKawaiiPhysics_ExternalForce* ExternalForcePtr)
		{
			return ExternalForcePtr && ExternalForcePtr->ExternalOwner == Owner;
		});

		if (NumRemoved > 0)
		{
			bResult = true;
		}
	}

	return bResult;
}
