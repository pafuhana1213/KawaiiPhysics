#include "AnimNotify_KawaiiPhysics.h"

#include "KawaiiPhysicsLibrary.h"
#include "Misc/UObjectToken.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysics_AnimNotify"

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_AN_AddExternalForce_Notify"), STAT_KawaiiPhysics_AN_AddExternalForce_Notify,
                   STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_AN_AddExternalForce_InitAnimNodeReferences"),
                   STAT_KawaiiPhysics_AN_AddExternalForce_InitAnimNodeReferences, STATGROUP_Anim);


UAnimNotify_KawaiiPhysicsAddExternalForce::UAnimNotify_KawaiiPhysicsAddExternalForce(
	const FObjectInitializer& ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 170, 0, 255);
#endif // WITH_EDITORONLY_DATA
}

FString UAnimNotify_KawaiiPhysicsAddExternalForce::GetNotifyName_Implementation() const
{
	return FString(TEXT("KP: Add ExternalForce"));
}

void UAnimNotify_KawaiiPhysicsAddExternalForce::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                                       const FAnimNotifyEventReference& EventReference)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AN_AddExternalForce_Notify);

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences = InitAnimNodeReferences(MeshComp);

	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		for (auto& AdditionalExternalForce : AdditionalExternalForces)
		{
			if (AdditionalExternalForce.IsValid())
			{
				UKawaiiPhysicsLibrary::AddExternalForce(KawaiiPhysicsReference, AdditionalExternalForce, this, true);
			}
		}
	}

	Super::Notify(MeshComp, Animation, EventReference);
}

TArray<FKawaiiPhysicsReference> UAnimNotify_KawaiiPhysicsAddExternalForce::InitAnimNodeReferences(
	const USkeletalMeshComponent* MeshComp)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AN_AddExternalForce_InitAnimNodeReferences);

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
void UAnimNotify_KawaiiPhysicsAddExternalForce::ValidateAssociatedAssets()
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
					          " AnimNotify(KawaiiPhysics_AddExternalForce) doesn't have a valid ExternalForce in {0}"),
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
