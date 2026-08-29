// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"

namespace KawaiiPhysicsEdUtils
{
	inline const UScriptStruct* GetExternalForceScriptStruct(const FInstancedStruct& InstancedStruct)
	{
		return InstancedStruct.IsValid() ? InstancedStruct.GetScriptStruct() : nullptr;
	}

	// ExternalForces の要素数と各インデックスの型が一致しているかを判定する。両方無効な要素は同じ形状として扱う
	inline bool IsExternalForceShapeMatched(const TArray<FInstancedStruct>& A,
	                                        const TArray<FInstancedStruct>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (GetExternalForceScriptStruct(A[Index]) != GetExternalForceScriptStruct(B[Index]))
			{
				return false;
			}
		}
		return true;
	}

	// デバッグ対象がある場合だけ実行中の KawaiiPhysics ノードを返す
	inline FAnimNode_KawaiiPhysics* ResolveLiveKawaiiPhysicsNode(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		if (!GraphNode)
		{
			return nullptr;
		}

		UAnimBlueprint* AnimBlueprint = GraphNode->GetAnimBlueprint();
		UObject* ObjectBeingDebugged = AnimBlueprint ? AnimBlueprint->GetObjectBeingDebugged() : nullptr;
		if (!Cast<UAnimInstance>(ObjectBeingDebugged))
		{
			return nullptr;
		}

		return GraphNode->GetDebuggedAnimNode<FAnimNode_KawaiiPhysics>();
	}
}
