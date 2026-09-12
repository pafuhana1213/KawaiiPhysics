// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Components/SkeletalMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMergedDescCacheTest,
	"KawaiiPhysics.SimpleWorld.MergedDescCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMergedDescCacheTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	const TWeakObjectPtr<const USkeletalMeshComponent> NoMesh;
	FKawaiiPhysicsSimpleWorldCollisionDesc First;
	First.CollisionChannel = ECC_Visibility;
	First.bGatherFamilyMembers = true;
	FKawaiiPhysicsSimpleWorldCollisionDesc Second;
	Second.CollisionChannel = ECC_Pawn;
	FKawaiiPhysicsSimpleWorldCollisionDesc Merged;
	TestFalse(TEXT("Empty entry has no merged provider description"), Entry.BuildMergedDesc(Merged));
	Entry.SetDesc(900, First, 100, NoMesh);
	Entry.SetDesc(10, Second, 100, NoMesh);
	TestEqual(TEXT("Each new provider rebuilds once"), Entry.GetMergedDescRebuildCount(), uint64(2));
	TestTrue(TEXT("Two providers have a merged result"), Entry.BuildMergedDesc(Merged));
	TestTrue(TEXT("Cache preserves registration order"), Merged ==
		FKawaiiPhysicsSimpleWorldCollisionDesc::Merge({First, Second}));

	for (uint64 Frame = 101; Frame <= 200; ++Frame)
	{
		Entry.SetDesc(900, First, Frame, NoMesh);
		Entry.MarkRead(10, Frame);
		Entry.AddReaderMember(20, NoMesh, Frame);
		Entry.MarkReaderRead(20, Frame, 60);
		Entry.RemoveExpiredDescs(Frame, 60);
		Entry.BuildMergedDesc(Merged);
	}
	TestEqual(TEXT("Unchanged settings, readers, heartbeats and cache reads never rebuild"),
		Entry.GetMergedDescRebuildCount(), uint64(2));
	Entry.RemoveReaderMember(20);
	TestEqual(TEXT("Removing a reader never rebuilds provider settings"), Entry.GetMergedDescRebuildCount(), uint64(2));
	Entry.ConsumeRegatherRequested();

	First.GatherIntervalSec = 0.1f;
	Entry.SetDesc(900, First, 201, NoMesh);
	TestEqual(TEXT("Interval change rebuilds the cached result once"), Entry.GetMergedDescRebuildCount(), uint64(3));
	TestFalse(TEXT("Interval-only changes preserve gathered shapes"), Entry.ConsumeRegatherRequested());
	Entry.BuildMergedDesc(Merged);
	TestEqual(TEXT("Interval change is visible immediately"), Merged.GatherIntervalSec, 0.1f);
	TestEqual(TEXT("Changing settings preserves registration priority"), Merged.CollisionChannel,
		TEnumAsByte<ECollisionChannel>(ECC_Visibility));

	// Demotion must invalidate even if the mesh is unchanged (including an empty weak pointer).
	Entry.AddReaderMember(900, NoMesh, 202);
	TestEqual(TEXT("Provider demotion through AddReaderMember rebuilds once"),
		Entry.GetMergedDescRebuildCount(), uint64(4));
	Entry.BuildMergedDesc(Merged);
	TestEqual(TEXT("Demoted provider no longer supplies collision channel"), Merged.CollisionChannel,
		TEnumAsByte<ECollisionChannel>(ECC_Pawn));
	TestFalse(TEXT("Demoted provider no longer enables family gathering"), Merged.bGatherFamilyMembers);

	Entry.SetDesc(900, First, 203, NoMesh, true);
	TestEqual(TEXT("Promotion rebuilds once"), Entry.GetMergedDescRebuildCount(), uint64(5));
	Entry.BuildMergedDesc(Merged);
	TestEqual(TEXT("Promoted source retains original registration priority"), Merged.CollisionChannel,
		TEnumAsByte<ECollisionChannel>(ECC_Visibility));
	Entry.SetDesc(900, First, 204, NoMesh, false);
	TestEqual(TEXT("SetDesc demotion also rebuilds once"), Entry.GetMergedDescRebuildCount(), uint64(6));
	Entry.SetDesc(900, Second, 205, NoMesh, false);
	TestEqual(TEXT("Reader settings do not rebuild provider cache"), Entry.GetMergedDescRebuildCount(), uint64(6));

	Entry.RemoveDesc(10);
	TestEqual(TEXT("Removing the last provider rebuilds once"), Entry.GetMergedDescRebuildCount(), uint64(7));
	TestFalse(TEXT("Reader-only entry has no merged provider description"), Entry.BuildMergedDesc(Merged));
	Entry.RemoveDesc(10);
	TestEqual(TEXT("Removing an absent provider never rebuilds"), Entry.GetMergedDescRebuildCount(), uint64(7));
	Entry.SetDesc(10, Second, 300, NoMesh);
	Entry.AddReaderMember(30, NoMesh, 299);
	Entry.RemoveExpiredDescs(360, 60);
	TestEqual(TEXT("Expiring readers does not rebuild provider cache"), Entry.GetMergedDescRebuildCount(), uint64(8));
	Entry.AddReaderMember(30, NoMesh, 361);
	Entry.RemoveExpiredDescs(361, 60);
	TestEqual(TEXT("Expiring a provider rebuilds once"), Entry.GetMergedDescRebuildCount(), uint64(9));
	TestFalse(TEXT("Expired provider does not leave cached settings visible to readers"), Entry.BuildMergedDesc(Merged));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMergedDescDemotionMemberTest,
	"KawaiiPhysics.SimpleWorld.MergedDescCache.DemotionMembers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMergedDescDemotionMemberTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSimpleWorldCollisionEntry Entry;
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	const TWeakObjectPtr<const USkeletalMeshComponent> MeshKey(Mesh);
	FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
	Desc.bGatherFamilyMembers = true;
	Entry.SetDesc(100, Desc, 100, MeshKey);
	Entry.MemberSlots.Add(MeshKey, MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>());
	Entry.AddReaderMember(100, MeshKey, 101);
	TestEqual(TEXT("Demotion with unchanged mesh clears family slots when no provider remains"),
		Entry.GetNumMemberSlots(), 0);
	FKawaiiPhysicsSimpleWorldCollisionDesc Merged;
	TestFalse(TEXT("Demotion removes the cached provider result"), Entry.BuildMergedDesc(Merged));
	return true;
}

#endif
