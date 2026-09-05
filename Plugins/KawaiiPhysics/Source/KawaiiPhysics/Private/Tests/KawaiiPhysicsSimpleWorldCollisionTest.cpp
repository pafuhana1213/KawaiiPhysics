// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "AnimNode_KawaiiPhysicsInternal.h"
#include "KawaiiPhysicsSimpleWorldCollision.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NativeGameplayTags.h"
#include "Misc/EngineVersionComparison.h"
#include "PhysicsEngine/PhysicsAsset.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"

#include <limits>

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsSimpleWorldRegistryX, "KawaiiPhysics.Test.SimpleWorld.Registry.X");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsSimpleWorldRegistryY, "KawaiiPhysics.Test.SimpleWorld.Registry.Y");

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

	const FBoxLimit IdentityLocalBox =
		KawaiiPhysicsSimpleWorldCollision::MakeSimpleWorldGroundBoxLocal(WorldBox, FTransform::Identity);
	TestTrue(TEXT("Identity local location matches"),
	         IdentityLocalBox.Location.Equals(WorldBox.Location, GSimpleWorldTol));
	TestTrue(TEXT("Identity local rotation matches"),
	         IdentityLocalBox.Rotation.Equals(WorldBox.Rotation, GSimpleWorldTol));
	TestTrue(TEXT("Identity local extent matches"),
	         IdentityLocalBox.Extent.Equals(WorldBox.Extent, GSimpleWorldTol));

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
			AggGeom, Scale, EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox, 64, false, OutLimits);

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

	FKawaiiPhysicsConvexLimit Convex;
	Convex.Location = FVector(3.0f, 0.0f, 0.0f);
	Convex.Rotation = FQuat::Identity;
	Convex.LocalPlanes = MakeUnitCubePlanes();
	Convex.LocalBounds = FBox(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f));
	LocalLimits.ConvexLimits.Add(Convex);

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

	TestTrue(TEXT("Convex count"), OutWorldLimits.ConvexLimits.Num() == 1);
	const FVector ExpectedConvexLocation = ComponentTM.TransformPosition(FVector(3.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Convex location is transformed by ComponentTM"),
	         OutWorldLimits.ConvexLimits.Num() == 1 &&
	         OutWorldLimits.ConvexLimits[0].Location.Equals(ExpectedConvexLocation, GSimpleWorldTol));
	TestTrue(TEXT("Convex rotation is composed with ComponentTM rotation"),
	         OutWorldLimits.ConvexLimits.Num() == 1 &&
	         OutWorldLimits.ConvexLimits[0].Rotation.Equals(ComponentRotation, GSimpleWorldTol));
	TestTrue(TEXT("Convex plane array is copied unchanged"),
	         OutWorldLimits.ConvexLimits.Num() == 1 &&
	         OutWorldLimits.ConvexLimits[0].LocalPlanes.Num() == Convex.LocalPlanes.Num() &&
	         OutWorldLimits.ConvexLimits[0].LocalPlanes[0].Equals(Convex.LocalPlanes[0], GSimpleWorldTol));

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
		TestTrue(TEXT("FadeAlpha=0.4 (< threshold): convex is withheld"), OutWorldLimits.ConvexLimits.Num() == 0);
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
		TestTrue(TEXT("FadeAlpha=1: convex is kept"), OutWorldLimits.ConvexLimits.Num() == 1);
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
		TestTrue(TEXT("Empty ObjectTypes ignores PhysicsBody"),
		         ResponseParams.CollisionResponse.GetResponse(ECC_PhysicsBody) == ECR_Ignore);
		TestTrue(TEXT("Empty ObjectTypes ignores Visibility"),
		         ResponseParams.CollisionResponse.GetResponse(ECC_Visibility) == ECR_Ignore);
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
		TestTrue(TEXT("Explicit ObjectTypes ignores WorldDynamic"),
		         ResponseParams.CollisionResponse.GetResponse(ECC_WorldDynamic) == ECR_Ignore);
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

	TestFalse(TEXT("NaN radius is invalid"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldGatherInputValid(
		          FVector(1.0f, 2.0f, 3.0f), std::numeric_limits<float>::quiet_NaN()));

	TestFalse(TEXT("Zero radius is invalid"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldGatherInputValid(FVector(1.0f, 2.0f, 3.0f), 0.0f));

	TestFalse(TEXT("Negative radius is invalid"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldGatherInputValid(FVector(1.0f, 2.0f, 3.0f), -1.0f));

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
	TestEqual(TEXT("Equivalent local keys have the same hash"), GetTypeHash(LocalA0), GetTypeHash(LocalA1));

	FKawaiiPhysicsSimpleWorldRegistryKey SharedX;
	SharedX.KeyObject = GetTransientPackage();
	SharedX.Tag = TAG_KawaiiPhysicsSimpleWorldRegistryX;
	FKawaiiPhysicsSimpleWorldRegistryKey SharedY;
	SharedY.KeyObject = GetTransientPackage();
	SharedY.Tag = TAG_KawaiiPhysicsSimpleWorldRegistryY;
	TestFalse(TEXT("Shared keys with different tags are different"), SharedX == SharedY);
	TestFalse(TEXT("MakeSharedKey separates different tags"),
	          FKawaiiPhysicsSimpleWorldRegistryKey::MakeSharedKey(nullptr, TAG_KawaiiPhysicsSimpleWorldRegistryX) ==
	          FKawaiiPhysicsSimpleWorldRegistryKey::MakeSharedKey(nullptr, TAG_KawaiiPhysicsSimpleWorldRegistryY));

	TMap<FKawaiiPhysicsSimpleWorldRegistryKey, int32> Registry;
	Registry.Add(LocalA0, 42);
	const int32* FoundValue = Registry.Find(LocalA1);
	TestTrue(TEXT("Registry key works as a TMap key"), FoundValue && *FoundValue == 42);

	// Worker から呼ぶ弱参照版 MakeLocalKey は raw ポインタ版と同じキーになり、IsValid() も一致する。
	const FKawaiiPhysicsSimpleWorldRegistryKey WeakLocalA =
		FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(TWeakObjectPtr<const USkeletalMeshComponent>(SkelCompA));
	TestTrue(TEXT("Weak MakeLocalKey equals the raw pointer key"), WeakLocalA == LocalA0);
	TestEqual(TEXT("Weak MakeLocalKey has the same hash as the raw pointer key"),
	          GetTypeHash(WeakLocalA), GetTypeHash(LocalA0));
	TestTrue(TEXT("Weak MakeLocalKey is valid"), WeakLocalA.IsValid());

	const FKawaiiPhysicsSimpleWorldRegistryKey DefaultKey;
	TestFalse(TEXT("Default-constructed key is invalid"), DefaultKey.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldProviderDescWinsTest,
                                 "KawaiiPhysics.SimpleWorld.ProviderDescWins",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldProviderDescWinsTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderSourceID = 1;
	constexpr uint64 ReaderSourceID0 = 2;
	constexpr uint64 ReaderSourceID1 = 3;
	constexpr uint64 Frame = 100;

	USkeletalMeshComponent* ProviderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* ReaderSkelComp0 =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* ReaderSkelComp1 =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);

	FKawaiiPhysicsSimpleWorldCollisionDesc ProviderDesc;
	ProviderDesc.GatherIntervalSec = 0.05f;
	ProviderDesc.GatherRadiusOverride = 120.0f;
	ProviderDesc.bGatherRadiusAllOverridden = true;
	ProviderDesc.CollisionChannel = ECC_Visibility;
	ProviderDesc.ObjectTypes = {UEngineTypes::ConvertToObjectType(ECC_WorldStatic)};
	ProviderDesc.bGroundCollision = false;

	FKawaiiPhysicsSimpleWorldCollisionDesc ReaderDesc;
	ReaderDesc.GatherIntervalSec = 0.5f;
	ReaderDesc.GatherRadiusOverride = 600.0f;
	ReaderDesc.CollisionChannel = ECC_Pawn;
	ReaderDesc.ObjectTypes = {UEngineTypes::ConvertToObjectType(ECC_Pawn)};
	ReaderDesc.GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily;

	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	Entry.SetDesc(ProviderSourceID, ProviderDesc, Frame, ProviderSkelComp, true);
	Entry.SetDesc(ReaderSourceID0, ReaderDesc, Frame, ReaderSkelComp0, false);
	Entry.SetDesc(ReaderSourceID1, ReaderDesc, Frame, ReaderSkelComp1, false);

	FKawaiiPhysicsSimpleWorldCollisionDesc MergedDesc;
	TestTrue(TEXT("BuildMergedDesc succeeds with one provider and readers"), Entry.BuildMergedDesc(MergedDesc));
	TestTrue(TEXT("Merged desc comes from the provider"), MergedDesc == ProviderDesc);
	TestEqual(TEXT("Reader count"), Entry.GetNumReaders(), 2);
	TestTrue(TEXT("Provider desc exists"), Entry.HasProviderDesc());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldReaderMembersLifecycleTest,
                                 "KawaiiPhysics.SimpleWorld.ReaderMembersLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldReaderMembersLifecycleTest::RunTest(const FString& Parameters)
{
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

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldReaderReleasedWhenProviderExpiresTest,
                                 "KawaiiPhysics.SimpleWorld.ReaderReleasedWhenProviderExpires",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldReaderReleasedWhenProviderExpiresTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderSourceID = 20;
	constexpr uint64 ReaderSourceID = 21;
	constexpr uint64 ProviderFrame = 100;
	constexpr uint64 ProviderMaxAge = 60;

	USkeletalMeshComponent* ProviderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* ReaderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);

	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	Entry.SetDesc(ProviderSourceID, Desc, ProviderFrame, ProviderSkelComp, true);
	Entry.AddReaderMember(ReaderSourceID, ReaderSkelComp, ProviderFrame);

	TestTrue(TEXT("Reader keeps membership while provider is within max age"),
	         Entry.MarkReaderRead(ReaderSourceID, ProviderFrame + ProviderMaxAge, ProviderMaxAge));
	TestFalse(TEXT("Reader releases after provider exceeds max age"),
	          Entry.MarkReaderRead(ReaderSourceID, ProviderFrame + ProviderMaxAge + 1, ProviderMaxAge));

	FKawaiiPhysicsSimpleWorldCollisionEntry ReaderOnlyEntry;
	ReaderOnlyEntry.AddReaderMember(ReaderSourceID, ReaderSkelComp, ProviderFrame);
	TestFalse(TEXT("Reader releases when no provider exists"),
	          ReaderOnlyEntry.MarkReaderRead(ReaderSourceID, ProviderFrame + 1, ProviderMaxAge));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldReaderKeepsMembershipWhenProviderDisabledTest,
                                 "KawaiiPhysics.SimpleWorld.ReaderKeepsMembershipWhenProviderDisabled",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldReaderKeepsMembershipWhenProviderDisabledTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderSourceID = 30;
	constexpr uint64 ReaderSourceID = 31;
	constexpr uint64 Frame = 100;

	USkeletalMeshComponent* ProviderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* ReaderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);

	FKawaiiPhysicsSimpleWorldCollisionDesc DisabledDesc;
	DisabledDesc.bProviderDisabled = true;

	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	Entry.SetDesc(ProviderSourceID, DisabledDesc, Frame, ProviderSkelComp, true);
	Entry.AddReaderMember(ReaderSourceID, ReaderSkelComp, Frame);

	TestTrue(TEXT("Disabled provider is still live for readers"),
	         Entry.MarkReaderRead(ReaderSourceID, Frame + 1, 60));
	TestTrue(TEXT("Merged provider disabled state is true"), Entry.IsProviderDisabled());

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

	TestTrue(TEXT("Primary component is the provider component"), Entry.GetPrimarySkelComp() == ProviderSkelComp);

	TArray<TWeakObjectPtr<const USkeletalMeshComponent>> Members;
	Entry.CollectMemberSkelComps(Members);
	TestEqual(TEXT("CollectMemberSkelComps returns provider and reader once"), Members.Num(), 2);
	TestTrue(TEXT("Collected members contain provider"),
	         Members.Contains(TWeakObjectPtr<const USkeletalMeshComponent>(ProviderSkelComp)));
	TestTrue(TEXT("Collected members contain reader"),
	         Members.Contains(TWeakObjectPtr<const USkeletalMeshComponent>(ReaderSkelComp)));

	Entry.RemoveDesc(ProviderSourceID);
	TestTrue(TEXT("Primary component is null after removing the provider"), Entry.GetPrimarySkelComp() == nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldDescMergeGatherScopeTest,
                                 "KawaiiPhysics.SimpleWorld.DescMergeGatherScope",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldDescMergeGatherScopeTest::RunTest(const FString& Parameters)
{
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

	FKawaiiPhysicsSimpleWorldCollisionDesc Base;
	FKawaiiPhysicsSimpleWorldCollisionDesc GatherScopeChanged = Base;
	GatherScopeChanged.GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily;
	TestTrue(TEXT("DoesChangeRequireRegather detects GatherScope"),
	         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, GatherScopeChanged));
	FKawaiiPhysicsSimpleWorldCollisionDesc FamilyMembersChanged = Base;
	FamilyMembersChanged.bGatherFamilyMembers = true;
	TestTrue(TEXT("DoesChangeRequireRegather detects bGatherFamilyMembers"),
	         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, FamilyMembersChanged));
	FKawaiiPhysicsSimpleWorldCollisionDesc ProviderDisabledChanged = Base;
	ProviderDisabledChanged.bProviderDisabled = true;
	TestTrue(TEXT("DoesChangeRequireRegather detects bProviderDisabled"),
	         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, ProviderDisabledChanged));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldGatherBoundsUnionTest,
                                 "KawaiiPhysics.SimpleWorld.GatherBoundsUnion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldGatherBoundsUnionTest::RunTest(const FString& Parameters)
{
	TArray<FBoxSphereBounds> EmptyBounds;
	FBoxSphereBounds OutBounds;
	TestFalse(TEXT("Empty bounds input returns false"),
	          KawaiiPhysicsSimpleWorldCollision::ComputeSimpleWorldGatherBounds(MakeArrayView(EmptyBounds), OutBounds));

	TArray<FBoxSphereBounds> SingleBounds;
	SingleBounds.Add(FBoxSphereBounds(FVector(10.0f, 20.0f, 30.0f), FVector(4.0f, 5.0f, 6.0f), 7.0f));
	TestTrue(TEXT("Single bounds input returns true"),
	         KawaiiPhysicsSimpleWorldCollision::ComputeSimpleWorldGatherBounds(MakeArrayView(SingleBounds), OutBounds));
	TestTrue(TEXT("Single bounds origin unchanged"), OutBounds.Origin.Equals(SingleBounds[0].Origin, GSimpleWorldTol));
	TestTrue(TEXT("Single bounds extent unchanged"), OutBounds.BoxExtent.Equals(SingleBounds[0].BoxExtent, GSimpleWorldTol));
	TestTrue(TEXT("Single bounds radius unchanged"),
	         FMath::IsNearlyEqual(OutBounds.SphereRadius, SingleBounds[0].SphereRadius, GSimpleWorldTol));

	TArray<FBoxSphereBounds> PairBounds;
	PairBounds.Add(FBoxSphereBounds(FVector(0.0f, 0.0f, 0.0f), FVector(10.0f, 10.0f, 10.0f), 10.0f));
	PairBounds.Add(FBoxSphereBounds(FVector(100.0f, 0.0f, 0.0f), FVector(5.0f, 5.0f, 5.0f), 5.0f));
	TestTrue(TEXT("Pair bounds input returns true"),
	         KawaiiPhysicsSimpleWorldCollision::ComputeSimpleWorldGatherBounds(MakeArrayView(PairBounds), OutBounds));

	const FBox UnionBox = OutBounds.GetBox();
	const FBox FirstBox = PairBounds[0].GetBox();
	const FBox SecondBox = PairBounds[1].GetBox();
	TestTrue(TEXT("Union min contains both boxes"),
	         UnionBox.Min.X <= FirstBox.Min.X + GSimpleWorldTol &&
	         UnionBox.Min.Y <= FirstBox.Min.Y + GSimpleWorldTol &&
	         UnionBox.Min.Z <= FirstBox.Min.Z + GSimpleWorldTol &&
	         UnionBox.Min.X <= SecondBox.Min.X + GSimpleWorldTol &&
	         UnionBox.Min.Y <= SecondBox.Min.Y + GSimpleWorldTol &&
	         UnionBox.Min.Z <= SecondBox.Min.Z + GSimpleWorldTol);
	TestTrue(TEXT("Union max contains both boxes"),
	         UnionBox.Max.X + GSimpleWorldTol >= FirstBox.Max.X &&
	         UnionBox.Max.Y + GSimpleWorldTol >= FirstBox.Max.Y &&
	         UnionBox.Max.Z + GSimpleWorldTol >= FirstBox.Max.Z &&
	         UnionBox.Max.X + GSimpleWorldTol >= SecondBox.Max.X &&
	         UnionBox.Max.Y + GSimpleWorldTol >= SecondBox.Max.Y &&
	         UnionBox.Max.Z + GSimpleWorldTol >= SecondBox.Max.Z);
	TestTrue(TEXT("Union extent spans separated bounds"), OutBounds.BoxExtent.X > PairBounds[0].BoxExtent.X);
	TestTrue(TEXT("Union sphere radius spans separated bounds"), OutBounds.SphereRadius > PairBounds[0].SphereRadius);

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
		Desc.CollisionChannel = ECC_Pawn;
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
		TestTrue(TEXT("Single desc: CollisionChannel unchanged"), Merged.CollisionChannel == ECC_Pawn);
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

		FKawaiiPhysicsSimpleWorldCollisionDesc SkeletalMeshChanged = Base;
		SkeletalMeshChanged.SkeletalMeshCollision = EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::PhysicsAsset;
		TestTrue(TEXT("DoesChangeRequireRegather detects SkeletalMeshCollision changes"),
		         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, SkeletalMeshChanged));

		FKawaiiPhysicsSimpleWorldCollisionDesc GroundChanged = Base;
		GroundChanged.bGroundCollision = false;
		TestTrue(TEXT("DoesChangeRequireRegather detects bGroundCollision changes"),
		         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, GroundChanged));

		FKawaiiPhysicsSimpleWorldCollisionDesc RadiusChanged = Base;
		RadiusChanged.GatherRadiusOverride = 300.0f;
		TestTrue(TEXT("DoesChangeRequireRegather detects GatherRadiusOverride changes"),
		         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, RadiusChanged));

		FKawaiiPhysicsSimpleWorldCollisionDesc RadiusFlagChanged = Base;
		RadiusFlagChanged.bGatherRadiusAllOverridden = true;
		TestTrue(TEXT("DoesChangeRequireRegather detects bGatherRadiusAllOverridden changes"),
		         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, RadiusFlagChanged));

		FKawaiiPhysicsSimpleWorldCollisionDesc ChannelChanged = Base;
		ChannelChanged.CollisionChannel = ECC_Pawn;
		TestTrue(TEXT("DoesChangeRequireRegather detects CollisionChannel changes"),
		         FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(Base, ChannelChanged));
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

	{
		FKawaiiPhysicsSimpleWorldCollisionDesc SphereDesc, HullDesc;
		SphereDesc.ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingSphere;
		HullDesc.ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull;
		const FKawaiiPhysicsSimpleWorldCollisionDesc Merged =
			FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({SphereDesc, HullDesc});
		TestTrue(TEXT("ConvexFallbackShape {BoundingSphere,ConvexHull} -> ConvexHull"),
		         Merged.ConvexFallbackShape == EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull);
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

	// BuildMergedDesc: CollisionChannel の「最初の非 ECC_MAX を採用」は TMap の反復順ではなく登録順で決まる。
	// SourceID を小さい整数にして、TMap の並び（ハッシュ順）と登録順が食い違う状況を作る。
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc VisibilityDesc;
		VisibilityDesc.CollisionChannel = ECC_Visibility;
		FKawaiiPhysicsSimpleWorldCollisionDesc PawnDesc;
		PawnDesc.CollisionChannel = ECC_Pawn;

		FKawaiiPhysicsSimpleWorldCollisionEntry VisibilityFirstEntry;
		VisibilityFirstEntry.SetDesc(SourceID2, VisibilityDesc);
		VisibilityFirstEntry.SetDesc(SourceID1, PawnDesc);

		FKawaiiPhysicsSimpleWorldCollisionDesc VisibilityFirstMerged;
		TestTrue(TEXT("BuildMergedDesc succeeds when two sources override the channel"),
		         VisibilityFirstEntry.BuildMergedDesc(VisibilityFirstMerged));
		TestTrue(TEXT("Channel comes from the earliest registered desc (Visibility registered first)"),
		         VisibilityFirstMerged.CollisionChannel == ECC_Visibility);

		FKawaiiPhysicsSimpleWorldCollisionEntry PawnFirstEntry;
		PawnFirstEntry.SetDesc(SourceID1, PawnDesc);
		PawnFirstEntry.SetDesc(SourceID2, VisibilityDesc);

		FKawaiiPhysicsSimpleWorldCollisionDesc PawnFirstMerged;
		TestTrue(TEXT("BuildMergedDesc succeeds with the reversed registration order"),
		         PawnFirstEntry.BuildMergedDesc(PawnFirstMerged));
		TestTrue(TEXT("Channel comes from the earliest registered desc (Pawn registered first)"),
		         PawnFirstMerged.CollisionChannel == ECC_Pawn);

		// 先に登録したノードが外れたら、残ったノードのチャンネルへ切り替わる。
		PawnFirstEntry.RemoveDesc(SourceID1);
		FKawaiiPhysicsSimpleWorldCollisionDesc MergedAfterRemove;
		TestTrue(TEXT("BuildMergedDesc succeeds after removing the earliest registered desc"),
		         PawnFirstEntry.BuildMergedDesc(MergedAfterRemove));
		TestTrue(TEXT("Channel falls back to the remaining desc after the earliest one is removed"),
		         MergedAfterRemove.CollisionChannel == ECC_Visibility);
	}

	// Slot: Publish→AppendTo のラウンドトリップ最小限（詳細な契約は KawaiiPhysicsSharedCollisionSlotTest でカバー済み）
	{
		FKawaiiPhysicsSimpleWorldCollisionEntry Entry;

		FKawaiiPhysicsSharedCollisionData PublishData;
		FSphericalLimit Sphere;
		Sphere.Radius = 12.0f;
		PublishData.SphericalLimits.Add(Sphere);
		FKawaiiPhysicsConvexLimit Convex;
		Convex.LocalPlanes = MakeUnitCubePlanes();
		Convex.LocalBounds = FBox(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f));
		PublishData.ConvexLimits.Add(Convex);
		Entry.Slot.Publish(PublishData);

		FKawaiiPhysicsSharedCollisionData OutData;
		Entry.Slot.AppendTo(OutData);
		TestTrue(TEXT("Entry.Slot round-trips published data"),
		         OutData.SphericalLimits.Num() == 1 &&
		         FMath::IsNearlyEqual(OutData.SphericalLimits[0].Radius, 12.0f, GSimpleWorldTol) &&
		         OutData.ConvexLimits.Num() == 1 &&
		         OutData.ConvexLimits[0].LocalPlanes.Num() == Convex.LocalPlanes.Num());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldGroundSlotIndependentPublishTest,
                                 "KawaiiPhysics.SimpleWorld.GroundSlotIndependentPublish",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldGroundSlotIndependentPublishTest::RunTest(const FString& Parameters)
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

	DistanceSquared.Reset();
	KawaiiPhysicsSimpleWorldCollision::SortSimpleWorldGatherOrderByDistance(
		MakeArrayView(DistanceSquared),
		OutOrder);
	TestTrue(TEXT("Empty distance list produces empty order"), OutOrder.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldGatherOrderSkippedForZeroCapTest,
                                 "KawaiiPhysics.SimpleWorld.GatherOrderSkippedForZeroCap",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldGatherOrderSkippedForZeroCapTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Zero cap skips gather order with several overlaps"),
	          KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(5, 0));
	TestFalse(TEXT("Zero cap skips gather order with a single overlap"),
	          KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(1, 0));
	TestFalse(TEXT("Zero cap skips gather order with no overlaps"),
	          KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(0, 0));
	TestTrue(TEXT("Gather order is used when overlaps exceed the cap"),
	         KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(5, 3));
	TestFalse(TEXT("Gather order is skipped when overlaps equal the cap"),
	          KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(3, 3));
	TestFalse(TEXT("Gather order is skipped when overlaps are under the cap"),
	          KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(2, 3));
	TestFalse(TEXT("Negative cap is defensively treated as skip"),
	          KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(5, -1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldFamilyMemberSlotsExcludeSelfTest,
                                 "KawaiiPhysics.SimpleWorld.FamilyMemberSlotsExcludeSelf",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldFamilyMemberSlotsExcludeSelfTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	USkeletalMeshComponent* SkelCompX = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* SkelCompY = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);

	FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& NormalComponent =
		Entry.GatheredComponents.AddDefaulted_GetRef();
	NormalComponent.FadeAlpha = 1.0f;
	NormalComponent.LastComponentTM = FTransform::Identity;
	FSphericalLimit NormalSphere;
	NormalSphere.Location = FVector(1.0f, 0.0f, 0.0f);
	NormalSphere.Radius = 10.0f;
	NormalSphere.bEnable = true;
	NormalSphere.SourceType = ECollisionSourceType::SimpleWorld;
	NormalComponent.LocalLimits.SphericalLimits.Add(NormalSphere);

	FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& MemberXComponent =
		Entry.GatheredComponents.AddDefaulted_GetRef();
	MemberXComponent.MemberSkelComp = SkelCompX;
	MemberXComponent.FadeAlpha = 1.0f;
	MemberXComponent.LastComponentTM = FTransform::Identity;
	FSphericalLimit SphereX = NormalSphere;
	SphereX.Location = FVector(20.0f, 0.0f, 0.0f);
	MemberXComponent.LocalLimits.SphericalLimits.Add(SphereX);

	FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& MemberYComponent =
		Entry.GatheredComponents.AddDefaulted_GetRef();
	MemberYComponent.MemberSkelComp = SkelCompY;
	MemberYComponent.FadeAlpha = 1.0f;
	MemberYComponent.LastComponentTM = FTransform::Identity;
	FSphericalLimit SphereY = NormalSphere;
	SphereY.Location = FVector(30.0f, 0.0f, 0.0f);
	MemberYComponent.LocalLimits.SphericalLimits.Add(SphereY);

	UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldShapeLimits(Entry, 0.5f);

	FKawaiiPhysicsSharedCollisionData MainOutData;
	Entry.Slot.AppendTo(MainOutData);
	TestEqual(TEXT("Main slot contains only the regular shape"), MainOutData.SphericalLimits.Num(), 1);
	if (MainOutData.SphericalLimits.Num() == 1)
	{
		TestTrue(TEXT("Main slot regular shape location"),
		         MainOutData.SphericalLimits[0].Location.Equals(FVector(1.0f, 0.0f, 0.0f), GSimpleWorldTol));
	}

	const TWeakObjectPtr<const USkeletalMeshComponent> KeyX(SkelCompX);
	const TWeakObjectPtr<const USkeletalMeshComponent> KeyY(SkelCompY);
	TestTrue(TEXT("Member slot X exists"), Entry.MemberSlots.Contains(KeyX));
	TestTrue(TEXT("Member slot Y exists"), Entry.MemberSlots.Contains(KeyY));

	FKawaiiPhysicsSharedCollisionData MemberXOutData;
	if (const TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>* SlotX = Entry.MemberSlots.Find(KeyX))
	{
		(*SlotX)->AppendTo(MemberXOutData);
	}
	TestEqual(TEXT("Member slot X contains one shape"), MemberXOutData.SphericalLimits.Num(), 1);
	if (MemberXOutData.SphericalLimits.Num() == 1)
	{
		TestTrue(TEXT("Member slot X shape location"),
		         MemberXOutData.SphericalLimits[0].Location.Equals(FVector(20.0f, 0.0f, 0.0f), GSimpleWorldTol));
	}

	FKawaiiPhysicsSharedCollisionData MemberYOutData;
	if (const TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>* SlotY = Entry.MemberSlots.Find(KeyY))
	{
		(*SlotY)->AppendTo(MemberYOutData);
	}
	TestEqual(TEXT("Member slot Y contains one shape"), MemberYOutData.SphericalLimits.Num(), 1);
	if (MemberYOutData.SphericalLimits.Num() == 1)
	{
		TestTrue(TEXT("Member slot Y shape location"),
		         MemberYOutData.SphericalLimits[0].Location.Equals(FVector(30.0f, 0.0f, 0.0f), GSimpleWorldTol));
	}

	FKawaiiPhysicsSharedCollisionData OutForX;
	Entry.AppendFamilyMemberLimits(SkelCompX, OutForX);
	TestEqual(TEXT("Append for X excludes self and includes Y"), OutForX.SphericalLimits.Num(), 1);
	if (OutForX.SphericalLimits.Num() == 1)
	{
		TestTrue(TEXT("Append for X contains Y shape"),
		         OutForX.SphericalLimits[0].Location.Equals(FVector(30.0f, 0.0f, 0.0f), GSimpleWorldTol));
	}

	FKawaiiPhysicsSharedCollisionData OutForY;
	Entry.AppendFamilyMemberLimits(SkelCompY, OutForY);
	TestEqual(TEXT("Append for Y excludes self and includes X"), OutForY.SphericalLimits.Num(), 1);
	if (OutForY.SphericalLimits.Num() == 1)
	{
		TestTrue(TEXT("Append for Y contains X shape"),
		         OutForY.SphericalLimits[0].Location.Equals(FVector(20.0f, 0.0f, 0.0f), GSimpleWorldTol));
	}

	FKawaiiPhysicsSharedCollisionData OutForNull;
	Entry.AppendFamilyMemberLimits(TWeakObjectPtr<const USkeletalMeshComponent>(), OutForNull);
	TestEqual(TEXT("Append for null includes both member shapes"), OutForNull.SphericalLimits.Num(), 2);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldProviderDisabledPublishesEmptyTest,
                                 "KawaiiPhysics.SimpleWorld.ProviderDisabledPublishesEmpty",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldProviderDisabledPublishesEmptyTest::RunTest(const FString& Parameters)
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
		TestTrue(TEXT("Empty entry ground component is static by default"), Info.bGroundComponentStatic);
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
		Entry.bGroundComponentStatic = false;
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
		TestFalse(TEXT("Filled entry ground component static"), Info.bGroundComponentStatic);
		TestTrue(TEXT("Filled entry GroundSource"),
		         Info.GroundSource == EKawaiiPhysicsSimpleWorldGroundSource::Provider);
		TestTrue(TEXT("Filled entry GroundBoxSource"),
		         Info.GroundBoxSource == EKawaiiPhysicsSimpleWorldGroundSource::CharacterMovement);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldDebugInfoSharedFieldsTest,
                                 "KawaiiPhysics.SimpleWorld.DebugInfoSharedFields",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldDebugInfoSharedFieldsTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderID = 601;
	constexpr uint64 ReaderID1 = 602;
	constexpr uint64 ReaderID2 = 603;

	UObject* KeyObject = NewObject<USkeletalMeshComponent>(
		GetTransientPackage(),
		FName(TEXT("SimpleWorldDebugInfoSharedKey")),
		RF_Transient);
	USkeletalMeshComponent* ProviderSkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* ReaderSkelComp1 =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalMeshComponent* ReaderSkelComp2 =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);

	const FGameplayTag GroupTag = FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Test")), false);
	FKawaiiPhysicsSimpleWorldRegistryKey Key;
	Key.KeyObject = KeyObject;
	Key.Tag = GroupTag;

	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	Desc.GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily;
	Desc.bProviderDisabled = true;
	Desc.bGatherFamilyMembers = true;

	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	Entry.SetDesc(ProviderID, Desc, GFrameCounter, ProviderSkelComp, true);
	Entry.AddReaderMember(ReaderID1, ReaderSkelComp1, GFrameCounter);
	Entry.AddReaderMember(ReaderID2, ReaderSkelComp2, GFrameCounter);
	Entry.MemberSlots.Add(
		TWeakObjectPtr<const USkeletalMeshComponent>(ProviderSkelComp),
		MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>());

	FKawaiiPhysicsSimpleWorldCollisionDebugInfo Info;
	UKawaiiPhysicsSharedCollisionSubsystem::FillSimpleWorldCollisionDebugInfo(Entry, Info, &Key);

	TestTrue(TEXT("Shared debug info has entry"), Info.bHasEntry);
	TestEqual(TEXT("Shared debug info reader count"), Info.NumReaders, 2);
	TestTrue(TEXT("Shared debug info gather scope"),
	         Info.GatherScope == EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily);
	TestTrue(TEXT("Shared debug info provider disabled"), Info.bProviderDisabled);
	TestTrue(TEXT("Shared debug info gather family members"), Info.bGatherFamilyMembers);
	TestEqual(TEXT("Shared debug info key object name"), Info.KeyObjectName, KeyObject->GetName());
	TestEqual(TEXT("Shared debug info member slot count"), Info.NumMemberSlots, 1);
	if (GroupTag.IsValid())
	{
		TestTrue(TEXT("Shared debug info group tag"), Info.GroupTag == GroupTag);
	}
	else
	{
		TestFalse(TEXT("Shared debug info group tag remains invalid"), Info.GroupTag.IsValid());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldProviderAliveByLastFrameTest,
                                 "KawaiiPhysics.SimpleWorld.ProviderAliveByLastFrame",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldProviderAliveByLastFrameTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderID = 0xFFFF1001;
	constexpr uint64 CurrentFrame = 100;
	constexpr uint64 MaxAge = 10;

	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	TestFalse(TEXT("Provider-less entry is not alive"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(Entry, CurrentFrame, MaxAge));

	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	Entry.SetDesc(ProviderID, Desc, CurrentFrame, TWeakObjectPtr<const USkeletalMeshComponent>(), true);
	TestTrue(TEXT("SetDesc marks provider alive"),
	         KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(Entry, CurrentFrame, MaxAge));
	TestTrue(TEXT("Provider is alive at the max-age boundary"),
	         KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(Entry, CurrentFrame + MaxAge, MaxAge));
	TestFalse(TEXT("Provider expires after the max-age boundary"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(Entry, CurrentFrame + MaxAge + 1, MaxAge));

	Entry.RemoveDesc(ProviderID);
	TestFalse(TEXT("Removed provider is not alive"),
	          KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldProviderAlive(Entry, CurrentFrame, MaxAge));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSharedSourceUsesReaderKeyTest,
                                 "KawaiiPhysics.SimpleWorld.SharedSourceUsesReaderKey",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSharedSourceUsesReaderKeyTest::RunTest(const FString& Parameters)
{
	constexpr uint64 ProviderID = 0xFFFF1003;
	USkeletalMeshComponent* SkelComp =
		NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> LocalEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SharedEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();

	FKawaiiPhysicsSharedPublisherState State = MakeSimpleWorldReaderState(false);
	SharedEntry->SetDesc(ProviderID, State.SimpleWorldDesc, GFrameCounter,
	                     TWeakObjectPtr<const USkeletalMeshComponent>(), true);

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	Accessor.SetSimpleWorldOwnSkelComp(SkelComp);
	Accessor.SetSimpleWorldCollisionSharedTag(TAG_KawaiiPhysicsSimpleWorldRegistryY);
	Accessor.SetSimpleWorldLocalEntryForAuto(LocalEntry);
	Accessor.SetSimpleWorldSharedEntryForAuto(SharedEntry);
	Accessor.SetSimpleWorldCollisionSource(EKawaiiPhysicsSimpleWorldCollisionSource::Shared);

	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);

	Accessor.InitializeSimpleWorldCollision();
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestTrue(TEXT("Shared source uses reader mode"), Accessor.IsSimpleWorldReaderMode());
	TestTrue(TEXT("Shared reader key is a shared key"), Accessor.GetSimpleWorldReaderKey().Tag.IsValid());
	TestTrue(TEXT("Shared reader key keeps the configured tag"),
	         Accessor.GetSimpleWorldReaderKey().Tag == TAG_KawaiiPhysicsSimpleWorldRegistryY);

	Accessor.SetSimpleWorldCollisionSource(EKawaiiPhysicsSimpleWorldCollisionSource::Local);
	Accessor.InitializeSimpleWorldCollision();
	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestFalse(TEXT("Local source returns to provider mode"), Accessor.IsSimpleWorldReaderMode());
	TestEqual(TEXT("Resolved source returns to Local"),
	          Accessor.GetSimpleWorldResolvedSource(), EKawaiiPhysicsSimpleWorldCollisionSource::Local);

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
	TestEqual(TEXT("Disabled provider clears spheres"), Accessor.GetSimpleWorldSphericalLimits().Num(), 0);
	TestEqual(TEXT("Disabled provider clears capsules"), Accessor.GetSimpleWorldCapsuleLimits().Num(), 0);
	TestEqual(TEXT("Disabled provider clears boxes"), Accessor.GetSimpleWorldBoxLimits().Num(), 0);
	TestEqual(TEXT("Disabled provider clears ground"), Accessor.GetSimpleWorldGroundBoxLimits().Num(), 0);
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

	AddExpectedError(TEXT("Shared Simple World Collision entry has no provider"),
	                 EAutomationExpectedErrorFlags::Contains, 1);
	for (int32 Attempt = 0; Attempt < 60; ++Attempt)
	{
		Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	}
	TestTrue(TEXT("Reader logs the no-provider warning once after repeated retries"),
	         Accessor.IsSimpleWorldReaderWarningLogged());

	Accessor.UpdateSimpleWorldCollisionLimits(PoseContext);
	TestTrue(TEXT("Reader warning flag remains set"),
	         Accessor.IsSimpleWorldReaderWarningLogged());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldSharedReaderSkipsRadiusCheckTest,
                                 "KawaiiPhysics.SimpleWorld.SharedReaderSkipsRadiusCheck",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldSharedReaderSkipsRadiusCheckTest::RunTest(const FString& Parameters)
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

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldRadiusWarningOnceTest,
                                 "KawaiiPhysics.SimpleWorld.RadiusWarningOnce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldRadiusWarningOnceTest::RunTest(const FString& Parameters)
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

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSimpleWorldRadiusCheckDeferralBoundedTest,
                                 "KawaiiPhysics.SimpleWorld.RadiusCheckDeferralBounded",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSimpleWorldRadiusCheckDeferralBoundedTest::RunTest(const FString& Parameters)
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
		TestEqual(FString::Printf(TEXT("Deferral counter after attempt %d"), Attempt),
		          static_cast<int32>(Accessor.GetSimpleWorldRadiusCheckDeferrals()), static_cast<int32>(Attempt));
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

	// bUseSimpleWorldCollision = false、Convex注入: 適用条件のゲート確認。
	{
		const FVector ConvexStart(5.8f, 0.0f, -8.0f);
		const FVector ExpectedNoConvexPushOut =
			ConvexStart.GetSafeNormal() * 10.0f;
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
		TArray<FKawaiiPhysicsConvexLimit> ConvexLimits = {Convex};

		A.SetSimpleWorldLimits(EmptySpheres, EmptyCapsules, EmptyTaperedCapsules, EmptyBoxes, ConvexLimits);
		A.Node.bUseSimpleWorldCollision = false;

		A.StepFrame(1.0f / 60.0f);

		TestTrue(FString::Printf(TEXT("Gated off: SimpleWorld convex does not push the bone: got %s expected %s"),
		                         *A.Bone(1).Location.ToString(), *ExpectedNoConvexPushOut.ToString()),
		         A.Bone(1).Location.Equals(ExpectedNoConvexPushOut, GSimpleWorldPushOutTol));
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
			64,
			false,
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
			64,
			false,
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
