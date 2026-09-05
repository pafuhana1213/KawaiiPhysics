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
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsWindPresetDataAsset.h"
#include "UObject/ScriptInterface.h"
#include "UObject/UObjectIterator.h"
#include "AnimNode_KawaiiPhysicsInternal.h"

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

	// シンプルワールドコリジョンの収集設定（Gather Interval / Object Types / Convex Shape /
	// Skeletal Mesh Collision / Ground Collision / Gather Radius系）はUpdateSimpleWorldCollisionLimits内の
	// Desc差分検知で毎フレーム自動追従するため対象外。
	// World CollisionのbOverrideCollisionParams / CollisionChannelSettings（のObjectType）も
	// 同じDesc差分検知でコリジョンチャンネルへ追従するため対象外。
	// Source / SharedTag はキー種別が変わるため Desc 差分では追従できず、Entry の取り直しが必要。
	// 有効/無効の切り替えのみEntryの取得・解放が必要なため対象に含める。
	const TSet<FName>& GetNodeSimpleWorldCollisionReinitPropertyNames()
	{
		static const TSet<FName> Names = {
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSimpleWorldCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSource),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSharedTag),
		};
		return Names;
	}

	bool IsDeniedRuntimeNodePropertyName(FName PropertyName)
	{
		return PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CustomExternalForces);
	}

	bool PropertyValueHasLiveObjectReference(const FProperty* Property, const void* ValuePtr);

	bool StructMemoryHasLiveObjectReference(const UScriptStruct* Struct, const void* StructMemory)
	{
		if (!Struct || !StructMemory)
		{
			return false;
		}

		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (!Property)
			{
				continue;
			}

			for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
			{
				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(StructMemory, ArrayIndex);
				if (PropertyValueHasLiveObjectReference(Property, ValuePtr))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool PropertyValueHasLiveObjectReference(const FProperty* Property, const void* ValuePtr)
	{
		if (!Property || !ValuePtr)
		{
			return false;
		}

		if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			return ObjectProperty->GetObjectPropertyValue(ValuePtr) != nullptr;
		}

		if (CastField<FSoftObjectProperty>(Property) || CastField<FWeakObjectProperty>(Property) ||
			CastField<FLazyObjectProperty>(Property))
		{
			// Soft/Weak/Lazy は UObject を生存させないため live 参照として扱わない。
			return false;
		}

		if (const FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(Property))
		{
			// TScriptInterface は UObject ポインタを保持するため、transient ストアでは GC 追跡外になる。
			const FScriptInterface* Interface = static_cast<const FScriptInterface*>(ValuePtr);
			return Interface && Interface->GetObject() != nullptr;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				// FInstancedStruct のペイロードは反射フィールドに現れないため中身へ再帰する
				const FInstancedStruct* Nested = static_cast<const FInstancedStruct*>(ValuePtr);
				return Nested && Nested->IsValid() &&
					StructMemoryHasLiveObjectReference(Nested->GetScriptStruct(), Nested->GetMemory());
			}

			return StructMemoryHasLiveObjectReference(StructProperty->Struct, ValuePtr);
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
			for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
			{
				if (PropertyValueHasLiveObjectReference(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index)))
				{
					return true;
				}
			}
			return false;
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper SetHelper(SetProperty, ValuePtr);
			for (int32 Index = 0; Index < SetHelper.GetMaxIndex(); ++Index)
			{
				if (!SetHelper.IsValidIndex(Index))
				{
					continue;
				}

				if (PropertyValueHasLiveObjectReference(SetProperty->ElementProp, SetHelper.GetElementPtr(Index)))
				{
					return true;
				}
			}
			return false;
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper MapHelper(MapProperty, ValuePtr);
			for (int32 Index = 0; Index < MapHelper.GetMaxIndex(); ++Index)
			{
				if (!MapHelper.IsValidIndex(Index))
				{
					continue;
				}

				if (PropertyValueHasLiveObjectReference(MapProperty->KeyProp, MapHelper.GetKeyPtr(Index)) ||
					PropertyValueHasLiveObjectReference(MapProperty->ValueProp, MapHelper.GetValuePtr(Index)))
				{
					return true;
				}
			}
			return false;
		}

		return false;
	}

	// InstancedStructがProceduralWindであれば可変ポインタを返す（型不一致・無効ならnullptr）
	FKawaiiPhysics_ExternalForce_ProceduralWind* GetMutableProceduralWind(FInstancedStruct& InstancedStruct)
	{
		if (!InstancedStruct.IsValid() ||
			InstancedStruct.GetScriptStruct() != FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct())
		{
			return nullptr;
		}

		return InstancedStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	}

	// 動的パラメータ更新も同様にMutex経由でキューイングする
	bool QueueProceduralWindParams(FKawaiiPhysics_ExternalForce_ProceduralWind& ProceduralWind,
	                               const FKawaiiProceduralWindDynamicParams& Params)
	{
		ProceduralWind.RequestDynamicParams(Params);
		return true;
	}

	// 検証済み前提の単一ノードキュー。公開 API は必ず CanQueueTransientExternalForce を通した後にこれを呼ぶ
	bool QueueTransientExternalForceToNodeUnchecked(FAnimNode_KawaiiPhysics& Node,
	                                                const FInstancedStruct& ExternalForce,
	                                                const float LifetimeSeconds,
	                                                const int64 HandleId)
	{
		FInstancedStruct ExternalForceCopy = ExternalForce;
		Node.RequestTransientExternalForce(MoveTemp(ExternalForceCopy), LifetimeSeconds, HandleId);
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

KawaiiPhysics::FWindGustEnvelope KawaiiPhysics::ResolveWindGustEnvelope(const float Duration, const float RiseTime,
                                                                        const float DecayTime)
{
	FWindGustEnvelope Envelope;
	if (Duration <= 0.0f)
	{
		return Envelope;
	}

	Envelope.RiseTime = FMath::Max(0.0f, RiseTime);
	Envelope.DecayTime = FMath::Max(0.0f, DecayTime);

	const float EdgeTime = Envelope.RiseTime + Envelope.DecayTime;
	if (EdgeTime > Duration && EdgeTime > 0.0f)
	{
		const float Scale = Duration / EdgeTime;
		Envelope.RiseTime *= Scale;
		Envelope.DecayTime *= Scale;
		Envelope.HoldTime = 0.0f;
		return Envelope;
	}

	Envelope.HoldTime = FMath::Max(0.0f, Duration - EdgeTime);
	return Envelope;
}

float KawaiiPhysics::EvaluateEnvelopeAlpha01(const float RiseTime, const float HoldTime, const float DecayTime,
                                             const float ElapsedTime)
{
	if (ElapsedTime < 0.0f)
	{
		return 0.0f;
	}

	const float SafeRiseTime = FMath::Max(RiseTime, 0.0f);
	const float SafeHoldTime = FMath::Max(HoldTime, 0.0f);
	const float SafeDecayTime = FMath::Max(DecayTime, 0.0f);

	// rise フェーズ: 0 から 1 へ線形に立ち上がる
	if (SafeRiseTime > KINDA_SMALL_NUMBER && ElapsedTime < SafeRiseTime)
	{
		return ElapsedTime / SafeRiseTime;
	}

	// hold フェーズ: 1 を維持する
	if (ElapsedTime < SafeRiseTime + SafeHoldTime)
	{
		return 1.0f;
	}

	// DecayTime が実質ゼロならここで即終了（ゼロ除算防止）
	if (SafeDecayTime <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	// decay フェーズ: 1 から 0 へ線形に収束
	const float DecayElapsedTime = ElapsedTime - SafeRiseTime - SafeHoldTime;
	if (DecayElapsedTime < SafeDecayTime)
	{
		return 1.0f - DecayElapsedTime / SafeDecayTime;
	}

	return 0.0f;
}

bool KawaiiPhysics::StructInstanceHasLiveObjectReference(const UScriptStruct* Struct, const void* StructMemory)
{
	return StructMemoryHasLiveObjectReference(Struct, StructMemory);
}

bool UKawaiiPhysicsLibrary::BuildSettingsMultiplierStartRequest(const FKawaiiPhysicsSettingsMultiplier& Scale,
                                                                const float Duration,
                                                                const float BlendInTime,
                                                                const float BlendOutTime,
                                                                FKawaiiPhysicsSettingsMultiplierRequest& OutRequest)
{
	if (Duration == 0.0f)
	{
		return false;
	}

	OutRequest = FKawaiiPhysicsSettingsMultiplierRequest();
	OutRequest.Scale = Scale;
	if (Duration < 0.0f)
	{
		OutRequest.bInfiniteHold = true;
		OutRequest.RiseTime = FMath::Max(BlendInTime, 0.0f);
		OutRequest.HoldTime = 0.0f;
		// 無限 Hold の解放時間は Stop 側の BlendOutTime で決まるため Start 側の値は保持しない
		OutRequest.DecayTime = 0.0f;
		return true;
	}

	const ::KawaiiPhysics::FWindGustEnvelope Envelope =
		::KawaiiPhysics::ResolveWindGustEnvelope(Duration, BlendInTime, BlendOutTime);
	OutRequest.bInfiniteHold = false;
	OutRequest.RiseTime = Envelope.RiseTime;
	OutRequest.HoldTime = Envelope.HoldTime;
	OutRequest.DecayTime = Envelope.DecayTime;
	return true;
}

// transientストアはGC追跡外のため、UObject参照を保持する外力はここで拒否する
bool KawaiiPhysics::CanQueueTransientExternalForce(const FInstancedStruct& ExternalForce, const TCHAR* ContextName)
{
	if (!ExternalForce.IsValid())
	{
		return false;
	}

	const UScriptStruct* ScriptStruct = ExternalForce.GetScriptStruct();
	if (!ScriptStruct || !ScriptStruct->IsChildOf(FKawaiiPhysics_ExternalForce::StaticStruct()))
	{
		return false;
	}

	if (StructInstanceHasLiveObjectReference(ScriptStruct, ExternalForce.GetMemory()))
	{
		UE_LOG(LogKawaiiPhysics, Warning,
		       TEXT("%s: transient force storage is not GC-tracked; rejected a force containing live UObject references (e.g. ExternalOwner or curve assets). Clear the references or use the authored ExternalForces array instead."),
		       ContextName);
		return false;
	}

	return true;
}

// 複数ノードへ同一ハンドルで一時外力をキューイングする
int32 KawaiiPhysics::QueueTransientExternalForceToNodes(TArrayView<FAnimNode_KawaiiPhysics* const> Nodes,
                                                        const FInstancedStruct& ExternalForce,
                                                        const float LifetimeSeconds,
                                                        FKawaiiPhysicsTransientHandle& OutHandle)
{
	OutHandle.Id = 0;
	if (!CanQueueTransientExternalForce(ExternalForce, TEXT("QueueTransientExternalForceToNodes")))
	{
		return 0;
	}

	const int64 HandleId = FAnimNode_KawaiiPhysics::GenerateTransientHandleId();
	int32 AppliedNodeCount = 0;
	for (FAnimNode_KawaiiPhysics* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}

		if (QueueTransientExternalForceToNodeUnchecked(*Node, ExternalForce, LifetimeSeconds, HandleId))
		{
			++AppliedNodeCount;
		}
	}

	if (AppliedNodeCount > 0)
	{
		OutHandle.Id = HandleId;
	}

	return AppliedNodeCount;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ConvertToKawaiiPhysics(const FAnimNodeReference& Node,
                                                                      EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FKawaiiPhysicsReference>(Node, Result);
}

FKawaiiPhysicsSharedPublisherReference UKawaiiPhysicsLibrary::ConvertToKawaiiPhysicsSharedPublisher(
	const FAnimNodeReference& Node,
	EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FKawaiiPhysicsSharedPublisherReference>(Node, Result);
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

TArray<FKawaiiPhysicsSharedPublisherReference>
UKawaiiPhysicsLibrary::CollectKawaiiPhysicsSharedPublisherNodes(
	UAnimInstance* AnimInstance,
	const FGameplayTagContainer& FilterTags,
	bool bFilterExactMatch)
{
	TArray<FKawaiiPhysicsSharedPublisherReference> Nodes;
	if (!ensure(AnimInstance && AnimInstance->GetClass()))
	{
		return Nodes;
	}

	if (const IAnimClassInterface* AnimClassInterface =
		IAnimClassInterface::GetFromClass(AnimInstance->GetClass()))
	{
		const TArray<FStructProperty*>& AnimNodeProperties = AnimClassInterface->GetAnimNodeProperties();
		for (int32 Index = 0; Index < AnimNodeProperties.Num(); ++Index)
		{
			const FStructProperty* AnimNodeProperty = AnimNodeProperties[Index];
			if (!AnimNodeProperty || !AnimNodeProperty->Struct
				|| !AnimNodeProperty->Struct->IsChildOf(
					FKawaiiPhysicsSharedPublisherReference::FInternalNodeType::StaticStruct()))
			{
				continue;
			}

			EAnimNodeReferenceConversionResult Result;
			FKawaiiPhysicsSharedPublisherReference PublisherReference =
				ConvertToKawaiiPhysicsSharedPublisher(FAnimNodeReference(AnimInstance, Index), Result);
			if (Result != EAnimNodeReferenceConversionResult::Succeeded)
			{
				continue;
			}

			const FGameplayTag Tag =
				PublisherReference.GetAnimNode<FAnimNode_KawaiiPhysicsSharedPublisher>().SharedGroupTag;
			const bool bMatches = FilterTags.IsEmpty()
				|| (bFilterExactMatch ? FilterTags.HasTagExact(Tag) : FilterTags.HasTag(Tag));
			if (bMatches)
			{
				Nodes.Add(PublisherReference);
			}
		}
	}

	return Nodes;
}

TArray<FKawaiiPhysicsSharedPublisherReference>
UKawaiiPhysicsLibrary::CollectKawaiiPhysicsSharedPublisherNodesOnComponent(
	USkeletalMeshComponent* MeshComp,
	const FGameplayTagContainer& FilterTags,
	bool bFilterExactMatch)
{
	TArray<FKawaiiPhysicsSharedPublisherReference> Nodes;
	if (!ensure(MeshComp))
	{
		return Nodes;
	}

	if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	{
		Nodes.Append(CollectKawaiiPhysicsSharedPublisherNodes(AnimInstance, FilterTags, bFilterExactMatch));
	}

	const TArray<UAnimInstance*>& LinkedInstances =
		const_cast<const USkeletalMeshComponent*>(MeshComp)->GetLinkedAnimInstances();
	for (UAnimInstance* LinkedInstance : LinkedInstances)
	{
		if (LinkedInstance)
		{
			Nodes.Append(CollectKawaiiPhysicsSharedPublisherNodes(LinkedInstance, FilterTags, bFilterExactMatch));
		}
	}

	if (UAnimInstance* PostProcessAnimInstance = MeshComp->GetPostProcessInstance())
	{
		Nodes.Append(CollectKawaiiPhysicsSharedPublisherNodes(PostProcessAnimInstance, FilterTags, bFilterExactMatch));
	}

	return Nodes;
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

bool UKawaiiPhysicsLibrary::GetSimpleWorldCollisionDebugInfo(
	const USkeletalMeshComponent* SkelComp,
	FKawaiiPhysicsSimpleWorldCollisionDebugInfo& OutInfo)
{
	OutInfo = FKawaiiPhysicsSimpleWorldCollisionDebugInfo();
	if (!SkelComp)
	{
		return false;
	}

	UWorld* World = SkelComp->GetWorld();
	if (!World)
	{
		return false;
	}

	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem =
		World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>();
	if (!Subsystem)
	{
		return false;
	}

	return Subsystem->BuildSimpleWorldCollisionDebugInfo(SkelComp, OutInfo);
}

bool UKawaiiPhysicsLibrary::SetSharedPublisherEnabled(
	AActor* Actor,
	FGameplayTag SharedGroupTag,
	bool bEnabled)
{
	if (!IsInGameThread() || !Actor || !SharedGroupTag.IsValid())
	{
		return false;
	}

	UWorld* World = Actor->GetWorld();
	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem =
		World ? World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return false;
	}

	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> Entry =
		Subsystem->FindSharedPublisherEntry(Actor, SharedGroupTag);
	const uint64 MaxAgeFrames = static_cast<uint64>(
		FMath::Max(0, GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge()));
	if (!Entry.IsValid() || Entry->IsExpired(GFrameCounter, MaxAgeFrames))
	{
		return false;
	}

	Entry->RequestPublisherEnabled(bEnabled);
	return true;
}

bool UKawaiiPhysicsLibrary::SetSimpleWorldCollisionSettingsOnSharedPublisher(
	AActor* Actor,
	FGameplayTag SharedGroupTag,
	const FKawaiiPhysicsSimpleWorldCollisionSettings& Settings)
{
	if (!IsInGameThread() || !Actor || !SharedGroupTag.IsValid())
	{
		return false;
	}

	UWorld* World = Actor->GetWorld();
	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem =
		World ? World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return false;
	}

	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> Entry =
		Subsystem->FindSharedPublisherEntry(Actor, SharedGroupTag);
	const uint64 MaxAgeFrames = static_cast<uint64>(
		FMath::Max(0, GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge()));
	if (!Entry.IsValid() || Entry->IsExpired(GFrameCounter, MaxAgeFrames))
	{
		return false;
	}

	Entry->RequestSimpleWorldSettings(Settings);
	return true;
}

bool UKawaiiPhysicsLibrary::GetSimpleWorldCollisionSettingsOnSharedPublisher(
	AActor* Actor,
	FGameplayTag SharedGroupTag,
	FKawaiiPhysicsSimpleWorldCollisionSettings& OutSettings)
{
	OutSettings = FKawaiiPhysicsSimpleWorldCollisionSettings();
	if (!IsInGameThread() || !Actor || !SharedGroupTag.IsValid())
	{
		return false;
	}

	UWorld* World = Actor->GetWorld();
	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem =
		World ? World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return false;
	}

	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> Entry =
		Subsystem->FindSharedPublisherEntry(Actor, SharedGroupTag);
	const uint64 MaxAgeFrames = static_cast<uint64>(
		FMath::Max(0, GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge()));
	if (!Entry.IsValid() || Entry->IsExpired(GFrameCounter, MaxAgeFrames))
	{
		return false;
	}

	FKawaiiPhysicsSharedPublisherState State;
	Entry->ReadState(State);
	OutSettings = State.SimpleWorldSettings;
	return true;
}

bool UKawaiiPhysicsLibrary::GetSharedPublisherDebugInfo(
	AActor* Actor,
	FGameplayTag SharedGroupTag,
	FKawaiiPhysicsSharedPublisherDebugInfo& OutInfo)
{
	OutInfo = FKawaiiPhysicsSharedPublisherDebugInfo();
	if (!IsInGameThread() || !Actor || !SharedGroupTag.IsValid())
	{
		return false;
	}

	UWorld* World = Actor->GetWorld();
	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem =
		World ? World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return false;
	}

	return Subsystem->BuildSharedPublisherDebugInfo(Actor, SharedGroupTag, OutInfo);
}

int32 UKawaiiPhysicsLibrary::GetSimpleWorldColliderCountOnComponent(
	USkeletalMeshComponent* MeshComp,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch)
{
	if (!MeshComp)
	{
		return 0;
	}

	int32 Count = 0;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("GetSimpleWorldColliderCountOnComponent"),
			[&Count](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				Count += InKawaiiPhysics.GetNumSimpleWorldColliders();
			});
	}

	return Count;
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

bool UKawaiiPhysicsLibrary::DoesNodePropertyRequireSimpleWorldCollisionReinit(FName PropertyName)
{
	return GetNodeSimpleWorldCollisionReinitPropertyNames().Contains(PropertyName);
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
		if (DoesNodePropertyRequireSimpleWorldCollisionReinit(PropertyName))
		{
			Node.RequestSimpleWorldCollisionReinit();
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
			if (DoesNodePropertyRequireSimpleWorldCollisionReinit(PropertyName))
			{
				Node.RequestSimpleWorldCollisionReinit();
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
		constexpr int32 ExportPortFlags = PPF_ExternalEditor;
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		Property->ExportTextItem(OutValueText, NodeValuePtr, NodeValuePtr, nullptr, ExportPortFlags);
#else
		Property->ExportTextItem_Direct(OutValueText, NodeValuePtr, NodeValuePtr, nullptr, ExportPortFlags);
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

bool UKawaiiPhysicsLibrary::AddExternalForcesOnComponent(USkeletalMeshComponent* MeshComp,
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

// 非推奨（v1.22.0）: AddExternalForcesOnComponent を使う
bool UKawaiiPhysicsLibrary::AddExternalForcesToComponent(USkeletalMeshComponent* MeshComp,
                                                         TArray<FInstancedStruct>& ExternalForces,
                                                         UObject* Owner,
                                                         FGameplayTagContainer& FilterTags,
                                                         bool bFilterExactMatch, bool bIsOneShot)
{
	return AddExternalForcesOnComponent(MeshComp, ExternalForces, Owner, FilterTags, bFilterExactMatch, bIsOneShot);
}

bool UKawaiiPhysicsLibrary::RemoveExternalForcesOnComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
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

// 非推奨（v1.22.0）: RemoveExternalForcesOnComponent を使う
bool UKawaiiPhysicsLibrary::RemoveExternalForcesFromComponent(USkeletalMeshComponent* MeshComp, UObject* Owner,
                                                              FGameplayTagContainer& FilterTags, bool bFilterExactMatch)
{
	return RemoveExternalForcesOnComponent(MeshComp, Owner, FilterTags, bFilterExactMatch);
}

// ランタイム専用の一時外力をノードへキューイングする
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::AddTransientExternalForce(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	FKawaiiPhysicsTransientHandle& OutHandle,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	FInstancedStruct ExternalForce,
	const float LifetimeSeconds)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;
	OutHandle.Id = 0;

	if (!::KawaiiPhysics::CanQueueTransientExternalForce(ExternalForce, TEXT("AddTransientExternalForce")))
	{
		return KawaiiPhysics;
	}

	const int64 HandleId = FAnimNode_KawaiiPhysics::GenerateTransientHandleId();

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("AddTransientExternalForce"),
		[&ExecResult, ExternalForce = MoveTemp(ExternalForce), LifetimeSeconds, HandleId](
			FAnimNode_KawaiiPhysics& InKawaiiPhysics) mutable
		{
			if (QueueTransientExternalForceToNodeUnchecked(InKawaiiPhysics, ExternalForce, LifetimeSeconds,
			                                                HandleId))
			{
				ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
			}
		});

	if (ExecResult == EKawaiiPhysicsAccessExternalForceResult::Valid)
	{
		OutHandle.Id = HandleId;
	}

	return KawaiiPhysics;
}

// Component内の対象ノードへ停止ハンドル付きの一時外力をキューイングする
int32 UKawaiiPhysicsLibrary::AddTransientExternalForceOnComponent(
	USkeletalMeshComponent* MeshComp,
	FKawaiiPhysicsTransientHandle& OutHandle,
	FInstancedStruct ExternalForce,
	const float LifetimeSeconds,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch)
{
	OutHandle.Id = 0;
	if (!::KawaiiPhysics::CanQueueTransientExternalForce(ExternalForce, TEXT("AddTransientExternalForceOnComponent")))
	{
		return 0;
	}

	const int64 HandleId = FAnimNode_KawaiiPhysics::GenerateTransientHandleId();
	int32 AppliedNodeCount = 0;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("AddTransientExternalForceOnComponent"),
			[&AppliedNodeCount, &ExternalForce, LifetimeSeconds, HandleId](
				FAnimNode_KawaiiPhysics& InKawaiiPhysics) mutable
			{
				if (QueueTransientExternalForceToNodeUnchecked(InKawaiiPhysics, ExternalForce, LifetimeSeconds,
				                                                HandleId))
				{
					++AppliedNodeCount;
				}
			});
	}

	if (AppliedNodeCount > 0)
	{
		OutHandle.Id = HandleId;
	}

	return AppliedNodeCount;
}

// 停止ハンドル付きのGustをノードへキューイングする
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::StartProceduralWindGust(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	FKawaiiPhysicsTransientHandle& OutHandle,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	const float Strength,
	const float Duration,
	const float RiseTime,
	const float DecayTime,
	const FVector GustDirection,
	const int32 ExternalForceIndex,
	const bool bRealTimeEnvelope)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;
	OutHandle.Id = 0;

	const ::KawaiiPhysics::FWindGustEnvelope Envelope =
		::KawaiiPhysics::ResolveWindGustEnvelope(Duration, RiseTime, DecayTime);
	const int64 HandleId = FAnimNode_KawaiiPhysics::GenerateTransientHandleId();

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("StartProceduralWindGust"),
		[&ExecResult, Strength, Envelope, GustDirection, ExternalForceIndex, HandleId, bRealTimeEnvelope](
			FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.RequestTransientGust(Strength, Envelope.RiseTime, Envelope.DecayTime, GustDirection,
			                                     ExternalForceIndex, Envelope.HoldTime, HandleId,
			                                     bRealTimeEnvelope);
			ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
		});

	if (ExecResult == EKawaiiPhysicsAccessExternalForceResult::Valid)
	{
		OutHandle.Id = HandleId;
	}

	return KawaiiPhysics;
}

// 指定したExternalForceIndexのProceduralWindへ動的パラメータ更新をリクエストする
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

// 指定したExternalForceIndexのProceduralWindから現在の動的パラメータを取得する
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::GetProceduralWindParameters(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	const int32 ExternalForceIndex,
	FKawaiiProceduralWindDynamicParams& OutParams)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetProceduralWindParameters"),
		[&ExecResult, ExternalForceIndex, &OutParams](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (!InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex))
			{
				return;
			}

			if (FKawaiiPhysics_ExternalForce_ProceduralWind* ProceduralWind =
				GetMutableProceduralWind(InKawaiiPhysics.ExternalForces[ExternalForceIndex]))
			{
				OutParams = ProceduralWind->BuildDynamicParamsSnapshot();
				ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
			}
		});

	return KawaiiPhysics;
}

// Component内の対象ノードへ停止ハンドル付きのGustをキューイングする
int32 UKawaiiPhysicsLibrary::StartProceduralWindGustOnComponent(
	USkeletalMeshComponent* MeshComp,
	FKawaiiPhysicsTransientHandle& OutHandle,
	const float Strength,
	const float Duration,
	const float RiseTime,
	const float DecayTime,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch,
	const FVector GustDirection,
	const bool bRealTimeEnvelope)
{
	OutHandle.Id = 0;
	int32 AppliedNodeCount = 0;

	const ::KawaiiPhysics::FWindGustEnvelope Envelope =
		::KawaiiPhysics::ResolveWindGustEnvelope(Duration, RiseTime, DecayTime);
	const int64 HandleId = FAnimNode_KawaiiPhysics::GenerateTransientHandleId();

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("StartProceduralWindGustOnComponent"),
			[&AppliedNodeCount, Strength, Envelope, GustDirection, HandleId, bRealTimeEnvelope](
				FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				InKawaiiPhysics.RequestTransientGust(Strength, Envelope.RiseTime, Envelope.DecayTime, GustDirection,
				                                     FAnimNode_KawaiiPhysics::TransientGustInheritAllWinds,
				                                     Envelope.HoldTime, HandleId, bRealTimeEnvelope);
				++AppliedNodeCount;
			});
	}

	if (AppliedNodeCount > 0)
	{
		OutHandle.Id = HandleId;
	}

	return AppliedNodeCount;
}

// ハンドルに一致する一時外力の停止をノードへキューイングする
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::StopTransientExternalForce(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	const FKawaiiPhysicsTransientHandle Handle,
	const float BlendOutTime)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("StopTransientExternalForce"),
		[&ExecResult, Handle, BlendOutTime](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.RequestStopTransientExternalForce(Handle.Id, BlendOutTime);
			ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
		});

	return KawaiiPhysics;
}

// Component内の対象ノードへ一時外力停止をキューイングする
int32 UKawaiiPhysicsLibrary::StopTransientExternalForceOnComponent(
	USkeletalMeshComponent* MeshComp,
	const FKawaiiPhysicsTransientHandle Handle,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch,
	const float BlendOutTime)
{
	if (!Handle.IsSet())
	{
		return 0;
	}

	int32 AppliedNodeCount = 0;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("StopTransientExternalForceOnComponent"),
			[&AppliedNodeCount, Handle, BlendOutTime](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				InKawaiiPhysics.RequestStopTransientExternalForce(Handle.Id, BlendOutTime);
				++AppliedNodeCount;
			});
	}

	return AppliedNodeCount;
}

// 停止ハンドル付きの物理設定倍率をノードへキューイングする
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::StartPhysicsSettingsMultiplier(
	EKawaiiPhysicsAccessResult& ExecResult,
	FKawaiiPhysicsTransientHandle& OutHandle,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	const FKawaiiPhysicsSettingsMultiplier SettingsScale,
	const float Duration,
	const float BlendInTime,
	const float BlendOutTime)
{
	ExecResult = EKawaiiPhysicsAccessResult::NotValid;
	OutHandle.Id = 0;

	FKawaiiPhysicsSettingsMultiplierRequest Request;
	if (!BuildSettingsMultiplierStartRequest(SettingsScale, Duration, BlendInTime, BlendOutTime, Request))
	{
		return KawaiiPhysics;
	}

	const int64 HandleId = FAnimNode_KawaiiPhysics::GenerateTransientHandleId();
	Request.HandleId = HandleId;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("StartPhysicsSettingsMultiplier"),
		[&ExecResult, Request](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.RequestStartPhysicsSettingsMultiplier(Request.Scale, Request.RiseTime, Request.HoldTime,
			                                              Request.DecayTime, Request.HandleId, Request.bInfiniteHold);
			ExecResult = EKawaiiPhysicsAccessResult::Valid;
		});

	if (ExecResult == EKawaiiPhysicsAccessResult::Valid)
	{
		OutHandle.Id = Request.HandleId;
	}

	return KawaiiPhysics;
}

// ハンドルに一致する物理設定倍率の停止をノードへキューイングする
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::StopPhysicsSettingsMultiplier(
	EKawaiiPhysicsAccessResult& ExecResult,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	const FKawaiiPhysicsTransientHandle Handle,
	const float BlendOutTime)
{
	ExecResult = EKawaiiPhysicsAccessResult::NotValid;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("StopPhysicsSettingsMultiplier"),
		[&ExecResult, Handle, BlendOutTime](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.RequestStopPhysicsSettingsMultiplier(Handle.Id, BlendOutTime);
			ExecResult = EKawaiiPhysicsAccessResult::Valid;
		});

	return KawaiiPhysics;
}

// Component内の対象ノード（Tagフィルタ適用）へ、共通ハンドルの物理設定倍率をキューイングする
int32 UKawaiiPhysicsLibrary::StartPhysicsSettingsMultiplierOnComponent(
	USkeletalMeshComponent* MeshComp,
	FKawaiiPhysicsTransientHandle& OutHandle,
	const FKawaiiPhysicsSettingsMultiplier SettingsScale,
	const float Duration,
	const float BlendInTime,
	const float BlendOutTime,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch)
{
	OutHandle.Id = 0;
	int32 AppliedNodeCount = 0;

	FKawaiiPhysicsSettingsMultiplierRequest Request;
	if (!BuildSettingsMultiplierStartRequest(SettingsScale, Duration, BlendInTime, BlendOutTime, Request))
	{
		return 0;
	}

	const int64 HandleId = FAnimNode_KawaiiPhysics::GenerateTransientHandleId();
	Request.HandleId = HandleId;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("StartPhysicsSettingsMultiplierOnComponent"),
			[&AppliedNodeCount, Request](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				InKawaiiPhysics.RequestStartPhysicsSettingsMultiplier(Request.Scale, Request.RiseTime, Request.HoldTime,
				                                              Request.DecayTime, Request.HandleId, Request.bInfiniteHold);
				++AppliedNodeCount;
			});
	}

	if (AppliedNodeCount > 0)
	{
		OutHandle.Id = Request.HandleId;
	}

	return AppliedNodeCount;
}

// Component内の対象ノードへ物理設定倍率の停止をキューイングする
int32 UKawaiiPhysicsLibrary::StopPhysicsSettingsMultiplierOnComponent(
	USkeletalMeshComponent* MeshComp,
	const FKawaiiPhysicsTransientHandle Handle,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch,
	const float BlendOutTime)
{
	if (!Handle.IsSet())
	{
		return 0;
	}

	int32 AppliedNodeCount = 0;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("StopPhysicsSettingsMultiplierOnComponent"),
			[&AppliedNodeCount, Handle, BlendOutTime](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				InKawaiiPhysics.RequestStopPhysicsSettingsMultiplier(Handle.Id, BlendOutTime);
				++AppliedNodeCount;
			});
	}

	return AppliedNodeCount;
}

FKawaiiPhysicsTransientHandle UKawaiiPhysicsLibrary::GenerateTransientHandle()
{
	FKawaiiPhysicsTransientHandle Handle;
	Handle.Id = FAnimNode_KawaiiPhysics::GenerateTransientHandleId();
	return Handle;
}

// 外部駆動の物理設定倍率をノードへキューイングする
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::PushPhysicsSettingsMultiplier(
	EKawaiiPhysicsAccessResult& ExecResult,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	const FKawaiiPhysicsTransientHandle Handle,
	const FKawaiiPhysicsSettingsMultiplier SettingsScale,
	const float Alpha)
{
	ExecResult = EKawaiiPhysicsAccessResult::NotValid;

	if (!Handle.IsSet())
	{
		return KawaiiPhysics;
	}

	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("PushPhysicsSettingsMultiplier"),
		[&ExecResult, Handle, SettingsScale, ClampedAlpha](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.RequestPushPhysicsSettingsMultiplier(SettingsScale, ClampedAlpha, Handle.Id))
			{
				ExecResult = EKawaiiPhysicsAccessResult::Valid;
			}
		});

	return KawaiiPhysics;
}

int32 UKawaiiPhysicsLibrary::PushPhysicsSettingsMultiplierOnComponent(
	USkeletalMeshComponent* MeshComp,
	const FKawaiiPhysicsTransientHandle Handle,
	const FKawaiiPhysicsSettingsMultiplier SettingsScale,
	const float Alpha,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch)
{
	return PushPhysicsSettingsMultiplierOnComponent(MeshComp, Handle, SettingsScale, Alpha, FilterTags, bFilterExactMatch,
	                                             0, 0.2f);
}

// Component内の対象ノードへ外部駆動の物理設定倍率をキューイングする
int32 UKawaiiPhysicsLibrary::PushPhysicsSettingsMultiplierOnComponent(
	USkeletalMeshComponent* MeshComp,
	const FKawaiiPhysicsTransientHandle Handle,
	const FKawaiiPhysicsSettingsMultiplier& SettingsScale,
	const float Alpha,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch,
	const int32 LeaseEvaluations,
	const float LeaseExpireBlendOutTime)
{
	if (!Handle.IsSet())
	{
		return 0;
	}

	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	int32 AppliedNodeCount = 0;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("PushPhysicsSettingsMultiplierOnComponent"),
			[&AppliedNodeCount, Handle, &SettingsScale, ClampedAlpha, LeaseEvaluations, LeaseExpireBlendOutTime](
				FAnimNode_KawaiiPhysics& InKawaiiPhysics)
			{
				if (InKawaiiPhysics.RequestPushPhysicsSettingsMultiplier(SettingsScale, ClampedAlpha, Handle.Id,
				                                                      LeaseEvaluations, LeaseExpireBlendOutTime))
				{
					++AppliedNodeCount;
				}
			});
	}

	return AppliedNodeCount;
}

bool UKawaiiPhysicsLibrary::IsTransientHandleSet(const FKawaiiPhysicsTransientHandle& Handle)
{
	return Handle.IsSet();
}

// ExternalForces配列から指定型のindexを検索する
TArray<int32> UKawaiiPhysicsLibrary::FindExternalForceIndicesByStruct(
	const FKawaiiPhysicsReference& KawaiiPhysics,
	UScriptStruct* StructType,
	const bool bEnabledOnly)
{
	TArray<int32> Indices;
	if (!StructType)
	{
		return Indices;
	}

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("FindExternalForceIndicesByStruct"),
		[&Indices, StructType, bEnabledOnly](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			for (int32 Index = 0; Index < InKawaiiPhysics.ExternalForces.Num(); ++Index)
			{
				FInstancedStruct& InstancedStruct = InKawaiiPhysics.ExternalForces[Index];
				if (!InstancedStruct.IsValid())
				{
					continue;
				}

				const UScriptStruct* ScriptStruct = InstancedStruct.GetScriptStruct();
				if (!ScriptStruct || !ScriptStruct->IsChildOf(StructType))
				{
					continue;
				}

				if (bEnabledOnly)
				{
					const FKawaiiPhysics_ExternalForce* Force =
						InstancedStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>();
					if (!Force || !Force->bIsEnabled)
					{
						continue;
					}
				}

				Indices.Add(Index);
			}
		});

	return Indices;
}

// Component内の対象ノード（Tagフィルタ適用）を走査し、ProceduralWindへ一括でパラメータ更新をリクエストする
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

// DataAssetのプリセットから指定したExternalForceIndexのProceduralWindへ動的パラメータ更新をリクエストする
FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ApplyProceduralWindPreset(
	EKawaiiPhysicsAccessExternalForceResult& ExecResult,
	const FKawaiiPhysicsReference& KawaiiPhysics,
	const int32 ExternalForceIndex,
	const UKawaiiPhysicsWindPresetDataAsset* PresetDataAsset,
	const FGameplayTag PresetTag)
{
	ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;

	FKawaiiProceduralWindDynamicParams Params;
	if (!UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(PresetDataAsset, PresetTag, Params))
	{
		return KawaiiPhysics;
	}

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("ApplyProceduralWindPreset"),
		[&ExecResult, ExternalForceIndex, Params](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
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

// DataAssetのプリセットからComponent内の対象ノード（Tagフィルタ適用）のProceduralWindへ一括でパラメータ更新をリクエストする
int32 UKawaiiPhysicsLibrary::ApplyProceduralWindPresetOnComponent(
	USkeletalMeshComponent* MeshComp,
	const UKawaiiPhysicsWindPresetDataAsset* PresetDataAsset,
	const FGameplayTag PresetTag,
	const FGameplayTagContainer& FilterTags,
	const bool bFilterExactMatch)
{
	FKawaiiProceduralWindDynamicParams Params;
	if (!UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(PresetDataAsset, PresetTag, Params))
	{
		return 0;
	}

	int32 AppliedForceCount = 0;

	TArray<FKawaiiPhysicsReference> KawaiiPhysicsReferences;
	CollectKawaiiPhysicsNodes(KawaiiPhysicsReferences, MeshComp, FilterTags, bFilterExactMatch);
	for (auto& KawaiiPhysicsReference : KawaiiPhysicsReferences)
	{
		KawaiiPhysicsReference.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
			TEXT("ApplyProceduralWindPresetOnComponent"),
			[&AppliedForceCount, Params](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
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

bool UKawaiiPhysicsLibrary::SetAlphaOnComponent(USkeletalMeshComponent* MeshComp, float Alpha,
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

// 非推奨（v1.22.0）: SetAlphaOnComponent を使う
bool UKawaiiPhysicsLibrary::SetAlphaToComponent(USkeletalMeshComponent* MeshComp, float Alpha,
                                                FGameplayTagContainer& FilterTags, bool bFilterExactMatch)
{
	return SetAlphaOnComponent(MeshComp, Alpha, FilterTags, bFilterExactMatch);
}

bool UKawaiiPhysicsLibrary::GetAlphaOnComponent(USkeletalMeshComponent* MeshComp, float& OutAlpha,
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

// 非推奨（v1.22.0）: GetAlphaOnComponent を使う
bool UKawaiiPhysicsLibrary::GetAlphaFromComponent(USkeletalMeshComponent* MeshComp, float& OutAlpha,
                                                  FGameplayTagContainer& FilterTags, bool bFilterExactMatch)
{
	return GetAlphaOnComponent(MeshComp, OutAlpha, FilterTags, bFilterExactMatch);
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
