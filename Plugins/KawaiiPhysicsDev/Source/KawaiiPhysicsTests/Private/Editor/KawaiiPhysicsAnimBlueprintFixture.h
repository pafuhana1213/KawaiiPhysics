// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "../Runtime/KawaiiPhysicsNodeFixture.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "KawaiiPhysicsLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/Package.h"

namespace KawaiiPhysicsAnimBlueprintFixture
{

struct FOptions
{
	TArray<KawaiiPhysicsNodeFixture::FBoneDefinition> Bones;
	int32 NodeCount = 1;
	float FrameDt = 1.0f / 60.0f;
	TFunction<void(FAnimNode_KawaiiPhysics&, int32)> ConfigureNode;
	TFunction<FTransform(int32)> GetComponentTransform;
};

struct FFixture
{
	TStrongObjectPtr<UPackage> Package;
	TStrongObjectPtr<USkeleton> Skeleton;
	TStrongObjectPtr<USkeletalMesh> Mesh;
	TStrongObjectPtr<UAnimBlueprint> AnimBlueprint;
	UWorld* World = nullptr;
	USkeletalMeshComponent* Component = nullptr;
	FOptions Options;
	TArray<FString> Messages;
	int32 CompileErrorCount = INDEX_NONE;
	int32 FrameIndex = 0;
	bool bReady = false;

	explicit FFixture(const FOptions& InOptions) : Options(InOptions)
	{
		InitFixture();
	}

	~FFixture()
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		// 保存対象にはせず、StrongObjectPtr の解放後は GC に任せる。
		if (AnimBlueprint.IsValid())
		{
			AnimBlueprint->ClearFlags(RF_Standalone);
		}
		if (Mesh.IsValid())
		{
			Mesh->ClearFlags(RF_Standalone);
		}
		if (Skeleton.IsValid())
		{
			Skeleton->ClearFlags(RF_Standalone);
		}
		if (Package.IsValid())
		{
			Package->ClearFlags(RF_Standalone);
			Package->SetDirtyFlag(false);
		}
	}

	bool IsReady() const { return bReady; }
	USkeletalMeshComponent* GetComponent() const { return Component; }
	UAnimInstance* GetAnimInstance() const { return Component ? Component->GetAnimInstance() : nullptr; }
	const TArray<FString>& GetMessages() const { return Messages; }

	TArray<FKawaiiPhysicsReference> GetNodes() const
	{
		TArray<FKawaiiPhysicsReference> Nodes;
		if (Component)
		{
			UKawaiiPhysicsLibrary::CollectKawaiiPhysicsNodes(Nodes, Component, FGameplayTagContainer(), false);
		}
		return Nodes;
	}

	bool GetBoneComponentTransform(const FName BoneName, FTransform& Transform) const
	{
		if (!Component)
		{
			return false;
		}
		const int32 BoneIndex = Component->GetBoneIndex(BoneName);
		const TArray<FTransform>& Transforms = Component->GetComponentSpaceTransforms();
		if (!Transforms.IsValidIndex(BoneIndex))
		{
			return false;
		}
		Transform = Transforms[BoneIndex];
		return true;
	}

	bool StepFrame()
	{
		if (!bReady)
		{
			return false;
		}
		if (Options.GetComponentTransform)
		{
			Component->SetWorldTransform(Options.GetComponentTransform(FrameIndex));
		}
		FMemMark MemMark(FMemStack::Get());
		Component->TickAnimation(Options.FrameDt, false);
		// TickFunction を渡さなければ評価タスクを投げず、戻る前にポーズが確定する。
		Component->RefreshBoneTransforms(nullptr);
		++FrameIndex;
		return true;
	}

private:
	void InitFixture()
	{
		if (Options.Bones.IsEmpty() || Options.NodeCount < 1)
		{
			Messages.Add(TEXT("Bones and at least one node are required"));
			return;
		}
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		Package.Reset(CreatePackage(*FString::Printf(TEXT("/Temp/KawaiiPhysicsAnimBlueprintFixture_%s"), *Suffix)));
		Package->SetFlags(RF_Transient);
		Skeleton.Reset(NewObject<USkeleton>(Package.Get(), NAME_None, RF_Transient));
		const TArray<FBoneIndexType> RequiredIndices = KawaiiPhysicsNodeFixture::BuildSkeleton(*Skeleton, Options.Bones);
		BuildMesh(RequiredIndices);

		AnimBlueprint.Reset(Cast<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UAnimInstance::StaticClass(), Package.Get(), FName(*FString::Printf(TEXT("ABP_%s"), *Suffix)),
			BPTYPE_Normal, UAnimBlueprint::StaticClass(), UAnimBlueprintGeneratedClass::StaticClass())));
		if (!AnimBlueprint.IsValid())
		{
			Messages.Add(TEXT("CreateBlueprint failed"));
			return;
		}
		AnimBlueprint->SetFlags(RF_Transient);
		AnimBlueprint->TargetSkeleton = Skeleton.Get();
		AnimBlueprint->bUseMultiThreadedAnimationUpdate = false;
		for (int32 NodeIndex = 0; NodeIndex < Options.NodeCount; ++NodeIndex)
		{
			FKawaiiPhysicsNodePlacementRequest Request;
			Request.RootBoneName = Options.Bones[0].Name;
			Request.bAutoConnect = true;
			Request.bAutoPosition = false;
			const TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
				UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(AnimBlueprint.Get(), {Request});
			if (Handles.Num() != 1 || !Handles[0].IsValid())
			{
				Messages.Add(TEXT("AddKawaiiPhysicsNodes failed"));
				return;
			}
			if (Options.ConfigureNode)
			{
				Options.ConfigureNode(Handles[0].Node->Node, NodeIndex);
			}
		}
		FString InputError;
		if (UKawaiiPhysicsEditorLibrary::SetAnimGraphInputPose(AnimBlueprint.Get(), InputError) < 0)
		{
			Messages.Add(InputError);
			return;
		}
		CompileErrorCount = UKawaiiPhysicsEditorLibrary::CompileAnimBlueprintWithMessages(AnimBlueprint.Get(), Messages);
		if (CompileErrorCount != 0 || !AnimBlueprint->GeneratedClass)
		{
			return;
		}

		World = KawaiiPhysicsNodeFixture::MakeWorld();
		if (!World)
		{
			Messages.Add(TEXT("CreateWorld failed"));
			return;
		}
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)
		{
			Messages.Add(TEXT("SpawnActor failed"));
			return;
		}
		Component = NewObject<USkeletalMeshComponent>(Actor);
		Actor->AddInstanceComponent(Component);
		Actor->SetRootComponent(Component);
		// 頂点を持たない評価専用 Mesh を、通常のエディタでも描画経路へ渡さない。
		Component->SetVisibility(false);
		Component->SetCastShadow(false);
		Component->bAffectDynamicIndirectLighting = false;
		Component->bEnableUpdateRateOptimizations = false;
		Component->RegisterComponent();
		Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Component->bUseRefPoseOnInitAnim = true;
		Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Component->SetAnimInstanceClass(AnimBlueprint->GeneratedClass);
		Component->SetSkeletalMeshAsset(Mesh.Get());
		Component->InitAnim(true);
		bReady = GetAnimInstance() != nullptr;
		if (bReady)
		{
			GetAnimInstance()->bUseMultiThreadedAnimationUpdate = false;
		}
		else
		{
			Messages.Add(TEXT("InitAnim did not create an AnimInstance"));
		}
	}

	void BuildMesh(const TArray<FBoneIndexType>& RequiredIndices)
	{
		Mesh.Reset(NewObject<USkeletalMesh>(Package.Get(), NAME_None, RF_Transient));
		Mesh->SetSkeleton(Skeleton.Get());
		Mesh->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
		Mesh->CalculateInvRefMatrices();
		Mesh->SetImportedBounds(FBoxSphereBounds(FVector::ZeroVector, FVector(100.0), 100.0));
		Mesh->AddLODInfo();
		FSkeletalMeshLODModel* ImportedLOD = new FSkeletalMeshLODModel();
		ImportedLOD->NumTexCoords = 1;
		ImportedLOD->RequiredBones = RequiredIndices;
		ImportedLOD->ActiveBoneIndices = RequiredIndices;
		Mesh->GetImportedModel()->LODModels.Add(ImportedLOD);
		// RequiredBones は描画データから収集されるため、nullrhi でも LOD を用意する。
		Mesh->AllocateResourceForRendering();
		FSkeletalMeshLODRenderData* RenderLOD = new FSkeletalMeshLODRenderData();
		RenderLOD->RequiredBones = RequiredIndices;
		RenderLOD->ActiveBoneIndices = RequiredIndices;
		Mesh->GetResourceForRendering()->LODRenderData.Add(RenderLOD);
	}
};

}

#endif
