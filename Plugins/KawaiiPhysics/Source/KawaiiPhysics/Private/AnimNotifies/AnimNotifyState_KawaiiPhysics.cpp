// Copyright 2019-2025 pafuhana1213. All Rights Reserved.


#include "AnimNotifies/AnimNotifyState_KawaiiPhysics.h"
#include "KawaiiPhysicsLibrary.h"
#include "Misc/UObjectToken.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNotifyState_KawaiiPhysics)

#define LOCTEXT_NAMESPACE "KawaiiPhysics_AnimNotifyState"

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
	UKawaiiPhysicsLibrary::AddExternalForcesToComponent(MeshComp, AdditionalExternalForces, this,
	                                                    FilterTags, bFilterExactMatch);
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
}

void UAnimNotifyState_KawaiiPhysicsAddExternalForce::NotifyEnd(USkeletalMeshComponent* MeshComp,
                                                               UAnimSequenceBase* Animation,
                                                               const FAnimNotifyEventReference& EventReference)
{
	UKawaiiPhysicsLibrary::RemoveExternalForcesFromComponent(MeshComp, this, FilterTags, bFilterExactMatch);

	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

#if WITH_EDITOR
void UAnimNotifyState_KawaiiPhysicsAddExternalForce::ValidateAssociatedAssets()
{
	static const FName NAME_AssetCheck("AssetCheck");

	if (const UAnimSequenceBase* ContainingAsset = Cast<UAnimSequenceBase>(GetContainingAsset()))
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
