// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSimpleWorldCollision.h"

#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"

#include "Algo/StableSort.h"
#include "Engine/EngineTypes.h"
#include "Misc/EngineVersionComparison.h"
#include "PhysicsEngine/PhysicsAsset.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

#include "ReferenceSkeleton.h"

namespace
{
	void InitializeSimpleWorldLimit(FCollisionLimitBase& Limit)
	{
		Limit.bEnable = true;
		Limit.SourceType = ECollisionSourceType::SimpleWorld;
	}

	bool HasUsableExtent(const FVector& Extent)
	{
		return Extent.GetAbsMax() > KINDA_SMALL_NUMBER;
	}

	void ApplyRadiusScale(FSphericalLimit& Limit, float RadiusScale)
	{
		Limit.Radius *= RadiusScale;
	}

	void ApplyRadiusScale(FCapsuleLimit& Limit, float RadiusScale)
	{
		Limit.Radius *= RadiusScale;
	}

	void ApplyRadiusScale(FTaperedCapsuleLimit& Limit, float RadiusScale)
	{
		Limit.Radius0 *= RadiusScale;
		Limit.Radius1 *= RadiusScale;
	}

	void ApplyRadiusScale(FBoxLimit&, float)
	{
	}

	void ApplyRadiusScale(FKawaiiPhysicsConvexLimit&, float)
	{
	}

	bool IsFiniteVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	}

	template <typename LimitType>
	void AppendTransformedLimits(
		TArrayView<const LimitType> LocalLimits,
		const FTransform& ComponentTM,
		TArray<LimitType>& OutWorldLimits,
		float RadiusScale = 1.0f)
	{
		OutWorldLimits.Reserve(OutWorldLimits.Num() + LocalLimits.Num());

		const FQuat ComponentRotation = ComponentTM.GetRotation();
		for (const auto& LocalLimit : LocalLimits)
		{
			auto WorldLimit = LocalLimit;
			WorldLimit.Location = ComponentTM.TransformPosition(LocalLimit.Location);
			WorldLimit.Rotation = ComponentRotation * LocalLimit.Rotation;
			WorldLimit.Rotation.Normalize();
			ApplyRadiusScale(WorldLimit, RadiusScale);
			OutWorldLimits.Add(WorldLimit);
		}
	}

	template <typename LimitType>
	TArrayView<const LimitType> MakeKawaiiPhysicsSimpleWorldLimitView(
		const TArray<LimitType>& Limits,
		int32 Offset,
		int32 Num)
	{
		check(Offset >= 0 && Num >= 0 && Offset + Num <= Limits.Num());
		return Num > 0
			? TArrayView<const LimitType>(Limits.GetData() + Offset, Num)
			: TArrayView<const LimitType>();
	}

	void AppendTransformedPlanarLimits(
		const TArray<FPlanarLimit>& LocalLimits,
		const FTransform& ComponentTM,
		TArray<FPlanarLimit>& OutWorldLimits)
	{
		OutWorldLimits.Reserve(OutWorldLimits.Num() + LocalLimits.Num());
		const FQuat ComponentRotation = ComponentTM.GetRotation();
		for (const auto& LocalLimit : LocalLimits)
		{
			auto WorldLimit = LocalLimit;
			WorldLimit.Location = ComponentTM.TransformPosition(LocalLimit.Location);
			WorldLimit.Rotation = ComponentRotation * LocalLimit.Rotation;
			WorldLimit.Rotation.Normalize();
			WorldLimit.Plane = FPlane(WorldLimit.Location, WorldLimit.Rotation.GetUpVector());
			OutWorldLimits.Add(WorldLimit);
		}
	}

	bool IsKawaiiPhysicsSimpleWorldCollisionAggGeomEmpty(const FKAggregateGeom& AggGeom)
	{
		return AggGeom.SphereElems.IsEmpty()
			&& AggGeom.SphylElems.IsEmpty()
			&& AggGeom.TaperedCapsuleElems.IsEmpty()
			&& AggGeom.BoxElems.IsEmpty()
			&& AggGeom.ConvexElems.IsEmpty();
	}

	struct FKawaiiPhysicsSimpleWorldPhysicsAssetBodyCandidate
	{
		const USkeletalBodySetup* BodySetup = nullptr;
		int32 BoneIndex = INDEX_NONE;
		int32 BodyIndex = INDEX_NONE;
	};
}

namespace KawaiiPhysicsSimpleWorldCollision
{
	FCollisionResponseParams BuildSimpleWorldResponseParams(
		const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes)
	{
		FCollisionResponseParams Params(ECR_Ignore);
		if (ObjectTypes.IsEmpty())
		{
			Params.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Block);
			Params.CollisionResponse.SetResponse(ECC_WorldDynamic, ECR_Block);
			return Params;
		}

		for (const TEnumAsByte<EObjectTypeQuery>& ObjectType : ObjectTypes)
		{
			const ECollisionChannel CollisionChannel = UEngineTypes::ConvertToCollisionChannel(ObjectType.GetValue());
			if (CollisionChannel != ECC_MAX)
			{
				Params.CollisionResponse.SetResponse(CollisionChannel, ECR_Block);
			}
		}
		return Params;
	}

	bool IsSimpleWorldGatherInputValid(const FVector& Center, float Radius)
	{
		return !Center.ContainsNaN()
			&& FMath::IsFinite(Radius)
			&& Radius > KINDA_SMALL_NUMBER;
	}

	bool ShouldUseSimpleWorldGatherOrder(int32 NumOverlaps, int32 MaxGatheredComponents)
	{
		// 上限 0（CVar で指定可）では visit ループが即座に打ち切られるため、距離計算と StableSort を払わない（master と同じ O(1)）。
		return MaxGatheredComponents > 0 && NumOverlaps > MaxGatheredComponents;
	}

	void SortSimpleWorldGatherOrderByDistance(TArrayView<const float> DistanceSquared, TArray<int32>& OutOrder)
	{
		OutOrder.Reset(DistanceSquared.Num());
		for (int32 Index = 0; Index < DistanceSquared.Num(); ++Index)
		{
			OutOrder.Add(Index);
		}

		Algo::StableSort(OutOrder, [&DistanceSquared](int32 Lhs, int32 Rhs)
		{
			return DistanceSquared[Lhs] < DistanceSquared[Rhs];
		});
	}

	bool IsSimpleWorldProviderAlive(
		const FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
		uint64 CurrentFrame,
		uint64 ProviderMaxAgeFrames)
	{
		if (!Entry.HasProviderDesc())
		{
			return false;
		}

		const uint64 LastProviderFrame = Entry.GetLastProviderFrame();
		return CurrentFrame <= LastProviderFrame || CurrentFrame - LastProviderFrame <= ProviderMaxAgeFrames;
	}

	void AppendBoundsLocalLimits(
		const FBoxSphereBounds& Bounds,
		const FTransform& ComponentTM,
		EKawaiiPhysicsSimpleWorldConvexFallbackShape BoundsShape,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits)
	{
		if (BoundsShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::None)
		{
			return;
		}

		const FVector LocalCenter = ComponentTM.InverseTransformPosition(Bounds.Origin);
		if (BoundsShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull
			|| BoundsShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox)
		{
			// Bounds にはハルの平面情報が無いため、ConvexHull 指定でも BoundingBox として扱う。
			FBoxLimit NewLimit;
			InitializeSimpleWorldLimit(NewLimit);
			NewLimit.Location = LocalCenter;
			NewLimit.Rotation = ComponentTM.GetRotation().Inverse();
			NewLimit.Extent = Bounds.BoxExtent;
			OutLocalLimits.BoxLimits.Add(NewLimit);
		}
		else if (BoundsShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere)
		{
			FSphericalLimit NewLimit;
			InitializeSimpleWorldLimit(NewLimit);
			NewLimit.Location = LocalCenter;
			NewLimit.Rotation = FQuat::Identity;
			NewLimit.Radius = Bounds.SphereRadius;
			NewLimit.LimitType = ESphericalLimitType::Outer;
			OutLocalLimits.SphericalLimits.Add(NewLimit);
		}
	}

	bool AppendConvexElemLocalLimit(
		TArrayView<const FPlane> BodySpacePlanes,
		TArrayView<const FVector> ElemLocalVertices,
		TArrayView<const int32> Indices,
		const FTransform& ElemTM,
		const FVector& Scale3D,
		int32 MaxConvexPlanes,
		bool bBuildDebugGeometry,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits)
	{
		if (BodySpacePlanes.Num() == 0
			|| BodySpacePlanes.Num() > MaxConvexPlanes
			|| ElemLocalVertices.Num() == 0
			|| FMath::Abs(Scale3D.X) < KINDA_SMALL_NUMBER
			|| FMath::Abs(Scale3D.Y) < KINDA_SMALL_NUMBER
			|| FMath::Abs(Scale3D.Z) < KINDA_SMALL_NUMBER)
		{
			return false;
		}

		for (const FPlane& BodySpacePlane : BodySpacePlanes)
		{
			const FVector PlaneNormal(BodySpacePlane.X, BodySpacePlane.Y, BodySpacePlane.Z);
			if (!IsFiniteVector(PlaneNormal)
				|| !FMath::IsFinite(BodySpacePlane.W)
				|| PlaneNormal.SizeSquared() <= FMath::Square(KINDA_SMALL_NUMBER))
			{
				return false;
			}
		}

		TArray<FVector> ScaledVertices;
		ScaledVertices.Reserve(ElemLocalVertices.Num());

		FBox ScaledBounds(ForceInit);
		for (const FVector& ElemLocalVertex : ElemLocalVertices)
		{
			if (!IsFiniteVector(ElemLocalVertex))
			{
				return false;
			}

			const FVector ScaledVertex = ElemTM.TransformPosition(ElemLocalVertex) * Scale3D;
			if (!IsFiniteVector(ScaledVertex))
			{
				return false;
			}

			ScaledVertices.Add(ScaledVertex);
			ScaledBounds += ScaledVertex;
		}

		if (!ScaledBounds.IsValid)
		{
			return false;
		}

		const FVector LocalCenter = ScaledBounds.GetCenter();

		FKawaiiPhysicsConvexLimit NewLimit;
		InitializeSimpleWorldLimit(NewLimit);
		NewLimit.Location = LocalCenter;
		NewLimit.Rotation = FQuat::Identity;
		NewLimit.LocalBounds = FBox(ScaledBounds.Min - LocalCenter, ScaledBounds.Max - LocalCenter);
		NewLimit.LocalPlanes.Reserve(BodySpacePlanes.Num());

		for (const FPlane& BodySpacePlane : BodySpacePlanes)
		{
			const FVector PlaneNormal(BodySpacePlane.X, BodySpacePlane.Y, BodySpacePlane.Z);
			const FVector BodySpacePoint = PlaneNormal * BodySpacePlane.W;
			const FVector ScaledPoint = BodySpacePoint * Scale3D;
			// 平面法線は逆転置でスケール変換する。負スケールでも外向き性はこの変換で保たれるため、符号反転は不要。
			const FVector ScaledNormal = FVector(
				PlaneNormal.X / Scale3D.X,
				PlaneNormal.Y / Scale3D.Y,
				PlaneNormal.Z / Scale3D.Z).GetSafeNormal();
			if (!IsFiniteVector(ScaledPoint)
				|| !IsFiniteVector(ScaledNormal)
				|| ScaledNormal.IsNearlyZero(KINDA_SMALL_NUMBER))
			{
				return false;
			}

			const float ScaledW = FVector::DotProduct(ScaledNormal, ScaledPoint);
			const float LocalW = ScaledW - FVector::DotProduct(ScaledNormal, LocalCenter);
			if (!FMath::IsFinite(LocalW))
			{
				return false;
			}

			NewLimit.LocalPlanes.Add(FPlane(ScaledNormal.X, ScaledNormal.Y, ScaledNormal.Z, LocalW));
		}

#if !UE_BUILD_SHIPPING
		if (bBuildDebugGeometry)
		{
			NewLimit.LocalVertices.Reserve(ScaledVertices.Num());
			for (const FVector& ScaledVertex : ScaledVertices)
			{
				NewLimit.LocalVertices.Add(ScaledVertex - LocalCenter);
			}

			struct FDebugConvexEdge
			{
				int32 IndexA = INDEX_NONE;
				int32 IndexB = INDEX_NONE;
				FVector FirstTriangleNormal = FVector::ZeroVector;
				int32 NumTriangles = 0;
				bool bHasNonCoplanarNeighbor = false;
			};

			TMap<uint64, FDebugConvexEdge> UniqueEdges;
			UniqueEdges.Reserve(Indices.Num());
			NewLimit.LocalEdges.Reserve(Indices.Num() * 2);

			const int32 VertexCount = ElemLocalVertices.Num();
			auto AppendEdge = [&UniqueEdges, VertexCount](
				int32 IndexA,
				int32 IndexB,
				const FVector& TriangleNormal)
			{
				if (IndexA == IndexB
					|| IndexA < 0
					|| IndexB < 0
					|| IndexA >= VertexCount
					|| IndexB >= VertexCount)
				{
					return;
				}

				const uint32 MinIndex = static_cast<uint32>(FMath::Min(IndexA, IndexB));
				const uint32 MaxIndex = static_cast<uint32>(FMath::Max(IndexA, IndexB));
				const uint64 EdgeKey = (static_cast<uint64>(MinIndex) << 32) | static_cast<uint64>(MaxIndex);
				if (FDebugConvexEdge* ExistingEdge = UniqueEdges.Find(EdgeKey))
				{
					++ExistingEdge->NumTriangles;
					if (!TriangleNormal.IsNearlyZero()
						&& !ExistingEdge->FirstTriangleNormal.IsNearlyZero()
						&& FMath::Abs(FVector::DotProduct(TriangleNormal, ExistingEdge->FirstTriangleNormal)) < 1.0f - KINDA_SMALL_NUMBER)
					{
						ExistingEdge->bHasNonCoplanarNeighbor = true;
					}
					return;
				}

				FDebugConvexEdge NewEdge;
				NewEdge.IndexA = static_cast<int32>(MinIndex);
				NewEdge.IndexB = static_cast<int32>(MaxIndex);
				NewEdge.FirstTriangleNormal = TriangleNormal;
				NewEdge.NumTriangles = 1;
				UniqueEdges.Add(EdgeKey, NewEdge);
			};

			for (int32 TriangleIndex = 0; TriangleIndex + 2 < Indices.Num(); TriangleIndex += 3)
			{
				const int32 Index0 = Indices[TriangleIndex];
				const int32 Index1 = Indices[TriangleIndex + 1];
				const int32 Index2 = Indices[TriangleIndex + 2];
				if (Index0 < 0
					|| Index1 < 0
					|| Index2 < 0
					|| Index0 >= ScaledVertices.Num()
					|| Index1 >= ScaledVertices.Num()
					|| Index2 >= ScaledVertices.Num())
				{
					continue;
				}

				const FVector TriangleNormal = FVector::CrossProduct(
					ScaledVertices[Index1] - ScaledVertices[Index0],
					ScaledVertices[Index2] - ScaledVertices[Index0]).GetSafeNormal();
				AppendEdge(Index0, Index1, TriangleNormal);
				AppendEdge(Index1, Index2, TriangleNormal);
				AppendEdge(Index2, Index0, TriangleNormal);
			}

			for (const TPair<uint64, FDebugConvexEdge>& EdgePair : UniqueEdges)
			{
				const FDebugConvexEdge& Edge = EdgePair.Value;
				if (Edge.NumTriangles == 1 || Edge.bHasNonCoplanarNeighbor)
				{
					NewLimit.LocalEdges.Add(Edge.IndexA);
					NewLimit.LocalEdges.Add(Edge.IndexB);
				}
			}
		}
#else
		(void)bBuildDebugGeometry;
		(void)Indices;
#endif

		OutLocalLimits.ConvexLimits.Add(MoveTemp(NewLimit));
		return true;
	}

	bool BuildSimpleWorldGroundBox(
		const FVector& ImpactPoint,
		const FVector& ImpactNormal,
		float Radius,
		FBoxLimit& OutBox)
	{
		if (ImpactPoint.ContainsNaN() || ImpactNormal.ContainsNaN() || !FMath::IsFinite(Radius))
		{
			return false;
		}

		const FVector Normal = ImpactNormal.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::UpVector);
		const float ClampedRadius = FMath::Max(0.0f, Radius);

		FBoxLimit NewBox;
		InitializeSimpleWorldLimit(NewBox);
		NewBox.Location = ImpactPoint - Normal * GroundBoxHalfThickness;
		NewBox.Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
		NewBox.Extent = FVector(ClampedRadius, ClampedRadius, GroundBoxHalfThickness);

		OutBox = NewBox;
		return true;
	}

	FBoxLimit MakeSimpleWorldGroundBoxLocal(const FBoxLimit& WorldBox, const FTransform& ComponentTM)
	{
		FBoxLimit Result = WorldBox;
		const FQuat InverseComponentRotation = ComponentTM.GetRotation().Inverse();
		Result.Location = ComponentTM.InverseTransformPosition(WorldBox.Location);
		Result.Rotation = (InverseComponentRotation * WorldBox.Rotation).GetNormalized();
		return Result;
	}

	FBoxLimit TransformSimpleWorldGroundBox(const FBoxLimit& LocalBox, const FTransform& ComponentTM)
	{
		FBoxLimit Result = LocalBox;
		Result.Location = ComponentTM.TransformPosition(LocalBox.Location);
		Result.Rotation = (ComponentTM.GetRotation() * LocalBox.Rotation).GetNormalized();
		return Result;
	}

	void ConvertAggGeomToLocalLimits(
		const FKAggregateGeom& AggGeom,
		const FVector& Scale3D,
		EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape,
		int32 MaxConvexPlanes,
		bool bBuildConvexDebugGeometry,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits)
	{
		OutLocalLimits.SphericalLimits.Reserve(OutLocalLimits.SphericalLimits.Num() + AggGeom.SphereElems.Num());
		// Shape単位のQuery無効設定は静的コンポーネント経路でもここで除外する。
		for (const auto& SphereElem : AggGeom.SphereElems)
		{
			if (!CollisionEnabledHasQuery(SphereElem.GetCollisionEnabled()))
			{
				continue;
			}

			const FKSphereElem ScaledElem = SphereElem.GetFinalScaled(Scale3D, FTransform::Identity);

			FSphericalLimit NewLimit;
			InitializeSimpleWorldLimit(NewLimit);
			NewLimit.Location = ScaledElem.Center;
			NewLimit.Rotation = FQuat::Identity;
			NewLimit.Radius = ScaledElem.Radius;
			NewLimit.LimitType = ESphericalLimitType::Outer;
			OutLocalLimits.SphericalLimits.Add(NewLimit);
		}

		OutLocalLimits.CapsuleLimits.Reserve(OutLocalLimits.CapsuleLimits.Num() + AggGeom.SphylElems.Num());
		for (const auto& CapsuleElem : AggGeom.SphylElems)
		{
			if (!CollisionEnabledHasQuery(CapsuleElem.GetCollisionEnabled()))
			{
				continue;
			}

			const FKSphylElem ScaledElem = CapsuleElem.GetFinalScaled(Scale3D, FTransform::Identity);

			FCapsuleLimit NewLimit;
			InitializeSimpleWorldLimit(NewLimit);
			NewLimit.Location = ScaledElem.Center;
			NewLimit.Rotation = ScaledElem.Rotation.Quaternion();
			NewLimit.Radius = ScaledElem.Radius;
			NewLimit.Length = ScaledElem.Length;
			OutLocalLimits.CapsuleLimits.Add(NewLimit);
		}

		OutLocalLimits.TaperedCapsuleLimits.Reserve(OutLocalLimits.TaperedCapsuleLimits.Num() + AggGeom.TaperedCapsuleElems.Num());
		for (const auto& TaperedCapsuleElem : AggGeom.TaperedCapsuleElems)
		{
			if (!CollisionEnabledHasQuery(TaperedCapsuleElem.GetCollisionEnabled()))
			{
				continue;
			}

			const FKTaperedCapsuleElem ScaledElem = TaperedCapsuleElem.GetFinalScaled(Scale3D, FTransform::Identity);

			FTaperedCapsuleLimit NewLimit;
			InitializeSimpleWorldLimit(NewLimit);
			NewLimit.Location = ScaledElem.Center;
			NewLimit.Rotation = ScaledElem.Rotation.Quaternion();
			NewLimit.Radius0 = ScaledElem.Radius0;
			NewLimit.Radius1 = ScaledElem.Radius1;
			NewLimit.Length = ScaledElem.Length;
			// Cloth用のWidth/bOneSidedCollisionは基本形状では扱わない
			OutLocalLimits.TaperedCapsuleLimits.Add(NewLimit);
		}

		OutLocalLimits.BoxLimits.Reserve(OutLocalLimits.BoxLimits.Num() + AggGeom.BoxElems.Num() + AggGeom.ConvexElems.Num());
		OutLocalLimits.ConvexLimits.Reserve(OutLocalLimits.ConvexLimits.Num() + AggGeom.ConvexElems.Num());
		for (const auto& BoxElem : AggGeom.BoxElems)
		{
			if (!CollisionEnabledHasQuery(BoxElem.GetCollisionEnabled()))
			{
				continue;
			}

			const FKBoxElem ScaledElem = BoxElem.GetFinalScaled(Scale3D, FTransform::Identity);

			FBoxLimit NewLimit;
			InitializeSimpleWorldLimit(NewLimit);
			NewLimit.Location = ScaledElem.Center;
			NewLimit.Rotation = ScaledElem.Rotation.Quaternion();
			NewLimit.Extent = FVector(ScaledElem.X, ScaledElem.Y, ScaledElem.Z) * 0.5f;
			OutLocalLimits.BoxLimits.Add(NewLimit);
		}

		TArray<FPlane> ConvexPlanes;
		for (const auto& ConvexElem : AggGeom.ConvexElems)
		{
			if (!CollisionEnabledHasQuery(ConvexElem.GetCollisionEnabled()))
			{
				continue;
			}

			if (ConvexFallbackShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull)
			{
				ConvexPlanes.Reset();
				ConvexElem.GetPlanes(ConvexPlanes);
				// GetPlanes の平面は ElemTM 適用済みの body 空間なので、ここで ElemTM は再適用しない。
				// 頂点は elem ローカルのため ElemTM を適用する。非一様スケールと Elem 回転の組み合わせでも平面経路は厳密。
				if (AppendConvexElemLocalLimit(
					MakeArrayView(ConvexPlanes),
					MakeArrayView(ConvexElem.VertexData),
					MakeArrayView(ConvexElem.IndexData),
					ConvexElem.GetTransform(),
					Scale3D,
					MaxConvexPlanes,
					bBuildConvexDebugGeometry,
					OutLocalLimits))
				{
					continue;
				}

				// 未クック、上限超過、退化平面などは従来どおり BoundingBox へフォールバックする。
			}

			if (!ConvexElem.ElemBox.IsValid)
			{
				continue;
			}

			const FTransform ElemTM = ConvexElem.GetTransform();
			const FVector ScaleAbs = Scale3D.GetAbs();
			const FVector BoxCenter = ConvexElem.ElemBox.GetCenter();
			const FVector BoxHalfSize = ConvexElem.ElemBox.GetExtent();
			const FVector Location = ElemTM.TransformPosition(BoxCenter) * Scale3D;
			// ConvexのBounds近似。非一様スケールとElem回転の組み合わせは厳密には歪むが、近似として成分ごとに半サイズへスケールを適用する。
			const FVector Extent = BoxHalfSize * ScaleAbs;
			if (!HasUsableExtent(Extent))
			{
				continue;
			}

			if (ConvexFallbackShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull
				|| ConvexFallbackShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox)
			{
				FBoxLimit NewLimit;
				InitializeSimpleWorldLimit(NewLimit);
				NewLimit.Location = Location;
				NewLimit.Rotation = ElemTM.GetRotation();
				NewLimit.Extent = Extent;
				OutLocalLimits.BoxLimits.Add(NewLimit);
			}
			else if (ConvexFallbackShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere)
			{
				FSphericalLimit NewLimit;
				InitializeSimpleWorldLimit(NewLimit);
				NewLimit.Location = Location;
				NewLimit.Rotation = FQuat::Identity;
				NewLimit.Radius = Extent.Size();
				NewLimit.LimitType = ESphericalLimitType::Outer;
				OutLocalLimits.SphericalLimits.Add(NewLimit);
			}
		}

		// LevelSetなど、このPhaseで扱わないAggGeom配列は明示的に無視する
	}

	void AppendLocalLimitsTransformed(
		const FKawaiiPhysicsSharedCollisionData& LocalLimits,
		const FTransform& ComponentTM,
		FKawaiiPhysicsSharedCollisionData& OutWorldLimits,
		float RadiusScale)
	{
		AppendTransformedLimits(
			MakeKawaiiPhysicsSimpleWorldLimitView(LocalLimits.SphericalLimits, 0, LocalLimits.SphericalLimits.Num()),
			ComponentTM, OutWorldLimits.SphericalLimits, RadiusScale);
		AppendTransformedLimits(
			MakeKawaiiPhysicsSimpleWorldLimitView(LocalLimits.CapsuleLimits, 0, LocalLimits.CapsuleLimits.Num()),
			ComponentTM, OutWorldLimits.CapsuleLimits, RadiusScale);
		AppendTransformedLimits(
			MakeKawaiiPhysicsSimpleWorldLimitView(
				LocalLimits.TaperedCapsuleLimits, 0, LocalLimits.TaperedCapsuleLimits.Num()),
			ComponentTM, OutWorldLimits.TaperedCapsuleLimits, RadiusScale);
		AppendTransformedLimits(
			MakeKawaiiPhysicsSimpleWorldLimitView(LocalLimits.BoxLimits, 0, LocalLimits.BoxLimits.Num()),
			ComponentTM, OutWorldLimits.BoxLimits);
		AppendTransformedLimits(
			MakeKawaiiPhysicsSimpleWorldLimitView(LocalLimits.ConvexLimits, 0, LocalLimits.ConvexLimits.Num()),
			ComponentTM, OutWorldLimits.ConvexLimits);
		AppendTransformedPlanarLimits(LocalLimits.PlanarLimits, ComponentTM, OutWorldLimits.PlanarLimits);
	}

	bool AppendBodyLocalLimits(
		const FKAggregateGeom& AggGeom,
		int32 BoneIndex,
		const FVector& Scale3D,
		EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape,
		int32 MaxConvexPlanes,
		bool bBuildConvexDebugGeometry,
		int32 MaxBodies,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits,
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding>& OutBindings)
	{
		if (BoneIndex == INDEX_NONE || MaxBodies <= 0 || OutBindings.Num() >= MaxBodies
			|| IsKawaiiPhysicsSimpleWorldCollisionAggGeomEmpty(AggGeom))
		{
			return false;
		}

		const int32 SphereOffset = OutLocalLimits.SphericalLimits.Num();
		const int32 CapsuleOffset = OutLocalLimits.CapsuleLimits.Num();
		const int32 TaperedCapsuleOffset = OutLocalLimits.TaperedCapsuleLimits.Num();
		const int32 BoxOffset = OutLocalLimits.BoxLimits.Num();
		const int32 ConvexOffset = OutLocalLimits.ConvexLimits.Num();

		ConvertAggGeomToLocalLimits(
			AggGeom,
			Scale3D,
			ConvexFallbackShape,
			MaxConvexPlanes,
			bBuildConvexDebugGeometry,
			OutLocalLimits);

		FKawaiiPhysicsSimpleWorldBodyBinding NewBinding;
		NewBinding.BoneIndex = BoneIndex;
		NewBinding.NumSphericalLimits = OutLocalLimits.SphericalLimits.Num() - SphereOffset;
		NewBinding.NumCapsuleLimits = OutLocalLimits.CapsuleLimits.Num() - CapsuleOffset;
		NewBinding.NumTaperedCapsuleLimits = OutLocalLimits.TaperedCapsuleLimits.Num() - TaperedCapsuleOffset;
		NewBinding.NumBoxLimits = OutLocalLimits.BoxLimits.Num() - BoxOffset;
		NewBinding.NumConvexLimits = OutLocalLimits.ConvexLimits.Num() - ConvexOffset;

		const int32 NumAddedLimits = NewBinding.NumSphericalLimits
			+ NewBinding.NumCapsuleLimits
			+ NewBinding.NumTaperedCapsuleLimits
			+ NewBinding.NumBoxLimits
			+ NewBinding.NumConvexLimits;
		if (NumAddedLimits == 0)
		{
			return false;
		}

		OutBindings.Add(NewBinding);
		return true;
	}

	int32 AppendPhysicsAssetLocalLimits(
		const UPhysicsAsset& PhysicsAsset,
		const FReferenceSkeleton& RefSkeleton,
		const FVector& Scale3D,
		EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape,
		int32 MaxConvexPlanes,
		bool bBuildConvexDebugGeometry,
		int32 MaxBodies,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits,
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding>& OutBindings)
	{
		if (MaxBodies <= 0)
		{
			return 0;
		}

		TArray<FKawaiiPhysicsSimpleWorldPhysicsAssetBodyCandidate> Candidates;
		Candidates.Reserve(PhysicsAsset.SkeletalBodySetups.Num());

		for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset.SkeletalBodySetups.Num(); ++BodyIndex)
		{
			const USkeletalBodySetup* BodySetup = PhysicsAsset.SkeletalBodySetups[BodyIndex];
			if (!BodySetup
				|| BodySetup->CollisionReponse == EBodyCollisionResponse::BodyCollision_Disabled
				|| IsKawaiiPhysicsSimpleWorldCollisionAggGeomEmpty(BodySetup->AggGeom))
			{
				continue;
			}

			const int32 BoneIndex = RefSkeleton.FindBoneIndex(BodySetup->BoneName);
			if (BoneIndex == INDEX_NONE)
			{
				continue;
			}

			FKawaiiPhysicsSimpleWorldPhysicsAssetBodyCandidate Candidate;
			Candidate.BodySetup = BodySetup;
			Candidate.BoneIndex = BoneIndex;
			Candidate.BodyIndex = BodyIndex;
			Candidates.Add(Candidate);
		}

		Candidates.Sort([](
			const FKawaiiPhysicsSimpleWorldPhysicsAssetBodyCandidate& Lhs,
			const FKawaiiPhysicsSimpleWorldPhysicsAssetBodyCandidate& Rhs)
		{
			if (Lhs.BoneIndex != Rhs.BoneIndex)
			{
				return Lhs.BoneIndex < Rhs.BoneIndex;
			}
			return Lhs.BodyIndex < Rhs.BodyIndex;
		});

		const int32 InitialBindingCount = OutBindings.Num();
		for (const FKawaiiPhysicsSimpleWorldPhysicsAssetBodyCandidate& Candidate : Candidates)
		{
			if (OutBindings.Num() >= MaxBodies)
			{
				break;
			}

			AppendBodyLocalLimits(
				Candidate.BodySetup->AggGeom,
				Candidate.BoneIndex,
				Scale3D,
				ConvexFallbackShape,
				MaxConvexPlanes,
				bBuildConvexDebugGeometry,
				MaxBodies,
				OutLocalLimits,
				OutBindings);
		}

		return OutBindings.Num() - InitialBindingCount;
	}

	int32 UpdateSkeletalBodyWorldTransforms(
		TArrayView<const FKawaiiPhysicsSimpleWorldBodyBinding> Bindings,
		TArrayView<const FTransform> ComponentSpaceTransforms,
		const FTransform& ComponentTM,
		TArray<FTransform>& OutBodyWorldTMs)
	{
		OutBodyWorldTMs.SetNum(Bindings.Num());

		int32 NumMissingBones = 0;
		for (int32 BindingIndex = 0; BindingIndex < Bindings.Num(); ++BindingIndex)
		{
			const int32 BoneIndex = Bindings[BindingIndex].BoneIndex;
			if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num())
			{
				OutBodyWorldTMs[BindingIndex] = FTransform::Identity;
				++NumMissingBones;
				continue;
			}

			FTransform BodyWorldTM = ComponentSpaceTransforms[BoneIndex] * ComponentTM;
			// コンポーネントスケールは収集時に形状サイズへ焼き込み、ここではbone平行移動へだけ反映する。
			// アニメ由来のボーンスケールは形状サイズへ反映しない。
			BodyWorldTM.SetScale3D(FVector::OneVector);
			OutBodyWorldTMs[BindingIndex] = BodyWorldTM;
		}

		return NumMissingBones;
	}

	void AppendFadedSkeletalLocalLimits(
		const FKawaiiPhysicsSharedCollisionData& LocalLimits,
		TArrayView<const FKawaiiPhysicsSimpleWorldBodyBinding> Bindings,
		TArrayView<const FTransform> BodyWorldTMs,
		float FadeAlpha,
		FKawaiiPhysicsSharedCollisionData& OutWorldLimits,
		float BoxEnableThreshold)
	{
		int32 SphereOffset = 0;
		int32 CapsuleOffset = 0;
		int32 TaperedCapsuleOffset = 0;
		int32 BoxOffset = 0;
		int32 ConvexOffset = 0;

		const int32 NumBodies = FMath::Min(Bindings.Num(), BodyWorldTMs.Num());
		for (int32 BodyIndex = 0; BodyIndex < NumBodies; ++BodyIndex)
		{
			const FKawaiiPhysicsSimpleWorldBodyBinding& Binding = Bindings[BodyIndex];
			const FTransform& BodyWorldTM = BodyWorldTMs[BodyIndex];

			AppendTransformedLimits(
				MakeKawaiiPhysicsSimpleWorldLimitView(
					LocalLimits.SphericalLimits, SphereOffset, Binding.NumSphericalLimits),
				BodyWorldTM,
				OutWorldLimits.SphericalLimits,
				FadeAlpha);
			AppendTransformedLimits(
				MakeKawaiiPhysicsSimpleWorldLimitView(
					LocalLimits.CapsuleLimits, CapsuleOffset, Binding.NumCapsuleLimits),
				BodyWorldTM,
				OutWorldLimits.CapsuleLimits,
				FadeAlpha);
			AppendTransformedLimits(
				MakeKawaiiPhysicsSimpleWorldLimitView(
					LocalLimits.TaperedCapsuleLimits, TaperedCapsuleOffset, Binding.NumTaperedCapsuleLimits),
				BodyWorldTM,
				OutWorldLimits.TaperedCapsuleLimits,
				FadeAlpha);
			if (FadeAlpha >= BoxEnableThreshold)
			{
				AppendTransformedLimits(
					MakeKawaiiPhysicsSimpleWorldLimitView(
						LocalLimits.BoxLimits, BoxOffset, Binding.NumBoxLimits),
					BodyWorldTM,
					OutWorldLimits.BoxLimits);
				// Convex は半径縮小できないため、Box と同じしきい値ゲートを共用する。
				AppendTransformedLimits(
					MakeKawaiiPhysicsSimpleWorldLimitView(
						LocalLimits.ConvexLimits, ConvexOffset, Binding.NumConvexLimits),
					BodyWorldTM,
					OutWorldLimits.ConvexLimits);
			}

			SphereOffset += Binding.NumSphericalLimits;
			CapsuleOffset += Binding.NumCapsuleLimits;
			TaperedCapsuleOffset += Binding.NumTaperedCapsuleLimits;
			BoxOffset += Binding.NumBoxLimits;
			ConvexOffset += Binding.NumConvexLimits;
		}
	}

	void AppendFadedLocalLimits(
		const FKawaiiPhysicsSharedCollisionData& LocalLimits,
		float FadeAlpha,
		const FTransform& ComponentTM,
		FKawaiiPhysicsSharedCollisionData& OutWorldLimits,
		float BoxEnableThreshold)
	{
		// フェード係数はワールドLimitへの追記時に適用し、ローカルLimit全体のコピーを避ける。
		// Spheres/Capsules/TaperedCapsules は半径のみ縮小し、Boxes は一定Alphaまでpublishしない。
		AppendTransformedLimits(
			MakeKawaiiPhysicsSimpleWorldLimitView(LocalLimits.SphericalLimits, 0, LocalLimits.SphericalLimits.Num()),
			ComponentTM, OutWorldLimits.SphericalLimits, FadeAlpha);
		AppendTransformedLimits(
			MakeKawaiiPhysicsSimpleWorldLimitView(LocalLimits.CapsuleLimits, 0, LocalLimits.CapsuleLimits.Num()),
			ComponentTM, OutWorldLimits.CapsuleLimits, FadeAlpha);
		AppendTransformedLimits(
			MakeKawaiiPhysicsSimpleWorldLimitView(
				LocalLimits.TaperedCapsuleLimits, 0, LocalLimits.TaperedCapsuleLimits.Num()),
			ComponentTM, OutWorldLimits.TaperedCapsuleLimits, FadeAlpha);
		if (FadeAlpha >= BoxEnableThreshold)
		{
			AppendTransformedLimits(
				MakeKawaiiPhysicsSimpleWorldLimitView(LocalLimits.BoxLimits, 0, LocalLimits.BoxLimits.Num()),
				ComponentTM, OutWorldLimits.BoxLimits);
			// Convex は半径縮小できないため、Box と同じしきい値ゲートを共用する。
			AppendTransformedLimits(
				MakeKawaiiPhysicsSimpleWorldLimitView(LocalLimits.ConvexLimits, 0, LocalLimits.ConvexLimits.Num()),
				ComponentTM, OutWorldLimits.ConvexLimits);
		}
		AppendTransformedPlanarLimits(LocalLimits.PlanarLimits, ComponentTM, OutWorldLimits.PlanarLimits);
	}

	bool ComputeSimpleWorldGatherBounds(
		TArrayView<const FBoxSphereBounds> MemberBounds,
		FBoxSphereBounds& OutBounds)
	{
		if (MemberBounds.IsEmpty())
		{
			return false;
		}

		OutBounds = MemberBounds[0];
		for (int32 Index = 1; Index < MemberBounds.Num(); ++Index)
		{
			OutBounds = OutBounds + MemberBounds[Index];
		}
		return true;
	}

	FKawaiiPhysicsSimpleWorldCollisionDesc BuildSimpleWorldCollisionDesc(
		const FKawaiiPhysicsSimpleWorldCollisionSettings& Settings)
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		Desc.GatherIntervalSec = Settings.GatherInterval;
		Desc.GatherRadiusOverride = Settings.bOverrideGatherRadius ? Settings.GatherRadius : 0.0f;
		Desc.CollisionChannel = Settings.bOverrideCollisionChannel ? Settings.CollisionChannel.GetValue() : ECC_MAX;
		Desc.ObjectTypes = Settings.ObjectTypes;
		Desc.ConvexFallbackShape = Settings.ConvexFallbackShape;
		Desc.SkeletalMeshCollision = Settings.SkeletalMeshCollision;
		Desc.bGroundCollision = Settings.bGroundCollision;
		Desc.GatherScope = Settings.GatherScope;
		Desc.bGatherFamilyMembers = Settings.bGatherFamilyMembers;
		Desc.bProviderDisabled = !Settings.bEnabled;
		return Desc;
	}
}
