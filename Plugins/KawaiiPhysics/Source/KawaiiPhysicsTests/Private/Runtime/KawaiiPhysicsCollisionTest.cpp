// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Animation/Skeleton.h"
#include "Misc/EngineVersionComparison.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/TaperedCapsuleElem.h"
#include "ReferenceSkeleton.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

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

	FTaperedCapsuleLimit StrongTaper;
	StrongTaper.Location = FVector::ZeroVector;
	StrongTaper.Rotation = FQuat::Identity;
	StrongTaper.Length = 5.0f;
	StrongTaper.Radius0 = 10.0f;
	StrongTaper.Radius1 = 1.0f;
	StrongTaper.bEnable = true;
	TArray<FTaperedCapsuleLimit> StrongTaperLimits{StrongTaper};

	// |R0-R1| >= Length: 小さい端球は大きい端球に包含されるため、+Z端の半径10の球として押し出す。
	// 大球中心は(0,0,2.5)、Bone半径2を加えた押し出し距離は12なので、-Z方向の(0,0,-9.5)へ移動する。
	FKawaiiPhysicsModifyBone StrongTaperBone = MakeBone(FVector(0, 0, -2.5f), 2.0f, FVector(0, 0, -2.5f));
	A.CallTaperedCapsuleCollision(StrongTaperBone, StrongTaperLimits);
	TestTrue(FString::Printf(TEXT("Strong taper falls back to larger endpoint sphere: got %s expected (0,0,-9.5)"),
	                         *StrongTaperBone.Location.ToString()),
	         StrongTaperBone.Location.Equals(FVector(0, 0, -9.5f), GCollisionTol));

	FTaperedCapsuleLimit BoundaryTaper = StrongTaper;
	BoundaryTaper.Length = 5.0f;
	BoundaryTaper.Radius0 = 1.0f;
	BoundaryTaper.Radius1 = 6.0f;
	TArray<FTaperedCapsuleLimit> BoundaryTaperLimits{BoundaryTaper};

	// |R0-R1| == Length の境界でも縮退し、半径が大きい -Z 端を球中心として選ぶ。
	FKawaiiPhysicsModifyBone BoundaryTaperBone = MakeBone(FVector(0, 0, 2.5f), 1.0f, FVector(0, 0, 2.5f));
	A.CallTaperedCapsuleCollision(BoundaryTaperBone, BoundaryTaperLimits);
	TestTrue(FString::Printf(TEXT("Boundary taper uses larger -Z endpoint sphere: got %s expected (0,0,4.5)"),
	                         *BoundaryTaperBone.Location.ToString()),
	         BoundaryTaperBone.Location.Equals(FVector(0, 0, 4.5f), GCollisionTol));

	return true;
}

// PhysicsAsset の FKTaperedCapsuleElem も Radius0 が +Z 端のため、半径を入れ替えずにそのまま取り込むことを確認
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTaperedCapsulePhysicsAssetImportTest,
	"KawaiiPhysics.Collision.TaperedCapsulePhysicsAssetImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTaperedCapsulePhysicsAssetImportTest::RunTest(const FString& Parameters)
{
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage());
	{
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("taper_bone"), TEXT("taper_bone"), 0), FTransform::Identity);
	}

	TArray<FBoneIndexType> RequiredIndexes{0, 1};
	FBoneContainer RequiredBones;
	RequiredBones.InitializeTo(RequiredIndexes, UE::Anim::FCurveFilterSettings(), *Skeleton);

	UPhysicsAsset* PhysicsAsset = NewObject<UPhysicsAsset>(GetTransientPackage());
	USkeletalBodySetup* BodySetup = NewObject<USkeletalBodySetup>(PhysicsAsset);
	BodySetup->BoneName = TEXT("taper_bone");
	FKTaperedCapsuleElem Elem;
	Elem.Center = FVector(1.0f, 2.0f, 3.0f);
	Elem.Rotation = FRotator(10.0f, 20.0f, 30.0f);
	Elem.Radius0 = 8.0f;
	Elem.Radius1 = 3.0f;
	Elem.Length = 14.0f;
	BodySetup->AggGeom.TaperedCapsuleElems.Add(Elem);
	PhysicsAsset->SkeletalBodySetups.Add(BodySetup);

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.PhysicsAssetForLimits = PhysicsAsset;
	Accessor.ApplyPhysicsAsset(RequiredBones);

	TestEqual(TEXT("PhysicsAsset imports one tapered capsule"), Accessor.Node.TaperedCapsuleLimitsData.Num(), 1);
	if (Accessor.Node.TaperedCapsuleLimitsData.Num() == 1)
	{
		const FTaperedCapsuleLimit& Imported = Accessor.Node.TaperedCapsuleLimitsData[0];
		TestEqual(TEXT("Imported driving bone"), Imported.DrivingBone.BoneName, FName(TEXT("taper_bone")));
		TestTrue(TEXT("Imported center"), Imported.OffsetLocation.Equals(Elem.Center, GCollisionTol));
		TestTrue(TEXT("Imported rotation"), Imported.OffsetRotation.Equals(Elem.Rotation, GCollisionTol));
		TestTrue(TEXT("Imported Radius0"), FMath::IsNearlyEqual(Imported.Radius0, Elem.Radius0, GCollisionTol));
		TestTrue(TEXT("Imported Radius1"), FMath::IsNearlyEqual(Imported.Radius1, Elem.Radius1, GCollisionTol));
		TestTrue(TEXT("Imported length"), FMath::IsNearlyEqual(Imported.Length, Elem.Length, GCollisionTol));
		TestTrue(TEXT("Imported source type"), Imported.SourceType == ECollisionSourceType::PhysicsAsset);
	}

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

	// 完全に内部のケース。最近面 X=10 までの貫通深さ 5 + 半径 3 だけ押し出す。
	FKawaiiPhysicsModifyBone Buried = MakeBone(FVector(5, 0, 0), 3.0f, FVector(5, 0, 0));
	A.CallBoxCollision(Buried, Limits);
	TestTrue(FString::Printf(TEXT("Box buried push-out: got %s expected (13,0,0)"),
	                         *Buried.Location.ToString()),
	         Buried.Location.Equals(FVector(13, 0, 0), GCollisionTol));

	// 中心一致では最小貫通軸（X==Y==Z なので +X）へ、貫通深さ 10 + 半径 3 だけ押し出す。
	FKawaiiPhysicsModifyBone Center = MakeBone(FVector(0, 0, 0), 3.0f, FVector(0, 0, 0));
	A.CallBoxCollision(Center, Limits);
	TestTrue(FString::Printf(TEXT("Box center-coincident push-out: got %s expected (13,0,0)"),
	                         *Center.Location.ToString()),
	         Center.Location.Equals(FVector(13, 0, 0), GCollisionTol));

	return true;
}

// 内部の最近面への押し出し。地面 Box でも端では側面から横に抜ける仕様を含む。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBoxInteriorPushOutToNearestFaceTest,
                                 "KawaiiPhysics.Collision.BoxInteriorPushOutToNearestFace",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBoxInteriorPushOutToNearestFaceTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	FBoxLimit Box;
	Box.Location = FVector::ZeroVector;
	Box.Rotation = FQuat::Identity;
	Box.Extent = FVector(100, 100, 2);
	Box.bEnable = true;
	Box.UpdateRuntimeCache();
	TArray<FBoxLimit> Limits = {Box};

	auto CheckPushOut = [&](const TCHAR* Label, const FVector& Start, float Radius, const FVector& Expected)
	{
		FKawaiiPhysicsModifyBone Bone = MakeBone(Start, Radius, Start);
		A.CallBoxCollision(Bone, Limits);
		TestTrue(FString::Printf(TEXT("%s: got %s expected %s"),
		                         Label, *Bone.Location.ToString(), *Expected.ToString()),
		         Bone.Location.Equals(Expected, 1e-4));
	};

	// (a) 最小貫通深さは Z の 1.5。正側へ押し出し、他の軸は保つ。
	CheckPushOut(TEXT("Interior positive Z"), FVector(30, -20, 0.5), 1.0f, FVector(30, -20, 3));
	// (b) 負側の最近面へ押し出す。
	CheckPushOut(TEXT("Interior negative Z"), FVector(30, -20, -0.5), 1.0f, FVector(30, -20, -3));
	// (c) 中心一致でも最も薄い軸の正側へ押し出す。
	CheckPushOut(TEXT("Thin box center"), FVector::ZeroVector, 1.0f, FVector(0, 0, 3));

	// (d) 立方体の中心では全軸同値なので +X を選ぶ。
	Limits[0].Extent = FVector(5, 5, 5);
	CheckPushOut(TEXT("Cube center tie chooses X"), FVector::ZeroVector, 1.0f, FVector(6, 0, 0));
	Limits[0].Extent = Box.Extent;

	// (e) 境界上は内部と同じ経路で半径ぶん押し出す。
	CheckPushOut(TEXT("On top face"), FVector(10, 0, 2), 1.0f, FVector(10, 0, 3));
	// (f) 半径ゼロでも面まで押し出す。
	CheckPushOut(TEXT("Zero radius inside"), FVector(10, 0, 1), 0.0f, FVector(10, 0, 2));

	// (g) Z 軸正方向に 90 度回転し、ローカル (10,0,3) はワールド (0,10,3) になる。
	Limits[0].Rotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(90.0f));
	CheckPushOut(TEXT("Rotated box interior"), FVector(0, 10, 1), 1.0f, FVector(0, 10, 3));
	Limits[0].Rotation = FQuat::Identity;

	// (h) 外部の交差は従来の最近点 + 法線 * 半径、非交差は不変。
	CheckPushOut(TEXT("Exterior overlapping"), FVector(0, 0, 2.5), 1.0f, FVector(0, 0, 3));
	CheckPushOut(TEXT("Exterior separated"), FVector(0, 0, 3.5), 1.0f, FVector(0, 0, 3.5));
	// (i) 地面 Box でも端では側面が最近面となり、横に抜ける。
	CheckPushOut(TEXT("Nearest side face at ground edge"), FVector(99.5, 0, 0), 1.0f, FVector(101, 0, 0));

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
