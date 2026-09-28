// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeLock.h"
#include "AnimNode_KawaiiPhysics.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"

namespace
{
	struct FQueueBufferSnapshot
	{
		SIZE_T AllocatedBytes = 0;
		int32 PendingItems = 0;
		int32 ConsumingItems = 0;

		template <typename ElementType>
		void Capture(const TArray<ElementType>& Pending, const TArray<ElementType>& Consuming)
		{
			AllocatedBytes += Pending.GetAllocatedSize() + Consuming.GetAllocatedSize();
			PendingItems += Pending.Num();
			ConsumingItems += Consuming.Num();
		}

		explicit FQueueBufferSnapshot(const FKawaiiPhysicsTransientForceStore& Store)
		{
			FScopeLock Lock(&Store.Queue->Mutex);
			Capture(Store.Queue->PendingForces, Store.ConsumingForces);
			Capture(Store.Queue->PendingGusts, Store.ConsumingGusts);
			Capture(Store.Queue->PendingStops, Store.ConsumingStops);
			Capture(Store.Queue->PendingSettingsMultipliers, Store.ConsumingSettingsMultipliers);
			Capture(Store.Queue->PendingSettingsMultiplierPushes, Store.ConsumingSettingsMultiplierPushes);
			Capture(Store.Queue->PendingSettingsMultiplierStops, Store.ConsumingSettingsMultiplierStops);
		}
	};

	void ProduceAndConsumeAllRequestTypes(FAnimNode_KawaiiPhysics& Node)
	{
		Node.RequestTransientExternalForce(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>(), 1.0f, 11);
		Node.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector::ForwardVector, INDEX_NONE, 0.0f, 22);
		Node.RequestStopTransientExternalForce(11, 0.0f);
		Node.RequestStopTransientExternalForce(22, 0.0f);
		Node.ConsumeAndRemoveExpiredTransientExternalForces(1.0f / 60.0f);
		FKawaiiPhysicsSettingsMultiplier Scale;
		Scale.Damping = 2.0f;
		Node.RequestPushPhysicsSettingsMultiplier(Scale, 0.75f, 101);
		Node.RequestStartPhysicsSettingsMultiplier(Scale, 0.1f, 1.0f, 0.1f, 102);
		Node.RequestStopPhysicsSettingsMultiplier(101, 0.0f);
		Node.RequestStopPhysicsSettingsMultiplier(102, 0.0f);
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(1.0f / 60.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRequestQueueBufferReuseTest,
                                 "KawaiiPhysics.TransientForce.QueueBufferReuse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 要求を同一評価で消費し、キュー容量とコピー時の独立性を守る。
bool FKawaiiPhysicsRequestQueueBufferReuseTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	bool bOk = TestEqual(TEXT("Unused queues retain no heap capacity"),
		FQueueBufferSnapshot(Node.TransientForceStore).AllocatedBytes, SIZE_T(0));
	ProduceAndConsumeAllRequestTypes(Node);
	ProduceAndConsumeAllRequestTypes(Node);
	FQueueBufferSnapshot Previous(Node.TransientForceStore);
	for (int32 Frame = 0; Frame < 32; ++Frame)
	{
		ProduceAndConsumeAllRequestTypes(Node);
		const FQueueBufferSnapshot Current(Node.TransientForceStore);
		bOk &= TestEqual(TEXT("All requests consumed once"), Current.PendingItems, 0);
		bOk &= TestEqual(TEXT("Consumed contents destroyed in the same evaluation"), Current.ConsumingItems, 0);
		bOk &= TestEqual(TEXT("Transient stops follow starts in the same evaluation"), Node.TransientForceStore.Items.Num(), 0);
		bOk &= TestEqual(TEXT("Multiplier stops follow starts and pushes"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
		bOk &= TestEqual(TEXT("Retained queue bytes stabilize after warmup"), Current.AllocatedBytes, Previous.AllocatedBytes);
		Previous = Current;
	}
	FAnimNode_KawaiiPhysics Copy = Node;
	const FQueueBufferSnapshot CopiedBuffers(Copy.TransientForceStore);
	bOk &= TestEqual(TEXT("Node copy does not copy retained capacity"), CopiedBuffers.AllocatedBytes, SIZE_T(0));
	bOk &= TestTrue(TEXT("Node copy retains independent queue ownership"),
		Copy.TransientForceStore.Queue != Node.TransientForceStore.Queue);
	Copy = Node;
	bOk &= TestEqual(TEXT("Assignment does not acquire the source consume buffers"),
		FQueueBufferSnapshot(Copy.TransientForceStore).AllocatedBytes, SIZE_T(0));
	// 同一フレームの同じハンドルでは、後続の timed 開始が push より優先する。
	FKawaiiPhysicsSettingsMultiplier Pushed;
	Pushed.Damping = 2.0f;
	FKawaiiPhysicsSettingsMultiplier Timed;
	Timed.Damping = 3.0f;
	Node.RequestPushPhysicsSettingsMultiplier(Pushed, 0.75f, 101);
	Node.RequestStartPhysicsSettingsMultiplier(Timed, 0.1f, 1.0f, 0.1f, 101);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(1.0f / 60.0f);
	bOk &= TestEqual(TEXT("One active item per shared handle"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Node.TransientForceStore.SettingsMultiplierItems.Num() == 1)
	{
		const auto& Item = Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFalse(TEXT("Timed request takes precedence over push"), Item.bExternallyDriven);
		bOk &= TestEqual(TEXT("Timed request scale is preserved"), Item.Scale.Damping, 3.0f);
	}
	return bOk;
}

#endif
