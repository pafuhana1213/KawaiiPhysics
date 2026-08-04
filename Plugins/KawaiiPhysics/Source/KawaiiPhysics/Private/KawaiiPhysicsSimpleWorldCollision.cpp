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

	template <typename LimitType>
	void AppendTransformedLimits(
		const TArray<LimitType>& LocalLimits,
		const FTransform& ComponentTM,
		TArray<LimitType>& OutWorldLimits)
	{
		OutWorldLimits.Reserve(OutWorldLimits.Num() + LocalLimits.Num());

		const FQuat ComponentRotation = ComponentTM.GetRotation();
		for (const auto& LocalLimit : LocalLimits)
		{
			auto WorldLimit = LocalLimit;
			WorldLimit.Location = ComponentTM.TransformPosition(LocalLimit.Location);
			WorldLimit.Rotation = ComponentRotation * LocalLimit.Rotation;
			WorldLimit.Rotation.Normalize();
			OutWorldLimits.Add(WorldLimit);
		}
	}
}

namespace KawaiiPhysicsSimpleWorldCollision
{
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
		FKawaiiPhysicsSharedCollisionData& OutWorldLimits)
	{
		AppendTransformedLimits(LocalLimits.SphericalLimits, ComponentTM, OutWorldLimits.SphericalLimits);
		AppendTransformedLimits(LocalLimits.CapsuleLimits, ComponentTM, OutWorldLimits.CapsuleLimits);
		AppendTransformedLimits(LocalLimits.TaperedCapsuleLimits, ComponentTM, OutWorldLimits.TaperedCapsuleLimits);
		AppendTransformedLimits(LocalLimits.BoxLimits, ComponentTM, OutWorldLimits.BoxLimits);

		OutWorldLimits.PlanarLimits.Reserve(OutWorldLimits.PlanarLimits.Num() + LocalLimits.PlanarLimits.Num());
		const FQuat ComponentRotation = ComponentTM.GetRotation();
		for (const auto& LocalLimit : LocalLimits.PlanarLimits)
		{
			auto WorldLimit = LocalLimit;
			WorldLimit.Location = ComponentTM.TransformPosition(LocalLimit.Location);
			WorldLimit.Rotation = ComponentRotation * LocalLimit.Rotation;
			WorldLimit.Rotation.Normalize();
			WorldLimit.Plane = FPlane(WorldLimit.Location, WorldLimit.Rotation.GetUpVector());
			OutWorldLimits.PlanarLimits.Add(WorldLimit);
		}
	}

	void AppendFadedLocalLimits(
		const FKawaiiPhysicsSharedCollisionData& LocalLimits,
		float FadeAlpha,
		const FTransform& ComponentTM,
		FKawaiiPhysicsSharedCollisionData& OutWorldLimits,
		float BoxEnableThreshold)
	{
		// フェードはローカルLimitのコピーに適用してからワールド変換する。
		// Spheres/Capsules/TaperedCapsules は半径のみ縮小し、Boxes は一定Alphaまでpublishしない。
		FKawaiiPhysicsSharedCollisionData FadedLimits = LocalLimits;
		for (FSphericalLimit& Limit : FadedLimits.SphericalLimits)
		{
			Limit.Radius *= FadeAlpha;
		}
		for (FCapsuleLimit& Limit : FadedLimits.CapsuleLimits)
		{
			Limit.Radius *= FadeAlpha;
		}
		for (FTaperedCapsuleLimit& Limit : FadedLimits.TaperedCapsuleLimits)
		{
			Limit.Radius0 *= FadeAlpha;
			Limit.Radius1 *= FadeAlpha;
		}
		if (FadeAlpha < BoxEnableThreshold)
		{
			FadedLimits.BoxLimits.Reset();
		}

		AppendLocalLimitsTransformed(FadedLimits, ComponentTM, OutWorldLimits);
	}
}
