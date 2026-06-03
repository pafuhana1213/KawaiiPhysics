// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysics.h"

#include "AnimationRuntime.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Animation/AnimInstanceProxy.h"
#include "Curves/CurveFloat.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsSettings.h"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
#include "Animation/AnimInstance.h"
#endif

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

#include "KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsInternal.h"

void FAnimNode_KawaiiPhysics::InitSyncBones(FComponentSpacePoseContext& Output)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitSyncBone);

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	for (FKawaiiPhysicsSyncBone& SyncBone : SyncBones)
	{
		InitSyncBone(Output, BoneContainer, SyncBone);
	}
}

void FAnimNode_KawaiiPhysics::InitSyncBone(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                           FKawaiiPhysicsSyncBone& SyncBone)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitSyncBone);

	SyncBone.Bone.Initialize(BoneContainer);
	if (!SyncBone.Bone.IsValidToEvaluate(BoneContainer))
	{
		SyncBone.InitialPoseLocation = FVector::ZeroVector;
		return;
	}
	SyncBone.InitialPoseLocation =
			FAnimationRuntime::GetComponentSpaceTransformRefPose(BoneContainer.GetReferenceSkeleton(),
			                                                     SyncBone.Bone.BoneIndex).GetLocation();

	// cleanup
	SyncBone.TargetRoots.RemoveAll([&](const FKawaiiPhysicsSyncTarget& Target)
	{
		return !Target.IsValid(BoneContainer);
	});

	for (auto& TargetRoot : SyncBone.TargetRoots)
	{
		TargetRoot.ChildTargets.Empty();

		TargetRoot.ModifyBoneIndex = ModifyBones.IndexOfByPredicate(
			[&](const FKawaiiPhysicsModifyBone& ModifyBone) { return ModifyBone.BoneRef == TargetRoot.Bone; });
		if (TargetRoot.ModifyBoneIndex == INDEX_NONE || !TargetRoot.bIncludeChildBones)
		{
			continue;
		}

		// For Calculate LengthRateFromSyncTargetRoot
		const float StartLength = ModifyBones[TargetRoot.ModifyBoneIndex].LengthFromRoot;
		float MaxLength = StartLength;

		// Collect Child Bones
		TArray<int32> IndicesToProcess = ModifyBones[TargetRoot.ModifyBoneIndex].ChildIndices;
		while (!IndicesToProcess.IsEmpty())
		{
			const int32 CurrentIndex = IndicesToProcess.Pop();
			if (!ModifyBones.IsValidIndex(CurrentIndex))
			{
				continue;
			}

			TargetRoot.ChildTargets.AddUnique({CurrentIndex});

#if WITH_EDITORONLY_DATA
			TargetRoot.ChildTargets.Last().PreviewBone = ModifyBones[CurrentIndex].BoneRef;
#endif

			IndicesToProcess.Append(ModifyBones[CurrentIndex].ChildIndices);
			MaxLength = FMath::Max(MaxLength, ModifyBones[CurrentIndex].LengthFromRoot);
		}

		// Calculate LengthRateFromSyncTargetRoot
		const float LengthRange = MaxLength - StartLength;
		TargetRoot.LengthRateFromSyncTargetRoot = 0.0f;
		for (auto& Target : TargetRoot.ChildTargets)
		{
			if (LengthRange > KINDA_SMALL_NUMBER)
			{
				Target.LengthRateFromSyncTargetRoot =
					(ModifyBones[Target.ModifyBoneIndex].LengthFromRoot - StartLength) / LengthRange;
			}
			else
			{
				Target.LengthRateFromSyncTargetRoot = 0.0f;
			}
		}

		// Update Alpha by Length Rate & Curve
		if (const FRichCurve* ScaleCurve = TargetRoot.ScaleCurveByBoneLengthRate.GetRichCurveConst();
			ScaleCurve && !ScaleCurve->IsEmpty())
		{
			TargetRoot.UpdateScaleByLengthRate(ScaleCurve);
			for (auto& Target : TargetRoot.ChildTargets)
			{
				Target.UpdateScaleByLengthRate(ScaleCurve);
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::ApplySyncBones(FComponentSpacePoseContext& Output,
                                             const FBoneContainer& BoneContainer)
{
	if (SyncBones.Num() == 0)
	{
		return;
	}

	for (auto& SyncBone : SyncBones)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ApplySyncBone);

		if (!SyncBone.Bone.IsValidToEvaluate(BoneContainer))
		{
			continue;
		}

		// Calculate Delta Movement in Component Space
		const FCompactPoseBoneIndex SyncBoneIndex = SyncBone.Bone.GetCompactPoseIndex(BoneContainer);
		FVector DeltaMovement = Output.Pose.GetComponentSpaceTransform(SyncBoneIndex).GetLocation() - SyncBone.
			InitialPoseLocation;

#if WITH_EDITORONLY_DATA
		SyncBone.DeltaDistance = DeltaMovement;
#endif

		// Apply Curve
		if (const FRichCurve* ScaleCurve = SyncBone.ScaleCurveByDeltaDistance.GetRichCurveConst();
			ScaleCurve && !ScaleCurve->IsEmpty())
		{
			DeltaMovement *= ScaleCurve->Eval(DeltaMovement.Length());
		}

		// Apply Global Alpha
		DeltaMovement *= SyncBone.GlobalScale;

#if WITH_EDITORONLY_DATA
		SyncBone.ScaledDeltaDistance = DeltaMovement;
#endif

		// Filter direction once per SyncBone in Component Space
		auto CheckDirection = [](const float Val, const ESyncBoneDirection Dir)
		{
			return (Dir == ESyncBoneDirection::Both) ||
				(Dir == ESyncBoneDirection::Positive && Val > 0.0) ||
				(Dir == ESyncBoneDirection::Negative && Val < 0.0);
		};

		FVector FilteredDeltaMovement(
			CheckDirection(DeltaMovement.X, SyncBone.ApplyDirectionX) ? DeltaMovement.X : 0.0,
			CheckDirection(DeltaMovement.Y, SyncBone.ApplyDirectionY) ? DeltaMovement.Y : 0.0,
			CheckDirection(DeltaMovement.Z, SyncBone.ApplyDirectionZ) ? DeltaMovement.Z : 0.0
		);

		if (FilteredDeltaMovement.IsNearlyZero())
		{
			continue;
		}

		// Convert to Simulation Space
		FilteredDeltaMovement = ConvertSimulationSpaceVector(Output,
		                                                     EKawaiiPhysicsSimulationSpace::ComponentSpace,
		                                                     SimulationSpace, FilteredDeltaMovement);

		// Cache SyncBone location in Simulation Space for distance attenuation
		const FVector SyncBoneLocationInSimulationSpace = ConvertSimulationSpaceLocation(
			Output,
			EKawaiiPhysicsSimulationSpace::ComponentSpace,
			SimulationSpace,
			Output.Pose.GetComponentSpaceTransform(SyncBoneIndex).GetLocation()
		);

		// Helper: compute attenuation alpha for a distance
		auto CalcAttenuationAlpha = [&](const float Distance) -> float
		{
			if (!SyncBone.bEnableDistanceAttenuation)
			{
				return 1.0f;
			}

			const float Inner = SyncBone.AttenuationInnerRadius;
			const float Outer = SyncBone.AttenuationOuterRadius;
			const float MaxAtten = SyncBone.MaxAttenuationRate;

			// Safety: if outer <= inner, treat as step function at inner
			const float EffectiveOuter = FMath::Max(Outer, Inner);

			float AttenAmount;
			if (Distance <= Inner)
			{
				AttenAmount = 0.0f;
			}
			else if (Distance >= EffectiveOuter)
			{
				AttenAmount = MaxAtten;
			}
			else
			{
				const float Denom = EffectiveOuter - Inner;
				const float T = (Denom > KINDA_SMALL_NUMBER) ? ((Distance - Inner) / Denom) : 1.0f;
				AttenAmount = T * MaxAtten;
			}

			// Convert attenuation amount to alpha multiplier
			return FMath::Max(0.0f, 1.0f - AttenAmount);
		};

		// Apply to Targets
		for (auto& TargetRoot : SyncBone.TargetRoots)
		{
			// Update Alpha by Length Rate & Curve
			// TODO : Need flag to optimize for skip updating Scale after InitSyncBone
			if (const FRichCurve* ScaleCurve = TargetRoot.ScaleCurveByBoneLengthRate.GetRichCurveConst();
				ScaleCurve && !ScaleCurve->IsEmpty())
			{
				TargetRoot.UpdateScaleByLengthRate(ScaleCurve);
				for (auto& Target : TargetRoot.ChildTargets)
				{
					Target.UpdateScaleByLengthRate(ScaleCurve);
				}
			}

			// Root target
			{
				const int32 ModifyBoneIndex = TargetRoot.ModifyBoneIndex;
				if (ModifyBones.IsValidIndex(ModifyBoneIndex))
				{
					const float Dist = FVector::Dist(SyncBoneLocationInSimulationSpace,
					                                 ModifyBones[ModifyBoneIndex].Location);
					const float AlphaMul = CalcAttenuationAlpha(Dist);
					TargetRoot.Apply(ModifyBones, FilteredDeltaMovement * AlphaMul);
				}
				else
				{
					TargetRoot.Apply(ModifyBones, FilteredDeltaMovement);
				}
			}

			// Child targets
			for (auto& Target : TargetRoot.ChildTargets)
			{
				const int32 ModifyBoneIndex = Target.ModifyBoneIndex;
				if (ModifyBones.IsValidIndex(ModifyBoneIndex))
				{
					const float Dist = FVector::Dist(SyncBoneLocationInSimulationSpace,
					                                 ModifyBones[ModifyBoneIndex].Location);
					const float AlphaMul = CalcAttenuationAlpha(Dist);
					Target.Apply(ModifyBones, FilteredDeltaMovement * AlphaMul);
				}
				else
				{
					Target.Apply(ModifyBones, FilteredDeltaMovement);
				}
			}
		}
	}
}
