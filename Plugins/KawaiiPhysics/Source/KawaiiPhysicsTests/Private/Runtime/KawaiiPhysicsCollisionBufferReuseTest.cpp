// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsCollisionBuffer.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	FKawaiiPhysicsConvexLimit MakeReuseConvex(double X, int32 Planes = 6)
	{
		FKawaiiPhysicsConvexLimit Result;
		Result.Location = FVector(X, 0, 0);
		Result.LocalBounds = FBox(FVector(-10), FVector(10));
		Result.SourceType = ECollisionSourceType::SimpleWorld;
		for (int32 Index = 0; Index < Planes; ++Index)
		{
			Result.LocalPlanes.Add(FPlane(FVector::XAxisVector, X + Index));
		}
#if !UE_BUILD_SHIPPING
		Result.LocalVertices = { FVector(-10), FVector(10) };
		Result.LocalEdges = { 0, 1 };
#endif
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsConvexReadReuseTest,
	"KawaiiPhysics.SimpleWorld.ConvexBuffer.ReadReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsConvexReadReuseTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::WorldSpace);
	Accessor.BuildVerticalChain(4, 10.0f);
	Accessor.SetSimpleWorldEntry(Entry);
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Pose(&Proxy);
	FKawaiiPhysicsSharedCollisionData PublishData, Snapshot;
	PublishData.ConvexLimits = { MakeReuseConvex(1), MakeReuseConvex(2) };
	Entry->Slot.Publish(PublishData);
	Entry->Slot.CopyTo(Snapshot);
	Accessor.UpdateSimpleWorldCollisionLimits(Pose);

	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		PublishData.ConvexLimits = { MakeReuseConvex(20 + Frame), MakeReuseConvex(10 + Frame) };
		Entry->Slot.Publish(PublishData);
		Entry->Slot.CopyTo(Snapshot);
		Accessor.UpdateSimpleWorldCollisionLimits(Pose);
		TestEqual(TEXT("Reordered shape data overwrites the reused slot"), Snapshot.ConvexLimits[0].Location.X, 20.0 + Frame);
		TestEqual(TEXT("Changed planes overwrite simulation data"), Accessor.GetSimpleWorldConvexLimits()[0].LocalPlanes[0].W, 20.0 + Frame);
	}
	PublishData.ConvexLimits = { MakeReuseConvex(99, 12) };
	Entry->Slot.Publish(PublishData);
	Accessor.UpdateSimpleWorldCollisionLimits(Pose);
	TestEqual(TEXT("Topology shrink removes old shapes"), Accessor.GetSimpleWorldConvexLimits().Num(), 1);
	TestEqual(TEXT("Topology growth copies every new plane"), Accessor.GetSimpleWorldConvexLimits()[0].LocalPlanes.Num(), 12);
	PublishData.Reset();
	Entry->Slot.Publish(PublishData);
	Entry->Slot.CopyTo(Snapshot);
	Accessor.UpdateSimpleWorldCollisionLimits(Pose);
	TestTrue(TEXT("Empty snapshot releases all nested elements"), Snapshot.ConvexLimits.IsEmpty());
	TestTrue(TEXT("Empty publication clears the simulation"), Accessor.GetSimpleWorldConvexLimits().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsConvexPublishReuseTest,
	"KawaiiPhysics.SimpleWorld.ConvexBuffer.PublishReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsConvexPublishReuseTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	USkeletalMeshComponent* Member = NewObject<USkeletalMeshComponent>();
	const TWeakObjectPtr<const USkeletalMeshComponent> Key(Member);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		auto& Component = Entry.GatheredComponents.AddDefaulted_GetRef();
		Component.FadeAlpha = 1;
		Component.LastComponentTM = FTransform(FVector(100, 0, 0));
		Component.LocalLimits.ConvexLimits.Add(MakeReuseConvex(Index));
		if (Index != 0) { Component.MemberSkelComp = Member; }
	}
	// 公開用バッファの両側を使い、同じメンバーからの複数形状を反映する。
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldShapeLimits(Entry, 0.5f);
	}
	for (int32 Frame = 0; Frame < 2; ++Frame)
	{
		UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldShapeLimits(Entry, 0.5f);
	}
	FKawaiiPhysicsSharedCollisionData Snapshot;
	Entry.CopyShapeLimits(nullptr, Snapshot, true);
	TestEqual(TEXT("Merged snapshot contains main and both member contributions"), Snapshot.ConvexLimits.Num(), 3);
	TestEqual(TEXT("Transform applied directly to copied shape"), Snapshot.ConvexLimits[0].Location.X, 100.0);
	Entry.CopyShapeLimits(Key, Snapshot, true);
	TestEqual(TEXT("Reader excludes its own member shapes"), Snapshot.ConvexLimits.Num(), 1);

	Entry.GatheredComponents.SetNum(1);
	UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldShapeLimits(Entry, 0.5f);
	Entry.CopyShapeLimits(nullptr, Snapshot, true);
	TestEqual(TEXT("Departed member has no visible shapes"), Snapshot.ConvexLimits.Num(), 1);
	Entry.GatheredComponents[0].FadeAlpha = 0;
	UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldShapeLimits(Entry, 0.5f);
	Entry.CopyShapeLimits(nullptr, Snapshot, true);
	TestTrue(TEXT("Below-threshold convexes are removed"), Snapshot.ConvexLimits.IsEmpty());
	return true;
}
#endif
