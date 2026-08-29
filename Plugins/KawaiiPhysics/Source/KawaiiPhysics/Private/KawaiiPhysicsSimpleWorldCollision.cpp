// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSimpleWorldCollision.h"

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

	template <typename LimitType>
	void AppendTransformedLimits(
		const TArray<LimitType>& LocalLimits,
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
}

namespace KawaiiPhysicsSimpleWorldCollision
{
	void AppendBoundsLocalLimits(
		const FBoxSphereBounds& Bounds,
		const FTransform& ComponentTM,
		EKawaiiPhysicsComplexShapeApproximation ApproxMode,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits)
	{
		if (ApproxMode == EKawaiiPhysicsComplexShapeApproximation::Ignore)
		{
			return;
		}

		const FVector LocalCenter = ComponentTM.InverseTransformPosition(Bounds.Origin);
		if (ApproxMode == EKawaiiPhysicsComplexShapeApproximation::BoxBounds)
		{
			FBoxLimit NewLimit;
			InitializeSimpleWorldLimit(NewLimit);
			NewLimit.Location = LocalCenter;
			NewLimit.Rotation = ComponentTM.GetRotation().Inverse();
			NewLimit.Extent = Bounds.BoxExtent;
			OutLocalLimits.BoxLimits.Add(NewLimit);
		}
		else if (ApproxMode == EKawaiiPhysicsComplexShapeApproximation::SphereBounds)
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

	void ConvertAggGeomToLocalLimits(
		const FKAggregateGeom& AggGeom,
		const FVector& Scale3D,
		EKawaiiPhysicsComplexShapeApproximation ApproxMode,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits)
	{
		OutLocalLimits.SphericalLimits.Reserve(OutLocalLimits.SphericalLimits.Num() + AggGeom.SphereElems.Num());
		for (const auto& SphereElem : AggGeom.SphereElems)
		{
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
		for (const auto& BoxElem : AggGeom.BoxElems)
		{
			const FKBoxElem ScaledElem = BoxElem.GetFinalScaled(Scale3D, FTransform::Identity);

			FBoxLimit NewLimit;
			InitializeSimpleWorldLimit(NewLimit);
			NewLimit.Location = ScaledElem.Center;
			NewLimit.Rotation = ScaledElem.Rotation.Quaternion();
			NewLimit.Extent = FVector(ScaledElem.X, ScaledElem.Y, ScaledElem.Z) * 0.5f;
			OutLocalLimits.BoxLimits.Add(NewLimit);
		}

		for (const auto& ConvexElem : AggGeom.ConvexElems)
		{
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

			if (ApproxMode == EKawaiiPhysicsComplexShapeApproximation::BoxBounds)
			{
				FBoxLimit NewLimit;
				InitializeSimpleWorldLimit(NewLimit);
				NewLimit.Location = Location;
				NewLimit.Rotation = ElemTM.GetRotation();
				NewLimit.Extent = Extent;
				OutLocalLimits.BoxLimits.Add(NewLimit);
			}
			else if (ApproxMode == EKawaiiPhysicsComplexShapeApproximation::SphereBounds)
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
		AppendTransformedLimits(LocalLimits.SphericalLimits, ComponentTM, OutWorldLimits.SphericalLimits, RadiusScale);
		AppendTransformedLimits(LocalLimits.CapsuleLimits, ComponentTM, OutWorldLimits.CapsuleLimits, RadiusScale);
		AppendTransformedLimits(LocalLimits.TaperedCapsuleLimits, ComponentTM, OutWorldLimits.TaperedCapsuleLimits, RadiusScale);
		AppendTransformedLimits(LocalLimits.BoxLimits, ComponentTM, OutWorldLimits.BoxLimits);
		AppendTransformedPlanarLimits(LocalLimits.PlanarLimits, ComponentTM, OutWorldLimits.PlanarLimits);
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
		AppendTransformedLimits(LocalLimits.SphericalLimits, ComponentTM, OutWorldLimits.SphericalLimits, FadeAlpha);
		AppendTransformedLimits(LocalLimits.CapsuleLimits, ComponentTM, OutWorldLimits.CapsuleLimits, FadeAlpha);
		AppendTransformedLimits(LocalLimits.TaperedCapsuleLimits, ComponentTM, OutWorldLimits.TaperedCapsuleLimits, FadeAlpha);
		if (FadeAlpha >= BoxEnableThreshold)
		{
			AppendTransformedLimits(LocalLimits.BoxLimits, ComponentTM, OutWorldLimits.BoxLimits);
		}
		AppendTransformedPlanarLimits(LocalLimits.PlanarLimits, ComponentTM, OutWorldLimits.PlanarLimits);
	}
}
