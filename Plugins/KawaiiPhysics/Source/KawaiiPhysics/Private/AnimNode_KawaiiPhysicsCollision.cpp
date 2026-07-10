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

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Animation/AnimInstance.h"
#endif

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

#include "KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsInternal.h"

// トレースはゲームスレッド上で実行されないため、TraceTag はデバッグトレースを描画しない
FKawaiiWorldCollisionQuerySettings::FKawaiiWorldCollisionQuerySettings()
	: Params(SCENE_QUERY_STAT(KawaiiCollision))
{
}

void FAnimNode_KawaiiPhysics::ApplyLimitsDataAsset(const FBoneContainer& RequiredBones)
{
	auto Initialize = [&RequiredBones](auto& Targets)
	{
		for (auto& Target : Targets)
		{
			Target.DrivingBone.Initialize(RequiredBones);
		}
	};
	auto RemoveAllSourceDataAssets = [](auto& Targets)
	{
		Targets.RemoveAll([](const FCollisionLimitBase& Limit)
		{
			return Limit.SourceType == ECollisionSourceType::DataAsset;
		});
	};

	RemoveAllSourceDataAssets(SphericalLimitsData);
	RemoveAllSourceDataAssets(CapsuleLimitsData);
	RemoveAllSourceDataAssets(BoxLimitsData);
	RemoveAllSourceDataAssets(PlanarLimitsData);

	if (LimitsDataAsset)
	{
		SphericalLimitsData.Append(LimitsDataAsset->SphericalLimits);
		CapsuleLimitsData.Append(LimitsDataAsset->CapsuleLimits);
		BoxLimitsData.Append(LimitsDataAsset->BoxLimits);
		PlanarLimitsData.Append(LimitsDataAsset->PlanarLimits);

		Initialize(SphericalLimitsData);
		Initialize(CapsuleLimitsData);
		Initialize(BoxLimitsData);
		Initialize(PlanarLimitsData);
	}
}

void FAnimNode_KawaiiPhysics::ApplyPhysicsAsset(const FBoneContainer& RequiredBones)
{
	auto Initialize = [&RequiredBones](auto& Targets)
	{
		for (auto& Target : Targets)
		{
			Target.DrivingBone.Initialize(RequiredBones);
		}
	};
	auto RemoveAllSourcePhysicsAssets = [](auto& Targets)
	{
		Targets.RemoveAll([](const FCollisionLimitBase& Limit)
		{
			return Limit.SourceType == ECollisionSourceType::PhysicsAsset;
		});
	};

	RemoveAllSourcePhysicsAssets(SphericalLimitsData);
	RemoveAllSourcePhysicsAssets(CapsuleLimitsData);
	RemoveAllSourcePhysicsAssets(BoxLimitsData);

	if (PhysicsAssetForLimits)
	{
		for (const auto& BodySetup : PhysicsAssetForLimits->SkeletalBodySetups)
		{
			FBoneReference DrivingBone = BodySetup->BoneName;
			DrivingBone.Initialize(RequiredBones);
			if (!DrivingBone.IsValidToEvaluate(RequiredBones))
			{
				continue;
			}

			const FKAggregateGeom& AggGeom = BodySetup->AggGeom;
			for (const auto& SphereElem : AggGeom.SphereElems)
			{
				FSphericalLimit NewLimit;
				NewLimit.DrivingBone = DrivingBone;
				NewLimit.OffsetLocation = SphereElem.Center;
				NewLimit.Radius = SphereElem.Radius;
				NewLimit.SourceType = ECollisionSourceType::PhysicsAsset;
				SphericalLimitsData.Add(NewLimit);
			}
			for (const auto& CapsuleElem : AggGeom.SphylElems)
			{
				FCapsuleLimit NewLimit;
				NewLimit.DrivingBone = DrivingBone;
				NewLimit.OffsetLocation = CapsuleElem.Center;
				NewLimit.OffsetRotation = CapsuleElem.Rotation;
				NewLimit.Length = CapsuleElem.Length;
				NewLimit.Radius = CapsuleElem.Radius;
				NewLimit.SourceType = ECollisionSourceType::PhysicsAsset;
				CapsuleLimitsData.Add(NewLimit);
			}
			for (const auto& BoxElem : AggGeom.BoxElems)
			{
				FBoxLimit NewLimit;
				NewLimit.DrivingBone = DrivingBone;
				NewLimit.OffsetLocation = BoxElem.Center;
				NewLimit.OffsetRotation = BoxElem.Rotation;
				NewLimit.Extent = FVector(BoxElem.X, BoxElem.Y, BoxElem.Z) / 2.0f;
				NewLimit.SourceType = ECollisionSourceType::PhysicsAsset;
				BoxLimitsData.Add(NewLimit);
			}
		}

		Initialize(SphericalLimitsData);
		Initialize(CapsuleLimitsData);
		Initialize(BoxLimitsData);
	}
}

void FAnimNode_KawaiiPhysics::ApplyBoneConstraintDataAsset(const FBoneContainer& RequiredBones)
{
	BoneConstraintsData.Empty();
	if (BoneConstraintsDataAsset)
	{
		BoneConstraintsData = BoneConstraintsDataAsset->GenerateBoneConstraints();
		for (auto& BoneConstraint : BoneConstraintsData)
		{
			BoneConstraint.InitializeBone(RequiredBones);
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateSphericalLimits(TArray<FSphericalLimit>& Limits, FComponentSpacePoseContext& Output,
                                                    const FBoneContainer& BoneContainer,
                                                    const FTransform& ComponentTransform) const
{
	for (auto& Sphere : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateSphericalLimit);

		if (Sphere.DrivingBone.IsValidToEvaluate(BoneContainer))
		{
			const FCompactPoseBoneIndex CompactPoseIndex = Sphere.DrivingBone.GetCompactPoseIndex(BoneContainer);
			FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);

			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);
			BoneTransform.SetRotation(Sphere.OffsetRotation.Quaternion() * BoneTransform.GetRotation());
			BoneTransform.AddToTranslation(Sphere.OffsetLocation);

			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);

			BoneTransform =
				ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulationSpace,
				                                BoneTransform);
			Sphere.Location = BoneTransform.GetLocation();
			Sphere.Rotation = BoneTransform.GetRotation();

			Sphere.bEnable = true;
		}
		else
		{
			Sphere.bEnable = false;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateCapsuleLimits(TArray<FCapsuleLimit>& Limits, FComponentSpacePoseContext& Output,
                                                  const FBoneContainer& BoneContainer,
                                                  const FTransform& ComponentTransform) const
{
	for (auto& Capsule : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateCapsuleLimit);

		if (Capsule.DrivingBone.IsValidToEvaluate(BoneContainer))
		{
			const FCompactPoseBoneIndex CompactPoseIndex = Capsule.DrivingBone.GetCompactPoseIndex(BoneContainer);
			FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);

			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);
			BoneTransform.SetRotation(Capsule.OffsetRotation.Quaternion() * BoneTransform.GetRotation());
			BoneTransform.AddToTranslation(Capsule.OffsetLocation);

			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);

			BoneTransform =
				ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulationSpace,
				                                BoneTransform);
			Capsule.Location = BoneTransform.GetLocation();
			Capsule.Rotation = BoneTransform.GetRotation();

			Capsule.bEnable = true;
		}
		else
		{
			Capsule.bEnable = false;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateBoxLimits(TArray<FBoxLimit>& Limits, FComponentSpacePoseContext& Output,
                                              const FBoneContainer& BoneContainer,
                                              const FTransform& ComponentTransform) const
{
	for (auto& Box : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateBoxLimit);

		if (Box.DrivingBone.IsValidToEvaluate(BoneContainer))
		{
			const FCompactPoseBoneIndex CompactPoseIndex = Box.DrivingBone.GetCompactPoseIndex(BoneContainer);
			FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);

			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);
			BoneTransform.SetRotation(Box.OffsetRotation.Quaternion() * BoneTransform.GetRotation());
			BoneTransform.AddToTranslation(Box.OffsetLocation);

			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);

			BoneTransform =
				//GetSimSpaceTransformFromComponentSpace(SimulationSpace, Output, BoneTransform);
				ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulationSpace,
				                                BoneTransform);
			Box.Location = BoneTransform.GetLocation();
			Box.Rotation = BoneTransform.GetRotation();

			Box.bEnable = true;
		}
		else
		{
			Box.bEnable = false;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdatePlanerLimits(TArray<FPlanarLimit>& Limits, FComponentSpacePoseContext& Output,
                                                 const FBoneContainer& BoneContainer,
                                                 const FTransform& ComponentTransform) const
{
	for (auto& Planar : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdatePlanerLimit);

		if (Planar.DrivingBone.IsValidToEvaluate(BoneContainer))
		{
			const FCompactPoseBoneIndex CompactPoseIndex = Planar.DrivingBone.GetCompactPoseIndex(BoneContainer);
			FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);

			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);
			BoneTransform.SetRotation(Planar.OffsetRotation.Quaternion() * BoneTransform.GetRotation());
			BoneTransform.AddToTranslation(Planar.OffsetLocation);

			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);

			BoneTransform = ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace,
			                                                SimulationSpace, BoneTransform);
			Planar.Location = BoneTransform.GetLocation();
			Planar.Rotation = BoneTransform.GetRotation();
			Planar.Rotation.Normalize();
			Planar.Plane = FPlane(Planar.Location, Planar.Rotation.GetUpVector());

			Planar.bEnable = true;
		}
		else
		{
			// 床用に DrivingBone が空に設定されている場合を考慮
			if (Planar.DrivingBone.BoneName.IsNone())
			{
				FTransform OffsetTransform(Planar.OffsetRotation, Planar.OffsetLocation);
				OffsetTransform = ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace,
				                                                  SimulationSpace, OffsetTransform);

				Planar.Location = OffsetTransform.GetLocation();
				Planar.Rotation = OffsetTransform.GetRotation();
				Planar.Rotation.Normalize();
				Planar.Plane = FPlane(Planar.Location, Planar.Rotation.GetUpVector());
				Planar.bEnable = true;
			}
			else
			{
				Planar.bEnable = false;
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByWorldCollision(
	FComponentSpacePoseContext& Output, FKawaiiPhysicsModifyBone& Bone, const USkeletalMeshComponent* OwningComp,
	const FKawaiiWorldCollisionQuerySettings& WorldCollisionQuerySettings)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_WorldCollision);

	// bridge dummy は ParentIndex<0 だがコリジョン代理として World Collision に参加させる（PrevLocation→Location でスイープ）
	if (!OwningComp || !OwningComp->GetWorld() || (Bone.ParentIndex < 0 && !Bone.bBridgeDummy))
	{
		return;
	}

	// 半径0のスフィアではsweepが無効化され押し戻しが効かないためスキップ
	if (Bone.PhysicsSettings.Radius <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const UWorld* World = OwningComp->GetWorld();

	const FVector TraceStartLocationWS =
		ConvertSimulationSpaceLocation(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace,
		                               Bone.PrevLocation);
	const FVector TraceEndLocationWS =
		ConvertSimulationSpaceLocation(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace,
		                               Bone.Location);

	if (bIgnoreSelfComponent)
	{
		// sphere sweep
		FHitResult Result;
		bool bHit = World->SweepSingleByChannel(
			Result, TraceStartLocationWS, TraceEndLocationWS, FQuat::Identity,
			WorldCollisionQuerySettings.TraceChannel, FCollisionShape::MakeSphere(Bone.PhysicsSettings.Radius),
			WorldCollisionQuerySettings.Params, WorldCollisionQuerySettings.ResponseParams);
		if (bHit)
		{
			if (Result.bStartPenetrating)
			{
				Bone.Location =
					ConvertSimulationSpaceLocation(Output, EKawaiiPhysicsSimulationSpace::WorldSpace, SimulationSpace,
					                               TraceEndLocationWS + Result.Normal * Result.PenetrationDepth);
			}
			else
			{
				Bone.Location =
					ConvertSimulationSpaceLocation(Output, EKawaiiPhysicsSimulationSpace::WorldSpace, SimulationSpace,
					                               Result.Location);
			}
		}
	}
	else
	{
		// sphere sweep（ヒット後に対象ボーンを除外）
		WorldCollisionHitsScratch.Reset();
		bool bHit = World->SweepMultiByChannel(WorldCollisionHitsScratch, TraceStartLocationWS,
		                                       TraceEndLocationWS, FQuat::Identity,
		                                       WorldCollisionQuerySettings.TraceChannel,
		                                       FCollisionShape::MakeSphere(Bone.PhysicsSettings.Radius),
		                                       WorldCollisionQuerySettings.Params,
		                                       WorldCollisionQuerySettings.ResponseParams);
		if (!bHit)
		{
			return;
		}

		bool IsIgnoreHit;
		if (IgnoreBoneNamePrefixCache != IgnoreBoneNamePrefix)
		{
			IgnoreBoneNamePrefixCache = IgnoreBoneNamePrefix;
			IgnoreBoneNamePrefixStrings.Reset(IgnoreBoneNamePrefix.Num());
			for (const FName& BoneNamePrefix : IgnoreBoneNamePrefix)
			{
				if (!BoneNamePrefix.IsNone())
				{
					IgnoreBoneNamePrefixStrings.Add(BoneNamePrefix.ToString());
				}
			}
		}

		for (const auto& Result : WorldCollisionHitsScratch)
		{
			if (!Result.bBlockingHit)
			{
				continue;
			}

			// このヒットを無視すべきか？
			IsIgnoreHit = false;
			if (Result.Component == OwningComp && Result.BoneName != NAME_None)
			{
				IsIgnoreHit = Result.BoneName == Bone.BoneRef.BoneName;
				if (!IsIgnoreHit)
				{
					for (const auto& BoneRef : IgnoreBones)
					{
						if (BoneRef.BoneName == Result.BoneName)
						{
							IsIgnoreHit = true;
							break;
						}
					}
				}
				// プレフィックス未設定（一般的なケース）ではToString自体を回避
				if (!IsIgnoreHit && !IgnoreBoneNamePrefixStrings.IsEmpty())
				{
					const FString ResultBoneNameString = Result.BoneName.ToString();
					for (const FString& BoneNamePrefix : IgnoreBoneNamePrefixStrings)
					{
						if (ResultBoneNameString.StartsWith(BoneNamePrefix))
						{
							IsIgnoreHit = true;
							break;
						}
					}
				}
			}

			// 無視対象でないブロッキングヒットを採用
			if (!IsIgnoreHit)
			{
				if (Result.bStartPenetrating)
				{
					Bone.Location =
						ConvertSimulationSpaceLocation(Output, EKawaiiPhysicsSimulationSpace::WorldSpace,
						                               SimulationSpace,
						                               TraceEndLocationWS + Result.Normal * Result.PenetrationDepth);
				}
				else
				{
					Bone.Location =
						ConvertSimulationSpaceLocation(Output, EKawaiiPhysicsSimulationSpace::WorldSpace,
						                               SimulationSpace,
						                               Result.Location);
				}
				break;
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits)
{
	for (auto& Sphere : Limits)
	{
		if (!Sphere.bEnable || Sphere.Radius <= 0.0f)
		{
			continue;
		}

		if (Sphere.LimitType == ESphericalLimitType::Outer)
		{
			const float LimitDistanceOuter = Sphere.Radius + Bone.PhysicsSettings.Radius;
			const FVector Delta = Bone.Location - Sphere.Location;
			const float DistSq = Delta.SizeSquared();
			if (DistSq > LimitDistanceOuter * LimitDistanceOuter)
			{
				continue;
			}

			const float Dist = FMath::Sqrt(DistSq);
			if (Dist > KINDA_SMALL_NUMBER)
			{
				Bone.Location += (LimitDistanceOuter - Dist) * (Delta / Dist);
			}
		}
		else
		{
			// ボーン半径≥スフィア半径だと内半径(=スフィア半径−ボーン半径)が負になり反対側へ飛ぶ。Max(...,0)で中心にピン留めして回避。
			const float LimitDistanceInner = FMath::Max(Sphere.Radius - Bone.PhysicsSettings.Radius, 0.0f);
			const FVector Delta = Bone.Location - Sphere.Location;
			const float DistSq = Delta.SizeSquared();
			if (DistSq < LimitDistanceInner * LimitDistanceInner)
			{
				continue;
			}

			const float Dist = FMath::Sqrt(DistSq);
			Bone.Location = Dist > KINDA_SMALL_NUMBER
				                ? Sphere.Location + LimitDistanceInner * (Delta / Dist)
				                : Sphere.Location;
		}
	}
}

void FAnimNode_KawaiiPhysics::PrepareCollisionShapeCaches()
{
	auto UpdateEnabledCaches = [](auto& Limits)
	{
		for (auto& Limit : Limits)
		{
			if (Limit.bEnable)
			{
				Limit.UpdateRuntimeCache();
			}
		}
	};

	UpdateEnabledCaches(CapsuleLimits);
	UpdateEnabledCaches(CapsuleLimitsData);
	UpdateEnabledCaches(SharedCapsuleLimits);
	UpdateEnabledCaches(BoxLimits);
	UpdateEnabledCaches(BoxLimitsData);
	UpdateEnabledCaches(SharedBoxLimits);
	UpdateEnabledCaches(PlanarLimits);
	UpdateEnabledCaches(PlanarLimitsData);
	UpdateEnabledCaches(SharedPlanarLimits);
}

void FAnimNode_KawaiiPhysics::AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits)
{
	for (auto& Capsule : Limits)
	{
		if (!Capsule.bEnable || Capsule.Radius <= 0 || Capsule.Length <= 0)
		{
			continue;
		}

		FVector StartPoint = Capsule.CachedStartPoint;
		FVector EndPoint = Capsule.CachedEndPoint;
		const float DistSquared = FMath::PointDistToSegmentSquared(Bone.Location, StartPoint, EndPoint);

		const float LimitDistance = Bone.PhysicsSettings.Radius + Capsule.Radius;
		if (DistSquared < LimitDistance * LimitDistance)
		{
			FVector ClosestPoint = FMath::ClosestPointOnSegment(Bone.Location, StartPoint, EndPoint);
			FVector PushDir = (Bone.Location - ClosestPoint).GetSafeNormal();
			if (PushDir.IsNearlyZero())
			{
				// ボーンがカプセル軸上に乗ると押し出し方向が消えるため軸直交方向を代替に使う
				PushDir = Capsule.CachedFallbackPushDir;
			}
			Bone.Location = ClosestPoint + PushDir * LimitDistance;
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByBoxCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FBoxLimit>& Limits)
{
	for (auto& Box : Limits)
	{
		if (!Box.bEnable)
		{
			continue;
		}

		FTransform BoxTransform = Box.CachedBoxTransform;
		float SphereRadius = Bone.PhysicsSettings.Radius;

		FVector LocalSphereCenter = BoxTransform.InverseTransformPosition(Bone.Location);
		FBox LocalBox = Box.CachedLocalBox;
		if (FMath::SphereAABBIntersection(FSphere(LocalSphereCenter, SphereRadius), LocalBox))
		{
			// Sphere の中心に最も近い Box 上の点を計算
			FVector ClosestPoint = LocalSphereCenter;
			ClosestPoint.X = FMath::Clamp(ClosestPoint.X, LocalBox.Min.X, LocalBox.Max.X);
			ClosestPoint.Y = FMath::Clamp(ClosestPoint.Y, LocalBox.Min.Y, LocalBox.Max.Y);
			ClosestPoint.Z = FMath::Clamp(ClosestPoint.Z, LocalBox.Min.Z, LocalBox.Max.Z);

			FVector PushOutVector = LocalSphereCenter - ClosestPoint;
			float Distance = PushOutVector.Size();

			// ボーンスフィアが Box 内部に完全に埋没している場合は強制的に押し出す。
			if (PushOutVector.IsNearlyZero())
			{
				PushOutVector = LocalSphereCenter;
				Distance = SphereRadius;

				// 中心一致時は半径方向が定まらず GetSafeNormal()==0 で動かなくなるため、最近面（最小貫通軸）を選ぶ。
				if (PushOutVector.IsNearlyZero())
				{
					const FVector Penetration = Box.Extent - LocalSphereCenter.GetAbs();
					if (Penetration.X <= Penetration.Y && Penetration.X <= Penetration.Z)
					{
						PushOutVector = FVector(LocalSphereCenter.X >= 0.0 ? 1.0 : -1.0, 0.0, 0.0);
					}
					else if (Penetration.Y <= Penetration.Z)
					{
						PushOutVector = FVector(0.0, LocalSphereCenter.Y >= 0.0 ? 1.0 : -1.0, 0.0);
					}
					else
					{
						PushOutVector = FVector(0.0, 0.0, LocalSphereCenter.Z >= 0.0 ? 1.0 : -1.0);
					}
				}
			}

			// 押し出し
			if (Distance <= SphereRadius)
			{
				FVector PushOutDirection = PushOutVector.GetSafeNormal();
				FVector NewLocalSphereCenter = ClosestPoint + PushOutDirection * SphereRadius;
				Bone.Location = BoxTransform.TransformPosition(NewLocalSphereCenter);
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits)
{
	for (auto& Planar : Limits)
	{
		if (!Planar.bEnable)
		{
			continue;
		}

		FVector PointOnPlane = FVector::PointPlaneProject(Bone.Location, Planar.Plane);
		const float DistSquared = (Bone.Location - PointOnPlane).SizeSquared();

		FVector IntersectionPoint;
		if (DistSquared < Bone.PhysicsSettings.Radius * Bone.PhysicsSettings.Radius ||
			FMath::SegmentPlaneIntersection(Bone.Location, Bone.PrevLocation, Planar.Plane, IntersectionPoint))
		{
			Bone.Location = PointOnPlane + Planar.CachedNormal * Bone.PhysicsSettings.Radius;
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByAngleLimit(
	FKawaiiPhysicsModifyBone& Bone,
	const FKawaiiPhysicsModifyBone& ParentBone)
{
	if (Bone.PhysicsSettings.LimitAngle == 0.0f)
	{
		return;
	}

	FVector BoneDir = (Bone.Location - ParentBone.Location).GetSafeNormal();
	const FVector PoseDir = (Bone.PoseLocation - ParentBone.PoseLocation).GetSafeNormal();
	const FVector Axis = FVector::CrossProduct(PoseDir, BoneDir);
	const float Angle = FMath::Atan2(Axis.Size(), FVector::DotProduct(PoseDir, BoneDir));
	const float AngleOverLimit = FMath::RadiansToDegrees(Angle) - Bone.PhysicsSettings.LimitAngle;

	if (AngleOverLimit > 0.0f)
	{
		FVector RotationAxis = Axis.GetSafeNormal();
		if (RotationAxis.IsNearlyZero())
		{
			// PoseDirとBoneDirがほぼ反平行だと回転軸が消えるため、親の側方軸を代替に使う
			RotationAxis = ParentBone.PoseRotation.GetAxisX();
		}
		BoneDir = BoneDir.RotateAngleAxis(-AngleOverLimit, RotationAxis);
		Bone.Location = BoneDir * (Bone.Location - ParentBone.Location).Size() + ParentBone.Location;
	}
}

void FAnimNode_KawaiiPhysics::AdjustByPlanarConstraint(FKawaiiPhysicsModifyBone& Bone,
                                                       const FKawaiiPhysicsModifyBone& ParentBone)
{
	if (PlanarConstraint != EPlanarConstraint::None)
	{
		FPlane Plane;
		switch (PlanarConstraint)
		{
		case EPlanarConstraint::X:
			Plane = FPlane(ParentBone.Location, ParentBone.PoseRotation.GetAxisX());
			break;
		case EPlanarConstraint::Y:
			Plane = FPlane(ParentBone.Location, ParentBone.PoseRotation.GetAxisY());
			break;
		case EPlanarConstraint::Z:
			Plane = FPlane(ParentBone.Location, ParentBone.PoseRotation.GetAxisZ());
			break;
		case EPlanarConstraint::None:
			break;
		default: ;
		}
		Bone.Location = FVector::PointPlaneProject(Bone.Location, Plane);
	}
}

static constexpr float XPBDComplianceValues[] =
{
	0.00000000004f, // 0.04 x 10^(-9) (M^2/N) Concrete
	0.00000000016f, // 0.16 x 10^(-9) (M^2/N) Wood
	0.000000001f, // 1.0  x 10^(-8) (M^2/N) Leather
	0.000000002f, // 0.2  x 10^(-7) (M^2/N) Tendon
	0.0000001f, // 1.0  x 10^(-6) (M^2/N) Rubber
	0.00002f, // 0.2  x 10^(-3) (M^2/N) Muscle
	0.0001f, // 1.0  x 10^(-3) (M^2/N) Fat
};

void FAnimNode_KawaiiPhysics::AdjustByBoneConstraints()
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AdjustByBoneConstraint);

	// StepDt は本関数の実行中ずっと同一なので、制約毎の除算をやめて 1 回だけ表を作る。
	// FMath::Max は極小 StepDt で compliance が発散しないためのガード。
	const float StepDt = FMath::Max(GetStepDeltaTime(), KINDA_SMALL_NUMBER);
	float CompliancePerDt2[UE_ARRAY_COUNT(XPBDComplianceValues)];
	for (int32 i = 0; i < UE_ARRAY_COUNT(XPBDComplianceValues); ++i)
	{
		CompliancePerDt2[i] = XPBDComplianceValues[i] / (StepDt * StepDt);
	}

	for (FModifyBoneConstraint& BoneConstraint : MergedBoneConstraints)
	{
		// IsValid()はLength>0のみ確認するため、indexの範囲も明示的に検証（堅牢化）
		if (!BoneConstraint.IsValid() ||
			!ModifyBones.IsValidIndex(BoneConstraint.ModifyBoneIndex1) ||
			!ModifyBones.IsValidIndex(BoneConstraint.ModifyBoneIndex2))
		{
			continue;
		}

		FKawaiiPhysicsModifyBone& ModifyBone1 = ModifyBones[BoneConstraint.ModifyBoneIndex1];
		FKawaiiPhysicsModifyBone& ModifyBone2 = ModifyBones[BoneConstraint.ModifyBoneIndex2];
		EXPBDComplianceType ComplianceType = BoneConstraint.bOverrideCompliance
			                                     ? BoneConstraint.ComplianceType
			                                     : BoneConstraintGlobalComplianceType;

		FVector Delta = ModifyBone2.Location - ModifyBone1.Location;
		float DeltaLength = Delta.Size();
		if (DeltaLength <= 0.0f)
		{
			continue;
		}

		// PBD
		// Delta *= (DeltaLength - BoneConstraint.Length) / DeltaLength * 0.5f;
		// ModifyBone1.Location += Delta * Stiffness;
		// ModifyBone2.Location -= Delta * Stiffness;

		// XBPD
		float Constraint = DeltaLength - BoneConstraint.Length;
		// enum 値の破損や将来の追加に備え、インデックスを配列範囲内へクランプ。
		const int32 ComplianceIndex = FMath::Clamp(static_cast<int32>(ComplianceType), 0,
		                                           static_cast<int32>(UE_ARRAY_COUNT(XPBDComplianceValues)) - 1);
		float Compliance = CompliancePerDt2[ComplianceIndex];
		float DeltaLambda = (Constraint - Compliance * BoneConstraint.Lambda) / (2 + Compliance); // 2 = SumMass
		Delta = (Delta / DeltaLength) * DeltaLambda;

		ModifyBone1.Location += Delta;
		ModifyBone2.Location -= Delta;
		BoneConstraint.Lambda += DeltaLambda;
	}
}

void FAnimNode_KawaiiPhysics::InitBoneConstraints()
{
	MergedBoneConstraints = BoneConstraints;
	MergedBoneConstraints.Append(BoneConstraintsData);

	TArray<FModifyBoneConstraint> DummyBoneConstraint;
	for (FModifyBoneConstraint& Constraint : MergedBoneConstraints)
	{
		Constraint.ModifyBoneIndex1 =
			ModifyBones.IndexOfByPredicate([Constraint](const FKawaiiPhysicsModifyBone& ModifyBone)
			{
				return ModifyBone.BoneRef == Constraint.Bone1;
			});
		if (Constraint.ModifyBoneIndex1 < 0)
		{
			continue;
		}

		Constraint.ModifyBoneIndex2 =
			ModifyBones.IndexOfByPredicate([Constraint](const FKawaiiPhysicsModifyBone& ModifyBone)
			{
				return ModifyBone.BoneRef == Constraint.Bone2;
			});
		if (Constraint.ModifyBoneIndex2 < 0)
		{
			continue;
		}

		Constraint.Length =
			(ModifyBones[Constraint.ModifyBoneIndex1].Location - ModifyBones[Constraint.ModifyBoneIndex2].Location).
			Size();

		// DummyBone の Constraint
		if (bAutoAddChildDummyBoneConstraint)
		{
			// tip dummy constraint（inter-bone dummyを除外）
			const int32 ChildDummyBoneIndex1 = ModifyBones[Constraint.ModifyBoneIndex1].ChildIndices.IndexOfByPredicate(
				[&](int32 Index)
				{
					return Index >= 0 && ModifyBones[Index].bDummy && !ModifyBones[Index].bInterBoneDummy;
				});
			const int32 ChildDummyBoneIndex2 = ModifyBones[Constraint.ModifyBoneIndex2].ChildIndices.IndexOfByPredicate(
				[&](int32 Index)
				{
					return Index >= 0 && ModifyBones[Index].bDummy && !ModifyBones[Index].bInterBoneDummy;
				});

			if (ChildDummyBoneIndex1 >= 0 && ChildDummyBoneIndex2 >= 0)
			{
				FModifyBoneConstraint NewDummyBoneConstraint;
				NewDummyBoneConstraint.ModifyBoneIndex1 = ModifyBones[Constraint.ModifyBoneIndex1].ChildIndices[
					ChildDummyBoneIndex1];
				NewDummyBoneConstraint.ModifyBoneIndex2 = ModifyBones[Constraint.ModifyBoneIndex2].ChildIndices[
					ChildDummyBoneIndex2];
				NewDummyBoneConstraint.Length =
					(ModifyBones[NewDummyBoneConstraint.ModifyBoneIndex1].Location - ModifyBones[NewDummyBoneConstraint.
						ModifyBoneIndex2].Location).
					Size();
				NewDummyBoneConstraint.bIsDummy = true;
				// 細分化の除外設定のみ継承（complianceは既存挙動を変えないため継承しない）
				NewDummyBoneConstraint.bExcludeFromSubdivision = Constraint.bExcludeFromSubdivision;
				DummyBoneConstraint.Add(NewDummyBoneConstraint);
			}

			// inter-bone dummy間の横方向Constraint自動生成
			auto CollectInterBoneDummies = [&](int32 BoneIdx) -> TArray<int32>
			{
				TArray<int32> Dummies;
				for (const int32 ChildIdx : ModifyBones[BoneIdx].ChildIndices)
				{
					if (ChildIdx >= 0 && ModifyBones[ChildIdx].bInterBoneDummy)
					{
						int32 Idx = ChildIdx;
						while (Idx >= 0 && ModifyBones[Idx].bInterBoneDummy)
						{
							Dummies.Add(Idx);
							int32 NextIdx = -1;
							for (const int32 CI : ModifyBones[Idx].ChildIndices)
							{
								if (CI >= 0 && ModifyBones[CI].bInterBoneDummy)
								{
									NextIdx = CI;
									break;
								}
							}
							Idx = NextIdx;
						}

						// 末端区間が分割されている場合、チェーン末尾の tip dummy も横ペア対象に含める（ID_N の後ろに移動し直接子探索では見つからないため）
						if (Dummies.Num() > 0)
						{
							const int32 LastDummy = Dummies.Last();
							for (const int32 CI : ModifyBones[LastDummy].ChildIndices)
							{
								if (CI >= 0 && ModifyBones[CI].bDummy && !ModifyBones[CI].bInterBoneDummy)
								{
									Dummies.Add(CI);
									break;
								}
							}
						}
						break;
					}
				}
				return Dummies;
			};

			const TArray<int32> Dummies1 = CollectInterBoneDummies(Constraint.ModifyBoneIndex1);
			const TArray<int32> Dummies2 = CollectInterBoneDummies(Constraint.ModifyBoneIndex2);
			const int32 PairCount = FMath::Min(Dummies1.Num(), Dummies2.Num());

			for (int32 k = 0; k < PairCount; k++)
			{
				FModifyBoneConstraint NewConstraint;
				NewConstraint.ModifyBoneIndex1 = Dummies1[k];
				NewConstraint.ModifyBoneIndex2 = Dummies2[k];
				NewConstraint.Length =
					(ModifyBones[Dummies1[k]].Location - ModifyBones[Dummies2[k]].Location).Size();
				NewConstraint.bIsDummy = true;
				// 細分化の除外設定のみ継承（complianceは既存挙動を変えないため継承しない）
				NewConstraint.bExcludeFromSubdivision = Constraint.bExcludeFromSubdivision;
				DummyBoneConstraint.Add(NewConstraint);
			}
		}
	}

	MergedBoneConstraints.Append(DummyBoneConstraint);

	// 横方向Constraintに沿って bridge dummy（コリジョンセンサー）を挿入。元Constraintは温存し、反映は毎フレームの直接変位転送（SimulateModifyBones）が行う。
	InsertBridgeDummiesForConstraints();
}

void FAnimNode_KawaiiPhysics::InsertBridgeDummiesForConstraints()
{
	if (BoneConstraintSubdivisionCount <= 0)
	{
		return;
	}


	const FRichCurve* RadiusCurve = RadiusCurveData.GetRichCurveConst();

	// 元のMergedBoneConstraintsは置換せず温存（列間隔の剛性を維持）。bridge dummy を ModifyBones に追加するだけ。
	// MergedBoneConstraints を変更しないため、ModifyBones が拡張されても range-for は安全。
	for (const FModifyBoneConstraint& Constraint : MergedBoneConstraints)
	{
		if (Constraint.bExcludeFromSubdivision)
		{
			continue;
		}
		if (!Constraint.IsBoneReferenceValid() ||
			!ModifyBones.IsValidIndex(Constraint.ModifyBoneIndex1) ||
			!ModifyBones.IsValidIndex(Constraint.ModifyBoneIndex2))
		{
			continue;
		}

		const int32 I1 = Constraint.ModifyBoneIndex1;
		const int32 I2 = Constraint.ModifyBoneIndex2;

		const FVector P1 = ModifyBones[I1].Location;
		const FVector P2 = ModifyBones[I2].Location;
		const float Dist = (P2 - P1).Size();

		// 端点ごとの実効Radiusを各端点のLengthRateでカーブ評価（テーパー対応。グローバル最大半径は使わない）。
		const float LR1 = ModifyBones[I1].LengthRateFromRoot;
		const float LR2 = ModifyBones[I2].LengthRateFromRoot;
		const float R1 = PhysicsSettings.Radius * FMath::Max(RadiusCurve->Eval(LR1, 1.0f), 0.0f);
		const float R2 = PhysicsSettings.Radius * FMath::Max(RadiusCurve->Eval(LR2, 1.0f), 0.0f);

		// 端点スフィアが既に重なる(Dist<=R1+R2)なら隙間が無いのでセンサー不要。被覆には重なりが必要なので半径による間引きはしない。
		if (Dist <= FMath::Max(R1 + R2, KINDA_SMALL_NUMBER))
		{
			continue;
		}

		const int32 N = BoneConstraintSubdivisionCount;
		const FQuat Q1 = ModifyBones[I1].PrevRotation;
		const FQuat Q2 = ModifyBones[I2].PrevRotation;
		const FVector ScaleA = ModifyBones[I1].PoseScale;
		const FVector ScaleB = ModifyBones[I2].PoseScale;
		const auto BaseSettings = ModifyBones[I1].PhysicsSettings;

		for (int32 k = 0; k < N; ++k)
		{
			const float LerpAlpha = static_cast<float>(k + 1) / static_cast<float>(N + 1);

			FKawaiiPhysicsModifyBone BridgeDummy;
			BridgeDummy.bDummy = true;
			BridgeDummy.bBridgeDummy = true;
			// 配置用に InterBone* フィールドを端点1/端点2/補間率として流用
			BridgeDummy.InterBoneRealParentIndex = I1;
			BridgeDummy.InterBoneRealChildIndex = I2;
			BridgeDummy.InterBoneAlpha = LerpAlpha;
			BridgeDummy.Location = FMath::Lerp(P1, P2, LerpAlpha);
			BridgeDummy.PrevLocation = BridgeDummy.Location;
			BridgeDummy.PoseLocation = BridgeDummy.Location;
			BridgeDummy.PrevRotation = FQuat::Slerp(Q1, Q2, LerpAlpha);
			BridgeDummy.PoseRotation = BridgeDummy.PrevRotation;
			BridgeDummy.PoseScale = FMath::Lerp(ScaleA, ScaleB, LerpAlpha);
			BridgeDummy.ParentIndex = -1; // 縦階層に属さない
			BridgeDummy.BoneLength = Dist / (N + 1);
			// LengthRateは端点平均（毎フレームのUpdatePhysicsSettingsがこれを基にRadius等を再計算するため必須）
			BridgeDummy.LengthRateFromRoot = 0.5f * (LR1 + LR2);
			BridgeDummy.PhysicsSettings = BaseSettings;
			BridgeDummy.PhysicsSettings.Radius = 0.5f * (R1 + R2);

			const int32 Idx = ModifyBones.Add(BridgeDummy);
			ModifyBones[Idx].Index = Idx;
		}
	}
}

// -------------------------------------------------------------------
// Shared Collision
// -------------------------------------------------------------------

void FAnimNode_KawaiiPhysics::InitializeSharedCollision()
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitializeSharedCollision);
	if (bSharedCollisionInitialized)
	{
		return;
	}

	// Subsystemとowner ActorはGameThread(OnInitializeAnimInstance)で解決済みのキャッシュを使う。
	// ファミリーrootはSubsystem側がownerから毎回辿り直す（ランタイムのアタッチ変更に追従）。
	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem = CachedSharedCollisionSubsystem.Get();
	if (!Subsystem)
	{
		return;
	}

	AActor* OwnerActor = CachedSharedCollisionOwnerActor.Get();
	if (!OwnerActor)
	{
		return;
	}

	if (bSharedCollisionSource)
	{
		CachedSharedCollisionEntry = Subsystem->FindOrCreateEntry(OwnerActor, SharedCollisionGroupTag);
		if (CachedSharedCollisionEntry.IsValid())
		{
			const uint64 SourceID = reinterpret_cast<uint64>(this);
			CachedSourceSlot = CachedSharedCollisionEntry->GetOrCreateSlot(SourceID);
		}
	}

	if (bUseSharedCollision && !bSharedCollisionSource)
	{
		if (!CachedSharedCollisionEntry.IsValid())
		{
			CachedSharedCollisionEntry = Subsystem->FindEntry(OwnerActor, SharedCollisionGroupTag);
		}
	}

	// Targetの場合、Entry取得成功時のみ初期化完了（未取得時は次フレームでリトライ）
	if (!bUseSharedCollision || bSharedCollisionSource || CachedSharedCollisionEntry.IsValid())
	{
		bSharedCollisionInitialized = true;
	}
}

void FAnimNode_KawaiiPhysics::WriteSharedCollisionToSubsystem(
	FComponentSpacePoseContext& Output, const FTransform& ComponentTransform)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_WriteSharedCollisionToSubsystem);
	if (!CachedSourceSlot.IsValid())
	{
		return;
	}

	// 使い回しの一時バッファを使う（Publishのswapで前フレームのBufferが戻り、確保済みメモリを再利用できる）
	FKawaiiPhysicsSharedCollisionData& Data = SharedCollisionPublishScratch;
	Data.Reset();

	// ヘルパー: 有効なコリジョンを SimulationSpace→WorldSpace に変換して収集
	auto ConvertAndAppend = [&](const auto& InLimits, auto& OutLimits, auto PostConvert)
	{
		for (const auto& Limit : InLimits)
		{
			if (!Limit.bEnable)
			{
				continue;
			}
			auto Converted = Limit;
			const FTransform SimTransform(Limit.Rotation, Limit.Location);
			const FTransform WorldTransform = ConvertSimulationSpaceTransform(
				Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace, SimTransform);
			Converted.Location = WorldTransform.GetLocation();
			Converted.Rotation = WorldTransform.GetRotation();
			PostConvert(Converted, WorldTransform);
			OutLimits.Add(Converted);
		}
	};

	auto NoOp = [](auto&, const FTransform&) {};
	auto RecomputePlane = [](FPlanarLimit& L, const FTransform& T)
	{
		L.Plane = FPlane(L.Location, T.GetRotation().GetUpVector());
	};

	// 再割り当てを避けるため事前確保（無効分も含む上限。少量の過剰確保は許容）。
	Data.SphericalLimits.Reserve(SphericalLimits.Num() + SphericalLimitsData.Num());
	Data.CapsuleLimits.Reserve(CapsuleLimits.Num() + CapsuleLimitsData.Num());
	Data.BoxLimits.Reserve(BoxLimits.Num() + BoxLimitsData.Num());
	Data.PlanarLimits.Reserve(PlanarLimits.Num() + PlanarLimitsData.Num());

	// 全コリジョンソースを収集
	ConvertAndAppend(SphericalLimits,     Data.SphericalLimits, NoOp);
	ConvertAndAppend(SphericalLimitsData, Data.SphericalLimits, NoOp);
	ConvertAndAppend(CapsuleLimits,       Data.CapsuleLimits,   NoOp);
	ConvertAndAppend(CapsuleLimitsData,   Data.CapsuleLimits,   NoOp);
	ConvertAndAppend(BoxLimits,           Data.BoxLimits,        NoOp);
	ConvertAndAppend(BoxLimitsData,       Data.BoxLimits,        NoOp);
	ConvertAndAppend(PlanarLimits,        Data.PlanarLimits,     RecomputePlane);
	ConvertAndAppend(PlanarLimitsData,    Data.PlanarLimits,     RecomputePlane);

	CachedSourceSlot->Publish(Data);
}

void FAnimNode_KawaiiPhysics::UpdateSharedCollisionLimits(
	FComponentSpacePoseContext& Output)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateSharedCollisionLimits);
	SharedSphericalLimits.Reset();
	SharedCapsuleLimits.Reset();
	SharedBoxLimits.Reset();
	SharedPlanarLimits.Reset();

	if (!CachedSharedCollisionEntry.IsValid())
	{
		return;
	}

	CachedSharedCollisionEntry->ReadMerged(SharedCollisionMergedData);

	if (SharedCollisionMergedData.IsEmpty())
	{
		return;
	}

	// ヘルパー: WorldSpace→SimulationSpace に変換して格納
	auto ConvertAndStore = [&](const auto& InLimits, auto& OutLimits, auto PostConvert)
	{
		OutLimits.Reserve(InLimits.Num());
		for (const auto& Limit : InLimits)
		{
			auto Converted = Limit;
			const FTransform WorldTransform(Limit.Rotation, Limit.Location);
			const FTransform SimTransform = ConvertSimulationSpaceTransform(
				Output, EKawaiiPhysicsSimulationSpace::WorldSpace, SimulationSpace, WorldTransform);
			Converted.Location = SimTransform.GetLocation();
			Converted.Rotation = SimTransform.GetRotation();
			Converted.bEnable = true;
			PostConvert(Converted, SimTransform);
			OutLimits.Add(Converted);
		}
	};

	auto NoOp = [](auto&, const FTransform&) {};
	auto RecomputePlane = [](FPlanarLimit& L, const FTransform& T)
	{
		L.Plane = FPlane(L.Location, T.GetRotation().GetUpVector());
	};

	ConvertAndStore(SharedCollisionMergedData.SphericalLimits, SharedSphericalLimits, NoOp);
	ConvertAndStore(SharedCollisionMergedData.CapsuleLimits,   SharedCapsuleLimits,   NoOp);
	ConvertAndStore(SharedCollisionMergedData.BoxLimits,       SharedBoxLimits,        NoOp);
	ConvertAndStore(SharedCollisionMergedData.PlanarLimits,    SharedPlanarLimits,     RecomputePlane);
}
