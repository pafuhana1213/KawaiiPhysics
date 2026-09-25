// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "AnimNotifies/AnimNotify_KawaiiPhysicsTriggerGust.h"
#include "KawaiiPhysicsSharedTags.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace
{
	bool RoundTripAnimNotifyProperty(FAutomationTestBase& Test,
	                                 UAnimNotify_KawaiiPhysicsTriggerGust* Source,
	                                 UAnimNotify_KawaiiPhysicsTriggerGust* Target,
	                                 const FName PropertyName)
	{
		const FProperty* Property =
			FindFProperty<FProperty>(UAnimNotify_KawaiiPhysicsTriggerGust::StaticClass(), PropertyName);
		if (!Test.TestNotNull(FString::Printf(TEXT("%s property exists"), *PropertyName.ToString()), Property))
		{
			return false;
		}

		FString ExportedText;
		Property->ExportTextItem_Direct(
			ExportedText,
			Property->ContainerPtrToValuePtr<void>(Source),
			nullptr,
			Source,
			PPF_None);

		void* TargetValue = Property->ContainerPtrToValuePtr<void>(Target);
		FOutputDeviceNull ErrorText;
		const TCHAR* ImportResult =
			Property->ImportText_Direct(*ExportedText, TargetValue, Target, PPF_None, &ErrorText);
		return Test.TestNotNull(FString::Printf(TEXT("%s import succeeds"), *PropertyName.ToString()), ImportResult);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAnimNotifyTriggerGustTargetRoundTripTest,
                                 "KawaiiPhysics.AnimNotify.TriggerGustTargetRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAnimNotifyTriggerGustTargetRoundTripTest::RunTest(const FString& Parameters)
{
	UAnimNotify_KawaiiPhysicsTriggerGust* Notify =
		NewObject<UAnimNotify_KawaiiPhysicsTriggerGust>(GetTransientPackage(), NAME_None, RF_Transient);
	const FGameplayTag DefaultSharedTag = TAG_KawaiiPhysics_Shared_Default;

	bool bOk = true;
	bOk &= TestTrue(TEXT("Default GustTarget is Nodes"),
	                Notify->GustTarget == EKawaiiPhysicsGustTarget::Nodes);
	bOk &= TestTrue(TEXT("Default SharedPublisherTag is Shared.Default"),
	                Notify->SharedPublisherTag == DefaultSharedTag);

	Notify->GustTarget = EKawaiiPhysicsGustTarget::SharedPublisher;
	Notify->SharedPublisherTag = DefaultSharedTag;

	UAnimNotify_KawaiiPhysicsTriggerGust* RoundTripped =
		NewObject<UAnimNotify_KawaiiPhysicsTriggerGust>(GetTransientPackage(), NAME_None, RF_Transient);
	RoundTripped->GustTarget = EKawaiiPhysicsGustTarget::Nodes;
	RoundTripped->SharedPublisherTag = FGameplayTag();

	bOk &= RoundTripAnimNotifyProperty(
		*this,
		Notify,
		RoundTripped,
		GET_MEMBER_NAME_CHECKED(UAnimNotify_KawaiiPhysicsTriggerGust, GustTarget));
	bOk &= RoundTripAnimNotifyProperty(
		*this,
		Notify,
		RoundTripped,
		GET_MEMBER_NAME_CHECKED(UAnimNotify_KawaiiPhysicsTriggerGust, SharedPublisherTag));

	bOk &= TestTrue(TEXT("GustTarget round-trips"),
	                RoundTripped->GustTarget == EKawaiiPhysicsGustTarget::SharedPublisher);
	bOk &= TestTrue(TEXT("SharedPublisherTag round-trips"),
	                RoundTripped->SharedPublisherTag == DefaultSharedTag);

	return bOk;
}

#endif
