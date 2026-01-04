// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#include "AnimNotifyState_KawaiiPhysics.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsExternalForce.h"
#include "Misc/UObjectToken.h"

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
                                                                 float TotalDuration)
{
	UKawaiiPhysicsLibrary::AddExternalForcesToComponent(MeshComp, AdditionalExternalForces, this,
	                                                    FilterTags, bFilterExactMatch);
	Super::NotifyBegin(MeshComp, Animation, TotalDuration);
}

void UAnimNotifyState_KawaiiPhysicsAddExternalForce::NotifyEnd(USkeletalMeshComponent* MeshComp,
                                                               UAnimSequenceBase* Animation)
{
	UKawaiiPhysicsLibrary::RemoveExternalForcesFromComponent(MeshComp, this, FilterTags, bFilterExactMatch);

	Super::NotifyEnd(MeshComp, Animation);
}

#undef LOCTEXT_NAMESPACE
