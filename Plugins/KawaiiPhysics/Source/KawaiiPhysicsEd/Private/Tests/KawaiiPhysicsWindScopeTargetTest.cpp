// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "../KawaiiPhysicsWindScopeTarget.h"

#include "AnimationGraph.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "GameplayTagContainer.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"

namespace
{
	FGameplayTag GetWindScopeTargetTestTagX()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Test.SimpleWorld.Registry.X")), false);
	}

	FGameplayTag GetWindScopeTargetTestTagY()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Test.SimpleWorld.Registry.Y")), false);
	}

	USkeleton* CreateWindScopeTargetTestSkeleton(UObject* Outer)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(Outer ? Outer : GetTransientPackage());
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hair_01"), TEXT("hair_01"), 0), FTransform::Identity);
		return Skeleton;
	}

	UAnimBlueprint* CreateWindScopeTargetTestAnimBlueprint(FAutomationTestBase& Test)
	{
		const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/KawaiiPhysicsWindScopeTarget_%s"), *UniqueSuffix));
		Package->SetFlags(RF_Transient);

		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			UAnimInstance::StaticClass(),
			Package,
			FName(*FString::Printf(TEXT("ABP_KawaiiPhysicsWindScopeTarget_%s"), *UniqueSuffix)),
			BPTYPE_Normal,
			UAnimBlueprint::StaticClass(),
			UAnimBlueprintGeneratedClass::StaticClass());

		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);
		Test.TestTrue(TEXT("Transient AnimBlueprint is created"), AnimBlueprint != nullptr);
		if (AnimBlueprint)
		{
			AnimBlueprint->TargetSkeleton = CreateWindScopeTargetTestSkeleton(Package);
		}
		return AnimBlueprint;
	}

	UEdGraph* FindWindScopeTargetTestAnimGraph(UAnimBlueprint* AnimBlueprint)
	{
		TArray<UAnimationGraph*> Graphs;
		KawaiiPhysicsEdUtils::CollectAnimGraphs(AnimBlueprint, Graphs);
		return Graphs.IsEmpty() ? nullptr : Graphs[0];
	}

	UAnimGraphNode_KawaiiPhysicsSharedPublisher* AddWindScopeTargetTestPublisher(
		UEdGraph* Graph,
		const FGameplayTag& Tag)
	{
		FGraphNodeCreator<UAnimGraphNode_KawaiiPhysicsSharedPublisher> NodeCreator(*Graph);
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* GraphNode = NodeCreator.CreateNode(false);
		GraphNode->Node.SharedGroupTag = Tag;
		NodeCreator.Finalize();
		return GraphNode;
	}

	UAnimGraphNode_KawaiiPhysics* AddWindScopeTargetTestKawaiiPhysicsNode(
		UEdGraph* Graph,
		EKawaiiPhysicsProceduralWindSource WindSource,
		const FGameplayTag& SharedWindTag)
	{
		FGraphNodeCreator<UAnimGraphNode_KawaiiPhysics> NodeCreator(*Graph);
		UAnimGraphNode_KawaiiPhysics* GraphNode = NodeCreator.CreateNode(false);
		GraphNode->Node.RootBone = FBoneReference(TEXT("hair_01"));
		GraphNode->Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>());
		if (FKawaiiPhysics_ExternalForce_ProceduralWind* Wind =
			GraphNode->Node.ExternalForces[0].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>())
		{
			Wind->WindSource = WindSource;
			Wind->SharedWindTag = SharedWindTag;
		}
		NodeCreator.Finalize();
		return GraphNode;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindScopeSharedTargetResolveTest,
                                 "KawaiiPhysics.Editor.WindScope.SharedTargetResolve",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindScopeSharedTargetResolveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bOk = true;
	const FGameplayTag TagX = GetWindScopeTargetTestTagX();
	const FGameplayTag TagY = GetWindScopeTargetTestTagY();
	bOk &= TestTrue(TEXT("Runtime test gameplay tags are registered"), TagX.IsValid() && TagY.IsValid());

	UAnimBlueprint* AnimBlueprint = CreateWindScopeTargetTestAnimBlueprint(*this);
	UEdGraph* AnimGraph = FindWindScopeTargetTestAnimGraph(AnimBlueprint);
	bOk &= TestTrue(TEXT("Default AnimGraph is found"), AnimGraph != nullptr);
	if (!AnimGraph)
	{
		return false;
	}

	UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
		AddWindScopeTargetTestPublisher(AnimGraph, TagX);
	UAnimGraphNode_KawaiiPhysics* KPShared =
		AddWindScopeTargetTestKawaiiPhysicsNode(AnimGraph, EKawaiiPhysicsProceduralWindSource::Shared, TagX);
	UAnimGraphNode_KawaiiPhysics* KPAuto =
		AddWindScopeTargetTestKawaiiPhysicsNode(AnimGraph, EKawaiiPhysicsProceduralWindSource::Auto, TagX);
	UAnimGraphNode_KawaiiPhysics* KPLocal =
		AddWindScopeTargetTestKawaiiPhysicsNode(AnimGraph, EKawaiiPhysicsProceduralWindSource::Local, TagX);
	UAnimGraphNode_KawaiiPhysics* KPMissingPublisher =
		AddWindScopeTargetTestKawaiiPhysicsNode(AnimGraph, EKawaiiPhysicsProceduralWindSource::Shared, TagY);

	FKawaiiPhysicsWindScopeTarget SharedTarget =
		FKawaiiPhysicsWindScopeTarget::MakeKawaiiPhysicsNode(KPShared, 0);
	bOk &= TestTrue(TEXT("Shared KP target is valid"), SharedTarget.IsValid());
	bOk &= TestTrue(TEXT("Shared KP graph wind resolves element 0"),
	                SharedTarget.ResolveGraphWind() ==
	                KPShared->Node.ExternalForces[0].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>());
	bOk &= TestTrue(TEXT("Shared KP resolved shared tag matches the consumer's SharedWindTag"),
	                SharedTarget.ResolveSharedWindTag() == TagX);
	bOk &= TestTrue(TEXT("Shared KP target redirects"), SharedTarget.IsRedirectedToShared());
	FKawaiiPhysicsWindScopeTarget SharedRedirect = SharedTarget.ResolveSharedRedirect();
	bOk &= TestTrue(TEXT("Shared redirect is publisher target"),
	                SharedRedirect.Kind == EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode);
	bOk &= TestTrue(TEXT("Shared redirect resolves publisher"),
	                SharedRedirect.ResolveSharedPublisherGraphNode() == Publisher);

	FKawaiiPhysicsWindScopeTarget AutoTarget =
		FKawaiiPhysicsWindScopeTarget::MakeKawaiiPhysicsNode(KPAuto, 0);
	bOk &= TestTrue(TEXT("Auto KP target is valid"), AutoTarget.IsValid());
	bOk &= TestTrue(TEXT("Auto KP target redirects while publisher exists"), AutoTarget.IsRedirectedToShared());
	bOk &= TestTrue(TEXT("Auto redirect resolves publisher"),
	                AutoTarget.ResolveSharedRedirect().ResolveSharedPublisherGraphNode() == Publisher);

	FKawaiiPhysicsWindScopeTarget LocalTarget =
		FKawaiiPhysicsWindScopeTarget::MakeKawaiiPhysicsNode(KPLocal, 0);
	bOk &= TestTrue(TEXT("Local KP target is valid"), LocalTarget.IsValid());
	bOk &= TestFalse(TEXT("Local KP target does not redirect"), LocalTarget.IsRedirectedToShared());
	bOk &= TestFalse(TEXT("Local redirect is invalid"), LocalTarget.ResolveSharedRedirect().IsValid());

	FKawaiiPhysicsWindScopeTarget MissingPublisherTarget =
		FKawaiiPhysicsWindScopeTarget::MakeKawaiiPhysicsNode(KPMissingPublisher, 0);
	bOk &= TestTrue(TEXT("Missing publisher KP target is valid"), MissingPublisherTarget.IsValid());
	bOk &= TestTrue(TEXT("Shared KP target redirects even without publisher"), MissingPublisherTarget.IsRedirectedToShared());
	bOk &= TestFalse(TEXT("Missing publisher redirect is invalid"), MissingPublisherTarget.ResolveSharedRedirect().IsValid());

	FKawaiiPhysicsWindScopeTarget PublisherTarget =
		FKawaiiPhysicsWindScopeTarget::MakeSharedPublisherNode(Publisher);
	bOk &= TestTrue(TEXT("Publisher target is valid"), PublisherTarget.IsValid());
	bOk &= TestTrue(TEXT("Publisher graph wind resolves SharedWind"),
	                PublisherTarget.ResolveGraphWind() == &Publisher->Node.SharedWind);
	bOk &= TestFalse(TEXT("Publisher target does not redirect"), PublisherTarget.IsRedirectedToShared());
	bOk &= TestTrue(TEXT("Publisher resolved shared tag matches its own SharedGroupTag"),
	                PublisherTarget.ResolveSharedWindTag() == TagX);
	bOk &= TestFalse(TEXT("Publisher resolved shared tag is not the unused SharedWind.SharedWindTag default"),
	                 PublisherTarget.ResolveSharedWindTag() == Publisher->Node.SharedWind.SharedWindTag);
	bOk &= TestTrue(TEXT("Publisher title contains tag"),
	                PublisherTarget.GetTitle().ToString().Contains(TagX.ToString()));
	bOk &= TestTrue(TEXT("Publisher live wind is null when preview is not running"),
	                PublisherTarget.ResolveLiveWind() == nullptr);

	FKawaiiPhysicsWindScopeTarget OutOfRangeTarget =
		FKawaiiPhysicsWindScopeTarget::MakeKawaiiPhysicsNode(KPShared, 99);
	bOk &= TestFalse(TEXT("Out-of-range KP target is invalid"), OutOfRangeTarget.IsValid());
	bOk &= TestTrue(TEXT("Out-of-range graph wind is null"), OutOfRangeTarget.ResolveGraphWind() == nullptr);
	bOk &= TestTrue(TEXT("Out-of-range live wind is null"), OutOfRangeTarget.ResolveLiveWind() == nullptr);

	FKawaiiPhysicsWindScopeTarget PublisherAsKawaiiPhysicsTarget;
	PublisherAsKawaiiPhysicsTarget.Kind = EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode;
	PublisherAsKawaiiPhysicsTarget.GraphNode = Publisher;
	PublisherAsKawaiiPhysicsTarget.ExternalForceIndex = 0;
	bOk &= TestFalse(TEXT("Publisher node with KP kind is invalid"), PublisherAsKawaiiPhysicsTarget.IsValid());
	bOk &= TestTrue(TEXT("Publisher node with KP kind resolves no KP node"),
	                PublisherAsKawaiiPhysicsTarget.ResolveKawaiiPhysicsGraphNode() == nullptr);
	bOk &= TestTrue(TEXT("Publisher node with KP kind resolves no wind"),
	                PublisherAsKawaiiPhysicsTarget.ResolveGraphWind() == nullptr);

	FKawaiiPhysicsWindScopeTarget KawaiiPhysicsAsPublisherTarget;
	KawaiiPhysicsAsPublisherTarget.Kind = EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode;
	KawaiiPhysicsAsPublisherTarget.GraphNode = KPShared;
	KawaiiPhysicsAsPublisherTarget.ExternalForceIndex = INDEX_NONE;
	bOk &= TestFalse(TEXT("KP node with publisher kind is invalid"), KawaiiPhysicsAsPublisherTarget.IsValid());
	bOk &= TestTrue(TEXT("KP node with publisher kind resolves no publisher node"),
	                KawaiiPhysicsAsPublisherTarget.ResolveSharedPublisherGraphNode() == nullptr);
	bOk &= TestTrue(TEXT("KP node with publisher kind resolves no wind"),
	                KawaiiPhysicsAsPublisherTarget.ResolveGraphWind() == nullptr);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsWindScopeRedirectResolveTest,
                                 "KawaiiPhysics.Editor.WindScope.RedirectResolve",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsWindScopeRedirectResolveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bOk = true;
	const FGameplayTag TagX = GetWindScopeTargetTestTagX();
	const FGameplayTag TagY = GetWindScopeTargetTestTagY();
	bOk &= TestTrue(TEXT("Runtime test gameplay tags are registered"), TagX.IsValid() && TagY.IsValid());

	UAnimBlueprint* AnimBlueprint = CreateWindScopeTargetTestAnimBlueprint(*this);
	UEdGraph* AnimGraph = FindWindScopeTargetTestAnimGraph(AnimBlueprint);
	bOk &= TestTrue(TEXT("Default AnimGraph is found"), AnimGraph != nullptr);
	if (!AnimGraph)
	{
		return false;
	}

	UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
		AddWindScopeTargetTestPublisher(AnimGraph, TagX);
	UAnimGraphNode_KawaiiPhysics* KPShared =
		AddWindScopeTargetTestKawaiiPhysicsNode(AnimGraph, EKawaiiPhysicsProceduralWindSource::Shared, TagX);
	UAnimGraphNode_KawaiiPhysics* KPLocal =
		AddWindScopeTargetTestKawaiiPhysicsNode(AnimGraph, EKawaiiPhysicsProceduralWindSource::Local, TagX);
	UAnimGraphNode_KawaiiPhysics* KPMissingPublisher =
		AddWindScopeTargetTestKawaiiPhysicsNode(AnimGraph, EKawaiiPhysicsProceduralWindSource::Shared, TagY);

	TOptional<FKawaiiPhysicsWindScopeTarget> Origin;
	const FKawaiiPhysicsWindScopeTarget SharedOpenTarget = ResolveWindScopeOpenTarget(
		FKawaiiPhysicsWindScopeTarget::MakeKawaiiPhysicsNode(KPShared, 0),
		Origin);
	bOk &= TestTrue(TEXT("Shared open target redirects to publisher"),
	                SharedOpenTarget.Kind == EKawaiiPhysicsWindScopeTargetKind::SharedPublisherNode);
	bOk &= TestTrue(TEXT("Shared open target resolves publisher"),
	                SharedOpenTarget.ResolveSharedPublisherGraphNode() == Publisher);
	bOk &= TestTrue(TEXT("Shared open target stores origin"),
	                Origin.IsSet() &&
	                Origin.GetValue().Kind == EKawaiiPhysicsWindScopeTargetKind::KawaiiPhysicsNode &&
	                Origin.GetValue().ResolveKawaiiPhysicsGraphNode() == KPShared);

	const FKawaiiPhysicsWindScopeTarget LocalTarget =
		FKawaiiPhysicsWindScopeTarget::MakeKawaiiPhysicsNode(KPLocal, 0);
	const FKawaiiPhysicsWindScopeTarget LocalOpenTarget = ResolveWindScopeOpenTarget(LocalTarget, Origin);
	bOk &= TestTrue(TEXT("Local open target is unchanged"), LocalOpenTarget == LocalTarget);
	bOk &= TestFalse(TEXT("Local open target has no origin"), Origin.IsSet());

	const FKawaiiPhysicsWindScopeTarget MissingPublisherTarget =
		FKawaiiPhysicsWindScopeTarget::MakeKawaiiPhysicsNode(KPMissingPublisher, 0);
	const FKawaiiPhysicsWindScopeTarget MissingOpenTarget = ResolveWindScopeOpenTarget(MissingPublisherTarget, Origin);
	bOk &= TestTrue(TEXT("Missing publisher open target is unchanged"), MissingOpenTarget == MissingPublisherTarget);
	bOk &= TestFalse(TEXT("Missing publisher open target has no origin"), Origin.IsSet());

	return bOk;
}

#endif
