// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_KawaiiPhysicsSharedPublisher.h"
#include "Animation/AnimBlueprint.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "KawaiiPhysicsEdUtils.h"
#include "Misc/Optional.h"

enum class EKawaiiPhysicsWindScopeTargetKind : uint8
{
	KawaiiPhysicsNode,
	SharedPublisherNode,
};

struct FKawaiiPhysicsWindScopeTarget
{
	EKawaiiPhysicsWindScopeTargetKind Kind = EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode;
	TWeakObjectPtr<UAnimGraphNode_Base> GraphNode;
	int32 ExternalForceIndex = INDEX_NONE;

	static FKawaiiPhysicsWindScopeTarget MakeKawaiiPhysicsNode(
		UAnimGraphNode_KawaiiPhysics* InNode,
		int32 InExternalForceIndex)
	{
		FKawaiiPhysicsWindScopeTarget Target;
		Target.Kind = EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode;
		Target.GraphNode = InNode;
		Target.ExternalForceIndex = InExternalForceIndex;
		return Target;
	}

	static FKawaiiPhysicsWindScopeTarget MakeSharedPublisherNode(
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* InNode)
	{
		FKawaiiPhysicsWindScopeTarget Target;
		Target.Kind = EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode;
		Target.GraphNode = InNode;
		Target.ExternalForceIndex = INDEX_NONE;
		return Target;
	}

	bool IsValid() const
	{
		return ResolveGraphNode() && ResolveGraphWind();
	}

	UAnimGraphNode_Base* ResolveGraphNode() const
	{
		UAnimGraphNode_Base* Node = GraphNode.Get();
		if (!Node)
		{
			return nullptr;
		}

		switch (Kind)
		{
		case EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode:
			return Cast<UAnimGraphNode_KawaiiPhysics>(Node) ? Node : nullptr;
		case EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode:
			return Cast<UAnimGraphNode_KawaiiPhysicsSharedPublisher>(Node) ? Node : nullptr;
		default:
			return nullptr;
		}
	}

	UAnimGraphNode_KawaiiPhysics* ResolveKawaiiPhysicsGraphNode() const
	{
		return Kind == EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode
			       ? Cast<UAnimGraphNode_KawaiiPhysics>(GraphNode.Get())
			       : nullptr;
	}

	UAnimGraphNode_KawaiiPhysicsSharedPublisher* ResolveSharedPublisherGraphNode() const
	{
		return Kind == EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode
			       ? Cast<UAnimGraphNode_KawaiiPhysicsSharedPublisher>(GraphNode.Get())
			       : nullptr;
	}

	UAnimBlueprint* ResolveAnimBlueprint() const
	{
		if (UAnimGraphNode_Base* Node = ResolveGraphNode())
		{
			return Node->GetAnimBlueprint();
		}
		return nullptr;
	}

	FKawaiiPhysics_ExternalForce_ProceduralWind* ResolveGraphWind() const
	{
		switch (Kind)
		{
		case EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode:
			if (UAnimGraphNode_KawaiiPhysics* Node = ResolveKawaiiPhysicsGraphNode())
			{
				return Node->Node.ExternalForces.IsValidIndex(ExternalForceIndex)
					       ? Node->Node.ExternalForces[ExternalForceIndex].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>()
					       : nullptr;
			}
			break;
		case EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode:
			if (UAnimGraphNode_KawaiiPhysicsSharedPublisher* Node = ResolveSharedPublisherGraphNode())
			{
				return &Node->Node.SharedWind;
			}
			break;
		default:
			break;
		}
		return nullptr;
	}

	// バナー表示等で使う共有 Tag を解決する。SharedPublisherNode は Node.SharedGroupTag（消費側と一致させる Tag）、KawaiiPhysicsNode は ResolveGraphWind() の SharedWindTag を返す。Publisher の SharedWind.SharedWindTag は Publisher 自身では未使用のため参照しない
	// Resolves the shared tag used for banner display etc. SharedPublisherNode returns Node.SharedGroupTag (the tag consumers must match); KawaiiPhysicsNode returns ResolveGraphWind()'s SharedWindTag. Never reads the Publisher's own SharedWind.SharedWindTag, which the Publisher itself does not use.
	FGameplayTag ResolveSharedWindTag() const
	{
		switch (Kind)
		{
		case EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode:
			if (const UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher = ResolveSharedPublisherGraphNode())
			{
				return Publisher->Node.SharedGroupTag;
			}
			break;
		case EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode:
			if (const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveGraphWind())
			{
				return Wind->SharedWindTag;
			}
			break;
		default:
			break;
		}
		return FGameplayTag();
	}

	FKawaiiPhysics_ExternalForce_ProceduralWind* ResolveLiveWind() const
	{
		switch (Kind)
		{
		case EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode:
			if (UAnimGraphNode_KawaiiPhysics* Node = ResolveKawaiiPhysicsGraphNode())
			{
				FAnimNode_KawaiiPhysics* RuntimeNode = KawaiiPhysicsEdUtils::ResolveLiveKawaiiPhysicsNode(Node);
				return ResolveLiveProceduralWind(Node, RuntimeNode, ExternalForceIndex);
			}
			break;
		case EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode:
			if (UAnimGraphNode_KawaiiPhysicsSharedPublisher* Node = ResolveSharedPublisherGraphNode())
			{
				FAnimNode_KawaiiPhysicsSharedPublisher* RuntimeNode =
					KawaiiPhysicsEdUtils::ResolveLiveSharedPublisherNode(Node);
				return RuntimeNode ? &RuntimeNode->SharedWind : nullptr;
			}
			break;
		default:
			break;
		}
		return nullptr;
	}

	FText GetTitle() const
	{
		switch (Kind)
		{
		case EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode:
			if (const UAnimGraphNode_KawaiiPhysics* Node = ResolveKawaiiPhysicsGraphNode())
			{
				return FText::Format(
					NSLOCTEXT("KawaiiPhysicsWindScopeTarget", "KawaiiPhysicsNodeTitleFormat", "{0} - External Force [{1}]"),
					Node->GetNodeTitle(ENodeTitleType::ListView),
					FText::AsNumber(ExternalForceIndex));
			}
			return NSLOCTEXT("KawaiiPhysicsWindScopeTarget", "UnknownKawaiiPhysicsNodeTitle", "KawaiiPhysics Node");
		case EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode:
			if (const UAnimGraphNode_KawaiiPhysicsSharedPublisher* Node = ResolveSharedPublisherGraphNode())
			{
				const FText TagText = Node->Node.SharedGroupTag.IsValid()
					                      ? FText::FromString(Node->Node.SharedGroupTag.ToString())
					                      : NSLOCTEXT("KawaiiPhysicsWindScopeTarget", "SharedPublisherNoTagTitle", "No Tag");
				return FText::Format(
					NSLOCTEXT("KawaiiPhysicsWindScopeTarget", "SharedPublisherTitleFormat", "Kawaii Physics Shared Publisher ({0})"),
					TagText);
			}
			return NSLOCTEXT("KawaiiPhysicsWindScopeTarget", "UnknownSharedPublisherTitle", "Kawaii Physics Shared Publisher");
		default:
			return FText::GetEmpty();
		}
	}

	bool IsRedirectedToShared() const
	{
		if (Kind != EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode)
		{
			return false;
		}

		const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveGraphWind();
		if (!Wind)
		{
			return false;
		}

		if (Wind->WindSource == EKawaiiPhysicsProceduralWindSource::Shared)
		{
			return true;
		}
		if (Wind->WindSource == EKawaiiPhysicsProceduralWindSource::Auto)
		{
			return KawaiiPhysicsEdUtils::FindSharedPublisherGraphNodeByTag(
				ResolveAnimBlueprint(),
				Wind->SharedWindTag) != nullptr;
		}
		return false;
	}

	FKawaiiPhysicsWindScopeTarget ResolveSharedRedirect() const
	{
		if (Kind != EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode)
		{
			return FKawaiiPhysicsWindScopeTarget();
		}

		const FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = ResolveGraphWind();
		if (!Wind ||
			(Wind->WindSource != EKawaiiPhysicsProceduralWindSource::Shared &&
				Wind->WindSource != EKawaiiPhysicsProceduralWindSource::Auto))
		{
			return FKawaiiPhysicsWindScopeTarget();
		}

		UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
			KawaiiPhysicsEdUtils::FindSharedPublisherGraphNodeByTag(ResolveAnimBlueprint(), Wind->SharedWindTag);
		return Publisher ? MakeSharedPublisherNode(Publisher) : FKawaiiPhysicsWindScopeTarget();
	}

	bool operator==(const FKawaiiPhysicsWindScopeTarget& Other) const
	{
		return Kind == Other.Kind &&
			GraphNode.Get() == Other.GraphNode.Get() &&
			ExternalForceIndex == Other.ExternalForceIndex;
	}

private:
	static FKawaiiPhysics_ExternalForce_ProceduralWind* ResolveLiveProceduralWind(
		UAnimGraphNode_KawaiiPhysics* GraphNode,
		FAnimNode_KawaiiPhysics* RuntimeNode,
		const int32 RequestedIndex)
	{
		if (!GraphNode || !RuntimeNode ||
			!KawaiiPhysicsEdUtils::IsExternalForceShapeMatched(GraphNode->Node.ExternalForces, RuntimeNode->ExternalForces))
		{
			return nullptr;
		}

		if (!GraphNode->Node.ExternalForces.IsValidIndex(RequestedIndex) ||
			!GraphNode->Node.ExternalForces[RequestedIndex].GetPtr<FKawaiiPhysics_ExternalForce_ProceduralWind>() ||
			!RuntimeNode->ExternalForces.IsValidIndex(RequestedIndex) ||
			!RuntimeNode->ExternalForces[RequestedIndex].GetPtr<FKawaiiPhysics_ExternalForce_ProceduralWind>())
		{
			return nullptr;
		}

		return RuntimeNode->ExternalForces[RequestedIndex].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	}
};

inline FKawaiiPhysicsWindScopeTarget ResolveWindScopeOpenTarget(
	const FKawaiiPhysicsWindScopeTarget& Target,
	TOptional<FKawaiiPhysicsWindScopeTarget>& OutOrigin)
{
	OutOrigin.Reset();

	if (Target.IsRedirectedToShared())
	{
		FKawaiiPhysicsWindScopeTarget Redirect = Target.ResolveSharedRedirect();
		if (Redirect.IsValid())
		{
			OutOrigin = Target;
			return Redirect;
		}
	}

	return Target;
}
