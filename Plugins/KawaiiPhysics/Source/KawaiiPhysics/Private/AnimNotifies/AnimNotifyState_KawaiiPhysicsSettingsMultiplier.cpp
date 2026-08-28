// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNotifies/AnimNotifyState_KawaiiPhysicsSettingsMultiplier.h"

#include "KawaiiPhysicsLibrary.h"
#include "Misc/UObjectToken.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNotifyState_KawaiiPhysicsSettingsMultiplier)

#define LOCTEXT_NAMESPACE "KawaiiPhysics_AnimNotifyState"

UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::UAnimNotifyState_KawaiiPhysicsSettingsMultiplier(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 140, 220, 255);
#endif
}

FString UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::GetNotifyName_Implementation() const
{
	return FString(TEXT("KP: Settings Multiplier"));
}

void UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::NotifyBegin(USkeletalMeshComponent* MeshComp,
                                                                 UAnimSequenceBase* Animation,
                                                                 float TotalDuration,
                                                                 const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp)
	{
		Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
		return;
	}

	CleanupStaleStates();

	const FActiveStateKey Key = MakeStateKey(MeshComp, EventReference);
	if (FActiveState* ExistingState = ActiveStates.Find(Key))
	{
		// 同一キーの重なりは既存ハンドルを共有し、全 End が来るまで保持する
		++ExistingState->ActiveCount;
		ExistingState->LastTouchedFrame = GFrameCounter;
		Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
		return;
	}

	FActiveState State;
	State.Handle = UKawaiiPhysicsLibrary::GenerateTransientHandle();
	State.ElapsedTime = 0.0f;
	State.TotalDuration = FMath::Max(TotalDuration, 0.0f);
	State.Envelope = KawaiiPhysics::ResolveWindGustEnvelope(State.TotalDuration, BlendInTime, BlendOutTime);
	State.LastTouchedFrame = GFrameCounter;

	const float Weight = ResolveWeight(MeshComp, State);
	UKawaiiPhysicsLibrary::PushPhysicsSettingsMultiplierOnComponent(MeshComp, State.Handle, SettingsScale, Weight,
	                                                             FilterTags, bFilterExactMatch, LeaseEvaluations,
	                                                             BlendOutTime);

	ActiveStates.Add(Key, State);

	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
}

void UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::NotifyTick(USkeletalMeshComponent* MeshComp,
                                                                UAnimSequenceBase* Animation,
                                                                float FrameDeltaTime,
                                                                const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp)
	{
		Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
		return;
	}

	FActiveState* State = ActiveStates.Find(MakeStateKey(MeshComp, EventReference));
	if (!State)
	{
		Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
		return;
	}

	State->ElapsedTime = FMath::Clamp(State->ElapsedTime + FMath::Max(FrameDeltaTime, 0.0f), 0.0f,
	                                 State->TotalDuration);
	State->LastTouchedFrame = GFrameCounter;

	const float Weight = ResolveWeight(MeshComp, *State);
	UKawaiiPhysicsLibrary::PushPhysicsSettingsMultiplierOnComponent(MeshComp, State->Handle, SettingsScale, Weight,
	                                                             FilterTags, bFilterExactMatch, LeaseEvaluations,
	                                                             BlendOutTime);

	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
}

void UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::NotifyEnd(USkeletalMeshComponent* MeshComp,
                                                               UAnimSequenceBase* Animation,
                                                               const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp)
	{
		Super::NotifyEnd(MeshComp, Animation, EventReference);
		return;
	}

	const FActiveStateKey Key = MakeStateKey(MeshComp, EventReference);
	if (FActiveState* State = ActiveStates.Find(Key))
	{
		--State->ActiveCount;
		if (State->ActiveCount <= 0)
		{
			UKawaiiPhysicsLibrary::StopPhysicsSettingsMultiplierOnComponent(MeshComp, State->Handle, FilterTags,
			                                                               bFilterExactMatch, BlendOutTime);
			ActiveStates.Remove(Key);
		}
		else
		{
			State->LastTouchedFrame = GFrameCounter;
		}
	}

	CleanupStaleStates();

	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

bool UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::FActiveStateKey::operator==(
	const FActiveStateKey& Other) const
{
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	return Component == Other.Component && NotifyInstanceID == Other.NotifyInstanceID;
#else
	return Component == Other.Component;
#endif
}

UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::FActiveStateKey
UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::MakeStateKey(
	USkeletalMeshComponent* MeshComp,
	const FAnimNotifyEventReference& EventReference)
{
	FActiveStateKey Key;
	Key.Component = MeshComp;
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	Key.NotifyInstanceID = EventReference.GetNotifyInstanceID();
#else
	(void)EventReference;
#endif
	return Key;
}

void UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::CleanupStaleStates()
{
	for (auto It = ActiveStates.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* Component = It.Key().Component.Get();
		const bool bComponentInvalid = !IsValid(Component);
		const bool bFrameCounterValid = GFrameCounter >= It.Value().LastTouchedFrame;
		const bool bStaleByFrame = bFrameCounterValid &&
			GFrameCounter - It.Value().LastTouchedFrame > StaleStateFrameThreshold;

		if (bStaleByFrame && !bComponentInvalid)
		{
			UKawaiiPhysicsLibrary::StopPhysicsSettingsMultiplierOnComponent(Component, It.Value().Handle, FilterTags,
			                                                               bFilterExactMatch, BlendOutTime);
		}

		if (bComponentInvalid || bStaleByFrame)
		{
			It.RemoveCurrent();
		}
	}
}

float UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::ResolveWeight(USkeletalMeshComponent* MeshComp,
                                                                    const FActiveState& State) const
{
	float Weight = 1.0f;

	switch (WeightSource)
	{
	default:
	case EKawaiiPhysicsSettingsMultiplierWeightSource::Envelope:
		{
			if (State.Envelope.RiseTime <= 0.0f && State.Envelope.HoldTime <= 0.0f &&
				State.Envelope.DecayTime <= 0.0f)
			{
				Weight = 1.0f;
			}
			else
			{
				Weight = KawaiiPhysics::EvaluateEnvelopeAlpha01(State.Envelope.RiseTime, State.Envelope.HoldTime,
				                                                State.Envelope.DecayTime, State.ElapsedTime);
			}
			break;
		}
	case EKawaiiPhysicsSettingsMultiplierWeightSource::Curve:
		{
			Weight = DefaultWeightIfNoCurve;
			if (MeshComp && CurveName != NAME_None)
			{
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
				MeshComp->GetCurveValue(CurveName, DefaultWeightIfNoCurve, Weight);
#else
				if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
				{
					AnimInst->GetCurveValueWithDefault(CurveName, DefaultWeightIfNoCurve, Weight);
				}
#endif
			}
			break;
		}
	}

	return FMath::Clamp(Weight, 0.0f, 1.0f);
}

#if WITH_EDITOR
void UAnimNotifyState_KawaiiPhysicsSettingsMultiplier::ValidateAssociatedAssets()
{
	static const FName NAME_AssetCheck("AssetCheck");

	if (const UAnimSequenceBase* ContainingAsset = Cast<UAnimSequenceBase>(GetContainingAsset()))
	{
		if (WeightSource == EKawaiiPhysicsSettingsMultiplierWeightSource::Curve && CurveName == NAME_None)
		{
			FMessageLog AssetCheckLog(NAME_AssetCheck);

			const FText Message = FText::Format(
				NSLOCTEXT("AnimNotify", "KawaiiPhysicsSettingsMultiplier_CurveNameEmpty",
				          " AnimNotifyState(KawaiiPhysics_SettingsMultiplier) CurveName is empty in {0}"),
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
