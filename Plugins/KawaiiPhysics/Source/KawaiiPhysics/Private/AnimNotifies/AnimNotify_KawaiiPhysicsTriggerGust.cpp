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

	// Notify は Gust API の Trigger 型オーサリングラッパ。Hold は旧 Trigger 経路と同じく 0 クランプし、Duration = Rise + Hold + Decay で渡し、wind 時間で進行させる
	FKawaiiPhysicsTransientHandle DiscardedHandle;
	UKawaiiPhysicsLibrary::StartProceduralWindGustOnComponent(MeshComp, DiscardedHandle, Strength,
	                                                          RiseTime + FMath::Max(HoldTime, 0.0f) + DecayTime, RiseTime,
	                                                          DecayTime, FilterTags, bFilterExactMatch,
	                                                          GustDirection, /*bRealTimeEnvelope=*/false);

	Super::Notify(MeshComp, Animation, EventReference);
}

#undef LOCTEXT_NAMESPACE
