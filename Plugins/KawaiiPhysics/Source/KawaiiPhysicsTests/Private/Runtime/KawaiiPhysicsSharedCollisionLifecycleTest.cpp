// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Animation/AnimInstance.h"
#include "Async/Async.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsLifecycle, "KawaiiPhysics.Test.Lifecycle");

namespace
{
	FKawaiiPhysicsSharedCollisionData LifecycleSphere(float Radius)
	{
		FKawaiiPhysicsSharedCollisionData Data;
		Data.SphericalLimits.AddDefaulted_GetRef().Radius = Radius;
		return Data;
	}

	struct FSharedCollisionLifecycleWorld
	{
		UWorld* World;
		AActor* Actor;
		UAnimInstance* Anim;
		UKawaiiPhysicsSharedCollisionSubsystem* Subsystem;

		FSharedCollisionLifecycleWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &Values);
			Actor = World->SpawnActor<AActor>();
			USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Actor);
			Actor->AddInstanceComponent(Mesh);
			Actor->SetRootComponent(Mesh);
			Anim = NewObject<UAnimInstance>(Mesh);
			Subsystem = World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>();
		}
		~FSharedCollisionLifecycleWorld() { Destroy(); }
		void Destroy()
		{
			if (World)
			{
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		void Register(FKawaiiPhysicsTestAccessor& Accessor, bool bSource)
		{
			Accessor.Node.bSharedCollisionSource = bSource;
			Accessor.Node.bUseSharedCollision = !bSource;
			Accessor.Node.SharedCollisionGroupTag = TAG_KawaiiPhysicsLifecycle;
			Accessor.Node.PreUpdate(Anim);
			Accessor.UpdateSharedCollisionRegistration();
		}
		void Cleanup()
		{
			IConsoleVariable* Interval = IConsoleManager::Get().FindConsoleVariable(
				TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.CleanupInterval"));
			Subsystem->Tick(FMath::Max(0.0f, Interval->GetFloat()) + 1.0f);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionLifecycleTest,
	"KawaiiPhysics.SharedCollision.Lifecycle.Reconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionLifecycleTest::RunTest(const FString& Parameters)
{
	FSharedCollisionLifecycleWorld Fixture;
	FKawaiiPhysicsTestAccessor Source, Target;
	Fixture.Register(Source, true);
	Fixture.Register(Target, false);
	const auto OldEntry = Source.GetSharedCollisionEntry();
	const auto OldSlot = Source.GetSharedCollisionSourceSlot();
	if (!TestTrue(TEXT("Source and target initially connect"), OldEntry.IsValid() && OldSlot.IsValid()
		&& Target.GetSharedCollisionEntry() == OldEntry))
	{
		return false;
	}

	auto Data = LifecycleSphere(10.0f);
	OldSlot->Publish(Data);
	OldSlot->MarkExpired();
	FKawaiiPhysicsSharedCollisionData ReadData;
	OldEntry->ReadMerged(ReadData);
	TestTrue(TEXT("Expired data is ignored before cleanup"), ReadData.IsEmpty());
	Fixture.Cleanup();
	TestTrue(TEXT("Cleanup retires the source slot even with retained node handles"), OldSlot->IsRetired());
	TestTrue(TEXT("Cleanup retires the empty entry even with retained target handles"), OldEntry->IsRetired());
	TestFalse(TEXT("Registry removes retired entry"), Fixture.Subsystem->FindEntry(Fixture.Actor,
		TAG_KawaiiPhysicsLifecycle).IsValid());

	// A target may keep evaluating while every source remains stopped.
	Target.SetSharedCollisionRetryState(25, true);
	Target.UpdateSharedCollisionRegistration();
	TestFalse(TEXT("Target drops the detached entry"), Target.GetSharedCollisionEntry().IsValid());
	TestFalse(TEXT("Target remains uninitialized while source is absent"), Target.IsSharedCollisionInitialized());
	TestEqual(TEXT("Retirement starts the original retry grace period"), Target.GetSharedCollisionRetryCount(), 1);
	TestFalse(TEXT("Retirement resets an earlier warning"), Target.HasSharedCollisionWarning());

	Source.UpdateSharedCollisionRegistration();
	const auto NewEntry = Source.GetSharedCollisionEntry();
	const auto NewSlot = Source.GetSharedCollisionSourceSlot();
	if (!TestTrue(TEXT("Source resumes by registering fresh handles"), NewEntry.IsValid() && NewSlot.IsValid()
		&& NewEntry != OldEntry && NewSlot != OldSlot))
	{
		return false;
	}
	Data = LifecycleSphere(20.0f);
	NewSlot->Publish(Data);
	Target.UpdateSharedCollisionRegistration();
	TestTrue(TEXT("Continuing target reconnects to the regenerated entry"),
		Target.GetSharedCollisionEntry() == NewEntry && Target.IsSharedCollisionInitialized());
	Target.GetSharedCollisionEntry()->ReadMerged(ReadData);
	TestEqual(TEXT("Resumed source reaches the target"), ReadData.SphericalLimits.Num(), 1);
	if (ReadData.SphericalLimits.Num() == 1)
	{
		TestEqual(TEXT("Target receives the current source shape"), ReadData.SphericalLimits[0].Radius, 20.0f);
	}

	// A different live source keeps the entry registered while this source's slot expires.
	const auto LiveSlot = NewEntry->GetOrCreateSlot(1);
	Data = LifecycleSphere(30.0f);
	LiveSlot->Publish(Data);
	NewSlot->MarkExpired();
	Fixture.Cleanup();
	TestFalse(TEXT("A live source keeps its entry registered"), NewEntry->IsRetired());
	TestTrue(TEXT("Only the stopped source slot is retired"), NewSlot->IsRetired());
	Source.UpdateSharedCollisionRegistration();
	TestTrue(TEXT("Source recovers inside the surviving entry"), Source.GetSharedCollisionEntry() == NewEntry
		&& Source.GetSharedCollisionSourceSlot() != NewSlot);
	Target.UpdateSharedCollisionRegistration();
	TestTrue(TEXT("Target retains a live entry"), Target.GetSharedCollisionEntry() == NewEntry);

	const auto ActiveSlot = Source.GetSharedCollisionSourceSlot();
	Fixture.Destroy();
	TestTrue(TEXT("Deinitialize retires every held entry and source slot"), NewEntry->IsRetired()
		&& ActiveSlot->IsRetired() && LiveSlot->IsRetired());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionRetirementRaceTest,
	"KawaiiPhysics.SharedCollision.Lifecycle.PublishRetirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionRetirementRaceTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedCollisionSourceSlot FreshSlot;
	auto Data = LifecycleSphere(10.0f);
	FreshSlot.Publish(Data);
	TestFalse(TEXT("Cleanup rechecks freshness under the publish lock"),
		FreshSlot.RetireIfExpired(GFrameCounter, 60));

	for (int32 Iteration = 0; Iteration < 64; ++Iteration)
	{
		FKawaiiPhysicsSharedCollisionSourceSlot Slot;
		auto Publish = Async(EAsyncExecution::ThreadPool, [&Slot]()
		{
			auto WorkerData = LifecycleSphere(50.0f);
			Slot.Publish(WorkerData);
		});
		Slot.Retire();
		Publish.Wait();
		TestTrue(TEXT("Retirement wins regardless of publish ordering"), Slot.IsRetired());
		auto LateData = LifecycleSphere(60.0f);
		const uint64 Serial = Slot.GetPublishSerial();
		Slot.Publish(LateData);
		TestEqual(TEXT("Late publish is rejected"), Slot.GetPublishSerial(), Serial);
		TestEqual(TEXT("Rejected publish leaves caller data intact"), LateData.SphericalLimits.Num(), 1);
		FKawaiiPhysicsSharedCollisionData Out;
		Slot.AppendTo(Out);
		TestTrue(TEXT("Retained retired handles cannot expose obsolete shapes"), Out.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionRetryThrottleTest,
	"KawaiiPhysics.SharedCollision.Lifecycle.RetryThrottle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionRetryThrottleTest::RunTest(const FString& Parameters)
{
	FSharedCollisionLifecycleWorld Fixture;
	FKawaiiPhysicsTestAccessor Target, Source;
	Fixture.Register(Target, false);
	TestFalse(TEXT("Target without source stays uninitialized"), Target.IsSharedCollisionInitialized());
	const IConsoleVariable* Throttle = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.InitRetryThrottleInterval"));
	const int32 Interval = FMath::Max(1, Throttle->GetInt());
	Target.SetSharedCollisionRetryState(FMath::Max(0, Interval - 2), true);
	Fixture.Register(Source, true);
	if (Interval > 1)
	{
		Target.UpdateSharedCollisionRegistration();
		TestFalse(TEXT("A warned target retains the configured retry interval"),
			Target.IsSharedCollisionInitialized());
	}
	Target.UpdateSharedCollisionRegistration();
	TestTrue(TEXT("Target reconnects at the throttle boundary when source appears"),
		Target.IsSharedCollisionInitialized() && Target.GetSharedCollisionEntry() == Source.GetSharedCollisionEntry());
	return true;
}

#endif
