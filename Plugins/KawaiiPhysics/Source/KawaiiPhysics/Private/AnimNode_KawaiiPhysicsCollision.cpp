// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysics.h"

#include "Animation/MirrorDataTable.h"
#include "AnimationRuntime.h"
#include "KawaiiPhysicsMirrorUtils.h"
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
#include "PhysicsEngine/TaperedCapsuleElem.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Templates/RemoveReference.h"

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
#include "KawaiiPhysicsNodeWarning.h"

extern TAutoConsoleVariable<int32> CVarSharedCollisionInitRetryThreshold;
extern TAutoConsoleVariable<int32> CVarSharedCollisionInitRetryThrottleInterval;

namespace
{
	// 相似変換と判定する相対許容。double の FTransform / FQuat 合成の丸め誤差は 1e-15 程度なので十分な余裕がある。
	// 高速経路は元の寸法をそのまま保つため、許容内の残差 ε は「ε × 写像の倍率 × 形状の寸法の和」のオーダーの包含不足に化ける
	//（係数 1 の厳密な不等式ではなく桁の目安。1e-9 なら 10 km の Box でもおおよそ 1e-3 cm 程度）。超える写像は全て保守経路（8 頂点フィット / σ_max 上界）へ回す。
	constexpr double LimitDimensionSimilarityTolerance = 1e-9;

	struct FLimitDimensionMapping
	{
		bool bUniform = true;
		double UniformScale = 1.0;
		double ConservativeScale = 1.0;
		FVector AxisImages[3];
	};

	FLimitDimensionMapping ComputeLimitDimensionMapping(
		const FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& Output,
		EKawaiiPhysicsSimulationSpace From, EKawaiiPhysicsSimulationSpace To)
	{
		FLimitDimensionMapping Map;
		const FVector Basis[3] = {FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector};
		double Lengths[3];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Map.AxisImages[Axis] = Node.ConvertSimulationSpaceVector(Output, From, To, Basis[Axis]);
			Lengths[Axis] = Map.AxisImages[Axis].Size();
		}

		// LimitDimensionSimilarityTolerance は double の合成丸め誤差を吸収し、全列の長さの差と直交性に適用する。
		// 高速経路の包含不足は許容 × 形状の寸法の和程度に抑え、許容を超える写像は保守経路へ回す。
		const double MaxLength = FMath::Max3(Lengths[0], Lengths[1], Lengths[2]);
		const double MinLength = FMath::Min3(Lengths[0], Lengths[1], Lengths[2]);
		const double Dot01 = FVector::DotProduct(Map.AxisImages[0], Map.AxisImages[1]);
		const double Dot02 = FVector::DotProduct(Map.AxisImages[0], Map.AxisImages[2]);
		const double Dot12 = FVector::DotProduct(Map.AxisImages[1], Map.AxisImages[2]);
		Map.bUniform = MaxLength > KINDA_SMALL_NUMBER
			&& (MaxLength - MinLength) <= LimitDimensionSimilarityTolerance * MaxLength
			&& FMath::Abs(Dot01) <= LimitDimensionSimilarityTolerance * Lengths[0] * Lengths[1]
			&& FMath::Abs(Dot02) <= LimitDimensionSimilarityTolerance * Lengths[0] * Lengths[2]
			&& FMath::Abs(Dot12) <= LimitDimensionSimilarityTolerance * Lengths[1] * Lengths[2];
		if (Map.bUniform)
		{
			// 平均ではなく最大列長を採用し、列長の残差に対して保守側に寄せる。
			Map.UniformScale = MaxLength;
			// 恒等・純回転の丸め誤差を１へスナップし、寸法をビット一致で保持する。
			if (FMath::Abs(MaxLength - 1.0) <= LimitDimensionSimilarityTolerance)
			{
				Map.UniformScale = 1.0;
			}
		}
		else
		{
			const double Square0 = FMath::Square(Lengths[0]);
			const double Square1 = FMath::Square(Lengths[1]);
			const double Square2 = FMath::Square(Lengths[2]);
			// Gram 行列 G_ij = c_i・c_j の Gershgorin 行和と trace（Frobenius ノルムの二乗）は共に σ_max² の上界なので、
			// 小さい方も上界になる。直交列では行和が MaxLength² に一致し、半径を最大 √3 倍に膨らませずに σ_max の上界を得る。
			const double RowSumBound = FMath::Max3(
				Square0 + FMath::Abs(Dot01) + FMath::Abs(Dot02),
				Square1 + FMath::Abs(Dot01) + FMath::Abs(Dot12),
				Square2 + FMath::Abs(Dot02) + FMath::Abs(Dot12));
			const double Trace = Square0 + Square1 + Square2;
			// 全列が潰れた写像でも係数をゼロにせず、実列のノルムに下限だけを設ける。
			Map.ConservativeScale = FMath::Max(FMath::Sqrt(FMath::Min(RowSumBound, Trace)),
				static_cast<double>(KINDA_SMALL_NUMBER));
		}
		return Map;
	}

	void ApplyScaledSphereDimensions(
		FSphericalLimit& Target, const FSphericalLimit& Source, const FLimitDimensionMapping& Map)
	{
		Target.Radius = Source.Radius * (Map.bUniform ? Map.UniformScale : Map.ConservativeScale);
	}

	template <typename LimitType, typename ConvertLocationType>
	void ApplyScaledCapsuleAxis(
		LimitType& Target, const LimitType& Source, const FLimitDimensionMapping& Map,
		ConvertLocationType ConvertLocation)
	{
		if (Map.bUniform)
		{
			// 相似変換では長さだけを拡縮し、呼び出し元が変換した位置と回転を保持する。
			Target.Length = Source.Length * Map.UniformScale;
			return;
		}

		// 非一様な写像では端点を変換して中心・長さ・軸方向を求める。
		const FVector Offset = Source.Rotation.GetAxisZ() * Source.Length * 0.5;
		const FVector Q0 = ConvertLocation(Source.Location + Offset);
		const FVector Q1 = ConvertLocation(Source.Location - Offset);
		const FVector Segment = Q0 - Q1;
		Target.Location = (Q0 + Q1) * 0.5;
		Target.Length = Segment.Size();
		const FVector Dir = Segment.GetSafeNormal();
		if (!Dir.IsZero())
		{
			// 元の＋Ｚ端を維持し、テーパードの両端の半径を入れ替えない。
			Target.Rotation = FQuat::FindBetweenNormals(Target.Rotation.GetAxisZ(), Dir) * Target.Rotation;
		}
	}

	template <typename ConvertLocationType>
	void ApplyScaledCapsuleDimensions(
		FCapsuleLimit& Target, const FCapsuleLimit& Source, const FLimitDimensionMapping& Map,
		ConvertLocationType ConvertLocation)
	{
		Target.Radius = Source.Radius * (Map.bUniform ? Map.UniformScale : Map.ConservativeScale);
		ApplyScaledCapsuleAxis(Target, Source, Map, ConvertLocation);
	}

	template <typename ConvertLocationType>
	void ApplyScaledTaperedCapsuleDimensions(
		FTaperedCapsuleLimit& Target, const FTaperedCapsuleLimit& Source, const FLimitDimensionMapping& Map,
		ConvertLocationType ConvertLocation)
	{
		if (Map.bUniform)
		{
			Target.Radius0 = Source.Radius0 * Map.UniformScale;
			Target.Radius1 = Source.Radius1 * Map.UniformScale;
		}
		else
		{
			// 非相似写像では点の線分への射影位置が変わり、ソルバが射影位置で補間する半径が細い端へ寄って包含が崩れ得る。
			// 両端を大きい方の半径 × σ_max 上界にそろえ、像を包含する通常のカプセルとして扱う（保守側の近似）。
			const float MaxRadius = FMath::Max(Source.Radius0, Source.Radius1) * Map.ConservativeScale;
			Target.Radius0 = MaxRadius;
			Target.Radius1 = MaxRadius;
		}
		ApplyScaledCapsuleAxis(Target, Source, Map, ConvertLocation);
	}

	template <typename ConvertLocationType>
	void ApplyScaledBoxDimensions(
		FBoxLimit& Target, const FBoxLimit& Source, const FLimitDimensionMapping& Map, ConvertLocationType ConvertLocation)
	{
		if (Map.bUniform)
		{
			Target.Extent = Source.Extent * Map.UniformScale;
			return;
		}

		// 非一様スケールでは元空間の８頂点を変換し、変換先の回転に沿う包含箱を作る。
		FVector Extent = FVector::ZeroVector;
		const FQuat InverseRotation = Target.Rotation.Inverse();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Offset(
				(Corner & 1) ? Source.Extent.X : -Source.Extent.X,
				(Corner & 2) ? Source.Extent.Y : -Source.Extent.Y,
				(Corner & 4) ? Source.Extent.Z : -Source.Extent.Z);
			const FVector Position = ConvertLocation(Source.Location + Source.Rotation.RotateVector(Offset));
			const FVector LocalPosition = InverseRotation.RotateVector(Position - Target.Location);
			Extent = Extent.ComponentMax(LocalPosition.GetAbs());
		}
		Target.Extent = Extent;
	}

	void ApplyScaledConvexDimensions(
		FKawaiiPhysicsConvexLimit& Target, const FKawaiiPhysicsConvexLimit& Source, const FLimitDimensionMapping& Map)
	{
		// 非一様スケールの凸は線形写像の σ_max 上界（ConservativeScale）による保守的な一様拡縮で近似する。
		const double UniformScale = Map.bUniform ? Map.UniformScale : Map.ConservativeScale;
		// 初回コピーで確保済みの配列を再利用し、更新時も必ず元の寸法から代入する。
		// 要素数が食い違ったら Source から取り直す（通常は一致するので再確保は起きない）。
		if (Target.LocalPlanes.Num() != Source.LocalPlanes.Num())
		{
			Target.LocalPlanes = Source.LocalPlanes;
		}
		for (int32 Index = 0; Index < Source.LocalPlanes.Num(); ++Index)
		{
			const FPlane& Plane = Source.LocalPlanes[Index];
			Target.LocalPlanes[Index] = FPlane(Plane.X, Plane.Y, Plane.Z, Plane.W * UniformScale);
		}
		Target.LocalBounds = FBox(Source.LocalBounds.Min * UniformScale, Source.LocalBounds.Max * UniformScale);
#if !UE_BUILD_SHIPPING
		if (Target.LocalVertices.Num() != Source.LocalVertices.Num())
		{
			Target.LocalVertices = Source.LocalVertices;
		}
		for (int32 Index = 0; Index < Source.LocalVertices.Num(); ++Index)
		{
			Target.LocalVertices[Index] = Source.LocalVertices[Index] * UniformScale;
		}
		if (Target.LocalEdges.Num() != Source.LocalEdges.Num())
		{
			Target.LocalEdges = Source.LocalEdges;
		}
		else
		{
			for (int32 Index = 0; Index < Source.LocalEdges.Num(); ++Index)
			{
				Target.LocalEdges[Index] = Source.LocalEdges[Index];
			}
		}
#endif
	}

	template <typename ConvertLocationType>
	void ApplyScaledLimitDimensions(
		FSphericalLimit& Target, const FSphericalLimit& Source, const FLimitDimensionMapping& Map, ConvertLocationType)
	{
		ApplyScaledSphereDimensions(Target, Source, Map);
	}

	template <typename ConvertLocationType>
	void ApplyScaledLimitDimensions(
		FCapsuleLimit& Target, const FCapsuleLimit& Source, const FLimitDimensionMapping& Map, ConvertLocationType ConvertLocation)
	{
		ApplyScaledCapsuleDimensions(Target, Source, Map, ConvertLocation);
	}

	template <typename ConvertLocationType>
	void ApplyScaledLimitDimensions(
		FTaperedCapsuleLimit& Target, const FTaperedCapsuleLimit& Source, const FLimitDimensionMapping& Map, ConvertLocationType ConvertLocation)
	{
		ApplyScaledTaperedCapsuleDimensions(Target, Source, Map, ConvertLocation);
	}

	template <typename ConvertLocationType>
	void ApplyScaledLimitDimensions(
		FBoxLimit& Target, const FBoxLimit& Source, const FLimitDimensionMapping& Map, ConvertLocationType ConvertLocation)
	{
		ApplyScaledBoxDimensions(Target, Source, Map, ConvertLocation);
	}

	template <typename ConvertLocationType>
	void ApplyScaledLimitDimensions(
		FKawaiiPhysicsConvexLimit& Target, const FKawaiiPhysicsConvexLimit& Source, const FLimitDimensionMapping& Map, ConvertLocationType)
	{
		ApplyScaledConvexDimensions(Target, Source, Map);
	}

	template <typename ConvertLocationType>
	void ApplyScaledLimitDimensions(FPlanarLimit&, const FPlanarLimit&, const FLimitDimensionMapping&, ConvertLocationType)
	{
	}

	template <typename LimitType, typename PostConvertType>
	void AppendWorldLimitsToSimulationSpace(
		const FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& Output,
		EKawaiiPhysicsSimulationSpace TargetSpace,
		const TArray<LimitType>& InLimits,
		TArray<LimitType>& OutLimits,
		PostConvertType PostConvert)
	{
		if (InLimits.IsEmpty())
		{
			return;
		}
		const FLimitDimensionMapping Map = ComputeLimitDimensionMapping(
			Node, Output, EKawaiiPhysicsSimulationSpace::WorldSpace, TargetSpace);
		OutLimits.Reserve(OutLimits.Num() + InLimits.Num());
		for (const LimitType& Limit : InLimits)
		{
			LimitType Converted = Limit;
			const FTransform WorldTransform(Limit.Rotation, Limit.Location);
			const FTransform SimTransform = Node.ConvertSimulationSpaceTransform(
				Output, EKawaiiPhysicsSimulationSpace::WorldSpace, TargetSpace, WorldTransform);
			Converted.Location = SimTransform.GetLocation();
			Converted.Rotation = SimTransform.GetRotation();
			Converted.bEnable = true;
			ApplyScaledLimitDimensions(Converted, Limit, Map, [&](const FVector& Position)
			{
				return Node.ConvertSimulationSpaceLocation(
					Output, EKawaiiPhysicsSimulationSpace::WorldSpace, TargetSpace, Position);
			});
			PostConvert(Converted, SimTransform);
			OutLimits.Add(Converted);
		}
	}

	template <typename LimitType, typename PostConvertType>
	bool RefreshWorldLimitsToSimulationSpaceInPlace(
		const FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& Output,
		EKawaiiPhysicsSimulationSpace TargetSpace,
		const TArray<LimitType>& InLimits,
		TArray<LimitType>& OutLimits,
		PostConvertType PostConvert)
	{
		if (InLimits.Num() != OutLimits.Num())
		{
			return false;
		}

		if (InLimits.IsEmpty())
		{
			return true;
		}
		const FLimitDimensionMapping Map = ComputeLimitDimensionMapping(
			Node, Output, EKawaiiPhysicsSimulationSpace::WorldSpace, TargetSpace);
		for (int32 Index = 0; Index < InLimits.Num(); ++Index)
		{
			const LimitType& Source = InLimits[Index];
			LimitType& Target = OutLimits[Index];
			const FTransform WorldTransform(Source.Rotation, Source.Location);
			const FTransform SimTransform = Node.ConvertSimulationSpaceTransform(
				Output, EKawaiiPhysicsSimulationSpace::WorldSpace, TargetSpace, WorldTransform);
			Target.Location = SimTransform.GetLocation();
			Target.Rotation = SimTransform.GetRotation();
			Target.bEnable = true;
			ApplyScaledLimitDimensions(Target, Source, Map, [&](const FVector& Position)
			{
				return Node.ConvertSimulationSpaceLocation(
					Output, EKawaiiPhysicsSimulationSpace::WorldSpace, TargetSpace, Position);
			});
			PostConvert(Target, SimTransform);
		}

		return true;
	}
}

void KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(
		const FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& Output,
		EKawaiiPhysicsSimulationSpace TargetSpace,
		const FKawaiiPhysicsSharedCollisionData& InData,
		TArray<FSphericalLimit>& OutSphericalLimits,
		TArray<FCapsuleLimit>& OutCapsuleLimits,
		TArray<FTaperedCapsuleLimit>& OutTaperedCapsuleLimits,
		TArray<FBoxLimit>& OutBoxLimits,
		TArray<FPlanarLimit>* OutPlanarLimits,
		TArray<FKawaiiPhysicsConvexLimit>* OutConvexLimits)
{
	auto NoOp = [](auto&, const FTransform&) {};
	auto RecomputePlane = [](FPlanarLimit& Limit, const FTransform& Transform)
	{
		Limit.Plane = FPlane(Limit.Location, Transform.GetRotation().GetUpVector());
	};
	auto RecomputeConvexCache = [](FKawaiiPhysicsConvexLimit& Limit, const FTransform&)
	{
		Limit.UpdateRuntimeCache();
	};

	AppendWorldLimitsToSimulationSpace(Node, Output, TargetSpace, InData.SphericalLimits,
		OutSphericalLimits, NoOp);
	AppendWorldLimitsToSimulationSpace(Node, Output, TargetSpace, InData.CapsuleLimits,
		OutCapsuleLimits, NoOp);
	AppendWorldLimitsToSimulationSpace(Node, Output, TargetSpace, InData.TaperedCapsuleLimits,
		OutTaperedCapsuleLimits, NoOp);
	AppendWorldLimitsToSimulationSpace(Node, Output, TargetSpace, InData.BoxLimits,
		OutBoxLimits, NoOp);

	if (OutPlanarLimits)
	{
		AppendWorldLimitsToSimulationSpace(Node, Output, TargetSpace, InData.PlanarLimits,
			*OutPlanarLimits, RecomputePlane);
	}

	if (OutConvexLimits)
	{
		AppendWorldLimitsToSimulationSpace(Node, Output, TargetSpace, InData.ConvexLimits,
			*OutConvexLimits, RecomputeConvexCache);
	}
}

bool KawaiiPhysicsSimpleWorldReadPath::RefreshSimulationSpaceLimitsInPlace(
	const FAnimNode_KawaiiPhysics& Node,
	FComponentSpacePoseContext& Output,
	EKawaiiPhysicsSimulationSpace TargetSpace,
	const FKawaiiPhysicsSharedCollisionData& InData,
	TArray<FSphericalLimit>& OutSphericalLimits,
	TArray<FCapsuleLimit>& OutCapsuleLimits,
	TArray<FTaperedCapsuleLimit>& OutTaperedCapsuleLimits,
	TArray<FBoxLimit>& OutBoxLimits,
	TArray<FKawaiiPhysicsConvexLimit>& OutConvexLimits)
{
	auto NoOp = [](auto&, const FTransform&) {};
	auto RecomputeConvexCache = [](FKawaiiPhysicsConvexLimit& Limit, const FTransform&)
	{
		Limit.UpdateRuntimeCache();
	};

	if (InData.SphericalLimits.Num() != OutSphericalLimits.Num()
		|| InData.CapsuleLimits.Num() != OutCapsuleLimits.Num()
		|| InData.TaperedCapsuleLimits.Num() != OutTaperedCapsuleLimits.Num()
		|| InData.BoxLimits.Num() != OutBoxLimits.Num()
		|| InData.ConvexLimits.Num() != OutConvexLimits.Num())
	{
		return false;
	}

	RefreshWorldLimitsToSimulationSpaceInPlace(Node, Output, TargetSpace, InData.SphericalLimits,
		OutSphericalLimits, NoOp);
	RefreshWorldLimitsToSimulationSpaceInPlace(Node, Output, TargetSpace, InData.CapsuleLimits,
		OutCapsuleLimits, NoOp);
	RefreshWorldLimitsToSimulationSpaceInPlace(Node, Output, TargetSpace, InData.TaperedCapsuleLimits,
		OutTaperedCapsuleLimits, NoOp);
	RefreshWorldLimitsToSimulationSpaceInPlace(Node, Output, TargetSpace, InData.BoxLimits,
		OutBoxLimits, NoOp);
	RefreshWorldLimitsToSimulationSpaceInPlace(Node, Output, TargetSpace, InData.ConvexLimits,
		OutConvexLimits, RecomputeConvexCache);
	return true;
}

bool KawaiiPhysicsSimpleWorldReadPath::RefreshSimulationSpaceLimitsInPlace(
	const FAnimNode_KawaiiPhysics& Node,
	FComponentSpacePoseContext& Output,
	EKawaiiPhysicsSimulationSpace TargetSpace,
	const TArray<FBoxLimit>& InBoxLimits,
	TArray<FBoxLimit>& OutBoxLimits)
{
	auto NoOp = [](FBoxLimit&, const FTransform&) {};
	if (InBoxLimits.Num() != OutBoxLimits.Num())
	{
		return false;
	}
	return RefreshWorldLimitsToSimulationSpaceInPlace(Node, Output, TargetSpace, InBoxLimits, OutBoxLimits, NoOp);
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
	RemoveAllSourceDataAssets(TaperedCapsuleLimitsData);
	RemoveAllSourceDataAssets(BoxLimitsData);
	RemoveAllSourceDataAssets(PlanarLimitsData);

	if (LimitsDataAsset)
	{
		SphericalLimitsData.Append(LimitsDataAsset->SphericalLimits);
		CapsuleLimitsData.Append(LimitsDataAsset->CapsuleLimits);
		TaperedCapsuleLimitsData.Append(LimitsDataAsset->TaperedCapsuleLimits);
		BoxLimitsData.Append(LimitsDataAsset->BoxLimits);
		PlanarLimitsData.Append(LimitsDataAsset->PlanarLimits);

		Initialize(SphericalLimitsData);
		Initialize(CapsuleLimitsData);
		Initialize(TaperedCapsuleLimitsData);
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
	RemoveAllSourcePhysicsAssets(TaperedCapsuleLimitsData);
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
			for (const auto& TaperedCapsuleElem : AggGeom.TaperedCapsuleElems)
			{
				FTaperedCapsuleLimit NewLimit;
				NewLimit.DrivingBone = DrivingBone;
				NewLimit.OffsetLocation = TaperedCapsuleElem.Center;
				NewLimit.OffsetRotation = TaperedCapsuleElem.Rotation;
				NewLimit.Radius0 = TaperedCapsuleElem.Radius0;
				NewLimit.Radius1 = TaperedCapsuleElem.Radius1;
				NewLimit.Length = TaperedCapsuleElem.Length;
				NewLimit.SourceType = ECollisionSourceType::PhysicsAsset;
				// Cloth用のWidth/bOneSidedCollisionは基本形状では扱わない
				TaperedCapsuleLimitsData.Add(NewLimit);
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
		Initialize(TaperedCapsuleLimitsData);
		Initialize(BoxLimitsData);
	}
}

void FAnimNode_KawaiiPhysics::ApplyMirrorLimits(const FBoneContainer& RequiredBones)
{
	auto RemoveAllSourceMirrors = [](auto& Targets)
	{
		Targets.RemoveAll([](const FCollisionLimitBase& Limit)
		{
			return Limit.SourceType == ECollisionSourceType::Mirror;
		});
	};

	RemoveAllSourceMirrors(SphericalLimitsData);
	RemoveAllSourceMirrors(CapsuleLimitsData);
	RemoveAllSourceMirrors(TaperedCapsuleLimitsData);
	RemoveAllSourceMirrors(BoxLimitsData);
	RemoveAllSourceMirrors(PlanarLimitsData);

	if (!MirrorDataTableForLimits)
	{
		return;
	}

	const EAxis::Type MirrorAxis = MirrorDataTableForLimits->MirrorAxis;
	if (MirrorAxis == EAxis::None)
	{
		return;
	}

	const USkeleton* Skeleton = RequiredBones.GetSkeletonAsset();
	if (!Skeleton)
	{
		KAWAII_LOG_NODE_WARNING_ONCE(bMirrorSkeletonMissingWarned, LogKawaiiPhysics,
			TEXT("MirrorDataTableForLimits is set, but RequiredBones has no Skeleton. Skip collision mirroring.%s"),
			TEXT(""));
		return;
	}

	TCustomBoneIndexArray<FSkeletonPoseBoneIndex, FSkeletonPoseBoneIndex> MirrorBoneIndexes;
	MirrorDataTableForLimits->FillMirrorBoneIndexes(Skeleton, MirrorBoneIndexes);

	const FReferenceSkeleton& SkeletonRefSkeleton = Skeleton->GetReferenceSkeleton();
	const auto ResolveMirrorBoneName = [&SkeletonRefSkeleton, &MirrorBoneIndexes](FName BoneName) -> FName
	{
		if (BoneName.IsNone())
		{
			return NAME_None;
		}

		const int32 SkeletonBoneIndex = SkeletonRefSkeleton.FindBoneIndex(BoneName);
		if (SkeletonBoneIndex == INDEX_NONE || !MirrorBoneIndexes.IsValidIndex(SkeletonBoneIndex))
		{
			return NAME_None;
		}

		const FSkeletonPoseBoneIndex MirroredBoneIndex = MirrorBoneIndexes[FSkeletonPoseBoneIndex(SkeletonBoneIndex)];
		if (!MirroredBoneIndex.IsValid() || MirroredBoneIndex.GetInt() == SkeletonBoneIndex
			|| !SkeletonRefSkeleton.IsValidIndex(MirroredBoneIndex.GetInt()))
		{
			return NAME_None;
		}

		return SkeletonRefSkeleton.GetBoneName(MirroredBoneIndex.GetInt());
	};

	const FReferenceSkeleton& MeshRefSkeleton = RequiredBones.GetReferenceSkeleton();
	TArray<FQuat> CSRefRotations;
	KawaiiPhysicsMirrorUtils::BuildComponentSpaceRefRotations(MeshRefSkeleton, CSRefRotations);

	const auto FindBoneIndex = [&MeshRefSkeleton](FName BoneName) -> int32
	{
		return MeshRefSkeleton.FindBoneIndex(BoneName);
	};

	auto AppendMirrored = [&RequiredBones, &ResolveMirrorBoneName, &FindBoneIndex, &CSRefRotations, MirrorAxis,
	                       this](const auto& NodeLimits, auto& MergedLimits)
	{
		using TLimit = typename TRemoveReference<decltype(MergedLimits)>::Type::ElementType;

		TArray<TLimit> NewLimits;
		KawaiiPhysicsMirrorUtils::AppendMirroredLimits(NodeLimits, NodeLimits, MergedLimits,
		                                          bSkipMirroredBoneWithExistingCollision,
		                                          ResolveMirrorBoneName, FindBoneIndex,
		                                          CSRefRotations, MirrorAxis, NewLimits);
		KawaiiPhysicsMirrorUtils::AppendMirroredLimits(MergedLimits, NodeLimits, MergedLimits,
		                                          bSkipMirroredBoneWithExistingCollision,
		                                          ResolveMirrorBoneName, FindBoneIndex,
		                                          CSRefRotations, MirrorAxis, NewLimits);

		for (auto& NewLimit : NewLimits)
		{
			NewLimit.DrivingBone.Initialize(RequiredBones);
		}
		MergedLimits.Append(MoveTemp(NewLimits));
	};

	AppendMirrored(SphericalLimits, SphericalLimitsData);
	AppendMirrored(CapsuleLimits, CapsuleLimitsData);
	AppendMirrored(TaperedCapsuleLimits, TaperedCapsuleLimitsData);
	AppendMirrored(BoxLimits, BoxLimitsData);
	AppendMirrored(PlanarLimits, PlanarLimitsData);
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

void FAnimNode_KawaiiPhysics::UpdateTaperedCapsuleLimits(TArray<FTaperedCapsuleLimit>& Limits,
                                                         FComponentSpacePoseContext& Output,
                                                         const FBoneContainer& BoneContainer,
                                                         const FTransform& ComponentTransform) const
{
	for (auto& TaperedCapsule : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateTaperedCapsuleLimit);

		if (TaperedCapsule.DrivingBone.IsValidToEvaluate(BoneContainer))
		{
			const FCompactPoseBoneIndex CompactPoseIndex = TaperedCapsule.DrivingBone.GetCompactPoseIndex(BoneContainer);
			FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);

			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);
			BoneTransform.SetRotation(TaperedCapsule.OffsetRotation.Quaternion() * BoneTransform.GetRotation());
			BoneTransform.AddToTranslation(TaperedCapsule.OffsetLocation);

			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);

			BoneTransform =
				ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulationSpace,
				                                BoneTransform);
			TaperedCapsule.Location = BoneTransform.GetLocation();
			TaperedCapsule.Rotation = BoneTransform.GetRotation();

			TaperedCapsule.bEnable = true;
		}
		else
		{
			TaperedCapsule.bEnable = false;
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

void FAnimNode_KawaiiPhysics::UpdatePlanarLimits(TArray<FPlanarLimit>& Limits, FComponentSpacePoseContext& Output,
                                                 const FBoneContainer& BoneContainer,
                                                 const FTransform& ComponentTransform) const
{
	for (auto& Planar : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdatePlanarLimit);

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

void FAnimNode_KawaiiPhysics::PrepareWorldCollisionQueryCaches(const USkeletalMeshComponent* OwningComp)
{
	// トレースはゲームスレッド上で実行されないため、TraceTag はデバッグトレースを描画しない
	WorldCollisionQueryParamsCache = FCollisionQueryParams(SCENE_QUERY_STAT(KawaiiCollision));

	if (bIgnoreSelfComponent)
	{
		WorldCollisionQueryParamsCache.AddIgnoredComponent(OwningComp);
	}

	// コンポーネントからコリジョン設定を取得
	WorldCollisionTraceChannelCache = bOverrideCollisionParams
		                                  ? CollisionChannelSettings.GetObjectType()
		                                  : OwningComp->GetCollisionObjectType();
	WorldCollisionResponseParamsCache = bOverrideCollisionParams
		                                    ? FCollisionResponseParams(
			                                    CollisionChannelSettings.GetResponseToChannels())
		                                    : FCollisionResponseParams(
			                                    OwningComp->GetCollisionResponseToChannels());
}

void FAnimNode_KawaiiPhysics::AdjustByWorldCollision(FComponentSpacePoseContext& Output, FKawaiiPhysicsModifyBone& Bone,
                                                     const USkeletalMeshComponent* OwningComp)
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
			WorldCollisionTraceChannelCache, FCollisionShape::MakeSphere(Bone.PhysicsSettings.Radius),
			WorldCollisionQueryParamsCache, WorldCollisionResponseParamsCache);
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
		                                       TraceEndLocationWS, FQuat::Identity, WorldCollisionTraceChannelCache,
		                                       FCollisionShape::MakeSphere(Bone.PhysicsSettings.Radius),
		                                       WorldCollisionQueryParamsCache, WorldCollisionResponseParamsCache);
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
	// キャッシュは operator= に意図的に載せていない（フィールド追加漏れで静かに陳腐化するため）。
	// またコピー構築（Shared 経路の auto Converted = Limit 等）は古いキャッシュ値ごと運ぶため、
	// AdjustBy* の前に必ず本関数で再計算することが正しさの前提。
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
	UpdateEnabledCaches(TaperedCapsuleLimits);
	UpdateEnabledCaches(TaperedCapsuleLimitsData);
	UpdateEnabledCaches(SharedTaperedCapsuleLimits);
	UpdateEnabledCaches(BoxLimits);
	UpdateEnabledCaches(BoxLimitsData);
	UpdateEnabledCaches(SharedBoxLimits);
	UpdateEnabledCaches(PlanarLimits);
	UpdateEnabledCaches(PlanarLimitsData);
	UpdateEnabledCaches(SharedPlanarLimits);
	UpdateEnabledCaches(SimpleWorldCapsuleLimits);
	UpdateEnabledCaches(SimpleWorldTaperedCapsuleLimits);
	UpdateEnabledCaches(SimpleWorldBoxLimits);
	UpdateEnabledCaches(SimpleWorldGroundBoxLimits);
	UpdateEnabledCaches(SimpleWorldConvexLimits);
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

void FAnimNode_KawaiiPhysics::AdjustByTaperedCapsuleCollision(FKawaiiPhysicsModifyBone& Bone,
                                                              TArray<FTaperedCapsuleLimit>& Limits)
{
	for (auto& TaperedCapsule : Limits)
	{
		if (!TaperedCapsule.bEnable || (TaperedCapsule.Radius0 <= 0.0f && TaperedCapsule.Radius1 <= 0.0f))
		{
			continue;
		}

		FVector ClosestPoint = TaperedCapsule.Location;
		// 負の半径が入り得るため、使用する半径は0以上に丸める。
		float TaperedRadius = FMath::Max(FMath::Max(TaperedCapsule.Radius0, TaperedCapsule.Radius1), 0.0f);

		if (TaperedCapsule.Length > KINDA_SMALL_NUMBER)
		{
			const FVector StartPoint = TaperedCapsule.CachedStartPoint;
			const FVector Segment = TaperedCapsule.CachedSegment;
			const float T = FMath::Clamp(FVector::DotProduct(Bone.Location - StartPoint, Segment) / TaperedCapsule.CachedSegmentSizeSq,
			                             0.0f, 1.0f);
			ClosestPoint = StartPoint + Segment * T;
			// Chaos PhiWithNormal 準拠の近似（厳密な2球凸包SDFではない）
			TaperedRadius = FMath::Max(FMath::Lerp(TaperedCapsule.Radius0, TaperedCapsule.Radius1, T), 0.0f);
		}

		const float LimitDistance = Bone.PhysicsSettings.Radius + TaperedRadius;
		const float DistSquared = (Bone.Location - ClosestPoint).SizeSquared();
		if (DistSquared < LimitDistance * LimitDistance)
		{
			FVector PushDir = (Bone.Location - ClosestPoint).GetSafeNormal();
			if (PushDir.IsNearlyZero())
			{
				// ボーンがカプセル軸上に乗ると押し出し方向が消えるため軸直交方向を代替に使う
				PushDir = TaperedCapsule.CachedFallbackPushDir;
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

			const bool bCenterInsideOrOnBox = LocalBox.IsInsideOrOn(LocalSphereCenter) || Distance <= KINDA_SMALL_NUMBER;
			// 内部・境界上の中心は最小貫通面へ、面までの貫通深さ + 半径だけ押し出す。
			// 放射方向では薄く広い Box（地面）で横滑りし、内部から抜けないため。
			// 境界のごく直外（Distance ≤ KINDA_SMALL_NUMBER）も含める（GetSafeNormal がゼロになる隙間を塞ぐ）。
			if (bCenterInsideOrOnBox)
			{
				const FVector Penetration = Box.Extent - LocalSphereCenter.GetAbs();
				int32 Axis;
				// 同値では X → Y → Z の順に選ぶ。
				if (Penetration.X <= Penetration.Y && Penetration.X <= Penetration.Z)
				{
					Axis = 0;
				}
				else if (Penetration.Y <= Penetration.Z)
				{
					Axis = 1;
				}
				else
				{
					Axis = 2;
				}

				const double Sign = LocalSphereCenter[Axis] >= 0.0 ? 1.0 : -1.0;
				FVector NewLocalSphereCenter = LocalSphereCenter;
				NewLocalSphereCenter[Axis] = Sign * (Box.Extent[Axis] + SphereRadius);
				Bone.Location = BoxTransform.TransformPosition(NewLocalSphereCenter);
				continue;
			}

			// 外部の中心は従来どおり最近点から半径だけ押し出す。
			if (Distance <= SphereRadius)
			{
				FVector PushOutDirection = PushOutVector.GetSafeNormal();
				FVector NewLocalSphereCenter = ClosestPoint + PushOutDirection * SphereRadius;
				Bone.Location = BoxTransform.TransformPosition(NewLocalSphereCenter);
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByConvexCollision(FKawaiiPhysicsModifyBone& Bone,
                                                      TArray<FKawaiiPhysicsConvexLimit>& Limits)
{
	for (auto& Limit : Limits)
	{
		if (!Limit.bEnable || Limit.LocalPlanes.IsEmpty())
		{
			continue;
		}

		const float SphereRadius = Bone.PhysicsSettings.Radius;
		const FTransform ConvexTransform = Limit.CachedConvexTransform;
		const FVector LocalSphereCenter = ConvexTransform.InverseTransformPosition(Bone.Location);
		if (!FMath::SphereAABBIntersection(FSphere(LocalSphereCenter, SphereRadius), Limit.LocalBounds))
		{
			continue;
		}

		float MaxDist = TNumericLimits<float>::Lowest();
		FVector BestNormal = FVector::ZeroVector;
		for (const FPlane& Plane : Limit.LocalPlanes)
		{
			const float Dist = Plane.PlaneDot(LocalSphereCenter);
			if (Dist > MaxDist)
			{
				MaxDist = Dist;
				BestNormal = FVector(Plane.X, Plane.Y, Plane.Z);
			}
		}

		if (MaxDist < SphereRadius)
		{
			// エッジ/コーナー近傍では max 符号付き距離が真の球-凸距離の下界になり、僅かに過剰押し出しになる。
			// TaperedCapsule と同格の意図的な近似で、トンネリング対策は Box/Capsule と同様に持たない。
			Bone.Location = ConvexTransform.TransformPosition(
				LocalSphereCenter + BestNormal * (SphereRadius - MaxDist));
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByPlanarCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits)
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
		float Compliance = XPBDComplianceValues[ComplianceIndex];
		// 極小 StepDt で compliance が発散しないようガード。
		const float StepDt = FMath::Max(GetStepDeltaTime(), KINDA_SMALL_NUMBER);
		Compliance /= StepDt * StepDt;
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

	const FLimitDimensionMapping Map = ComputeLimitDimensionMapping(
		*this, Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace);

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
			ApplyScaledLimitDimensions(Converted, Limit, Map, [&](const FVector& Position)
			{
				return ConvertSimulationSpaceLocation(
					Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace, Position);
			});
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
	Data.TaperedCapsuleLimits.Reserve(TaperedCapsuleLimits.Num() + TaperedCapsuleLimitsData.Num());
	Data.BoxLimits.Reserve(BoxLimits.Num() + BoxLimitsData.Num());
	Data.PlanarLimits.Reserve(PlanarLimits.Num() + PlanarLimitsData.Num());

	// 全コリジョンソースを収集
	ConvertAndAppend(SphericalLimits,     Data.SphericalLimits, NoOp);
	ConvertAndAppend(SphericalLimitsData, Data.SphericalLimits, NoOp);
	ConvertAndAppend(CapsuleLimits,       Data.CapsuleLimits,   NoOp);
	ConvertAndAppend(CapsuleLimitsData,   Data.CapsuleLimits,   NoOp);
	ConvertAndAppend(TaperedCapsuleLimits,     Data.TaperedCapsuleLimits, NoOp);
	ConvertAndAppend(TaperedCapsuleLimitsData, Data.TaperedCapsuleLimits, NoOp);
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
	SharedTaperedCapsuleLimits.Reset();
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

	// source ノードは Convex を publish しない。SimpleWorld 経路が Planar を捨てるのと対称の扱い。
	KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(*this, Output, SimulationSpace, SharedCollisionMergedData,
		SharedSphericalLimits, SharedCapsuleLimits, SharedTaperedCapsuleLimits, SharedBoxLimits,
		&SharedPlanarLimits, nullptr);
}

FKawaiiPhysicsSimpleWorldCollisionDesc FAnimNode_KawaiiPhysics::BuildSimpleWorldCollisionDesc() const
{
	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	Desc.GatherIntervalSec = SimpleWorldCollisionGatherInterval;
	Desc.GatherRadiusOverride =
		bOverrideSimpleWorldCollisionGatherRadius ? SimpleWorldCollisionGatherRadius : 0.0f;
	Desc.CollisionChannel = bOverrideCollisionParams ? CollisionChannelSettings.GetObjectType() : ECC_MAX;
	Desc.ObjectTypes = SimpleWorldCollisionObjectTypes;
	Desc.ConvexFallbackShape = SimpleWorldCollisionConvexFallbackShape;
	Desc.SkeletalMeshCollision = SimpleWorldCollisionSkeletalMeshCollision;
	Desc.bGroundCollision = bSimpleWorldCollisionGroundCollision;
	return Desc;
}

void FAnimNode_KawaiiPhysics::ResolveSimpleWorldCollisionSource(uint64 CurrentFrame)
{
	// この解決に使った設定側の値を控える。Update側はこれと現在値を比べてpin駆動の変更を検知する（どのreturn経路でも同じ値になるよう冒頭で記録）。
	SimpleWorldResolvedInputSource = SimpleWorldCollisionSource;
	SimpleWorldResolvedInputTag = SimpleWorldCollisionSharedTag;

	const bool bInjectedReaderKey =
		bSimpleWorldReaderMode
		&& SimpleWorldReaderKey.IsValid()
		&& SimpleWorldCollisionSource == EKawaiiPhysicsSimpleWorldCollisionSource::Local
		&& !(SimpleWorldReaderKey == SimpleWorldSharedKey);
	if (bInjectedReaderKey)
	{
		SimpleWorldResolvedSource = EKawaiiPhysicsSimpleWorldCollisionSource::Shared;
		return;
	}

	auto SetLocalSource = [this]()
	{
		SimpleWorldResolvedSource = EKawaiiPhysicsSimpleWorldCollisionSource::Local;
		bSimpleWorldReaderMode = false;
		SimpleWorldReaderKey = FKawaiiPhysicsSimpleWorldRegistryKey();
		SimpleWorldReaderKeyObjectName = NAME_None;
	};

	auto SetSharedSource = [this](const FKawaiiPhysicsSimpleWorldRegistryKey& Key, FName KeyObjectName)
	{
		SimpleWorldResolvedSource = EKawaiiPhysicsSimpleWorldCollisionSource::Shared;
		SimpleWorldSharedKey = Key;
		SimpleWorldReaderKey = Key;
		SimpleWorldReaderKeyObjectName = KeyObjectName;
		bSimpleWorldReaderMode = true;
	};

	if (SimpleWorldCollisionSource == EKawaiiPhysicsSimpleWorldCollisionSource::Local)
	{
		SetLocalSource();
		return;
	}

	if (!SimpleWorldCollisionSharedTag.IsValid())
	{
#if !UE_BUILD_SHIPPING
		if (!bSimpleWorldInvalidSharedTagWarningLogged)
		{
			KAWAII_LOG_NODE_WARNING(LogKawaiiPhysics,
				TEXT("SimpleWorldCollision: Shared Tag is None (Source: %s). Falling back to Local source."),
				*UEnum::GetValueAsString(SimpleWorldCollisionSource));
			bSimpleWorldInvalidSharedTagWarningLogged = true;
		}
#endif
		SimpleWorldSharedKey = FKawaiiPhysicsSimpleWorldRegistryKey();
		SetLocalSource();
		SimpleWorldAutoResolveCountdown = FMath::Max(1, GetKawaiiPhysicsSharedPublisherAutoResolveInterval());
		return;
	}

	AActor* OwnerActor = CachedSharedCollisionOwnerActor.Get();
	AActor* FamilyRoot = OwnerActor ? UKawaiiPhysicsSharedCollisionSubsystem::GetFamilyRoot(OwnerActor) : nullptr;
	if (FamilyRoot)
	{
		SimpleWorldSharedKey = FKawaiiPhysicsSimpleWorldRegistryKey::MakeSharedKey(FamilyRoot, SimpleWorldCollisionSharedTag);
	}
#if WITH_DEV_AUTOMATION_TESTS
	else if (!(SimpleWorldSharedKey.IsValid() && SimpleWorldSharedKey.Tag == SimpleWorldCollisionSharedTag))
	{
		SimpleWorldSharedKey = FKawaiiPhysicsSimpleWorldRegistryKey();
	}
#else
	else
	{
		SimpleWorldSharedKey = FKawaiiPhysicsSimpleWorldRegistryKey();
	}
#endif
	if (!SimpleWorldSharedKey.IsValid())
	{
		if (SimpleWorldCollisionSource == EKawaiiPhysicsSimpleWorldCollisionSource::Shared)
		{
			SetSharedSource(SimpleWorldSharedKey, NAME_None);
			return;
		}
		SetLocalSource();
		return;
	}

	if (SimpleWorldCollisionSource == EKawaiiPhysicsSimpleWorldCollisionSource::Shared)
	{
		SetSharedSource(SimpleWorldSharedKey, FamilyRoot ? FamilyRoot->GetFName() : NAME_None);
		return;
	}

	if (IsSharedProviderAlive(SimpleWorldSharedKey, CurrentFrame))
	{
		SetSharedSource(SimpleWorldSharedKey, FamilyRoot ? FamilyRoot->GetFName() : NAME_None);
	}
	else
	{
		SetLocalSource();
		SimpleWorldAutoResolveCountdown = FMath::Max(1, GetKawaiiPhysicsSharedPublisherAutoResolveInterval());
	}
}

bool FAnimNode_KawaiiPhysics::IsSharedProviderAlive(
	const FKawaiiPhysicsSimpleWorldRegistryKey& Key,
	uint64 CurrentFrame) const
{
	if (!Key.IsValid())
	{
		return false;
	}

	const uint64 ProviderMaxAge =
		static_cast<uint64>(FMath::Max(0, GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge()));
#if WITH_DEV_AUTOMATION_TESTS
	if (SimpleWorldAutomationSharedEntry.IsValid())
	{
		return KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(
			*SimpleWorldAutomationSharedEntry, CurrentFrame, ProviderMaxAge);
	}
#endif

	const UKawaiiPhysicsSharedCollisionSubsystem* Subsystem = CachedSharedCollisionSubsystem.Get();
	if (!Subsystem)
	{
		return false;
	}

	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry = Subsystem->FindSimpleWorldEntry(Key);
	return Entry.IsValid()
		&& KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(*Entry, CurrentFrame, ProviderMaxAge);
}

bool FAnimNode_KawaiiPhysics::ShouldRetrySimpleWorldReaderInitialize() const
{
	const int32 ThrottleInterval =
		FMath::Max(1, CVarSharedCollisionInitRetryThrottleInterval.GetValueOnAnyThread());
	// 警告前は毎評価試行して正常な起動ウィンドウでの即接続を維持し、警告後はThrottleInterval評価に1回へ間引く。
	// 間引き中はEntryの作成・解放（Registryのwrite lock）が消費ノード数×毎フレームで走り続けるのを防ぐ。
	return !bSimpleWorldReaderWarningLogged || (SimpleWorldReaderRetryCount % ThrottleInterval) == 0;
}

void FAnimNode_KawaiiPhysics::InitializeSimpleWorldCollision()
{
	if (bSimpleWorldCollisionInitialized)
	{
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	++NumSimpleWorldInitializeAttempts;
#endif

	ResolveSimpleWorldCollisionSource(GFrameCounter);

	const uint64 SourceID = reinterpret_cast<uint64>(this);
#if WITH_DEV_AUTOMATION_TESTS
	if (bSimpleWorldReaderMode && SimpleWorldAutomationSharedEntry.IsValid())
	{
		CachedSimpleWorldEntry = SimpleWorldAutomationSharedEntry;
		CachedSimpleWorldEntry->AddReaderMember(SourceID, CachedSimpleWorldCollisionSkelComp, GFrameCounter);
		bSimpleWorldCollisionInitialized = true;
		return;
	}
	if (!bSimpleWorldReaderMode && SimpleWorldAutomationLocalEntry.IsValid())
	{
		const FKawaiiPhysicsSimpleWorldCollisionDesc Desc = BuildSimpleWorldCollisionDesc();
		CachedSimpleWorldEntry = SimpleWorldAutomationLocalEntry;
		CachedSimpleWorldEntry->SetDesc(SourceID, Desc, GFrameCounter, CachedSimpleWorldCollisionSkelComp, true);
		LastSentSimpleWorldDesc = Desc;
		bSimpleWorldDescSent = true;
		bSimpleWorldCollisionInitialized = true;
		return;
	}
#endif

	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem = CachedSharedCollisionSubsystem.Get();
	if (!Subsystem)
	{
		return;
	}

	if (bSimpleWorldReaderMode)
	{
		CachedSimpleWorldEntry = Subsystem->FindOrCreateSimpleWorldEntry(
			SimpleWorldReaderKey,
			SourceID,
			FKawaiiPhysicsSimpleWorldCollisionDesc(),
			CachedSimpleWorldCollisionSkelComp,
			false);
		if (CachedSimpleWorldEntry.IsValid())
		{
			bSimpleWorldCollisionInitialized = true;
		}
		return;
	}

	const FKawaiiPhysicsSimpleWorldCollisionDesc Desc = BuildSimpleWorldCollisionDesc();
	CachedSimpleWorldEntry = Subsystem->FindOrCreateSimpleWorldEntry(CachedSimpleWorldCollisionSkelComp, SourceID, Desc);
	if (CachedSimpleWorldEntry.IsValid())
	{
		LastSentSimpleWorldDesc = Desc;
		bSimpleWorldDescSent = true;
		bSimpleWorldCollisionInitialized = true;
	}
}

void FAnimNode_KawaiiPhysics::UpdateSimpleWorldCollisionLimits(FComponentSpacePoseContext& Output,
                                                              bool bInitializeAlreadyAttempted)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateSimpleWorldCollisionLimits);

	auto ResetSimpleWorldSimulationSpaceLimits = [this]()
	{
		SimpleWorldSphericalLimits.Reset();
		SimpleWorldCapsuleLimits.Reset();
		SimpleWorldTaperedCapsuleLimits.Reset();
		SimpleWorldBoxLimits.Reset();
		SimpleWorldGroundBoxLimits.Reset();
		SimpleWorldConvexLimits.Reset();
	};

	// pin駆動でSource / Shared Tagが変わったら再初期化する（Wind側のResolvedSharedTag比較と同じ流儀）。
	// 比べるのは解決時に控えた設定側の値なので、ハーネス注入readerキー（Sourceは設定どおりLocalのまま）や
	// AutoのLocalフォールバック中（設定値はAutoのまま）では一致したままで、毎評価の再初期化にはならない。
	// provider待ちでスロットル中のreader（未初期化のままreaderモードだけ保持している状態）も検知対象にする。
	// 再試行状態をリセットするので、旧スロットル間隔が明けるのを待たず次の評価で新しいSource / Tagへ解決し直せる。
	// bSimpleWorldReaderModeが立つのは一度Sharedとして解決した後だけなので、未解決のノードでは誤検知しない。
	// Local provider側は解放されてもスロットルが無く次の評価で即初期化されるため、この条件に含めなくてよい。
	if ((bSimpleWorldCollisionInitialized || bSimpleWorldReaderMode)
		&& (SimpleWorldCollisionSource != SimpleWorldResolvedInputSource
			|| SimpleWorldCollisionSharedTag != SimpleWorldResolvedInputTag))
	{
		// 旧モードの登録（provider Desc / readerメンバー）を解除してEntryを手放す。scratchとserialもこの中でクリアされる。
		// SimpleWorldReaderRetryCountとbSimpleWorldReaderWarningLoggedもここで戻るので、再試行スロットルは即座に解除される。
		RequestSimpleWorldCollisionReinit();
		ResetSimpleWorldSimulationSpaceLimits();
		// この評価は押し出し無しで抜け、次の評価のInitializeが新しいSource / Tagで解決し直す（1評価の空白は既存のAuto遷移と同じ）。
		return;
	}

	if (!CachedSimpleWorldEntry.IsValid())
	{
		ResetSimpleWorldSimulationSpaceLimits();
		if (bSimpleWorldReaderMode)
		{
			const int32 RetryThreshold = FMath::Max(1, CVarSharedCollisionInitRetryThreshold.GetValueOnAnyThread());
			// Evaluate側は同じ述語でInitializeを試みるため、そこで試みた評価ではここで二重に呼ばない。
			// 結果としてInitializeは1評価につき最大1回、下の++SimpleWorldReaderRetryCountも1評価につき最大1回になる。
			// （Evaluateが間引いた評価ではSimpleWorldReaderRetryCountが同値のまま渡ってくるので、述語の結果も一致する）
			const bool bShouldRetryInitialize =
				!bInitializeAlreadyAttempted && ShouldRetrySimpleWorldReaderInitialize();

			if (bShouldRetryInitialize)
			{
				InitializeSimpleWorldCollision();
			}

			if (CachedSimpleWorldEntry.IsValid())
			{
				bSimpleWorldCollisionInitialized = true;
			}
			else
			{
				++SimpleWorldReaderRetryCount;
				if (!bSimpleWorldReaderWarningLogged && SimpleWorldReaderRetryCount >= RetryThreshold)
				{
					const FString TagName = SimpleWorldReaderKey.Tag.ToString();
					const FString KeyObjectName = SimpleWorldReaderKeyObjectName.IsNone() ? FString(TEXT("None")) : SimpleWorldReaderKeyObjectName.ToString();
					UE_LOG(LogKawaiiPhysics, Warning,
					       TEXT("Shared Simple World Collision entry has no provider (Tag=%s, KeyObject=%s). Waiting for a Shared Publisher."),
					       *TagName, *KeyObjectName);
					bSimpleWorldReaderWarningLogged = true;
				}
				return;
			}
		}
		else
		{
			return;
		}
	}

	if (bSimpleWorldReaderMode)
	{
		const uint64 SourceID = reinterpret_cast<uint64>(this);
		const uint64 ProviderMaxAge =
			static_cast<uint64>(FMath::Max(0, GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge()));
		if (!CachedSimpleWorldEntry->MarkReaderRead(SourceID, GFrameCounter, ProviderMaxAge))
		{
			if (SimpleWorldCollisionSource == EKawaiiPhysicsSimpleWorldCollisionSource::Auto)
			{
				ReleaseSimpleWorldCollision();
				RequestSimpleWorldCollisionReinit();
				ResetSimpleWorldSimulationSpaceLimits();
				SimpleWorldMergedScratch.Reset();
				SimpleWorldGroundScratch.Reset();
				LastReadSimpleWorldShapeSerial = 0;
				LastReadSimpleWorldGroundSerial = 0;
				LastReadSimpleWorldMemberSerialSum = 0;
				return;
			}

			ReleaseSimpleWorldCollision();
			++SimpleWorldReaderRetryCount;
			const int32 RetryThreshold = FMath::Max(1, CVarSharedCollisionInitRetryThreshold.GetValueOnAnyThread());
			if (!bSimpleWorldReaderWarningLogged && SimpleWorldReaderRetryCount >= RetryThreshold)
			{
				const FString TagName = SimpleWorldReaderKey.Tag.ToString();
				const FString KeyObjectName = SimpleWorldReaderKeyObjectName.IsNone() ? FString(TEXT("None")) : SimpleWorldReaderKeyObjectName.ToString();
				UE_LOG(LogKawaiiPhysics, Warning,
				       TEXT("Shared Simple World Collision entry has no provider (Tag=%s, KeyObject=%s). Waiting for a Shared Publisher."),
				       *TagName, *KeyObjectName);
				bSimpleWorldReaderWarningLogged = true;
			}
			ResetSimpleWorldSimulationSpaceLimits();
			SimpleWorldMergedScratch.Reset();
			SimpleWorldGroundScratch.Reset();
			LastReadSimpleWorldShapeSerial = 0;
			LastReadSimpleWorldGroundSerial = 0;
			LastReadSimpleWorldMemberSerialSum = 0;
			return;
		}
		SimpleWorldReaderRetryCount = 0;

		if (CachedSimpleWorldEntry->IsProviderDisabled())
		{
			ResetSimpleWorldSimulationSpaceLimits();
			SimpleWorldMergedScratch.Reset();
			SimpleWorldGroundScratch.Reset();
			LastReadSimpleWorldShapeSerial = 0;
			LastReadSimpleWorldGroundSerial = 0;
			LastReadSimpleWorldMemberSerialSum = 0;
			return;
		}

		const uint64 ShapeSerial = CachedSimpleWorldEntry->Slot.GetPublishSerial();
		const uint64 MemberSerialSum = CachedSimpleWorldEntry->GetMemberSlotsPublishSerialSum();
		if (LastReadSimpleWorldShapeSerial == 0
			|| ShapeSerial != LastReadSimpleWorldShapeSerial
			|| MemberSerialSum != LastReadSimpleWorldMemberSerialSum)
		{
			SimpleWorldMergedScratch.Reset();
			CachedSimpleWorldEntry->Slot.AppendTo(SimpleWorldMergedScratch);
			CachedSimpleWorldEntry->AppendFamilyMemberLimits(
				CachedSimpleWorldCollisionSkelComp, SimpleWorldMergedScratch);
			LastReadSimpleWorldShapeSerial = ShapeSerial;
			LastReadSimpleWorldMemberSerialSum = MemberSerialSum;

			SimpleWorldSphericalLimits.Reset();
			SimpleWorldCapsuleLimits.Reset();
			SimpleWorldTaperedCapsuleLimits.Reset();
			SimpleWorldBoxLimits.Reset();
			SimpleWorldConvexLimits.Reset();
			KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(
				*this, Output, SimulationSpace, SimpleWorldMergedScratch,
				SimpleWorldSphericalLimits, SimpleWorldCapsuleLimits, SimpleWorldTaperedCapsuleLimits,
				SimpleWorldBoxLimits, nullptr, &SimpleWorldConvexLimits);
		}
		else if (!KawaiiPhysicsSimpleWorldReadPath::RefreshSimulationSpaceLimitsInPlace(
			*this, Output, SimulationSpace, SimpleWorldMergedScratch,
			SimpleWorldSphericalLimits, SimpleWorldCapsuleLimits, SimpleWorldTaperedCapsuleLimits,
			SimpleWorldBoxLimits, SimpleWorldConvexLimits))
		{
			// 配列数がずれた場合は、前回配列が外部テスト注入や将来の形状追加で変わった可能性があるため全再構築へ戻す。
			SimpleWorldSphericalLimits.Reset();
			SimpleWorldCapsuleLimits.Reset();
			SimpleWorldTaperedCapsuleLimits.Reset();
			SimpleWorldBoxLimits.Reset();
			SimpleWorldConvexLimits.Reset();
			KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(
				*this, Output, SimulationSpace, SimpleWorldMergedScratch,
				SimpleWorldSphericalLimits, SimpleWorldCapsuleLimits, SimpleWorldTaperedCapsuleLimits,
				SimpleWorldBoxLimits, nullptr, &SimpleWorldConvexLimits);
		}

		const uint64 GroundSerial = CachedSimpleWorldEntry->GroundSlot.GetPublishSerial();
		if (LastReadSimpleWorldGroundSerial == 0 || GroundSerial != LastReadSimpleWorldGroundSerial)
		{
			SimpleWorldGroundScratch.Reset();
			CachedSimpleWorldEntry->GroundSlot.AppendTo(SimpleWorldGroundScratch);
			LastReadSimpleWorldGroundSerial = GroundSerial;

			SimpleWorldGroundBoxLimits.Reset();
			auto NoOp = [](FBoxLimit&, const FTransform&) {};
			AppendWorldLimitsToSimulationSpace(*this, Output, SimulationSpace, SimpleWorldGroundScratch.BoxLimits,
				SimpleWorldGroundBoxLimits, NoOp);
		}
		else if (!KawaiiPhysicsSimpleWorldReadPath::RefreshSimulationSpaceLimitsInPlace(
			*this, Output, SimulationSpace, SimpleWorldGroundScratch.BoxLimits, SimpleWorldGroundBoxLimits))
		{
			// 配列数がずれた場合は、地面 Box の有無が外部から変わった可能性があるため全再構築へ戻す。
			SimpleWorldGroundBoxLimits.Reset();
			auto NoOp = [](FBoxLimit&, const FTransform&) {};
			AppendWorldLimitsToSimulationSpace(*this, Output, SimulationSpace, SimpleWorldGroundScratch.BoxLimits,
				SimpleWorldGroundBoxLimits, NoOp);
		}

		return;
	}

	const uint64 SourceID = reinterpret_cast<uint64>(this);

	if (SimpleWorldCollisionSource == EKawaiiPhysicsSimpleWorldCollisionSource::Auto
		&& SimpleWorldResolvedSource == EKawaiiPhysicsSimpleWorldCollisionSource::Local)
	{
		if (--SimpleWorldAutoResolveCountdown <= 0)
		{
			if (IsSharedProviderAlive(SimpleWorldSharedKey, GFrameCounter))
			{
				RequestSimpleWorldCollisionReinit();
				ResetSimpleWorldSimulationSpaceLimits();
				return;
			}
			SimpleWorldAutoResolveCountdown = FMath::Max(1, GetKawaiiPhysicsSharedPublisherAutoResolveInterval());
		}
	}

	const auto ReleaseInvalidSimpleWorldEntry = [this]()
	{
		ReleaseSimpleWorldCollision();
		SimpleWorldSphericalLimits.Reset();
		SimpleWorldCapsuleLimits.Reset();
		SimpleWorldTaperedCapsuleLimits.Reset();
		SimpleWorldBoxLimits.Reset();
		SimpleWorldGroundBoxLimits.Reset();
		SimpleWorldConvexLimits.Reset();
		SimpleWorldMergedScratch.Reset();
		SimpleWorldGroundScratch.Reset();
		LastReadSimpleWorldShapeSerial = 0;
		LastReadSimpleWorldGroundSerial = 0;
		LastReadSimpleWorldMemberSerialSum = 0;
	};
	if (CachedSimpleWorldEntry->IsRetired())
	{
		ReleaseInvalidSimpleWorldEntry();
		return;
	}

	const FKawaiiPhysicsSimpleWorldCollisionDesc Desc = BuildSimpleWorldCollisionDesc();

	if (!bSimpleWorldDescSent || !(Desc == LastSentSimpleWorldDesc))
	{
		// 半径警告の再チェックは収集半径の指定が変わったときだけ解禁する（GatherInterval のピン駆動で毎フレーム再送されても走査を繰り返さない）。
		if (!bSimpleWorldDescSent
			|| !FMath::IsNearlyEqual(Desc.GatherRadiusOverride, LastSentSimpleWorldDesc.GatherRadiusOverride))
		{
			bSimpleWorldRadiusChecked = false;
			SimpleWorldRadiusCheckDeferrals = 0;
		}
		// 期限切れで provider slot が消えた後の再送でも SkelComp を失わないよう、必ず自分の SkelComp を渡す。
		if (!CachedSimpleWorldEntry->SetDesc(SourceID, Desc, GFrameCounter, CachedSimpleWorldCollisionSkelComp, true))
		{
			ReleaseInvalidSimpleWorldEntry();
			return;
		}
		LastSentSimpleWorldDesc = Desc;
		bSimpleWorldDescSent = true;
	}

	if (!CachedSimpleWorldEntry->MarkRead(SourceID))
	{
		ReleaseInvalidSimpleWorldEntry();
		return;
	}

	const uint64 ShapeSerial = CachedSimpleWorldEntry->Slot.GetPublishSerial();
	if (LastReadSimpleWorldShapeSerial == 0 || ShapeSerial != LastReadSimpleWorldShapeSerial)
	{
		SimpleWorldMergedScratch.Reset();
		CachedSimpleWorldEntry->Slot.AppendTo(SimpleWorldMergedScratch);
		LastReadSimpleWorldShapeSerial = ShapeSerial;

		SimpleWorldSphericalLimits.Reset();
		SimpleWorldCapsuleLimits.Reset();
		SimpleWorldTaperedCapsuleLimits.Reset();
		SimpleWorldBoxLimits.Reset();
		SimpleWorldConvexLimits.Reset();
		KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(
			*this, Output, SimulationSpace, SimpleWorldMergedScratch,
			SimpleWorldSphericalLimits, SimpleWorldCapsuleLimits, SimpleWorldTaperedCapsuleLimits,
			SimpleWorldBoxLimits, nullptr, &SimpleWorldConvexLimits);
	}
	else if (!KawaiiPhysicsSimpleWorldReadPath::RefreshSimulationSpaceLimitsInPlace(
		*this, Output, SimulationSpace, SimpleWorldMergedScratch,
		SimpleWorldSphericalLimits, SimpleWorldCapsuleLimits, SimpleWorldTaperedCapsuleLimits,
		SimpleWorldBoxLimits, SimpleWorldConvexLimits))
	{
		// 配列数がずれた場合は、前回配列が外部テスト注入や将来の形状追加で変わった可能性があるため全再構築へ戻す。
		SimpleWorldSphericalLimits.Reset();
		SimpleWorldCapsuleLimits.Reset();
		SimpleWorldTaperedCapsuleLimits.Reset();
		SimpleWorldBoxLimits.Reset();
		SimpleWorldConvexLimits.Reset();
		KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(
			*this, Output, SimulationSpace, SimpleWorldMergedScratch,
			SimpleWorldSphericalLimits, SimpleWorldCapsuleLimits, SimpleWorldTaperedCapsuleLimits,
			SimpleWorldBoxLimits, nullptr, &SimpleWorldConvexLimits);
	}

	const uint64 GroundSerial = CachedSimpleWorldEntry->GroundSlot.GetPublishSerial();
	if (LastReadSimpleWorldGroundSerial == 0 || GroundSerial != LastReadSimpleWorldGroundSerial)
	{
		SimpleWorldGroundScratch.Reset();
		CachedSimpleWorldEntry->GroundSlot.AppendTo(SimpleWorldGroundScratch);
		LastReadSimpleWorldGroundSerial = GroundSerial;

		SimpleWorldGroundBoxLimits.Reset();
		auto NoOp = [](FBoxLimit&, const FTransform&) {};
		AppendWorldLimitsToSimulationSpace(*this, Output, SimulationSpace, SimpleWorldGroundScratch.BoxLimits,
			SimpleWorldGroundBoxLimits, NoOp);
	}
	else if (!KawaiiPhysicsSimpleWorldReadPath::RefreshSimulationSpaceLimitsInPlace(
		*this, Output, SimulationSpace, SimpleWorldGroundScratch.BoxLimits, SimpleWorldGroundBoxLimits))
	{
		// 配列数がずれた場合は、地面 Box の有無が外部から変わった可能性があるため全再構築へ戻す。
		SimpleWorldGroundBoxLimits.Reset();
		auto NoOp = [](FBoxLimit&, const FTransform&) {};
		AppendWorldLimitsToSimulationSpace(*this, Output, SimulationSpace, SimpleWorldGroundScratch.BoxLimits,
			SimpleWorldGroundBoxLimits, NoOp);
	}

	CheckSimpleWorldGatherRadius(Output);
}

void FAnimNode_KawaiiPhysics::CheckSimpleWorldGatherRadius(FComponentSpacePoseContext& Output)
{
	if (!bOverrideSimpleWorldCollisionGatherRadius || bSimpleWorldRadiusChecked || ModifyBones.IsEmpty())
	{
		return;
	}

	bool bHasNonZeroPoseLocation = false;
	for (const FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (!Bone.PoseLocation.IsNearlyZero())
		{
			bHasNonZeroPoseLocation = true;
			break;
		}
	}
	if (!bHasNonZeroPoseLocation)
	{
		// 全ボーンがゼロ姿勢のフレームは持ち越すが、退化チェーンで無限に走査し続けないよう上限で打ち切る。
		if (++SimpleWorldRadiusCheckDeferrals >= MaxSimpleWorldRadiusCheckDeferrals)
		{
			bSimpleWorldRadiusChecked = true;
		}
		return;
	}

	const FVector ComponentOriginSim = ConvertSimulationSpaceLocation(
		Output, EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulationSpace, FVector::ZeroVector);

	float RequiredRadius = 0.0f;
	for (const FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		RequiredRadius = FMath::Max(
			RequiredRadius,
			FVector::Dist(ComponentOriginSim, Bone.PoseLocation) + Bone.PhysicsSettings.Radius);
	}

	if (SimpleWorldCollisionGatherRadius + KINDA_SMALL_NUMBER < RequiredRadius)
	{
		KAWAII_LOG_NODE_WARNING(LogKawaiiPhysics,
			TEXT("SimpleWorldCollision: GatherRadius %.2f may be smaller than the physics chain reach %.2f. "
				"Increase SimpleWorldCollisionGatherRadius or disable the override to use SkeletalMesh bounds."),
			SimpleWorldCollisionGatherRadius, RequiredRadius);
	}

	bSimpleWorldRadiusChecked = true;
}

void FAnimNode_KawaiiPhysics::ReleaseSimpleWorldCollision()
{
	if (CachedSimpleWorldEntry.IsValid())
	{
		if (bSimpleWorldReaderMode)
		{
			CachedSimpleWorldEntry->RemoveReaderMember(reinterpret_cast<uint64>(this));
		}
		else
		{
			CachedSimpleWorldEntry->RemoveDesc(reinterpret_cast<uint64>(this));
		}
	}

	CachedSimpleWorldEntry.Reset();
	bSimpleWorldCollisionInitialized = false;
	bSimpleWorldDescSent = false;
	SimpleWorldMergedScratch.Reset();
	SimpleWorldGroundScratch.Reset();
	LastReadSimpleWorldShapeSerial = 0;
	LastReadSimpleWorldGroundSerial = 0;
	LastReadSimpleWorldMemberSerialSum = 0;
}

void FAnimNode_KawaiiPhysics::RequestSimpleWorldCollisionReinit()
{
	bSimpleWorldCollisionInitialized = false;
	bSimpleWorldDescSent = false;
	bSimpleWorldRadiusChecked = false;
	SimpleWorldRadiusCheckDeferrals = 0;
	LastReadSimpleWorldShapeSerial = 0;
	LastReadSimpleWorldGroundSerial = 0;
	LastReadSimpleWorldMemberSerialSum = 0;
	SimpleWorldReaderRetryCount = 0;
	bSimpleWorldReaderWarningLogged = false;
	SimpleWorldMergedScratch.Reset();
	SimpleWorldGroundScratch.Reset();
	if (CachedSimpleWorldEntry.IsValid())
	{
		if (bSimpleWorldReaderMode)
		{
			CachedSimpleWorldEntry->RemoveReaderMember(reinterpret_cast<uint64>(this));
		}
		else
		{
			CachedSimpleWorldEntry->RemoveDesc(reinterpret_cast<uint64>(this));
		}
		CachedSimpleWorldEntry.Reset();
	}
}
