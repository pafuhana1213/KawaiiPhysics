// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsMirrorTableCache.h"

#if WITH_EDITOR

#include "Animation/Skeleton.h"
#include "KawaiiPhysicsMirrorUtils.h"

FKawaiiPhysicsMirrorTableCache::FKawaiiPhysicsMirrorTableCache(const FInputs& Inputs)
	: TableKey(&Inputs.Table)
	, SkeletonKey(&Inputs.Skeleton)
	, MeshAssetKey(Inputs.MeshAsset)
	, SkeletonRefIdentity(&Inputs.Skeleton.GetReferenceSkeleton())
	, MeshRefIdentity(&Inputs.MeshRefSkeleton)
	, BoneContainerIdentity(Inputs.BoneContainer)
	, BoneContainerSerial(Inputs.BoneContainerSerial)
	, Axis(Inputs.Table.MirrorAxis)
{
	Inputs.Table.ForeachRow<FMirrorTableRow>(TEXT("KawaiiPhysicsMirrorTableCache"),
		[this](FName, const FMirrorTableRow& Row) { Rows.Add(Row); });
	const FReferenceSkeleton& SkeletonRef = Inputs.Skeleton.GetReferenceSkeleton();
	SkeletonBoneNames.Reserve(SkeletonRef.GetNum());
	for (int32 Index = 0; Index < SkeletonRef.GetNum(); ++Index)
	{
		SkeletonBoneNames.Add(SkeletonRef.GetBoneName(Index));
	}
	MeshBones.Reserve(Inputs.MeshRefSkeleton.GetNum());
	for (int32 Index = 0; Index < Inputs.MeshRefSkeleton.GetNum(); ++Index)
	{
		MeshBones.Add({ Inputs.MeshRefSkeleton.GetBoneName(Index), Inputs.MeshRefSkeleton.GetParentIndex(Index),
			Inputs.MeshRefSkeleton.GetRefBonePose()[Index].GetRotation() });
	}
	Inputs.Table.FillMirrorBoneIndexes(&Inputs.Skeleton, MirrorBoneIndexes);
	KawaiiPhysicsMirrorUtils::BuildComponentSpaceRefRotations(Inputs.MeshRefSkeleton, CSRefRotations);
}

bool FKawaiiPhysicsMirrorTableCache::Matches(const FInputs& Inputs) const
{
	const FReferenceSkeleton& SkeletonRef = Inputs.Skeleton.GetReferenceSkeleton();
	if (TableKey != FObjectKey(&Inputs.Table) || SkeletonKey != FObjectKey(&Inputs.Skeleton)
		|| MeshAssetKey != FObjectKey(Inputs.MeshAsset) || Axis != Inputs.Table.MirrorAxis
		|| SkeletonRefIdentity != &SkeletonRef || MeshRefIdentity != &Inputs.MeshRefSkeleton
		|| BoneContainerIdentity != Inputs.BoneContainer || BoneContainerSerial != Inputs.BoneContainerSerial
		|| Rows.Num() != Inputs.Table.GetRowMap().Num()
		|| SkeletonBoneNames.Num() != SkeletonRef.GetNum() || MeshBones.Num() != Inputs.MeshRefSkeleton.GetNum())
	{
		return false;
	}

	// Compare the inputs themselves: editor transactions and in-place edits need not emit a property delegate.
	// Preserve row iteration order because duplicate source names are resolved in that order by the engine.
	bool bRowsMatch = true;
	int32 RowIndex = 0;
	Inputs.Table.ForeachRow<FMirrorTableRow>(TEXT("KawaiiPhysicsMirrorTableCache"),
		[this, &bRowsMatch, &RowIndex](FName, const FMirrorTableRow& Row)
		{
			bRowsMatch &= Rows.IsValidIndex(RowIndex) && Rows[RowIndex] == Row;
			++RowIndex;
		});
	if (!bRowsMatch || RowIndex != Rows.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < SkeletonBoneNames.Num(); ++Index)
	{
		if (SkeletonBoneNames[Index] != SkeletonRef.GetBoneName(Index))
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < MeshBones.Num(); ++Index)
	{
		const FMeshBoneSnapshot& Bone = MeshBones[Index];
		if (Bone.Name != Inputs.MeshRefSkeleton.GetBoneName(Index)
			|| Bone.ParentIndex != Inputs.MeshRefSkeleton.GetParentIndex(Index)
			|| Bone.Rotation != Inputs.MeshRefSkeleton.GetRefBonePose()[Index].GetRotation())
		{
			return false;
		}
	}
	return true;
}

SIZE_T FKawaiiPhysicsMirrorTableCache::GetAllocatedSize() const
{
	return MirrorBoneIndexes.GetAllocatedSize() + CSRefRotations.GetAllocatedSize() + Rows.GetAllocatedSize()
		+ SkeletonBoneNames.GetAllocatedSize() + MeshBones.GetAllocatedSize();
}

#endif // WITH_EDITOR
