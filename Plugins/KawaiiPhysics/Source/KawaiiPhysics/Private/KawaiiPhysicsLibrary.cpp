// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsLibrary.h"

#include "Misc/EngineVersionComparison.h"

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Animation/AnimInstance.h"
#endif

#include "AnimNode_KawaiiPhysics.h"
#include "BlueprintGameplayTagLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "KawaiiPhysics.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsLibrary)

DEFINE_LOG_CATEGORY_STATIC(LogKawaiiPhysicsLibrary, Verbose, All);

namespace
{
	const TSet<FName>& GetNodeModifyBonesReinitPropertyNames()
	{
		static const TSet<FName> Names = {
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, RootBone),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExcludeBones),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, AdditionalRootBones),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DummyBoneLength),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneSubdivisionCount),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bBoneSubdivisionCollisionOnly),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bBoneSubdivisionDensifyByRadius),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneForwardAxis),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, RadiusCurveData),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsAssetForLimits),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, MirrorDataTableForLimits),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bSkipMirroredBoneWithExistingCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintSubdivisionCount),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraints),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintsDataAsset),
		};
		return Names;
	}

	const TSet<FName>& GetNodeSharedCollisionReinitPropertyNames()
	{
		static const TSet<FName> Names = {
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bSharedCollisionSource),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSharedCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SharedCollisionGroupTag),
		};
		return Names;
	}

	bool IsDeniedRuntimeNodePropertyName(FName PropertyName)
	{
		return PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CustomExternalForces);
	}

	FKawaiiPhysics_ExternalForce_ProceduralWind* GetMutableProceduralWind(FInstancedStruct& InstancedStruct)
	{
		if (!InstancedStruct.IsValid() ||
			InstancedStruct.GetScriptStruct() != FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct())
		{
			return nullptr;
		}

		return InstancedStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	}

	bool QueueProceduralWindGust(FKawaiiPhysics_ExternalForce_ProceduralWind& ProceduralWind,
	                             const float Strength, const float RiseTime, const float DecayTime)
	{
		if (!ProceduralWind.RuntimeState.IsValid())
		{
			ProceduralWind.ResetRuntimeState();
		}

		FScopeLock Lock(&ProceduralWind.RuntimeState->Mutex);
		ProceduralWind.RuntimeState->PendingGust = FKawaiiProceduralWindGustRequest{
			Strength,
			RiseTime,
			DecayTime
		};
		return true;
	}

	bool QueueProceduralWindParams(FKawaiiPhysics_ExternalForce_ProceduralWind& ProceduralWind,
	                               const FKawaiiProceduralWindDynamicParams& Params)
	{
		if (!ProceduralWind.RuntimeState.IsValid())
		{
			ProceduralWind.ResetRuntimeState();
		}

		FScopeLock Lock(&ProceduralWind.RuntimeState->Mutex);
		ProceduralWind.RuntimeState->PendingParams = Params;
		return true;
	}

#if !UE_BUILD_SHIPPING
	void DumpKawaiiPhysicsNodes()
	{
		if (!IsInGameThread())
		{
			UE_LOG(LogKawaiiPhysics, Warning, TEXT("p.KawaiiPhysics.DumpNodes must run on the GameThread."));
			return;
		}

		FGameplayTagContainer EmptyFilterTags;
		int32 DumpedNodeCount = 0;

		for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			USkeletalMeshComponent* MeshComp = *It;
			if (!IsValid(MeshComp) || MeshComp->IsTemplate() || !MeshComp->GetWorld())
			{
				continue;
			}

			TArray<FKawaiiPhysicsReference> Nodes;
			UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, MeshComp, EmptyFilterTags, false);
			if (Nodes.IsEmpty())
			{
				continue;
			}

			const UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
			const AActor* OwnerActor = MeshComp->GetOwner();
			const FName AnimBPName = AnimInstance ? AnimInstance->GetClass()->GetFName() : NAME_None;
			const FName ComponentName = MeshComp->GetFName();
			const FName ActorName = OwnerActor ? OwnerActor->GetFName() : NAME_None;

			for (FKawaiiPhysicsReference& NodeRef : Nodes)
			{
				NodeRef.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
					TEXT("DumpKawaiiPhysicsNode"),
					[&DumpedNodeCount, AnimBPName, ComponentName, ActorName](FAnimNode_KawaiiPhysics& Node)
					{
						++DumpedNodeCount;
						UE_LOG(LogKawaiiPhysics, Log,
						       TEXT("DumpNodes: AnimBP=%s Component=%s Actor=%s Tag=%s RootBone=%s Damping=%.3f Stiffness=%.3f Radius=%.3f Gravity=%s Wind=%s WindScale=%.3f ModifyBones=%d"),
						       *AnimBPName.ToString(),
						       *ComponentName.ToString(),
						       *ActorName.ToString(),
						       *Node.KawaiiPhysicsTag.ToString(),
						       *Node.RootBone.BoneName.ToString(),
						       Node.PhysicsSettings.Damping,
						       Node.PhysicsSettings.Stiffness,
						       Node.PhysicsSettings.Radius,
						       *Node.Gravity.ToCompactString(),
						       Node.bEnableWind ? TEXT("true") : TEXT("false"),
						       Node.WindScale,
						       Node.ModifyBones.Num());
					});
			}
		}

		UE_LOG(LogKawaiiPhysics, Log, TEXT("DumpNodes: Total=%d"), DumpedNodeCount);
	}

	FAutoConsoleCommand CVarKawaiiPhysicsDumpNodes(
		TEXT("p.KawaiiPhysics.DumpNodes"),
		TEXT("Dump runtime KawaiiPhysics nodes in all valid skeletal mesh components."),
		FConsoleCommandDelegate::CreateStatic(&DumpKawaiiPhysicsNodes));
#endif
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ConvertToKawaiiPhysics(const FAnimNodeReference& Node,
                                                                      EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FKawaiiPhysicsReference>(Node, Result);
}

bool UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(TArray<FKawaiiPhysicsReference>& Nodes,
                                                      UAnimInstance* AnimInstance,
                                                      const FGameplayTagContainer& FilterTags, bool bFilterExactMatch)
{
	if (!ensure(AnimInstance && AnimInstance->GetClass()))
	{
		return false;
	}

	bool bResult = false;
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
				FKawaiiPhysicsReference KawaiiPhysicsReference = ConvertToKawaiiPhysics(
					FAnimNodeReference(AnimInstance, i), Result);

				if (Result == EAnimNodeReferenceConversionResult::Succeeded)
				{
					auto& Tag = KawaiiPhysicsReference.GetAnimNode<FAnimNode_KawaiiPhysics>().KawaiiPhysicsTag;
					if (FilterTags.IsEmpty() || UBlueprintGameplayTagLibrary::MatchesAnyTags(
						Tag, FilterTags, bFilterExactMatch))
					{
						Nodes.Add(KawaiiPhysicsReference);
						bResult = true;
					}
				}
			}
		}
	}

	return bResult;
}

bool UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(TArray<FKawaiiPhysicsReference>& Nodes,
                                                      USkeletalMeshComponent* MeshComp,
                                                      const FGameplayTagContainer& FilterTags, bool bFilterExactMatch)
{
	if (!ensure(MeshComp))
	{
		return false;
	}

	const int NodeNum = Nodes.Num();

	if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	{
		CollectKawaiiPhysicsNodes(Nodes, AnimInstance, FilterTags,
		                          bFilterExactMatch);
	}

	const TArray<UAnimInstance*>& LinkedInstances =
		const_cast<const USkeletalMeshComponent*>(MeshComp)->GetLinkedAnimInstances();
	for (UAnimInstance* LinkedInstance : LinkedInstances)
	{
		if (LinkedInstance)
		{
			CollectKawaiiPhysicsNodes(Nodes, LinkedInstance, FilterTags,
			                          bFilterExactMatch);
		}
	}

	if (UAnimInstance* PostProcessAnimInstance = MeshComp->GetPostProcessInstance())
	{
		CollectKawaiiPhysicsNodes(Nodes, PostProcessAnimInstance, FilterTags,
		                          bFilterExactMatch);
	}

	return NodeNum != Nodes.Num();
}

bool UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodesFromAnimInstance(TArray<FKawaiiPhysicsReference>& Nodes,
                                                                      UAnimInstance* AnimInstance,
                                                                      const FGameplayTagContainer& FilterTags,
                                                                      bool bFilterExactMatch)
{
	return CollectKawaiiPhysicsNodes(Nodes, AnimInstance, FilterTags, bFilterExactMatch);
}

bool UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodesFromComponent(TArray<FKawaiiPhysicsReference>& Nodes,
                                                                   USkeletalMeshComponent* MeshComp,
                                                                   const FGameplayTagContainer& FilterTags,
                                                                   bool bFilterExactMatch)
{
	return CollectKawaiiPhysicsNodes(Nodes, MeshComp, FilterTags, bFilterExactMatch);
}

bool UKawaiiPhysicsLibrary::IsNodePropertyAccessible(const FProperty* Property)
{
	if (!Property)
	{
		return false;
	}

	if (Property->GetOwnerStruct() != FAnimNode_KawaiiPhysics::StaticStruct())
	{
		return false;
	}

	if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly))
	{
		return false;
	}

	if (IsDeniedRuntimeNodePropertyName(Property->GetFName()))
	{
		return false;
	}

	const EKawaiiPhysicsPresetPropertyClass PropertyClass =
		UKawaiiPhysicsPresetDataAsset::ClassifyNodeProperty(*Property);
	return PropertyClass != EKawaiiPhysicsPresetPropertyClass::Deny &&
		PropertyClass != EKawaiiPhysicsPresetPropertyClass::Unknown;
}

bool UKawaiiPhysicsLibrary::IsNodePropertyAccessible(FName PropertyName)
{
	return IsNodePropertyAccessible(FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName));
}

bool UKawaiiPhysicsLibrary::DoesNodePropertyRequireModifyBonesReinit(FName PropertyName)
{
	return GetNodeModifyBonesReinitPropertyNames().Contains(PropertyName);
}

bool UKawaiiPhysicsLibrary::DoesNodePropertyRequireSharedCollisionReinit(FName PropertyName)
{
	return GetNodeSharedCollisionReinitPropertyNames().Contains(PropertyName);
}

bool UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(FAnimNode_KawaiiPhysics& Node, FName PropertyName,
                                                         const FProperty* ValueProperty, const void* ValuePtr)
{
	const FProperty* Property = FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
	if (!IsNodePropertyAccessible(Property) || !ValueProperty || !ValuePtr || !ValueProperty->SameType(Property))
	{
		return false;
	}

	if (void* NodeValuePtr = Property->ContainerPtrToValuePtr<void>(&Node))
	{
		Property->CopyCompleteValue(NodeValuePtr, ValuePtr);
		if (DoesNodePropertyRequireModifyBonesReinit(PropertyName))
		{
			Node.RequestModifyBonesReinit();
		}
		if (DoesNodePropertyRequireSharedCollisionReinit(PropertyName))
		{
			Node.RequestSharedCollisionReinit();
		}
		return true;
	}

	return false;
}

bool UKawaiiPhysicsLibrary::GetNodeWildcardPropertyValue(const FAnimNode_KawaiiPhysics& Node, FName PropertyName,
                                                         const FProperty* ValueProperty, void* ValuePtr)
{
	const FProperty* Property = FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
	if (!IsNodePropertyAccessible(Property) || !ValueProperty || !ValuePtr || !ValueProperty->SameType(Property))
	{
		return false;
	}

	if (const void* NodeValuePtr = Property->ContainerPtrToValuePtr<void>(&Node))
	{
		ValueProperty->CopyCompleteValue(ValuePtr, NodeValuePtr);
		return true;
	}

	return false;
}

bool UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(FAnimNode_KawaiiPhysics& Node, FName PropertyName,
                                                           const FString& ValueText)
{
	const FProperty* Property = FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
	if (!IsNodePropertyAccessible(Property))
	{
		return false;
	}

	if (void* NodeValuePtr = Property->ContainerPtrToValuePtr<void>(&Node))
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		if (Property->ImportText(*ValueText, NodeValuePtr, PPF_None, nullptr) != nullptr)
#else
		if (Property->ImportText_Direct(*ValueText, NodeValuePtr, nullptr, PPF_None) != nullptr)
#endif
		{
			if (DoesNodePropertyRequireModifyBonesReinit(PropertyName))
			{
				Node.RequestModifyBonesReinit();
			}
			if (DoesNodePropertyRequireSharedCollisionReinit(PropertyName))
			{
				Node.RequestSharedCollisionReinit();
			}
			return true;
		}
	}

	return false;
}

bool UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(const FAnimNode_KawaiiPhysics& Node, FName PropertyName,
                                                         FString& OutValueText)
{
	const FProperty* Property = FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
	if (!IsNodePropertyAccessible(Property))
	{
		return false;
	}

	if (const void* NodeValuePtr = Property->ContainerPtrToValuePtr<void>(&Node))
	{
		OutValueText.Reset();
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		Property->ExportTextItem(OutValueText, NodeValuePtr, nullptr, nullptr, PPF_None);
#else
		Property->ExportText_Direct(OutValueText, NodeValuePtr, nullptr, nullptr, PPF_None);
#endif
		return true;
	}

	return false;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ApplyPresetDataAsset(EKawaiiPhysicsAccessResult& ExecResult,
                                                                    const FKawaiiPhysicsReference& KawaiiPhysics,
                                                                    UKawaiiPhysicsPresetDataAsset* Preset,
                                                                    FKawaiiPhysicsPresetApplyOptions Options)
{
	ExecResult = EKawaiiPhysicsAccessResult::NotValid;

	if (!Preset)
	{
		return KawaiiPhysics;
	}

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("ApplyPresetDataAsset"),
		[&ExecResult, Preset, Options](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			Preset->ApplyToNode(InKawaiiPhysics, Options, nullptr);
			InKawaiiPhysics.RequestModifyBonesReinit();
			InKawaiiPhysics.RequestSharedCollisionReinit();
			ExecResult = EKawaiiPhysicsAccessResult::Valid;
		});

	return KawaiiPhysics;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ResetDynamics(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("ResetDynamics"),
		[](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.ResetDynamics(ETeleportType::ResetPhysics);
		});

	return KawaiiPhysics;
}


FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                               FName& RootBoneName)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetRootBoneName"),
		[RootBoneName](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.RootBone = FBoneReference(RootBoneName);
			InKawaiiPhysics.RequestModifyBonesReinit();
		});

	return KawaiiPhysics;
}

FName UKawaiiPhysicsLibrary::GetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	FName RootBoneName;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetRootBoneName"),
		[&RootBoneName](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			RootBoneName = InKawaiiPhysics.RootBone.BoneName;
		});

	return RootBoneName;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                                   TArray<FName>& ExcludeBoneNames)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetExcludeBoneNames"),
		[&ExcludeBoneNames](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.ExcludeBones.Empty();
			for (auto& ExcludeBoneName : ExcludeBoneNames)
			{
				InKawaiiPhysics.ExcludeBones.Add(FBoneReference(ExcludeBoneName));
			}
			InKawaiiPhysics.RequestModifyBonesReinit();
		});

	return KawaiiPhysics;
}

TArray<FName> UKawaiiPhysicsLibrary::GetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	TArray<FName> ExcludeBoneNames;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetExcludeBoneNames"),
		[&ExcludeBoneNames](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			for (auto& ExcludeBone : InKawaiiPhysics.ExcludeBones)
			{
				ExcludeBoneNames.Add(ExcludeBone.BoneName);
			}
		});

	return ExcludeBoneNames;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::AddExternalForceWithExecResult(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	FInstancedStruct& ExternalForce, UObject* Owner)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	if (AddExternalForce(KawaiiPhysics, ExternalForce, Owner))
	{
		ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
	}

	return KawaiiPhysics;
}

bool UKawaiiPhysicsLibrary::AddExternalForce(const FKawaiiPhysicsReference& KawaiiPhysics,
                                             FInstancedStruct& ExternalForce, UObject* Owner, bool bIsOneShot)
{
	bool bResult = false;

	if (ExternalForce.IsValid())
	{
		if (auto* ExternalForcePtr = ExternalForce.GetMutablePtr<FKawaiiPhysics_ExternalForce>())
		{
			ExternalForcePtr->ExternalOwner = Owner;
			ExternalForcePtr->bIsOneShot = bIsOneShot;

			KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
				TEXT("AddExternalForce"),
				[&](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
				{
					InKawaiiPhysics.ExternalForces.Add(ExternalForce);
				});

			bResult = true;
		}
	}

	return bResult;
}

bool UKawaiiPhysicsLibrary::AddExternalForcesToComponent(USkeletalMeshComponent* MeshComp,
                                                         TArray<FInstancedStruct>& ExternalForces,
                                                         UObject* Owner,
                                                         FGameplayTagContainer& FilterTags,
                                                         bool bFilterExactMatch, bool bIsOneShot)
{
	bool bResult = false;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		for (auto& AExternalForce : ExternalForces)
		{
			if (AExternalForce.IsValid())
			{
				if (AddExternalForce(KawaiiPhysicsReference, AExternalForce, Owner, bIsOneShot))
				{
					bResult = true;
				}
			}
		}
	}

	return bResult;
}

bool UKawaiiPhysicsLibrary::RemoveExternalForcesFromComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
                                                              FGameplayTagContainer& FilterTags, bool bFilterExactMatch)
{
	bool bResult = false;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("RemoveExternalForce"),
			[&](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				const int32 NumRemoved = InKawaiiPhysics.ExternalForces.RemoveAll([&](FInstancedStruct& InstancedStruct)
				{
					const auto* ExternalForcePtr = InstancedStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>();
					return ExternalForcePtr && ExternalForcePtr->ExternalOwner == Owner;
				});

				if (NumRemoved > 0)
				{
					bResult = true;
				}
			});
	}

	return bResult;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::TriggerProceduralWindGust(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	const int32 ExternalForceIndex,
	const float Strength,
	const float RiseTime,
	const float DecayTime)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("TriggerProceduralWindGust"),
		[&ExecResult, ExternalForceIndex, Strength, RiseTime, DecayTime](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (!InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex))
			{
				return;
			}

			if (FKawaiiPhysics_ExternalForce_ProceduralWind* ProceduralWind =
				GetMutableProceduralWind(InKawaiiPhysics.ExternalForces[ExternalForceIndex]))
			{
				if (QueueProceduralWindGust(*ProceduralWind, Strength, RiseTime, DecayTime))
				{
					ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
				}
			}
		});

	return KawaiiPhysics;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetProceduralWindParameters(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	const int32 ExternalForceIndex,
	const FKawaiiProceduralWindDynamicParams& Params)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetProceduralWindParameters"),
		[&ExecResult, ExternalForceIndex, &Params](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (!InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex))
			{
				return;
			}

			if (FKawaiiPhysics_ExternalForce_ProceduralWind* ProceduralWind =
				GetMutableProceduralWind(InKawaiiPhysics.ExternalForces[ExternalForceIndex]))
			{
				if (QueueProceduralWindParams(*ProceduralWind, Params))
				{
					ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
				}
			}
		});

	return KawaiiPhysics;
}

int32 UKawaiiPhysicsLibrary::TriggerProceduralWindGustOnComponent(
	USkeletalMeshComponent* MeshComp,
	const float Strength,
	const float RiseTime,
	const float DecayTime,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch)
{
	int32 AppliedForceCount = 0;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("TriggerProceduralWindGustOnComponent"),
			[&AppliedForceCount, Strength, RiseTime, DecayTime](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				for (FInstancedStruct& InstancedStruct : InKawaiiPhysics.ExternalForces)
				{
					if (FKawaiiPhysics_ExternalForce_ProceduralWind* ProceduralWind =
						GetMutableProceduralWind(InstancedStruct))
					{
						if (QueueProceduralWindGust(*ProceduralWind, Strength, RiseTime, DecayTime))
						{
							++AppliedForceCount;
						}
					}
				}
			});
	}

	return AppliedForceCount;
}

int32 UKawaiiPhysicsLibrary::SetProceduralWindParametersOnComponent(
	USkeletalMeshComponent* MeshComp,
	const FKawaiiProceduralWindDynamicParams& Params,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch)
{
	int32 AppliedForceCount = 0;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetProceduralWindParametersOnComponent"),
			[&AppliedForceCount, &Params](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				for (FInstancedStruct& InstancedStruct : InKawaiiPhysics.ExternalForces)
				{
					if (FKawaiiPhysics_ExternalForce_ProceduralWind* ProceduralWind =
						GetMutableProceduralWind(InstancedStruct))
					{
						if (QueueProceduralWindParams(*ProceduralWind, Params))
						{
							++AppliedForceCount;
						}
					}
				}
			});
	}

	return AppliedForceCount;
}

bool UKawaiiPhysicsLibrary::SetAlphaToComponent(USkeletalMeshComponent* MeshComp, float Alpha,
                                                FGameplayTagContainer& FilterTags, bool bFilterExactMatch)
{
	bool bResult = false;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("SetAlpha"),
			[&](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				InKawaiiPhysics.Alpha = Alpha;
				bResult = true;
			});
	}

	return bResult;
}

bool UKawaiiPhysicsLibrary::GetAlphaFromComponent(USkeletalMeshComponent* MeshComp, float& OutAlpha,
                                                  FGameplayTagContainer& FilterTags, bool bFilterExactMatch)
{
	bool bResult = false;
	OutAlpha = 0.0f;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetAlpha"),
			[&](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				OutAlpha = InKawaiiPhysics.Alpha;
				bResult = true;
			});
		if (bResult)
		{
			break;
		}
	}

	return bResult;
}

DEFINE_FUNCTION(UKawaiiPhysicsLibrary::execSetNodeWildcardProperty)
{
	P_GET_ENUM_REF(EKawaiiPhysicsAccessResult, ExecResult);
	P_GET_STRUCT_REF(FKawaiiPhysicsReference, KawaiiPhysics);
	P_GET_STRUCT_REF(FName, PropertyName);

	ExecResult = EKawaiiPhysicsAccessResult::NotValid;

	// ワイルドカードの Value 入力を読み取る
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FProperty* ValueProp = CastField<FProperty>(Stack.MostRecentProperty);
	const void* ValuePtr = Stack.MostRecentPropertyAddress;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetNodeWildcardProperty"),
		[&ExecResult, &PropertyName, &ValuePtr, &ValueProp](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (SetNodeWildcardPropertyValue(InKawaiiPhysics, PropertyName, ValueProp, ValuePtr))
			{
				ExecResult = EKawaiiPhysicsAccessResult::Valid;
			}
		});

	P_FINISH;
}

DEFINE_FUNCTION(UKawaiiPhysicsLibrary::execGetNodeWildcardProperty)
{
	P_GET_ENUM_REF(EKawaiiPhysicsAccessResult, ExecResult);
	P_GET_STRUCT_REF(FKawaiiPhysicsReference, KawaiiPhysics);
	P_GET_STRUCT_REF(FName, PropertyName);

	ExecResult = EKawaiiPhysicsAccessResult::NotValid;

	// ワイルドカードの Value 出力を読み取る
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FProperty* ValueProp = CastField<FProperty>(Stack.MostRecentProperty);
	void* ValuePtr = Stack.MostRecentPropertyAddress;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetNodeWildcardProperty"),
		[&ExecResult, &PropertyName, &ValuePtr, &ValueProp](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (GetNodeWildcardPropertyValue(InKawaiiPhysics, PropertyName, ValueProp, ValuePtr))
			{
				ExecResult = EKawaiiPhysicsAccessResult::Valid;
			}
		});

	P_FINISH;
}

DEFINE_FUNCTION(UKawaiiPhysicsLibrary::execSetExternalForceWildcardProperty)
{
	P_GET_ENUM_REF(EKawaiiPhysicsAccessExternalForceResult, ExecResult);
	P_GET_STRUCT_REF(FKawaiiPhysicsReference, KawaiiPhysics);
	P_GET_PROPERTY(FIntProperty, ExternalForceIndex);
	P_GET_STRUCT_REF(FName, PropertyName);

	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	// ワイルドカードの Value 入力を読み取る
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FProperty* ValueProp = CastField<FProperty>(Stack.MostRecentProperty);
	void* ValuePtr = Stack.MostRecentPropertyAddress;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetExternalForceWildcardProperty"),
		[&ExecResult, &ExternalForceIndex, &PropertyName, &ValuePtr, &ValueProp](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				// ExternalForcesは型なしTArray<FInstancedStruct>のため、check版GetMutable<>ではなくnull返しのGetMutablePtr<>で型ガードする
				if (FKawaiiPhysics_ExternalForce* Force =
					InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutablePtr<FKawaiiPhysics_ExternalForce>())
				{
					if (const FProperty* Property = FindFProperty<FProperty>(ScriptStruct, PropertyName))
					{
						// BPワイルドカード入力ピン型と外力側プロパティ型が一致する時のみコピー（型不一致のメモリ破壊を防ぐ）
						if (ValuePtr && ValueProp && ValueProp->SameType(Property))
						{
							if (void* ForceValuePtr = Property->ContainerPtrToValuePtr<uint8>(Force))
							{
								Property->CopyCompleteValue(ForceValuePtr, ValuePtr);
								ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
							}
						}
					}
				}
			}
		});

	P_FINISH;
}

DEFINE_FUNCTION(UKawaiiPhysicsLibrary::execGetExternalForceWildcardProperty)
{
	P_GET_ENUM_REF(EKawaiiPhysicsAccessExternalForceResult, ExecResult);
	P_GET_STRUCT_REF(FKawaiiPhysicsReference, KawaiiPhysics);
	P_GET_PROPERTY(FIntProperty, ExternalForceIndex);
	P_GET_STRUCT_REF(FName, PropertyName);

	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	// ワイルドカードの Value 入力を読み取る
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	const FProperty* ValueProp = CastField<FProperty>(Stack.MostRecentProperty);
	void* ValuePtr = Stack.MostRecentPropertyAddress;

	void* Result = nullptr;
	const FProperty* ResultProperty = nullptr;
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetExternalForceWildcardProperty"),
		[&Result, &ResultProperty, &ExternalForceIndex, &PropertyName](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				// 型なしFInstancedStructのためGetMutablePtr<>で型ガード
				if (FKawaiiPhysics_ExternalForce* Force =
					InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutablePtr<FKawaiiPhysics_ExternalForce>())
				{
					if (const FProperty* Property = FindFProperty<FProperty>(ScriptStruct, PropertyName))
					{
						Result = Property->ContainerPtrToValuePtr<void>(Force);
						ResultProperty = Property;
					}
				}
			}
		});

	P_FINISH;

	// 出力ピン型と外力側プロパティ型が一致する時のみコピーし、成功扱いにする
	// （型不一致のメモリ破壊を防ぎ、かつ未コピーのまま成功報告しないようsetterと挙動を揃える）
	if (ValueProp && ValuePtr && Result && ResultProperty && ValueProp->SameType(ResultProperty))
	{
		P_NATIVE_BEGIN;
			ValueProp->CopyCompleteValue(ValuePtr, Result);
		P_NATIVE_END;
		ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
	}
}
