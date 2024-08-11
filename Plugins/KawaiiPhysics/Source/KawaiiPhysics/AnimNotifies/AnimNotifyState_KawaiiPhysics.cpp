// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimNotifyState_KawaiiPhysics.h"

#include "KawaiiPhysicsLibrary.h"
#include "Misc/UObjectToken.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeReference.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysics_AnimNotify"

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_AddExternalForce_NotifyBegin"), STAT_KawaiiPhysics_AddExternalForce_NotifyBegin,
                   STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_AddExternalForce_NotifyEnd"), STAT_KawaiiPhysics_AddExternalForce_NotifyEnd,
                   STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_AddExternalForce_InitAnimNodeReferences"),
                   STAT_KawaiiPhysics_AddExternalForce_InitAnimNodeReferences, STATGROUP_Anim);

UAnimNotifyState_KawaiiPhysicsAddExternalForce::UAnimNotifyState_KawaiiPhysicsAddExternalForce(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 170, 0, 255);
#endif // WITH_EDITORONLY_DATA
}

FString UAnimNotifyState_KawaiiPhysicsAddExternalForce::GetNotifyName_Implementation() const
{
	return FString(TEXT("KP: Add ExternalForce"));
}

void UAnimNotifyState_KawaiiPhysicsAddExternalForce::NotifyBegin(USkeletalMeshComponent* MeshComp,
                                                                 UAnimSequenceBase* Animation,
                                                                 float TotalDuration,
                                                                 const FAnimNotifyEventReference& EventReference)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AddExternalForce_NotifyBegin);

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences = InitAnimNodeReferences(MeshComp);

	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		for (auto& AdditionalExternalForce : AdditionalExternalForces)
		{
			if (AdditionalExternalForce.IsValid())
			{
				UKawaiiPhysicsLibrary::AddExternalForce(KawaiiPhysicsReference, AdditionalExternalForce, this);
			}
		}
	}

	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
}

void UAnimNotifyState_KawaiiPhysicsAddExternalForce::NotifyEnd(USkeletalMeshComponent* MeshComp,
                                                               UAnimSequenceBase* Animation,
                                                               const FAnimNotifyEventReference& EventReference)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AddExternalForce_NotifyEnd);

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences = InitAnimNodeReferences(MeshComp);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("RemoveExternalForce"),
			[&](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				InKawaiiPhysics.ExternalForces.RemoveAll([&](FInstancedStruct& InstancedStruct)
				{
					const auto* ExternalForcePtr = InstancedStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>();
					return ExternalForcePtr && ExternalForcePtr->ExternalOwner == this;
				});
			});
	}

	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

TArray<FKawaiiPhysicsReference> UAnimNotifyState_KawaiiPhysicsAddExternalForce::InitAnimNodeReferences(
	const USkeletalMeshComponent* MeshComp)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AddExternalForce_InitAnimNodeReferences);

	// Collect KawaiiPhysics Nodes from ABP
	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;

	if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	{
		UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, AnimInstance, FilterTags,
		                                                 bFilterExactMatch);
	}

	if (UAnimInstance* PostProcessAnimInstance = MeshComp->GetPostProcessInstance())
	{
		UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, PostProcessAnimInstance, FilterTags,
		                                                 bFilterExactMatch);
	}

	return KawaiiPhysicsReferences;
}

#if WITH_EDITOR
void UAnimNotifyState_KawaiiPhysicsAddExternalForce::ValidateAssociatedAssets()
{
	static const FName NAME_AssetCheck("AssetCheck");

	if (UAnimSequenceBase* ContainingAsset = Cast<UAnimSequenceBase>(GetContainingAsset()))
	{
		for (auto& ForceInstancedStruct : AdditionalExternalForces)
		{
			if (!ForceInstancedStruct.IsValid())
			{
				FMessageLog AssetCheckLog(NAME_AssetCheck);

				const FText MessageLooping = FText::Format(
					NSLOCTEXT("AnimNotify", "ExternalForce_ShouldSet",
					          " AnimNotifyState(KawaiiPhysics_AddExternalForce) doesn't have a valid ExternalForce in {0}"),
					FText::AsCultureInvariant(ContainingAsset->GetPathName()));

				AssetCheckLog.Warning()
				             ->AddToken(FUObjectToken::Create(ContainingAsset))
				             ->AddToken(FTextToken::Create(MessageLooping));

				if (GIsEditor)
				{
					constexpr bool bForce = true;
					AssetCheckLog.Notify(MessageLooping, EMessageSeverity::Warning, bForce);
				}
			}

			//const auto& ExternalForce = ForceInstancedStruct.Get<FKawaiiPhysics_ExternalForce>();
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE
