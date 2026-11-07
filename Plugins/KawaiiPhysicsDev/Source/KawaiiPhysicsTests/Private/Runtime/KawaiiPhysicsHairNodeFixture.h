// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimNode_KawaiiPhysics.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

/**
 * 髪の毛束を模した骨格（根元 1 本 + 4 本 × 150 チェーン）を、本番と同じ経路
 * （UpdateInternal → EvaluateSkeletalControl_AnyThread）で評価するテスト用ノード。
 * 外力のボーンフィルタの性能計測と、本番経路でフィルタの判定表が使われることの確認で共有する。
 * master にある公開 API だけを使うので、変更前のコードに対しても同じ計測ができる。
 */
namespace KawaiiPhysicsHairNodeFixture
{
	constexpr int32 GChains = 150;
	constexpr int32 GBonesPerChain = 4;
	constexpr int32 GSimBones = GChains * GBonesPerChain;
	constexpr int32 GTargetFramerate = 120;

	inline FName HairBoneName(const int32 Chain, const int32 Link)
	{
		return FName(*FString::Printf(TEXT("hair_%03d_%d"), Chain, Link));
	}

	enum class EFilterLayout : uint8
	{
		/** フィルタ無し */
		None,
		/** チェーンを外力へ順番に振り分け、チェーン内の全ボーンを登録する（Issue #297 の構成） */
		RoundRobinChains,
		/** 1 本目の外力だけ、先頭チェーンの根元 1 ボーンを登録する（小さいフィルタ） */
		SingleBone,
	};

	struct FCase
	{
		FString Name;
		int32 Fps = 60;
		int32 NumForces = 0;
		EFilterLayout Filter = EFilterLayout::None;
	};

	struct FProxy : FAnimInstanceProxy
	{
		using FAnimInstanceProxy::FAnimInstanceProxy;
		using FAnimInstanceProxy::PreUpdate;
	};

	struct FFixture
	{
		TStrongObjectPtr<USkeleton> Skeleton{NewObject<USkeleton>()};
		UWorld* World = nullptr;
		UAnimInstance* Anim = nullptr;
		TUniquePtr<FProxy> Proxy;
		FAnimNode_KawaiiPhysics Node;
		TArray<FBoneTransform> OutTransforms;
		float FrameDt = 1.0f / 60.0f;

		explicit FFixture(const FCase& Case)
		{
			FrameDt = 1.0f / static_cast<float>(Case.Fps);
			TArray<FBoneIndexType> RequiredIndices;
			{
				FReferenceSkeletonModifier Modifier(Skeleton.Get());
				Modifier.Add(FMeshBoneInfo(TEXT("hair_root"), TEXT("hair_root"), INDEX_NONE), FTransform::Identity);
				RequiredIndices.Add(0);
				for (int32 Chain = 0; Chain < GChains; ++Chain)
				{
					int32 Parent = 0;
					for (int32 Link = 0; Link < GBonesPerChain; ++Link)
					{
						const FName Name = HairBoneName(Chain, Link);
						const double Angle = 2.0 * PI * Chain / GChains;
						const FVector Offset = Link == 0
							? FVector(FMath::Cos(Angle) * 8.0, FMath::Sin(Angle) * 8.0, 0.0)
							: FVector(0.0, 0.0, -4.0);
						const int32 Index = RequiredIndices.Num();
						Modifier.Add(FMeshBoneInfo(Name, Name.ToString(), Parent), FTransform(Offset));
						RequiredIndices.Add(static_cast<FBoneIndexType>(Index));
						Parent = Index;
					}
				}
			}

			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &Values);
			AActor* Actor = World->SpawnActor<AActor>();
			USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Actor);
			Actor->AddInstanceComponent(Component);
			Actor->SetRootComponent(Component);
			Anim = NewObject<UAnimInstance>(Component);
			Proxy = MakeUnique<FProxy>(Anim);
			Proxy->PreUpdate(Anim, FrameDt);
			Proxy->GetRequiredBones().InitializeTo(RequiredIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);

			Node.RootBone.BoneName = TEXT("hair_root");
			Node.DummyBoneLength = 0.0f;
			Node.WarmUpFrames = 0;
			Node.TargetFramerate = GTargetFramerate;
			Node.bAllowWorldCollision = false;
			Node.bUseSimpleWorldCollision = false;
			Node.Gravity = FVector(0.0, 0.0, -980.0);

			for (int32 ForceIndex = 0; ForceIndex < Case.NumForces; ++ForceIndex)
			{
				FInstancedStruct& Item =
					Node.ExternalForces.Add_GetRef(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>());
				FKawaiiPhysics_ExternalForce_ProceduralWind& Wind =
					Item.GetMutable<FKawaiiPhysics_ExternalForce_ProceduralWind>();
				Wind.WindSource = EKawaiiPhysicsProceduralWindSource::Local;
				Wind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
				Wind.WindDirection = FVector(0.0f, 1.0f, 0.0f);
				Wind.ConstantForce = 10.0f;
				Wind.SwayForce = 2.0f;
				Wind.SwayPeriod = 0.5f;
				Wind.RandomForce = 1.0f;
				Wind.RandomForcePeriod = 0.4f;
				Wind.RipplePhaseOffset = 360.0f / Case.NumForces * ForceIndex;
				if (Case.Filter == EFilterLayout::RoundRobinChains)
				{
					for (int32 Chain = ForceIndex; Chain < GChains; Chain += Case.NumForces)
					{
						for (int32 Link = 0; Link < GBonesPerChain; ++Link)
						{
							Wind.ApplyBoneFilter.AddDefaulted_GetRef().BoneName = HairBoneName(Chain, Link);
						}
					}
				}
				else if (Case.Filter == EFilterLayout::SingleBone && ForceIndex == 0)
				{
					Wind.ApplyBoneFilter.AddDefaulted_GetRef().BoneName = HairBoneName(0, 0);
				}
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
		}

		void Evaluate(FComponentSpacePoseContext& Context)
		{
			Context.Pose.InitPose(&Proxy->GetRequiredBones());
			Node.UpdateInternal(FAnimationUpdateContext(Proxy.Get(), FrameDt));
			OutTransforms.Reset();
			Node.EvaluateSkeletalControl_AnyThread(Context, OutTransforms);
		}

		bool OutputsAllBonesFinite() const
		{
			if (OutTransforms.Num() != GSimBones + 1)
			{
				return false;
			}
			for (const FBoneTransform& Bone : OutTransforms)
			{
				if (Bone.Transform.ContainsNaN())
				{
					return false;
				}
			}
			return true;
		}
	};
}

#endif
