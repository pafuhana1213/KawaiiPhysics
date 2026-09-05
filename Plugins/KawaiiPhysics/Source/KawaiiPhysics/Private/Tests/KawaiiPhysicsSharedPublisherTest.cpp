// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "KawaiiPhysicsSharedTags.h"
#include "AnimNode_KawaiiPhysicsSharedPublisherInternal.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherExpiredEntryReplacedOnAcquireTest,
                                 "KawaiiPhysics.SharedPublisher.ExpiredEntryReplacedOnAcquire",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherExpiredEntryReplacedOnAcquireTest::RunTest(const FString& Parameters)
{
	// FindOrCreateSharedPublisherEntry は GetFamilyRoot のアタッチ階層参照しか行わないので、World 無しの
	// Subsystem / Actor で足りる。GC に回収されないよう TStrongObjectPtr で保持する。
	// Tick は Initialize を呼ばない限り登録されない（UTickableWorldSubsystem は ETickableTickType::Never で構築される）。
	const TStrongObjectPtr<UKawaiiPhysicsSharedCollisionSubsystem> Subsystem(
		NewObject<UKawaiiPhysicsSharedCollisionSubsystem>(GetTransientPackage(), NAME_None, RF_Transient));
	const TStrongObjectPtr<AActor> Actor(NewObject<AActor>(GetTransientPackage(), NAME_None, RF_Transient));
	const FGameplayTag Tag = TAG_KawaiiPhysics_Shared_Default;

	const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> Entry1 =
		Subsystem->FindOrCreateSharedPublisherEntry(Actor.Get(), Tag);
	if (!TestTrue(TEXT("First acquire creates an entry"), Entry1.IsValid()))
	{
		return false;
	}

	FKawaiiPhysicsSharedPublisherState State;
	State.bPublisherEnabled = true;
	State.bSimpleWorldEnabled = true;
	TestTrue(TEXT("Provider A publishes on the first entry"), Entry1->PublishState(State, 0xA001, 1, 60));

	// provider A の ReleaseEntries 相当（Entry は Tick の Cleanup まで Registry に残る）
	Entry1->MarkExpired();
	TestTrue(TEXT("Released entry is marked expired"), Entry1->IsMarkedExpired());

	const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> Entry2 =
		Subsystem->FindOrCreateSharedPublisherEntry(Actor.Get(), Tag);
	if (!TestTrue(TEXT("Second acquire returns an entry"), Entry2.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("Marked expired entry is replaced on acquire"), Entry2 != Entry1);
	TestFalse(TEXT("Replacement entry is not marked expired"), Entry2->IsMarkedExpired());
	TestTrue(TEXT("Old entry stays marked expired"), Entry1->IsMarkedExpired());
	TestEqual(TEXT("Replacement entry starts unowned"), Entry2->GetProviderID(), static_cast<uint64>(0));
	TestTrue(TEXT("Provider B publishes on the replacement entry"),
		Entry2->PublishState(State, 0xA002, 2, 60));
	TestEqual(TEXT("Replacement entry is owned by Provider B"),
		Entry2->GetProviderID(), static_cast<uint64>(0xA002));

	// 旧 Entry は publish を拒否し続け、掴んだままの provider は再取得へ回る
	TestFalse(TEXT("Old entry keeps rejecting publishes"), Entry1->PublishState(State, 0xA001, 3, 60));

	TestTrue(TEXT("Lookup returns the replacement entry"),
		Subsystem->FindSharedPublisherEntry(Actor.Get(), Tag) == Entry2);
	TestTrue(TEXT("Live entry is reused on the next acquire"),
		Subsystem->FindOrCreateSharedPublisherEntry(Actor.Get(), Tag) == Entry2);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedPublisherPublishHelperUpdateTest,
                                 "KawaiiPhysics.SharedPublisher.PublishHelperUpdate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedPublisherPublishHelperUpdateTest::RunTest(const FString& Parameters)
{
	constexpr uint64 SourceID = 0xA001;
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry = MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry =
		MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe> WindState =
		MakeShared<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>();

	FKawaiiPhysicsSharedPublishInputs Defaults;
	FKawaiiPhysicsSharedPublishHelper Helper;
	Helper.SetSourceID(SourceID);
	Helper.SetEntries(PublisherEntry, SimpleWorldEntry, SkelComp);
	Helper.ResetEffectiveValues(Defaults);

	FKawaiiPhysicsSharedPublishInputs Inputs = Defaults;
	uint64 PreviousSerial = 0;
	for (uint64 Frame = 1; Frame <= 3; ++Frame)
	{
		TestTrue(FString::Printf(TEXT("Update frame %llu publishes"), static_cast<unsigned long long>(Frame)),
			Helper.Update(Inputs, WindState, 0.1f, Frame, 60));
		TestTrue(TEXT("Publish serial increases"), Helper.GetLastPublishSerial() > PreviousSerial);
		PreviousSerial = Helper.GetLastPublishSerial();
	}

	TestEqual(TEXT("Wind time accumulates at scale 1"), WindState->Time, 0.3f);
	FKawaiiPhysicsSharedPublisherState ReadState;
	PublisherEntry->ReadState(ReadState);
	TestEqual(TEXT("Published wind time matches runtime state"), ReadState.Wind.Time, 0.3f);
	TestTrue(TEXT("SimpleWorld is enabled"), ReadState.bSimpleWorldEnabled);
	TestEqual(TEXT("Published default settings gather interval"),
		ReadState.SimpleWorldSettings.GatherInterval, Defaults.SimpleWorld.GatherInterval);
	TestTrue(TEXT("SimpleWorld provider desc exists"), SimpleWorldEntry->HasProviderDesc());
	TestTrue(TEXT("Provider SkelComp is preserved"), SimpleWorldEntry->GetPrimarySkelComp() == SkelComp);
	TestEqual(TEXT("Provider heartbeat reaches frame 3"), SimpleWorldEntry->GetLastProviderFrame(), static_cast<uint64>(3));
	TestEqual(TEXT("SetDesc called only for first desc"), Helper.GetNumSetDescCalls(), 1);

	Inputs.WindTimeScale = 2.0f;
	TestTrue(TEXT("TimeScale 2 publishes"), Helper.Update(Inputs, WindState, 0.1f, 4, 60));
	TestEqual(TEXT("Wind time accumulates at scale 2"), WindState->Time, 0.5f);
	TestEqual(TEXT("SetDesc not called for wind-only change"), Helper.GetNumSetDescCalls(), 1);

	Inputs.SimpleWorld.GatherInterval = 0.5f;
	TestTrue(TEXT("Desc change publishes"), Helper.Update(Inputs, WindState, 0.1f, 5, 60));
	TestEqual(TEXT("SetDesc called for desc change"), Helper.GetNumSetDescCalls(), 2);
	FKawaiiPhysicsSimpleWorldCollisionDesc MergedDesc;
	TestTrue(TEXT("Merged desc exists after desc change"), SimpleWorldEntry->BuildMergedDesc(MergedDesc));
	TestEqual(TEXT("Merged desc follows gather interval"), MergedDesc.GatherIntervalSec, 0.5f);

	Inputs.bEnabled = false;
	const float TimeBeforeDisabled = WindState->Time;
	TestTrue(TEXT("Disabled state still publishes"), Helper.Update(Inputs, WindState, 0.1f, 6, 60));
	PublisherEntry->ReadState(ReadState);
	TestFalse(TEXT("Disabled publish disables SimpleWorld"), ReadState.bSimpleWorldEnabled);
	TestTrue(TEXT("Disabled publish marks provider disabled"), ReadState.SimpleWorldDesc.bProviderDisabled);
	TestFalse(TEXT("Disabled publish disables wind"), ReadState.Wind.bPublisherWindEnabled);
	TestFalse(TEXT("Disabled publish clears publisher enabled"), ReadState.bPublisherEnabled);
	TestEqual(TEXT("Disabled publish stops wind time"), WindState->Time, TimeBeforeDisabled);
	TestTrue(TEXT("SimpleWorld entry reports provider disabled"), SimpleWorldEntry->IsProviderDisabled());

	Inputs.bEnabled = true;
	TestTrue(TEXT("Re-enabled state publishes"), Helper.Update(Inputs, WindState, 0.1f, 7, 60));
	PublisherEntry->ReadState(ReadState);
	TestTrue(TEXT("Re-enabled publish enables SimpleWorld"), ReadState.bSimpleWorldEnabled);
	TestTrue(TEXT("Re-enabled publish restores publisher enabled"), ReadState.bPublisherEnabled);
	TestFalse(TEXT("Re-enabled publish clears provider disabled"), ReadState.SimpleWorldDesc.bProviderDisabled);

	PublisherEntry->RequestPublisherEnabled(false);
	TestTrue(TEXT("Pending enabled request publishes"), Helper.Update(Inputs, WindState, 0.1f, 8, 60));
	TestFalse(TEXT("Pending enabled request changes effective enabled"), Helper.IsEffectiveEnabled());

	FKawaiiPhysicsSimpleWorldCollisionSettings PendingSettings = Inputs.SimpleWorld;
	PendingSettings.GatherInterval = 0.25f;
	PublisherEntry->RequestSimpleWorldSettings(PendingSettings);
	TestTrue(TEXT("Pending settings request publishes"), Helper.Update(Inputs, WindState, 0.1f, 9, 60));
	TestEqual(TEXT("Pending settings update effective gather interval"),
		Helper.GetEffectiveSimpleWorldSettings().GatherInterval, 0.25f);
	PublisherEntry->ReadState(ReadState);
	TestEqual(TEXT("Published state keeps effective settings"),
		ReadState.SimpleWorldSettings.GatherInterval, 0.25f);
	TestTrue(TEXT("Pending settings persist after consume"), Helper.Update(Inputs, WindState, 0.1f, 10, 60));
	TestEqual(TEXT("Effective settings persist after consume"),
		Helper.GetEffectiveSimpleWorldSettings().GatherInterval, 0.25f);

	Helper.ResetEffectiveValues(Defaults);
	TestTrue(TEXT("Reset effective values publishes defaults"), Helper.Update(Defaults, WindState, 0.1f, 11, 60));
	TestTrue(TEXT("Reset effective values restores enabled"), Helper.IsEffectiveEnabled());
	TestEqual(TEXT("Reset effective values restores gather interval"),
		Helper.GetEffectiveSimpleWorldSettings().GatherInterval, Defaults.SimpleWorld.GatherInterval);

	PublisherEntry->RequestPublisherEnabled(false);
	TestTrue(TEXT("Pending disable before UPROPERTY change publishes"), Helper.Update(Defaults, WindState, 0.1f, 12, 60));
	TestFalse(TEXT("Pending disable takes effect"), Helper.IsEffectiveEnabled());
	FKawaiiPhysicsSharedPublishInputs ChangedInputs = Defaults;
	ChangedInputs.bEnabled = false;
	TestTrue(TEXT("UPROPERTY false change publishes"), Helper.Update(ChangedInputs, WindState, 0.1f, 13, 60));
	TestFalse(TEXT("UPROPERTY false keeps effective false"), Helper.IsEffectiveEnabled());
	ChangedInputs.bEnabled = true;
	TestTrue(TEXT("UPROPERTY true change publishes"), Helper.Update(ChangedInputs, WindState, 0.1f, 14, 60));
	TestTrue(TEXT("UPROPERTY true restores effective true"), Helper.IsEffectiveEnabled());

	{
		TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> ConflictPublisherEntry =
			MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
		TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> ConflictSimpleWorldEntry =
			MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		FKawaiiPhysicsSharedPublishHelper ProviderA;
		FKawaiiPhysicsSharedPublishHelper ProviderB;
		ProviderA.SetSourceID(0xA001);
		ProviderB.SetSourceID(0xA002);
		ProviderA.SetEntries(ConflictPublisherEntry, ConflictSimpleWorldEntry, SkelComp);
		ProviderB.SetEntries(ConflictPublisherEntry, ConflictSimpleWorldEntry, SkelComp);
		ProviderA.ResetEffectiveValues(Defaults);
		ProviderB.ResetEffectiveValues(Defaults);

		TestTrue(TEXT("Provider A publishes conflict setup"),
			ProviderA.Update(Defaults, WindState, 0.0f, 1, 60));
		const uint64 ConflictSerial = ConflictPublisherEntry->GetPublishSerial();
		TestEqual(TEXT("Provider A is the only registered provider"),
			ConflictSimpleWorldEntry->GetNumDescs(), 1);

		// 負け側は Provider A と違う設定を持たせ、収集 Desc へ混入しないことを確認する。
		// GatherInterval は Merge が min、bGatherFamilyMembers は or なので、混入すればマージ結果が変わる値を選ぶ。
		FKawaiiPhysicsSharedPublishInputs ConflictInputs = Defaults;
		ConflictInputs.SimpleWorld.GatherInterval = 0.05f;
		ConflictInputs.SimpleWorld.bGatherFamilyMembers = true;
		ConflictInputs.bEnabled = false;

		AddExpectedError(TEXT("Kawaii Physics Shared Publisher rejected publish"),
		                 EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Provider B is rejected while Provider A is alive"),
			ProviderB.Update(ConflictInputs, WindState, 0.0f, 2, 60));
		TestEqual(TEXT("Rejected provider keeps serial"), ConflictPublisherEntry->GetPublishSerial(), ConflictSerial);
		TestFalse(TEXT("Rejected provider does not request reacquire"), ProviderB.NeedsEntryReacquire());
		TestEqual(TEXT("Rejected provider registers no desc"), ConflictSimpleWorldEntry->GetNumDescs(), 1);

		FKawaiiPhysicsSimpleWorldCollisionDesc ConflictMergedDesc;
		TestTrue(TEXT("Conflict merged desc exists"),
			ConflictSimpleWorldEntry->BuildMergedDesc(ConflictMergedDesc));
		TestEqual(TEXT("Conflict merged desc keeps Provider A gather interval"),
			ConflictMergedDesc.GatherIntervalSec, Defaults.SimpleWorld.GatherInterval);
		TestFalse(TEXT("Conflict merged desc ignores the rejected provider family members"),
			ConflictMergedDesc.bGatherFamilyMembers);
		TestFalse(TEXT("Conflict merged desc ignores the rejected provider disable"),
			ConflictMergedDesc.bProviderDisabled);

		// BP からの Pending 要求は勝ち側（Provider A）だけが消費する。
		ConflictPublisherEntry->RequestPublisherEnabled(false);
		TestFalse(TEXT("Provider B stays rejected while a request is pending"),
			ProviderB.Update(ConflictInputs, WindState, 0.0f, 3, 60));
		TestTrue(TEXT("Provider A keeps publishing"),
			ProviderA.Update(Defaults, WindState, 0.0f, 4, 60));
		TestFalse(TEXT("Provider A consumes the pending disable request"), ProviderA.IsEffectiveEnabled());
		TestTrue(TEXT("Provider B keeps its own effective enabled"), ProviderB.IsEffectiveEnabled());

		ProviderA.ReleaseEntries();
		TestEqual(TEXT("Release removes the winning provider desc"),
			ConflictSimpleWorldEntry->GetNumDescs(), 0);
		TestFalse(TEXT("Expired entry rejects Provider B"),
			ProviderB.Update(ConflictInputs, WindState, 0.0f, 5, 60));
		TestTrue(TEXT("Expired entry requests reacquire"), ProviderB.NeedsEntryReacquire());
		// 期限切れ経路では provider Desc も取り下げるので、幽霊 Desc が収集側に残らない
		TestEqual(TEXT("Expired entry leaves Provider B unregistered"),
			ConflictSimpleWorldEntry->GetNumDescs(), 0);
	}

	// 自分が provider の Entry を外部（Subsystem の Cleanup 等）で期限切れにされた場合も、
	// 次の Update で登録済みの provider Desc を取り下げてから再取得を要求する。
	{
		TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> ExpiringPublisherEntry =
			MakeShared<FKawaiiPhysicsSharedPublisherEntry>();
		TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> ExpiringSimpleWorldEntry =
			MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
		FKawaiiPhysicsSharedPublishHelper ProviderC;
		ProviderC.SetSourceID(0xA003);
		ProviderC.SetEntries(ExpiringPublisherEntry, ExpiringSimpleWorldEntry, SkelComp);
		ProviderC.ResetEffectiveValues(Defaults);

		TestTrue(TEXT("Provider C publishes before expiry"),
			ProviderC.Update(Defaults, WindState, 0.0f, 1, 60));
		TestEqual(TEXT("Provider C registers its desc"), ExpiringSimpleWorldEntry->GetNumDescs(), 1);

		ExpiringPublisherEntry->MarkExpired();
		TestFalse(TEXT("Expired entry rejects its own provider"),
			ProviderC.Update(Defaults, WindState, 0.0f, 2, 60));
		TestTrue(TEXT("Expired entry requests reacquire for its own provider"),
			ProviderC.NeedsEntryReacquire());
		TestEqual(TEXT("Expired entry withdraws the provider desc"),
			ExpiringSimpleWorldEntry->GetNumDescs(), 0);
	}

	const float TimeBeforeRelease = WindState->Time;
	Helper.ReleaseEntries();
	TestFalse(TEXT("Release removes provider desc"), SimpleWorldEntry->HasProviderDesc());
	TestEqual(TEXT("Release keeps wind time"), WindState->Time, TimeBeforeRelease);

	return true;
}

#endif
