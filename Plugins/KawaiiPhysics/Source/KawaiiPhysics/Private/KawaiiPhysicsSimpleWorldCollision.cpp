// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSimpleWorldCollision.h"

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
		if (BoundsShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox)
		{
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

	void ConvertAggGeomToLocalLimits(
		const FKAggregateGeom& AggGeom,
		const FVector& Scale3D,
		EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape,
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

		for (const auto& ConvexElem : AggGeom.ConvexElems)
		{
			if (!CollisionEnabledHasQuery(ConvexElem.GetCollisionEnabled()))
			{
				continue;
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

			if (ConvexFallbackShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox)
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
		AppendTransformedPlanarLimits(LocalLimits.PlanarLimits, ComponentTM, OutWorldLimits.PlanarLimits);
	}

	bool AppendBodyLocalLimits(
		const FKAggregateGeom& AggGeom,
		int32 BoneIndex,
		const FVector& Scale3D,
		EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape,
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

		ConvertAggGeomToLocalLimits(AggGeom, Scale3D, ConvexFallbackShape, OutLocalLimits);

		FKawaiiPhysicsSimpleWorldBodyBinding NewBinding;
		NewBinding.BoneIndex = BoneIndex;
		NewBinding.NumSphericalLimits = OutLocalLimits.SphericalLimits.Num() - SphereOffset;
		NewBinding.NumCapsuleLimits = OutLocalLimits.CapsuleLimits.Num() - CapsuleOffset;
		NewBinding.NumTaperedCapsuleLimits = OutLocalLimits.TaperedCapsuleLimits.Num() - TaperedCapsuleOffset;
		NewBinding.NumBoxLimits = OutLocalLimits.BoxLimits.Num() - BoxOffset;

		const int32 NumAddedLimits = NewBinding.NumSphericalLimits
			+ NewBinding.NumCapsuleLimits
			+ NewBinding.NumTaperedCapsuleLimits
			+ NewBinding.NumBoxLimits;
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
			}

			SphereOffset += Binding.NumSphericalLimits;
			CapsuleOffset += Binding.NumCapsuleLimits;
			TaperedCapsuleOffset += Binding.NumTaperedCapsuleLimits;
			BoxOffset += Binding.NumBoxLimits;
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
		}
		AppendTransformedPlanarLimits(LocalLimits.PlanarLimits, ComponentTM, OutWorldLimits.PlanarLimits);
	}
}
