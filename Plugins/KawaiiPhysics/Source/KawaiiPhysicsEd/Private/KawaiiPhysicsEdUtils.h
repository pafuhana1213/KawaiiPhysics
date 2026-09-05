// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_KawaiiPhysicsSharedPublisher.h"
#include "AnimationGraph.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "EdGraph/EdGraph.h"

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

	// AnimBlueprint 内の AnimGraph を列挙する
	inline void CollectAnimGraphs(const UAnimBlueprint* AnimBlueprint, TArray<UAnimationGraph*>& OutGraphs)
	{
		OutGraphs.Reset();
		if (!AnimBlueprint)
		{
			return;
		}

		auto AddGraph = [&OutGraphs](UEdGraph* Graph)
		{
			if (UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph))
			{
				OutGraphs.AddUnique(AnimGraph);
			}
		};

		for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			AddGraph(Graph);
		}
		for (UEdGraph* Graph : AnimBlueprint->UbergraphPages)
		{
			AddGraph(Graph);
		}

		TArray<UEdGraph*> AllGraphs;
		AnimBlueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			AddGraph(Graph);
		}
	}

	template<typename TGraphNode>
	inline void CollectAnimGraphNodes(const UAnimBlueprint* AnimBlueprint, TArray<TGraphNode*>& OutNodes)
	{
		OutNodes.Reset();

		TArray<UAnimationGraph*> Graphs;
		CollectAnimGraphs(AnimBlueprint, Graphs);
		for (UAnimationGraph* Graph : Graphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (TGraphNode* GraphNode = Cast<TGraphNode>(Node))
				{
					OutNodes.Add(GraphNode);
				}
			}
		}
	}

	// デバッグ対象がある場合だけ実行中の AnimNode を返す
	template<typename TGraphNode, typename TAnimNode>
	inline TAnimNode* ResolveLiveAnimNode(const TGraphNode* GraphNode)
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

		return GraphNode->template GetDebuggedAnimNode<TAnimNode>();
	}

	// デバッグ対象がある場合だけ実行中の KawaiiPhysics ノードを返す
	inline FAnimNode_KawaiiPhysics* ResolveLiveKawaiiPhysicsNode(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		return ResolveLiveAnimNode<UAnimGraphNode_KawaiiPhysics, FAnimNode_KawaiiPhysics>(GraphNode);
	}

	inline FAnimNode_KawaiiPhysicsSharedPublisher* ResolveLiveSharedPublisherNode(
		const UAnimGraphNode_KawaiiPhysicsSharedPublisher* GraphNode)
	{
		return ResolveLiveAnimNode<UAnimGraphNode_KawaiiPhysicsSharedPublisher, FAnimNode_KawaiiPhysicsSharedPublisher>(
			GraphNode);
	}

	inline UAnimGraphNode_KawaiiPhysicsSharedPublisher* FindSharedPublisherGraphNodeByTag(
		const UAnimBlueprint* AnimBlueprint,
		const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return nullptr;
		}

		TArray<UAnimGraphNode_KawaiiPhysicsSharedPublisher*> PublisherNodes;
		CollectAnimGraphNodes(AnimBlueprint, PublisherNodes);
		for (UAnimGraphNode_KawaiiPhysicsSharedPublisher* PublisherNode : PublisherNodes)
		{
			if (PublisherNode && PublisherNode->Node.SharedGroupTag == Tag)
			{
				return PublisherNode;
			}
		}
		return nullptr;
	}

	inline void FindKawaiiPhysicsConsumerGraphNodes(
		const UAnimBlueprint* AnimBlueprint,
		const FGameplayTag& Tag,
		TArray<UAnimGraphNode_KawaiiPhysics*>& OutConsumers)
	{
		OutConsumers.Reset();
		if (!Tag.IsValid())
		{
			return;
		}

		TArray<UAnimGraphNode_KawaiiPhysics*> KawaiiPhysicsNodes;
		CollectAnimGraphNodes(AnimBlueprint, KawaiiPhysicsNodes);
		for (UAnimGraphNode_KawaiiPhysics* KawaiiPhysicsNode : KawaiiPhysicsNodes)
		{
			if (KawaiiPhysicsNode &&
				KawaiiPhysicsNode->Node.bUseSimpleWorldCollision &&
				KawaiiPhysicsNode->Node.SimpleWorldCollisionSource != EKawaiiPhysicsSimpleWorldCollisionSource::Local &&
				KawaiiPhysicsNode->Node.SimpleWorldCollisionSharedTag == Tag)
			{
				OutConsumers.Add(KawaiiPhysicsNode);
			}
		}
	}
}
