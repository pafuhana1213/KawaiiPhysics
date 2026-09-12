// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstanceProxy.h"
#include "KawaiiPhysicsExternalForcePostApply.h"

namespace
{
	using FOwnedForces = TArray<TUniquePtr<FKawaiiPhysics_ExternalForce>>;

	struct FObservedForce : FKawaiiPhysics_ExternalForce
	{
		TArray<int32>& Events;
		TArray<int32>& VisibleCounts;
		FOwnedForces& Forces;
		int32 Id;
		int32 MemberReadAfterSuper = 100;
		bool bCallSuper = true;

		FObservedForce(TArray<int32>& InEvents, TArray<int32>& InVisibleCounts,
		               FOwnedForces& InForces, int32 InId)
			: Events(InEvents), VisibleCounts(InVisibleCounts), Forces(InForces), Id(InId) {}

		virtual ~FObservedForce() override { Events.Add(-Id); }

		virtual void PostApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& Context) override
		{
			Events.Add(Id);
			VisibleCounts.Add(Forces.Num());
			if (bCallSuper)
			{
				FKawaiiPhysics_ExternalForce::PostApply(Node, Context);
			}
			// The object must still be alive after a derived class calls its base callback.
			Events.Add(MemberReadAfterSuper + Id);
		}
	};

	struct FCallbackForce : FKawaiiPhysics_ExternalForce
	{
		TFunction<void(FCallbackForce&, FAnimNode_KawaiiPhysics&, FComponentSpacePoseContext&)> Callback;
		TFunction<void()> OnDestroyed;

		virtual ~FCallbackForce() override
		{
			if (OnDestroyed)
			{
				OnDestroyed();
			}
		}

		virtual void PostApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& Context) override
		{
			// Keep the callable alive even if its invocation removes the owning force.
			auto CallbackCopy = Callback;
			CallbackCopy(*this, Node, Context);
		}
	};

	void PostApplyOwnedForces(FOwnedForces& Forces, FAnimNode_KawaiiPhysics& Node,
	                         FComponentSpacePoseContext& Context)
	{
		FKawaiiPhysicsExternalForcePostApply::Apply(Forces, Node, Context,
			[](const TUniquePtr<FKawaiiPhysics_ExternalForce>& Force) { return Force.Get(); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsOneShotAdjacentInstancesTest,
                                 "KawaiiPhysics.ExternalForce.OneShot.AdjacentInstances",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsOneShotAdjacentInstancesTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Context(&Proxy);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		auto& Force = Node.ExternalForces.Add_GetRef(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
		Force.GetMutable<FKawaiiPhysics_ExternalForce>().bIsOneShot = Index < 2;
	}
	const auto* PersistentForce = Node.ExternalForces[2].GetPtr<FKawaiiPhysics_ExternalForce>();
	Node.ExternalForces.AddDefaulted();
	FKawaiiPhysicsExternalForcePostApply::Apply(Node, Context);
	bool bOk = TestEqual(TEXT("Both adjacent one-shots removed in the first substep"), Node.ExternalForces.Num(), 2);
	if (Node.ExternalForces.Num() == 2)
	{
		bOk &= TestTrue(TEXT("Persistent force retains identity and order"),
			Node.ExternalForces[0].GetPtr<FKawaiiPhysics_ExternalForce>() == PersistentForce);
		bOk &= TestFalse(TEXT("Invalid entry is left untouched"), Node.ExternalForces[1].IsValid());
	}
	FKawaiiPhysicsExternalForcePostApply::Apply(Node, Context);
	bOk &= TestEqual(TEXT("No one-shot survives to the second substep"), Node.ExternalForces.Num(), 2);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsOneShotDerivedLifetimeTest,
                                 "KawaiiPhysics.ExternalForce.OneShot.DerivedLifetimeAndOrder",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsOneShotDerivedLifetimeTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Context(&Proxy);
	TArray<int32> Events;
	TArray<int32> VisibleCounts;
	FOwnedForces Forces;
	for (int32 Id = 1; Id <= 4; ++Id)
	{
		auto Force = MakeUnique<FObservedForce>(Events, VisibleCounts, Forces, Id);
		Force->bIsOneShot = Id != 3;
		Force->bCallSuper = Id != 4;
		Forces.Add(MoveTemp(Force));
	}
	PostApplyOwnedForces(Forces, Node, Context);
	const TArray<int32> ExpectedEvents = {1, 101, -1, 2, 102, -2, 3, 103, 4, 104};
	const TArray<int32> ExpectedCounts = {4, 3, 2, 2};
	bool bOk = TestTrue(TEXT("Destroy only after each full callback, preserving order"), Events == ExpectedEvents);
	bOk &= TestTrue(TEXT("Following callbacks see preceding one-shots removed"), VisibleCounts == ExpectedCounts);
	bOk &= TestEqual(TEXT("No-Super one-shot retains previous lifetime semantics"), Forces.Num(), 2);
	Events.Reset();
	VisibleCounts.Reset();
	PostApplyOwnedForces(Forces, Node, Context);
	const TArray<int32> ExpectedSecondSubstep = {3, 103, 4, 104};
	bOk &= TestTrue(TEXT("Only permanent and no-Super forces reach subsequent substeps"), Events == ExpectedSecondSubstep);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsOneShotCallbackMutationTest,
                                 "KawaiiPhysics.ExternalForce.OneShot.CallbackMutation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsOneShotCallbackMutationTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Context(&Proxy);
	bool bOk = true;
	{
		FOwnedForces Forces;
		int32 Calls = 0;
		auto Force = MakeUnique<FCallbackForce>();
		Force->Callback = [&](FCallbackForce&, FAnimNode_KawaiiPhysics&, FComponentSpacePoseContext&)
		{
			++Calls;
			Forces.RemoveAt(0);
		};
		Forces.Add(MoveTemp(Force));
		Forces.Add(MakeUnique<FKawaiiPhysics_ExternalForce>());
		PostApplyOwnedForces(Forces, Node, Context);
		bOk &= TestEqual(TEXT("Third-party self-removal is not dereferenced or removed twice"), Forces.Num(), 1);
		bOk &= TestEqual(TEXT("Self-removing callback runs once"), Calls, 1);
	}
	{
		FOwnedForces Forces;
		int32 AppendedCalls = 0;
		auto Force = MakeUnique<FCallbackForce>();
		Force->Callback = [&](FCallbackForce&, FAnimNode_KawaiiPhysics&, FComponentSpacePoseContext&)
		{
			auto Appended = MakeUnique<FCallbackForce>();
			Appended->Callback = [&](FCallbackForce&, FAnimNode_KawaiiPhysics&, FComponentSpacePoseContext&)
			{
				++AppendedCalls;
			};
			Forces.Add(MoveTemp(Appended));
		};
		Forces.Add(MoveTemp(Force));
		PostApplyOwnedForces(Forces, Node, Context);
		bOk &= TestEqual(TEXT("Dynamically appended forces retain forward traversal behavior"), AppendedCalls, 1);
	}
	{
		FOwnedForces Forces;
		TArray<int32> Order;
		auto Force = MakeUnique<FCallbackForce>();
		Force->bIsOneShot = true;
		Force->Callback = [&](FCallbackForce& Self, FAnimNode_KawaiiPhysics& InNode, FComponentSpacePoseContext& InContext)
		{
			Order.Add(1);
			Self.FKawaiiPhysics_ExternalForce::PostApply(InNode, InContext);
			auto Appended = MakeUnique<FCallbackForce>();
			Appended->Callback = [&](FCallbackForce&, FAnimNode_KawaiiPhysics&, FComponentSpacePoseContext&)
			{
				Order.Add(3);
			};
			Forces.Add(MoveTemp(Appended));
		};
		auto Following = MakeUnique<FCallbackForce>();
		Following->Callback = [&](FCallbackForce&, FAnimNode_KawaiiPhysics&, FComponentSpacePoseContext&)
		{
			Order.Add(2);
		};
		Forces.Add(MoveTemp(Force));
		Forces.Add(MoveTemp(Following));
		PostApplyOwnedForces(Forces, Node, Context);
		const TArray<int32> ExpectedOrder = {1, 2, 3};
		bOk &= TestTrue(TEXT("Appending during one-shot removal never skips the shifted successor"), Order == ExpectedOrder);
	}
	{
		FOwnedForces Forces;
		int32 Calls = 0;
		TFunction<TUniquePtr<FCallbackForce>()> MakeReplacingForce;
		MakeReplacingForce = [&]()
		{
			auto Force = MakeUnique<FCallbackForce>();
			Force->bIsOneShot = true;
			Force->Callback = [&](FCallbackForce& Self, FAnimNode_KawaiiPhysics& InNode, FComponentSpacePoseContext& InContext)
			{
				++Calls;
				Self.FKawaiiPhysics_ExternalForce::PostApply(InNode, InContext);
				// Dynamic callbacks are responsible for bounding their own additions.
				if (Calls < 10)
				{
					Forces.Add(MakeReplacingForce());
				}
			};
			return Force;
		};
		Forces.Add(MakeReplacingForce());
		PostApplyOwnedForces(Forces, Node, Context);
		bOk &= TestEqual(TEXT("Finite one-shot replacements retain dynamic traversal"), Calls, 10);
		bOk &= TestEqual(TEXT("Each replacement removed after its own callback"), Forces.Num(), 0);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsOneShotPreviousRemovalTest,
                                 "KawaiiPhysics.ExternalForce.OneShot.PreviousRemoval",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsOneShotPreviousRemovalTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Context(&Proxy);
	bool bOk = true;
	for (const int32 PriorCount : {1, 2})
	{
		TArray<int32> Order;
		TArray<int32> DestroyedPrior;
		bool bCurrentDestroyed = false;
		bool bMemberReadAfterEarlierRemoval = false;
		FOwnedForces Forces;
		for (int32 Id = 1; Id <= PriorCount; ++Id)
		{
			auto Prior = MakeUnique<FCallbackForce>();
			Prior->Callback = [&, Id](FCallbackForce&, FAnimNode_KawaiiPhysics&, FComponentSpacePoseContext&)
			{
				Order.Add(Id);
			};
			Prior->OnDestroyed = [&, Id]() { DestroyedPrior.Add(Id); };
			Forces.Add(MoveTemp(Prior));
		}
		auto Current = MakeUnique<FCallbackForce>();
		Current->bIsOneShot = true;
		Current->OnDestroyed = [&]() { bCurrentDestroyed = true; };
		Current->Callback = [&](FCallbackForce& Self, FAnimNode_KawaiiPhysics& InNode, FComponentSpacePoseContext& InContext)
		{
			Order.Add(PriorCount + 1);
			Self.FKawaiiPhysics_ExternalForce::PostApply(InNode, InContext);
			Forces.RemoveAt(0, PriorCount);
			bMemberReadAfterEarlierRemoval = !bCurrentDestroyed && Self.bIsOneShot;
		};
		Forces.Add(MoveTemp(Current));
		auto Following = MakeUnique<FCallbackForce>();
		Following->Callback = [&](FCallbackForce&, FAnimNode_KawaiiPhysics&, FComponentSpacePoseContext&)
		{
			Order.Add(PriorCount + 2);
		};
		const auto* FollowingIdentity = Following.Get();
		Forces.Add(MoveTemp(Following));
		PostApplyOwnedForces(Forces, Node, Context);

		TArray<int32> ExpectedOrder;
		for (int32 Id = 1; Id <= PriorCount + 2; ++Id)
		{
			ExpectedOrder.Add(Id);
		}
		bOk &= TestTrue(TEXT("Deleting preceding forces preserves successor callback order"), Order == ExpectedOrder);
		bOk &= TestEqual(TEXT("Earlier objects are destroyed by the callback"), DestroyedPrior.Num(), PriorCount);
		bOk &= TestTrue(TEXT("Current object survives base callback and earlier-element removal"), bMemberReadAfterEarlierRemoval);
		bOk &= TestTrue(TEXT("Current one-shot is destroyed after the complete callback"), bCurrentDestroyed);
		bOk &= TestEqual(TEXT("Only the following force remains"), Forces.Num(), 1);
		if (Forces.Num() == 1)
		{
			bOk &= TestTrue(TEXT("Following force retains ownership"), Forces[0].Get() == FollowingIdentity);
		}
	}
	return bOk;
}

#endif
