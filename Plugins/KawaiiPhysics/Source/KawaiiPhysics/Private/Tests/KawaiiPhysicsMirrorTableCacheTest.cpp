// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "KawaiiPhysicsMirrorTableCache.h"
#include "KawaiiPhysicsMemoryTraceRegion.h"
#include "KawaiiPhysicsMirrorUtils.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Animation/Skeleton.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FMirrorCacheFixture
	{
		TStrongObjectPtr<USkeleton> Skeleton{ NewObject<USkeleton>() };
		TStrongObjectPtr<UMirrorDataTable> Table{ NewObject<UMirrorDataTable>() };
		FReferenceSkeleton MeshRef;
		FBoneContainer Container;

		explicit FMirrorCacheFixture(int32 Pairs = 2)
		{
			Table->RowStruct = FMirrorTableRow::StaticStruct();
			{
				FReferenceSkeletonModifier Modifier(Skeleton.Get());
				Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
				for (int32 Index = 0; Index < Pairs; ++Index)
				{
					const FString Left = FString::Printf(TEXT("bone_%d_l"), Index);
					const FString Right = FString::Printf(TEXT("bone_%d_r"), Index);
					const int32 Parent = Index == 0 ? 0 : 2 * Index - 1;
					Modifier.Add(FMeshBoneInfo(FName(*Left), Left, Parent), FTransform(FRotator(5, Index, 10)));
					Modifier.Add(FMeshBoneInfo(FName(*Right), Right, Parent), FTransform(FRotator(-5, -Index, 10)));
					FMirrorTableRow Row;
					Row.Name = FName(*Left);
					Row.MirroredName = FName(*Right);
					Table->AddRow(Row.Name, Row);
				}
			}
			MeshRef = Skeleton->GetReferenceSkeleton();
		}

		FKawaiiPhysicsMirrorTableCache::FInputs Inputs() const
		{
			return { *Table, *Skeleton, MeshRef, &Container, 1, Skeleton.Get() };
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorTableCacheInvalidationTest,
	"KawaiiPhysics.Mirror.TableCache.Invalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorTableCacheInvalidationTest::RunTest(const FString& Parameters)
{
	FMirrorCacheFixture Fixture;
	const FKawaiiPhysicsMirrorTableCache Cache(Fixture.Inputs());
	TestTrue(TEXT("Unchanged tables reuse their snapshot"), Cache.Matches(Fixture.Inputs()));

	TCustomBoneIndexArray<FSkeletonPoseBoneIndex, FSkeletonPoseBoneIndex> ExpectedIndexes;
	TArray<FQuat> ExpectedRotations;
	Fixture.Table->FillMirrorBoneIndexes(Fixture.Skeleton.Get(), ExpectedIndexes);
	KawaiiPhysicsMirrorUtils::BuildComponentSpaceRefRotations(Fixture.MeshRef, ExpectedRotations);
	TestEqual(TEXT("Mirror index count matches the uncached engine output"), Cache.MirrorBoneIndexes.Num(), ExpectedIndexes.Num());
	for (int32 Index = 0; Index < ExpectedIndexes.Num(); ++Index)
	{
		TestEqual(TEXT("Mirror indexes are unchanged"), Cache.MirrorBoneIndexes[FSkeletonPoseBoneIndex(Index)].GetInt(),
			ExpectedIndexes[FSkeletonPoseBoneIndex(Index)].GetInt());
		TestTrue(TEXT("Reference rotations are unchanged"), Cache.CSRefRotations[Index] == ExpectedRotations[Index]);
	}

	Fixture.Table->MirrorAxis = EAxis::Y;
	TestFalse(TEXT("Axis edit invalidates"), Cache.Matches(Fixture.Inputs()));
	Fixture.Table->MirrorAxis = EAxis::X;
	TestTrue(TEXT("Axis undo restores valid inputs"), Cache.Matches(Fixture.Inputs()));
	FMirrorTableRow* EditedRow = Fixture.Table->FindRow<FMirrorTableRow>(TEXT("bone_0_l"), TEXT("Mirror cache test"));
	const FName SavedMirror = EditedRow->MirroredName;
	EditedRow->MirroredName = TEXT("bone_1_r");
	TestFalse(TEXT("In-place row edits invalidate without a notification"), Cache.Matches(Fixture.Inputs()));
	EditedRow->MirroredName = SavedMirror;
	TestTrue(TEXT("Row undo restores valid inputs"), Cache.Matches(Fixture.Inputs()));
	EditedRow->MirrorEntryType = EMirrorRowType::Curve;
	TestFalse(TEXT("Bone row changing to a curve invalidates"), Cache.Matches(Fixture.Inputs()));
	EditedRow->MirrorEntryType = EMirrorRowType::Bone;
	FMirrorTableRow ExtraRow;
	ExtraRow.Name = TEXT("root");
	ExtraRow.MirroredName = TEXT("root");
	Fixture.Table->AddRow(TEXT("root"), ExtraRow);
	TestFalse(TEXT("Added rows invalidate"), Cache.Matches(Fixture.Inputs()));
	Fixture.Table->RemoveRow(TEXT("root"));
	TestTrue(TEXT("Removing an added row restores the old inputs"), Cache.Matches(Fixture.Inputs()));

	const FTransform OriginalPose = Fixture.MeshRef.GetRefBonePose()[1];
	{
		FReferenceSkeletonModifier Modifier(Fixture.MeshRef, Fixture.Skeleton.Get());
		Modifier.UpdateRefPoseTransform(1, FTransform(FRotator(40, 20, 10)));
	}
	TestFalse(TEXT("Reference rotation edits invalidate without a notification"), Cache.Matches(Fixture.Inputs()));
	{
		FReferenceSkeletonModifier Modifier(Fixture.MeshRef, Fixture.Skeleton.Get());
		Modifier.UpdateRefPoseTransform(1, OriginalPose);
	}
	TestTrue(TEXT("Reference pose undo restores valid inputs"), Cache.Matches(Fixture.Inputs()));

	auto ChangedSerial = Fixture.Inputs();
	++ChangedSerial.BoneContainerSerial;
	TestFalse(TEXT("LOD bone-container serial invalidates"), Cache.Matches(ChangedSerial));
	FBoneContainer OtherContainer;
	auto ChangedContainer = Fixture.Inputs();
	ChangedContainer.BoneContainer = &OtherContainer;
	TestFalse(TEXT("Bone-container replacement invalidates"), Cache.Matches(ChangedContainer));
	FReferenceSkeleton OtherMeshRef = Fixture.MeshRef;
	FKawaiiPhysicsMirrorTableCache::FInputs ChangedMeshRef{
		*Fixture.Table, *Fixture.Skeleton, OtherMeshRef, &Fixture.Container, 1, Fixture.Skeleton.Get() };
	TestFalse(TEXT("Mesh reference-skeleton replacement invalidates"), Cache.Matches(ChangedMeshRef));

	FMirrorCacheFixture OtherFixture;
	FKawaiiPhysicsMirrorTableCache::FInputs ChangedTable{
		*OtherFixture.Table, *Fixture.Skeleton, Fixture.MeshRef, &Fixture.Container, 1, Fixture.Skeleton.Get() };
	TestFalse(TEXT("Identical replacement table invalidates"), Cache.Matches(ChangedTable));
	FKawaiiPhysicsMirrorTableCache::FInputs ChangedSkeleton{
		*Fixture.Table, *OtherFixture.Skeleton, Fixture.MeshRef, &Fixture.Container, 1, Fixture.Skeleton.Get() };
	TestFalse(TEXT("Identical replacement skeleton invalidates"), Cache.Matches(ChangedSkeleton));
	auto ChangedAsset = Fixture.Inputs();
	ChangedAsset.MeshAsset = OtherFixture.Skeleton.Get();
	TestFalse(TEXT("Mesh asset replacement invalidates"), Cache.Matches(ChangedAsset));

	{
		FReferenceSkeletonModifier Modifier(Fixture.MeshRef, Fixture.Skeleton.Get());
		Modifier.SetParent(TEXT("bone_1_l"), TEXT("root"));
	}
	TestFalse(TEXT("Mesh hierarchy edits invalidate"), Cache.Matches(Fixture.Inputs()));
	const FKawaiiPhysicsMirrorTableCache BeforeRename(Fixture.Inputs());
	{
		FReferenceSkeletonModifier Modifier(Fixture.Skeleton.Get());
		Modifier.Rename(TEXT("bone_1_r"), TEXT("renamed_r"));
	}
	TestFalse(TEXT("Skeleton bone rename invalidates its index mapping"), BeforeRename.Matches(Fixture.Inputs()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorTableCacheLiveLimitsTest,
	"KawaiiPhysics.Mirror.TableCache.LiveLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorTableCacheLiveLimitsTest::RunTest(const FString& Parameters)
{
	FMirrorCacheFixture Fixture;
	TArray<FBoneIndexType> RequiredIndexes;
	for (int32 Index = 0; Index < Fixture.Skeleton->GetReferenceSkeleton().GetNum(); ++Index)
	{
		RequiredIndexes.Add(static_cast<FBoneIndexType>(Index));
	}
	Fixture.Container.InitializeTo(RequiredIndexes, UE::Anim::FCurveFilterSettings(), *Fixture.Skeleton);
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.MirrorDataTableForLimits = Fixture.Table.Get();
	FSphericalLimit Sphere;
	Sphere.DrivingBone = FBoneReference(TEXT("bone_0_l"));
	Sphere.Radius = 7;
	Accessor.Node.SphericalLimits.Add(Sphere);
	Accessor.ApplyMirrorLimits(Fixture.Container);
	TestNull(TEXT("Preview does not retain mirror tables"), Accessor.GetMirrorTableCache());
	TestEqual(TEXT("Preview generates the mirrored limit"), Accessor.Node.SphericalLimitsData.Num(), 1);
	Accessor.SetMirrorTableCacheForPIE(true);
	Accessor.ApplyMirrorLimits(Fixture.Container);
	const FKawaiiPhysicsMirrorTableCache* Cache = Accessor.GetMirrorTableCache();
	TestNotNull(TEXT("PIE retains skeleton-derived tables"), Cache);
	Accessor.Node.SphericalLimits[0].Radius = 19;
	Accessor.Node.SphericalLimits[0].OffsetLocation = FVector(3, 4, 5);
	Accessor.ApplyMirrorLimits(Fixture.Container);
	TestTrue(TEXT("A limit edit reuses unchanged skeleton tables"), Accessor.GetMirrorTableCache() == Cache);
	TestEqual(TEXT("Re-evaluation replaces the mirror instead of duplicating it"), Accessor.Node.SphericalLimitsData.Num(), 1);
	TestEqual(TEXT("PIE mirrors see in-place radius edits immediately"), Accessor.Node.SphericalLimitsData[0].Radius, 19.0f);
	Accessor.Node.SphericalLimits.Reset();
	Accessor.ApplyMirrorLimits(Fixture.Container);
	TestEqual(TEXT("Removing an input limit removes its mirrored output"), Accessor.Node.SphericalLimitsData.Num(), 0);
	Accessor.Node.MirrorDataTableForLimits = nullptr;
	Accessor.ApplyMirrorLimits(Fixture.Container);
	TestNull(TEXT("Removing the mirror table releases its retained cache"), Accessor.GetMirrorTableCache());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorTableCacheCopyIsolationTest,
	"KawaiiPhysics.Mirror.TableCache.CopyIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorTableCacheCopyIsolationTest::RunTest(const FString& Parameters)
{
	FMirrorCacheFixture Fixture;
	TSharedPtr<const FKawaiiPhysicsMirrorTableCache, ESPMode::ThreadSafe> Original =
		MakeShared<FKawaiiPhysicsMirrorTableCache, ESPMode::ThreadSafe>(Fixture.Inputs());
	auto Copy = Original;
	const int32 OriginalMirror = Original->MirrorBoneIndexes[FSkeletonPoseBoneIndex(1)].GetInt();
	FMirrorTableRow* Row = Fixture.Table->FindRow<FMirrorTableRow>(TEXT("bone_0_l"), TEXT("Mirror cache test"));
	Row->MirroredName = TEXT("bone_1_r");
	TestFalse(TEXT("Both copies see that their immutable snapshot is out of date"), Copy->Matches(Fixture.Inputs()));
	Copy = MakeShared<FKawaiiPhysicsMirrorTableCache, ESPMode::ThreadSafe>(Fixture.Inputs());
	TestTrue(TEXT("A node rebuild gets its own snapshot"), Copy.Get() != Original.Get());
	TestEqual(TEXT("Rebuilding a copy never changes the original cached indexes"),
		Original->MirrorBoneIndexes[FSkeletonPoseBoneIndex(1)].GetInt(), OriginalMirror);
	TestTrue(TEXT("Rebuilt snapshot matches edited inputs"), Copy->Matches(Fixture.Inputs()));
	TestFalse(TEXT("Old snapshot still requires a rebuild"), Original->Matches(Fixture.Inputs()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorTableCachePerfTest,
	"KawaiiPhysics.Perf.MirrorTableCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

bool FKawaiiPhysicsMirrorTableCachePerfTest::RunTest(const FString& Parameters)
{
	FMirrorCacheFixture Fixture(80);
	const auto Inputs = Fixture.Inputs();
	constexpr int32 Iterations = 10000;
	int64 Sink = 0;
	for (int32 Trial = 0; Trial < 5; ++Trial)
	{
		FKawaiiPhysicsMemoryTraceRegion RebuildMemory(TEXT("MirrorTables.Uncached"), Trial + 1);
		RebuildMemory.Warmup();
		const double RebuildStart = FPlatformTime::Seconds();
		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			TCustomBoneIndexArray<FSkeletonPoseBoneIndex, FSkeletonPoseBoneIndex> Indexes;
			TArray<FQuat> Rotations;
			Fixture.Table->FillMirrorBoneIndexes(Fixture.Skeleton.Get(), Indexes);
			KawaiiPhysicsMirrorUtils::BuildComponentSpaceRefRotations(Fixture.MeshRef, Rotations);
			Sink += Indexes[FSkeletonPoseBoneIndex(1)].GetInt() + Rotations.Num();
		}
		const double RebuildSeconds = FPlatformTime::Seconds() - RebuildStart;
		RebuildMemory.End();
		FKawaiiPhysicsMemoryTraceRegion CachedMemory(TEXT("MirrorTables.Cached"), Trial + 1);
		const FKawaiiPhysicsMirrorTableCache Cache(Inputs);
		CachedMemory.Warmup();
		const double CachedStart = FPlatformTime::Seconds();
		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			Sink += Cache.Matches(Inputs) ? Cache.MirrorBoneIndexes[FSkeletonPoseBoneIndex(1)].GetInt()
				+ Cache.CSRefRotations.Num() : 0;
		}
		const double CachedSeconds = FPlatformTime::Seconds() - CachedStart;
		CachedMemory.End();
		AddInfo(FString::Printf(TEXT("MIRROR_TABLE_CACHE trial=%d bones=%d rebuild_us=%.3f validate_us=%.3f retained_bytes=%llu"),
			Trial + 1, Fixture.MeshRef.GetNum(), RebuildSeconds * 1.e6 / Iterations,
			CachedSeconds * 1.e6 / Iterations, static_cast<uint64>(sizeof(Cache) + Cache.GetAllocatedSize())));
	}
	TestTrue(TEXT("Both benchmark paths consumed their results"), Sink > 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
