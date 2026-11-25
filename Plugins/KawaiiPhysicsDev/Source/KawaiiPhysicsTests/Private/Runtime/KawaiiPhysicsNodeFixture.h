// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Misc/MemStack.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace KawaiiPhysicsNodeFixture
{

struct FBoneDefinition
{
	FName Name;
	int32 ParentIndex = INDEX_NONE;
	FTransform LocalTransform = FTransform::Identity;
};

inline TArray<FBoneIndexType> BuildSkeleton(USkeleton& Skeleton, const TArray<FBoneDefinition>& Bones)
{
	TArray<FBoneIndexType> Indices;
	FReferenceSkeletonModifier Modifier(&Skeleton);
	for (const FBoneDefinition& Bone : Bones)
	{
		Modifier.Add(FMeshBoneInfo(Bone.Name, Bone.Name.ToString(), Bone.ParentIndex), Bone.LocalTransform);
		Indices.Add(static_cast<FBoneIndexType>(Indices.Num()));
	}
	return Indices;
}

inline TArray<FBoneDefinition> BuildVerticalChain(const int32 BoneCount,
	const FVector& Offset = FVector(0.0, 0.0, -10.0), const FName RootName = TEXT("root"),
	const FString& BoneNamePrefix = TEXT("chain_"))
{
	TArray<FBoneDefinition> Bones;
	Bones.Add({RootName, INDEX_NONE, FTransform::Identity});
	for (int32 BoneIndex = 1; BoneIndex < BoneCount; ++BoneIndex)
	{
		Bones.Add({FName(*FString::Printf(TEXT("%s%d"), *BoneNamePrefix, BoneIndex)), BoneIndex - 1, FTransform(Offset)});
	}
	return Bones;
}

inline FName GetHairBoneName(const int32 Chain, const int32 Link)
{
	return FName(*FString::Printf(TEXT("hair_%03d_%d"), Chain, Link));
}

inline TArray<FBoneDefinition> BuildHair(const int32 Chains, const int32 BonesPerChain)
{
	TArray<FBoneDefinition> Bones;
	Bones.Add({TEXT("hair_root"), INDEX_NONE, FTransform::Identity});
	for (int32 Chain = 0; Chain < Chains; ++Chain)
	{
		int32 Parent = 0;
		for (int32 Link = 0; Link < BonesPerChain; ++Link)
		{
			const double Angle = 2.0 * PI * Chain / Chains;
			const FVector Offset = Link == 0
				? FVector(FMath::Cos(Angle) * 8.0, FMath::Sin(Angle) * 8.0, 0.0)
				: FVector(0.0, 0.0, -4.0);
			const int32 Index = Bones.Num();
			Bones.Add({GetHairBoneName(Chain, Link), Parent, FTransform(Offset)});
			Parent = Index;
		}
	}
	return Bones;
}

struct FScopedDeveloperSettings
{
	UKawaiiPhysicsDeveloperSettings* Settings = GetMutableDefault<UKawaiiPhysicsDeveloperSettings>();
	TStrongObjectPtr<UKawaiiPhysicsDeveloperSettings> Snapshot{
		DuplicateObject<UKawaiiPhysicsDeveloperSettings>(Settings, GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), Settings->GetClass()))};

	explicit FScopedDeveloperSettings(
		TFunctionRef<void(UKawaiiPhysicsDeveloperSettings&)> Configure)
	{
		Configure(*Settings);
	}

	~FScopedDeveloperSettings()
	{
		// UObject 自体は置き換えず、継承分を含む全プロパティを復元する。
		for (TFieldIterator<FProperty> Property(Settings->GetClass()); Property; ++Property)
		{
			Property->CopyCompleteValue_InContainer(Settings, Snapshot.Get());
		}
	}

	FScopedDeveloperSettings(const FScopedDeveloperSettings&) = delete;
	FScopedDeveloperSettings& operator=(const FScopedDeveloperSettings&) = delete;
};

struct FScopedCVar
{
	IConsoleVariable* Variable = nullptr;
	FString PreviousValue;
	EConsoleVariableFlags PreviousFlags = ECVF_Default;

	FScopedCVar(const FString& Name, const FString& Value)
	{
		Variable = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (Variable)
		{
			PreviousValue = Variable->GetString();
			PreviousFlags = Variable->GetFlags();
			Variable->Set(*Value, ECVF_SetByConsole);
		}
	}

	~FScopedCVar()
	{
		if (Variable)
		{
			Variable->Set(*PreviousValue, ECVF_SetByConsole);
			Variable->ClearFlags(ECVF_SetByMask);
			Variable->SetFlags(PreviousFlags);
		}
	}

	FScopedCVar(const FScopedCVar&) = delete;
	FScopedCVar& operator=(const FScopedCVar&) = delete;
};

template <typename TForce>
TForce& AddExternalForce(FAnimNode_KawaiiPhysics& Node)
{
	return Node.ExternalForces.Add_GetRef(FInstancedStruct::Make<TForce>()).GetMutable<TForce>();
}

inline FSphericalLimit& AddSphericalLimit(FAnimNode_KawaiiPhysics& Node, const FSphericalLimit& Limit = {})
{
	return Node.SphericalLimits.Add_GetRef(Limit);
}

inline FCapsuleLimit& AddCapsuleLimit(FAnimNode_KawaiiPhysics& Node, const FCapsuleLimit& Limit = {})
{
	return Node.CapsuleLimits.Add_GetRef(Limit);
}

inline FBoxLimit& AddBoxLimit(FAnimNode_KawaiiPhysics& Node, const FBoxLimit& Limit = {})
{
	return Node.BoxLimits.Add_GetRef(Limit);
}

inline FPlanarLimit& AddPlanarLimit(FAnimNode_KawaiiPhysics& Node, const FPlanarLimit& Limit = {})
{
	return Node.PlanarLimits.Add_GetRef(Limit);
}

inline FTaperedCapsuleLimit& AddTaperedCapsuleLimit(FAnimNode_KawaiiPhysics& Node,
	const FTaperedCapsuleLimit& Limit = {})
{
	return Node.TaperedCapsuleLimits.Add_GetRef(Limit);
}

inline void SetLimitsDataAsset(FAnimNode_KawaiiPhysics& Node, UKawaiiPhysicsLimitsDataAsset* DataAsset)
{
	Node.LimitsDataAsset = DataAsset;
}

inline UWorld* MakeWorld()
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
	return UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Values);
}

struct FProxy : FAnimInstanceProxy
{
	using FAnimInstanceProxy::FAnimInstanceProxy;
	using FAnimInstanceProxy::PreUpdate;
};

enum class EActorKind : uint8
{
	Actor,
	Character,
};

struct FCVarOverride
{
	FString Name;
	FString Value;
};

struct FOptions
{
	TArray<FBoneDefinition> Bones;
	float FrameDt = 1.0f / 60.0f;
	EKawaiiPhysicsSimulationSpace SimulationSpace = EKawaiiPhysicsSimulationSpace::ComponentSpace;
	FName BaseBoneName = NAME_None;
	EActorKind ActorKind = EActorKind::Actor;
	bool bRegisterComponent = false;
	bool bPreUpdateEveryFrame = false;
	bool bUseLocalPose = false;
	TFunction<void(FAnimNode_KawaiiPhysics&)> ConfigureNode;
	TFunction<void(UKawaiiPhysicsDeveloperSettings&)> ConfigureDeveloperSettings;
	TArray<FCVarOverride> ConsoleVariables;
	TFunction<void(FCompactPose&, int32)> SetInputPose;
	TFunction<FTransform(int32)> GetComponentTransform;
	// Location と Rotator の個別設定が必要な Golden で丸め方を維持する。
	TFunction<void(USkeletalMeshComponent&, int32)> SetComponentTransform;
};

struct FFixture
{
	FOptions Options;
	TUniquePtr<FScopedDeveloperSettings> SettingsScope;
	TArray<TUniquePtr<FScopedCVar>> CVarScopes;
	TStrongObjectPtr<USkeleton> Skeleton{NewObject<USkeleton>()};
	UWorld* World = nullptr;
	AActor* Actor = nullptr;
	USkeletalMeshComponent* Component = nullptr;
	UAnimInstance* Anim = nullptr;
	TUniquePtr<FProxy> Proxy;
	FKawaiiPhysicsTestAccessor Accessor;
	FAnimNode_KawaiiPhysics& Node = Accessor.Node;
	TArray<FBoneTransform> OutTransforms;
	float FrameDt;
	int32 FrameIndex = 0;

	explicit FFixture(const FOptions& InOptions) : Options(InOptions), FrameDt(Options.FrameDt)
	{
		if (Options.ConfigureDeveloperSettings)
		{
			SettingsScope = MakeUnique<FScopedDeveloperSettings>(Options.ConfigureDeveloperSettings);
		}
		for (const FCVarOverride& Override : Options.ConsoleVariables)
		{
			CVarScopes.Add(MakeUnique<FScopedCVar>(Override.Name, Override.Value));
		}
		const TArray<FBoneIndexType> RequiredIndices = BuildSkeleton(*Skeleton, Options.Bones);
		World = MakeWorld();
		if (Options.ActorKind == EActorKind::Character)
		{
			ACharacter* Character = World->SpawnActor<ACharacter>();
			Actor = Character;
			Component = Character->GetMesh();
		}
		else
		{
			Actor = World->SpawnActor<AActor>();
			Component = NewObject<USkeletalMeshComponent>(Actor);
			Actor->AddInstanceComponent(Component);
			Actor->SetRootComponent(Component);
		}
		if (Options.bRegisterComponent)
		{
			Component->RegisterComponent();
		}
		Anim = NewObject<UAnimInstance>(Component);
		Proxy = MakeUnique<FProxy>(Anim);
		Proxy->PreUpdate(Anim, FrameDt);
		Proxy->GetRequiredBones().InitializeTo(RequiredIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);
		Node.SimulationSpace = Options.SimulationSpace;
		if (!Options.BaseBoneName.IsNone())
		{
			Node.SimulationBaseBone.BoneName = Options.BaseBoneName;
		}
		if (Options.ConfigureNode)
		{
			Options.ConfigureNode(Node);
		}
		Node.OnInitializeAnimInstance(Proxy.Get(), Anim);
		Node.Initialize_AnyThread(FAnimationInitializeContext(Proxy.Get()));
		Node.CacheBones_AnyThread(FAnimationCacheBonesContext(Proxy.Get()));
		Node.PreUpdate(Anim);
		OutTransforms.Reserve(RequiredIndices.Num());
	}

	~FFixture()
	{
		Proxy.Reset();
		World->DestroyWorld(false);
		// 同じ CVar の上書きを重ねた場合も、設定した逆順で戻す。
		while (!CVarScopes.IsEmpty())
		{
			CVarScopes.Pop();
		}
	}

	void SetSimulationSpace(const EKawaiiPhysicsSimulationSpace Space, const FName BaseBoneName)
	{
		Node.SimulationSpace = Space;
		Node.SimulationBaseBone.BoneName = BaseBoneName;
	}

	void SetRequiredBones(const TArray<FBoneIndexType>& RequiredIndices)
	{
		Proxy->GetRequiredBones().InitializeTo(RequiredIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);
		Node.CacheBones_AnyThread(FAnimationCacheBonesContext(Proxy.Get()));
	}

	template <typename TBeforeEvaluate>
	void Evaluate(FComponentSpacePoseContext& Context, TBeforeEvaluate&& BeforeEvaluate)
	{
		Context.Pose.InitPose(&Proxy->GetRequiredBones());
		Node.UpdateInternal(FAnimationUpdateContext(Proxy.Get(), FrameDt));
		OutTransforms.Reset();
		BeforeEvaluate(Node);
		Node.EvaluateSkeletalControl_AnyThread(Context, OutTransforms);
	}

	// Perf では Context と呼び出し側の MemMark を再利用し、計測区間を維持する。
	void Evaluate(FComponentSpacePoseContext& Context)
	{
		Evaluate(Context, [](FAnimNode_KawaiiPhysics&) {});
	}

	const TArray<FBoneTransform>& EvaluateFrame(const int32 Frame = INDEX_NONE)
	{
		const int32 CurrentFrame = Frame == INDEX_NONE ? FrameIndex : Frame;
		FrameIndex = CurrentFrame + 1;
		if (Options.GetComponentTransform)
		{
			Component->SetWorldTransform(Options.GetComponentTransform(CurrentFrame));
		}
		if (Options.SetComponentTransform)
		{
			Options.SetComponentTransform(*Component, CurrentFrame);
		}
		if (Options.bPreUpdateEveryFrame || Options.GetComponentTransform || Options.SetComponentTransform)
		{
			Proxy->PreUpdate(Anim, FrameDt);
			Node.PreUpdate(Anim);
		}
		FMemMark MemMark(FMemStack::Get());
		FComponentSpacePoseContext Context(Proxy.Get());
		if (Options.bUseLocalPose || Options.SetInputPose)
		{
			FCompactPose LocalPose;
			LocalPose.SetBoneContainer(&Proxy->GetRequiredBones());
			LocalPose.ResetToRefPose();
			if (Options.SetInputPose)
			{
				Options.SetInputPose(LocalPose, CurrentFrame);
			}
			Context.Pose.InitPose(LocalPose);
			Node.UpdateInternal(FAnimationUpdateContext(Proxy.Get(), FrameDt));
			OutTransforms.Reset();
			Node.EvaluateSkeletalControl_AnyThread(Context, OutTransforms);
		}
		else
		{
			Evaluate(Context);
		}
		return OutTransforms;
	}
};

}

#endif
