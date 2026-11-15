// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

namespace KawaiiPhysicsSyncBoneInitTestPrivate
{
// グラフノードから実行時のノードへコピーされた直後の SyncBones は、TargetRoot のボーン参照が未初期化
// （BoneIndex は UPROPERTY ではないのでコピーで運ばれない）。PIE 中はボーン参照の一括初期化が走らないので、
// InitSyncBone 自身が参照を初期化しないと、存在するボーンのターゲットまで無効として取り除かれる
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSyncBoneInitUninitializedTargetTest,
                                 "KawaiiPhysics.SyncBone.InitKeepsUninitializedTargets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSyncBoneInitUninitializedTargetTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>(GetTransientPackage()));
	{
		FReferenceSkeletonModifier Modifier(Skeleton.Get());
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("thigh"), TEXT("thigh"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("skirt_a"), TEXT("skirt_a"), 0), FTransform::Identity);
	}
	FBoneContainer Container;
	TArray<FBoneIndexType> Indices{0, 1, 2};
	Container.InitializeTo(Indices, UE::Anim::FCurveFilterSettings(), *Skeleton);

	// グラフノードからコピーされた状態：名前だけで、ボーン番号は未初期化
	FKawaiiPhysicsSyncBone SyncBone;
	SyncBone.Bone = FBoneReference(TEXT("thigh"));
	SyncBone.TargetRoots.AddDefaulted_GetRef().Bone = FBoneReference(TEXT("skirt_a"));
	SyncBone.TargetRoots.AddDefaulted_GetRef().Bone = FBoneReference(TEXT("skirt_missing"));

	FKawaiiPhysicsTestAccessor A;
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);
	A.CallInitSyncBone(Output, Container, SyncBone);

	bool bOk = TestEqual(TEXT("only the target missing from the mesh is removed"), SyncBone.TargetRoots.Num(), 1);
	bOk &= TestTrue(TEXT("the existing target is kept"),
		SyncBone.TargetRoots.Num() == 1 && SyncBone.TargetRoots[0].Bone.BoneName == FName(TEXT("skirt_a")));
	return bOk;
}

} // namespace KawaiiPhysicsSyncBoneInitTestPrivate

#endif
