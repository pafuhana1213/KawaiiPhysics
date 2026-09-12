// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Animation/MirrorDataTable.h"
#include "BoneContainer.h"
#include "UObject/ObjectKey.h"

#if WITH_EDITOR

/** Immutable snapshot: copied anim nodes may share it, but never mutate one another's cache. */
struct FKawaiiPhysicsMirrorTableCache
{
	struct FInputs
	{
		const UMirrorDataTable& Table;
		const USkeleton& Skeleton;
		const FReferenceSkeleton& MeshRefSkeleton;
		const FBoneContainer* BoneContainer = nullptr;
		uint16 BoneContainerSerial = 0;
		const UObject* MeshAsset = nullptr;
	};

	explicit FKawaiiPhysicsMirrorTableCache(const FInputs& Inputs);
	bool Matches(const FInputs& Inputs) const;
	SIZE_T GetAllocatedSize() const;

	TCustomBoneIndexArray<FSkeletonPoseBoneIndex, FSkeletonPoseBoneIndex> MirrorBoneIndexes;
	TArray<FQuat> CSRefRotations;

private:
	struct FMeshBoneSnapshot
	{
		FName Name;
		int32 ParentIndex;
		FQuat Rotation;
	};

	FObjectKey TableKey;
	FObjectKey SkeletonKey;
	FObjectKey MeshAssetKey;
	const FReferenceSkeleton* SkeletonRefIdentity;
	const FReferenceSkeleton* MeshRefIdentity;
	const FBoneContainer* BoneContainerIdentity;
	uint16 BoneContainerSerial;
	EAxis::Type Axis;
	TArray<FMirrorTableRow> Rows;
	TArray<FName> SkeletonBoneNames;
	TArray<FMeshBoneSnapshot> MeshBones;
};

#endif // WITH_EDITOR
