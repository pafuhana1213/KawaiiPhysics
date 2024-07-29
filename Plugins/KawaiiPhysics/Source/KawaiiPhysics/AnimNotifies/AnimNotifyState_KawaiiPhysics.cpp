// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimNotifyState_KawaiiPhysics.h"

#include "KawaiiPhysicsLibrary.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeReference.h"

UAnimNotifyState_KawaiiPhysicsAddExternalForce::UAnimNotifyState_KawaiiPhysicsAddExternalForce(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAnimNotifyState_KawaiiPhysicsAddExternalForce::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                                 float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences = InitAnimNodeReferences(MeshComp);

	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		for (auto& AdditionalExternalForce : AdditionalExternalForces)
		{
			EKawaiiPhysicsAccessExternalForceResult ExecResult;
			UKawaiiPhysicsLibrary::AddExternalForce(ExecResult, KawaiiPhysicsReference, AdditionalExternalForce, this);
		}
	}

	Received_NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
}

void UAnimNotifyState_KawaiiPhysicsAddExternalForce::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                               const FAnimNotifyEventReference& EventReference)
{
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

	Received_NotifyEnd(MeshComp, Animation, EventReference);
}

TArray<FKawaiiPhysicsReference> UAnimNotifyState_KawaiiPhysicsAddExternalForce::InitAnimNodeReferences(
	const USkeletalMeshComponent* MeshComp)
{
	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	auto AddAnimNodeRefs = [&](const UAnimInstance* AnimInstance)
	{
		if (const IAnimClassInterface* AnimClassInterface =
			IAnimClassInterface::GetFromClass((AnimInstance->GetClass())))
		{
			const TArray<FStructProperty*>& AnimNodeProperties = AnimClassInterface->GetAnimNodeProperties();
			for (int i = 0; i < AnimNodeProperties.Num(); ++i)
			{
				if (AnimNodeProperties[i]->Struct->
				                           IsChildOf(FKawaiiPhysicsReference::FInternalNodeType::StaticStruct()))
				{
					EAnimNodeReferenceConversionResult Result;
					FKawaiiPhysicsReference KawaiiPhysicsReference = UKawaiiPhysicsLibrary::ConvertToKawaiiPhysics(
						FAnimNodeReference(MeshComp->GetAnimInstance(), i), Result);

					if (Result == EAnimNodeReferenceConversionResult::Succeeded)
					{
						KawaiiPhysicsReferences.Add(KawaiiPhysicsReference);
					}
				}
			}
		}
	};

	if (const UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	{
		AddAnimNodeRefs(AnimInstance);
	}

	if (const UAnimInstance* PostProcessAnimInstance = MeshComp->GetPostProcessInstance())
	{
		AddAnimNodeRefs(PostProcessAnimInstance);
	}

	return KawaiiPhysicsReferences;
}
