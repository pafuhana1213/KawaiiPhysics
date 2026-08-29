// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNotifies/AnimNotify_KawaiiPhysicsSettingsMultiplier.h"

#include "KawaiiPhysicsLibrary.h"
#include "Misc/UObjectToken.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNotify_KawaiiPhysicsSettingsMultiplier)

#define LOCTEXT_NAMESPACE "KawaiiPhysics_AnimNotify"

UAnimNotify_KawaiiPhysicsSettingsMultiplier::UAnimNotify_KawaiiPhysicsSettingsMultiplier(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 120, 200, 255);
#endif
}

FString UAnimNotify_KawaiiPhysicsSettingsMultiplier::GetNotifyName_Implementation() const
{
	return FString(TEXT("KP: Settings Multiplier Pulse"));
}

void UAnimNotify_KawaiiPhysicsSettingsMultiplier::Notify(USkeletalMeshComponent* MeshComp,
                                                       UAnimSequenceBase* Animation,
                                                       const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp)
	{
		return;
	}

	// 一発 Notify に無限 Hold は許さない
	if (Duration <= 0.0f)
	{
		return;
	}

	// コンポーネント内の対象ノードへ時間型の物理設定倍率を開始する
	FKawaiiPhysicsTransientHandle UnusedHandle;
	UKawaiiPhysicsLibrary::StartPhysicsSettingsMultiplierOnComponent(MeshComp, UnusedHandle, SettingsScale, Duration,
	                                                               BlendInTime, BlendOutTime, FilterTags,
	                                                               bFilterExactMatch);

	Super::Notify(MeshComp, Animation, EventReference);
}

#if WITH_EDITOR
void UAnimNotify_KawaiiPhysicsSettingsMultiplier::ValidateAssociatedAssets()
{
	static const FName NAME_AssetCheck("AssetCheck");

	if (const UAnimSequenceBase* ContainingAsset = Cast<UAnimSequenceBase>(GetContainingAsset()))
	{
		if (Duration <= 0.0f)
		{
			FMessageLog AssetCheckLog(NAME_AssetCheck);

			const FText Message = FText::Format(
				NSLOCTEXT("AnimNotify", "KawaiiPhysicsSettingsMultiplier_DurationNotPositive",
				          " AnimNotify(KawaiiPhysics_SettingsMultiplier) Duration is 0 or less in {0}"),
				FText::AsCultureInvariant(ContainingAsset->GetPathName()));

			AssetCheckLog.Warning()
			             ->AddToken(FUObjectToken::Create(ContainingAsset))
			             ->AddToken(FTextToken::Create(Message));

			if (GIsEditor)
			{
				constexpr bool bForce = true;
				AssetCheckLog.Notify(Message, EMessageSeverity::Warning, bForce);
			}
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
