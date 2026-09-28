// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"

namespace
{
	FKawaiiPhysicsSharedCollisionData MakeSharedCollisionSlotSphericalData(int32 Count = 1)
	{
		FKawaiiPhysicsSharedCollisionData Data;
		Data.SphericalLimits.AddDefaulted(Count);
		return Data;
	}

	FKawaiiPhysicsSharedCollisionData MakeSharedCollisionSlotFullData()
	{
		FKawaiiPhysicsSharedCollisionData Data = MakeSharedCollisionSlotSphericalData();
		Data.CapsuleLimits.AddDefaulted();
		Data.TaperedCapsuleLimits.AddDefaulted();
		Data.BoxLimits.AddDefaulted();
		Data.PlanarLimits.AddDefaulted();
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
		FKawaiiPhysicsSharedCollisionData PublishData = MakeSharedCollisionSlotFullData();
		Slot.Publish(PublishData);
		FKawaiiPhysicsSharedCollisionData OutData;
		Slot.AppendTo(OutData);
		TestEqual(TEXT("Published sphere count"), OutData.SphericalLimits.Num(), 1);
		TestEqual(TEXT("Published capsule count"), OutData.CapsuleLimits.Num(), 1);
		TestEqual(TEXT("Published tapered capsule count"), OutData.TaperedCapsuleLimits.Num(), 1);
		TestEqual(TEXT("Published box count"), OutData.BoxLimits.Num(), 1);
		TestEqual(TEXT("Published plane count"), OutData.PlanarLimits.Num(), 1);
	}

	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		FKawaiiPhysicsSharedCollisionData FirstData = MakeSharedCollisionSlotFullData();
		Slot.Publish(FirstData);
		FKawaiiPhysicsSharedCollisionData SecondData = MakeSharedCollisionSlotSphericalData(2);
		Slot.Publish(SecondData);
		FKawaiiPhysicsSharedCollisionData OutData;
		Slot.AppendTo(OutData);
		TestEqual(TEXT("Second publish replaces spherical data"), OutData.SphericalLimits.Num(), 2);
		TestEqual(TEXT("Second publish removes old capsule data"), OutData.CapsuleLimits.Num(), 0);
		TestEqual(TEXT("Second publish removes old tapered capsule data"), OutData.TaperedCapsuleLimits.Num(), 0);
		TestEqual(TEXT("Second publish removes old box data"), OutData.BoxLimits.Num(), 0);
		TestEqual(TEXT("Second publish removes old planar data"), OutData.PlanarLimits.Num(), 0);
	}

	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		FKawaiiPhysicsSharedCollisionData PublishData = MakeSharedCollisionSlotSphericalData();
		Slot.Publish(PublishData);
		FKawaiiPhysicsSharedCollisionData OutData;
		Slot.AppendTo(OutData);
		Slot.AppendTo(OutData);
		TestEqual(TEXT("AppendTo does not reset output data"), OutData.SphericalLimits.Num(), 2);
	}

	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		FKawaiiPhysicsSharedCollisionData PublishData = MakeSharedCollisionSlotSphericalData();
		Slot.Publish(PublishData);
		const uint64 CurrentFrame = GFrameCounter;
		TestFalse(TEXT("Recently published slot is not expired"), Slot.IsExpired(CurrentFrame, 1));
		TestFalse(TEXT("MaxAge boundary is inclusive"), Slot.IsExpired(CurrentFrame + 1, 1));
		TestTrue(TEXT("Slot expires after MaxAge"), Slot.IsExpired(CurrentFrame + 10, 1));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
