// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

namespace
{
	bool TestKawaiiPhysicsSharedPublisherWind(
		FAutomationTestBase& Test,
		const TCHAR* Context,
		const FKawaiiPhysicsSharedWindState& Actual,
		const FKawaiiPhysicsSharedWindState& Expected)
	{
		bool bResult = true;
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s bPublisherWindEnabled"), Context),
			Actual.bPublisherWindEnabled, Expected.bPublisherWindEnabled);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s Time"), Context), Actual.Time, Expected.Time);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s PublisherTimeScale"), Context),
			Actual.PublisherTimeScale, Expected.PublisherTimeScale);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s ConstantForce"), Context),
			Actual.Params.ConstantForce, Expected.Params.ConstantForce);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s bOverrideConstantForce"), Context),
			Actual.Params.bOverrideConstantForce, Expected.Params.bOverrideConstantForce);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s ActiveGust bIsActive"), Context),
			Actual.ActiveGust.bIsActive, Expected.ActiveGust.bIsActive);
		bResult &= Test.TestEqual(FString::Printf(TEXT("%s ActiveGust Strength"), Context),
			Actual.ActiveGust.Strength, Expected.ActiveGust.Strength);
		return bResult;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionSubsystemSupportsEditorPreviewTest,
                                 "KawaiiPhysics.SharedCollision.SubsystemSupportsEditorPreview",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionSubsystemSupportsEditorPreviewTest::RunTest(const FString& Parameters)
{
	const UKawaiiPhysicsSharedCollisionSubsystem* CDO = GetDefault<UKawaiiPhysicsSharedCollisionSubsystem>();
	UWorld* World = NewObject<UWorld>(GetTransientPackage(), NAME_None, RF_Transient);

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.EnableInPreviewWorld"));
	if (!TestNotNull(TEXT("EnableInPreviewWorld CVar exists"), CVar))
	{
		return false;
	}

	const int32 Saved = CVar->GetInt();
	ON_SCOPE_EXIT
	{
		CVar->Set(Saved, ECVF_SetByCode);
	};

	CVar->Set(1, ECVF_SetByCode);

	World->WorldType = EWorldType::EditorPreview;
	TestTrue(TEXT("EditorPreview world creates subsystem by default"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Game;
	TestTrue(TEXT("Game world creates subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Editor;
	TestTrue(TEXT("Editor world creates subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::PIE;
	TestTrue(TEXT("PIE world creates subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::GamePreview;
	TestFalse(TEXT("GamePreview world does not create subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Inactive;
	TestFalse(TEXT("Inactive world does not create subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::None;
	TestFalse(TEXT("None world does not create subsystem"), CDO->ShouldCreateSubsystem(World));

	CVar->Set(0, ECVF_SetByCode);

	World->WorldType = EWorldType::EditorPreview;
	TestFalse(TEXT("EditorPreview world does not create subsystem when disabled by CVar"),
	          CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Game;
	TestTrue(TEXT("Game world still creates subsystem when preview CVar is disabled"),
	         CDO->ShouldCreateSubsystem(World));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherEntryPublishReadTest,
                                 "KawaiiPhysics.SharedPublisher.EntryPublishRead",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherEntryPublishReadTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedPublisherEntry Entry;

	// provider ID 0 は未所有の番兵と衝突するため拒否される。
	{
		FKawaiiPhysicsSharedPublisherState ZeroState;
		const uint64 SerialBeforeZero = Entry.GetPublishSerial();
		TestFalse(TEXT("Zero provider ID is rejected"), Entry.PublishState(ZeroState, 0, 1, 10));
		TestEqual(TEXT("Zero provider ID keeps serial"), Entry.GetPublishSerial(), SerialBeforeZero);
		TestEqual(TEXT("Zero provider ID leaves entry unowned"), Entry.GetProviderID(), static_cast<uint64>(0));
	}

	FKawaiiPhysicsSharedPublisherState State;
	State.bSimpleWorldEnabled = true;
	State.GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily;
	State.SimpleWorldDesc.GatherIntervalSec = 0.05f;
	State.SimpleWorldDesc.bGatherFamilyMembers = true;
	State.Wind.bPublisherWindEnabled = true;
	State.Wind.Time = 12.5f;
	State.Wind.PublisherTimeScale = 0.5f;
	State.Wind.Params.bOverrideConstantForce = true;
	State.Wind.Params.ConstantForce = 3.0f;
	State.Wind.ActiveGust.bIsActive = true;
	State.Wind.ActiveGust.Strength = 7.0f;

	TestTrue(TEXT("Initial provider publishes"), Entry.PublishState(State, 11, 100, 10));
	TestEqual(TEXT("Publish serial after first publish"), Entry.GetPublishSerial(), static_cast<uint64>(1));
	TestEqual(TEXT("Provider ID after first publish"), Entry.GetProviderID(), static_cast<uint64>(11));
	TestEqual(TEXT("Last publish frame after first publish"), Entry.GetLastPublishFrame(), static_cast<uint64>(100));

	FKawaiiPhysicsSharedPublisherState ReadState;
	const uint64 ReadSerial = Entry.ReadState(ReadState);
	TestEqual(TEXT("ReadState returns current serial"), ReadSerial, static_cast<uint64>(1));
	TestTrue(TEXT("ReadState keeps SimpleWorld enabled"), ReadState.bSimpleWorldEnabled);
	TestTrue(TEXT("ReadState keeps gather scope"),
		ReadState.GatherScope == EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily);
	TestEqual(TEXT("ReadState keeps gather interval"), ReadState.SimpleWorldDesc.GatherIntervalSec, 0.05f);
	TestTrue(TEXT("ReadState keeps family members flag"), ReadState.SimpleWorldDesc.bGatherFamilyMembers);
	TestKawaiiPhysicsSharedPublisherWind(*this, TEXT("ReadState wind"), ReadState.Wind, State.Wind);

	FKawaiiPhysicsSharedPublisherState BlockedState = State;
	BlockedState.bSimpleWorldEnabled = false;
	BlockedState.SimpleWorldDesc.GatherIntervalSec = 0.75f;
	BlockedState.Wind.Time = 20.0f;
	TestFalse(TEXT("Live different provider is rejected"), Entry.PublishState(BlockedState, 22, 105, 10));

	FKawaiiPhysicsSharedPublisherState AfterRejected;
	Entry.ReadState(AfterRejected);
	TestTrue(TEXT("Rejected publish keeps SimpleWorld enabled"), AfterRejected.bSimpleWorldEnabled);
	TestEqual(TEXT("Rejected publish keeps gather interval"), AfterRejected.SimpleWorldDesc.GatherIntervalSec, 0.05f);
	TestEqual(TEXT("Rejected publish keeps wind time"), AfterRejected.Wind.Time, 12.5f);
	TestEqual(TEXT("Rejected publish keeps serial"), Entry.GetPublishSerial(), static_cast<uint64>(1));

	TestTrue(TEXT("Expired previous provider allows replacement"), Entry.PublishState(BlockedState, 22, 111, 10));
	TestEqual(TEXT("Publish serial after provider replacement"), Entry.GetPublishSerial(), static_cast<uint64>(2));
	TestEqual(TEXT("Provider ID after provider replacement"), Entry.GetProviderID(), static_cast<uint64>(22));
	TestEqual(TEXT("Last publish frame after provider replacement"), Entry.GetLastPublishFrame(), static_cast<uint64>(111));

	FKawaiiPhysicsSharedWindState ReadWind;
	const uint64 WindSerial = Entry.ReadWindState(ReadWind);
	TestEqual(TEXT("ReadWindState returns current serial"), WindSerial, static_cast<uint64>(2));
	TestKawaiiPhysicsSharedPublisherWind(*this, TEXT("ReadWindState"), ReadWind, BlockedState.Wind);

	TestFalse(TEXT("Entry is not expired within max age"), Entry.IsExpired(120, 10));
	TestTrue(TEXT("Entry is expired beyond max age"), Entry.IsExpired(122, 10));
	Entry.MarkExpired();
	TestTrue(TEXT("MarkExpired makes entry expired"), Entry.IsExpired(111, 10));

	// 期限切れ Entry は同じ provider が publish しても復活しない。
	const uint64 ExpiredSerial = Entry.GetPublishSerial();
	FKawaiiPhysicsSharedPublisherState RevivedState = BlockedState;
	RevivedState.Wind.Time = 33.0f;
	TestFalse(TEXT("Expired entry rejects publish"), Entry.PublishState(RevivedState, 22, 200, 10));
	TestEqual(TEXT("Expired entry keeps serial"), Entry.GetPublishSerial(), ExpiredSerial);
	TestTrue(TEXT("Expired entry stays expired"), Entry.IsExpired(200, 10));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherGustQueueTest,
                                 "KawaiiPhysics.SharedPublisher.GustQueue",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherGustQueueTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedPublisherEntry Entry;
	Entry.RequestGust(1.0f, 0.1f, 0.2f, 0.3f);
	Entry.RequestGust(2.0f, 0.4f, 0.5f, 0.6f);
	Entry.RequestGustStop(0.7f);

	TArray<FKawaiiPhysicsSharedPublisherGustRequest> Requests;
	Entry.ConsumePendingGustRequests(Requests);
	TestEqual(TEXT("Consumes all gust requests"), Requests.Num(), 3);

	if (Requests.Num() == 3)
	{
		TestFalse(TEXT("First request starts gust"), Requests[0].bStop);
		TestEqual(TEXT("First request strength"), Requests[0].Strength, 1.0f);
		TestEqual(TEXT("First request rise"), Requests[0].RiseTime, 0.1f);
		TestEqual(TEXT("First request decay"), Requests[0].DecayTime, 0.2f);
		TestEqual(TEXT("First request hold"), Requests[0].HoldTime, 0.3f);

		TestFalse(TEXT("Second request starts gust"), Requests[1].bStop);
		TestEqual(TEXT("Second request strength"), Requests[1].Strength, 2.0f);
		TestEqual(TEXT("Second request rise"), Requests[1].RiseTime, 0.4f);
		TestEqual(TEXT("Second request decay"), Requests[1].DecayTime, 0.5f);
		TestEqual(TEXT("Second request hold"), Requests[1].HoldTime, 0.6f);

		TestTrue(TEXT("Third request stops gust"), Requests[2].bStop);
		TestEqual(TEXT("Third request blend out"), Requests[2].BlendOutTime, 0.7f);
	}

	Requests.Reset();
	Entry.ConsumePendingGustRequests(Requests);
	TestEqual(TEXT("Second consume is empty"), Requests.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherPublisherRequestQueueTest,
                                 "KawaiiPhysics.SharedPublisher.PublisherRequestQueue",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherPublisherRequestQueueTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSharedPublisherEntry Entry;

	FKawaiiPhysicsSimpleWorldCollisionSettings Settings;
	Settings.GatherInterval = 0.4f;
	Settings.bGatherFamilyMembers = true;
	Entry.RequestPublisherEnabled(false);
	Entry.RequestSimpleWorldSettings(Settings);

	FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests Requests;
	TestTrue(TEXT("Consumes pending publisher requests"), Entry.ConsumePendingPublisherRequests(Requests));
	TestTrue(TEXT("Enabled request is set"), Requests.Enabled.IsSet());
	if (Requests.Enabled.IsSet())
	{
		TestFalse(TEXT("Enabled request value is false"), Requests.Enabled.GetValue());
	}
	TestTrue(TEXT("SimpleWorld settings request is set"), Requests.SimpleWorldSettings.IsSet());
	if (Requests.SimpleWorldSettings.IsSet())
	{
		TestEqual(TEXT("SimpleWorld settings gather interval"),
			Requests.SimpleWorldSettings.GetValue().GatherInterval, 0.4f);
		TestTrue(TEXT("SimpleWorld settings family members flag"),
			Requests.SimpleWorldSettings.GetValue().bGatherFamilyMembers);
	}

	FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests EmptyRequests;
	TestFalse(TEXT("Second publisher request consume is empty"),
		Entry.ConsumePendingPublisherRequests(EmptyRequests));

	FKawaiiPhysicsSimpleWorldCollisionSettings DefaultSettings;
	const FKawaiiPhysicsSimpleWorldCollisionDesc DefaultDesc =
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldCollisionDesc(DefaultSettings);
	TestTrue(TEXT("Default settings use ActorFamily gather scope"),
		DefaultDesc.GatherScope == EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily);
	TestFalse(TEXT("Default settings keep provider enabled"), DefaultDesc.bProviderDisabled);
	TestTrue(TEXT("Default settings keep collision channel unspecified"), DefaultDesc.CollisionChannel == ECC_MAX);
	TestFalse(TEXT("Default settings do not gather family members"), DefaultDesc.bGatherFamilyMembers);

	DefaultSettings.bEnabled = false;
	const FKawaiiPhysicsSimpleWorldCollisionDesc DisabledDesc =
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldCollisionDesc(DefaultSettings);
	TestTrue(TEXT("Disabled settings disable provider"), DisabledDesc.bProviderDisabled);

	return true;
}

#endif
