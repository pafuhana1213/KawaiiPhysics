// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Components/SkeletalMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMergedDescCacheTest,
	"KawaiiPhysics.SimpleWorld.MergedDescCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 設定変更と降格・昇格・期限切れ後のマージ結果、登録順、キャッシュ安定性を守る。
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
	const uint64 RebuildCountBeforeHeartbeats = Entry.GetMergedDescRebuildCount();
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
		Entry.GetMergedDescRebuildCount(), RebuildCountBeforeHeartbeats);
	Entry.RemoveReaderMember(20);
	Entry.ConsumeRegatherRequested();

	First.GatherIntervalSec = 0.1f;
	Entry.SetDesc(900, First, 201, NoMesh);
	TestFalse(TEXT("Interval-only changes preserve gathered shapes"), Entry.ConsumeRegatherRequested());
	Entry.BuildMergedDesc(Merged);
	TestEqual(TEXT("Interval change is visible immediately"), Merged.GatherIntervalSec, 0.1f);
	TestEqual(TEXT("Changing settings preserves registration priority"), Merged.CollisionChannel,
		TEnumAsByte<ECollisionChannel>(ECC_Visibility));

	// メッシュが変わらない降格でもマージ結果を更新する。
	Entry.AddReaderMember(900, NoMesh, 202);
	Entry.BuildMergedDesc(Merged);
	TestEqual(TEXT("Demoted provider no longer supplies collision channel"), Merged.CollisionChannel,
		TEnumAsByte<ECollisionChannel>(ECC_Pawn));
	TestFalse(TEXT("Demoted provider no longer enables family gathering"), Merged.bGatherFamilyMembers);

	Entry.SetDesc(900, First, 203, NoMesh, true);
	Entry.BuildMergedDesc(Merged);
	TestEqual(TEXT("Promoted source retains original registration priority"), Merged.CollisionChannel,
		TEnumAsByte<ECollisionChannel>(ECC_Visibility));
	Entry.SetDesc(900, First, 204, NoMesh, false);
	FKawaiiPhysicsSimpleWorldCollisionDesc ReaderOnly = First;
	ReaderOnly.CollisionChannel = ECC_Camera;
	Entry.SetDesc(900, ReaderOnly, 205, NoMesh, false);
	TestTrue(TEXT("Reader settings leave a provider result"), Entry.BuildMergedDesc(Merged));
	TestEqual(TEXT("Reader description does not override provider collision channel"), Merged.CollisionChannel,
		TEnumAsByte<ECollisionChannel>(ECC_Pawn));

	Entry.RemoveDesc(10);
	TestFalse(TEXT("Reader-only entry has no merged provider description"), Entry.BuildMergedDesc(Merged));
	Entry.RemoveDesc(10);
	Entry.SetDesc(10, Second, 300, NoMesh);
	Entry.AddReaderMember(30, NoMesh, 299);
	Entry.RemoveExpiredDescs(360, 60);
	TestTrue(TEXT("Expiring readers leaves the provider result"), Entry.BuildMergedDesc(Merged));
	TestEqual(TEXT("Provider channel survives reader expiration"), Merged.CollisionChannel,
		TEnumAsByte<ECollisionChannel>(ECC_Pawn));
	Entry.AddReaderMember(30, NoMesh, 361);
	Entry.RemoveExpiredDescs(361, 60);
	TestFalse(TEXT("Expired provider does not leave cached settings visible to readers"), Entry.BuildMergedDesc(Merged));

	// 同じメッシュを reader に降格しても、family slot と provider の結果を消す。
	FKawaiiPhysicsSimpleWorldCollisionEntry MemberEntry;
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	const TWeakObjectPtr<const USkeletalMeshComponent> MeshKey(Mesh);
	FKawaiiPhysicsSimpleWorldCollisionDesc MemberDesc;
	MemberDesc.bGatherFamilyMembers = true;
	MemberEntry.SetDesc(100, MemberDesc, 100, MeshKey);
	MemberEntry.MemberSlots.Add(MeshKey, MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>());
	MemberEntry.AddReaderMember(100, MeshKey, 101);
	TestEqual(TEXT("Demotion with unchanged mesh clears family slots when no provider remains"),
		MemberEntry.GetNumMemberSlots(), 0);
	FKawaiiPhysicsSimpleWorldCollisionDesc MemberMerged;
	TestFalse(TEXT("Demotion removes the cached provider result"), MemberEntry.BuildMergedDesc(MemberMerged));
	return true;
}

#endif
