// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "AnimNode_KawaiiPhysicsSharedPublisherInternal.h"
#include "AnimNotifies/AnimNotify_KawaiiPhysicsTriggerGust.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsLibrary.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsRegressionA, "KawaiiPhysics.Test.Regression.A");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsRegressionB, "KawaiiPhysics.Test.Regression.B");

namespace
{
	// 実際の PreUpdate を通し、World 時計だけを決定的に進める。
	struct FSharedRegressionWorld
	{
		UWorld* World;
		AActor* RootA;
		AActor* RootB;
		AActor* Child;
		UAnimInstance* Anim;

		FSharedRegressionWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &Values);
			RootA = World->SpawnActor<AActor>();
			RootB = World->SpawnActor<AActor>();
			RootA->SetRootComponent(NewObject<USceneComponent>(RootA));
			RootB->SetRootComponent(NewObject<USceneComponent>(RootB));
			Child = World->SpawnActor<AActor>();
			USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Child);
			Child->AddInstanceComponent(Mesh);
			Child->SetRootComponent(Mesh);
			SetFamily(RootA);
			Anim = NewObject<UAnimInstance>(Mesh);
		}

		~FSharedRegressionWorld() { World->DestroyWorld(false); }

		void SetFamily(AActor* Root)
		{
			// FamilyRoot は Owner 値ではなく Actor のアタッチ階層で定義される。
			Child->SetOwner(Root);
			Child->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
		}

		void Publish(FAnimNode_KawaiiPhysicsSharedPublisher& Publisher, double Time, float Dt = 0.0f)
		{
			World->TimeSeconds = Time;
			Publisher.PreUpdate(Anim);
			FAnimInstanceProxy Proxy(Anim);
			FAnimationUpdateContext Context(&Proxy, Dt);
			Publisher.Update_AnyThread(Context);
		}

		void Consume(FKawaiiPhysicsTestAccessor& Accessor, FKawaiiPhysics_ExternalForce_ProceduralWind& Wind,
			double Time, float Dt = 0.1f)
		{
			World->TimeSeconds = Time;
			Accessor.Node.PreUpdate(Anim);
			Accessor.SetTimeState(Dt, Dt);
			FAnimInstanceProxy Proxy(Anim);
			FComponentSpacePoseContext Context(&Proxy);
			Wind.PreApply(Accessor.Node, Context);
		}
	};

	void ConfigureRegressionPublisher(FAnimNode_KawaiiPhysicsSharedPublisher& Publisher, FGameplayTag Tag)
	{
		Publisher.SharedGroupTag = Tag;
		Publisher.SharedWind.EnsureRuntimeState();
		Publisher.SharedWind.ConstantForce = 0.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSourcePinRegressionTest,
	"KawaiiPhysics.SimpleWorld.Regression.DirectSourceAndTagChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSourcePinRegressionTest::RunTest(const FString& Parameters)
{
	FSharedRegressionWorld Fixture;
	FAnimNode_KawaiiPhysicsSharedPublisher PublisherA, PublisherB;
	ConfigureRegressionPublisher(PublisherA, TAG_KawaiiPhysicsRegressionA);
	ConfigureRegressionPublisher(PublisherB, TAG_KawaiiPhysicsRegressionB);
	Fixture.Publish(PublisherA, 0.0);
	Fixture.Publish(PublisherB, 0.0);
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.bUseSimpleWorldCollision = true;
	Accessor.Node.PreUpdate(Fixture.Anim);
	FAnimInstanceProxy Proxy(Fixture.Anim);
	FComponentSpacePoseContext Context(&Proxy);
	const auto Subsystem = Fixture.World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>();
	const auto SharedA = PublisherA.GetSimpleWorldEntry();
	const auto SharedB = PublisherB.GetSimpleWorldEntry();
	if (!TestTrue(TEXT("Both providers exist"), SharedA.IsValid() && SharedB.IsValid())) return false;

	const EKawaiiPhysicsSimpleWorldCollisionSource Sources[] = {
		EKawaiiPhysicsSimpleWorldCollisionSource::Local,
		EKawaiiPhysicsSimpleWorldCollisionSource::Shared,
		EKawaiiPhysicsSimpleWorldCollisionSource::Auto };
	for (const auto From : Sources)
	{
		for (const auto To : Sources)
		{
			for (const bool bChangeTag : {false, true})
			{
				// setter / reinit を介さず、AnimGraph のピン更新と同じ代入だけで切り替える。
				Accessor.Node.SimpleWorldCollisionSource = From;
				Accessor.Node.SimpleWorldCollisionSharedTag = TAG_KawaiiPhysicsRegressionA;
				Accessor.InitializeSimpleWorldCollision();
				Accessor.UpdateSimpleWorldCollisionLimits(Context);
				const auto Local = Subsystem->FindSimpleWorldEntry(
					FKawaiiPhysicsSimpleWorldRegistryKey::MakeLocalKey(Fixture.Anim->GetSkelMeshComponent()));
				Accessor.Node.SimpleWorldCollisionSource = To;
				const FGameplayTag NextTag = bChangeTag ? TAG_KawaiiPhysicsRegressionB : TAG_KawaiiPhysicsRegressionA;
				Accessor.Node.SimpleWorldCollisionSharedTag = NextTag;
				Accessor.UpdateSimpleWorldCollisionLimits(Context);
				const bool bShared = To != EKawaiiPhysicsSimpleWorldCollisionSource::Local;
				TestEqual(TEXT("New source resolved in same evaluation"), Accessor.IsSimpleWorldReaderMode(), bShared);
				TestEqual(TEXT("First tag reader registration"), SharedA->GetNumReaders(), bShared && !bChangeTag ? 1 : 0);
				TestEqual(TEXT("Second tag reader registration"), SharedB->GetNumReaders(), bShared && bChangeTag ? 1 : 0);
				if (bShared)
				{
					TestTrue(TEXT("New family/tag key"), Accessor.GetSimpleWorldReaderKey() ==
						FKawaiiPhysicsSimpleWorldRegistryKey::MakeSharedKey(Fixture.RootA, NextTag));
					if (Local.IsValid()) TestEqual(TEXT("Old local desc released"), Local->GetNumDescs(), 0);
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPublisherFamilyRegressionTest,
	"KawaiiPhysics.SharedPublisher.Regression.OwnerFamilyChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPublisherFamilyRegressionTest::RunTest(const FString& Parameters)
{
	FSharedRegressionWorld Fixture;
	FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
	ConfigureRegressionPublisher(Publisher, TAG_KawaiiPhysicsRegressionA);
	Fixture.Publish(Publisher, 0.0);
	const auto OldEntry = Publisher.GetSharedPublisherEntry();
	const auto OldCollision = Publisher.GetSimpleWorldEntry();
	if (!TestTrue(TEXT("Initial registration"), OldEntry.IsValid() && OldCollision.IsValid())) return false;
	Fixture.SetFamily(Fixture.RootB);
	Fixture.Publish(Publisher, 0.1);
	TestTrue(TEXT("Old entry expired"), OldEntry->IsMarkedExpired());
	TestEqual(TEXT("Old provider desc removed"), OldCollision->GetNumDescs(), 0);
	const auto NewEntry = Publisher.GetSharedPublisherEntry();
	TestTrue(TEXT("Moved to new family"), NewEntry.IsValid() && NewEntry != OldEntry);
	TestTrue(TEXT("New family lookup"), NewEntry == Fixture.World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>()
		->FindSharedPublisherEntry(Fixture.RootB, TAG_KawaiiPhysicsRegressionA));

	// 競合ノードが所有権を持たないまま所属を変えても、勝ち側の Entry を失効させない。
	FAnimNode_KawaiiPhysicsSharedPublisher Contender;
	ConfigureRegressionPublisher(Contender, TAG_KawaiiPhysicsRegressionA);
	Contender.PreUpdate(Fixture.Anim);
	Fixture.SetFamily(Fixture.RootA);
	Contender.PreUpdate(Fixture.Anim);
	TestFalse(TEXT("Competing publisher remains alive"), NewEntry->IsMarkedExpired());
	TestEqual(TEXT("Winning provider desc preserved"), Publisher.GetSimpleWorldEntry()->GetNumDescs(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedClockRegressionTest,
	"KawaiiPhysics.SharedPublisher.Regression.GameTimeClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedClockRegressionTest::RunTest(const FString& Parameters)
{
	FSharedRegressionWorld Fixture;
	FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
	ConfigureRegressionPublisher(Publisher, TAG_KawaiiPhysicsRegressionA);
	Fixture.Publish(Publisher, 0.0);
	UKawaiiPhysicsLibrary::StartProceduralWindGustOnSharedPublisher(Fixture.Child,
		TAG_KawaiiPhysicsRegressionA, 100.0f, 2.0f, 0.0f, 2.0f);
	Fixture.Publish(Publisher, 0.0);
	FKawaiiPhysicsTestAccessor Accessor;
	FKawaiiPhysics_ExternalForce_ProceduralWind Consumer;
	Consumer.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
	Consumer.SharedWindTag = TAG_KawaiiPhysicsRegressionA;
	Fixture.Consume(Accessor, Consumer, 0.0);
	for (int32 Frame = 1; Frame <= 10; ++Frame)
	{
		// Publisher は PreUpdate と Update の両方を完全に省略する。
		Fixture.Consume(Accessor, Consumer, Frame * 0.1);
	}
	TestEqual(TEXT("Consumer extrapolates ten missing frames"), Consumer.RuntimeState->Time, 1.0f);
	const float GustBefore = Consumer.RuntimeState->CachedGust;
	Publisher.SharedWind.TimeScale = 2.0f;
	Fixture.Publish(Publisher, 1.0, 0.1f);
	TestEqual(TEXT("Resume uses previously published scale"), Publisher.SharedWind.RuntimeState->Time, 1.0f);
	Fixture.Consume(Accessor, Consumer, 1.0);
	TestEqual(TEXT("No phase rewind"), Consumer.RuntimeState->Time, 1.0f);
	TestEqual(TEXT("No gust rewind"), Consumer.RuntimeState->CachedGust, GustBefore);
	Accessor.SetWarmingUpForTest(true);
	for (int32 Step = 0; Step < 10; ++Step) Fixture.Consume(Accessor, Consumer, 1.0);
	TestEqual(TEXT("Warm-up does not advance game clock"), Consumer.RuntimeState->Time, 1.0f);
	Accessor.SetWarmingUpForTest(false);
	Fixture.Consume(Accessor, Consumer, 1.1);
	const float ConsumerFirst = Consumer.RuntimeState->Time;
	Fixture.Publish(Publisher, 1.1, 0.1f);
	TestEqual(TEXT("Consumer-first ordering"), Publisher.SharedWind.RuntimeState->Time, ConsumerFirst);
	Fixture.Publish(Publisher, 1.2, 0.1f);
	Fixture.Consume(Accessor, Consumer, 1.2);
	TestEqual(TEXT("Publisher-first ordering"), Consumer.RuntimeState->Time, Publisher.SharedWind.RuntimeState->Time);
	Publisher.SharedWind.bIsEnabled = false;
	Fixture.Publish(Publisher, 2.2, 0.1f);
	const float DisabledTime = Publisher.SharedWind.RuntimeState->Time;
	Fixture.Consume(Accessor, Consumer, 3.2);
	TestEqual(TEXT("Disabled consumer clock pauses"), Consumer.RuntimeState->Time, DisabledTime);
	Publisher.SharedWind.bIsEnabled = true;
	Fixture.Publish(Publisher, 3.2, 0.1f);
	TestEqual(TEXT("Resume retains previously disabled interval"), Publisher.SharedWind.RuntimeState->Time, DisabledTime);
	Fixture.Publish(Publisher, 3.2, 1.0f);
	TestEqual(TEXT("Paused world ignores evaluation delta"), Publisher.SharedWind.RuntimeState->Time, DisabledTime);
	Publisher.ResetDynamics(ETeleportType::ResetPhysics);
	Fixture.Publish(Publisher, 3.2);
	Fixture.Consume(Accessor, Consumer, 3.2);
	TestEqual(TEXT("Physics reset resets clock anchor"), Consumer.RuntimeState->Time, 0.0f);
	TestFalse(TEXT("Physics reset clears gust"), Consumer.RuntimeState->ActiveGust.bIsActive);
	{
		FSharedRegressionWorld OtherWorld;
		Publisher.SharedWind.RuntimeState->Time = 10.0f;
		OtherWorld.Publish(Publisher, 5.0);
		OtherWorld.Consume(Accessor, Consumer, 5.0);
		TestEqual(TEXT("World change resets publisher clock"), Publisher.SharedWind.RuntimeState->Time, 0.0f);
		TestEqual(TEXT("World change resets consumer clock"), Consumer.RuntimeState->Time, 0.0f);
		Publisher.PreUpdate(nullptr);
		Accessor.Node.PreUpdate(nullptr);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsRealGustRegressionTest,
	"KawaiiPhysics.ProceduralWind.Regression.SharedRealTimeEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsRealGustRegressionTest::RunTest(const FString& Parameters)
{
	FSharedRegressionWorld Fixture;
	for (const float Scale : {0.0f, 0.5f, 1.0f, 2.0f})
	{
		FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
		ConfigureRegressionPublisher(Publisher, TAG_KawaiiPhysicsRegressionA);
		Publisher.SharedWind.TimeScale = Scale;
		Fixture.Publish(Publisher, 0.0);
		TestTrue(TEXT("Queue default real-time gust"), UKawaiiPhysicsLibrary::StartProceduralWindGustOnSharedPublisher(
			Fixture.Child, TAG_KawaiiPhysicsRegressionA, 100.0f, 2.0f, 0.0f, 1.0f));
		Fixture.Publish(Publisher, 0.0);
		FKawaiiPhysicsTestAccessor Accessor;
		FKawaiiPhysics_ExternalForce_ProceduralWind Consumer;
		Consumer.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
		Consumer.SharedWindTag = TAG_KawaiiPhysicsRegressionA;
		Fixture.Publish(Publisher, 1.5);
		Fixture.Consume(Accessor, Consumer, 1.5);
		TestEqual(TEXT("Real-time decay ignores wind scale"), Consumer.RuntimeState->CachedGust, 50.0f);
		Publisher.SharedWind.TimeScale = 2.0f - Scale;
		Fixture.Publish(Publisher, 1.5);
		Fixture.Consume(Accessor, Consumer, 2.0);
		TestEqual(TEXT("Real-time duration survives scale change and stall"), Consumer.RuntimeState->CachedGust, 0.0f);
		Fixture.Publish(Publisher, 2.0);
		TestEqual(TEXT("Publisher uses same envelope"), Publisher.SharedWind.ComputeWindSample(
			Publisher.SharedWind.RuntimeState->Time).Gust, 0.0f);
	}

	// C++ の既定値と Notify が選択する false は従来の wind 時間。
	FKawaiiPhysics_ExternalForce_ProceduralWind Wind;
	Wind.TimeScale = 0.5f;
	Wind.RequestGust(100.0f, 0.0f, 2.0f);
	Wind.ConsumePendingRequests();
	Wind.AdvanceWindTime(2.0f);
	TestEqual(TEXT("Legacy gust uses wind time"), Wind.ComputeWindSample(Wind.RuntimeState->Time).Gust, 50.0f);
	Wind.AdvanceWindTime(2.0f);
	TestEqual(TEXT("Legacy gust ends at scaled duration"), Wind.ComputeWindSample(Wind.RuntimeState->Time).Gust, 0.0f);
	Wind.ResetRuntimeState();
	Wind.RequestGust(100.0f, 0.0f, 2.0f, 0.0f, true);
	Wind.ConsumePendingRequests();
	Wind.AdvanceWindTime(1.0f);
	Wind.RequestGustStop(1.0f);
	Wind.ConsumePendingRequests();
	TestEqual(TEXT("Stop starts at original envelope strength"), Wind.ComputeWindSample(Wind.RuntimeState->Time).Gust, 50.0f);
	TestFalse(TEXT("Stop fade switches to wind clock"), Wind.RuntimeState->ActiveGust.bRealTimeEnvelope);
	Wind.AdvanceWindTime(1.0f);
	TestEqual(TEXT("Stop fade retains wind duration"), Wind.ComputeWindSample(Wind.RuntimeState->Time).Gust, 25.0f);
	Wind.AdvanceWindTime(1.0f);
	TestEqual(TEXT("Stop fade ends"), Wind.ComputeWindSample(Wind.RuntimeState->Time).Gust, 0.0f);

	FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
	ConfigureRegressionPublisher(Publisher, TAG_KawaiiPhysicsRegressionA);
	Publisher.SharedWind.TimeScale = 0.5f;
	Fixture.Publish(Publisher, 0.0);
	UKawaiiPhysicsLibrary::StartProceduralWindGustOnSharedPublisher(Fixture.Child,
		TAG_KawaiiPhysicsRegressionA, 100.0f, 2.0f, 0.0f, 2.0f);
	Fixture.Publish(Publisher, 0.0);
	Fixture.Publish(Publisher, 1.0);
	UKawaiiPhysicsLibrary::StopProceduralWindGustOnSharedPublisher(Fixture.Child,
		TAG_KawaiiPhysicsRegressionA, 1.0f);
	Fixture.Publish(Publisher, 1.0);
	FKawaiiPhysicsTestAccessor Accessor;
	FKawaiiPhysics_ExternalForce_ProceduralWind Consumer;
	Consumer.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
	Consumer.SharedWindTag = TAG_KawaiiPhysicsRegressionA;
	Fixture.Consume(Accessor, Consumer, 1.0);
	TestEqual(TEXT("Shared stop starts from real envelope"), Consumer.RuntimeState->CachedGust, 50.0f);
	Fixture.Consume(Accessor, Consumer, 2.0);
	TestEqual(TEXT("Shared stop extrapolates in wind time"), Consumer.RuntimeState->CachedGust, 25.0f);
	Fixture.Publish(Publisher, 3.0);
	Fixture.Consume(Accessor, Consumer, 3.0);
	TestEqual(TEXT("Shared stop finishes after wind duration"), Consumer.RuntimeState->CachedGust, 0.0f);

	UAnimNotify_KawaiiPhysicsTriggerGust* Notify = NewObject<UAnimNotify_KawaiiPhysicsTriggerGust>();
	Notify->GustTarget = EKawaiiPhysicsGustTarget::SharedPublisher;
	Notify->SharedPublisherTag = TAG_KawaiiPhysicsRegressionA;
	Notify->Strength = 100.0f;
	Notify->DecayTime = 2.0f;
	Notify->Notify(Fixture.Anim->GetSkelMeshComponent(), nullptr, FAnimNotifyEventReference());
	Fixture.Publish(Publisher, 3.0);
	TestFalse(TEXT("Shared Notify selects legacy wind time"), Publisher.SharedWind.RuntimeState->ActiveGust.bRealTimeEnvelope);
	Fixture.Publish(Publisher, 5.0);
	Fixture.Consume(Accessor, Consumer, 5.0);
	TestEqual(TEXT("Shared Notify preserves authored duration"), Consumer.RuntimeState->CachedGust, 50.0f);
	return true;
}

#endif
