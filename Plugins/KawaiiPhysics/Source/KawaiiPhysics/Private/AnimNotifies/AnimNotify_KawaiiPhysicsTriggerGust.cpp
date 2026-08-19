// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNotifies/AnimNotify_KawaiiPhysicsTriggerGust.h"

#include "KawaiiPhysicsLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNotify_KawaiiPhysicsTriggerGust)

#define LOCTEXT_NAMESPACE "KawaiiPhysics_AnimNotify"

UAnimNotify_KawaiiPhysicsTriggerGust::UAnimNotify_KawaiiPhysicsTriggerGust(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(120, 210, 255, 255);
#endif
}

FString UAnimNotify_KawaiiPhysicsTriggerGust::GetNotifyName_Implementation() const
{
	return FString(TEXT("KP: Trigger Gust"));
}

void UAnimNotify_KawaiiPhysicsTriggerGust::Notify(USkeletalMeshComponent* MeshComp,
                                                  UAnimSequenceBase* Animation,
                                                  const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp)
	{
		return;
	}

	// タグでフィルタしつつ、Component内の対象ノードへ突風をキューイングする
	UKawaiiPhysicsLibrary::TriggerProceduralWindGustOnComponent(MeshComp, Strength, RiseTime, DecayTime,
	                                                            FilterTags, bFilterExactMatch, GustDirection,
	                                                            HoldTime);

	Super::Notify(MeshComp, Animation, EventReference);
}

#undef LOCTEXT_NAMESPACE
