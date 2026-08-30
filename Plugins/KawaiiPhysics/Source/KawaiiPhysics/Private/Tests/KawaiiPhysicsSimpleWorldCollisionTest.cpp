// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsSimpleWorldCollision.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/EngineVersionComparison.h"
#include "PhysicsEngine/PhysicsAsset.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"

#include <limits>

// シンプルワールドコリジョン（KawaiiPhysicsSimpleWorldCollision namespace / SharedCollisionSubsystem の関連構造体）の単体テスト。
// AggGeom→Limit変換、ローカル→ワールド変換、フェード、Desc Merge、Entryのライフサイクル、ハーネス経由のpush-out統合を検証する。

namespace
{
	constexpr float GSimpleWorldTol = 0.001f;
	constexpr float GSimpleWorldPushOutTol = 0.01f; // 0.1mm スケール（他コリジョンテストと同じ粒度）
}

// ---------------------------------------------------------------------------
//  BuildGroundBox
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldBuildGroundBoxTest,
                                 "KawaiiPhysics.SimpleWorld.BuildGroundBox",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldBuildGroundBoxTest::RunTest(const FString& Parameters)
{
	using KawaiiPhysicsSimpleWorldCollision::GroundBoxHalfThickness;

	const FVector ImpactPoint(100.0f, 200.0f, 50.0f);

	{
		FBoxLimit OutBox;
		const bool bBuilt = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
			ImpactPoint, FVector::UpVector, 300.0f, OutBox);

		TestTrue(TEXT("Up normal builds a ground box"), bBuilt);
		TestTrue(TEXT("Up normal location offsets by half thickness"),
		         OutBox.Location.Equals(FVector(100.0f, 200.0f, 40.0f), GSimpleWorldTol));
		TestTrue(TEXT("Up normal extent uses radius and half thickness"),
		         OutBox.Extent.Equals(FVector(300.0f, 300.0f, 10.0f), GSimpleWorldTol));
		TestTrue(TEXT("Up normal rotation is identity"), OutBox.Rotation.Equals(FQuat::Identity, GSimpleWorldTol));
		TestTrue(TEXT("Ground box is enabled and sourced from SimpleWorld"),
		         OutBox.bEnable && OutBox.SourceType == ECollisionSourceType::SimpleWorld);
	}

	{
		const FVector TiltedNormal(
			FMath::Sin(FMath::DegreesToRadians(30.0f)),
			0.0f,
			FMath::Cos(FMath::DegreesToRadians(30.0f)));
		FBoxLimit OutBox;
		const bool bBuilt = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
			ImpactPoint, TiltedNormal, 300.0f, OutBox);

		TestTrue(TEXT("Tilted normal builds a ground box"), bBuilt);
		TestTrue(TEXT("Tilted normal rotation up vector matches normal"),
		         OutBox.Rotation.GetUpVector().Equals(TiltedNormal, 0.0001f));
		TestTrue(TEXT("Tilted normal location offsets along normal"),
		         OutBox.Location.Equals(ImpactPoint - TiltedNormal * GroundBoxHalfThickness, GSimpleWorldTol));
	}

	{
		FBoxLimit OutBox;
		const bool bBuilt = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
			ImpactPoint, FVector::UpVector, 0.0f, OutBox);

		TestTrue(TEXT("Zero radius builds a ground box"), bBuilt);
		TestTrue(TEXT("Zero radius clamps XY extent to zero"),
		         OutBox.Extent.Equals(FVector(0.0f, 0.0f, 10.0f), GSimpleWorldTol));
		TestFalse(TEXT("Zero radius output has no NaN"), OutBox.Location.ContainsNaN() || OutBox.Extent.ContainsNaN());
	}

	{
		FBoxLimit OutBox;
		const bool bBuilt = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
			ImpactPoint, FVector::ZeroVector, 300.0f, OutBox);

		TestTrue(TEXT("Zero normal builds a ground box"), bBuilt);
		TestTrue(TEXT("Zero normal falls back to up"),
		         OutBox.Rotation.GetUpVector().Equals(FVector::UpVector, GSimpleWorldTol));
	}

	{
		FBoxLimit SentinelBox;
		SentinelBox.Location = FVector(1.0f, 2.0f, 3.0f);
		SentinelBox.Rotation = FRotator(10.0f, 20.0f, 30.0f).Quaternion();
		SentinelBox.Extent = FVector(4.0f, 5.0f, 6.0f);
		SentinelBox.bEnable = false;
		SentinelBox.SourceType = ECollisionSourceType::DataAsset;

		FBoxLimit OutBox = SentinelBox;
		const FVector NaNPoint(std::numeric_limits<float>::quiet_NaN(), 200.0f, 50.0f);
		const bool bBuilt = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
			NaNPoint, FVector::UpVector, 300.0f, OutBox);

		TestFalse(TEXT("NaN impact point is rejected"), bBuilt);
		TestTrue(TEXT("Rejected input leaves location unchanged"), OutBox.Location.Equals(SentinelBox.Location));
		TestTrue(TEXT("Rejected input leaves rotation unchanged"), OutBox.Rotation.Equals(SentinelBox.Rotation));
		TestTrue(TEXT("Rejected input leaves extent unchanged"), OutBox.Extent.Equals(SentinelBox.Extent));
		TestTrue(TEXT("Rejected input leaves flags unchanged"),
		         OutBox.bEnable == SentinelBox.bEnable && OutBox.SourceType == SentinelBox.SourceType);
	}

	{
		FBoxLimit OutBox;
		const bool bBuilt = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
			ImpactPoint, FVector::UpVector, -50.0f, OutBox);

		TestTrue(TEXT("Negative radius builds a ground box"), bBuilt);
		TestTrue(TEXT("Negative radius clamps XY extent to zero"),
		         OutBox.Extent.Equals(FVector(0.0f, 0.0f, 10.0f), GSimpleWorldTol));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  ConvertAggGeomToLocalLimits
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldConvertAggGeomTest,
                                 "KawaiiPhysics.SimpleWorld.ConvertAggGeomToLocalLimits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldConvertAggGeomTest::RunTest(const FString& Parameters)
{
	// --- 等倍スケールでの基本マッピング（Sphere/Sphyl/Box/TaperedCapsule 各1elem） ---
	{
		FKAggregateGeom AggGeom;

		FKSphereElem SphereElem;
		SphereElem.Center = FVector(1.0f, 2.0f, 3.0f);
		SphereElem.Radius = 5.0f;
		AggGeom.SphereElems.Add(SphereElem);

		FKSphylElem SphylElem;
		SphylElem.Center = FVector(4.0f, 5.0f, 6.0f);
		SphylElem.Rotation = FRotator(10.0f, 20.0f, 30.0f);
		SphylElem.Radius = 2.0f;
		SphylElem.Length = 8.0f;
		AggGeom.SphylElems.Add(SphylElem);

		FKBoxElem BoxElem;
		BoxElem.Center = FVector(7.0f, 8.0f, 9.0f);
		BoxElem.Rotation = FRotator(0.0f, 45.0f, 0.0f);
		BoxElem.X = 4.0f;
		BoxElem.Y = 6.0f;
		BoxElem.Z = 8.0f;
		AggGeom.BoxElems.Add(BoxElem);

		FKTaperedCapsuleElem TaperedElem;
		TaperedElem.Center = FVector(10.0f, 11.0f, 12.0f);
		TaperedElem.Rotation = FRotator(0.0f, 0.0f, 90.0f);
		TaperedElem.Radius0 = 3.0f;
		TaperedElem.Radius1 = 1.5f;
		TaperedElem.Length = 6.0f;
		AggGeom.TaperedCapsuleElems.Add(TaperedElem);

		FKawaiiPhysicsSharedCollisionData OutLimits;
		KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
			AggGeom, FVector::OneVector, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, OutLimits);

		TestTrue(TEXT("Sphere elem maps to exactly one spherical limit"), OutLimits.SphericalLimits.Num() == 1);
		TestTrue(TEXT("Sphyl elem maps to exactly one capsule limit"), OutLimits.CapsuleLimits.Num() == 1);
		TestTrue(TEXT("Box elem maps to exactly one box limit"), OutLimits.BoxLimits.Num() == 1);
		TestTrue(TEXT("TaperedCapsule elem maps to exactly one tapered capsule limit"),
		         OutLimits.TaperedCapsuleLimits.Num() == 1);

		const FSphericalLimit& SphereLimit = OutLimits.SphericalLimits[0];
		TestTrue(TEXT("Sphere limit is enabled and sourced from SimpleWorld"),
		         SphereLimit.bEnable && SphereLimit.SourceType == ECollisionSourceType::SimpleWorld);
		TestTrue(TEXT("Sphere limit location"), SphereLimit.Location.Equals(FVector(1, 2, 3), GSimpleWorldTol));
		TestTrue(TEXT("Sphere limit radius"), FMath::IsNearlyEqual(SphereLimit.Radius, 5.0f, GSimpleWorldTol));

		const FCapsuleLimit& CapsuleLimit = OutLimits.CapsuleLimits[0];
		TestTrue(TEXT("Capsule limit is enabled and sourced from SimpleWorld"),
		         CapsuleLimit.bEnable && CapsuleLimit.SourceType == ECollisionSourceType::SimpleWorld);
		TestTrue(TEXT("Capsule limit location"), CapsuleLimit.Location.Equals(FVector(4, 5, 6), GSimpleWorldTol));
		TestTrue(TEXT("Capsule limit rotation"),
		         CapsuleLimit.Rotation.Equals(FRotator(10, 20, 30).Quaternion(), GSimpleWorldTol));
		TestTrue(TEXT("Capsule limit radius/length"),
		         FMath::IsNearlyEqual(CapsuleLimit.Radius, 2.0f, GSimpleWorldTol) &&
		         FMath::IsNearlyEqual(CapsuleLimit.Length, 8.0f, GSimpleWorldTol));

		const FBoxLimit& BoxLimit = OutLimits.BoxLimits[0];
		TestTrue(TEXT("Box limit is enabled and sourced from SimpleWorld"),
		         BoxLimit.bEnable && BoxLimit.SourceType == ECollisionSourceType::SimpleWorld);
		TestTrue(TEXT("Box limit location"), BoxLimit.Location.Equals(FVector(7, 8, 9), GSimpleWorldTol));
		TestTrue(TEXT("Box limit rotation"),
		         BoxLimit.Rotation.Equals(FRotator(0, 45, 0).Quaternion(), GSimpleWorldTol));
		TestTrue(TEXT("Box limit extent is half of X/Y/Z"), BoxLimit.Extent.Equals(FVector(2, 3, 4), GSimpleWorldTol));

		const FTaperedCapsuleLimit& TaperedLimit = OutLimits.TaperedCapsuleLimits[0];
		TestTrue(TEXT("TaperedCapsule limit is enabled and sourced from SimpleWorld"),
		         TaperedLimit.bEnable && TaperedLimit.SourceType == ECollisionSourceType::SimpleWorld);
		TestTrue(TEXT("TaperedCapsule limit location"),
		         TaperedLimit.Location.Equals(FVector(10, 11, 12), GSimpleWorldTol));
		TestTrue(TEXT("TaperedCapsule limit rotation"),
		         TaperedLimit.Rotation.Equals(FRotator(0, 0, 90).Quaternion(), GSimpleWorldTol));
		TestTrue(TEXT("TaperedCapsule limit radii/length"),
		         FMath::IsNearlyEqual(TaperedLimit.Radius0, 3.0f, GSimpleWorldTol) &&
		         FMath::IsNearlyEqual(TaperedLimit.Radius1, 1.5f, GSimpleWorldTol) &&
		         FMath::IsNearlyEqual(TaperedLimit.Length, 6.0f, GSimpleWorldTol));
	}

	// --- 非一様スケール(2,1,0.5)。期待値は FK*Elem::GetFinalScaled を直接呼んで得た値と突き合わせる（回帰検知目的） ---
	{
		const FVector Scale(2.0f, 1.0f, 0.5f);

		FKSphereElem SphereElem;
		SphereElem.Center = FVector(2.0f, -3.0f, 4.0f);
		SphereElem.Radius = 6.0f;
		const FKSphereElem ExpectedSphere = SphereElem.GetFinalScaled(Scale, FTransform::Identity);

		FKSphylElem SphylElem;
		SphylElem.Center = FVector(1.0f, 1.0f, 1.0f);
		SphylElem.Rotation = FRotator(15.0f, -25.0f, 35.0f);
		SphylElem.Radius = 3.0f;
		SphylElem.Length = 10.0f;
		const FKSphylElem ExpectedSphyl = SphylElem.GetFinalScaled(Scale, FTransform::Identity);

		FKBoxElem BoxElem;
		BoxElem.Center = FVector(-2.0f, 3.0f, -4.0f);
		BoxElem.Rotation = FRotator(5.0f, 10.0f, 15.0f);
		BoxElem.X = 4.0f;
		BoxElem.Y = 6.0f;
		BoxElem.Z = 2.0f;
		const FKBoxElem ExpectedBox = BoxElem.GetFinalScaled(Scale, FTransform::Identity);

		FKTaperedCapsuleElem TaperedElem;
		TaperedElem.Center = FVector(0.0f, 2.0f, -2.0f);
		TaperedElem.Rotation = FRotator(0.0f, 90.0f, 0.0f);
		TaperedElem.Radius0 = 4.0f;
		TaperedElem.Radius1 = 2.0f;
		TaperedElem.Length = 12.0f;
		const FKTaperedCapsuleElem ExpectedTapered = TaperedElem.GetFinalScaled(Scale, FTransform::Identity);

		FKAggregateGeom AggGeom;
		AggGeom.SphereElems.Add(SphereElem);
		AggGeom.SphylElems.Add(SphylElem);
		AggGeom.BoxElems.Add(BoxElem);
		AggGeom.TaperedCapsuleElems.Add(TaperedElem);

		FKawaiiPhysicsSharedCollisionData OutLimits;
		KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
			AggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, OutLimits);

		const FSphericalLimit& SphereLimit = OutLimits.SphericalLimits[0];
		TestTrue(TEXT("Non-uniform scale: sphere location matches GetFinalScaled"),
		         SphereLimit.Location.Equals(ExpectedSphere.Center, GSimpleWorldTol));
		TestTrue(TEXT("Non-uniform scale: sphere radius matches GetFinalScaled"),
		         FMath::IsNearlyEqual(SphereLimit.Radius, ExpectedSphere.Radius, GSimpleWorldTol));

		const FCapsuleLimit& CapsuleLimit = OutLimits.CapsuleLimits[0];
		TestTrue(TEXT("Non-uniform scale: capsule location matches GetFinalScaled"),
		         CapsuleLimit.Location.Equals(ExpectedSphyl.Center, GSimpleWorldTol));
		TestTrue(TEXT("Non-uniform scale: capsule rotation matches GetFinalScaled"),
		         CapsuleLimit.Rotation.Equals(ExpectedSphyl.Rotation.Quaternion(), GSimpleWorldTol));
		TestTrue(TEXT("Non-uniform scale: capsule radius/length matches GetFinalScaled"),
		         FMath::IsNearlyEqual(CapsuleLimit.Radius, ExpectedSphyl.Radius, GSimpleWorldTol) &&
		         FMath::IsNearlyEqual(CapsuleLimit.Length, ExpectedSphyl.Length, GSimpleWorldTol));

		const FBoxLimit& BoxLimit = OutLimits.BoxLimits[0];
		TestTrue(TEXT("Non-uniform scale: box location matches GetFinalScaled"),
		         BoxLimit.Location.Equals(ExpectedBox.Center, GSimpleWorldTol));
		TestTrue(TEXT("Non-uniform scale: box rotation matches GetFinalScaled"),
		         BoxLimit.Rotation.Equals(ExpectedBox.Rotation.Quaternion(), GSimpleWorldTol));
		TestTrue(TEXT("Non-uniform scale: box extent is half of GetFinalScaled size"),
		         BoxLimit.Extent.Equals(FVector(ExpectedBox.X, ExpectedBox.Y, ExpectedBox.Z) * 0.5f, GSimpleWorldTol));

		const FTaperedCapsuleLimit& TaperedLimit = OutLimits.TaperedCapsuleLimits[0];
		TestTrue(TEXT("Non-uniform scale: tapered capsule location matches GetFinalScaled"),
		         TaperedLimit.Location.Equals(ExpectedTapered.Center, GSimpleWorldTol));
		TestTrue(TEXT("Non-uniform scale: tapered capsule rotation matches GetFinalScaled"),
		         TaperedLimit.Rotation.Equals(ExpectedTapered.Rotation.Quaternion(), GSimpleWorldTol));
		TestTrue(TEXT("Non-uniform scale: tapered capsule radii/length matches GetFinalScaled"),
		         FMath::IsNearlyEqual(TaperedLimit.Radius0, ExpectedTapered.Radius0, GSimpleWorldTol) &&
		         FMath::IsNearlyEqual(TaperedLimit.Radius1, ExpectedTapered.Radius1, GSimpleWorldTol) &&
		         FMath::IsNearlyEqual(TaperedLimit.Length, ExpectedTapered.Length, GSimpleWorldTol));
	}

	// --- Convex（ElemBox 有効・Elem Transform 付き）: BoundingBox/BoundingSphere/None の3分岐 ---
	{
		const FVector Scale(2.0f, 1.0f, 0.5f);

		FKConvexElem ConvexElem;
		ConvexElem.SetTransform(FTransform(FQuat::Identity, FVector(1.0f, 2.0f, 3.0f)));
		ConvexElem.ElemBox = FBox(FVector(-4.0f, -2.0f, -1.0f), FVector(4.0f, 2.0f, 1.0f));

		// Location = ElemTM.TransformPosition(BoxCenter) * Scale3D = TransformPosition(0,0,0) * Scale = (1,2,3)*(2,1,0.5) で算出される
		const FVector ExpectedLocation(2.0f, 2.0f, 1.5f);
		// Extent = BoxHalfSize(4,2,1) * ScaleAbs(2,1,0.5) で算出される
		const FVector ExpectedExtent(8.0f, 2.0f, 0.5f);

		// BoundingBox近似: FBoxLimitを生成
		{
			FKAggregateGeom AggGeom;
			AggGeom.ConvexElems.Add(ConvexElem);
			FKawaiiPhysicsSharedCollisionData OutLimits;
			KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
				AggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, OutLimits);

			TestTrue(TEXT("Convex BoundingBox: produces exactly one box limit and no sphere limit"),
			         OutLimits.BoxLimits.Num() == 1 && OutLimits.SphericalLimits.Num() == 0);
			TestTrue(TEXT("Convex BoundingBox: location"),
			         OutLimits.BoxLimits[0].Location.Equals(ExpectedLocation, GSimpleWorldTol));
			TestTrue(TEXT("Convex BoundingBox: rotation matches elem transform"),
			         OutLimits.BoxLimits[0].Rotation.Equals(FQuat::Identity, GSimpleWorldTol));
			TestTrue(TEXT("Convex BoundingBox: extent"),
			         OutLimits.BoxLimits[0].Extent.Equals(ExpectedExtent, GSimpleWorldTol));
		}

		// BoundingSphere近似: 外接球としてFSphericalLimitを生成
		{
			FKAggregateGeom AggGeom;
			AggGeom.ConvexElems.Add(ConvexElem);
			FKawaiiPhysicsSharedCollisionData OutLimits;
			KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
				AggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere, OutLimits);

			TestTrue(TEXT("Convex BoundingSphere: produces exactly one sphere limit and no box limit"),
			         OutLimits.SphericalLimits.Num() == 1 && OutLimits.BoxLimits.Num() == 0);
			TestTrue(TEXT("Convex BoundingSphere: location"),
			         OutLimits.SphericalLimits[0].Location.Equals(ExpectedLocation, GSimpleWorldTol));
			TestTrue(TEXT("Convex BoundingSphere: radius is the scaled bounding-box extent length"),
			         FMath::IsNearlyEqual(OutLimits.SphericalLimits[0].Radius, ExpectedExtent.Size(), GSimpleWorldTol));
			TestTrue(TEXT("Convex BoundingSphere: limit type is Outer"),
			         OutLimits.SphericalLimits[0].LimitType == ESphericalLimitType::Outer);
		}

		// Ignore近似: 何も生成しない
		{
			FKAggregateGeom AggGeom;
			AggGeom.ConvexElems.Add(ConvexElem);
			FKawaiiPhysicsSharedCollisionData OutLimits;
			KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
				AggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::None, OutLimits);

			TestTrue(TEXT("Convex Ignore: produces no limits"), OutLimits.IsEmpty());
		}
	}

	// --- Shape単位のQuery無効設定はSimpleWorldのOverlap対象から除外する ---
	{
		FKAggregateGeom AggGeom;

		FKSphereElem NoCollisionSphere;
		NoCollisionSphere.Radius = 5.0f;
		NoCollisionSphere.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AggGeom.SphereElems.Add(NoCollisionSphere);

		FKSphereElem QueryOnlySphere;
		QueryOnlySphere.Center = FVector(10.0f, 0.0f, 0.0f);
		QueryOnlySphere.Radius = 7.0f;
		QueryOnlySphere.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		AggGeom.SphereElems.Add(QueryOnlySphere);

		FKBoxElem PhysicsOnlyBox;
		PhysicsOnlyBox.X = 4.0f;
		PhysicsOnlyBox.Y = 6.0f;
		PhysicsOnlyBox.Z = 8.0f;
		PhysicsOnlyBox.SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		AggGeom.BoxElems.Add(PhysicsOnlyBox);

		FKawaiiPhysicsSharedCollisionData OutLimits;
		KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
			AggGeom, FVector::OneVector, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, OutLimits);

		TestTrue(TEXT("NoCollision sphere and PhysicsOnly box do not produce limits"),
		         OutLimits.SphericalLimits.Num() == 1 && OutLimits.BoxLimits.Num() == 0);
		TestTrue(TEXT("QueryOnly sphere produces a spherical limit"),
		         OutLimits.SphericalLimits.Num() == 1 &&
		         OutLimits.SphericalLimits[0].Location.Equals(FVector(10.0f, 0.0f, 0.0f), GSimpleWorldTol) &&
		         FMath::IsNearlyEqual(OutLimits.SphericalLimits[0].Radius, 7.0f, GSimpleWorldTol));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  AppendBoundsLocalLimits
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldAppendBoundsLocalLimitsTest,
                                 "KawaiiPhysics.SimpleWorld.AppendBoundsLocalLimits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldAppendBoundsLocalLimitsTest::RunTest(const FString& Parameters)
{
	// ComponentTM: Z軸周り90度回転 + 平行移動(100,0,0)
	const FQuat ComponentRotation = FQuat(FVector::ZAxisVector, PI / 2.0f);
	const FTransform ComponentTM(ComponentRotation, FVector(100.0f, 0.0f, 0.0f));
	const FBoxSphereBounds Bounds(FVector(100.0f, 0.0f, 50.0f), FVector(10.0f, 1.0f, 1.0f), 10.05f);

	{
		FKawaiiPhysicsSharedCollisionData LocalLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendBoundsLocalLimits(
			Bounds, ComponentTM, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, LocalLimits);

		FKawaiiPhysicsSharedCollisionData WorldLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendLocalLimitsTransformed(LocalLimits, ComponentTM, WorldLimits);

		TestTrue(TEXT("BoundingBox appends exactly one box"), WorldLimits.BoxLimits.Num() == 1);
		if (WorldLimits.BoxLimits.Num() == 1)
		{
			const FBoxLimit& Box = WorldLimits.BoxLimits[0];
			TestTrue(TEXT("BoundingBox world location stays at Bounds origin"),
			         Box.Location.Equals(FVector(100.0f, 0.0f, 50.0f), GSimpleWorldTol));
			TestTrue(TEXT("BoundingBox world rotation stays identity"),
			         Box.Rotation.Equals(FQuat::Identity, GSimpleWorldTol));
			TestTrue(TEXT("BoundingBox world extent stays unchanged"),
			         Box.Extent.Equals(FVector(10.0f, 1.0f, 1.0f), GSimpleWorldTol));
		}
	}

	{
		FKawaiiPhysicsSharedCollisionData LocalLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendBoundsLocalLimits(
			Bounds, ComponentTM, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere, LocalLimits);

		FKawaiiPhysicsSharedCollisionData WorldLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendLocalLimitsTransformed(LocalLimits, ComponentTM, WorldLimits);

		TestTrue(TEXT("BoundingSphere appends exactly one sphere"), WorldLimits.SphericalLimits.Num() == 1);
		if (WorldLimits.SphericalLimits.Num() == 1)
		{
			const FSphericalLimit& Sphere = WorldLimits.SphericalLimits[0];
			TestTrue(TEXT("BoundingSphere world location stays at Bounds origin"),
			         Sphere.Location.Equals(FVector(100.0f, 0.0f, 50.0f), GSimpleWorldTol));
			TestTrue(TEXT("BoundingSphere radius stays unchanged"),
			         FMath::IsNearlyEqual(Sphere.Radius, 10.05f, GSimpleWorldTol));
		}
	}

	{
		FKawaiiPhysicsSharedCollisionData LocalLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendBoundsLocalLimits(
			Bounds, ComponentTM, EKawaiiPhysicsSimpleWorldConvexFallbackShape::None, LocalLimits);

		TestTrue(TEXT("Ignore appends no limits"), LocalLimits.IsEmpty());
	}

	return true;
}

// ---------------------------------------------------------------------------
//  AppendLocalLimitsTransformed
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldAppendLocalLimitsTransformedTest,
                                 "KawaiiPhysics.SimpleWorld.AppendLocalLimitsTransformed",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldAppendLocalLimitsTransformedTest::RunTest(const FString& Parameters)
{
	// ComponentTM: Z軸周り90度回転 + 平行移動(100,0,0)
	const FQuat ComponentRotation = FRotator(0.0f, 90.0f, 0.0f).Quaternion();
	const FTransform ComponentTM(ComponentRotation, FVector(100.0f, 0.0f, 0.0f));

	FKawaiiPhysicsSharedCollisionData LocalLimits;

	FSphericalLimit Sphere;
	Sphere.Location = FVector(1.0f, 0.0f, 0.0f);
	Sphere.Radius = 3.0f;
	LocalLimits.SphericalLimits.Add(Sphere);

	FCapsuleLimit Capsule;
	Capsule.Location = FVector(2.0f, 0.0f, 0.0f);
	Capsule.Rotation = FQuat::Identity;
	Capsule.Radius = 1.0f;
	Capsule.Length = 5.0f;
	LocalLimits.CapsuleLimits.Add(Capsule);

	FPlanarLimit Planar;
	Planar.Location = FVector::ZeroVector;
	Planar.Rotation = FQuat::Identity; // ローカル UpVector = +Z
	LocalLimits.PlanarLimits.Add(Planar);

	// Append前の既存要素（番兵）が保持されることを確認する
	FKawaiiPhysicsSharedCollisionData OutWorldLimits;
	FSphericalLimit Sentinel;
	Sentinel.Radius = 999.0f;
	OutWorldLimits.SphericalLimits.Add(Sentinel);

	KawaiiPhysicsSimpleWorldCollision::AppendLocalLimitsTransformed(LocalLimits, ComponentTM, OutWorldLimits);

	TestTrue(TEXT("Existing sphere is preserved ahead of the appended one"),
	         OutWorldLimits.SphericalLimits.Num() == 2 &&
	         FMath::IsNearlyEqual(OutWorldLimits.SphericalLimits[0].Radius, 999.0f, GSimpleWorldTol));

	const FVector ExpectedSphereLocation = ComponentTM.TransformPosition(FVector(1.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Sphere location is transformed by ComponentTM"),
	         OutWorldLimits.SphericalLimits[1].Location.Equals(ExpectedSphereLocation, GSimpleWorldTol));

	TestTrue(TEXT("Capsule count"), OutWorldLimits.CapsuleLimits.Num() == 1);
	const FVector ExpectedCapsuleLocation = ComponentTM.TransformPosition(FVector(2.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Capsule location is transformed by ComponentTM"),
	         OutWorldLimits.CapsuleLimits[0].Location.Equals(ExpectedCapsuleLocation, GSimpleWorldTol));
	TestTrue(TEXT("Capsule rotation is composed with ComponentTM rotation"),
	         OutWorldLimits.CapsuleLimits[0].Rotation.Equals(ComponentRotation, GSimpleWorldTol));

	TestTrue(TEXT("Planar count"), OutWorldLimits.PlanarLimits.Num() == 1);
	const FPlanarLimit& WorldPlanar = OutWorldLimits.PlanarLimits[0];
	const FVector ExpectedPlanarLocation = ComponentTM.TransformPosition(FVector::ZeroVector);
	TestTrue(TEXT("Planar location is transformed by ComponentTM"),
	         WorldPlanar.Location.Equals(ExpectedPlanarLocation, GSimpleWorldTol));
	TestTrue(TEXT("Planar rotation is composed with ComponentTM rotation"),
	         WorldPlanar.Rotation.Equals(ComponentRotation, GSimpleWorldTol));
	const FPlane ExpectedPlane(WorldPlanar.Location, WorldPlanar.Rotation.GetUpVector());
	TestTrue(TEXT("Planar plane is recomputed from the transformed location/rotation"),
	         WorldPlanar.Plane.Equals(ExpectedPlane, GSimpleWorldTol));

	return true;
}

// ---------------------------------------------------------------------------
//  AppendFadedLocalLimits（Subsystem.cpp の無名namespaceから移設）
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldAppendFadedLocalLimitsTest,
                                 "KawaiiPhysics.SimpleWorld.AppendFadedLocalLimits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldAppendFadedLocalLimitsTest::RunTest(const FString& Parameters)
{
	auto MakeLocalLimits = []()
	{
		FKawaiiPhysicsSharedCollisionData LocalLimits;

		FSphericalLimit Sphere;
		Sphere.Location = FVector::ZeroVector;
		Sphere.Radius = 10.0f;
		LocalLimits.SphericalLimits.Add(Sphere);

		FCapsuleLimit Capsule;
		Capsule.Location = FVector::ZeroVector;
		Capsule.Rotation = FQuat::Identity;
		Capsule.Radius = 4.0f;
		Capsule.Length = 20.0f;
		LocalLimits.CapsuleLimits.Add(Capsule);

		FTaperedCapsuleLimit Tapered;
		Tapered.Location = FVector::ZeroVector;
		Tapered.Rotation = FQuat::Identity;
		Tapered.Radius0 = 6.0f;
		Tapered.Radius1 = 2.0f;
		Tapered.Length = 20.0f;
		LocalLimits.TaperedCapsuleLimits.Add(Tapered);

		FBoxLimit Box;
		Box.Location = FVector::ZeroVector;
		Box.Rotation = FQuat::Identity;
		Box.Extent = FVector(5.0f, 5.0f, 5.0f);
		LocalLimits.BoxLimits.Add(Box);

		return LocalLimits;
	};

	const FTransform Identity = FTransform::Identity;
	constexpr float BoxEnableThreshold = 0.5f;

	// FadeAlpha=0.5（== BoxEnableThreshold）: 球/カプセル/テーパードカプセルの半径は半減、Box はしきい値以上なので等倍で残る
	{
		FKawaiiPhysicsSharedCollisionData LocalLimits = MakeLocalLimits();
		FKawaiiPhysicsSharedCollisionData OutWorldLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendFadedLocalLimits(
			LocalLimits, 0.5f, Identity, OutWorldLimits, BoxEnableThreshold);

		TestTrue(TEXT("FadeAlpha=0.5: sphere radius is halved"),
		         FMath::IsNearlyEqual(OutWorldLimits.SphericalLimits[0].Radius, 5.0f, GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=0.5: capsule radius is halved"),
		         FMath::IsNearlyEqual(OutWorldLimits.CapsuleLimits[0].Radius, 2.0f, GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=0.5: tapered capsule radii are halved"),
		         FMath::IsNearlyEqual(OutWorldLimits.TaperedCapsuleLimits[0].Radius0, 3.0f, GSimpleWorldTol) &&
		         FMath::IsNearlyEqual(OutWorldLimits.TaperedCapsuleLimits[0].Radius1, 1.0f, GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=0.5 (== threshold): box is kept at full extent"),
		         OutWorldLimits.BoxLimits.Num() == 1 &&
		         OutWorldLimits.BoxLimits[0].Extent.Equals(FVector(5.0f, 5.0f, 5.0f), GSimpleWorldTol));

		// BoxEnableThreshold はデフォルト引数(0.5f)としても公開されている。明示指定と同じ結果になることを確認する。
		FKawaiiPhysicsSharedCollisionData OutWorldLimitsDefaultArg;
		KawaiiPhysicsSimpleWorldCollision::AppendFadedLocalLimits(LocalLimits, 0.5f, Identity, OutWorldLimitsDefaultArg);
		TestTrue(TEXT("Default BoxEnableThreshold(0.5f) matches explicitly-passed 0.5f"),
		         OutWorldLimitsDefaultArg.BoxLimits.Num() == OutWorldLimits.BoxLimits.Num());
	}

	// FadeAlpha=0.4（< threshold）: Box は追記されない
	{
		FKawaiiPhysicsSharedCollisionData LocalLimits = MakeLocalLimits();
		FKawaiiPhysicsSharedCollisionData OutWorldLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendFadedLocalLimits(
			LocalLimits, 0.4f, Identity, OutWorldLimits, BoxEnableThreshold);

		TestTrue(TEXT("FadeAlpha=0.4 (< threshold): box is withheld"), OutWorldLimits.BoxLimits.Num() == 0);
		TestTrue(TEXT("FadeAlpha=0.4: sphere radius is scaled by 0.4"),
		         FMath::IsNearlyEqual(OutWorldLimits.SphericalLimits[0].Radius, 4.0f, GSimpleWorldTol));
	}

	// FadeAlpha=1: 全形状そのまま
	{
		FKawaiiPhysicsSharedCollisionData LocalLimits = MakeLocalLimits();
		FKawaiiPhysicsSharedCollisionData OutWorldLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendFadedLocalLimits(
			LocalLimits, 1.0f, Identity, OutWorldLimits, BoxEnableThreshold);

		TestTrue(TEXT("FadeAlpha=1: sphere radius unchanged"),
		         FMath::IsNearlyEqual(OutWorldLimits.SphericalLimits[0].Radius, 10.0f, GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=1: capsule radius unchanged"),
		         FMath::IsNearlyEqual(OutWorldLimits.CapsuleLimits[0].Radius, 4.0f, GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=1: tapered capsule radii unchanged"),
		         FMath::IsNearlyEqual(OutWorldLimits.TaperedCapsuleLimits[0].Radius0, 6.0f, GSimpleWorldTol) &&
		         FMath::IsNearlyEqual(OutWorldLimits.TaperedCapsuleLimits[0].Radius1, 2.0f, GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=1: box is kept at full extent"),
		         OutWorldLimits.BoxLimits.Num() == 1 &&
		         OutWorldLimits.BoxLimits[0].Extent.Equals(FVector(5.0f, 5.0f, 5.0f), GSimpleWorldTol));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  FKawaiiPhysicsSimpleWorldCollisionDesc::Merge
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldDescMergeTest,
                                 "KawaiiPhysics.SimpleWorld.DescMerge",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldDescMergeTest::RunTest(const FString& Parameters)
{
	// 単一Desc → そのまま（ObjectTypesは空だと自動的にWorldStatic/WorldDynamicのunionへ変換されるため、
	// 「そのまま」の確認は非空ObjectTypesで構成する）
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		Desc.GatherIntervalSec = 0.1f;
		Desc.GatherRadiusOverride = 0.0f; // 自動
		Desc.ObjectTypes = {UEngineTypes::ConvertToObjectType(ECC_WorldStatic)};
		Desc.ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere;
		Desc.SkeletalMeshCollision = EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::BoundingBox;
		Desc.bGroundCollision = false;

		const FKawaiiPhysicsSimpleWorldCollisionDesc Merged =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({Desc});

		TestTrue(TEXT("Single desc: GatherIntervalSec unchanged"),
		         FMath::IsNearlyEqual(Merged.GatherIntervalSec, 0.1f, GSimpleWorldTol));
		TestTrue(TEXT("Single desc: GatherRadiusOverride stays automatic (0)"),
		         FMath::IsNearlyEqual(Merged.GatherRadiusOverride, 0.0f, GSimpleWorldTol));
		TestTrue(TEXT("Single desc: ObjectTypes unchanged"), Merged.ObjectTypes.Num() == 1);
		TestTrue(TEXT("Single desc: ConvexFallbackShape unchanged"),
		         Merged.ConvexFallbackShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere);
		TestTrue(TEXT("Single desc: SkeletalMeshCollision unchanged"),
		         Merged.SkeletalMeshCollision == EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::BoundingBox);
		TestTrue(TEXT("Single desc: bGroundCollision unchanged"), Merged.bGroundCollision == false);
	}

	// GatherIntervalSec: 最小値優先（0は毎フレーム収集として最優先）
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc A, B;
		A.GatherIntervalSec = 0.2f;
		B.GatherIntervalSec = 0.05f;
		const FKawaiiPhysicsSimpleWorldCollisionDesc Merged1 = FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({A, B});
		TestTrue(TEXT("Interval {0.2,0.05} -> 0.05"),
		         FMath::IsNearlyEqual(Merged1.GatherIntervalSec, 0.05f, GSimpleWorldTol));

		FKawaiiPhysicsSimpleWorldCollisionDesc C, D;
		C.GatherIntervalSec = 0.2f;
		D.GatherIntervalSec = 0.0f;
		const FKawaiiPhysicsSimpleWorldCollisionDesc Merged2 = FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({C, D});
		TestTrue(TEXT("Interval {0.2,0.0} -> 0.0 (every-frame priority)"),
		         FMath::IsNearlyEqual(Merged2.GatherIntervalSec, 0.0f, GSimpleWorldTol));
	}

	// GatherRadiusOverride: 全DescがOverride指定の場合だけ最大値。1つでも自動(0)なら自動側(0)を維持する（二段解決前提）
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc Auto, Override;
		Auto.GatherRadiusOverride = 0.0f;
		Override.GatherRadiusOverride = 300.0f;
		const FKawaiiPhysicsSimpleWorldCollisionDesc Merged =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({Auto, Override});
		TestTrue(TEXT("Override {0,300}: mixed auto+override falls back to automatic (0)"),
		         FMath::IsNearlyEqual(Merged.GatherRadiusOverride, 0.0f, GSimpleWorldTol));

		FKawaiiPhysicsSimpleWorldCollisionDesc OverrideA, OverrideB;
		OverrideA.GatherRadiusOverride = 200.0f;
		OverrideB.GatherRadiusOverride = 300.0f;
		const FKawaiiPhysicsSimpleWorldCollisionDesc MergedAllOverridden =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({OverrideA, OverrideB});
		TestTrue(TEXT("Override {200,300}: all-overridden picks the max"),
		         FMath::IsNearlyEqual(MergedAllOverridden.GatherRadiusOverride, 300.0f, GSimpleWorldTol));
	}

	// ObjectTypes: 空+非空 → WorldStatic/WorldDynamic を含むunion
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc EmptyTypes, PawnTypes;
		// EmptyTypes.ObjectTypes は既定で空のまま
		PawnTypes.ObjectTypes = {UEngineTypes::ConvertToObjectType(ECC_Pawn)};

		const FKawaiiPhysicsSimpleWorldCollisionDesc Merged =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({EmptyTypes, PawnTypes});

		const TEnumAsByte<EObjectTypeQuery> WorldStaticType = UEngineTypes::ConvertToObjectType(ECC_WorldStatic);
		const TEnumAsByte<EObjectTypeQuery> WorldDynamicType = UEngineTypes::ConvertToObjectType(ECC_WorldDynamic);
		const TEnumAsByte<EObjectTypeQuery> PawnType = UEngineTypes::ConvertToObjectType(ECC_Pawn);

		TestTrue(TEXT("ObjectTypes union contains WorldStatic/WorldDynamic/Pawn only"),
		         Merged.ObjectTypes.Num() == 3 &&
		         Merged.ObjectTypes.Contains(WorldStaticType) &&
		         Merged.ObjectTypes.Contains(WorldDynamicType) &&
		         Merged.ObjectTypes.Contains(PawnType));
	}

	// enum優先: SkeletalMeshCollision は PhysicsAsset > BoundingBox > None
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc IgnoreDesc, PhysicsAssetDesc;
		IgnoreDesc.SkeletalMeshCollision = EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::None;
		PhysicsAssetDesc.SkeletalMeshCollision = EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::PhysicsAsset;
		const FKawaiiPhysicsSimpleWorldCollisionDesc Merged =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({IgnoreDesc, PhysicsAssetDesc});
		TestTrue(TEXT("SkeletalMeshCollision {None,PhysicsAsset} -> PhysicsAsset"),
		         Merged.SkeletalMeshCollision == EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::PhysicsAsset);
	}

	// enum優先: ConvexFallbackShape は BoundingBox > BoundingSphere > None
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc SphereDesc, BoxDesc;
		SphereDesc.ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere;
		BoxDesc.ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox;
		const FKawaiiPhysicsSimpleWorldCollisionDesc Merged =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({SphereDesc, BoxDesc});
		TestTrue(TEXT("ConvexFallbackShape {BoundingSphere,BoundingBox} -> BoundingBox"),
		         Merged.ConvexFallbackShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox);
	}

	// ground: OR
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc NoGround, WithGround;
		NoGround.bGroundCollision = false;
		WithGround.bGroundCollision = true;
		const FKawaiiPhysicsSimpleWorldCollisionDesc Merged =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({NoGround, WithGround});
		TestTrue(TEXT("bGroundCollision {false,true} -> true"), Merged.bGroundCollision == true);
	}

	return true;
}

// ---------------------------------------------------------------------------
//  FKawaiiPhysicsSimpleWorldCollisionEntry のライフサイクル
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldEntryLifecycleTest,
                                 "KawaiiPhysics.SimpleWorld.EntryLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldEntryLifecycleTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID1 = 1;
	constexpr uint64 SourceID2 = 2;

	// SetDesc → BuildMergedDesc がマージ結果を返す。
	// SetDesc直後のDescも現在フレームで既読扱いに刻印されるため、即時cleanupでは除去されない。
	// 注: MarkRead は GFrameCounter を直接参照するため CurrentFrame を明示的に渡せない。
	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc1;
		Desc1.GatherIntervalSec = 0.1f;
		Entry.SetDesc(SourceID1, Desc1);

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc2;
		Desc2.GatherIntervalSec = 0.3f;
		Entry.SetDesc(SourceID2, Desc2);

		TestTrue(TEXT("HasAnyDesc true after two SetDesc"), Entry.HasAnyDesc());

		FKawaiiPhysicsSimpleWorldCollisionDesc MergedBoth;
		TestTrue(TEXT("BuildMergedDesc succeeds with two descs"), Entry.BuildMergedDesc(MergedBoth));
		TestTrue(TEXT("Merged interval is the min of the two"),
		         FMath::IsNearlyEqual(MergedBoth.GatherIntervalSec, 0.1f, GSimpleWorldTol));

		const uint64 BaseFrame = GFrameCounter;
		TestTrue(TEXT("MarkRead returns true for an existing desc"), Entry.MarkRead(SourceID1));

		Entry.RemoveExpiredDescs(BaseFrame + 1000, 1000000);

		TestTrue(TEXT("HasAnyDesc true after cleanup within max age"), Entry.HasAnyDesc());
		FKawaiiPhysicsSimpleWorldCollisionDesc MergedAfterExpire;
		TestTrue(TEXT("BuildMergedDesc still succeeds after cleanup within max age"),
		         Entry.BuildMergedDesc(MergedAfterExpire));
		TestTrue(TEXT("Merged interval is still the min after cleanup within max age"),
		         FMath::IsNearlyEqual(MergedAfterExpire.GatherIntervalSec, 0.1f, GSimpleWorldTol));

		Entry.RemoveDesc(SourceID2);
		TestTrue(TEXT("HasAnyDesc true after removing one desc"), Entry.HasAnyDesc());
		Entry.RemoveDesc(SourceID1);
		TestTrue(TEXT("HasAnyDesc false after removing the last desc"), !Entry.HasAnyDesc());
		TestTrue(TEXT("MarkRead returns false for a removed desc"), !Entry.MarkRead(SourceID1));
		FKawaiiPhysicsSimpleWorldCollisionDesc MergedEmpty;
		TestTrue(TEXT("BuildMergedDesc fails when no desc remains"), !Entry.BuildMergedDesc(MergedEmpty));
	}

	// RemoveExpiredDescs で期限切れになったDescは MarkRead できない。
	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		Desc.GatherIntervalSec = 0.2f;
		Entry.SetDesc(SourceID1, Desc);

		const uint64 ExpiredFrame = GFrameCounter + 1;
		Entry.RemoveExpiredDescs(ExpiredFrame, 0);

		TestTrue(TEXT("HasAnyDesc false after expiration cleanup"), !Entry.HasAnyDesc());
		TestTrue(TEXT("MarkRead returns false after expiration cleanup"), !Entry.MarkRead(SourceID1));
	}

	// SetDesc直後のDescはLastReadFrameが刻印済みのため、作成直後のcleanupでは空Entryにならない。
	{
		constexpr uint64 SourceID = 100;
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		Desc.GatherIntervalSec = 0.25f;
		Entry.SetDesc(SourceID, Desc);

		Entry.RemoveExpiredDescs(GFrameCounter, 60);
		TestTrue(TEXT("HasAnyDesc true after immediate cleanup following SetDesc"), Entry.HasAnyDesc());
	}

	// RequestRegather / ConsumeRegatherRequested: 1回だけ true を返す
	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
		TestTrue(TEXT("Regather not requested initially"), !Entry.ConsumeRegatherRequested());

		Entry.RequestRegather();
		TestTrue(TEXT("First consume returns true"), Entry.ConsumeRegatherRequested());
		TestTrue(TEXT("Second consume returns false (already consumed)"), !Entry.ConsumeRegatherRequested());
	}

	// Slot: Publish→AppendTo のラウンドトリップ最小限（詳細な契約は KawaiiPhysicsSharedCollisionSlotTest でカバー済み）
	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSharedCollisionData PublishData;
		FSphericalLimit Sphere;
		Sphere.Radius = 12.0f;
		PublishData.SphericalLimits.Add(Sphere);
		Entry.Slot.Publish(PublishData);

		FKawaiiPhysicsSharedCollisionData OutData;
		Entry.Slot.AppendTo(OutData);
		TestTrue(TEXT("Entry.Slot round-trips published data"),
		         OutData.SphericalLimits.Num() == 1 &&
		         FMath::IsNearlyEqual(OutData.SphericalLimits[0].Radius, 12.0f, GSimpleWorldTol));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  DebugInfo
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldDebugInfoTest,
                                 "KawaiiPhysics.SimpleWorld.DebugInfo",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldDebugInfoTest::RunTest(const FString& Parameters)
{
	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
		FKawaiiPhysicsSimpleWorldCollisionDebugInfo Info;
		UKawaiiPhysicsSharedCollisionSubsystem::FillSimpleWorldCollisionDebugInfo(Entry, Info);

		TestTrue(TEXT("Empty entry sets bHasEntry"), Info.bHasEntry);
		TestEqual(TEXT("Empty entry NumDescs"), Info.NumDescs, 0);
		TestEqual(TEXT("Empty entry NumGatheredComponents"), Info.NumGatheredComponents, 0);
		TestEqual(TEXT("Empty entry NumStaticComponents"), Info.NumStaticComponents, 0);
		TestEqual(TEXT("Empty entry NumMovableComponents"), Info.NumMovableComponents, 0);
		TestEqual(TEXT("Empty entry NumSkeletalBodies"), Info.NumSkeletalBodies, 0);
		TestEqual(TEXT("Empty entry gathered names"), Info.GatheredComponentNames.Num(), 0);
		TestTrue(TEXT("Empty entry MinFadeAlpha"),
		         FMath::IsNearlyEqual(Info.MinFadeAlpha, 1.0f, GSimpleWorldTol));
		TestFalse(TEXT("Empty entry has no ground box"), Info.bHasGroundBox);
		TestTrue(TEXT("Empty entry GroundSource is None"),
		         Info.GroundSource == EKawaiiPhysicsSimpleWorldGroundSource::None);
		TestTrue(TEXT("Empty entry GroundBoxSource is None"),
		         Info.GroundBoxSource == EKawaiiPhysicsSimpleWorldGroundSource::None);
		TestTrue(TEXT("Empty entry TimeSinceLastGather is sentinel"),
		         FMath::IsNearlyEqual(Info.TimeSinceLastGather, -1.0f, GSimpleWorldTol));
	}

	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& StaticComponent =
			Entry.GatheredComponents.AddDefaulted_GetRef();
		StaticComponent.Component = NewObject<UStaticMeshComponent>(GetTransientPackage());
		StaticComponent.bStatic = true;
		StaticComponent.FadeAlpha = 1.0f;

		FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& InvalidInstanceComponent =
			Entry.GatheredComponents.AddDefaulted_GetRef();
		InvalidInstanceComponent.bStatic = false;
		InvalidInstanceComponent.FadeAlpha = 0.25f;
		InvalidInstanceComponent.InstanceIndex = 2;

		FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& SkeletalBodyComponent =
			Entry.GatheredComponents.AddDefaulted_GetRef();
		SkeletalBodyComponent.bStatic = false;
		SkeletalBodyComponent.BodyBindings.SetNum(2);

		Entry.bHasGroundBox = true;
		Entry.GroundBox.Location = FVector(1.0f, 2.0f, 3.0f);
		Entry.GroundBox.Extent = FVector(10.0f, 20.0f, 30.0f);
		Entry.GroundSource = EKawaiiPhysicsSimpleWorldGroundSource::Provider;
		Entry.GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::CharacterMovement;
		Entry.LastGatherRadius = 123.0f;
		Entry.TimeSinceLastGather = 0.05f;

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		Entry.SetDesc(1, Desc);
		Entry.SetDesc(2, Desc);

		FKawaiiPhysicsSimpleWorldCollisionDebugInfo Info;
		UKawaiiPhysicsSharedCollisionSubsystem::FillSimpleWorldCollisionDebugInfo(Entry, Info);

		TestTrue(TEXT("Filled entry sets bHasEntry"), Info.bHasEntry);
		TestEqual(TEXT("Filled entry NumGatheredComponents"), Info.NumGatheredComponents, 3);
		TestEqual(TEXT("Filled entry NumStaticComponents"), Info.NumStaticComponents, 1);
		TestEqual(TEXT("Filled entry NumMovableComponents"), Info.NumMovableComponents, 2);
		TestEqual(TEXT("Filled entry NumSkeletalBodies"), Info.NumSkeletalBodies, 2);
		TestEqual(TEXT("Filled entry GatheredComponentNames"), Info.GatheredComponentNames.Num(), 3);
		if (Info.GatheredComponentNames.Num() == 3)
		{
			TestTrue(TEXT("Valid component name uses no-owner prefix"),
			         Info.GatheredComponentNames[0].StartsWith(TEXT("<no owner>:")));
			TestTrue(TEXT("Valid component name includes component name"),
			         Info.GatheredComponentNames[0].Contains(TEXT(":StaticMeshComponent")));
			TestEqual(TEXT("Invalid ISM name keeps instance index"),
			          Info.GatheredComponentNames[1], FString(TEXT("<invalid>[2]")));
			TestEqual(TEXT("Invalid component name"), Info.GatheredComponentNames[2], FString(TEXT("<invalid>")));
		}
		TestTrue(TEXT("Filled entry MinFadeAlpha"),
		         FMath::IsNearlyEqual(Info.MinFadeAlpha, 0.25f, GSimpleWorldTol));
		TestEqual(TEXT("Filled entry NumDescs"), Info.NumDescs, 2);
		TestTrue(TEXT("Filled entry GatherRadius"),
		         FMath::IsNearlyEqual(Info.GatherRadius, 123.0f, GSimpleWorldTol));
		TestTrue(TEXT("Filled entry TimeSinceLastGather"),
		         FMath::IsNearlyEqual(Info.TimeSinceLastGather, 0.05f, GSimpleWorldTol));
		TestTrue(TEXT("Filled entry GroundBoxLocation"),
		         Info.GroundBoxLocation.Equals(FVector(1.0f, 2.0f, 3.0f), GSimpleWorldTol));
		TestTrue(TEXT("Filled entry GroundBoxExtent"),
		         Info.GroundBoxExtent.Equals(FVector(10.0f, 20.0f, 30.0f), GSimpleWorldTol));
		TestTrue(TEXT("Filled entry has ground box"), Info.bHasGroundBox);
		TestTrue(TEXT("Filled entry GroundSource"),
		         Info.GroundSource == EKawaiiPhysicsSimpleWorldGroundSource::Provider);
		TestTrue(TEXT("Filled entry GroundBoxSource"),
		         Info.GroundBoxSource == EKawaiiPhysicsSimpleWorldGroundSource::CharacterMovement);
	}

	return true;
}

// ---------------------------------------------------------------------------
//  FKawaiiPhysicsSimpleWorldCollisionEntry の即時cleanup回帰
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldEntrySetDescSurvivesImmediateCleanupTest,
                                 "KawaiiPhysics.SimpleWorld.EntrySetDescSurvivesImmediateCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldEntrySetDescSurvivesImmediateCleanupTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID = 100;

	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	Desc.GatherIntervalSec = 0.25f;

	Entry.SetDesc(SourceID, Desc);
	const uint64 CurrentFrame = GFrameCounter;
	Entry.RemoveExpiredDescs(CurrentFrame, 0);

	TestTrue(TEXT("Desc survives RemoveExpiredDescs in the same frame after SetDesc"), Entry.HasAnyDesc());

	FKawaiiPhysicsSimpleWorldCollisionDesc Merged;
	TestTrue(TEXT("BuildMergedDesc succeeds after immediate cleanup"), Entry.BuildMergedDesc(Merged));
	TestTrue(TEXT("Merged desc keeps the SetDesc value"),
	         FMath::IsNearlyEqual(Merged.GatherIntervalSec, 0.25f, GSimpleWorldTol));

	Entry.RemoveDesc(SourceID);
	TestTrue(TEXT("MarkRead returns false after the slot disappears"), !Entry.MarkRead(SourceID));

	return true;
}

// ---------------------------------------------------------------------------
//  push-out 統合テスト（ハーネス経由。SimpleWorld配列に注入した形状で実際にボーンが押し出されるか）
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldCollisionPushOutTest,
                                 "KawaiiPhysics.SimpleWorld.CollisionPushOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldCollisionPushOutTest::RunTest(const FString& Parameters)
{
	// root(0,0,0) - child(pose (0,0,-10), BoneLength=10) の2ボーンチェーン。
	// 子ボーンの実位置(Location/PrevLocation)を pose とは独立に (8,0,-4) へ直接セットし、
	// 中心(10,0,0)・半径4√5(=|(8,0,-4)-(10,0,0)|の2倍=食い込み確保)のOuterスフィアに食い込ませる。
	// このスフィアの半径・中心は「押し出し後の点(6,0,-8)が親からの距離(BoneLength=10)にちょうど一致する」よう選定してあるため、
	// コリジョン後の最終ボーン長復元（親からの距離をBoneLengthへ再投影する処理）が恒等変換になり、
	// 1フレームで解析的に厳密一致する（重力・剛性・角度制限はすべて無効化し、コリジョンの効果のみを分離）。
	const float SphereRadius = FMath::Sqrt(80.0f); // = 4*sqrt(5) ≈ 8.9443
	const FVector SphereCenter(10.0f, 0.0f, 0.0f);
	const FVector StartInsideSphere(8.0f, 0.0f, -4.0f);
	const FVector ExpectedPushedOut(6.0f, 0.0f, -8.0f);

	// コリジョンが働かない場合の期待値: BoneLength復元だけが働き、
	// StartInsideSphere の方向を保ったまま距離をBoneLength(10)へ再スケールした点になる。
	const float Sqrt5 = FMath::Sqrt(5.0f);
	const FVector ExpectedNoPushOut(4.0f * Sqrt5, 0.0f, -2.0f * Sqrt5); // = StartInsideSphere正規化 * 10

	auto BuildChain = [&](FKawaiiPhysicsTestAccessor& A)
	{
		A.BuildVerticalChain(2, 10.0f); // root(0,0,0) と child pose(0,0,-10) の2ボーンチェーン、BoneLength=10
		FKawaiiPhysicsSettings S;
		S.Damping = 0.0f;
		S.Stiffness = 0.0f; // Pull to Pose を完全無効化
		S.LimitAngle = 0.0f; // 角度制限を無効化（0はAdjustByAngleLimit内で無制限扱い）
		S.Radius = 0.0f; // ボーン自身のコリジョン半径は0（数値を単純化）
		A.SetAllPhysicsSettings(S);
		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector::ZeroVector);
		A.Bone(1).Location = StartInsideSphere;
		A.Bone(1).PrevLocation = StartInsideSphere;
	};

	FSphericalLimit Sphere;
	Sphere.Location = SphereCenter;
	Sphere.Radius = SphereRadius;
	Sphere.LimitType = ESphericalLimitType::Outer;
	Sphere.bEnable = true;
	TArray<FSphericalLimit> SphereLimits = {Sphere};
	TArray<FSphericalLimit> EmptySpheres;
	TArray<FCapsuleLimit> EmptyCapsules;
	TArray<FTaperedCapsuleLimit> EmptyTaperedCapsules;
	TArray<FBoxLimit> EmptyBoxes;

	// bUseSimpleWorldCollision = true: SimpleWorld配列のSphereに押し出される
	{
		FKawaiiPhysicsTestAccessor A;
		BuildChain(A);
		A.SetSimpleWorldLimits(SphereLimits, EmptyCapsules, EmptyTaperedCapsules, EmptyBoxes);

		TestEqual(TEXT("Injected SimpleWorld collider count"),
		          A.Node.GetNumSimpleWorldColliders(),
		          SphereLimits.Num() + EmptyCapsules.Num() + EmptyTaperedCapsules.Num() + EmptyBoxes.Num());

		A.StepFrame(1.0f / 60.0f);

		TestTrue(FString::Printf(TEXT("SimpleWorld sphere push-out: got %s expected %s"),
		                         *A.Bone(1).Location.ToString(), *ExpectedPushedOut.ToString()),
		         A.Bone(1).Location.Equals(ExpectedPushedOut, GSimpleWorldPushOutTol));
	}

	// bUseSimpleWorldCollision = false: 同じ形状を注入しても押し出されない（適用条件のゲート確認）
	{
		FKawaiiPhysicsTestAccessor A;
		BuildChain(A);
		A.SetSimpleWorldLimits(SphereLimits, EmptyCapsules, EmptyTaperedCapsules, EmptyBoxes);
		A.Node.bUseSimpleWorldCollision = false; // SetSimpleWorldLimitsが立てたフラグを明示的に無効化

		A.StepFrame(1.0f / 60.0f);

		TestTrue(FString::Printf(TEXT("Gated off: SimpleWorld collision does not push the bone: got %s expected %s"),
		                         *A.Bone(1).Location.ToString(), *ExpectedNoPushOut.ToString()),
		         A.Bone(1).Location.Equals(ExpectedNoPushOut, GSimpleWorldPushOutTol));
	}

	// bUseSimpleWorldCollision = true、Capsule注入: SimpleWorldCapsuleLimitsがStepOnce内のPrepareCollisionShapeCaches()で
	// 再計算されないと、CachedStartPoint/CachedEndPointがゼロ初期化のまま(=原点に潰れたカプセル)扱いになり
	// このケースは赤化する（形状キャッシュ登録漏れの回帰チェック）。
	// カプセル軸をY軸に向けているため、Bone(Y=0)からの垂線はちょうどLocation(=SphereCenter)に落ち、
	// Sphereケースと全く同じ押し出し方向・到達点(ExpectedPushedOut)になる。
	{
		FKawaiiPhysicsTestAccessor A;
		BuildChain(A);

		FCapsuleLimit Capsule;
		Capsule.Location = SphereCenter;
		Capsule.Rotation = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-90.0f));
		Capsule.Radius = SphereRadius;
		Capsule.Length = 10.0f;
		Capsule.bEnable = true;
		Capsule.SourceType = ECollisionSourceType::SimpleWorld;
		TArray<FCapsuleLimit> CapsuleLimits = {Capsule};

		A.SetSimpleWorldLimits(EmptySpheres, CapsuleLimits, EmptyTaperedCapsules, EmptyBoxes);

		A.StepFrame(1.0f / 60.0f);

		// Nodeが内部で持つCachedStartPoint/CachedEndPointは使わず、Location/Rotation/Lengthから
		// 独立に軸を再計算して判定する（形状キャッシュが更新されていない場合にのみ失敗させたいため）。
		const FVector CapsuleAxis = Capsule.Rotation.GetAxisZ();
		const FVector CapsuleStart = Capsule.Location + CapsuleAxis * Capsule.Length * 0.5f;
		const FVector CapsuleEnd = Capsule.Location - CapsuleAxis * Capsule.Length * 0.5f;
		const float DistToAxis = FMath::Sqrt(
			FMath::PointDistToSegmentSquared(A.Bone(1).Location, CapsuleStart, CapsuleEnd));

		TestTrue(FString::Printf(TEXT("SimpleWorld capsule push-out: dist-to-axis %.4f expected >= %.4f"),
		                         DistToAxis, Capsule.Radius - GSimpleWorldPushOutTol),
		         DistToAxis >= Capsule.Radius - GSimpleWorldPushOutTol);

		TestTrue(FString::Printf(TEXT("SimpleWorld capsule push-out position: got %s expected %s"),
		                         *A.Bone(1).Location.ToString(), *ExpectedPushedOut.ToString()),
		         A.Bone(1).Location.Equals(ExpectedPushedOut, GSimpleWorldPushOutTol));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  UpdateSkeletalBodyWorldTransforms
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldUpdateSkeletalBodyWorldTransformsTest,
                                 "KawaiiPhysics.SimpleWorld.UpdateSkeletalBodyWorldTransforms",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldUpdateSkeletalBodyWorldTransformsTest::RunTest(const FString& Parameters)
{
	using KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding;

	TArray<FTransform> ComponentSpaceTransforms;
	ComponentSpaceTransforms.SetNum(3);
	ComponentSpaceTransforms[0] = FTransform::Identity;
	ComponentSpaceTransforms[1] = FTransform::Identity;
	ComponentSpaceTransforms[2] = FTransform(
		FQuat(FVector::ZAxisVector, PI / 2.0f),
		FVector(0.0f, 0.0f, 50.0f));

	const FTransform ComponentTM(
		FQuat(FVector::ZAxisVector, PI / 2.0f),
		FVector(100.0f, 0.0f, 0.0f));

	TArray<FKawaiiPhysicsSimpleWorldBodyBinding> Bindings;
	FKawaiiPhysicsSimpleWorldBodyBinding RootBinding;
	RootBinding.BoneIndex = 0;
	Bindings.Add(RootBinding);
	FKawaiiPhysicsSimpleWorldBodyBinding Bone2Binding;
	Bone2Binding.BoneIndex = 2;
	Bindings.Add(Bone2Binding);
	FKawaiiPhysicsSimpleWorldBodyBinding MissingBinding;
	MissingBinding.BoneIndex = 5;
	Bindings.Add(MissingBinding);

	TArray<FTransform> OutBodyWorldTMs;
	const int32 NumMissingBones = KawaiiPhysicsSimpleWorldCollision::UpdateSkeletalBodyWorldTransforms(
		MakeArrayView(Bindings),
		MakeArrayView(ComponentSpaceTransforms),
		ComponentTM,
		OutBodyWorldTMs);

	TestTrue(TEXT("Out transform count matches binding count"), OutBodyWorldTMs.Num() == 3);
	TestTrue(TEXT("One missing bone is reported"), NumMissingBones == 1);
	if (OutBodyWorldTMs.Num() == 3)
	{
		TestTrue(TEXT("Bone2 world location composes bone and component transforms"),
		         OutBodyWorldTMs[1].GetLocation().Equals(FVector(100.0f, 0.0f, 50.0f), GSimpleWorldTol));
		TestTrue(TEXT("Bone2 world rotation is Z180"),
		         OutBodyWorldTMs[1].GetRotation().Equals(FQuat(FVector::ZAxisVector, PI), GSimpleWorldTol));
		TestTrue(TEXT("Bone2 world scale is stripped"),
		         OutBodyWorldTMs[1].GetScale3D().Equals(FVector::OneVector, GSimpleWorldTol));
		TestTrue(TEXT("Missing bone leaves identity transform"),
		         OutBodyWorldTMs[2].Equals(FTransform::Identity, GSimpleWorldTol));
	}

	{
		const FTransform ScaledComponentTM(
			FQuat(FVector::ZAxisVector, PI / 2.0f),
			FVector(100.0f, 0.0f, 0.0f),
			FVector(2.0f, 2.0f, 2.0f));

		TArray<FTransform> ScaledOutBodyWorldTMs;
		const int32 NumMissingBonesWithScale = KawaiiPhysicsSimpleWorldCollision::UpdateSkeletalBodyWorldTransforms(
			MakeArrayView(Bindings),
			MakeArrayView(ComponentSpaceTransforms),
			ScaledComponentTM,
			ScaledOutBodyWorldTMs);

		TestTrue(TEXT("Scaled component reports the same missing bone count"), NumMissingBonesWithScale == 1);
		TestTrue(TEXT("Scaled component applies scale to bone translation"),
		         ScaledOutBodyWorldTMs.Num() == 3 &&
		         ScaledOutBodyWorldTMs[1].GetLocation().Equals(FVector(100.0f, 0.0f, 100.0f), GSimpleWorldTol));
		TestTrue(TEXT("Scaled component strips final body scale"),
		         ScaledOutBodyWorldTMs.Num() == 3 &&
		         ScaledOutBodyWorldTMs[1].GetScale3D().Equals(FVector::OneVector, GSimpleWorldTol));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  AppendFadedSkeletalLocalLimits
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldAppendFadedSkeletalLocalLimitsTest,
                                 "KawaiiPhysics.SimpleWorld.AppendFadedSkeletalLocalLimits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldAppendFadedSkeletalLocalLimitsTest::RunTest(const FString& Parameters)
{
	using KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding;

	FKawaiiPhysicsSharedCollisionData LocalLimits;

	FSphericalLimit BodyASphere0;
	BodyASphere0.Location = FVector(10.0f, 0.0f, 0.0f);
	BodyASphere0.Radius = 4.0f;
	LocalLimits.SphericalLimits.Add(BodyASphere0);

	FSphericalLimit BodyASphere1;
	BodyASphere1.Location = FVector(20.0f, 0.0f, 0.0f);
	BodyASphere1.Radius = 6.0f;
	LocalLimits.SphericalLimits.Add(BodyASphere1);

	FSphericalLimit BodyBSphere;
	BodyBSphere.Location = FVector::ZeroVector;
	BodyBSphere.Radius = 8.0f;
	LocalLimits.SphericalLimits.Add(BodyBSphere);

	FCapsuleLimit Capsule;
	Capsule.Location = FVector::ZeroVector;
	Capsule.Rotation = FQuat::Identity;
	Capsule.Radius = 3.0f;
	Capsule.Length = 20.0f;
	LocalLimits.CapsuleLimits.Add(Capsule);

	FBoxLimit Box;
	Box.Location = FVector::ZeroVector;
	Box.Rotation = FQuat::Identity;
	Box.Extent = FVector(2.0f, 3.0f, 4.0f);
	LocalLimits.BoxLimits.Add(Box);

	TArray<FKawaiiPhysicsSimpleWorldBodyBinding> Bindings;
	FKawaiiPhysicsSimpleWorldBodyBinding BodyA;
	BodyA.BoneIndex = 0;
	BodyA.NumSphericalLimits = 2;
	BodyA.NumBoxLimits = 1;
	Bindings.Add(BodyA);
	FKawaiiPhysicsSimpleWorldBodyBinding BodyB;
	BodyB.BoneIndex = 1;
	BodyB.NumSphericalLimits = 1;
	BodyB.NumCapsuleLimits = 1;
	Bindings.Add(BodyB);

	TArray<FTransform> BodyWorldTMs;
	BodyWorldTMs.Add(FTransform(FQuat::Identity, FVector(0.0f, 0.0f, 100.0f)));
	BodyWorldTMs.Add(FTransform(FQuat(FVector::ZAxisVector, PI / 2.0f), FVector(5.0f, 0.0f, 0.0f)));

	{
		FKawaiiPhysicsSharedCollisionData OutWorldLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendFadedSkeletalLocalLimits(
			LocalLimits,
			MakeArrayView(Bindings),
			MakeArrayView(BodyWorldTMs),
			0.5f,
			OutWorldLimits,
			0.5f);

		TestTrue(TEXT("FadeAlpha=0.5: first body A sphere location follows body A"),
		         OutWorldLimits.SphericalLimits.Num() == 3 &&
		         OutWorldLimits.SphericalLimits[0].Location.Equals(FVector(10.0f, 0.0f, 100.0f), GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=0.5: second body A sphere location follows body A"),
		         OutWorldLimits.SphericalLimits.Num() == 3 &&
		         OutWorldLimits.SphericalLimits[1].Location.Equals(FVector(20.0f, 0.0f, 100.0f), GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=0.5: body B sphere uses accumulated offset and follows body B"),
		         OutWorldLimits.SphericalLimits.Num() == 3 &&
		         OutWorldLimits.SphericalLimits[2].Location.Equals(FVector(5.0f, 0.0f, 0.0f), GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=0.5: sphere radius is halved"),
		         OutWorldLimits.SphericalLimits.Num() == 3 &&
		         FMath::IsNearlyEqual(OutWorldLimits.SphericalLimits[0].Radius, 2.0f, GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=0.5: box is kept at full extent"),
		         OutWorldLimits.BoxLimits.Num() == 1 &&
		         OutWorldLimits.BoxLimits[0].Extent.Equals(FVector(2.0f, 3.0f, 4.0f), GSimpleWorldTol));
		TestTrue(TEXT("FadeAlpha=0.5: capsule rotation follows body B"),
		         OutWorldLimits.CapsuleLimits.Num() == 1 &&
		         OutWorldLimits.CapsuleLimits[0].Rotation.Equals(
			         FQuat(FVector::ZAxisVector, PI / 2.0f), GSimpleWorldTol));
	}

	{
		FKawaiiPhysicsSharedCollisionData OutWorldLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendFadedSkeletalLocalLimits(
			LocalLimits,
			MakeArrayView(Bindings),
			MakeArrayView(BodyWorldTMs),
			0.4f,
			OutWorldLimits,
			0.5f);

		TestTrue(TEXT("FadeAlpha=0.4: box is withheld"), OutWorldLimits.BoxLimits.Num() == 0);
	}

	return true;
}

// ---------------------------------------------------------------------------
//  AppendBodyLocalLimitsGuard
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldAppendBodyLocalLimitsGuardTest,
                                 "KawaiiPhysics.SimpleWorld.AppendBodyLocalLimitsGuard",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldAppendBodyLocalLimitsGuardTest::RunTest(const FString& Parameters)
{
	using KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding;

	FKAggregateGeom SphereAggGeom;
	FKSphereElem SphereElem;
	SphereElem.Radius = 5.0f;
	SphereAggGeom.SphereElems.Add(SphereElem);

	FKawaiiPhysicsSharedCollisionData OutLimits;
	TArray<FKawaiiPhysicsSimpleWorldBodyBinding> Bindings;
	TestTrue(TEXT("First body is accepted"),
	         KawaiiPhysicsSimpleWorldCollision::AppendBodyLocalLimits(
		         SphereAggGeom,
		         0,
		         FVector::OneVector,
		         EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
		         2,
		         OutLimits,
		         Bindings));
	TestTrue(TEXT("Second body is accepted"),
	         KawaiiPhysicsSimpleWorldCollision::AppendBodyLocalLimits(
		         SphereAggGeom,
		         1,
		         FVector::OneVector,
		         EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
		         2,
		         OutLimits,
		         Bindings));

	const int32 SphereCountBeforeRejectedBody = OutLimits.SphericalLimits.Num();
	const int32 BindingCountBeforeRejectedBody = Bindings.Num();
	TestTrue(TEXT("Third body is rejected by MaxBodies"),
	         !KawaiiPhysicsSimpleWorldCollision::AppendBodyLocalLimits(
		         SphereAggGeom,
		         2,
		         FVector::OneVector,
		         EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
		         2,
		         OutLimits,
		         Bindings));
	TestTrue(TEXT("Rejected body does not mutate arrays"),
	         OutLimits.SphericalLimits.Num() == SphereCountBeforeRejectedBody &&
	         Bindings.Num() == BindingCountBeforeRejectedBody);

	FKAggregateGeom EmptyAggGeom;
	TestTrue(TEXT("Empty AggGeom is rejected"),
	         !KawaiiPhysicsSimpleWorldCollision::AppendBodyLocalLimits(
		         EmptyAggGeom,
		         3,
		         FVector::OneVector,
		         EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
		         10,
		         OutLimits,
		         Bindings));
	TestTrue(TEXT("Empty AggGeom does not add a binding"), Bindings.Num() == BindingCountBeforeRejectedBody);

	FKConvexElem ConvexElem;
	ConvexElem.ElemBox = FBox(FVector(-1.0f, -2.0f, -3.0f), FVector(1.0f, 2.0f, 3.0f));
	FKAggregateGeom ConvexAggGeom;
	ConvexAggGeom.ConvexElems.Add(ConvexElem);

	{
		FKawaiiPhysicsSharedCollisionData ConvexLimits;
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding> ConvexBindings;
		TestTrue(TEXT("Convex BoundingBox body is accepted"),
		         KawaiiPhysicsSimpleWorldCollision::AppendBodyLocalLimits(
			         ConvexAggGeom,
			         0,
			         FVector::OneVector,
			         EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
			         10,
			         ConvexLimits,
			         ConvexBindings));
		TestTrue(TEXT("Convex BoundingBox binding records one box"),
		         ConvexBindings.Num() == 1 &&
		         ConvexBindings[0].NumBoxLimits == 1 &&
		         ConvexBindings[0].NumSphericalLimits == 0);
	}

	{
		FKawaiiPhysicsSharedCollisionData ConvexLimits;
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding> ConvexBindings;
		TestTrue(TEXT("Convex BoundingSphere body is accepted"),
		         KawaiiPhysicsSimpleWorldCollision::AppendBodyLocalLimits(
			         ConvexAggGeom,
			         0,
			         FVector::OneVector,
			         EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere,
			         10,
			         ConvexLimits,
			         ConvexBindings));
		TestTrue(TEXT("Convex BoundingSphere binding records one sphere"),
		         ConvexBindings.Num() == 1 &&
		         ConvexBindings[0].NumSphericalLimits == 1 &&
		         ConvexBindings[0].NumBoxLimits == 0);
	}

	return true;
}

// ---------------------------------------------------------------------------
//  AppendPhysicsAssetLocalLimits
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldAppendPhysicsAssetLocalLimitsTest,
                                 "KawaiiPhysics.SimpleWorld.AppendPhysicsAssetLocalLimits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldAppendPhysicsAssetLocalLimitsTest::RunTest(const FString& Parameters)
{
	using KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding;

	UPhysicsAsset* PhysicsAsset = NewObject<UPhysicsAsset>(GetTransientPackage());

	USkeletalBodySetup* SpineBody = NewObject<USkeletalBodySetup>(PhysicsAsset);
	SpineBody->BoneName = TEXT("spine");
	FKSphereElem SpineSphere;
	SpineSphere.Radius = 8.0f;
	SpineBody->AggGeom.SphereElems.Add(SpineSphere);
	PhysicsAsset->SkeletalBodySetups.Add(SpineBody);

	USkeletalBodySetup* HandBody = NewObject<USkeletalBodySetup>(PhysicsAsset);
	HandBody->BoneName = TEXT("hand_l");
	FKSphylElem HandCapsule;
	HandCapsule.Radius = 3.0f;
	HandCapsule.Length = 12.0f;
	HandBody->AggGeom.SphylElems.Add(HandCapsule);
	FKSphereElem HandNoCollisionSphere;
	HandNoCollisionSphere.Radius = 6.0f;
	HandNoCollisionSphere.SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HandBody->AggGeom.SphereElems.Add(HandNoCollisionSphere);
	PhysicsAsset->SkeletalBodySetups.Add(HandBody);

	USkeletalBodySetup* UnknownBody = NewObject<USkeletalBodySetup>(PhysicsAsset);
	UnknownBody->BoneName = TEXT("unknown");
	FKBoxElem UnknownBox;
	UnknownBox.X = 4.0f;
	UnknownBox.Y = 4.0f;
	UnknownBox.Z = 4.0f;
	UnknownBody->AggGeom.BoxElems.Add(UnknownBox);
	PhysicsAsset->SkeletalBodySetups.Add(UnknownBody);

	USkeletalBodySetup* HeadBody = NewObject<USkeletalBodySetup>(PhysicsAsset);
	HeadBody->BoneName = TEXT("head");
	HeadBody->CollisionReponse = EBodyCollisionResponse::BodyCollision_Disabled;
	FKSphereElem HeadSphere;
	HeadSphere.Radius = 5.0f;
	HeadBody->AggGeom.SphereElems.Add(HeadSphere);
	PhysicsAsset->SkeletalBodySetups.Add(HeadBody);

	FReferenceSkeleton RefSkeleton;
	{
		FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("spine"), TEXT("spine"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("head"), TEXT("head"), 1), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hand_l"), TEXT("hand_l"), 1), FTransform::Identity);
	}

	{
		FKawaiiPhysicsSharedCollisionData OutLimits;
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding> Bindings;
		const int32 NumBodies = KawaiiPhysicsSimpleWorldCollision::AppendPhysicsAssetLocalLimits(
			*PhysicsAsset,
			RefSkeleton,
			FVector::OneVector,
			EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
			32,
			OutLimits,
			Bindings);

		TestTrue(TEXT("Only known and enabled bodies are accepted"), NumBodies == 2 && Bindings.Num() == 2);
		if (Bindings.Num() == 2)
		{
			TestTrue(TEXT("Bindings are sorted by bone index"),
			         Bindings[0].BoneIndex == 1 && Bindings[1].BoneIndex == 3);
			TestTrue(TEXT("Spine contributes one sphere"),
			         Bindings[0].NumSphericalLimits == 1 && Bindings[0].NumCapsuleLimits == 0);
			TestTrue(TEXT("Hand contributes one capsule"),
			         Bindings[1].NumCapsuleLimits == 1 && Bindings[1].NumSphericalLimits == 0);
			TestTrue(TEXT("Hand NoCollision sphere is ignored"),
			         Bindings[1].NumSphericalLimits == 0 && OutLimits.SphericalLimits.Num() == 1);
		}
	}

	{
		FKawaiiPhysicsSharedCollisionData OutLimits;
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding> Bindings;
		const int32 NumBodies = KawaiiPhysicsSimpleWorldCollision::AppendPhysicsAssetLocalLimits(
			*PhysicsAsset,
			RefSkeleton,
			FVector::OneVector,
			EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
			1,
			OutLimits,
			Bindings);

		TestTrue(TEXT("MaxBodies=1 accepts only spine"), NumBodies == 1 && Bindings.Num() == 1);
		TestTrue(TEXT("MaxBodies=1 keeps the lowest bone index body"),
		         Bindings.Num() == 1 && Bindings[0].BoneIndex == 1);
	}

	{
		UPhysicsAsset* DisabledBodyPhysicsAsset = NewObject<UPhysicsAsset>(GetTransientPackage());

		USkeletalBodySetup* DisabledSpineBody = NewObject<USkeletalBodySetup>(DisabledBodyPhysicsAsset);
		DisabledSpineBody->BoneName = TEXT("spine");
		DisabledSpineBody->CollisionReponse = EBodyCollisionResponse::BodyCollision_Disabled;
		FKSphereElem DisabledSpineSphere;
		DisabledSpineSphere.Radius = 8.0f;
		DisabledSpineBody->AggGeom.SphereElems.Add(DisabledSpineSphere);
		DisabledBodyPhysicsAsset->SkeletalBodySetups.Add(DisabledSpineBody);

		USkeletalBodySetup* DisabledHandBody = NewObject<USkeletalBodySetup>(DisabledBodyPhysicsAsset);
		DisabledHandBody->BoneName = TEXT("hand_l");
		DisabledHandBody->CollisionReponse = EBodyCollisionResponse::BodyCollision_Disabled;
		FKSphylElem DisabledHandCapsule;
		DisabledHandCapsule.Radius = 3.0f;
		DisabledHandCapsule.Length = 12.0f;
		DisabledHandBody->AggGeom.SphylElems.Add(DisabledHandCapsule);
		DisabledBodyPhysicsAsset->SkeletalBodySetups.Add(DisabledHandBody);

		FKawaiiPhysicsSharedCollisionData OutLimits;
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding> Bindings;
		const int32 NumBodies = KawaiiPhysicsSimpleWorldCollision::AppendPhysicsAssetLocalLimits(
			*DisabledBodyPhysicsAsset,
			RefSkeleton,
			FVector::OneVector,
			EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
			32,
			OutLimits,
			Bindings);

		TestTrue(TEXT("All disabled bodies accept no bodies"), NumBodies == 0);
		TestTrue(TEXT("All disabled bodies leave bindings empty"), Bindings.IsEmpty());
		TestTrue(TEXT("All disabled bodies leave limits empty"), OutLimits.IsEmpty());
	}

	{
		UPhysicsAsset* NoCollisionShapePhysicsAsset = NewObject<UPhysicsAsset>(GetTransientPackage());

		USkeletalBodySetup* NoCollisionSpineBody = NewObject<USkeletalBodySetup>(NoCollisionShapePhysicsAsset);
		NoCollisionSpineBody->BoneName = TEXT("spine");
		FKSphereElem NoCollisionSpineSphere;
		NoCollisionSpineSphere.Radius = 8.0f;
		NoCollisionSpineSphere.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NoCollisionSpineBody->AggGeom.SphereElems.Add(NoCollisionSpineSphere);
		NoCollisionShapePhysicsAsset->SkeletalBodySetups.Add(NoCollisionSpineBody);

		USkeletalBodySetup* NoCollisionHandBody = NewObject<USkeletalBodySetup>(NoCollisionShapePhysicsAsset);
		NoCollisionHandBody->BoneName = TEXT("hand_l");
		FKSphylElem NoCollisionHandCapsule;
		NoCollisionHandCapsule.Radius = 3.0f;
		NoCollisionHandCapsule.Length = 12.0f;
		NoCollisionHandCapsule.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NoCollisionHandBody->AggGeom.SphylElems.Add(NoCollisionHandCapsule);
		NoCollisionShapePhysicsAsset->SkeletalBodySetups.Add(NoCollisionHandBody);

		FKawaiiPhysicsSharedCollisionData OutLimits;
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding> Bindings;
		const int32 NumBodies = KawaiiPhysicsSimpleWorldCollision::AppendPhysicsAssetLocalLimits(
			*NoCollisionShapePhysicsAsset,
			RefSkeleton,
			FVector::OneVector,
			EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
			32,
			OutLimits,
			Bindings);

		TestTrue(TEXT("All NoCollision shapes accept no bodies"), NumBodies == 0);
		TestTrue(TEXT("All NoCollision shapes leave bindings empty"), Bindings.IsEmpty());
		TestTrue(TEXT("All NoCollision shapes leave limits empty"), OutLimits.IsEmpty());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
