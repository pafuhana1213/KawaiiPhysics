#include "AnimNotify_KawaiiPhysics.h"
#include "KawaiiPhysicsLibrary.h"
#include "Misc/UObjectToken.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysics_AnimNotify"

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
	UKawaiiPhysicsLibrary::AddExternalForcesToComponent(MeshComp, AdditionalExternalForces, this,
	                                                    FilterTags, bFilterExactMatch, true);

	Super::Notify(MeshComp, Animation, EventReference);
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
