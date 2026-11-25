// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsNodeFixture.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"

namespace KawaiiPhysicsHairNodeFixture
{
	constexpr int32 GChains = 150;
	constexpr int32 GBonesPerChain = 4;
	constexpr int32 GSimBones = GChains * GBonesPerChain;
	constexpr int32 GTargetFramerate = 120;

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

	struct FFixture : KawaiiPhysicsNodeFixture::FFixture
	{
		static KawaiiPhysicsNodeFixture::FOptions MakeOptions(const FCase& Case)
		{
			KawaiiPhysicsNodeFixture::FOptions Options;
			Options.FrameDt = 1.0f / static_cast<float>(Case.Fps);
			Options.Bones = KawaiiPhysicsNodeFixture::BuildHair(GChains, GBonesPerChain);
			Options.ConfigureNode = [Case](FAnimNode_KawaiiPhysics& InNode)
			{
				InNode.RootBone.BoneName = TEXT("hair_root");
				InNode.DummyBoneLength = 0.0f;
				InNode.WarmUpFrames = 0;
				InNode.TargetFramerate = GTargetFramerate;
				InNode.bAllowWorldCollision = false;
				InNode.bUseSimpleWorldCollision = false;
				InNode.Gravity = FVector(0.0, 0.0, -980.0);

				for (int32 ForceIndex = 0; ForceIndex < Case.NumForces; ++ForceIndex)
				{
					FInstancedStruct& Item =
						InNode.ExternalForces.Add_GetRef(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>());
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
								Wind.ApplyBoneFilter.AddDefaulted_GetRef().BoneName = KawaiiPhysicsNodeFixture::GetHairBoneName(Chain, Link);
							}
						}
					}
					else if (Case.Filter == EFilterLayout::SingleBone && ForceIndex == 0)
					{
						Wind.ApplyBoneFilter.AddDefaulted_GetRef().BoneName = KawaiiPhysicsNodeFixture::GetHairBoneName(0, 0);
					}
				}

			};
			return Options;
		}

		explicit FFixture(const FCase& Case) : KawaiiPhysicsNodeFixture::FFixture(MakeOptions(Case)) {}

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
