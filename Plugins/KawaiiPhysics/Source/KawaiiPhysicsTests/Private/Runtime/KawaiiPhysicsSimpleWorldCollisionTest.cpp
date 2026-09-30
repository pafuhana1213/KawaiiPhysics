// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "KawaiiPhysicsTestHarness.h"
#include "AnimNode_KawaiiPhysicsInternal.h"
#include "KawaiiPhysicsSimpleWorldCollision.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "Misc/EngineVersionComparison.h"
#include "PhysicsEngine/PhysicsAsset.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"

#include <limits>

KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsSimpleWorldRegistryX, "KawaiiPhysics.Test.SimpleWorld.Registry.X");
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsSimpleWorldRegistryY, "KawaiiPhysics.Test.SimpleWorld.Registry.Y");

// シンプルワールドコリジョン（KawaiiPhysicsSimpleWorldCollision namespace / SharedCollisionSubsystem の関連構造体）の単体テスト。
// AggGeom→Limit変換、ローカル→ワールド変換、フェード、Desc Merge、Entryのライフサイクル、ハーネス経由のpush-out統合を検証する。

namespace
{
	constexpr float GSimpleWorldTol = 0.001f;
	constexpr float GSimpleWorldPushOutTol = 0.01f; // 0.1mm スケール（他コリジョンテストと同じ粒度）

	TArray<FPlane> MakeUnitCubePlanes()
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

	TArray<FVector> MakeUnitCubeVertices()
	{
		TArray<FVector> Vertices;
		Vertices.Reserve(8);
		Vertices.Add(FVector(-1.0f, -1.0f, -1.0f));
		Vertices.Add(FVector(1.0f, -1.0f, -1.0f));
		Vertices.Add(FVector(1.0f, 1.0f, -1.0f));
		Vertices.Add(FVector(-1.0f, 1.0f, -1.0f));
		Vertices.Add(FVector(-1.0f, -1.0f, 1.0f));
		Vertices.Add(FVector(1.0f, -1.0f, 1.0f));
		Vertices.Add(FVector(1.0f, 1.0f, 1.0f));
		Vertices.Add(FVector(-1.0f, 1.0f, 1.0f));
		return Vertices;
	}

	void AddQuadTriangles(TArray<int32>& Indices, int32 Index0, int32 Index1, int32 Index2, int32 Index3)
	{
		Indices.Add(Index0);
		Indices.Add(Index1);
		Indices.Add(Index2);
		Indices.Add(Index0);
		Indices.Add(Index2);
		Indices.Add(Index3);
	}

	TArray<int32> MakeUnitCubeTriangleIndices()
	{
		TArray<int32> Indices;
		Indices.Reserve(36);
		AddQuadTriangles(Indices, 0, 3, 2, 1);
		AddQuadTriangles(Indices, 4, 5, 6, 7);
		AddQuadTriangles(Indices, 0, 4, 7, 3);
		AddQuadTriangles(Indices, 1, 2, 6, 5);
		AddQuadTriangles(Indices, 0, 1, 5, 4);
		AddQuadTriangles(Indices, 3, 7, 6, 2);
		return Indices;
	}

	const FPlane* FindPlaneWithNormal(TArrayView<const FPlane> Planes, const FVector& ExpectedNormal)
	{
		for (const FPlane& Plane : Planes)
		{
			if (FVector(Plane.X, Plane.Y, Plane.Z).Equals(ExpectedNormal, GSimpleWorldTol))
			{
				return &Plane;
			}
		}
		return nullptr;
	}

	FKawaiiPhysicsSharedCollisionData MakeReadPathWorldData(const FVector& Offset)
	{
		FKawaiiPhysicsSharedCollisionData Data;

		FSphericalLimit SphereA;
		SphereA.Location = Offset + FVector(10.0f, 0.0f, 20.0f);
		SphereA.Rotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(15.0f));
		SphereA.Radius = 12.0f;
		SphereA.LimitType = ESphericalLimitType::Outer;
		SphereA.bEnable = true;
		SphereA.SourceType = ECollisionSourceType::SimpleWorld;
		Data.SphericalLimits.Add(SphereA);

		FSphericalLimit SphereB;
		SphereB.Location = Offset + FVector(-20.0f, 5.0f, 40.0f);
		SphereB.Rotation = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-20.0f));
		SphereB.Radius = 6.0f;
		SphereB.LimitType = ESphericalLimitType::Inner;
		SphereB.bEnable = true;
		SphereB.SourceType = ECollisionSourceType::SimpleWorld;
		Data.SphericalLimits.Add(SphereB);

		FCapsuleLimit Capsule;
		Capsule.Location = Offset + FVector(30.0f, -10.0f, 25.0f);
		Capsule.Rotation = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(45.0f));
		Capsule.Radius = 4.0f;
		Capsule.Length = 18.0f;
		Capsule.bEnable = true;
		Capsule.SourceType = ECollisionSourceType::SimpleWorld;
		Data.CapsuleLimits.Add(Capsule);

		FBoxLimit Box;
		Box.Location = Offset + FVector(0.0f, 40.0f, 10.0f);
		Box.Rotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(30.0f));
		Box.Extent = FVector(5.0f, 7.0f, 9.0f);
		Box.bEnable = true;
		Box.SourceType = ECollisionSourceType::SimpleWorld;
		Data.BoxLimits.Add(Box);

		FKawaiiPhysicsConvexLimit Convex;
		Convex.Location = Offset + FVector(-15.0f, -25.0f, 12.0f);
		Convex.Rotation = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(60.0f));
		Convex.LocalPlanes = MakeUnitCubePlanes();
		Convex.LocalBounds = FBox(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f));
		Convex.bEnable = true;
		Convex.SourceType = ECollisionSourceType::SimpleWorld;
		Data.ConvexLimits.Add(Convex);

		return Data;
	}

	FKawaiiPhysicsSharedCollisionData MakeReadPathGroundWorldData(const FVector& Offset)
	{
		FKawaiiPhysicsSharedCollisionData Data;

		FBoxLimit GroundBox;
		GroundBox.Location = Offset + FVector(0.0f, 0.0f, -12.0f);
		GroundBox.Rotation = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(5.0f));
		GroundBox.Extent = FVector(80.0f, 80.0f, 10.0f);
		GroundBox.bEnable = true;
		GroundBox.SourceType = ECollisionSourceType::SimpleWorld;
		Data.BoxLimits.Add(GroundBox);

		return Data;
	}

	FSphericalLimit MakeSimpleWorldReaderSphere(const FVector& Location)
	{
		FSphericalLimit Sphere;
		Sphere.Location = Location;
		Sphere.Radius = 8.0f;
		Sphere.LimitType = ESphericalLimitType::Outer;
		Sphere.bEnable = true;
		Sphere.SourceType = ECollisionSourceType::SimpleWorld;
		return Sphere;
	}

	FCapsuleLimit MakeSimpleWorldReaderCapsule(const FVector& Location)
	{
		FCapsuleLimit Capsule;
		Capsule.Location = Location;
		Capsule.Rotation = FQuat::Identity;
		Capsule.Radius = 5.0f;
		Capsule.Length = 24.0f;
		Capsule.bEnable = true;
		Capsule.SourceType = ECollisionSourceType::SimpleWorld;
		return Capsule;
	}

	FBoxLimit MakeSimpleWorldReaderBox(const FVector& Location)
	{
		FBoxLimit Box;
		Box.Location = Location;
		Box.Rotation = FQuat::Identity;
		Box.Extent = FVector(10.0f, 12.0f, 14.0f);
		Box.bEnable = true;
		Box.SourceType = ECollisionSourceType::SimpleWorld;
		return Box;
	}

	FKawaiiPhysicsSharedPublisherState MakeSimpleWorldReaderState(bool bProviderDisabled)
	{
		FKawaiiPhysicsSharedPublisherState State;
		State.bSimpleWorldEnabled = true;
		State.SimpleWorldDesc.bGatherFamilyMembers = true;
		State.SimpleWorldDesc.bProviderDisabled = bProviderDisabled;
		return State;
	}

	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> MakeSimpleWorldReaderEntry(
		USkeletalMeshComponent* SkelCompA,
		USkeletalMeshComponent* SkelCompB)
	{
		TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry =
			MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();

		// メンバー Slot は登録済みメンバーにしか残らないため、A / B を reader として先に登録しておく。
		Entry->AddReaderMember(0xFFFF0002ull, SkelCompA, GFrameCounter);
		Entry->AddReaderMember(0xFFFF0003ull, SkelCompB, GFrameCounter);

		FKawaiiPhysicsSharedCollisionData MainData;
		MainData.BoxLimits.Add(MakeSimpleWorldReaderBox(FVector(10.0f, 0.0f, 0.0f)));
		Entry->Slot.Publish(MainData);

		FKawaiiPhysicsSharedCollisionData GroundData;
		GroundData.BoxLimits.Add(MakeSimpleWorldReaderBox(FVector(0.0f, 0.0f, -20.0f)));
		Entry->GroundSlot.Publish(GroundData);

		FKawaiiPhysicsSharedCollisionData MemberAData;
		MemberAData.SphericalLimits.Add(MakeSimpleWorldReaderSphere(FVector(20.0f, 0.0f, 0.0f)));
		TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>& MemberASlot =
			Entry->MemberSlots.FindOrAdd(TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompA));
		MemberASlot = MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>();
		MemberASlot->Publish(MemberAData);

		FKawaiiPhysicsSharedCollisionData MemberBData;
		MemberBData.CapsuleLimits.Add(MakeSimpleWorldReaderCapsule(FVector(30.0f, 0.0f, 0.0f)));
		TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>& MemberBSlot =
			Entry->MemberSlots.FindOrAdd(TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompB));
		MemberBSlot = MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>();
		MemberBSlot->Publish(MemberBData);

		return Entry;
	}

	void PublishSimpleWorldReaderMemberBExtraSphere(
		FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
		USkeletalMeshComponent* SkelCompB)
	{
		FKawaiiPhysicsSharedCollisionData MemberBData;
		MemberBData.CapsuleLimits.Add(MakeSimpleWorldReaderCapsule(FVector(30.0f, 0.0f, 0.0f)));
		MemberBData.SphericalLimits.Add(MakeSimpleWorldReaderSphere(FVector(40.0f, 0.0f, 0.0f)));
		if (TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>* MemberBSlot =
			Entry.MemberSlots.Find(TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompB)))
		{
			(*MemberBSlot)->Publish(MemberBData);
		}
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldGroundBoxFollowsComponentTest,
                                 "KawaiiPhysics.SimpleWorld.GroundBoxFollowsComponent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldGroundBoxFollowsComponentTest::RunTest(const FString& Parameters)
{
	FBoxLimit WorldBox;
	const bool bBuilt = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
		FVector(10.0f, 20.0f, 30.0f),
		FVector::UpVector,
		100.0f,
		WorldBox);
	TestTrue(TEXT("Builds source ground box"), bBuilt);

	const FTransform ComponentTM(
		FRotator(0.0f, 90.0f, 0.0f).Quaternion(),
		FVector(100.0f, 50.0f, 10.0f));
	const FBoxLimit LocalBox =
		KawaiiPhysicsSimpleWorldCollision::MakeSimpleWorldGroundBoxLocal(WorldBox, ComponentTM);
	const FBoxLimit RoundTripBox =
		KawaiiPhysicsSimpleWorldCollision::TransformSimpleWorldGroundBox(LocalBox, ComponentTM);

	TestTrue(TEXT("Round-trip location matches"),
	         RoundTripBox.Location.Equals(WorldBox.Location, GSimpleWorldTol));
	TestTrue(TEXT("Round-trip rotation matches"),
	         RoundTripBox.Rotation.Equals(WorldBox.Rotation, GSimpleWorldTol));
	TestTrue(TEXT("Round-trip extent matches"),
	         RoundTripBox.Extent.Equals(WorldBox.Extent, GSimpleWorldTol));

	const FTransform RaisedComponentTM(
		FRotator(0.0f, 90.0f, 0.0f).Quaternion(),
		FVector(100.0f, 50.0f, 60.0f));
	const FBoxLimit RaisedBox =
		KawaiiPhysicsSimpleWorldCollision::TransformSimpleWorldGroundBox(LocalBox, RaisedComponentTM);

	TestTrue(TEXT("Raised component moves ground box up"),
	         RaisedBox.Location.Equals(WorldBox.Location + FVector(0.0f, 0.0f, 50.0f), GSimpleWorldTol));
	TestTrue(TEXT("Raised component keeps extent"),
	         RaisedBox.Extent.Equals(WorldBox.Extent, GSimpleWorldTol));
	TestEqual(TEXT("Raised component keeps bEnable"), RaisedBox.bEnable, WorldBox.bEnable);
	TestTrue(TEXT("Raised component keeps SourceType"), RaisedBox.SourceType == WorldBox.SourceType);

	const FTransform ScaledComponentTM(
		FQuat::Identity,
		FVector(100.0f, 50.0f, 10.0f),
		FVector(2.0f, 2.0f, 2.0f));
	const FBoxLimit ScaledLocalBox =
		KawaiiPhysicsSimpleWorldCollision::MakeSimpleWorldGroundBoxLocal(WorldBox, ScaledComponentTM);
	const FBoxLimit ScaledRoundTripBox =
		KawaiiPhysicsSimpleWorldCollision::TransformSimpleWorldGroundBox(ScaledLocalBox, ScaledComponentTM);
	TestTrue(TEXT("Scaled transform keeps local extent"),
	         ScaledLocalBox.Extent.Equals(WorldBox.Extent, GSimpleWorldTol));
	TestTrue(TEXT("Scaled transform keeps world extent"),
	         ScaledRoundTripBox.Extent.Equals(WorldBox.Extent, GSimpleWorldTol));

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
			AggGeom, FVector::OneVector, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, 64, false, OutLimits);

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
				AggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, 64, false, OutLimits);

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
				AggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere, 64, false, OutLimits);

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
				AggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::None, 64, false, OutLimits);

			TestTrue(TEXT("Convex Ignore: produces no limits"), OutLimits.IsEmpty());
		}
	}

	// --- ConvexHull 指定でも未クックで GetPlanes が空なら BoundingBox へフォールバックする ---
	{
		const FVector Scale(2.0f, 1.0f, 0.5f);

		FKConvexElem ConvexElem;
		ConvexElem.SetTransform(FTransform(FQuat::Identity, FVector(1.0f, 2.0f, 3.0f)));
		ConvexElem.ElemBox = FBox(FVector(-4.0f, -2.0f, -1.0f), FVector(4.0f, 2.0f, 1.0f));

		FKAggregateGeom HullAggGeom;
		HullAggGeom.ConvexElems.Add(ConvexElem);
		FKawaiiPhysicsSharedCollisionData HullLimits;
		KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
			HullAggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull, 64, false, HullLimits);

		FKAggregateGeom BoxAggGeom;
		BoxAggGeom.ConvexElems.Add(ConvexElem);
		FKawaiiPhysicsSharedCollisionData BoxLimits;
		KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
			BoxAggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, 64, false, BoxLimits);

		TestTrue(TEXT("Uncooked ConvexHull falls back to exactly one box"),
		         HullLimits.ConvexLimits.Num() == 0 && HullLimits.BoxLimits.Num() == 1);
		TestTrue(TEXT("Uncooked ConvexHull fallback matches BoundingBox output"),
		         HullLimits.BoxLimits.Num() == 1 &&
		         BoxLimits.BoxLimits.Num() == 1 &&
		         HullLimits.BoxLimits[0].Location.Equals(BoxLimits.BoxLimits[0].Location, GSimpleWorldTol) &&
		         HullLimits.BoxLimits[0].Rotation.Equals(BoxLimits.BoxLimits[0].Rotation, GSimpleWorldTol) &&
		         HullLimits.BoxLimits[0].Extent.Equals(BoxLimits.BoxLimits[0].Extent, GSimpleWorldTol));
	}

	// --- ConvexHull混在: 平面ありElem相当はConvex、未クックElemはBoundingBoxへフォールバックする ---
	{
		FKawaiiPhysicsSharedCollisionData OutLimits;
		const TArray<FPlane> UnitPlanes = MakeUnitCubePlanes();
		const TArray<FVector> UnitVertices = MakeUnitCubeVertices();
		const TArray<int32> UnitIndices = MakeUnitCubeTriangleIndices();
		TestTrue(TEXT("Mixed ConvexHull: cooked-equivalent convex is appended"),
		         KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			         MakeArrayView(UnitPlanes),
			         MakeArrayView(UnitVertices),
			         MakeArrayView(UnitIndices),
			         FTransform::Identity,
			         FVector::OneVector,
			         64,
			         false,
			         OutLimits));

		FKConvexElem UncookedConvexElem;
		UncookedConvexElem.ElemBox = FBox(FVector(-2.0f, -3.0f, -4.0f), FVector(2.0f, 3.0f, 4.0f));
		FKAggregateGeom AggGeom;
		AggGeom.ConvexElems.Add(UncookedConvexElem);
		KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
			AggGeom, FVector::OneVector, EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull, 64, false, OutLimits);

		TestTrue(TEXT("Mixed ConvexHull produces one convex and one fallback box"),
		         OutLimits.ConvexLimits.Num() == 1 && OutLimits.BoxLimits.Num() == 1);
	}

	// --- Shape単位のQuery無効設定はSimpleWorldのOverlap対象から除外する ---
	{
		FKAggregateGeom AggGeom;

		FKSphereElem NoCollisionSphere;
		NoCollisionSphere.Radius = 5.0f;
		AggGeom.SphereElems.Add(NoCollisionSphere);

		FKSphereElem QueryOnlySphere;
		QueryOnlySphere.Center = FVector(10.0f, 0.0f, 0.0f);
		QueryOnlySphere.Radius = 7.0f;
		AggGeom.SphereElems.Add(QueryOnlySphere);
		// UE 5.3 の FKShapeElem コピーは CollisionEnabled を初期値へ戻すため、配列追加後に設定する。
		AggGeom.SphereElems[0].SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AggGeom.SphereElems[1].SetCollisionEnabled(ECollisionEnabled::QueryOnly);

		FKBoxElem PhysicsOnlyBox;
		PhysicsOnlyBox.X = 4.0f;
		PhysicsOnlyBox.Y = 6.0f;
		PhysicsOnlyBox.Z = 8.0f;
		AggGeom.BoxElems.Add_GetRef(PhysicsOnlyBox).SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);

		FKawaiiPhysicsSharedCollisionData OutLimits;
		KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
			AggGeom, FVector::OneVector, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, 64, false, OutLimits);

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
//  AppendConvexElemLocalLimit
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldAppendConvexElemLocalLimitTest,
                                 "KawaiiPhysics.SimpleWorld.AppendConvexElemLocalLimit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldAppendConvexElemLocalLimitTest::RunTest(const FString& Parameters)
{
	const TArray<FPlane> UnitPlanes = MakeUnitCubePlanes();
	const TArray<FVector> UnitVertices = MakeUnitCubeVertices();
	const TArray<int32> UnitIndices = MakeUnitCubeTriangleIndices();

	{
		FKawaiiPhysicsSharedCollisionData OutLimits;
		const bool bAdded = KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			MakeArrayView(UnitPlanes),
			MakeArrayView(UnitVertices),
			MakeArrayView(UnitIndices),
			FTransform::Identity,
			FVector::OneVector,
			64,
			true,
			OutLimits);

		TestTrue(TEXT("Unit cube convex is accepted"), bAdded);
		TestTrue(TEXT("Unit cube produces one convex limit"), OutLimits.ConvexLimits.Num() == 1);
		if (OutLimits.ConvexLimits.Num() == 1)
		{
			const FKawaiiPhysicsConvexLimit& Convex = OutLimits.ConvexLimits[0];
			TestTrue(TEXT("Unit cube has six planes"), Convex.LocalPlanes.Num() == 6);
			TestTrue(TEXT("Unit cube location is AABB center"), Convex.Location.Equals(FVector::ZeroVector, GSimpleWorldTol));
			TestTrue(TEXT("Unit cube rotation is identity"), Convex.Rotation.Equals(FQuat::Identity, GSimpleWorldTol));
			TestTrue(TEXT("Unit cube limit is enabled and sourced from SimpleWorld"),
			         Convex.bEnable && Convex.SourceType == ECollisionSourceType::SimpleWorld);
			TestTrue(TEXT("Unit cube local bounds are centered"),
			         Convex.LocalBounds.Min.Equals(FVector(-1.0f, -1.0f, -1.0f), GSimpleWorldTol) &&
			         Convex.LocalBounds.Max.Equals(FVector(1.0f, 1.0f, 1.0f), GSimpleWorldTol));
#if !UE_BUILD_SHIPPING
			TestTrue(TEXT("Unit cube debug vertices are stored"), Convex.LocalVertices.Num() == 8);
			TestTrue(TEXT("Unit cube debug edges are the 12 hull edges"), Convex.LocalEdges.Num() == 24);
#endif
		}
	}

	{
		FKawaiiPhysicsSharedCollisionData OutLimits;
		const FVector Scale(2.0f, 1.0f, 0.5f);
		const bool bAdded = KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			MakeArrayView(UnitPlanes),
			MakeArrayView(UnitVertices),
			MakeArrayView(UnitIndices),
			FTransform::Identity,
			Scale,
			64,
			false,
			OutLimits);

		TestTrue(TEXT("Non-uniform scaled cube is accepted"), bAdded);
		if (OutLimits.ConvexLimits.Num() == 1)
		{
			const FKawaiiPhysicsConvexLimit& Convex = OutLimits.ConvexLimits[0];
			const FPlane* PlaneX = FindPlaneWithNormal(MakeArrayView(Convex.LocalPlanes), FVector(1.0f, 0.0f, 0.0f));
			const FPlane* PlaneY = FindPlaneWithNormal(MakeArrayView(Convex.LocalPlanes), FVector(0.0f, 1.0f, 0.0f));
			const FPlane* PlaneZ = FindPlaneWithNormal(MakeArrayView(Convex.LocalPlanes), FVector(0.0f, 0.0f, 1.0f));
			TestTrue(TEXT("Non-uniform scale: +X plane distance"), PlaneX && FMath::IsNearlyEqual(PlaneX->W, 2.0f, GSimpleWorldTol));
			TestTrue(TEXT("Non-uniform scale: +Y plane distance"), PlaneY && FMath::IsNearlyEqual(PlaneY->W, 1.0f, GSimpleWorldTol));
			TestTrue(TEXT("Non-uniform scale: +Z plane distance"), PlaneZ && FMath::IsNearlyEqual(PlaneZ->W, 0.5f, GSimpleWorldTol));
		}
	}

	{
		FKawaiiPhysicsSharedCollisionData OutLimits;
		const FVector Scale(2.0f, 1.0f, 0.5f);
		const FVector BodyNormal = FVector(1.0f, 0.0f, 1.0f).GetSafeNormal();
		const FVector BodyPoint(1.0f, 0.0f, 1.0f);
		TArray<FPlane> ObliquePlanes;
		ObliquePlanes.Add(FPlane(
			BodyNormal.X,
			BodyNormal.Y,
			BodyNormal.Z,
			FVector::DotProduct(BodyNormal, BodyPoint)));

		const bool bAdded = KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			MakeArrayView(ObliquePlanes),
			MakeArrayView(UnitVertices),
			MakeArrayView(UnitIndices),
			FTransform::Identity,
			Scale,
			64,
			false,
			OutLimits);

		TestTrue(TEXT("Non-uniform oblique plane is accepted"), bAdded);
		if (OutLimits.ConvexLimits.Num() == 1 && OutLimits.ConvexLimits[0].LocalPlanes.Num() == 1)
		{
			const FPlane& Plane = OutLimits.ConvexLimits[0].LocalPlanes[0];
			const FVector ExpectedNormal = FVector(
				BodyNormal.X / Scale.X,
				BodyNormal.Y / Scale.Y,
				BodyNormal.Z / Scale.Z).GetSafeNormal();
			const FVector ExpectedScaledPoint = BodyPoint * Scale;
			const float ExpectedW = FVector::DotProduct(ExpectedNormal, ExpectedScaledPoint);
			TestTrue(TEXT("Non-uniform oblique plane normal uses inverse transpose"),
			         FVector(Plane.X, Plane.Y, Plane.Z).Equals(ExpectedNormal, GSimpleWorldTol));
			TestTrue(TEXT("Non-uniform oblique plane W uses transformed point"),
			         FMath::IsNearlyEqual(Plane.W, ExpectedW, GSimpleWorldTol));
		}
	}

	{
		FKawaiiPhysicsSharedCollisionData OutLimits;
		const bool bAdded = KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			MakeArrayView(UnitPlanes),
			MakeArrayView(UnitVertices),
			MakeArrayView(UnitIndices),
			FTransform::Identity,
			FVector(-1.0f, 1.0f, 1.0f),
			64,
			false,
			OutLimits);

		TestTrue(TEXT("Negative scaled cube is accepted"), bAdded);
		if (OutLimits.ConvexLimits.Num() == 1)
		{
			for (const FPlane& Plane : OutLimits.ConvexLimits[0].LocalPlanes)
			{
				const FVector Normal(Plane.X, Plane.Y, Plane.Z);
				const float CenterSide = FVector::DotProduct(Normal, FVector::ZeroVector) - Plane.W;
				const float OutsideSide = FVector::DotProduct(Normal, Normal * (Plane.W + 0.1f)) - Plane.W;
				TestTrue(TEXT("Negative scale keeps outward positive side"), CenterSide < 0.0f && OutsideSide > 0.0f);
			}
		}
	}

	{
		FKawaiiPhysicsSharedCollisionData OutLimits;
		const FTransform ElemTM(FRotator(0.0f, 90.0f, 0.0f).Quaternion(), FVector(10.0f, 0.0f, 0.0f));
		const bool bAdded = KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			MakeArrayView(UnitPlanes),
			MakeArrayView(UnitVertices),
			MakeArrayView(UnitIndices),
			ElemTM,
			FVector::OneVector,
			64,
			true,
			OutLimits);

		TestTrue(TEXT("Rotated elem cube is accepted"), bAdded);
		if (OutLimits.ConvexLimits.Num() == 1)
		{
			const FKawaiiPhysicsConvexLimit& Convex = OutLimits.ConvexLimits[0];
			const FVector ExpectedCenter = ElemTM.TransformPosition(FVector::ZeroVector);
			TestTrue(TEXT("Rotated elem AABB center uses transformed vertices"),
			         Convex.Location.Equals(ExpectedCenter, GSimpleWorldTol));
#if !UE_BUILD_SHIPPING
			const FVector ExpectedLocalVertex0 = ElemTM.TransformPosition(UnitVertices[0]) - ExpectedCenter;
			TestTrue(TEXT("Rotated elem debug vertices are ElemTM-baked"),
			         Convex.LocalVertices.Num() == UnitVertices.Num() &&
			         Convex.LocalVertices[0].Equals(ExpectedLocalVertex0, GSimpleWorldTol));
#endif
		}
	}

	{
		FKawaiiPhysicsSharedCollisionData OutLimits;
		TestFalse(TEXT("Plane count over max is rejected"),
		          KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			          MakeArrayView(UnitPlanes),
			          MakeArrayView(UnitVertices),
			          MakeArrayView(UnitIndices),
			          FTransform::Identity,
			          FVector::OneVector,
			          5,
			          false,
			          OutLimits));
		TestTrue(TEXT("Plane count rejection adds no limit"), OutLimits.ConvexLimits.Num() == 0);
		TestFalse(TEXT("Zero scale component is rejected"),
		          KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			          MakeArrayView(UnitPlanes),
			          MakeArrayView(UnitVertices),
			          MakeArrayView(UnitIndices),
			          FTransform::Identity,
			          FVector(1.0f, 0.0f, 1.0f),
			          64,
			          false,
			          OutLimits));

		TArray<FPlane> BadPlanes = UnitPlanes;
		BadPlanes[0] = FPlane(0.0f, 0.0f, 0.0f, 0.0f);
		TestFalse(TEXT("Degenerate plane is rejected"),
		          KawaiiPhysicsSimpleWorldCollision::AppendConvexElemLocalLimit(
			          MakeArrayView(BadPlanes),
			          MakeArrayView(UnitVertices),
			          MakeArrayView(UnitIndices),
			          FTransform::Identity,
			          FVector::OneVector,
			          64,
			          false,
			          OutLimits));
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
			Bounds, ComponentTM, EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull, LocalLimits);

		FKawaiiPhysicsSharedCollisionData WorldLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendLocalLimitsTransformed(LocalLimits, ComponentTM, WorldLimits);

		TestTrue(TEXT("ConvexHull bounds appends BoundingBox because Bounds has no hull data"),
		         WorldLimits.BoxLimits.Num() == 1 && WorldLimits.ConvexLimits.Num() == 0);
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

		FKawaiiPhysicsConvexLimit Convex;
		Convex.Location = FVector::ZeroVector;
		Convex.Rotation = FQuat::Identity;
		Convex.LocalPlanes = MakeUnitCubePlanes();
		Convex.LocalBounds = FBox(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f));
		LocalLimits.ConvexLimits.Add(Convex);

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
		TestTrue(TEXT("FadeAlpha=0.5 (== threshold): convex is kept"),
		         OutWorldLimits.ConvexLimits.Num() == 1);

	}

	// FadeAlpha=0.4（< threshold）: Box は追記されない
	{
		FKawaiiPhysicsSharedCollisionData LocalLimits = MakeLocalLimits();
		FKawaiiPhysicsSharedCollisionData OutWorldLimits;
		KawaiiPhysicsSimpleWorldCollision::AppendFadedLocalLimits(
			LocalLimits, 0.4f, Identity, OutWorldLimits, BoxEnableThreshold);

		TestTrue(TEXT("FadeAlpha=0.4 (< threshold): box is withheld"), OutWorldLimits.BoxLimits.Num() == 0);
		TestTrue(TEXT("FadeAlpha=0.4 (< threshold): convex is withheld"), OutWorldLimits.ConvexLimits.Num() == 0);
		TestTrue(TEXT("FadeAlpha=0.4: sphere radius is scaled by 0.4"),
		         FMath::IsNearlyEqual(OutWorldLimits.SphericalLimits[0].Radius, 4.0f, GSimpleWorldTol));
	}

	return true;
}

// ---------------------------------------------------------------------------
//  レスポンスパラメータ構築
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldResponseParamsTest,
                                 "KawaiiPhysics.SimpleWorld.ResponseParams",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldResponseParamsTest::RunTest(const FString& Parameters)
{
	{
		const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
		const FCollisionResponseParams ResponseParams =
			KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldResponseParams(ObjectTypes);

		TestTrue(TEXT("Empty ObjectTypes blocks WorldStatic"),
		         ResponseParams.CollisionResponse.GetResponse(ECC_WorldStatic) == ECR_Block);
		TestTrue(TEXT("Empty ObjectTypes blocks WorldDynamic"),
		         ResponseParams.CollisionResponse.GetResponse(ECC_WorldDynamic) == ECR_Block);
		TestTrue(TEXT("Empty ObjectTypes ignores Pawn"),
		         ResponseParams.CollisionResponse.GetResponse(ECC_Pawn) == ECR_Ignore);
	}

	{
		const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes =
		{
			UEngineTypes::ConvertToObjectType(ECC_Pawn),
			UEngineTypes::ConvertToObjectType(ECC_PhysicsBody),
		};
		const FCollisionResponseParams ResponseParams =
			KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldResponseParams(ObjectTypes);

		TestTrue(TEXT("Explicit ObjectTypes blocks Pawn"),
		         ResponseParams.CollisionResponse.GetResponse(ECC_Pawn) == ECR_Block);
		TestTrue(TEXT("Explicit ObjectTypes blocks PhysicsBody"),
		         ResponseParams.CollisionResponse.GetResponse(ECC_PhysicsBody) == ECR_Block);
		TestTrue(TEXT("Explicit ObjectTypes ignores WorldStatic"),
		         ResponseParams.CollisionResponse.GetResponse(ECC_WorldStatic) == ECR_Ignore);
	}

	return true;
}

// ---------------------------------------------------------------------------
//  収集入力ガード
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldGatherInputValidTest,
                                 "KawaiiPhysics.SimpleWorld.GatherInputValid",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldGatherInputValidTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Finite center and positive radius are valid"),
	         KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldGatherInputValid(FVector(1.0f, 2.0f, 3.0f), 100.0f));

	TestFalse(TEXT("NaN center is invalid"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldGatherInputValid(
		          FVector(std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f), 100.0f));

	TestFalse(TEXT("Infinite radius is invalid"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldGatherInputValid(
		          FVector(1.0f, 2.0f, 3.0f), std::numeric_limits<float>::infinity()));

	TestFalse(TEXT("Zero radius is invalid"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldGatherInputValid(FVector(1.0f, 2.0f, 3.0f), 0.0f));

	return true;
}

// ---------------------------------------------------------------------------
//  SimpleWorld Registry / provider-reader backend
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldRegistryKeyTest,
                                 "KawaiiPhysics.SimpleWorld.RegistryKey",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldRegistryKeyTest::RunTest(const FString& Parameters)
{
	USkeletalMeshComponent* SkelCompA = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* SkelCompB = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);

	const FKawaiiPhysicsSimpleWorldRegistryKey LocalA0 =
		FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(SkelCompA);
	const FKawaiiPhysicsSimpleWorldRegistryKey LocalA1 =
		FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(SkelCompA);
	const FKawaiiPhysicsSimpleWorldRegistryKey LocalB =
		FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(SkelCompB);

	TestTrue(TEXT("MakeLocalKey returns equal keys for the same component"), LocalA0 == LocalA1);
	TestFalse(TEXT("MakeLocalKey separates different components"), LocalA0 == LocalB);

	TestFalse(TEXT("MakeSharedKey separates different tags"),
	          FKawaiiPhysicsSimpleWorldRegistryKey::MakeSharedKey(nullptr, TAG_KawaiiPhysicsSimpleWorldRegistryX) ==
	          FKawaiiPhysicsSimpleWorldRegistryKey::MakeSharedKey(nullptr, TAG_KawaiiPhysicsSimpleWorldRegistryY));

	// Worker から呼ぶ弱参照版 MakeLocalKey は raw ポインタ版と同じキーになり、IsValid() も一致する。
	const FKawaiiPhysicsSimpleWorldRegistryKey WeakLocalA =
		FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompA));
	TestTrue(TEXT("Weak MakeLocalKey equals the raw pointer key"), WeakLocalA == LocalA0);
	TestTrue(TEXT("Weak MakeLocalKey is valid"), WeakLocalA.IsValid());

	const FKawaiiPhysicsSimpleWorldRegistryKey DefaultKey;
	TestFalse(TEXT("Default-constructed key is invalid"), DefaultKey.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldReaderProviderAgingTest,
                                 "KawaiiPhysics.SimpleWorld.ReaderProviderAging",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldReaderProviderAgingTest::RunTest(const FString& Parameters)
{
	// reader の境界、延命、provider の境界と削除を確認する。
	constexpr uint64 ReaderSourceID = 10;
	constexpr uint64 ProviderSourceID = 11;
	constexpr uint64 StartFrame = 100;
	constexpr uint64 MaxAge = 5;
	USkeletalMeshComponent* ReaderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	FKawaiiPhysicsSimpleWorldCollisionEntry ReaderEntry;
	ReaderEntry.AddReaderMember(ReaderSourceID, ReaderSkelComp, StartFrame);
	TestTrue(TEXT("AddReaderMember creates reader membership"), ReaderEntry.HasAnyReader());
	TestFalse(TEXT("MarkReaderRead returns false without a provider"),
	          ReaderEntry.MarkReaderRead(ReaderSourceID, StartFrame + 10, MaxAge));
	ReaderEntry.RemoveExpiredDescs(StartFrame + 10 + MaxAge, MaxAge);
	TestTrue(TEXT("Reader survives at exactly MaxAge after MarkReaderRead"), ReaderEntry.HasAnyReader());
	ReaderEntry.RemoveExpiredDescs(StartFrame + 10 + MaxAge + 1, MaxAge);
	TestFalse(TEXT("Reader expires after MaxAge"), ReaderEntry.HasAnyReader());

	FKawaiiPhysicsSimpleWorldCollisionEntry ProviderEntry;
	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	ProviderEntry.SetDesc(ProviderSourceID, Desc, StartFrame, ReaderSkelComp, true);
	ProviderEntry.RemoveExpiredDescs(StartFrame + MaxAge + 1, MaxAge);
	TestFalse(TEXT("Provider expires with the same age rule"), ProviderEntry.HasAnyDesc());

	USkeletalMeshComponent* ProviderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	Entry.SetDesc(ProviderSourceID, Desc, StartFrame, ProviderSkelComp, true);
	Entry.AddReaderMember(ReaderSourceID, ReaderSkelComp, StartFrame);
	TestTrue(TEXT("Reader keeps membership while provider is within max age"),
	         Entry.MarkReaderRead(ReaderSourceID, StartFrame + MaxAge, MaxAge));
	TestFalse(TEXT("Reader releases after provider exceeds max age"),
	          Entry.MarkReaderRead(ReaderSourceID, StartFrame + MaxAge + 1, MaxAge));

	FKawaiiPhysicsSimpleWorldCollisionEntry ReaderOnlyEntry;
	ReaderOnlyEntry.AddReaderMember(ReaderSourceID, ReaderSkelComp, StartFrame);
	TestFalse(TEXT("Reader releases when no provider exists"),
	          ReaderOnlyEntry.MarkReaderRead(ReaderSourceID, StartFrame + 1, MaxAge));

	FKawaiiPhysicsSimpleWorldCollisionEntry LastFrameEntry;
	TestFalse(TEXT("Provider-less entry is not alive"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(LastFrameEntry, StartFrame, MaxAge));
	LastFrameEntry.SetDesc(ProviderSourceID, Desc, StartFrame, TWeakObjectPtr<const USkeletalMeshComponent>(), true);
	TestTrue(TEXT("SetDesc marks provider alive"),
	         KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(LastFrameEntry, StartFrame, MaxAge));
	TestTrue(TEXT("Provider is alive at the max-age boundary"),
	         KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(LastFrameEntry, StartFrame + MaxAge, MaxAge));
	TestFalse(TEXT("Provider expires after the max-age boundary"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(LastFrameEntry, StartFrame + MaxAge + 1, MaxAge));
	LastFrameEntry.RemoveDesc(ProviderSourceID);
	TestFalse(TEXT("Removed provider is not alive"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(LastFrameEntry, StartFrame, MaxAge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldPrimaryIsProviderSkelCompTest,
                                 "KawaiiPhysics.SimpleWorld.PrimaryIsProviderSkelComp",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldPrimaryIsProviderSkelCompTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderSourceID = 40;
	constexpr uint64 ReaderSourceID = 41;
	constexpr uint64 Frame = 100;

	USkeletalMeshComponent* ProviderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* ReaderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);

	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	Entry.SetDesc(ProviderSourceID, Desc, Frame, ProviderSkelComp, true);
	Entry.AddReaderMember(ReaderSourceID, ReaderSkelComp, Frame);

	TArray<TWeakObjectPtr<const USkeletalMeshComponent>> Members;
	Entry.CollectMemberSkelComps(Members);
	TestEqual(TEXT("CollectMemberSkelComps returns provider and reader once"), Members.Num(), 2);
	TestTrue(TEXT("Collected members contain provider"),
	         Members.Contains(TWeakObjectPtr<const USkeletalMeshComponent>(ProviderSkelComp)));
	TestTrue(TEXT("Collected members contain reader"),
	         Members.Contains(TWeakObjectPtr<const USkeletalMeshComponent>(ReaderSkelComp)));

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
	// 複数 Desc の優先順位と収集範囲の統合条件を確認する。
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

	// GatherRadiusOverride: Override指定の最大値を保持し、全DescがOverride指定かを別フラグで持つ。
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc OverrideA, OverrideB;
		OverrideA.GatherRadiusOverride = 150.0f;
		OverrideB.GatherRadiusOverride = 300.0f;
		const FKawaiiPhysicsSimpleWorldCollisionDesc MergedAllOverridden =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({OverrideA, OverrideB});
		TestTrue(TEXT("Override {150,300}: all-overridden picks the max"),
		         FMath::IsNearlyEqual(MergedAllOverridden.GatherRadiusOverride, 300.0f, GSimpleWorldTol));
		TestTrue(TEXT("Override {150,300}: all-overridden flag is true"),
		         MergedAllOverridden.bGatherRadiusAllOverridden);

		FKawaiiPhysicsSimpleWorldCollisionDesc Auto, Override;
		Auto.GatherRadiusOverride = 0.0f;
		Override.GatherRadiusOverride = 300.0f;
		const FKawaiiPhysicsSimpleWorldCollisionDesc MergedMixed =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({Override, Auto});
		TestTrue(TEXT("Override {300,0}: mixed keeps max override"),
		         FMath::IsNearlyEqual(MergedMixed.GatherRadiusOverride, 300.0f, GSimpleWorldTol));
		TestFalse(TEXT("Override {300,0}: mixed all-overridden flag is false"),
		          MergedMixed.bGatherRadiusAllOverridden);

		FKawaiiPhysicsSimpleWorldCollisionDesc AutoA, AutoB;
		AutoA.GatherRadiusOverride = 0.0f;
		AutoB.GatherRadiusOverride = 0.0f;
		const FKawaiiPhysicsSimpleWorldCollisionDesc MergedAuto =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({AutoA, AutoB});
		TestTrue(TEXT("Override {0,0}: automatic radius stays 0"),
		         FMath::IsNearlyEqual(MergedAuto.GatherRadiusOverride, 0.0f, GSimpleWorldTol));
		TestFalse(TEXT("Override {0,0}: all-overridden flag is false"),
		          MergedAuto.bGatherRadiusAllOverridden);
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

	// CollisionChannel: ECC_MAX以外の先頭を採用。全てECC_MAXならECC_MAXのまま。
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc AutoA, AutoB;
		AutoA.CollisionChannel = ECC_MAX;
		AutoB.CollisionChannel = ECC_MAX;
		const FKawaiiPhysicsSimpleWorldCollisionDesc MergedAuto =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({AutoA, AutoB});
		TestTrue(TEXT("CollisionChannel {ECC_MAX,ECC_MAX} -> ECC_MAX"),
		         MergedAuto.CollisionChannel == ECC_MAX);

		FKawaiiPhysicsSimpleWorldCollisionDesc Auto, Pawn;
		Auto.CollisionChannel = ECC_MAX;
		Pawn.CollisionChannel = ECC_Pawn;
		const FKawaiiPhysicsSimpleWorldCollisionDesc MergedPawn =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({Auto, Pawn});
		TestTrue(TEXT("CollisionChannel {ECC_MAX,Pawn} -> Pawn"),
		         MergedPawn.CollisionChannel == ECC_Pawn);

		FKawaiiPhysicsSimpleWorldCollisionDesc Visibility, PawnSecond;
		Visibility.CollisionChannel = ECC_Visibility;
		PawnSecond.CollisionChannel = ECC_Pawn;
		const FKawaiiPhysicsSimpleWorldCollisionDesc MergedFirst =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({Visibility, PawnSecond});
		TestTrue(TEXT("CollisionChannel {Visibility,Pawn} -> Visibility"),
		         MergedFirst.CollisionChannel == ECC_Visibility);
	}

	// operator==: CollisionChannelの差を比較に含める。
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc A, B;
		A.CollisionChannel = ECC_Visibility;
		B.CollisionChannel = ECC_Pawn;
		TestFalse(TEXT("operator== detects different CollisionChannel"), A == B);
	}

	// DoesChangeRequireRegather: 収集内容へ影響するフィールドだけtrue。
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc Base;
		Base.ObjectTypes = {UEngineTypes::ConvertToObjectType(ECC_WorldStatic)};
		Base.ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere;
		Base.SkeletalMeshCollision = EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::BoundingBox;
		Base.bGroundCollision = true;
		Base.GatherRadiusOverride = 200.0f;
		Base.CollisionChannel = ECC_Visibility;

		FKawaiiPhysicsSimpleWorldCollisionDesc Same = Base;
		TestFalse(TEXT("DoesChangeRequireRegather returns false for identical descs"),
		          FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, Same));

		FKawaiiPhysicsSimpleWorldCollisionDesc IntervalOnly = Base;
		IntervalOnly.GatherIntervalSec = Base.GatherIntervalSec + 0.25f;
		TestFalse(TEXT("DoesChangeRequireRegather ignores GatherIntervalSec-only changes"),
		          FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, IntervalOnly));

		FKawaiiPhysicsSimpleWorldCollisionDesc ObjectTypesChanged = Base;
		ObjectTypesChanged.ObjectTypes = {UEngineTypes::ConvertToObjectType(ECC_WorldDynamic)};
		TestTrue(TEXT("DoesChangeRequireRegather detects ObjectTypes changes"),
		         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, ObjectTypesChanged));

		FKawaiiPhysicsSimpleWorldCollisionDesc ShapeChanged = Base;
		ShapeChanged.ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox;
		TestTrue(TEXT("DoesChangeRequireRegather detects ConvexFallbackShape changes"),
		         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, ShapeChanged));

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

	// enum優先: ConvexFallbackShape は宣言順（高精度が先）
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

	// 収集範囲と家族メンバーの統合条件を確認する。
	FKawaiiPhysicsSimpleWorldCollisionDesc SkeletalScopeDesc;
	SkeletalScopeDesc.GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::SkeletalMeshComponent;
	FKawaiiPhysicsSimpleWorldCollisionDesc ActorFamilyDesc;
	ActorFamilyDesc.GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily;
	FKawaiiPhysicsSimpleWorldCollisionDesc MergedScope =
		FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({SkeletalScopeDesc, ActorFamilyDesc});
	TestTrue(TEXT("ActorFamily gather scope wins merge"),
	         MergedScope.GatherScope == EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily);

	FKawaiiPhysicsSimpleWorldCollisionDesc NoFamilyMembers;
	NoFamilyMembers.bGatherFamilyMembers = false;
	FKawaiiPhysicsSimpleWorldCollisionDesc WithFamilyMembers;
	WithFamilyMembers.bGatherFamilyMembers = true;
	FKawaiiPhysicsSimpleWorldCollisionDesc MergedFamilyMembers =
		FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({NoFamilyMembers, WithFamilyMembers});
	TestTrue(TEXT("bGatherFamilyMembers merges with OR"), MergedFamilyMembers.bGatherFamilyMembers);

	FKawaiiPhysicsSimpleWorldCollisionDesc DisabledProvider;
	DisabledProvider.bProviderDisabled = true;
	FKawaiiPhysicsSimpleWorldCollisionDesc EnabledProvider;
	EnabledProvider.bProviderDisabled = false;
	FKawaiiPhysicsSimpleWorldCollisionDesc MergedMixedDisabled =
		FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({DisabledProvider, EnabledProvider});
	TestFalse(TEXT("bProviderDisabled true+false merges to false"), MergedMixedDisabled.bProviderDisabled);
	FKawaiiPhysicsSimpleWorldCollisionDesc MergedAllDisabled =
		FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({DisabledProvider, DisabledProvider});
	TestTrue(TEXT("bProviderDisabled true+true merges to true"), MergedAllDisabled.bProviderDisabled);

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
	// Desc の登録・失効と再収集要求の境界を確認する。
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

	// 登録直後は MaxAge=0 の cleanup でも Desc が残る。
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

	}

	// RequestRegather / ConsumeRegatherRequested: 1回だけ true を返す
	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
		TestTrue(TEXT("Regather not requested initially"), !Entry.ConsumeRegatherRequested());

		Entry.RequestRegather();
		TestTrue(TEXT("First consume returns true"), Entry.ConsumeRegatherRequested());
		TestTrue(TEXT("Second consume returns false (already consumed)"), !Entry.ConsumeRegatherRequested());
	}

	// SetDesc: 初回登録と収集内容に影響する変更だけ再収集を要求する。
	{
		constexpr uint64 SourceID = 200;
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		Desc.GatherIntervalSec = 0.2f;
		Entry.SetDesc(SourceID, Desc);
		TestTrue(TEXT("Initial SetDesc requests regather"), Entry.ConsumeRegatherRequested());

		FKawaiiPhysicsSimpleWorldCollisionDesc IntervalChanged = Desc;
		IntervalChanged.GatherIntervalSec = 0.05f;
		Entry.SetDesc(SourceID, IntervalChanged);
		TestFalse(TEXT("Interval-only SetDesc does not request regather"), Entry.ConsumeRegatherRequested());

		FKawaiiPhysicsSimpleWorldCollisionDesc ObjectTypesChanged = IntervalChanged;
		ObjectTypesChanged.ObjectTypes = {UEngineTypes::ConvertToObjectType(ECC_Pawn)};
		Entry.SetDesc(SourceID, ObjectTypesChanged);
		TestTrue(TEXT("ObjectTypes SetDesc requests regather"), Entry.ConsumeRegatherRequested());
	}

	// SetDesc: 同一設定のノードが追加で登録されてもMerge結果は変わらないため再収集しない。
	{
		constexpr uint64 SourceA = 210;
		constexpr uint64 SourceB = 211;
		constexpr uint64 SourceC = 212;
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		Desc.GatherIntervalSec = 0.2f;
		Entry.SetDesc(SourceA, Desc);
		TestTrue(TEXT("First source SetDesc requests regather"), Entry.ConsumeRegatherRequested());

		Entry.SetDesc(SourceB, Desc);
		TestFalse(TEXT("Second source with an identical desc does not request regather"),
		          Entry.ConsumeRegatherRequested());

		FKawaiiPhysicsSimpleWorldCollisionDesc DifferentDesc = Desc;
		DifferentDesc.ObjectTypes = {UEngineTypes::ConvertToObjectType(ECC_Pawn)};
		Entry.SetDesc(SourceC, DifferentDesc);
		TestTrue(TEXT("Third source with a different desc requests regather"), Entry.ConsumeRegatherRequested());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldGroundSlotIndependentPublishTest,
                                 "KawaiiPhysics.SimpleWorld.GroundSlotIndependentPublish",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldGroundSlotIndependentPublishTest::RunTest(const FString& Parameters)
{
	// 形状・地面 Slot の独立性と空 publish の serial 更新を確認する。
	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& GatheredComponent =
			Entry.GatheredComponents.AddDefaulted_GetRef();
		GatheredComponent.FadeAlpha = 1.0f;
		GatheredComponent.LastComponentTM = FTransform::Identity;

		FSphericalLimit Sphere;
		Sphere.Location = FVector(10.0f, 20.0f, 30.0f);
		Sphere.Radius = 40.0f;
		Sphere.bEnable = true;
		Sphere.SourceType = ECollisionSourceType::SimpleWorld;
		GatheredComponent.LocalLimits.SphericalLimits.Add(Sphere);

		UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldShapeLimits(Entry, 0.5f);
		TestEqual(TEXT("Shape slot serial after shape publish"), Entry.Slot.GetPublishSerial(), static_cast<uint64>(1));
		TestEqual(TEXT("Shape publish does not advance ground serial"), Entry.GroundSlot.GetPublishSerial(), static_cast<uint64>(0));

		FKawaiiPhysicsSharedCollisionData ShapeOutData;
		Entry.Slot.AppendTo(ShapeOutData);
		TestEqual(TEXT("Shape slot publishes one sphere"), ShapeOutData.SphericalLimits.Num(), 1);
		TestEqual(TEXT("Shape slot publishes no boxes"), ShapeOutData.BoxLimits.Num(), 0);

		Entry.bHasGroundBox = true;
		Entry.GroundBox.Location = FVector(0.0f, 0.0f, -10.0f);
		Entry.GroundBox.Extent = FVector(100.0f, 100.0f, 10.0f);
		Entry.GroundBox.Rotation = FQuat::Identity;
		Entry.GroundBox.bEnable = true;
		Entry.GroundBox.SourceType = ECollisionSourceType::SimpleWorld;
		UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldGroundBox(Entry);

		TestEqual(TEXT("Ground slot serial after ground publish"),
		          Entry.GroundSlot.GetPublishSerial(),
		          static_cast<uint64>(1));
		TestEqual(TEXT("Shape slot serial is unchanged after ground publish"),
		          Entry.Slot.GetPublishSerial(),
		          static_cast<uint64>(1));

		FKawaiiPhysicsSharedCollisionData GroundOutData;
		Entry.GroundSlot.AppendTo(GroundOutData);
		TestEqual(TEXT("Ground slot publishes one box"), GroundOutData.BoxLimits.Num(), 1);

		Entry.bHasGroundBox = false;
		Entry.bGroundBoxDirty = true;
		UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldGroundBox(Entry);

		FKawaiiPhysicsSharedCollisionData ClearedGroundOutData;
		Entry.GroundSlot.AppendTo(ClearedGroundOutData);
		TestEqual(TEXT("Ground slot publishes zero boxes after clear"), ClearedGroundOutData.BoxLimits.Num(), 0);
		TestEqual(TEXT("Ground slot serial after clear publish"),
		          Entry.GroundSlot.GetPublishSerial(),
		          static_cast<uint64>(2));
	}

	// 空 publish は両 Slot を一度だけ更新する。
	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& GatheredComponent =
			Entry.GatheredComponents.AddDefaulted_GetRef();
		GatheredComponent.FadeAlpha = 1.0f;
		GatheredComponent.LastComponentTM = FTransform::Identity;
		FSphericalLimit Sphere;
		Sphere.Location = FVector(5.0f, 0.0f, 0.0f);
		Sphere.Radius = 8.0f;
		Sphere.bEnable = true;
		Sphere.SourceType = ECollisionSourceType::SimpleWorld;
		GatheredComponent.LocalLimits.SphericalLimits.Add(Sphere);

		Entry.bHasGroundBox = true;
		Entry.GroundBox.Location = FVector(0.0f, 0.0f, -20.0f);
		Entry.GroundBox.Extent = FVector(100.0f, 100.0f, 5.0f);
		Entry.GroundBox.Rotation = FQuat::Identity;
		Entry.GroundBox.bEnable = true;
		Entry.GroundBox.SourceType = ECollisionSourceType::SimpleWorld;

		UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldShapeLimits(Entry, 0.5f);
		UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldGroundBox(Entry);
		const uint64 ShapeSerialBeforeClear = Entry.Slot.GetPublishSerial();
		const uint64 GroundSerialBeforeClear = Entry.GroundSlot.GetPublishSerial();

		UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldEmptyLimits(Entry, 0.5f);
		TestEqual(TEXT("Shape slot serial advances once when disabled clears data"),
		          Entry.Slot.GetPublishSerial(),
		          ShapeSerialBeforeClear + 1);
		TestEqual(TEXT("Ground slot serial advances once when disabled clears data"),
		          Entry.GroundSlot.GetPublishSerial(),
		          GroundSerialBeforeClear + 1);

		FKawaiiPhysicsSharedCollisionData ShapeOutData;
		Entry.Slot.AppendTo(ShapeOutData);
		TestTrue(TEXT("Shape slot is empty after disabled clear"), ShapeOutData.IsEmpty());
		FKawaiiPhysicsSharedCollisionData GroundOutData;
		Entry.GroundSlot.AppendTo(GroundOutData);
		TestTrue(TEXT("Ground slot is empty after disabled clear"), GroundOutData.IsEmpty());

		UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldEmptyLimits(Entry, 0.5f);
		TestEqual(TEXT("Shape slot serial does not advance when already empty"),
		          Entry.Slot.GetPublishSerial(),
		          ShapeSerialBeforeClear + 1);
		TestEqual(TEXT("Ground slot serial does not advance when already empty"),
		          Entry.GroundSlot.GetPublishSerial(),
		          GroundSerialBeforeClear + 1);

	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSortGatherOrderByDistanceTest,
                                 "KawaiiPhysics.SimpleWorld.SortGatherOrderByDistance",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSortGatherOrderByDistanceTest::RunTest(const FString& Parameters)
{
	TArray<float> DistanceSquared = {9.0f, 1.0f, 4.0f, 1.0f, 0.0f};
	TArray<int32> OutOrder;
	KawaiiPhysicsSimpleWorldCollision::SortSimpleWorldGatherOrderByDistance(
		MakeArrayView(DistanceSquared),
		OutOrder);

	const TArray<int32> ExpectedOrder = {4, 1, 3, 2, 0};
	TestTrue(TEXT("Distance order keeps equal-distance inputs stable"), OutOrder == ExpectedOrder);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldGatherOrderSkippedForZeroCapTest,
                                 "KawaiiPhysics.SimpleWorld.GatherOrderSkippedForZeroCap",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldGatherOrderSkippedForZeroCapTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Zero cap skips gather order with several overlaps"),
	          KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(5, 0));
	TestTrue(TEXT("Gather order is used when overlaps exceed the cap"),
	         KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(5, 3));
	TestFalse(TEXT("Gather order is skipped when overlaps equal the cap"),
	          KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(3, 3));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldMemberSlotRemovedWithMemberTest,
                                 "KawaiiPhysics.SimpleWorld.MemberSlotRemovedWithMember",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldMemberSlotRemovedWithMemberTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderID = 501;
	constexpr uint64 ReaderID = 502;
	USkeletalMeshComponent* SkelCompX = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* SkelCompY = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);

	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	Desc.bGatherFamilyMembers = true;

	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
		Entry.SetDesc(ProviderID, Desc, GFrameCounter, SkelCompX, true);
		Entry.AddReaderMember(ReaderID, SkelCompY, GFrameCounter);

		Entry.MemberSlots.Add(
			TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompX),
			MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>());
		Entry.MemberSlots.Add(
			TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompY),
			MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>());
		TestEqual(TEXT("Two member slots exist before reader removal"), Entry.GetNumMemberSlots(), 2);

		Entry.RemoveReaderMember(ReaderID);
		TestEqual(TEXT("Only provider member slot remains after removing reader Y"), Entry.GetNumMemberSlots(), 1);
		TestTrue(TEXT("Reader Y member slot is removed"),
		         !Entry.MemberSlots.Contains(TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompY)));
	}

	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
		Entry.SetDesc(ProviderID, Desc, GFrameCounter, SkelCompX, true);
		Entry.AddReaderMember(ReaderID, SkelCompY, GFrameCounter);
		Entry.MemberSlots.Add(
			TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompX),
			MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>());
		Entry.MemberSlots.Add(
			TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompY),
			MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>());

		FKawaiiPhysicsSimpleWorldCollisionDesc DisabledMemberGatherDesc = Desc;
		DisabledMemberGatherDesc.bGatherFamilyMembers = false;
		Entry.SetDesc(ProviderID, DisabledMemberGatherDesc, GFrameCounter, SkelCompX, true);
		TestEqual(TEXT("All member slots are removed when merged desc stops gathering members"),
		          Entry.GetNumMemberSlots(),
		          0);
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

		FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& StaticComponent =
			Entry.GatheredComponents.AddDefaulted_GetRef();
		StaticComponent.Component = NewObject<UStaticMeshComponent>(GetTransientPackage());
		StaticComponent.bStatic = true;
		StaticComponent.FadeAlpha = 1.0f;

		FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& InvalidInstanceComponent =
			Entry.GatheredComponents.AddDefaulted_GetRef();
		InvalidInstanceComponent.bStatic = false;
		InvalidInstanceComponent.FadeAlpha = 0.25f;

		FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& SkeletalBodyComponent =
			Entry.GatheredComponents.AddDefaulted_GetRef();
		SkeletalBodyComponent.bStatic = false;
		SkeletalBodyComponent.BodyBindings.SetNum(2);

		Entry.bHasGroundBox = true;
		Entry.bGroundComponentStatic = false;

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
		TestTrue(TEXT("Filled entry MinFadeAlpha"),
		         FMath::IsNearlyEqual(Info.MinFadeAlpha, 0.25f, GSimpleWorldTol));
		TestEqual(TEXT("Filled entry NumDescs"), Info.NumDescs, 2);
		TestTrue(TEXT("Filled entry has ground box"), Info.bHasGroundBox);
		TestFalse(TEXT("Filled entry ground component static"), Info.bGroundComponentStatic);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldScaledConsumerKeepsWorldDimensionsTest,
                                 "KawaiiPhysics.SimpleWorld.ScaledConsumerKeepsWorldDimensions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldScaledConsumerKeepsWorldDimensionsTest::RunTest(const FString& Parameters)
{
	// ローカルと共有リーダーの両方で、形状・地面の再構築と同一シリアル更新を通す。
	for (const bool bSharedReader : {false, true})
	{
		FKawaiiPhysicsSharedCollisionData WorldData;
		FSphericalLimit Sphere;
		Sphere.Location = FVector(20.0f, 40.0f, 60.0f);
		Sphere.Radius = 10.0f;
		WorldData.SphericalLimits.Add(Sphere);
		FCapsuleLimit Capsule;
		Capsule.Location = Sphere.Location;
		Capsule.Radius = 4.0f;
		Capsule.Length = 50.0f;
		WorldData.CapsuleLimits.Add(Capsule);
		FTaperedCapsuleLimit Tapered;
		Tapered.Location = Sphere.Location;
		Tapered.Radius0 = 4.0f;
		Tapered.Radius1 = 6.0f;
		Tapered.Length = 50.0f;
		WorldData.TaperedCapsuleLimits.Add(Tapered);
		FBoxLimit Box;
		Box.Location = FVector(0.0f, 0.0f, -10.0f);
		Box.Extent = FVector(100.0f, 100.0f, 10.0f);
		WorldData.BoxLimits.Add(Box);
		FKawaiiPhysicsConvexLimit Convex;
		Convex.Location = Sphere.Location;
		Convex.LocalPlanes = MakeUnitCubePlanes();
		Convex.LocalBounds = FBox(-FVector::OneVector, FVector::OneVector);
#if !UE_BUILD_SHIPPING
		Convex.LocalVertices = MakeUnitCubeVertices();
		Convex.LocalEdges = {0, 1, 1, 2};
#endif
		WorldData.ConvexLimits.Add(Convex);
		FKawaiiPhysicsSharedCollisionData GroundData;
		GroundData.BoxLimits.Add(Box);
		const auto Entry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		Entry->Slot.Publish(WorldData);
		Entry->GroundSlot.Publish(GroundData);

		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.SetComponentSpaceCollisionTransform(
			FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0f)));
		if (bSharedReader)
		{
			Accessor.InjectSharedPublisherState(MakeSimpleWorldReaderState(false), Entry);
		}
		else
		{
			Accessor.SetSimpleWorldEntry(Entry);
		}
		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext Output(&Proxy);
		Accessor.UpdateSimpleWorldCollisionLimits(Output);
		if (!TestEqual(TEXT("One sphere"), Accessor.GetSimpleWorldSphericalLimits().Num(), 1)
			|| !TestEqual(TEXT("One capsule"), Accessor.GetSimpleWorldCapsuleLimits().Num(), 1)
			|| !TestEqual(TEXT("One tapered capsule"), Accessor.GetSimpleWorldTaperedCapsuleLimits().Num(), 1)
			|| !TestEqual(TEXT("One box"), Accessor.GetSimpleWorldBoxLimits().Num(), 1)
			|| !TestEqual(TEXT("One convex"), Accessor.GetSimpleWorldConvexLimits().Num(), 1)
			|| !TestEqual(TEXT("One ground box"), Accessor.GetSimpleWorldGroundBoxLimits().Num(), 1))
		{
			return false;
		}
		const uint64 Serial = Accessor.GetLastReadSimpleWorldShapeSerial();
		for (int32 Refresh = 0; Refresh <= 1; ++Refresh)
		{
			if (Refresh > 0)
			{
				Accessor.UpdateSimpleWorldCollisionLimits(Output);
			}
			TestEqual(TEXT("Shape serial is unchanged"), Accessor.GetLastReadSimpleWorldShapeSerial(), Serial);
			TestEqual(TEXT("Sphere radius remains half"), Accessor.GetSimpleWorldSphericalLimits()[0].Radius, 5.0f);
			TestTrue(TEXT("Sphere position is halved"),
				Accessor.GetSimpleWorldSphericalLimits()[0].Location.Equals(Sphere.Location * 0.5f));
			TestEqual(TEXT("Capsule radius remains half"), Accessor.GetSimpleWorldCapsuleLimits()[0].Radius, 2.0f);
			TestEqual(TEXT("Capsule length remains half"), Accessor.GetSimpleWorldCapsuleLimits()[0].Length, 25.0f);
			TestTrue(TEXT("Capsule position is halved"),
				Accessor.GetSimpleWorldCapsuleLimits()[0].Location.Equals(Capsule.Location * 0.5f));
			const FTaperedCapsuleLimit& SimTapered = Accessor.GetSimpleWorldTaperedCapsuleLimits()[0];
			TestEqual(TEXT("Tapered radius zero remains half"), SimTapered.Radius0, 2.0f);
			TestEqual(TEXT("Tapered radius one remains half"), SimTapered.Radius1, 3.0f);
			TestEqual(TEXT("Tapered length remains half"), SimTapered.Length, 25.0f);
			TestTrue(TEXT("Box extent remains half"),
				Accessor.GetSimpleWorldBoxLimits()[0].Extent.Equals(FVector(50.0f, 50.0f, 5.0f)));
			TestTrue(TEXT("Box position is halved"),
				Accessor.GetSimpleWorldBoxLimits()[0].Location.Equals(Box.Location * 0.5f));
			const FBoxLimit& Ground = Accessor.GetSimpleWorldGroundBoxLimits()[0];
			TestTrue(TEXT("Ground extent remains half"), Ground.Extent.Equals(FVector(50.0f, 50.0f, 5.0f)));
			TestTrue(TEXT("Ground top remains at world zero"),
				FMath::IsNearlyZero((Ground.Location.Z + Ground.Extent.Z) * 2.0));
			const FKawaiiPhysicsConvexLimit& SimConvex = Accessor.GetSimpleWorldConvexLimits()[0];
			TestTrue(TEXT("Convex position is halved"), SimConvex.Location.Equals(Convex.Location * 0.5f));
			TestTrue(TEXT("Convex bounds remain half"), SimConvex.LocalBounds.Min.Equals(FVector(-0.5f))
				&& SimConvex.LocalBounds.Max.Equals(FVector(0.5f)));
			for (int32 Index = 0; Index < Convex.LocalPlanes.Num(); ++Index)
			{
				const FPlane& Plane = SimConvex.LocalPlanes[Index];
				TestTrue(TEXT("Convex plane distance remains half"), FMath::IsNearlyEqual(Plane.W, 0.5));
				TestTrue(TEXT("Convex plane normal is unchanged"),
					FVector(Plane.X, Plane.Y, Plane.Z).Equals(FVector(Convex.LocalPlanes[Index])));
			}
#if !UE_BUILD_SHIPPING
			for (int32 Index = 0; Index < Convex.LocalVertices.Num(); ++Index)
			{
				TestTrue(TEXT("Convex vertex remains half"),
					SimConvex.LocalVertices[Index].Equals(Convex.LocalVertices[Index] * 0.5f));
			}
			TestTrue(TEXT("Convex edge indices are unchanged"), SimConvex.LocalEdges == Convex.LocalEdges);
#endif
		}
	}
	return true;
}

namespace
{
	FVector ConvertSimpleWorldScaleLocation(FKawaiiPhysicsTestAccessor& Accessor,
		FComponentSpacePoseContext& Output, EKawaiiPhysicsSimulationSpace TargetSpace, const FVector& Position)
	{
		return Accessor.Node.ConvertSimulationSpaceLocation(Output,
			EKawaiiPhysicsSimulationSpace::WorldSpace, TargetSpace, Position);
	}

	FVector GetSimpleWorldScaleAxisImageLengths(FKawaiiPhysicsTestAccessor& Accessor,
		FComponentSpacePoseContext& Output, EKawaiiPhysicsSimulationSpace TargetSpace)
	{
		const FVector Origin = ConvertSimpleWorldScaleLocation(Accessor, Output, TargetSpace, FVector::ZeroVector);
		return FVector(
			(ConvertSimpleWorldScaleLocation(Accessor, Output, TargetSpace, FVector::XAxisVector) - Origin).Size(),
			(ConvertSimpleWorldScaleLocation(Accessor, Output, TargetSpace, FVector::YAxisVector) - Origin).Size(),
			(ConvertSimpleWorldScaleLocation(Accessor, Output, TargetSpace, FVector::ZAxisVector) - Origin).Size());
	}

	bool AreSimpleWorldScaleCornersContained(FKawaiiPhysicsTestAccessor& Accessor,
		FComponentSpacePoseContext& Output, EKawaiiPhysicsSimulationSpace TargetSpace,
		const FBoxLimit& Source, const FBoxLimit& Mapped, double Tolerance)
	{
		for (const FVector& Sign : MakeUnitCubeVertices())
		{
			const FVector WorldCorner = Source.Location + Source.Rotation.RotateVector(Sign * Source.Extent);
			const FVector LocalCorner = Mapped.Rotation.UnrotateVector(
				ConvertSimpleWorldScaleLocation(Accessor, Output, TargetSpace, WorldCorner) - Mapped.Location).GetAbs();
			if (LocalCorner.X > Mapped.Extent.X + Tolerance
				|| LocalCorner.Y > Mapped.Extent.Y + Tolerance
				|| LocalCorner.Z > Mapped.Extent.Z + Tolerance)
			{
				return false;
			}
		}
		return true;
	}

	bool DoesSimpleWorldScaleSphereCoverAxes(const FSphericalLimit& Sphere,
		const FVector& AxisLengths, double Tolerance)
	{
		return Sphere.Radius + Tolerance >= FMath::Max3(AxisLengths.X, AxisLengths.Y, AxisLengths.Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldNonUniformScaleMappingIsConservativeTest,
                                 "KawaiiPhysics.SimpleWorld.NonUniformScaleMappingIsConservative",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldNonUniformScaleMappingIsConservativeTest::RunTest(const FString& Parameters)
{
	// 異なる写像の前提と、頂点・球半径の包含をケースごとに確認する。
	struct FScaleCase
	{
		const TCHAR* Name;
		int32 Kind;
		double Value;
	};
	const FScaleCase Cases[] = {
		{TEXT("AxisSwap90"), 0, 90.0},
		{TEXT("AxisShear37"), 0, 37.0},
		{TEXT("Compensating0"), 1, 0.0},
		{TEXT("Compensating90"), 1, 90.0},
		{TEXT("CollapsedSmall"), 2, 0.00001},
		{TEXT("CollapsedLarge"), 2, 100000.0},
		{TEXT("NearUniformOrthogonal"), 3, 0.0},
		{TEXT("NearUniformShear"), 3, 1.0},
		{TEXT("WithinTolerance"), 4, 4e-10},
		{TEXT("OutsideTolerance"), 4, 4e-9},
	};
	for (const FScaleCase& Case : Cases)
	{
		FKawaiiPhysicsTestAccessor Accessor;
		auto TargetSpace = EKawaiiPhysicsSimulationSpace::ComponentSpace;
		FBoxLimit Box;
		Box.Location = FVector::ZeroVector;
		Box.Rotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(45.0));
		Box.Extent = FVector(10.0, 1.0, 1.0);
		if (Case.Kind == 0)
		{
			Accessor.SetComponentSpaceCollisionTransform(
				FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0, 1.0, 1.0)));
			Box.Location = FVector(20.0, 30.0, 40.0);
			Box.Rotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Case.Value));
		}
		else if (Case.Kind == 1)
		{
			Accessor.SetComponentSpaceCollisionTransform(FTransform(
				FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Case.Value)),
				FVector::ZeroVector, FVector(2.0, 1.0, 1.0)));
			TargetSpace = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
			Accessor.SetSimulationSpaceCollisionTransform(TargetSpace, FTransform(
				FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(90.0)),
				FVector::ZeroVector, FVector(0.5, 1.0, 1.0)));
			Box.Rotation = FQuat::Identity;
			Box.Extent = FVector::OneVector;
		}
		else if (Case.Kind == 2)
		{
			Accessor.SetComponentSpaceCollisionTransform(
				FTransform(FQuat::Identity, FVector::ZeroVector, FVector(Case.Value, 1.0, 1.0)));
		}
		else if (Case.Kind == 3)
		{
			Box.Extent = FVector(1000.0, 0.1, 1.0);
			if (Case.Value != 0.0)
			{
				const FQuat Rotation(FVector::ZAxisVector, FMath::DegreesToRadians(45.0));
				Accessor.SetComponentSpaceCollisionTransform(FTransform(Rotation, FVector::ZeroVector));
				TargetSpace = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
				Accessor.SetSimulationSpaceCollisionTransform(TargetSpace,
					FTransform(Rotation.Inverse(), FVector::ZeroVector,
						FVector(1.0 / 1.00025, 1.0 / 0.99975, 1.0)));
			}
			else
			{
				Accessor.SetComponentSpaceCollisionTransform(
					FTransform(FQuat::Identity, FVector::ZeroVector, FVector(1.0005, 1.0, 1.0)));
			}
		}
		else
		{
			Box.Extent = FVector(100000.0, 0.1, 1.0);
			Accessor.SetComponentSpaceCollisionTransform(FTransform(
				FQuat::Identity, FVector::ZeroVector, FVector(1.0 / (1.0 + Case.Value), 1.0, 1.0)));
		}

		FSphericalLimit Sphere;
		Sphere.Location = FVector::ZeroVector;
		Sphere.Radius = 1.0f;
		FKawaiiPhysicsSharedCollisionData WorldData;
		WorldData.BoxLimits.Add(Box);
		WorldData.SphericalLimits.Add(Sphere);
		const auto Entry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		Entry->Slot.Publish(WorldData);
		Accessor.SetSimpleWorldEntry(Entry);
		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext Output(&Proxy);
		const FVector AxisLengths = GetSimpleWorldScaleAxisImageLengths(Accessor, Output, TargetSpace);
		const double MaxImageLength = FMath::Max3(AxisLengths.X, AxisLengths.Y, AxisLengths.Z);
		const double MinImageLength = FMath::Min3(AxisLengths.X, AxisLengths.Y, AxisLengths.Z);
		const double RelativeLengthSpread = (MaxImageLength - MinImageLength) / MaxImageLength;

		if (Case.Kind == 1 && Case.Value == 90.0)
		{
			TestTrue(FString::Printf(TEXT("%s: mapped unit axes are unequal"), Case.Name),
				MaxImageLength - MinImageLength > 1e-3 * MaxImageLength);
		}
		if (Case.Kind == 2)
		{
			TestTrue(FString::Printf(TEXT("%s: mapped unit axes are non-uniform"), Case.Name),
				FMath::Abs(AxisLengths.X - AxisLengths.Y) > 1e-6 * MaxImageLength);
			if (Case.Value == 100000.0)
			{
				TestTrue(FString::Printf(TEXT("%s: one mapped axis is collapsed"), Case.Name),
					AxisLengths.X > 0.0 && AxisLengths.X < KINDA_SMALL_NUMBER);
			}
		}
		if (Case.Kind == 3)
		{
			if (Case.Value != 0.0)
			{
				TestTrue(FString::Printf(TEXT("%s: length spread is in the shear range"), Case.Name),
					RelativeLengthSpread > 1e-9 && RelativeLengthSpread < 1e-3);
				const FVector Origin = ConvertSimpleWorldScaleLocation(Accessor, Output, TargetSpace, FVector::ZeroVector);
				const FVector AxisX = ConvertSimpleWorldScaleLocation(Accessor, Output, TargetSpace, FVector::XAxisVector) - Origin;
				const FVector AxisY = ConvertSimpleWorldScaleLocation(Accessor, Output, TargetSpace, FVector::YAxisVector) - Origin;
				const double RelativeDot = FMath::Abs(FVector::DotProduct(AxisX, AxisY))
					/ (AxisX.Size() * AxisY.Size());
				TestTrue(FString::Printf(TEXT("%s: shear is in the measured range"), Case.Name),
					RelativeDot > 1e-9 && RelativeDot < 1e-3);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s: anisotropy is in the measured range"), Case.Name),
					FMath::Abs(AxisLengths.X - AxisLengths.Y) / MaxImageLength > 1e-9
					&& FMath::Abs(AxisLengths.X - AxisLengths.Y) / MaxImageLength < 1e-3);
			}
		}
		if (Case.Kind == 4)
		{
			TestTrue(FString::Printf(TEXT("%s: similarity tolerance side"), Case.Name),
				Case.Value == 4e-10 ? RelativeLengthSpread <= 1e-9 : RelativeLengthSpread > 1e-9);
		}

		// 更新二回目と serial の不変性は一つのケースで確認する。
		const int32 NumRefreshes = Case.Kind == 1 && Case.Value == 90.0 ? 2 : 1;
		uint64 ShapeSerial = 0;
		for (int32 Refresh = 0; Refresh < NumRefreshes; ++Refresh)
		{
			Accessor.UpdateSimpleWorldCollisionLimits(Output);
			if (!TestEqual(FString::Printf(TEXT("%s: one transformed box"), Case.Name),
					Accessor.GetSimpleWorldBoxLimits().Num(), 1)
				|| !TestEqual(FString::Printf(TEXT("%s: one transformed sphere"), Case.Name),
					Accessor.GetSimpleWorldSphericalLimits().Num(), 1))
			{
				return false;
			}
			if (Refresh == 0)
			{
				ShapeSerial = Accessor.GetLastReadSimpleWorldShapeSerial();
			}
			else
			{
				TestEqual(FString::Printf(TEXT("%s: refresh keeps shape serial"), Case.Name),
					Accessor.GetLastReadSimpleWorldShapeSerial(), ShapeSerial);
			}
			const FBoxLimit& SimBox = Accessor.GetSimpleWorldBoxLimits()[0];
			const FSphericalLimit& SimSphere = Accessor.GetSimpleWorldSphericalLimits()[0];
			if (Case.Kind == 0 && Case.Value == 90.0)
			{
				TestTrue(FString::Printf(TEXT("%s: quarter-turn extent"), Case.Name),
					SimBox.Extent.Equals(FVector(10.0, 0.5, 1.0), 1e-4));
			}
			const double CornerTolerance = Case.Kind == 0 ? 1e-4
				: Case.Kind == 1 ? GSimpleWorldTol
				: Case.Kind == 4 && Case.Value == 4e-10
					? 1e-9 * (Box.Extent.X + Box.Extent.Y + Box.Extent.Z) : 1e-9;
			TestTrue(FString::Printf(TEXT("%s: all eight mapped corners meet the error bound"), Case.Name),
				AreSimpleWorldScaleCornersContained(Accessor, Output, TargetSpace, Box, SimBox, CornerTolerance));
			TestTrue(FString::Printf(TEXT("%s: sphere covers the longest mapped axis"), Case.Name),
				DoesSimpleWorldScaleSphereCoverAxes(SimSphere, AxisLengths,
					Case.Kind == 1 ? GSimpleWorldTol : Case.Kind == 4 ? MaxImageLength * 1e-6 : 0.0));

			if (Case.Kind == 3)
			{
				TestTrue(FString::Printf(TEXT("%s: conservative sphere radius remains tight"), Case.Name),
					SimSphere.Radius <= MaxImageLength * (Case.Value != 0.0 ? 1.001 : 1.0 + 1e-6));
			}
			if (Case.Kind == 4 && Case.Value == 4e-10)
			{
				TestEqual(FString::Printf(TEXT("%s: fast path box extent X"), Case.Name), SimBox.Extent.X, Box.Extent.X);
				TestEqual(FString::Printf(TEXT("%s: fast path box extent Y"), Case.Name), SimBox.Extent.Y, Box.Extent.Y);
				TestEqual(FString::Printf(TEXT("%s: fast path box extent Z"), Case.Name), SimBox.Extent.Z, Box.Extent.Z);
				TestEqual(FString::Printf(TEXT("%s: fast path sphere radius"), Case.Name), SimSphere.Radius, 1.0f);
			}
			if (Case.Kind == 4 && Case.Value == 4e-9)
			{
				TestTrue(FString::Printf(TEXT("%s: corner fit grows thin box axis"), Case.Name),
					SimBox.Extent.Y >= 0.1 + 1e-4);
				TestTrue(FString::Printf(TEXT("%s: sphere radius matches longest axis"), Case.Name),
					SimSphere.Radius >= MaxImageLength * (1.0 - 1e-6)
					&& SimSphere.Radius <= MaxImageLength * (1.0 + 1e-6));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionScaledSourceAndTargetRoundTripTest,
                                 "KawaiiPhysics.SharedCollision.ScaledSourceAndTargetRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionScaledSourceAndTargetRoundTripTest::RunTest(const FString& Parameters)
{
	const auto Entry = MakeShared<FKawaiiPhysicsSharedCollisionEntry>();
	const auto Slot = Entry->GetOrCreateSlot(501);
	FKawaiiPhysicsTestAccessor Source;
	const FTransform SourceTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0f));
	Source.SetComponentSpaceCollisionTransform(SourceTransform);
	Source.SetSharedCollisionSourceSlot(Slot);
	FSphericalLimit Sphere;
	Sphere.bEnable = true;
	Sphere.Radius = 10.0f;
	Sphere.Location = FVector(10.0f, 20.0f, 30.0f);
	Source.Node.SphericalLimits.Add(Sphere);
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	Source.WriteSharedCollisionToSubsystem(Output, SourceTransform);
	FKawaiiPhysicsSharedCollisionData WorldData;
	Slot->AppendTo(WorldData);
	if (!TestEqual(TEXT("Published one sphere"), WorldData.SphericalLimits.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Source scale is baked into world radius"), WorldData.SphericalLimits[0].Radius, 20.0f);
	TestTrue(TEXT("Source position is converted to world"),
		WorldData.SphericalLimits[0].Location.Equals(Sphere.Location * 2.0f));
	for (const float TargetScale : {1.0f, 2.0f})
	{
		FKawaiiPhysicsTestAccessor Target;
		Target.SetComponentSpaceCollisionTransform(
			FTransform(FQuat::Identity, FVector::ZeroVector, FVector(TargetScale)));
		Target.SetSharedCollisionEntry(Entry);
		Target.UpdateSharedCollisionLimits(Output);
		if (!TestEqual(TEXT("Target reads one sphere"), Target.GetSharedSphericalLimits().Num(), 1))
		{
			return false;
		}
		TestEqual(TEXT("Target radius accounts for both scales"),
			Target.GetSharedSphericalLimits()[0].Radius, 20.0f / TargetScale);
		TestTrue(TEXT("Target position accounts for both scales"),
			Target.GetSharedSphericalLimits()[0].Location.Equals(Sphere.Location * (2.0f / TargetScale)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionRotatedNonUniformCapsulePublishContainsSourceTest,
                                 "KawaiiPhysics.SharedCollision.RotatedNonUniformCapsulePublishContainsSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionRotatedNonUniformCapsulePublishContainsSourceTest::RunTest(const FString& Parameters)
{
	const FQuat QuarterTurn(FVector::YAxisVector, FMath::DegreesToRadians(90.0));
	const FTransform ComponentTransform(QuarterTurn, FVector::ZeroVector, FVector(2.0, 1.0, 1.0));
	const auto Entry = MakeShared<FKawaiiPhysicsSharedCollisionEntry>();
	const auto Slot = Entry->GetOrCreateSlot(502);
	FKawaiiPhysicsTestAccessor Source;
	Source.SetComponentSpaceCollisionTransform(ComponentTransform);
	Source.SetSharedCollisionSourceSlot(Slot);
	FCapsuleLimit Capsule;
	Capsule.bEnable = true;
	Capsule.Location = FVector::ZeroVector;
	Capsule.Rotation = QuarterTurn;
	Capsule.Length = 10.0f;
	Capsule.Radius = 1.0f;
	Capsule.UpdateRuntimeCache();
	Source.Node.CapsuleLimits.Add(Capsule);

	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	Source.WriteSharedCollisionToSubsystem(Output, ComponentTransform);
	FKawaiiPhysicsSharedCollisionData WorldData;
	Slot->AppendTo(WorldData);
	if (!TestEqual(TEXT("Published one capsule"), WorldData.CapsuleLimits.Num(), 1))
	{
		return false;
	}
	FCapsuleLimit& WorldCapsule = WorldData.CapsuleLimits[0];
	WorldCapsule.UpdateRuntimeCache();
	const FVector Q0 = ComponentTransform.TransformPosition(Capsule.CachedStartPoint);
	const FVector Q1 = ComponentTransform.TransformPosition(Capsule.CachedEndPoint);
	TestTrue(TEXT("Published capsule length is twenty"),
		FMath::IsNearlyEqual(WorldCapsule.Length, 20.0f, GSimpleWorldTol));
	TestTrue(TEXT("Published endpoint distance is twenty"),
		FMath::IsNearlyEqual(FVector::Distance(WorldCapsule.CachedStartPoint, WorldCapsule.CachedEndPoint),
			20.0, static_cast<double>(GSimpleWorldTol)));
	TestTrue(TEXT("Positive endpoint matches the transformed source endpoint"),
		WorldCapsule.CachedStartPoint.Equals(Q0, GSimpleWorldTol));
	TestTrue(TEXT("Negative endpoint matches the transformed source endpoint"),
		WorldCapsule.CachedEndPoint.Equals(Q1, GSimpleWorldTol));
	TestTrue(TEXT("Published radius covers the source radius"), WorldCapsule.Radius >= 1.0f);
	TestTrue(TEXT("Published positive axis follows transformed endpoints"),
		WorldCapsule.Rotation.GetAxisZ().Equals((Q0 - Q1).GetSafeNormal(), GSimpleWorldTol));

	// 両端点・中点と、軸に直交する断面の４点を実際のコンポーネント変換で写す。
	const FVector SideX = Capsule.Rotation.GetAxisX() * Capsule.Radius;
	const FVector SideY = Capsule.Rotation.GetAxisY() * Capsule.Radius;
	const FVector SourcePoints[] = {
		Capsule.CachedStartPoint, Capsule.CachedEndPoint, Capsule.Location,
		Capsule.Location + SideX, Capsule.Location - SideX,
		Capsule.Location + SideY, Capsule.Location - SideY
	};
	for (const FVector& Point : SourcePoints)
	{
		const FVector WorldPoint = ComponentTransform.TransformPosition(Point);
		const FVector Closest = FMath::ClosestPointOnSegment(
			WorldPoint, WorldCapsule.CachedStartPoint, WorldCapsule.CachedEndPoint);
		TestTrue(TEXT("Transformed source point is inside published capsule"),
			FVector::Distance(WorldPoint, Closest) <= WorldCapsule.Radius + GSimpleWorldTol);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldNonUniformTaperedCapsuleKeepsContainmentTest,
	"KawaiiPhysics.SimpleWorld.NonUniformTaperedCapsuleKeepsContainment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldNonUniformTaperedCapsuleKeepsContainmentTest::RunTest(const FString& Parameters)
{
	// 各断面の ±AxisX・±AxisY と、両端点から ±AxisZ へ半径分進めた点を省略せず使う。
	const auto MakeSourcePoints = [](const FTaperedCapsuleLimit& Capsule, const TArray<float>& Parameters)
	{
		TArray<FVector> Points;
		for (const float T : Parameters)
		{
			const FVector Center = Capsule.CachedStartPoint + Capsule.CachedSegment * T;
			const float Radius = FMath::Lerp(Capsule.Radius0, Capsule.Radius1, T);
			const FVector SideX = Capsule.Rotation.GetAxisX() * Radius;
			const FVector SideY = Capsule.Rotation.GetAxisY() * Radius;
			Points.Add(Center + SideX);
			Points.Add(Center - SideX);
			Points.Add(Center + SideY);
			Points.Add(Center - SideY);
		}
		const FVector AxisZ = Capsule.Rotation.GetAxisZ();
		Points.Add(Capsule.CachedStartPoint + AxisZ * Capsule.Radius0);
		Points.Add(Capsule.CachedStartPoint - AxisZ * Capsule.Radius0);
		Points.Add(Capsule.CachedEndPoint + AxisZ * Capsule.Radius1);
		Points.Add(Capsule.CachedEndPoint - AxisZ * Capsule.Radius1);
		return Points;
	};
	// ソルバと同じ線分への射影位置で半径を補間して包含を判定する。
	const auto ContainsPoint = [](const FTaperedCapsuleLimit& Capsule, const FVector& Point)
	{
		const float T = FMath::Clamp(FVector::DotProduct(Point - Capsule.CachedStartPoint, Capsule.CachedSegment)
			/ Capsule.CachedSegmentSizeSq, 0.0f, 1.0f);
		const FVector Closest = Capsule.CachedStartPoint + Capsule.CachedSegment * T;
		const float Radius = FMath::Lerp(Capsule.Radius0, Capsule.Radius1, T);
		return FVector::Distance(Point, Closest) <= Radius + 1e-6;
	};

	// Read path で旧実装の反例点を含む包含を確認する。
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetComponentSpaceCollisionTransform(
		FTransform(FQuat::Identity, FVector::ZeroVector, FVector(20.0 / 11.0, 2.0, 2.0)));
	FTaperedCapsuleLimit WorldCapsule;
	WorldCapsule.Location = FVector::ZeroVector;
	WorldCapsule.Rotation = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(45.0));
	WorldCapsule.Length = 10.0f;
	WorldCapsule.Radius0 = 10.0f;
	WorldCapsule.Radius1 = 1.0f;
	WorldCapsule.UpdateRuntimeCache();
	TArray<FVector> WorldPoints = MakeSourcePoints(WorldCapsule, {0.0f, 0.25f, 0.5f, 0.75f, 1.0f});
	const FVector U = FVector(1.0, 0.0, 1.0) / FMath::Sqrt(2.0);
	const FVector V = FVector(1.0, 0.0, -1.0) / FMath::Sqrt(2.0);
	// 旧実装の端点ごとの半径スケールでは P = 5u - 9.9v が包含外（距離 ≈ 5.18、補間半径 ≈ 5.03）になる。
	WorldPoints.Add(5.0 * U - 9.9 * V);
	FKawaiiPhysicsSharedCollisionData ReadWorldData;
	ReadWorldData.TaperedCapsuleLimits.Add(WorldCapsule);
	const auto ReadEntry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	ReadEntry->Slot.Publish(ReadWorldData);
	Accessor.SetSimpleWorldEntry(ReadEntry);
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	Accessor.UpdateSimpleWorldCollisionLimits(Output);
	if (!TestEqual(TEXT("One transformed tapered capsule"), Accessor.GetSimpleWorldTaperedCapsuleLimits().Num(), 1))
	{
		return false;
	}
	// Read path はキャッシュを再計算しないため、ソルバの準備処理と同様にコピー上で更新する。
	FTaperedCapsuleLimit SimCapsule = Accessor.GetSimpleWorldTaperedCapsuleLimits()[0];
	SimCapsule.UpdateRuntimeCache();
	TestEqual(TEXT("Converted endpoint radii are equal"), SimCapsule.Radius0, SimCapsule.Radius1);
	TestEqual(TEXT("Converted radius uses the larger source radius and scale bound"), SimCapsule.Radius0, 5.5f);
	for (int32 Index = 0; Index < WorldPoints.Num(); ++Index)
	{
		const FVector SimPoint = Accessor.Node.ConvertSimulationSpaceLocation(Output,
			EKawaiiPhysicsSimulationSpace::WorldSpace, EKawaiiPhysicsSimulationSpace::ComponentSpace, WorldPoints[Index]);
		TestTrue(FString::Printf(TEXT("Read pass contains mapped source point %d"), Index),
			ContainsPoint(SimCapsule, SimPoint));
	}

	// Publish path: 回転と非一様スケールを持つ Component から同じ包含判定を通す。
	const FQuat QuarterTurn(FVector::YAxisVector, FMath::DegreesToRadians(90.0));
	const FTransform ComponentTransform(QuarterTurn, FVector::ZeroVector, FVector(2.0, 1.0, 1.0));
	const auto Entry = MakeShared<FKawaiiPhysicsSharedCollisionEntry>();
	const auto Slot = Entry->GetOrCreateSlot(503);
	FKawaiiPhysicsTestAccessor Source;
	Source.SetComponentSpaceCollisionTransform(ComponentTransform);
	Source.SetSharedCollisionSourceSlot(Slot);
	FTaperedCapsuleLimit Capsule;
	Capsule.bEnable = true;
	Capsule.Location = FVector::ZeroVector;
	Capsule.Rotation = QuarterTurn;
	Capsule.Length = 10.0f;
	Capsule.Radius0 = 3.0f;
	Capsule.Radius1 = 1.0f;
	Capsule.UpdateRuntimeCache();
	Source.Node.TaperedCapsuleLimits.Add(Capsule);
	Source.WriteSharedCollisionToSubsystem(Output, ComponentTransform);
	FKawaiiPhysicsSharedCollisionData WorldData;
	Slot->AppendTo(WorldData);
	if (!TestEqual(TEXT("Published one tapered capsule"), WorldData.TaperedCapsuleLimits.Num(), 1))
	{
		return false;
	}
	FTaperedCapsuleLimit PublishedCapsule = WorldData.TaperedCapsuleLimits[0];
	PublishedCapsule.UpdateRuntimeCache();
	TestEqual(TEXT("Published endpoint radii are equal"), PublishedCapsule.Radius0, PublishedCapsule.Radius1);
	TestEqual(TEXT("Published radius uses the larger source radius and scale bound"), PublishedCapsule.Radius0, 6.0f);
	const TArray<FVector> SourcePoints = MakeSourcePoints(Capsule, {0.0f, 0.5f, 1.0f});
	for (int32 Index = 0; Index < SourcePoints.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("Published tapered capsule contains mapped source point %d"), Index),
			ContainsPoint(PublishedCapsule, ComponentTransform.TransformPosition(SourcePoints[Index])));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSimilarityMappingKeepsDimensionsExactTest,
                                 "KawaiiPhysics.SimpleWorld.SimilarityMappingKeepsDimensionsExact",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSimilarityMappingKeepsDimensionsExactTest::RunTest(const FString& Parameters)
{
	for (const double Angle : {0.0, 37.0})
	{
		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.SetComponentSpaceCollisionTransform(FTransform(
			FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Angle)), FVector::ZeroVector, FVector(1.0)));
		FSphericalLimit Sphere;
		Sphere.Radius = 10.25f;
		FBoxLimit Box;
		Box.Rotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(45.0));
		Box.Extent = FVector(1000.125, 0.1, 1.25);
		FCapsuleLimit Capsule;
		Capsule.Radius = 4.25f;
		Capsule.Length = 50.75f;
		FKawaiiPhysicsSharedCollisionData WorldData;
		WorldData.SphericalLimits.Add(Sphere);
		WorldData.BoxLimits.Add(Box);
		WorldData.CapsuleLimits.Add(Capsule);
		const auto Entry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		Entry->Slot.Publish(WorldData);
		Accessor.SetSimpleWorldEntry(Entry);
		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext Output(&Proxy);
		Accessor.UpdateSimpleWorldCollisionLimits(Output);
		if (!TestEqual(TEXT("One transformed sphere"), Accessor.GetSimpleWorldSphericalLimits().Num(), 1)
			|| !TestEqual(TEXT("One transformed box"), Accessor.GetSimpleWorldBoxLimits().Num(), 1)
			|| !TestEqual(TEXT("One transformed capsule"), Accessor.GetSimpleWorldCapsuleLimits().Num(), 1))
		{
			return false;
		}
		const FSphericalLimit& SimSphere = Accessor.GetSimpleWorldSphericalLimits()[0];
		const FBoxLimit& SimBox = Accessor.GetSimpleWorldBoxLimits()[0];
		const FCapsuleLimit& SimCapsule = Accessor.GetSimpleWorldCapsuleLimits()[0];
		// 近似比較を使わず、恒等・純回転で各寸法が完全に不変であることを確認する。
		TestTrue(TEXT("Unit similarity preserves sphere radius exactly"), SimSphere.Radius == Sphere.Radius);
		TestTrue(TEXT("Unit similarity preserves box extent exactly"), SimBox.Extent == Box.Extent);
		TestTrue(TEXT("Unit similarity preserves capsule length exactly"), SimCapsule.Length == Capsule.Length);
		TestTrue(TEXT("Unit similarity preserves capsule radius exactly"), SimCapsule.Radius == Capsule.Radius);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldInPlaceRefreshMatchesRebuildTest,
                                 "KawaiiPhysics.SimpleWorld.InPlaceRefreshMatchesRebuild",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldInPlaceRefreshMatchesRebuildTest::RunTest(const FString& Parameters)
{
	auto RunCase = [this](EKawaiiPhysicsSimulationSpace TargetSpace, const TCHAR* CaseName)
	{
		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.SetSimulationSpace(TargetSpace);

		FAnimInstanceProxy AnimInstanceProxy;
		FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

		const FKawaiiPhysicsSharedCollisionData WorldData = MakeReadPathWorldData(FVector::ZeroVector);
		const FKawaiiPhysicsSharedCollisionData GroundWorldData = MakeReadPathGroundWorldData(FVector::ZeroVector);
		const FKawaiiPhysicsSharedCollisionData InitialWorldData = MakeReadPathWorldData(FVector(100.0f, 50.0f, -25.0f));
		const FKawaiiPhysicsSharedCollisionData InitialGroundWorldData =
			MakeReadPathGroundWorldData(FVector(-40.0f, 20.0f, 15.0f));

		TArray<FSphericalLimit> RebuiltSpheres;
		TArray<FCapsuleLimit> RebuiltCapsules;
		TArray<FTaperedCapsuleLimit> RebuiltTaperedCapsules;
		TArray<FBoxLimit> RebuiltBoxes;
		TArray<FKawaiiPhysicsConvexLimit> RebuiltConvexes;
		KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(
			Accessor.Node, PoseContext, TargetSpace, WorldData,
			RebuiltSpheres, RebuiltCapsules, RebuiltTaperedCapsules, RebuiltBoxes, nullptr, &RebuiltConvexes);

		TArray<FSphericalLimit> GroundDummySpheres;
		TArray<FCapsuleLimit> GroundDummyCapsules;
		TArray<FTaperedCapsuleLimit> GroundDummyTaperedCapsules;
		TArray<FBoxLimit> RebuiltGroundBoxes;
		TArray<FKawaiiPhysicsConvexLimit> GroundDummyConvexes;
		KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(
			Accessor.Node, PoseContext, TargetSpace, GroundWorldData,
			GroundDummySpheres, GroundDummyCapsules, GroundDummyTaperedCapsules,
			RebuiltGroundBoxes, nullptr, &GroundDummyConvexes);

		TArray<FSphericalLimit> RefreshedSpheres;
		TArray<FCapsuleLimit> RefreshedCapsules;
		TArray<FTaperedCapsuleLimit> RefreshedTaperedCapsules;
		TArray<FBoxLimit> RefreshedBoxes;
		TArray<FKawaiiPhysicsConvexLimit> RefreshedConvexes;
		KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(
			Accessor.Node, PoseContext, TargetSpace, InitialWorldData,
			RefreshedSpheres, RefreshedCapsules, RefreshedTaperedCapsules, RefreshedBoxes, nullptr,
			&RefreshedConvexes);

		TArray<FBoxLimit> RefreshedGroundBoxes;
		KawaiiPhysicsSimpleWorldReadPath::AppendSharedCollisionDataToSimulationSpace(
			Accessor.Node, PoseContext, TargetSpace, InitialGroundWorldData,
			GroundDummySpheres, GroundDummyCapsules, GroundDummyTaperedCapsules,
			RefreshedGroundBoxes, nullptr, &GroundDummyConvexes);

		TestTrue(FString::Printf(TEXT("%s shape refresh succeeds"), CaseName),
		         KawaiiPhysicsSimpleWorldReadPath::RefreshSimulationSpaceLimitsInPlace(
			         Accessor.Node, PoseContext, TargetSpace, WorldData,
			         RefreshedSpheres, RefreshedCapsules, RefreshedTaperedCapsules,
			         RefreshedBoxes, RefreshedConvexes));
		TestTrue(FString::Printf(TEXT("%s ground refresh succeeds"), CaseName),
		         KawaiiPhysicsSimpleWorldReadPath::RefreshSimulationSpaceLimitsInPlace(
			         Accessor.Node, PoseContext, TargetSpace, GroundWorldData.BoxLimits, RefreshedGroundBoxes));

		TestEqual(FString::Printf(TEXT("%s sphere count"), CaseName), RefreshedSpheres.Num(), RebuiltSpheres.Num());
		for (int32 Index = 0; Index < RebuiltSpheres.Num() && Index < RefreshedSpheres.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s sphere %d location"), CaseName, Index),
			         RefreshedSpheres[Index].Location.Equals(RebuiltSpheres[Index].Location, 0.0001f));
			TestTrue(FString::Printf(TEXT("%s sphere %d rotation"), CaseName, Index),
			         RefreshedSpheres[Index].Rotation.Equals(RebuiltSpheres[Index].Rotation, 0.0001f));
			TestTrue(FString::Printf(TEXT("%s sphere %d radius"), CaseName, Index),
			         FMath::IsNearlyEqual(RefreshedSpheres[Index].Radius, RebuiltSpheres[Index].Radius, 0.0001f));
		}

		TestEqual(FString::Printf(TEXT("%s capsule count"), CaseName), RefreshedCapsules.Num(), RebuiltCapsules.Num());
		for (int32 Index = 0; Index < RebuiltCapsules.Num() && Index < RefreshedCapsules.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s capsule %d location"), CaseName, Index),
			         RefreshedCapsules[Index].Location.Equals(RebuiltCapsules[Index].Location, 0.0001f));
			TestTrue(FString::Printf(TEXT("%s capsule %d rotation"), CaseName, Index),
			         RefreshedCapsules[Index].Rotation.Equals(RebuiltCapsules[Index].Rotation, 0.0001f));
			TestTrue(FString::Printf(TEXT("%s capsule %d radius"), CaseName, Index),
			         FMath::IsNearlyEqual(RefreshedCapsules[Index].Radius, RebuiltCapsules[Index].Radius, 0.0001f));
		}

		TestEqual(FString::Printf(TEXT("%s tapered count"), CaseName),
		          RefreshedTaperedCapsules.Num(), RebuiltTaperedCapsules.Num());

		TestEqual(FString::Printf(TEXT("%s box count"), CaseName), RefreshedBoxes.Num(), RebuiltBoxes.Num());
		for (int32 Index = 0; Index < RebuiltBoxes.Num() && Index < RefreshedBoxes.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s box %d location"), CaseName, Index),
			         RefreshedBoxes[Index].Location.Equals(RebuiltBoxes[Index].Location, 0.0001f));
			TestTrue(FString::Printf(TEXT("%s box %d rotation"), CaseName, Index),
			         RefreshedBoxes[Index].Rotation.Equals(RebuiltBoxes[Index].Rotation, 0.0001f));
			TestTrue(FString::Printf(TEXT("%s box %d extent"), CaseName, Index),
			         RefreshedBoxes[Index].Extent.Equals(RebuiltBoxes[Index].Extent, 0.0001f));
		}

		TestEqual(FString::Printf(TEXT("%s convex count"), CaseName), RefreshedConvexes.Num(), RebuiltConvexes.Num());
		for (int32 Index = 0; Index < RebuiltConvexes.Num() && Index < RefreshedConvexes.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s convex %d location"), CaseName, Index),
			         RefreshedConvexes[Index].Location.Equals(RebuiltConvexes[Index].Location, 0.0001f));
			TestTrue(FString::Printf(TEXT("%s convex %d rotation"), CaseName, Index),
			         RefreshedConvexes[Index].Rotation.Equals(RebuiltConvexes[Index].Rotation, 0.0001f));
			TestEqual(FString::Printf(TEXT("%s convex %d plane count"), CaseName, Index),
			          RefreshedConvexes[Index].LocalPlanes.Num(), RebuiltConvexes[Index].LocalPlanes.Num());
		}

		TestEqual(FString::Printf(TEXT("%s ground box count"), CaseName),
		          RefreshedGroundBoxes.Num(), RebuiltGroundBoxes.Num());
		for (int32 Index = 0; Index < RebuiltGroundBoxes.Num() && Index < RefreshedGroundBoxes.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s ground box %d location"), CaseName, Index),
			         RefreshedGroundBoxes[Index].Location.Equals(RebuiltGroundBoxes[Index].Location, 0.0001f));
			TestTrue(FString::Printf(TEXT("%s ground box %d rotation"), CaseName, Index),
			         RefreshedGroundBoxes[Index].Rotation.Equals(RebuiltGroundBoxes[Index].Rotation, 0.0001f));
			TestTrue(FString::Printf(TEXT("%s ground box %d extent"), CaseName, Index),
			         RefreshedGroundBoxes[Index].Extent.Equals(RebuiltGroundBoxes[Index].Extent, 0.0001f));
		}
	};

	RunCase(EKawaiiPhysicsSimulationSpace::ComponentSpace, TEXT("ComponentSpace"));
	RunCase(EKawaiiPhysicsSimulationSpace::WorldSpace, TEXT("WorldSpace"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSharedReaderConsumesInjectedStateTest,
                                 "KawaiiPhysics.SimpleWorld.SharedReaderConsumesInjectedState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSharedReaderConsumesInjectedStateTest::RunTest(const FString& Parameters)
{
	USkeletalMeshComponent* SkelCompA =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* SkelCompB =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry =
		MakeSimpleWorldReaderEntry(SkelCompA, SkelCompB);

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetSimpleWorldOwnSkelComp(SkelCompA);
	Accessor.InjectSharedPublisherState(MakeSimpleWorldReaderState(false), Entry);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestTrue(TEXT("Injected shared reader mode is enabled"), Accessor.IsSimpleWorldReaderMode());
	TestEqual(TEXT("Reader includes one main box"), Accessor.GetSimpleWorldBoxLimits().Num(), 1);
	TestEqual(TEXT("Reader excludes own member sphere"), Accessor.GetSimpleWorldSphericalLimits().Num(), 0);
	TestEqual(TEXT("Reader includes the other member capsule"), Accessor.GetSimpleWorldCapsuleLimits().Num(), 1);
	TestEqual(TEXT("Reader includes one ground box"), Accessor.GetSimpleWorldGroundBoxLimits().Num(), 1);
	TestEqual(TEXT("Reader collider count includes shape and ground"), Accessor.GetNumSimpleWorldColliders(), 3);

	const uint64 FirstShapeSerial = Accessor.GetLastReadSimpleWorldShapeSerial();
	const uint64 FirstMemberSerialSum = Accessor.GetLastReadSimpleWorldMemberSerialSum();
	FVector FirstBoxLocation = FVector::ZeroVector;
	FVector FirstCapsuleLocation = FVector::ZeroVector;
	FVector FirstGroundLocation = FVector::ZeroVector;
	const bool bHasInitialReaderShapes =
		Accessor.GetSimpleWorldBoxLimits().IsValidIndex(0)
		&& Accessor.GetSimpleWorldCapsuleLimits().IsValidIndex(0)
		&& Accessor.GetSimpleWorldGroundBoxLimits().IsValidIndex(0);
	if (bHasInitialReaderShapes)
	{
		FirstBoxLocation = Accessor.GetSimpleWorldBoxLimits()[0].Location;
		FirstCapsuleLocation = Accessor.GetSimpleWorldCapsuleLimits()[0].Location;
		FirstGroundLocation = Accessor.GetSimpleWorldGroundBoxLimits()[0].Location;
	}

	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestEqual(TEXT("Unchanged shape serial stays cached"),
	          Accessor.GetLastReadSimpleWorldShapeSerial(), FirstShapeSerial);
	TestEqual(TEXT("Unchanged member serial sum stays cached"),
	          Accessor.GetLastReadSimpleWorldMemberSerialSum(), FirstMemberSerialSum);
	if (bHasInitialReaderShapes
		&& Accessor.GetSimpleWorldBoxLimits().IsValidIndex(0)
		&& Accessor.GetSimpleWorldCapsuleLimits().IsValidIndex(0)
		&& Accessor.GetSimpleWorldGroundBoxLimits().IsValidIndex(0))
	{
		TestTrue(TEXT("In-place refresh keeps box location"),
		         Accessor.GetSimpleWorldBoxLimits()[0].Location.Equals(FirstBoxLocation, GSimpleWorldTol));
		TestTrue(TEXT("In-place refresh keeps capsule location"),
		         Accessor.GetSimpleWorldCapsuleLimits()[0].Location.Equals(FirstCapsuleLocation, GSimpleWorldTol));
		TestTrue(TEXT("In-place refresh keeps ground location"),
		         Accessor.GetSimpleWorldGroundBoxLimits()[0].Location.Equals(FirstGroundLocation, GSimpleWorldTol));
	}

	PublishSimpleWorldReaderMemberBExtraSphere(*Entry, SkelCompB);
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestEqual(TEXT("Member serial change rebuilds and includes the new sphere"),
	          Accessor.GetSimpleWorldSphericalLimits().Num(), 1);
	TestEqual(TEXT("Capsule remains after member rebuild"), Accessor.GetSimpleWorldCapsuleLimits().Num(), 1);
	TestEqual(TEXT("Reader collider count includes rebuilt member sphere"),
	          Accessor.GetNumSimpleWorldColliders(), 4);
	TestTrue(TEXT("Member serial sum changed"),
	         Accessor.GetLastReadSimpleWorldMemberSerialSum() != FirstMemberSerialSum);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldAutoSourceFollowsProviderTest,
                                 "KawaiiPhysics.SimpleWorld.AutoSourceFollowsProvider",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldAutoSourceFollowsProviderTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderID = 0xFFFF1002;
	USkeletalMeshComponent* SkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> LocalEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SharedEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetSimpleWorldOwnSkelComp(SkelComp);
	Accessor.SetSimpleWorldCollisionSharedTag(TAG_KawaiiPhysicsSimpleWorldRegistryX);
	Accessor.SetSimpleWorldLocalEntryForAuto(LocalEntry);
	Accessor.SetSimpleWorldSharedEntryForAuto(SharedEntry);
	Accessor.SetSimpleWorldCollisionSource(EKawaiiPhysicsSimpleWorldCollisionSource::Auto);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	Accessor.InitializeSimpleWorldCollision();
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestEqual(TEXT("Auto resolves to Local without a provider"),
	          Accessor.GetSimpleWorldResolvedSource(), EKawaiiPhysicsSimpleWorldCollisionSource::Local);
	TestFalse(TEXT("Auto Local is not reader mode"), Accessor.IsSimpleWorldReaderMode());

	FKawaiiPhysicsSharedPublisherState State = MakeSimpleWorldReaderState(false);
	SharedEntry->SetDesc(ProviderID, State.SimpleWorldDesc, GFrameCounter,
	                     TWeakObjectPtr<const USkeletalMeshComponent>(), true);

	const int32 AutoResolveInterval = FMath::Max(1, GetKawaiiPhysicsSharedPublisherAutoResolveInterval());
	for (int32 FrameIndex = 0; FrameIndex < AutoResolveInterval + 1; ++FrameIndex)
	{
		Accessor.InitializeSimpleWorldCollision();
		Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	}

	TestEqual(TEXT("Auto switches to Shared when provider appears"),
	          Accessor.GetSimpleWorldResolvedSource(), EKawaiiPhysicsSimpleWorldCollisionSource::Shared);
	TestTrue(TEXT("Auto Shared uses reader mode"), Accessor.IsSimpleWorldReaderMode());

	SharedEntry->RemoveDesc(ProviderID);
	Accessor.InitializeSimpleWorldCollision();
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	Accessor.InitializeSimpleWorldCollision();
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);

	TestEqual(TEXT("Auto falls back to Local when provider disappears"),
	          Accessor.GetSimpleWorldResolvedSource(), EKawaiiPhysicsSimpleWorldCollisionSource::Local);
	TestFalse(TEXT("Auto fallback leaves reader mode"), Accessor.IsSimpleWorldReaderMode());
	TestFalse(TEXT("Auto fallback does not log a reader warning"), Accessor.IsSimpleWorldReaderWarningLogged());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldThrottledReaderDetectsPinChangeTest,
                                 "KawaiiPhysics.SimpleWorld.ThrottledReaderDetectsPinChange",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldThrottledReaderDetectsPinChangeTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderID = 0xFFFF1006;
	constexpr int32 NumNoChangeEvaluations = 1;

	IConsoleVariable* RetryThresholdCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.InitRetryThreshold"));
	IConsoleVariable* ThrottleIntervalCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.InitRetryThrottleInterval"));
	if (!TestNotNull(TEXT("Init retry threshold CVar exists"), RetryThresholdCVar)
		|| !TestNotNull(TEXT("Init retry throttle interval CVar exists"), ThrottleIntervalCVar))
	{
		return false;
	}
	const int32 RetryThreshold = FMath::Max(1, RetryThresholdCVar->GetInt());
	const int32 ThrottleInterval = FMath::Max(1, ThrottleIntervalCVar->GetInt());

	// provider 不在の間 RetryCount は 1 評価につき 1 増えるので、警告しきい値を 1 評価だけ越えたところで止めると
	// 以後のゲート（RetryCount % ThrottleInterval == 0）は閉じたままになる。
	const int32 NumThrottleEvaluations = RetryThreshold + 1;
	if (!TestTrue(TEXT("Throttle keeps the initialize gate closed after the warning"),
	              ThrottleInterval > 1
	              && (NumThrottleEvaluations % ThrottleInterval) != 0
	              && ((NumThrottleEvaluations + NumNoChangeEvaluations) % ThrottleInterval) != 0))
	{
		return false;
	}

	// provider 待ちの警告は Source 変更用と Shared Tag 変更用のノードで 1 回ずつ出る。
	AddExpectedError(TEXT("Shared Simple World Collision entry has no provider"),
	                 EAutomationExpectedErrorFlags::Contains, 2);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	// Source ピンの変更（provider 不在の Shared → Local）。
	{
		UKawaiiPhysicsSharedCollisionSubsystem* Subsystem = NewObject<UKawaiiPhysicsSharedCollisionSubsystem>();
		USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());
		const FKawaiiPhysicsSimpleWorldRegistryKey LocalKey =
			FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(SkelComp);

		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		Accessor.SetSimpleWorldOwnSkelComp(SkelComp);
		Accessor.SetSimpleWorldSubsystem(Subsystem);
		Accessor.SetSimpleWorldCollisionSharedTag(TAG_KawaiiPhysicsSimpleWorldRegistryX);
		Accessor.SetSimpleWorldCollisionSource(EKawaiiPhysicsSimpleWorldCollisionSource::Shared);

		// family root も provider も居ないので Shared は reader として解決されたまま Entry を掴めず、再試行スロットルに入る。
		for (int32 EvaluationIndex = 0; EvaluationIndex < NumThrottleEvaluations; ++EvaluationIndex)
		{
			Accessor.EvaluateSimpleWorldCollision(PoseContext);
		}
		TestFalse(TEXT("Throttled reader is not initialized"), Accessor.IsSimpleWorldCollisionInitialized());
		TestTrue(TEXT("Throttled reader keeps reader mode"), Accessor.IsSimpleWorldReaderMode());
		TestTrue(TEXT("Throttled reader logs the no-provider warning"),
		         Accessor.IsSimpleWorldReaderWarningLogged());
		TestFalse(TEXT("Throttled reader holds no entry"), Accessor.HasSimpleWorldEntry());

		// 設定が変わらない評価では初期化を試みない（スロットルが効いていること＝誤検知していないこと）。
		const int32 AttemptsWhileThrottled = Accessor.GetNumSimpleWorldInitializeAttempts();
		for (int32 EvaluationIndex = 0; EvaluationIndex < NumNoChangeEvaluations; ++EvaluationIndex)
		{
			Accessor.EvaluateSimpleWorldCollision(PoseContext);
		}
		TestEqual(TEXT("Throttled reader skips initialization while nothing changes"),
		          Accessor.GetNumSimpleWorldInitializeAttempts(), AttemptsWhileThrottled);

		// setter を通さず pin 相当の直接代入で Source を Local へ変える。
		// 初期化ゲートは閉じたままだが Update 冒頭の検知が走り、同じ評価の中で Local provider として解決し直される。
		// 検知条件が bSimpleWorldCollisionInitialized のみだった頃は未初期化の reader が検知されず、
		// RetryCount が ThrottleInterval の倍数に達するまで（既定で最大 59 評価）初期化されなかった。
		Accessor.Node.SimpleWorldCollisionSource = EKawaiiPhysicsSimpleWorldCollisionSource::Local;
		Accessor.EvaluateSimpleWorldCollision(PoseContext);
		TestTrue(TEXT("Throttled reader initializes as Local after the source pin change"),
		         Accessor.IsSimpleWorldCollisionInitialized());
		TestFalse(TEXT("Local source leaves reader mode"), Accessor.IsSimpleWorldReaderMode());
		const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> LocalEntry = Subsystem->FindSimpleWorldEntry(LocalKey);
		if (!TestTrue(TEXT("Local initialization creates registry entry"), LocalEntry.IsValid()))
		{
			return false;
		}
		TestTrue(TEXT("Local initialization registers provider"), LocalEntry->HasAnyDesc());
	}

	// Shared Tag ピンの変更（provider 不在の X → provider 在籍の Y）。
	{
		UKawaiiPhysicsSharedCollisionSubsystem* Subsystem = NewObject<UKawaiiPhysicsSharedCollisionSubsystem>();
		USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());

		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		Accessor.SetSimpleWorldOwnSkelComp(SkelComp);
		Accessor.SetSimpleWorldSubsystem(Subsystem);
		Accessor.SetSimpleWorldCollisionSharedTag(TAG_KawaiiPhysicsSimpleWorldRegistryX);
		Accessor.SetSimpleWorldCollisionSource(EKawaiiPhysicsSimpleWorldCollisionSource::Shared);

		for (int32 EvaluationIndex = 0; EvaluationIndex < NumThrottleEvaluations; ++EvaluationIndex)
		{
			Accessor.EvaluateSimpleWorldCollision(PoseContext);
		}
		TestFalse(TEXT("Throttled tag reader is not initialized"), Accessor.IsSimpleWorldCollisionInitialized());
		TestTrue(TEXT("Throttled tag reader keeps reader mode"), Accessor.IsSimpleWorldReaderMode());

		const int32 AttemptsWhileThrottled = Accessor.GetNumSimpleWorldInitializeAttempts();
		for (int32 EvaluationIndex = 0; EvaluationIndex < NumNoChangeEvaluations; ++EvaluationIndex)
		{
			Accessor.EvaluateSimpleWorldCollision(PoseContext);
		}
		TestEqual(TEXT("Throttled tag reader skips initialization while nothing changes"),
		          Accessor.GetNumSimpleWorldInitializeAttempts(), AttemptsWhileThrottled);

		// provider 付きの Entry を用意してから、pin 相当の直接代入で Shared Tag を切り替える。
		const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SharedEntry =
			MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		const FKawaiiPhysicsSharedPublisherState State = MakeSimpleWorldReaderState(false);
		SharedEntry->SetDesc(ProviderID, State.SimpleWorldDesc, GFrameCounter,
		                     TWeakObjectPtr<const USkeletalMeshComponent>(), true);
		Accessor.SetSimpleWorldSharedEntryForAuto(SharedEntry);

		// Source 変更と同じく、検知が走った評価の中で新しい Tag の reader として初期化される。
		Accessor.Node.SimpleWorldCollisionSharedTag = TAG_KawaiiPhysicsSimpleWorldRegistryY;
		Accessor.EvaluateSimpleWorldCollision(PoseContext);
		TestTrue(TEXT("Throttled reader rebinds after the shared tag pin change"),
		         Accessor.IsSimpleWorldCollisionInitialized());
		TestTrue(TEXT("Rebound node stays in reader mode"), Accessor.IsSimpleWorldReaderMode());
		TestEqual(TEXT("Rebound node resolves to Shared"),
		          Accessor.GetSimpleWorldResolvedSource(), EKawaiiPhysicsSimpleWorldCollisionSource::Shared);
		TestTrue(TEXT("Rebound reader registers on the shared entry"), SharedEntry->HasAnyReader());
		// provider を掴み直した評価で再試行スロットルも解除される。
		TestEqual(TEXT("Shared Tag pin change clears the reader retry throttle"),
		          Accessor.GetSimpleWorldReaderRetryCount(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldRetiredEntryRejectsRegistrationTest,
                                 "KawaiiPhysics.SimpleWorld.RetiredEntryRejectsRegistration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldRetiredEntryRejectsRegistrationTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID = 0xA001;
	constexpr uint64 ReaderID = 0xA002;
	constexpr uint64 Frame = 100;
	const TWeakObjectPtr<const USkeletalMeshComponent> SkelComp;
	const FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	TestTrue(TEXT("Provider registration succeeds"), Entry->SetDesc(SourceID, Desc, Frame, SkelComp));
	TestFalse(TEXT("Provider prevents retirement"), Entry->MarkRetiredIfEmpty());
	TestFalse(TEXT("Entry with provider stays live"), Entry->IsRetired());
	Entry->RemoveDesc(SourceID);
	TestTrue(TEXT("Empty entry retires"), Entry->MarkRetiredIfEmpty());
	TestTrue(TEXT("Retired state is visible"), Entry->IsRetired());
	TestTrue(TEXT("Retirement is idempotent"), Entry->MarkRetiredIfEmpty());

	const auto TestRejectedRegistration = [&](const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>& RetiredEntry)
	{
		TestFalse(TEXT("Retired entry rejects explicit-frame desc"), RetiredEntry->SetDesc(SourceID, Desc, Frame + 1, SkelComp));
		TestFalse(TEXT("Retired entry rejects implicit-frame desc"), RetiredEntry->SetDesc(SourceID, Desc));
		TestFalse(TEXT("Retired entry rejects reader registration"), RetiredEntry->AddReaderMember(ReaderID, SkelComp, Frame + 1));
		TestFalse(TEXT("Retired entry rejects provider heartbeat"), RetiredEntry->MarkRead(SourceID));
		TestFalse(TEXT("Retired entry rejects explicit-frame heartbeat"), RetiredEntry->MarkRead(SourceID, Frame + 1));
		TestFalse(TEXT("Retired entry rejects reader heartbeat"), RetiredEntry->MarkReaderRead(ReaderID, Frame + 1, 60));
	};
	TestRejectedRegistration(Entry);
	TestFalse(TEXT("Rejected registrations leave no providers"), Entry->HasAnyDesc());
	TestFalse(TEXT("Rejected registrations leave no readers"), Entry->HasAnyReader());

	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> OccupiedEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	TestTrue(TEXT("Reader registration succeeds"), OccupiedEntry->AddReaderMember(ReaderID, SkelComp, Frame));
	TestFalse(TEXT("Reader alone prevents retirement"), OccupiedEntry->MarkRetiredIfEmpty());
	TestFalse(TEXT("Entry with reader stays live"), OccupiedEntry->IsRetired());
	TestTrue(TEXT("Second provider registration succeeds"), OccupiedEntry->SetDesc(SourceID, Desc, Frame, SkelComp));
	OccupiedEntry->MarkRetired();
	TestTrue(TEXT("Forced retirement works with slots present"), OccupiedEntry->IsRetired());
	TestTrue(TEXT("Already retired occupied entry stays retired"), OccupiedEntry->MarkRetiredIfEmpty());
	TestRejectedRegistration(OccupiedEntry);
	TestEqual(TEXT("Rejected heartbeat preserves last provider frame"), OccupiedEntry->GetLastProviderFrame(), Frame);
	TestEqual(TEXT("Forced retirement preserves provider slots"), OccupiedEntry->GetNumDescs(), 1);
	TestEqual(TEXT("Forced retirement preserves reader slots"), OccupiedEntry->GetNumReaders(), 1);
	OccupiedEntry->RemoveDesc(SourceID);
	OccupiedEntry->RemoveReaderMember(ReaderID);
	TestFalse(TEXT("Retired provider can still be released"), OccupiedEntry->HasAnyDesc());
	TestFalse(TEXT("Retired reader can still be released"), OccupiedEntry->HasAnyReader());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldDormantLocalNodeRebindsAfterRegistryCleanupTest,
                                 "KawaiiPhysics.SimpleWorld.DormantLocalNodeRebindsAfterRegistryCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldDormantLocalNodeRebindsAfterRegistryCleanupTest::RunTest(const FString& Parameters)
{
	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem = NewObject<UKawaiiPhysicsSharedCollisionSubsystem>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	const FKawaiiPhysicsSimpleWorldRegistryKey Key = FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(SkelComp);
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetSimpleWorldOwnSkelComp(SkelComp);
	Accessor.SetSimpleWorldSubsystem(Subsystem);
	Accessor.SetSimpleWorldCollisionSource(EKawaiiPhysicsSimpleWorldCollisionSource::Local);
	Accessor.InitializeSimpleWorldCollision();
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> OldEntry = Subsystem->FindSimpleWorldEntry(Key);
	if (!TestTrue(TEXT("Local initialization creates registry entry"), OldEntry.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("Local initialization registers provider"), OldEntry->HasAnyDesc());
	FKawaiiPhysicsSharedCollisionData PublishedShapes;
	FSphericalLimit Sphere;
	Sphere.Radius = 10.0f;
	PublishedShapes.SphericalLimits.Add(Sphere);
	OldEntry->Slot.Publish(PublishedShapes);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestTrue(TEXT("Local node reads published shapes before dormancy"), Accessor.GetSimpleWorldSphericalLimits().Num() > 0);
	TestTrue(TEXT("Local node caches the published serial"), Accessor.GetLastReadSimpleWorldShapeSerial() > 0);
	// 古い収集結果が各配列に残った状態で休止し、実際の Registry 掃除を通す。
	Accessor.SetSimpleWorldLimits({FSphericalLimit()}, {FCapsuleLimit()}, {FTaperedCapsuleLimit()},
		{FBoxLimit()}, {FKawaiiPhysicsConvexLimit()}, {FBoxLimit()});
	TestTrue(TEXT("Simulation arrays contain stale shapes"), Accessor.GetNumSimpleWorldColliders() > 0);
	OldEntry->RemoveExpiredDescs(GFrameCounter + 61, 60);
	IConsoleVariable* CleanupInterval = IConsoleManager::Get().FindConsoleVariable(TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.CleanupInterval"));
	if (!TestNotNull(TEXT("Cleanup interval CVar exists"), CleanupInterval))
	{
		return false;
	}
	Subsystem->Tick(FMath::Max(0.0f, CleanupInterval->GetFloat()) + 1.0f);
	TestTrue(TEXT("Registry cleanup retires the old entry"), OldEntry->IsRetired());
	TestFalse(TEXT("Registry no longer contains old entry"), Subsystem->FindSimpleWorldEntry(Key).IsValid());

	Accessor.Node.SimpleWorldCollisionGatherInterval += 0.25f;
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestFalse(TEXT("Resumed local node releases retired entry"), Accessor.HasSimpleWorldEntry());
	TestFalse(TEXT("Resumed local node requests initialization"), Accessor.IsSimpleWorldCollisionInitialized());
	TestEqual(TEXT("Resumed local node clears simulation shapes"), Accessor.GetNumSimpleWorldColliders(), 0);
	TestEqual(TEXT("Shape serial cleared"), Accessor.GetLastReadSimpleWorldShapeSerial(), static_cast<uint64>(0));

	Accessor.InitializeSimpleWorldCollision();
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> NewEntry = Subsystem->FindSimpleWorldEntry(Key);
	if (!TestTrue(TEXT("Next initialization creates a replacement"), NewEntry.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("Replacement differs from retired entry"), NewEntry != OldEntry);
	TestFalse(TEXT("Replacement is live"), NewEntry->IsRetired());
	TestTrue(TEXT("Local node is initialized again"), Accessor.IsSimpleWorldCollisionInitialized());
	TestTrue(TEXT("Replacement accepts local provider heartbeat"), NewEntry->MarkRead(reinterpret_cast<uint64>(&Accessor.Node)));
	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	TestTrue(TEXT("Replacement holds provider desc"), NewEntry->BuildMergedDesc(Desc));
	TestEqual(TEXT("Replacement keeps changed gather interval"), Desc.GatherIntervalSec, Accessor.Node.SimpleWorldCollisionGatherInterval);
	TestTrue(TEXT("Old entry remains retired"), OldEntry->IsRetired());
	TestFalse(TEXT("Old entry still has no providers"), OldEntry->HasAnyDesc());
	TestFalse(TEXT("Old entry still has no readers"), OldEntry->HasAnyReader());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldLocalProviderKeepsSkelCompAfterExpiryTest,
                                 "KawaiiPhysics.SimpleWorld.LocalProviderKeepsSkelCompAfterExpiry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldLocalProviderKeepsSkelCompAfterExpiryTest::RunTest(const FString& Parameters)
{
	USkeletalMeshComponent* SkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetSimpleWorldOwnSkelComp(SkelComp);
	Accessor.SetSimpleWorldEntry(Entry);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestFalse(TEXT("Local provider stays out of reader mode"), Accessor.IsSimpleWorldReaderMode());
	TestTrue(TEXT("Local provider registers a provider desc"), Entry->HasProviderDesc());
	TestTrue(TEXT("Local provider slot carries the own skeletal mesh component"),
	         Entry->GetPrimarySkelComp() == SkelComp);

	// 収集 Tick が長く止まった状況を模し、provider slot を期限切れで落とす。
	Entry->RemoveExpiredDescs(GFrameCounter + 1000, 10);
	TestFalse(TEXT("Expired provider desc is removed"), Entry->HasProviderDesc());
	TestTrue(TEXT("Expired provider slot drops the skeletal mesh component"),
	         Entry->GetPrimarySkelComp() == nullptr);

	// Desc が同値だと再送されず MarkRead 失敗で Entry が解放されるため、Desc を変えて再送経路を通す。
	Accessor.SetSimpleWorldGroundCollision(!Accessor.GetSimpleWorldGroundCollision());
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestTrue(TEXT("Changed desc recreates the provider slot"), Entry->HasProviderDesc());
	TestTrue(TEXT("Recreated provider slot keeps SkelComp"), Entry->GetPrimarySkelComp() == SkelComp);
	TestTrue(TEXT("Local provider keeps the cached entry"), Accessor.HasSimpleWorldEntry());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSharedReaderClearsWhenProviderDisabledTest,
                                 "KawaiiPhysics.SimpleWorld.SharedReaderClearsWhenProviderDisabled",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSharedReaderClearsWhenProviderDisabledTest::RunTest(const FString& Parameters)
{
	USkeletalMeshComponent* SkelCompA =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* SkelCompB =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry =
		MakeSimpleWorldReaderEntry(SkelCompA, SkelCompB);

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetSimpleWorldOwnSkelComp(SkelCompA);
	Accessor.InjectSharedPublisherState(MakeSimpleWorldReaderState(false), Entry);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestEqual(TEXT("Reader starts with injected colliders"), Accessor.GetNumSimpleWorldColliders(), 3);

	Accessor.InjectSharedPublisherState(MakeSimpleWorldReaderState(true), Entry);
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestEqual(TEXT("Disabled provider clears colliders"), Accessor.GetNumSimpleWorldColliders(), 0);
	TestFalse(TEXT("Disabled provider does not log reader warning"), Accessor.IsSimpleWorldReaderWarningLogged());
	TestEqual(TEXT("Disabled provider does not increment reader retry"),
	          Accessor.GetSimpleWorldReaderRetryCount(), 0);

	Accessor.InjectSharedPublisherState(MakeSimpleWorldReaderState(false), Entry);
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestEqual(TEXT("Reader colliders return when provider is enabled again"),
	          Accessor.GetNumSimpleWorldColliders(), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSharedReaderReleasesWhenProviderGoneTest,
                                 "KawaiiPhysics.SimpleWorld.SharedReaderReleasesWhenProviderGone",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSharedReaderReleasesWhenProviderGoneTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderID = 0xFFFF0001;
	USkeletalMeshComponent* SkelCompA =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* SkelCompB =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry =
		MakeSimpleWorldReaderEntry(SkelCompA, SkelCompB);

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetSimpleWorldOwnSkelComp(SkelCompA);
	Accessor.InjectSharedPublisherState(MakeSimpleWorldReaderState(false), Entry, ProviderID);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestEqual(TEXT("Reader starts with injected colliders"), Accessor.GetNumSimpleWorldColliders(), 3);

	Entry->RemoveDesc(ProviderID);
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestFalse(TEXT("Reader releases the cached entry when provider disappears"), Accessor.HasSimpleWorldEntry());
	TestEqual(TEXT("Reader clears colliders after provider disappears"),
	          Accessor.GetNumSimpleWorldColliders(), 0);
	TestEqual(TEXT("Reader increments retry after release"), Accessor.GetSimpleWorldReaderRetryCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldMissingProviderInitIsThrottledTest,
                                 "KawaiiPhysics.SimpleWorld.MissingProviderInitIsThrottled",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldMissingProviderInitIsThrottledTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderID = 0xFFFF1004;
	constexpr int32 NumEvaluations = 130;

	IConsoleVariable* RetryThresholdCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.InitRetryThreshold"));
	IConsoleVariable* ThrottleIntervalCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.InitRetryThrottleInterval"));
	if (!TestNotNull(TEXT("Init retry threshold CVar exists"), RetryThresholdCVar)
		|| !TestNotNull(TEXT("Init retry throttle interval CVar exists"), ThrottleIntervalCVar))
	{
		return false;
	}

	const int32 ThrottleInterval = FMath::Max(1, ThrottleIntervalCVar->GetInt());

	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem = NewObject<UKawaiiPhysicsSharedCollisionSubsystem>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());

	// provider が居ない Shared キーへ reader を向ける。Source は既定の Local のままで、注入 reader キー経路
	// （ResolveSimpleWorldCollisionSource の bInjectedReaderKey）により Shared として解決される。
	FKawaiiPhysicsSimpleWorldRegistryKey ReaderKey;
	ReaderKey.KeyObject = GetTransientPackage();
	ReaderKey.Tag = TAG_KawaiiPhysicsSimpleWorldRegistryX;

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetSimpleWorldOwnSkelComp(SkelComp);
	Accessor.SetSimpleWorldSubsystem(Subsystem);
	Accessor.SetSimpleWorldReaderKey(ReaderKey);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	AddExpectedError(TEXT("Shared Simple World Collision entry has no provider"),
	                 EAutomationExpectedErrorFlags::Contains, 1);
	for (int32 EvaluationIndex = 0; EvaluationIndex < NumEvaluations; ++EvaluationIndex)
	{
		Accessor.EvaluateSimpleWorldCollision(PoseContext);
	}

	TestTrue(TEXT("Reader mode survives the release"), Accessor.IsSimpleWorldReaderMode());
	TestTrue(TEXT("Missing provider logs the reader warning once"), Accessor.IsSimpleWorldReaderWarningLogged());
	TestTrue(TEXT("Missing provider initialization is throttled"),
	          Accessor.GetNumSimpleWorldInitializeAttempts() < NumEvaluations);
	TestFalse(TEXT("Throttled reader holds no entry"), Accessor.HasSimpleWorldEntry());

	// provider が現れたら、遅くとも ThrottleInterval 評価以内に接続が戻る。
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> ProviderEntry = Subsystem->FindOrCreateSimpleWorldEntry(
		ReaderKey, ProviderID, FKawaiiPhysicsSimpleWorldCollisionDesc(),
		TWeakObjectPtr<const USkeletalMeshComponent>(SkelComp), true);
	if (!TestTrue(TEXT("Provider registration reuses the shared entry"), ProviderEntry.IsValid()))
	{
		return false;
	}
	for (int32 EvaluationIndex = 0; EvaluationIndex < ThrottleInterval; ++EvaluationIndex)
	{
		Accessor.EvaluateSimpleWorldCollision(PoseContext);
		if (Accessor.GetSimpleWorldReaderRetryCount() == 0)
		{
			break;
		}
	}
	TestTrue(TEXT("Reader rebinds to the entry once a provider appears"), Accessor.HasSimpleWorldEntry());
	TestEqual(TEXT("Reader retry resets after a successful read"), Accessor.GetSimpleWorldReaderRetryCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldRadiusWarningOnceTest,
                                 "KawaiiPhysics.SimpleWorld.RadiusWarningOnce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldRadiusWarningOnceTest::RunTest(const FString& Parameters)
{
	// 半径チェックの警告・保留上限・再武装と reader の除外を確認する。
	{
		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		Accessor.SetSimpleWorldGatherRadiusOverride(1.0f);

		FAnimInstanceProxy AnimInstanceProxy;
		FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

		Accessor.BuildVerticalChain(1, 10.0f);
		Accessor.CheckSimpleWorldGatherRadius(PoseContext);
		TestFalse(TEXT("Zero pose frame does not mark radius check done"), Accessor.IsSimpleWorldRadiusChecked());

		Accessor.BuildVerticalChain(2, 10.0f);
		Accessor.SetSimpleWorldGatherRadiusOverride(1.0f);
		AddExpectedError(TEXT("SimpleWorldCollision: GatherRadius"), EAutomationExpectedErrorFlags::Contains, 1);

		Accessor.CheckSimpleWorldGatherRadius(PoseContext);
		TestTrue(TEXT("First non-zero pose frame marks radius check done"), Accessor.IsSimpleWorldRadiusChecked());

		Accessor.CheckSimpleWorldGatherRadius(PoseContext);
		TestTrue(TEXT("Second radius check call remains done"), Accessor.IsSimpleWorldRadiusChecked());
	}

	// ゼロ姿勢の保留上限と Override 変更後の再武装を確認する。
	{
		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		Accessor.BuildVerticalChain(1, 10.0f);
		Accessor.SetSimpleWorldGatherRadiusOverride(1.0f);

		FAnimInstanceProxy AnimInstanceProxy;
		FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

		// 全ボーンがゼロ姿勢のまま呼び続けても、上限回数で完了扱いになり走査が止まる（警告は出ない）。
		for (uint8 Attempt = 1; Attempt < FAnimNode_KawaiiPhysics::MaxSimpleWorldRadiusCheckDeferrals; ++Attempt)
		{
			Accessor.CheckSimpleWorldGatherRadius(PoseContext);
			TestFalse(FString::Printf(TEXT("Deferral %d keeps the check pending"), Attempt), Accessor.IsSimpleWorldRadiusChecked());
		}

		Accessor.CheckSimpleWorldGatherRadius(PoseContext);
		TestTrue(TEXT("Reaching the deferral cap marks the check done"), Accessor.IsSimpleWorldRadiusChecked());

		Accessor.CheckSimpleWorldGatherRadius(PoseContext);
		TestEqual(TEXT("Counter stops growing once the check is done"),
		          static_cast<int32>(Accessor.GetSimpleWorldRadiusCheckDeferrals()),
		          static_cast<int32>(FAnimNode_KawaiiPhysics::MaxSimpleWorldRadiusCheckDeferrals));

		// 半径 Override を変えると持ち越しカウンタもリセットされる。
		Accessor.SetSimpleWorldGatherRadiusOverride(2.0f);
		TestFalse(TEXT("Radius override change re-arms the check"), Accessor.IsSimpleWorldRadiusChecked());
		TestEqual(TEXT("Radius override change resets the deferral counter"),
		          static_cast<int32>(Accessor.GetSimpleWorldRadiusCheckDeferrals()), 0);

	}

	// 共有 reader には半径チェックを適用しない。
	{
		USkeletalMeshComponent* SkelCompA =
			NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		USkeletalMeshComponent* SkelCompB =
			NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry =
			MakeSimpleWorldReaderEntry(SkelCompA, SkelCompB);

		FKawaiiPhysicsTestAccessor Accessor;
		Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		Accessor.BuildVerticalChain(2, 10.0f);
		Accessor.SetSimpleWorldGatherRadiusOverride(1.0f);
		Accessor.SetSimpleWorldOwnSkelComp(SkelCompA);
		Accessor.InjectSharedPublisherState(MakeSimpleWorldReaderState(false), Entry);

		FAnimInstanceProxy AnimInstanceProxy;
		FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

		Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
		TestFalse(TEXT("Shared reader path does not run SimpleWorld radius check"),
		          Accessor.IsSimpleWorldRadiusChecked());

	}

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
	TArray<FKawaiiPhysicsConvexLimit> EmptyConvexes;

	// bUseSimpleWorldCollision = true: SimpleWorld配列のSphereに押し出される
	{
		FKawaiiPhysicsTestAccessor A;
		BuildChain(A);
		A.SetSimpleWorldLimits(SphereLimits, EmptyCapsules, EmptyTaperedCapsules, EmptyBoxes, EmptyConvexes);

		TestEqual(TEXT("Injected SimpleWorld collider count"),
		          A.Node.GetNumSimpleWorldColliders(),
		          SphereLimits.Num() + EmptyCapsules.Num() + EmptyTaperedCapsules.Num() + EmptyBoxes.Num() +
		          EmptyConvexes.Num());

		A.StepFrame(1.0f / 60.0f);

		TestTrue(FString::Printf(TEXT("SimpleWorld sphere push-out: got %s expected %s"),
		                         *A.Bone(1).Location.ToString(), *ExpectedPushedOut.ToString()),
		         A.Bone(1).Location.Equals(ExpectedPushedOut, GSimpleWorldPushOutTol));
	}

	// bUseSimpleWorldCollision = false: 同じ形状を注入しても押し出されない（適用条件のゲート確認）
	{
		FKawaiiPhysicsTestAccessor A;
		BuildChain(A);
		A.SetSimpleWorldLimits(SphereLimits, EmptyCapsules, EmptyTaperedCapsules, EmptyBoxes, EmptyConvexes);
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

		A.SetSimpleWorldLimits(EmptySpheres, CapsuleLimits, EmptyTaperedCapsules, EmptyBoxes, EmptyConvexes);

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

	// bUseSimpleWorldCollision = true、Convex注入: CachedConvexTransform を意図的に古くして注入し、
	// StepOnce内のPrepareCollisionShapeCaches()で再計算されない場合だけ押し出されない配置にする。
	{
		const FVector ConvexStart(5.8f, 0.0f, -8.0f);
		const FVector ConvexExpected(6.0f, 0.0f, -8.0f);
		FKawaiiPhysicsTestAccessor A;
		BuildChain(A);
		A.Bone(1).Location = ConvexStart;
		A.Bone(1).PrevLocation = ConvexStart;

		FKawaiiPhysicsConvexLimit Convex;
		Convex.Location = FVector(5.0f, 0.0f, -8.0f);
		Convex.Rotation = FQuat::Identity;
		Convex.LocalPlanes = MakeUnitCubePlanes();
		Convex.LocalBounds = FBox(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f));
		Convex.bEnable = true;
		Convex.SourceType = ECollisionSourceType::SimpleWorld;
		Convex.CachedConvexTransform = FTransform::Identity;
		TArray<FKawaiiPhysicsConvexLimit> ConvexLimits = {Convex};

		A.SetSimpleWorldLimits(EmptySpheres, EmptyCapsules, EmptyTaperedCapsules, EmptyBoxes, ConvexLimits);

		A.StepFrame(1.0f / 60.0f);

		TestTrue(FString::Printf(TEXT("SimpleWorld convex push-out: got %s expected %s"),
		                         *A.Bone(1).Location.ToString(), *ConvexExpected.ToString()),
		         A.Bone(1).Location.Equals(ConvexExpected, GSimpleWorldPushOutTol));
	}

	// 地面 Box を通常 Box の末尾に入れた旧相当経路と、専用配列に分けた経路の押し出し順序を一致させる。
	{
		const float GroundTopZ = -10.0f;
		const FVector GroundStart(1.0f, 0.0f, -11.0f);

		FBoxLimit GroundBox;
		GroundBox.Location = FVector(0.0f, 0.0f, -12.0f);
		GroundBox.Rotation = FQuat::Identity;
		GroundBox.Extent = FVector(100.0f, 100.0f, 2.0f);
		GroundBox.bEnable = true;
		GroundBox.SourceType = ECollisionSourceType::SimpleWorld;
		TArray<FBoxLimit> GroundBoxes = {GroundBox};

		auto BuildGroundChain = [&](FKawaiiPhysicsTestAccessor& A)
		{
			BuildChain(A);
			A.Bone(1).PhysicsSettings.Radius = 1.0f;
			A.Bone(1).Location = GroundStart;
			A.Bone(1).PrevLocation = GroundStart;
		};

		FKawaiiPhysicsTestAccessor LegacyBoxPath;
		BuildGroundChain(LegacyBoxPath);
		LegacyBoxPath.SetSimpleWorldLimits(
			EmptySpheres, EmptyCapsules, EmptyTaperedCapsules, GroundBoxes, EmptyConvexes);
		LegacyBoxPath.StepFrame(1.0f / 60.0f);

		FKawaiiPhysicsTestAccessor GroundBoxPath;
		BuildGroundChain(GroundBoxPath);
		GroundBoxPath.SetSimpleWorldLimits(
			EmptySpheres, EmptyCapsules, EmptyTaperedCapsules, EmptyBoxes, EmptyConvexes, GroundBoxes);
		GroundBoxPath.StepFrame(1.0f / 60.0f);

		TestTrue(FString::Printf(TEXT("Ground box pushes bone upward: got %s top %.2f"),
		                         *GroundBoxPath.Bone(1).Location.ToString(), GroundTopZ),
		         GroundBoxPath.Bone(1).Location.Z >= GroundTopZ - GSimpleWorldPushOutTol);
		TestTrue(FString::Printf(TEXT("Dedicated ground box path matches legacy box tail path: got %s expected %s"),
		                         *GroundBoxPath.Bone(1).Location.ToString(),
		                         *LegacyBoxPath.Bone(1).Location.ToString()),
		         GroundBoxPath.Bone(1).Location.Equals(LegacyBoxPath.Bone(1).Location, GSimpleWorldPushOutTol));
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

	FKawaiiPhysicsConvexLimit Convex;
	Convex.Location = FVector(30.0f, 0.0f, 0.0f);
	Convex.Rotation = FQuat::Identity;
	Convex.LocalPlanes = MakeUnitCubePlanes();
	Convex.LocalBounds = FBox(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f));
	LocalLimits.ConvexLimits.Add(Convex);

	TArray<FKawaiiPhysicsSimpleWorldBodyBinding> Bindings;
	FKawaiiPhysicsSimpleWorldBodyBinding BodyA;
	BodyA.BoneIndex = 0;
	BodyA.NumSphericalLimits = 2;
	BodyA.NumBoxLimits = 1;
	BodyA.NumConvexLimits = 1;
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
		TestTrue(TEXT("FadeAlpha=0.5: convex follows body A"),
		         OutWorldLimits.ConvexLimits.Num() == 1 &&
		         OutWorldLimits.ConvexLimits[0].Location.Equals(FVector(30.0f, 0.0f, 100.0f), GSimpleWorldTol));
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
		TestTrue(TEXT("FadeAlpha=0.4: convex is withheld"), OutWorldLimits.ConvexLimits.Num() == 0);
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
		         64,
		         false,
		         2,
		         OutLimits,
		         Bindings));
	TestTrue(TEXT("Second body is accepted"),
	         KawaiiPhysicsSimpleWorldCollision::AppendBodyLocalLimits(
		         SphereAggGeom,
		         1,
		         FVector::OneVector,
		         EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
		         64,
		         false,
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
		         64,
		         false,
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
		         64,
		         false,
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
			         64,
			         false,
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
			         64,
			         false,
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
	HandBody->AggGeom.SphereElems.Add_GetRef(HandNoCollisionSphere).SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
			64,
			false,
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
			64,
			false,
			1,
			OutLimits,
			Bindings);

		TestTrue(TEXT("MaxBodies=1 accepts only spine"), NumBodies == 1 && Bindings.Num() == 1);
		TestTrue(TEXT("MaxBodies=1 keeps the lowest bone index body"),
		         Bindings.Num() == 1 && Bindings[0].BoneIndex == 1);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
