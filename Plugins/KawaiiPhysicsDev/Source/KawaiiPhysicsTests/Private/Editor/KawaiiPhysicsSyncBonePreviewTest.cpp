// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsSyncBonePreview.h"
#include "Misc/AutomationTest.h"

namespace KawaiiPhysicsSyncBonePreviewTestPrivate
{
FKawaiiPhysicsSyncTargetRoot MakeSyncBonePreviewTestTargetRoot(const FName BoneName)
{
	FKawaiiPhysicsSyncTargetRoot TargetRoot;
	TargetRoot.Bone.BoneName = BoneName;
	return TargetRoot;
}

// 実行時の TargetRoot に、InitSyncBone が作るプレビュー値を入れる
FKawaiiPhysicsSyncTargetRoot MakeSyncBonePreviewTestRuntimeRoot(const FName BoneName, const int32 ModifyBoneIndex,
                                                                const int32 NumChildren)
{
	FKawaiiPhysicsSyncTargetRoot TargetRoot = MakeSyncBonePreviewTestTargetRoot(BoneName);
	TargetRoot.ModifyBoneIndex = ModifyBoneIndex;
	TargetRoot.LengthRateFromSyncTargetRoot = 0.25f;
	TargetRoot.ScaleByLengthRateCurve = 0.5f;
	TargetRoot.TranslationBySyncBone = FVector(1.0, 2.0, 3.0);
	for (int32 Child = 0; Child < NumChildren; ++Child)
	{
		TargetRoot.ChildTargets.Add(FKawaiiPhysicsSyncTarget(ModifyBoneIndex + Child + 1));
	}
	return TargetRoot;
}

// 実行時は無効なターゲットを取り除くが、プレビュー値の同期でグラフノードの設定を消さない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSyncBonePreviewKeepsSettingsTest,
                                 "KawaiiPhysics.Editor.SyncBonePreview.KeepsSettings",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSyncBonePreviewKeepsSettingsTest::RunTest(const FString& Parameters)
{
	// グラフノード：skirt_a、プレビューメッシュに無い skirt_lod、同名の skirt_a（2つ目）
	TArray<FKawaiiPhysicsSyncBone> GraphSyncBones;
	FKawaiiPhysicsSyncBone& GraphSyncBone = GraphSyncBones.AddDefaulted_GetRef();
	GraphSyncBone.Bone.BoneName = TEXT("thigh_l");
	GraphSyncBone.GlobalScale = FVector(0.5);
	GraphSyncBone.TargetRoots.Add(MakeSyncBonePreviewTestTargetRoot(TEXT("skirt_a")));
	GraphSyncBone.TargetRoots.Add(MakeSyncBonePreviewTestTargetRoot(TEXT("skirt_lod")));
	GraphSyncBone.TargetRoots.Add(MakeSyncBonePreviewTestTargetRoot(TEXT("skirt_a")));
	GraphSyncBone.TargetRoots[1].bIncludeChildBones = false;
	// 前回の同期で入った古いプレビュー値
	GraphSyncBone.TargetRoots[1].ChildTargets.Add(FKawaiiPhysicsSyncTarget(7));
	GraphSyncBone.TargetRoots[1].ModifyBoneIndex = 7;

	// 実行時のノード：skirt_lod は無効として取り除かれている
	TArray<FKawaiiPhysicsSyncBone> RuntimeSyncBones;
	FKawaiiPhysicsSyncBone& RuntimeSyncBone = RuntimeSyncBones.AddDefaulted_GetRef();
	RuntimeSyncBone.Bone.BoneName = TEXT("thigh_l");
	RuntimeSyncBone.GlobalScale = FVector(0.5);
	RuntimeSyncBone.InitialPoseLocation = FVector(0.0, 10.0, 90.0);
	RuntimeSyncBone.TargetRoots.Add(MakeSyncBonePreviewTestRuntimeRoot(TEXT("skirt_a"), 2, 3));
	RuntimeSyncBone.TargetRoots.Add(MakeSyncBonePreviewTestRuntimeRoot(TEXT("skirt_a"), 10, 1));

	KawaiiPhysicsSyncBonePreview::CopyPreviewValues(GraphSyncBones, RuntimeSyncBones);

	const FKawaiiPhysicsSyncBone& Result = GraphSyncBones[0];
	bool bOk = TestEqual(TEXT("no target root is removed"), Result.TargetRoots.Num(), 3);
	if (Result.TargetRoots.Num() != 3)
	{
		return false;
	}
	bOk &= TestEqual(TEXT("the target missing at runtime keeps its bone"),
		Result.TargetRoots[1].Bone.BoneName, FName(TEXT("skirt_lod")));
	bOk &= TestFalse(TEXT("the target missing at runtime keeps its settings"), Result.TargetRoots[1].bIncludeChildBones);
	bOk &= TestEqual(TEXT("stale child targets are cleared"), Result.TargetRoots[1].ChildTargets.Num(), 0);
	bOk &= TestEqual(TEXT("stale modify bone index is cleared"), Result.TargetRoots[1].ModifyBoneIndex, -1);

	// 同名の重複は出現順で対応付ける
	bOk &= TestEqual(TEXT("first skirt_a gets the first runtime preview"), Result.TargetRoots[0].ChildTargets.Num(), 3);
	bOk &= TestEqual(TEXT("first skirt_a modify bone index"), Result.TargetRoots[0].ModifyBoneIndex, 2);
	bOk &= TestEqual(TEXT("second skirt_a gets the second runtime preview"), Result.TargetRoots[2].ChildTargets.Num(), 1);
	bOk &= TestEqual(TEXT("second skirt_a modify bone index"), Result.TargetRoots[2].ModifyBoneIndex, 10);
	bOk &= TestEqual(TEXT("translation preview is copied"),
		Result.TargetRoots[0].TranslationBySyncBone, FVector(1.0, 2.0, 3.0));
	bOk &= TestEqual(TEXT("length rate preview is copied"), Result.TargetRoots[0].LengthRateFromSyncTargetRoot, 0.25f);

	bOk &= TestEqual(TEXT("sync bone preview is copied"), Result.InitialPoseLocation, FVector(0.0, 10.0, 90.0));
	bOk &= TestEqual(TEXT("sync bone settings are kept"), Result.GlobalScale, FVector(0.5));
	return bOk;
}

// 実行時のノードに無い SyncBone（同名が足りない場合も）はプレビュー値だけクリアし、要素は残す
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSyncBonePreviewUnmatchedSyncBoneTest,
                                 "KawaiiPhysics.Editor.SyncBonePreview.UnmatchedSyncBone",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSyncBonePreviewUnmatchedSyncBoneTest::RunTest(const FString& Parameters)
{
	TArray<FKawaiiPhysicsSyncBone> GraphSyncBones;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FKawaiiPhysicsSyncBone& SyncBone = GraphSyncBones.AddDefaulted_GetRef();
		SyncBone.Bone.BoneName = TEXT("thigh_l");
		SyncBone.InitialPoseLocation = FVector(5.0);
		SyncBone.TargetRoots.Add(MakeSyncBonePreviewTestTargetRoot(TEXT("skirt_a")));
		SyncBone.TargetRoots[0].ChildTargets.Add(FKawaiiPhysicsSyncTarget(4));
	}

	TArray<FKawaiiPhysicsSyncBone> RuntimeSyncBones;
	FKawaiiPhysicsSyncBone& RuntimeSyncBone = RuntimeSyncBones.AddDefaulted_GetRef();
	RuntimeSyncBone.Bone.BoneName = TEXT("thigh_l");
	RuntimeSyncBone.InitialPoseLocation = FVector(1.0);
	RuntimeSyncBone.TargetRoots.Add(MakeSyncBonePreviewTestRuntimeRoot(TEXT("skirt_a"), 3, 2));

	KawaiiPhysicsSyncBonePreview::CopyPreviewValues(GraphSyncBones, RuntimeSyncBones);

	bool bOk = TestEqual(TEXT("no sync bone is removed"), GraphSyncBones.Num(), 2);
	bOk &= TestEqual(TEXT("first sync bone is matched"), GraphSyncBones[0].InitialPoseLocation, FVector(1.0));
	bOk &= TestEqual(TEXT("first sync bone gets child targets"), GraphSyncBones[0].TargetRoots[0].ChildTargets.Num(), 2);
	bOk &= TestEqual(TEXT("unmatched sync bone preview is cleared"), GraphSyncBones[1].InitialPoseLocation, FVector::ZeroVector);
	bOk &= TestEqual(TEXT("unmatched sync bone keeps its target roots"), GraphSyncBones[1].TargetRoots.Num(), 1);
	bOk &= TestEqual(TEXT("unmatched sync bone child targets are cleared"),
		GraphSyncBones[1].TargetRoots[0].ChildTargets.Num(), 0);
	return bOk;
}

} // namespace KawaiiPhysicsSyncBonePreviewTestPrivate

#endif
