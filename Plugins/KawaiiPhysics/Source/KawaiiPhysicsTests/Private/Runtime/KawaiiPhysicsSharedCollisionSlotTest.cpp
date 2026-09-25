// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"

namespace
{
	constexpr float GSharedCollisionSlotTol = 0.001f;

	FSphericalLimit MakeSphere(float Base)
	{
		FSphericalLimit Limit;
		Limit.Location = FVector(Base, Base + 1.0f, Base + 2.0f);
		Limit.Radius = Base + 3.0f;
		Limit.LimitType = ESphericalLimitType::Outer;
		Limit.bEnable = true;
		return Limit;
	}

	FKawaiiPhysicsSharedCollisionData MakeSphericalData(float Base, int32 Count = 1)
	{
		FKawaiiPhysicsSharedCollisionData Data;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Data.SphericalLimits.Add(MakeSphere(Base + static_cast<float>(Index) * 10.0f));
		}
		return Data;
	}

	FKawaiiPhysicsSharedCollisionData MakeFullData(float Base)
	{
		FKawaiiPhysicsSharedCollisionData Data = MakeSphericalData(Base);

		FCapsuleLimit Capsule;
		Capsule.Location = FVector(Base + 10.0f, Base + 11.0f, Base + 12.0f);
		Capsule.Radius = Base + 13.0f;
		Capsule.Length = Base + 14.0f;
		Capsule.bEnable = true;
		Data.CapsuleLimits.Add(Capsule);

		FTaperedCapsuleLimit TaperedCapsule;
		TaperedCapsule.Location = FVector(Base + 40.0f, Base + 41.0f, Base + 42.0f);
		TaperedCapsule.Radius0 = Base + 43.0f;
		TaperedCapsule.Radius1 = Base + 44.0f;
		TaperedCapsule.Length = Base + 45.0f;
		TaperedCapsule.bEnable = true;
		Data.TaperedCapsuleLimits.Add(TaperedCapsule);

		FBoxLimit Box;
		Box.Location = FVector(Base + 20.0f, Base + 21.0f, Base + 22.0f);
		Box.Extent = FVector(Base + 23.0f, Base + 24.0f, Base + 25.0f);
		Box.bEnable = true;
		Data.BoxLimits.Add(Box);

		FPlanarLimit Planar;
		Planar.Location = FVector(Base + 30.0f, Base + 31.0f, Base + 32.0f);
		Planar.Plane = FPlane(0.0f, 0.0f, 1.0f, Base + 33.0f);
		Planar.bEnable = true;
		Data.PlanarLimits.Add(Planar);

		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionSourceSlotTest,
                                 "KawaiiPhysics.SharedCollision.SourceSlot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionSourceSlotTest::RunTest(const FString& Parameters)
{
	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		FKawaiiPhysicsSharedCollisionData OutData;

		TestTrue(TEXT("Default slot is expired"), Slot.IsExpired(GFrameCounter, 1));
		Slot.AppendTo(OutData);
		TestTrue(TEXT("Default slot appends no data"), OutData.IsEmpty());
	}

	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		// Publishは非const参照を取りBufferとSwapする（呼び出し側で旧Bufferを受け取る）ため、lvalueを渡す
		FKawaiiPhysicsSharedCollisionData PublishData = MakeFullData(10.0f);
		Slot.Publish(PublishData);

		FKawaiiPhysicsSharedCollisionData OutData;
		Slot.AppendTo(OutData);

		TestTrue(TEXT("Published data appends one sphere"), OutData.SphericalLimits.Num() == 1);
		TestTrue(TEXT("Published data appends one capsule"), OutData.CapsuleLimits.Num() == 1);
		TestTrue(TEXT("Published data appends one tapered capsule"), OutData.TaperedCapsuleLimits.Num() == 1);
		TestTrue(TEXT("Published data appends one box"), OutData.BoxLimits.Num() == 1);
		TestTrue(TEXT("Published data appends one plane"), OutData.PlanarLimits.Num() == 1);
		TestTrue(TEXT("Sphere location is preserved"),
		         OutData.SphericalLimits[0].Location.Equals(FVector(10.0f, 11.0f, 12.0f), GSharedCollisionSlotTol));
		TestTrue(TEXT("Sphere radius is preserved"),
		         FMath::IsNearlyEqual(OutData.SphericalLimits[0].Radius, 13.0f, GSharedCollisionSlotTol));
		TestTrue(TEXT("Capsule location is preserved"),
		         OutData.CapsuleLimits[0].Location.Equals(FVector(20.0f, 21.0f, 22.0f), GSharedCollisionSlotTol));
		TestTrue(TEXT("Capsule radius is preserved"),
		         FMath::IsNearlyEqual(OutData.CapsuleLimits[0].Radius, 23.0f, GSharedCollisionSlotTol));
		TestTrue(TEXT("Tapered capsule location is preserved"),
		         OutData.TaperedCapsuleLimits[0].Location.Equals(FVector(50.0f, 51.0f, 52.0f),
		                                                          GSharedCollisionSlotTol));
		TestTrue(TEXT("Tapered capsule Radius0 is preserved"),
		         FMath::IsNearlyEqual(OutData.TaperedCapsuleLimits[0].Radius0, 53.0f,
		                              GSharedCollisionSlotTol));
		TestTrue(TEXT("Tapered capsule Radius1 is preserved"),
		         FMath::IsNearlyEqual(OutData.TaperedCapsuleLimits[0].Radius1, 54.0f,
		                              GSharedCollisionSlotTol));
		TestTrue(TEXT("Tapered capsule length is preserved"),
		         FMath::IsNearlyEqual(OutData.TaperedCapsuleLimits[0].Length, 55.0f,
		                              GSharedCollisionSlotTol));
		TestTrue(TEXT("Box extent is preserved"),
		         OutData.BoxLimits[0].Extent.Equals(FVector(33.0f, 34.0f, 35.0f), GSharedCollisionSlotTol));
		TestTrue(TEXT("Planar limit is preserved"),
		         FMath::IsNearlyEqual(OutData.PlanarLimits[0].Plane.W, 43.0f, GSharedCollisionSlotTol));
	}

	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		FKawaiiPhysicsSharedCollisionData FirstData = MakeFullData(1.0f);
		Slot.Publish(FirstData);
		FKawaiiPhysicsSharedCollisionData SecondData = MakeSphericalData(50.0f, 2);
		Slot.Publish(SecondData);

		FKawaiiPhysicsSharedCollisionData OutData;
		Slot.AppendTo(OutData);

		TestTrue(TEXT("Second publish replaces spherical data"), OutData.SphericalLimits.Num() == 2);
		TestTrue(TEXT("Second publish removes old capsule data"), OutData.CapsuleLimits.Num() == 0);
		TestTrue(TEXT("Second publish removes old tapered capsule data"), OutData.TaperedCapsuleLimits.Num() == 0);
		TestTrue(TEXT("Second publish removes old box data"), OutData.BoxLimits.Num() == 0);
		TestTrue(TEXT("Second publish removes old planar data"), OutData.PlanarLimits.Num() == 0);
		TestTrue(TEXT("Second publish first sphere radius"),
		         FMath::IsNearlyEqual(OutData.SphericalLimits[0].Radius, 53.0f, GSharedCollisionSlotTol));
		TestTrue(TEXT("Second publish second sphere radius"),
		         FMath::IsNearlyEqual(OutData.SphericalLimits[1].Radius, 63.0f, GSharedCollisionSlotTol));
	}

	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		FKawaiiPhysicsSharedCollisionData PublishData = MakeSphericalData(100.0f);
		Slot.Publish(PublishData);

		FKawaiiPhysicsSharedCollisionData OutData;
		Slot.AppendTo(OutData);
		Slot.AppendTo(OutData);

		TestTrue(TEXT("AppendTo does not reset output data"), OutData.SphericalLimits.Num() == 2);
		TestTrue(TEXT("AppendTo preserves appended values"),
		         FMath::IsNearlyEqual(OutData.SphericalLimits[0].Radius, 103.0f, GSharedCollisionSlotTol)
		         && FMath::IsNearlyEqual(OutData.SphericalLimits[1].Radius, 103.0f, GSharedCollisionSlotTol));
	}

	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		FKawaiiPhysicsSharedCollisionData PublishData = MakeSphericalData(200.0f);
		Slot.Publish(PublishData);

		const uint64 CurrentFrame = GFrameCounter;
		TestTrue(TEXT("Recently published slot is not expired"),
		         !Slot.IsExpired(CurrentFrame, 1));
		TestTrue(TEXT("MaxAge boundary is inclusive"),
		         !Slot.IsExpired(CurrentFrame + 1, 1));
		TestTrue(TEXT("Slot expires after MaxAge"),
		         Slot.IsExpired(CurrentFrame + 10, 1));
	}

	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		FKawaiiPhysicsSharedCollisionData PublishData = MakeSphericalData(300.0f);
		Slot.Publish(PublishData);
		Slot.MarkExpired();

		TestTrue(TEXT("MarkExpired makes the slot expired"), Slot.IsExpired(GFrameCounter, 1));
	}

	// Publishのswap契約を検証: 入力とBufferをSwapするため、Publish後は入力側に旧Bufferが戻る。
	// （内部copy実装に退行すると入力側が空/旧値にならず、ここで検出できる）
	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;

		FKawaiiPhysicsSharedCollisionData First = MakeSphericalData(400.0f); // 球1個（半径403）
		Slot.Publish(First);
		// 1回目: Bufferは初期状態（空）だったので、Publish後の入力側は空になる
		TestTrue(TEXT("First publish swaps the empty old buffer back into input"), First.IsEmpty());

		FKawaiiPhysicsSharedCollisionData Second = MakeSphericalData(500.0f, 3); // 球3個
		Slot.Publish(Second);
		// 2回目: 直前にpublishしたFirstのデータ（球1個・半径403）が入力側へ戻る
		TestTrue(TEXT("Second publish returns the previously published buffer"),
		         Second.SphericalLimits.Num() == 1);
		TestTrue(TEXT("Returned buffer holds the first published sphere"),
		         FMath::IsNearlyEqual(Second.SphericalLimits[0].Radius, 403.0f, GSharedCollisionSlotTol));

		// 最新のpublish結果（球3個）がslotに残っていることも確認
		FKawaiiPhysicsSharedCollisionData OutData;
		Slot.AppendTo(OutData);
		TestTrue(TEXT("Latest published data has three spheres"), OutData.SphericalLimits.Num() == 3);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionSlotPublishSerialTest,
                                 "KawaiiPhysics.SharedCollision.SlotPublishSerial",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionSlotPublishSerialTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedCollisionSourceSlot Slot;

	TestEqual(TEXT("Initial publish serial is zero"), Slot.GetPublishSerial(), static_cast<uint64>(0));

	FKawaiiPhysicsSharedCollisionData FirstData = MakeSphericalData(10.0f);
	Slot.Publish(FirstData);
	TestEqual(TEXT("First Publish increments serial"), Slot.GetPublishSerial(), static_cast<uint64>(1));

	FKawaiiPhysicsSharedCollisionData OutData;
	Slot.AppendTo(OutData);
	TestEqual(TEXT("AppendTo keeps serial unchanged"), Slot.GetPublishSerial(), static_cast<uint64>(1));

	Slot.IsExpired(GFrameCounter, 1);
	TestEqual(TEXT("IsExpired keeps serial unchanged"), Slot.GetPublishSerial(), static_cast<uint64>(1));

	Slot.MarkExpired();
	TestEqual(TEXT("MarkExpired keeps serial unchanged"), Slot.GetPublishSerial(), static_cast<uint64>(1));

	FKawaiiPhysicsSharedCollisionData SecondData = MakeSphericalData(20.0f);
	Slot.Publish(SecondData);
	TestEqual(TEXT("Second Publish increments serial"), Slot.GetPublishSerial(), static_cast<uint64>(2));

	FKawaiiPhysicsSharedCollisionData ThirdData = MakeSphericalData(30.0f);
	Slot.Publish(ThirdData);
	TestEqual(TEXT("Third Publish increments serial"), Slot.GetPublishSerial(), static_cast<uint64>(3));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
