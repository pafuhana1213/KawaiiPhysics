// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNotifies/AnimNotifyState_KawaiiPhysicsSetAlpha.h"
#include "KawaiiPhysicsLibrary.h"
#include "Misc/UObjectToken.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNotifyState_KawaiiPhysicsSetAlpha)

#define LOCTEXT_NAMESPACE "KawaiiPhysics_AnimNotifyState"

UAnimNotifyState_KawaiiPhysicsSetAlpha::UAnimNotifyState_KawaiiPhysicsSetAlpha(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(80, 200, 255, 255);
#endif
}

FString UAnimNotifyState_KawaiiPhysicsSetAlpha::GetNotifyName_Implementation() const
{
	return FString(TEXT("KP: Set Alpha"));
}

void UAnimNotifyState_KawaiiPhysicsSetAlpha::NotifyBegin(USkeletalMeshComponent* MeshComp,
                                                         UAnimSequenceBase* Animation,
                                                         float TotalDuration,
                                                         const FAnimNotifyEventReference& EventReference)
{
	// Save current alpha for restore on end.
	bHasSavedAlpha = UKawaiiPhysicsLibrary::GetAlphaFromComponent(MeshComp, SavedAlpha, FilterTags, bFilterExactMatch);

	const float Alpha = ResolveAlpha(MeshComp, Animation);
	UKawaiiPhysicsLibrary::SetAlphaToComponent(MeshComp, Alpha, FilterTags, bFilterExactMatch);

	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
}

void UAnimNotifyState_KawaiiPhysicsSetAlpha::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                                        float FrameDeltaTime,
                                                        const FAnimNotifyEventReference& EventReference)
{
	const float Alpha = ResolveAlpha(MeshComp, Animation);
	UKawaiiPhysicsLibrary::SetAlphaToComponent(MeshComp, Alpha, FilterTags, bFilterExactMatch);

	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
}

void UAnimNotifyState_KawaiiPhysicsSetAlpha::NotifyEnd(USkeletalMeshComponent* MeshComp,
                                                       UAnimSequenceBase* Animation,
                                                       const FAnimNotifyEventReference& EventReference)
{
	if (bHasSavedAlpha)
	{
		UKawaiiPhysicsLibrary::SetAlphaToComponent(MeshComp, SavedAlpha, FilterTags, bFilterExactMatch);
	}

	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

float UAnimNotifyState_KawaiiPhysicsSetAlpha::ResolveAlpha(USkeletalMeshComponent* MeshComp,
                                                           UAnimSequenceBase* Animation) const
{
	float Alpha = 1.0f;

	switch (Source)
	{
	default:
	case EKawaiiPhysicsSetAlphaSource::Curve:
		{
			Alpha = DefaultAlphaIfNoCurve;
			if (MeshComp && CurveName != NAME_None)
			{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
				MeshComp->GetCurveValue(CurveName, DefaultAlphaIfNoCurve, Alpha);
#else
				MeshComp->GetAnimInstance()->GetCurveValueWithDefault(CurveName, DefaultAlphaIfNoCurve, Alpha);
#endif
			}
			break;
		}
	case EKawaiiPhysicsSetAlphaSource::Constant:
		{
			Alpha = ConstantAlpha;
			break;
		}
	}


	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);


	return Alpha;
}

#if WITH_EDITOR
void UAnimNotifyState_KawaiiPhysicsSetAlpha::ValidateAssociatedAssets()
{
	static const FName NAME_AssetCheck("AssetCheck");

	if (const UAnimSequenceBase* ContainingAsset = Cast<UAnimSequenceBase>(GetContainingAsset()))
	{
		if (Source == EKawaiiPhysicsSetAlphaSource::Curve && CurveName == NAME_None)
		{
			FMessageLog AssetCheckLog(NAME_AssetCheck);

			const FText Message = FText::Format(
				NSLOCTEXT("AnimNotify", "KawaiiPhysicsSetAlpha_CurveNameEmpty",
				          " AnimNotifyState(KawaiiPhysics_SetAlpha) CurveName is empty in {0}"),
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
