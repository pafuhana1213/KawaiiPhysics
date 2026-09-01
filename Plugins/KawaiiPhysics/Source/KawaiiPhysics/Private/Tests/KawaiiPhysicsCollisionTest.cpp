// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"

// コリジョン押し出しの正しさ（解析的基準値）。
// 各形状: ボーン(半径r)が形状に食い込んだとき、表面+r へ正しく押し出されることを検証。

namespace
{
	// ボーン1個を生成（位置・前フレーム位置・コリジョン半径）
	FKawaiiPhysicsModifyBone MakeBone(const FVector& Location, float Radius,
	                                  const FVector& PrevLocation)
	{
		FKawaiiPhysicsModifyBone Bone;
		Bone.Location = Location;
		Bone.PrevLocation = PrevLocation;
		Bone.PoseLocation = Location;
		Bone.PhysicsSettings.Radius = Radius;
		return Bone;
	}

	TArray<FPlane> MakeUnitCubeConvexPlanes()
	{
		TArray<FPlane> Planes;
		Planes.Reserve(6);
		Planes.Add(FPlane(1.0f, 0.0f, 0.0f, 1.0f));
		Planes.Add(FPlane(-1.0f, 0.0f, 0.0f, 1.0f));
		Planes.Add(FPlane(0.0f, 1.0f, 0.0f, 1.0f));
		Planes.Add(FPlane(0.0f, -1.0f, 0.0f, 1.0f));
		Planes.Add(FPlane(0.0f, 0.0f, 1.0f, 1.0f));
		Planes.Add(FPlane(0.0f, 0.0f, -1.0f, 1.0f));
		return Planes;
	}

	FKawaiiPhysicsConvexLimit MakeUnitCubeConvex(
		const FVector& Location = FVector::ZeroVector,
		const FQuat& Rotation = FQuat::Identity)
	{
		FKawaiiPhysicsConvexLimit Convex;
		Convex.Location = Location;
		Convex.Rotation = Rotation;
		Convex.LocalPlanes = MakeUnitCubeConvexPlanes();
		Convex.LocalBounds = FBox(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f));
		Convex.bEnable = true;
		return Convex;
	}

	constexpr float GCollisionTol = 0.01f; // 0.1mm スケール
}

// ---------------------------------------------------------------------------
//  Sphere (Outer)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSphereOuterTest,
                                 "KawaiiPhysics.Collision.SphereOuterPushOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSphereOuterTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;

	// 中心 O, 半径 10。ボーン半径 3 が (5,0,0) に食い込み → 中心から 13 の表面 (13,0,0) へ。
	FKawaiiPhysicsModifyBone Bone = MakeBone(FVector(5, 0, 0), 3.0f, FVector(5, 0, 0));

	TArray<FSphericalLimit> Limits;
	FSphericalLimit Sphere;
	Sphere.Location = FVector::ZeroVector;
	Sphere.Radius = 10.0f;
	Sphere.LimitType = ESphericalLimitType::Outer;
	Sphere.bEnable = true;
	Limits.Add(Sphere);

	A.CallSphereCollision(Bone, Limits);

	const FVector Expected(13, 0, 0);
	TestTrue(FString::Printf(TEXT("Sphere push-out: got %s expected %s"),
	                         *Bone.Location.ToString(), *Expected.ToString()),
	         Bone.Location.Equals(Expected, GCollisionTol));

	// 表面の外側にあるボーンは動かさない
	FKawaiiPhysicsModifyBone Outside = MakeBone(FVector(20, 0, 0), 3.0f, FVector(20, 0, 0));
	A.CallSphereCollision(Outside, Limits);
	TestTrue(TEXT("Sphere: bone outside is untouched"),
	         Outside.Location.Equals(FVector(20, 0, 0), GCollisionTol));

	// Inner タイプ: 内側に閉じ込める。inner limit = max(R - boneR, 0) = 7。距離 10 は外なので 7 へ引き戻し。
	FKawaiiPhysicsModifyBone Inner = MakeBone(FVector(10, 0, 0), 3.0f, FVector(10, 0, 0));
	TArray<FSphericalLimit> InnerLimits;
	FSphericalLimit InnerSphere;
	InnerSphere.Location = FVector::ZeroVector;
	InnerSphere.Radius = 10.0f;
	InnerSphere.LimitType = ESphericalLimitType::Inner;
	InnerSphere.bEnable = true;
	InnerLimits.Add(InnerSphere);
	A.CallSphereCollision(Inner, InnerLimits);
	TestTrue(FString::Printf(TEXT("Sphere inner pull-in: got %s expected (7,0,0)"), *Inner.Location.ToString()),
	         Inner.Location.Equals(FVector(7, 0, 0), GCollisionTol));

	return true;
}

// ---------------------------------------------------------------------------
//  Capsule
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCapsuleTest,
                                 "KawaiiPhysics.Collision.CapsulePushOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCapsuleTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;

	// Z 軸カプセル, 中心 O, 長さ 10, 半径 4。ボーン半径 3 が (2,0,0) に食い込み。
	// 軸への最近点 (0,0,0)、LimitDistance = 3+4 = 7 → (7,0,0)。
	FKawaiiPhysicsModifyBone Bone = MakeBone(FVector(2, 0, 0), 3.0f, FVector(2, 0, 0));

	TArray<FCapsuleLimit> Limits;
	FCapsuleLimit Capsule;
	Capsule.Location = FVector::ZeroVector;
	Capsule.Rotation = FQuat::Identity;
	Capsule.Length = 10.0f;
	Capsule.Radius = 4.0f;
	Capsule.bEnable = true;
	Limits.Add(Capsule);

	A.CallCapsuleCollision(Bone, Limits);

	const FVector Expected(7, 0, 0);
	TestTrue(FString::Printf(TEXT("Capsule push-out: got %s expected %s"),
	                         *Bone.Location.ToString(), *Expected.ToString()),
	         Bone.Location.Equals(Expected, GCollisionTol));

	return true;
}

// ---------------------------------------------------------------------------
//  Tapered Capsule
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTaperedCapsuleTest,
                                 "KawaiiPhysics.Collision.TaperedCapsulePushOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTaperedCapsuleTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;

	TArray<FTaperedCapsuleLimit> Limits;
	FTaperedCapsuleLimit TaperedCapsule;
	TaperedCapsule.Location = FVector::ZeroVector;
	TaperedCapsule.Rotation = FQuat::Identity;
	TaperedCapsule.Length = 10.0f;
	TaperedCapsule.Radius0 = 6.0f; // +Z 端
	TaperedCapsule.Radius1 = 2.0f; // -Z 端
	TaperedCapsule.bEnable = true;
	Limits.Add(TaperedCapsule);

	auto TestPushOut = [&](const TCHAR* CaseName, const FVector& InitialLocation, const FVector& ExpectedLocation)
	{
		FKawaiiPhysicsModifyBone Bone = MakeBone(InitialLocation, 3.0f, InitialLocation);
		A.CallTaperedCapsuleCollision(Bone, Limits);
		TestTrue(FString::Printf(TEXT("Tapered capsule %s: got %s expected %s"),
		                         CaseName, *Bone.Location.ToString(), *ExpectedLocation.ToString()),
		         Bone.Location.Equals(ExpectedLocation, GCollisionTol));
	};

	// 軸中央 t=0.5: R=Lerp(6,2,0.5)=4, LimitDistance=3+4=7 → (7,0,0)。
	TestPushOut(TEXT("center t=0.5"), FVector(1, 0, 0), FVector(7, 0, 0));

	// 非対称 t=0.25: R=Lerp(6,2,0.25)=5, LimitDistance=3+5=8 → (8,0,2.5)。
	TestPushOut(TEXT("asymmetric t=0.25"), FVector(1, 0, 2.5f), FVector(8, 0, 2.5f));

	// +Z 端クランプ: 最近点 (0,0,5), R=Radius0=6, LimitDistance=9 → (0,0,14)。
	TestPushOut(TEXT("+Z clamp"), FVector(0, 0, 10), FVector(0, 0, 14));

	// -Z 端クランプ: 最近点 (0,0,-5), R=Radius1=2, LimitDistance=5 → (0,0,-10)。
	TestPushOut(TEXT("-Z clamp"), FVector(0, 0, -8), FVector(0, 0, -10));

	// 非接触: 最近点 (0,0,5), dist=15 > LimitDistance=9 のため不変。
	TestPushOut(TEXT("outside"), FVector(0, 0, 20), FVector(0, 0, 20));

	FTaperedCapsuleLimit DegenerateCapsule = TaperedCapsule;
	DegenerateCapsule.Length = 0.0f;
	TArray<FTaperedCapsuleLimit> DegenerateLimits;
	DegenerateLimits.Add(DegenerateCapsule);

	// 縮退 Length=0: 球扱い R=Max(6,2)=6, LimitDistance=9 → (9,0,0)。
	FKawaiiPhysicsModifyBone DegenerateBone = MakeBone(FVector(2, 0, 0), 3.0f, FVector(2, 0, 0));
	A.CallTaperedCapsuleCollision(DegenerateBone, DegenerateLimits);
	TestTrue(FString::Printf(TEXT("Tapered capsule degenerate length: got %s expected (9,0,0)"),
	                         *DegenerateBone.Location.ToString()),
	         DegenerateBone.Location.Equals(FVector(9, 0, 0), GCollisionTol));

	// 軸上フォールバック: PushDir=Rotation.GetAxisX()、t=0.5, LimitDistance=7 → (7,0,0)。
	TestPushOut(TEXT("axis fallback"), FVector(0, 0, 0), FVector(7, 0, 0));

	FTaperedCapsuleLimit NegativeEndCapsule = TaperedCapsule;
	NegativeEndCapsule.Radius0 = -2.0f;
	NegativeEndCapsule.Radius1 = 6.0f;
	TArray<FTaperedCapsuleLimit> NegativeEndLimits;
	NegativeEndLimits.Add(NegativeEndCapsule);

	// 片端負半径: +Z端で t=0, R=Max(Lerp(-2,6,0),0)=0, LimitDistance=3+0=3 → (3,0,5)。
	FKawaiiPhysicsModifyBone NegativeEndBone = MakeBone(FVector(1, 0, 5), 3.0f, FVector(1, 0, 5));
	A.CallTaperedCapsuleCollision(NegativeEndBone, NegativeEndLimits);
	TestTrue(FString::Printf(TEXT("Tapered capsule negative end radius: got %s expected (3,0,5)"),
	                         *NegativeEndBone.Location.ToString()),
	         NegativeEndBone.Location.Equals(FVector(3, 0, 5), GCollisionTol));

	return true;
}

// ---------------------------------------------------------------------------
//  Box
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoxTest,
                                 "KawaiiPhysics.Collision.BoxPushOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoxTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;

	// 原点・無回転・extent 10 のボックス。ボーン半径 3 が (12,0,0)（面 X=10 の外側 2）に食い込み。
	// 最近点 (10,0,0)、押し出し → (10,0,0)+(1,0,0)*3 = (13,0,0)。
	FKawaiiPhysicsModifyBone Bone = MakeBone(FVector(12, 0, 0), 3.0f, FVector(12, 0, 0));

	TArray<FBoxLimit> Limits;
	FBoxLimit Box;
	Box.Location = FVector::ZeroVector;
	Box.Rotation = FQuat::Identity;
	Box.Extent = FVector(10, 10, 10);
	Box.bEnable = true;
	Limits.Add(Box);

	A.CallBoxCollision(Bone, Limits);

	const FVector Expected(13, 0, 0);
	TestTrue(FString::Printf(TEXT("Box push-out: got %s expected %s"),
	                         *Bone.Location.ToString(), *Expected.ToString()),
	         Bone.Location.Equals(Expected, GCollisionTol));

	// 完全に内部（buried）ケース。現行アルゴリズムは中心方向へ「半径ぶん」だけ押すため箱から出きらず
	// (8,0,0) で止まる（理想は (13,0,0)）。現挙動の固定＝リグレッション検出用。
	FKawaiiPhysicsModifyBone Buried = MakeBone(FVector(5, 0, 0), 3.0f, FVector(5, 0, 0));
	A.CallBoxCollision(Buried, Limits);
	TestTrue(FString::Printf(TEXT("Box buried push (pins current behavior): got %s expected (8,0,0)"),
	                         *Buried.Location.ToString()),
	         Buried.Location.Equals(FVector(8, 0, 0), GCollisionTol));

	// 中心一致の縮退ケース。修正前はゼロ法線で中心に留まるバグ → 最小貫通軸（X==Y==Z なので +X）へ決定的に押し出し (3,0,0)。
	// 目的は完全脱出でなくゼロ法線バグの解消（buried 同様「半径ぶんのみ押す」ので Box 内部に留まる）。
	FKawaiiPhysicsModifyBone Center = MakeBone(FVector(0, 0, 0), 3.0f, FVector(0, 0, 0));
	A.CallBoxCollision(Center, Limits);
	TestTrue(FString::Printf(TEXT("Box center-coincident push-out: got %s expected (3,0,0)"),
	                         *Center.Location.ToString()),
	         Center.Location.Equals(FVector(3, 0, 0), GCollisionTol));

	return true;
}

// ---------------------------------------------------------------------------
//  Convex
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsConvexTest,
                                 "KawaiiPhysics.Collision.ConvexPushOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsConvexTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;

	{
		TArray<FKawaiiPhysicsConvexLimit> Limits;
		Limits.Add(MakeUnitCubeConvex());

		FKawaiiPhysicsModifyBone NearFace = MakeBone(FVector(1.2f, 0.0f, 0.0f), 0.5f, FVector(1.2f, 0.0f, 0.0f));
		A.CallConvexCollision(NearFace, Limits);
		TestTrue(FString::Printf(TEXT("Convex face push-out: got %s expected (1.5,0,0)"),
		                         *NearFace.Location.ToString()),
		         NearFace.Location.Equals(FVector(1.5f, 0.0f, 0.0f), GCollisionTol));

		FKawaiiPhysicsModifyBone Inside = MakeBone(FVector(0.2f, 0.0f, 0.0f), 0.5f, FVector(0.2f, 0.0f, 0.0f));
		A.CallConvexCollision(Inside, Limits);
		TestTrue(FString::Printf(TEXT("Convex buried push-out: got %s expected (1.5,0,0)"),
		                         *Inside.Location.ToString()),
		         Inside.Location.Equals(FVector(1.5f, 0.0f, 0.0f), GCollisionTol));

		FKawaiiPhysicsModifyBone OutsideBounds = MakeBone(FVector(2.0f, 0.0f, 0.0f), 0.25f, FVector(2.0f, 0.0f, 0.0f));
		A.CallConvexCollision(OutsideBounds, Limits);
		TestTrue(TEXT("Convex local bounds reject leaves the bone untouched"),
		         OutsideBounds.Location.Equals(FVector(2.0f, 0.0f, 0.0f), GCollisionTol));
	}

	{
		TArray<FKawaiiPhysicsConvexLimit> DisabledLimits;
		FKawaiiPhysicsConvexLimit Disabled = MakeUnitCubeConvex();
		Disabled.bEnable = false;
		DisabledLimits.Add(Disabled);
		FKawaiiPhysicsModifyBone DisabledBone = MakeBone(FVector(0.2f, 0.0f, 0.0f), 0.5f, FVector(0.2f, 0.0f, 0.0f));
		A.CallConvexCollision(DisabledBone, DisabledLimits);
		TestTrue(TEXT("Convex disabled leaves the bone untouched"),
		         DisabledBone.Location.Equals(FVector(0.2f, 0.0f, 0.0f), GCollisionTol));

		TArray<FKawaiiPhysicsConvexLimit> EmptyPlaneLimits;
		FKawaiiPhysicsConvexLimit EmptyPlanes = MakeUnitCubeConvex();
		EmptyPlanes.LocalPlanes.Reset();
		EmptyPlaneLimits.Add(EmptyPlanes);
		FKawaiiPhysicsModifyBone EmptyPlaneBone = MakeBone(FVector(0.2f, 0.0f, 0.0f), 0.5f, FVector(0.2f, 0.0f, 0.0f));
		A.CallConvexCollision(EmptyPlaneBone, EmptyPlaneLimits);
		TestTrue(TEXT("Convex empty planes leave the bone untouched"),
		         EmptyPlaneBone.Location.Equals(FVector(0.2f, 0.0f, 0.0f), GCollisionTol));
	}

	{
		const FQuat Rotation(FVector::ZAxisVector, PI / 2.0f);
		const FTransform ConvexTransform(Rotation, FVector(10.0f, 0.0f, 0.0f));
		TArray<FKawaiiPhysicsConvexLimit> Limits;
		Limits.Add(MakeUnitCubeConvex(ConvexTransform.GetLocation(), ConvexTransform.GetRotation()));

		const FVector Initial = ConvexTransform.TransformPosition(FVector(0.8f, 0.0f, 0.0f));
		const FVector Expected = ConvexTransform.TransformPosition(FVector(1.5f, 0.0f, 0.0f));
		FKawaiiPhysicsModifyBone Rotated = MakeBone(Initial, 0.5f, Initial);
		A.CallConvexCollision(Rotated, Limits);
		TestTrue(FString::Printf(TEXT("Convex rotated push-out: got %s expected %s"),
		                         *Rotated.Location.ToString(), *Expected.ToString()),
		         Rotated.Location.Equals(Expected, GCollisionTol));
	}

	{
		TArray<FKawaiiPhysicsConvexLimit> Limits;
		Limits.Add(MakeUnitCubeConvex());

		FKawaiiPhysicsModifyBone Edge = MakeBone(FVector(1.2f, 1.2f, 0.0f), 0.5f, FVector(1.2f, 1.2f, 0.0f));
		A.CallConvexCollision(Edge, Limits);
		const FVector Once = Edge.Location;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			A.CallConvexCollision(Edge, Limits);
		}
		TestTrue(FString::Printf(TEXT("Convex edge repeated push-out is stable: got %s once %s"),
		                         *Edge.Location.ToString(), *Once.ToString()),
		         Edge.Location.Equals(Once, GCollisionTol));

		FKawaiiPhysicsModifyBone TieA = MakeBone(FVector(0.8f, 0.8f, 0.0f), 0.5f, FVector(0.8f, 0.8f, 0.0f));
		FKawaiiPhysicsModifyBone TieB = MakeBone(FVector(0.8f, 0.8f, 0.0f), 0.5f, FVector(0.8f, 0.8f, 0.0f));
		A.CallConvexCollision(TieA, Limits);
		A.CallConvexCollision(TieB, Limits);
		TestTrue(TEXT("Convex equal plane distance picks deterministically"),
		         TieA.Location.Equals(TieB.Location, GCollisionTol));
		TestTrue(TEXT("Convex equal plane distance keeps the first plane"),
		         TieA.Location.Equals(FVector(1.5f, 0.8f, 0.0f), GCollisionTol));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  Planar
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPlanarTest,
                                 "KawaiiPhysics.Collision.PlanarPushOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPlanarTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;

	// z=0 平面（法線 +Z）。ボーン半径 3 が平面の下 (0,0,-5)、前フレーム (0,0,5) で平面をまたぐ。
	// → 平面射影 (0,0,0) + UpVector*3 = (0,0,3)。
	FKawaiiPhysicsModifyBone Bone = MakeBone(FVector(0, 0, -5), 3.0f, FVector(0, 0, 5));

	TArray<FPlanarLimit> Limits;
	FPlanarLimit Planar;
	Planar.Location = FVector::ZeroVector;
	Planar.Rotation = FQuat::Identity; // UpVector = +Z
	Planar.Plane = FPlane(0, 0, 1, 0); // z = 0
	Planar.bEnable = true;
	Limits.Add(Planar);

	A.CallPlanarCollision(Bone, Limits);

	const FVector Expected(0, 0, 3);
	TestTrue(FString::Printf(TEXT("Planar push-out: got %s expected %s"),
	                         *Bone.Location.ToString(), *Expected.ToString()),
	         Bone.Location.Equals(Expected, GCollisionTol));

	return true;
}

// ---------------------------------------------------------------------------
//  Angle Limit
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAngleLimitTest,
                                 "KawaiiPhysics.Collision.AngleLimit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAngleLimitTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;

	// 親原点、ポーズ方向 +X（child pose (10,0,0)）。現在 child は (0,10,0)=+Y（ポーズから 90°）。
	// LimitAngle 30° → 30° まで戻され、長さ 10 を保って (10cos30, 10sin30, 0)=(8.660,5,0)。
	FKawaiiPhysicsModifyBone Parent;
	Parent.Location = FVector::ZeroVector;
	Parent.PoseLocation = FVector::ZeroVector;

	FKawaiiPhysicsModifyBone Child;
	Child.Location = FVector(0, 10, 0);
	Child.PoseLocation = FVector(10, 0, 0);
	Child.PhysicsSettings.LimitAngle = 30.0f;

	A.CallAngleLimit(Child, Parent);

	const FVector Expected(10.0f * FMath::Cos(FMath::DegreesToRadians(30.0f)),
	                       10.0f * FMath::Sin(FMath::DegreesToRadians(30.0f)), 0.0f);
	TestTrue(FString::Printf(TEXT("Angle limit: got %s expected %s"),
	                         *Child.Location.ToString(), *Expected.ToString()),
	         Child.Location.Equals(Expected, GCollisionTol));

	// 距離（ボーン長）が保存されること
	TestTrue(TEXT("Angle limit preserves bone length"),
	         FMath::IsNearlyEqual(static_cast<float>((Child.Location - Parent.Location).Size()), 10.0f, GCollisionTol));

	// 制限角度内のボーンは動かさない
	FKawaiiPhysicsModifyBone Within;
	Within.Location = FVector(10, 0, 0); // ポーズと一致（0°）
	Within.PoseLocation = FVector(10, 0, 0);
	Within.PhysicsSettings.LimitAngle = 30.0f;
	A.CallAngleLimit(Within, Parent);
	TestTrue(TEXT("Angle limit: bone within limit is untouched"),
	         Within.Location.Equals(FVector(10, 0, 0), GCollisionTol));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
