// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "ExternalForces/KawaiiPhysicsExternalForce.h"

// Shared by the production traversal and lifetime regression fixtures. The getter supports
// FInstancedStruct in the node and owning polymorphic pointers in tests without reflected test types.
struct FKawaiiPhysicsExternalForcePostApply
{
	template <typename ArrayType, typename GetForceType>
	static void Apply(ArrayType& Forces, FAnimNode_KawaiiPhysics& Node,
	                  FComponentSpacePoseContext& PoseContext, GetForceType GetForce)
	{
		for (int32 Index = 0; Index < Forces.Num(); ++Index)
		{
			FKawaiiPhysics_ExternalForce* Force = GetForce(Forces[Index]);
			if (!Force)
			{
				continue;
			}

			Force->bRemovalRequestedAfterPostApply = false;
			Force->PostApply(Node, PoseContext);

			// A third-party callback may remove itself or move the array. Resolve ownership
			// again before touching the force; the normal path needs no array search.
			int32 ForceIndex = Index;
			if (!Forces.IsValidIndex(Index) || GetForce(Forces[Index]) != Force)
			{
				ForceIndex = INDEX_NONE;
				for (int32 CandidateIndex = 0; CandidateIndex < Forces.Num(); ++CandidateIndex)
				{
					if (GetForce(Forces[CandidateIndex]) == Force)
					{
						ForceIndex = CandidateIndex;
						break;
					}
				}
			}
			if (ForceIndex != INDEX_NONE && Force->bRemovalRequestedAfterPostApply)
			{
				Forces.RemoveAt(ForceIndex);
				// Resume at the removed force's current index: the callback may have removed
				// preceding elements as well. Callbacks must not grow the work list indefinitely.
				if (ForceIndex <= Index)
				{
					Index = ForceIndex - 1;
				}
			}
		}
	}

	static void Apply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext)
	{
		Apply(Node.ExternalForces, Node, PoseContext, [](FInstancedStruct& Item)
		{
			return Item.GetMutablePtr<FKawaiiPhysics_ExternalForce>();
		});
	}
};
