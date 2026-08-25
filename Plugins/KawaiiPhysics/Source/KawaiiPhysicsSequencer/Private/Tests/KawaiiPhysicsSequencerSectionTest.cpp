// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Generators/MovieSceneEasingCurves.h"
#include "KawaiiPhysicsSequencerOverrideRegistry.h"
#include "KawaiiPhysicsWindPresetTags.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideSection.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideTemplate.h"
#include "MovieSceneKawaiiPhysicsSettingsOverrideTrack.h"

namespace
{
constexpr float GSequencerSectionTol = 0.000001f;

bool TestFloatNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected)
{
	return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	                     FMath::IsNearlyEqual(Actual, Expected, GSequencerSectionTol));
}

UMovieSceneKawaiiPhysicsSettingsOverrideSection* NewSection()
{
	return NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(GetTransientPackage());
}

void SetupLinearEaseIn(UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section)
{
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(1000)));
	Section->Easing.bManualEaseIn = true;
	Section->Easing.ManualEaseInDuration = 200;

	UMovieSceneBuiltInEasingFunction* LinearEase = NewObject<UMovieSceneBuiltInEasingFunction>(Section);
	LinearEase->Type = EMovieSceneBuiltInEasing::Linear;
	Section->Easing.EaseIn.SetObject(LinearEase);
	Section->Easing.EaseIn.SetInterface(static_cast<IMovieSceneEasingFunction*>(LinearEase));
}

FKawaiiPhysicsSettingsScale MakeScale()
{
	FKawaiiPhysicsSettingsScale Scale;
	Scale.Damping = 0.5f;
	Scale.Stiffness = 0.25f;
	Scale.WorldDampingLocation = 0.75f;
	Scale.WorldDampingRotation = 0.8f;
	Scale.Radius = 1.5f;
	Scale.LimitAngle = 0.6f;
	return Scale;
}

void ApplyScaleToSection(UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section,
                         const FKawaiiPhysicsSettingsScale& Scale)
{
	Section->Damping.SetDefault(Scale.Damping);
	Section->Stiffness.SetDefault(Scale.Stiffness);
	Section->WorldDampingLocation.SetDefault(Scale.WorldDampingLocation);
	Section->WorldDampingRotation.SetDefault(Scale.WorldDampingRotation);
	Section->Radius.SetDefault(Scale.Radius);
	Section->LimitAngle.SetDefault(Scale.LimitAngle);
}

bool TestScaleEqual(FAutomationTestBase& Test, const FKawaiiPhysicsSettingsScale& Actual,
                    const FKawaiiPhysicsSettingsScale& Expected)
{
	bool bOk = true;
	bOk &= TestFloatNear(Test, TEXT("Scale.Damping"), Actual.Damping, Expected.Damping);
	bOk &= TestFloatNear(Test, TEXT("Scale.Stiffness"), Actual.Stiffness, Expected.Stiffness);
	bOk &= TestFloatNear(Test, TEXT("Scale.WorldDampingLocation"), Actual.WorldDampingLocation,
	                     Expected.WorldDampingLocation);
	bOk &= TestFloatNear(Test, TEXT("Scale.WorldDampingRotation"), Actual.WorldDampingRotation,
	                     Expected.WorldDampingRotation);
	bOk &= TestFloatNear(Test, TEXT("Scale.Radius"), Actual.Radius, Expected.Radius);
	bOk &= TestFloatNear(Test, TEXT("Scale.LimitAngle"), Actual.LimitAngle, Expected.LimitAngle);
	return bOk;
}

bool TestChannelDefaultNear(FAutomationTestBase& Test, const TCHAR* Name, const FMovieSceneFloatChannel& Channel,
                            const float Expected)
{
	const TOptional<float> DefaultValue = Channel.GetDefault();
	bool bOk = Test.TestTrue(FString::Printf(TEXT("%s default set"), Name), DefaultValue.IsSet());
	if (DefaultValue.IsSet())
	{
		bOk &= TestFloatNear(Test, Name, DefaultValue.GetValue(), Expected);
	}
	return bOk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateWeightEasingTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateWeight_Easing",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateWeightEasingTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	SetupLinearEaseIn(Section);

	bool bOk = TestFloatNear(*this, TEXT("Ease start"), Section->EvaluateWeightAtTime(FFrameTime(0)), 0.0f);
	bOk &= TestFloatNear(*this, TEXT("Ease mid"), Section->EvaluateWeightAtTime(FFrameTime(100)), 0.5f);
	bOk &= TestFloatNear(*this, TEXT("Ease outside"), Section->EvaluateWeightAtTime(FFrameTime(500)), 1.0f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateWeightChannelDefaultTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateWeight_ChannelDefault",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateWeightChannelDefaultTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	SetupLinearEaseIn(Section);
	Section->Weight.SetDefault(0.5f);

	bool bOk = TestFloatNear(*this, TEXT("Default outside ease"), Section->EvaluateWeightAtTime(FFrameTime(500)),
	                         0.5f);
	bOk &= TestFloatNear(*this, TEXT("Default ease mid"), Section->EvaluateWeightAtTime(FFrameTime(100)), 0.25f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateWeightClampTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateWeight_Clamp",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateWeightClampTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(1000)));

	Section->Weight.SetDefault(2.0f);
	bool bOk = TestFloatNear(*this, TEXT("Clamp high"), Section->EvaluateWeightAtTime(FFrameTime(500)), 1.0f);

	Section->Weight.SetDefault(-1.0f);
	bOk &= TestFloatNear(*this, TEXT("Clamp low"), Section->EvaluateWeightAtTime(FFrameTime(500)), 0.0f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTemplateFromTrackTest,
                                 "KawaiiPhysics.Sequencer.Section.TemplateFromTrack",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTemplateFromTrackTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(Track->CreateNewSection());

	const FKawaiiPhysicsSettingsScale ExpectedScale = MakeScale();
	ApplyScaleToSection(Section, ExpectedScale);
	Section->bFilterExactMatch = true;
	Section->BlendOutTimeOnEnd = 0.75f;

	const FGameplayTag TestTag = FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Sequencer.Test")), false);
	if (TestTag.IsValid())
	{
		Section->FilterTags.AddTag(TestTag);
	}

	Track->AddSection(*Section);

	FMovieSceneEvalTemplatePtr TemplatePtr = Track->CreateTemplateForSection(*Section);
	bool bOk = TestTrue(TEXT("Template valid"), TemplatePtr.IsValid());
	if (!TemplatePtr.IsValid())
	{
		return false;
	}

	bOk &= TestTrue(TEXT("Template script struct"),
	                &TemplatePtr->GetScriptStruct() ==
	                FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate::StaticStruct());

	const FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate* Template =
		static_cast<const FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate*>(TemplatePtr.GetPtr());
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.Damping"), Template->Damping, ExpectedScale.Damping);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.Stiffness"), Template->Stiffness, ExpectedScale.Stiffness);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.WorldDampingLocation"), Template->WorldDampingLocation,
	                              ExpectedScale.WorldDampingLocation);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.WorldDampingRotation"), Template->WorldDampingRotation,
	                              ExpectedScale.WorldDampingRotation);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.Radius"), Template->Radius, ExpectedScale.Radius);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.LimitAngle"), Template->LimitAngle, ExpectedScale.LimitAngle);
	bOk &= TestTrue(TEXT("FilterTags copied"), Template->FilterTags == Section->FilterTags);
	bOk &= TestTrue(TEXT("Exact copied"), Template->bFilterExactMatch);
	bOk &= TestFloatNear(*this, TEXT("BlendOut copied"), Template->BlendOutTimeOnEnd, 0.75f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackRootFlagPropagatesToTemplateTest,
                                 "KawaiiPhysics.Sequencer.Section.Track_RootFlagPropagatesToTemplate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackRootFlagPropagatesToTemplateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UMovieSceneKawaiiPhysicsSettingsOverrideTrack* DefaultTrack =
		NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* DefaultSection =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(DefaultTrack->CreateNewSection());
	FMovieSceneEvalTemplatePtr DefaultTemplatePtr = DefaultTrack->CreateTemplateForSection(*DefaultSection);

	UMovieSceneKawaiiPhysicsSettingsOverrideTrack* RootTrack =
		NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>(GetTransientPackage());
	RootTrack->bIsRootTrack = true;
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* RootSection =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(RootTrack->CreateNewSection());
	FMovieSceneEvalTemplatePtr RootTemplatePtr = RootTrack->CreateTemplateForSection(*RootSection);

	bool bOk = TestTrue(TEXT("Default template valid"), DefaultTemplatePtr.IsValid());
	bOk &= TestTrue(TEXT("Root template valid"), RootTemplatePtr.IsValid());
	if (!DefaultTemplatePtr.IsValid() || !RootTemplatePtr.IsValid())
	{
		return false;
	}

	const FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate* DefaultTemplate =
		static_cast<const FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate*>(DefaultTemplatePtr.GetPtr());
	const FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate* RootTemplate =
		static_cast<const FMovieSceneKawaiiPhysicsSettingsOverrideSectionTemplate*>(RootTemplatePtr.GetPtr());
	bOk &= TestFalse(TEXT("Default track root flag"), DefaultTemplate->bIsRootTrack);
	bOk &= TestTrue(TEXT("Root track root flag"), RootTemplate->bIsRootTrack);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerChannelProxyAfterNewAndDuplicateTest,
                                 "KawaiiPhysics.Sequencer.Section.ChannelProxy_AfterNewAndDuplicate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerChannelProxyAfterNewAndDuplicateTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	bool bOk = TestEqual(TEXT("New channel count"),
	                     Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>().Num(), 7);

	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Duplicate =
		DuplicateObject<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(Section, GetTransientPackage());
	bOk &= TestEqual(TEXT("Duplicate channel count"),
	                 Duplicate->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>().Num(), 7);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateScaleChannelKeysTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateScale_ChannelKeys",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateScaleChannelKeysTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	Section->Damping.AddLinearKey(FFrameNumber(0), 0.5f);
	Section->Damping.AddLinearKey(FFrameNumber(1000), 1.5f);

	FKawaiiPhysicsSettingsScale Expected;
	FKawaiiPhysicsSettingsScale Actual = Section->EvaluateScaleAtTime(FFrameTime(500));
	Expected.Damping = 1.0f;
	bool bOk = TestScaleEqual(*this, Actual, Expected);

	Section->Damping.AddLinearKey(FFrameNumber(2000), -1.0f);
	Actual = Section->EvaluateScaleAtTime(FFrameTime(2000));
	bOk &= TestFloatNear(*this, TEXT("Scale.Damping clamp low"), Actual.Damping, 0.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.Stiffness unchanged"), Actual.Stiffness, 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.WorldDampingLocation unchanged"), Actual.WorldDampingLocation, 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.WorldDampingRotation unchanged"), Actual.WorldDampingRotation, 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.Radius unchanged"), Actual.Radius, 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale.LimitAngle unchanged"), Actual.LimitAngle, 1.0f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryStopForSectionTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_StopForSection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryStopForSectionTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* SectionA = NewSection();
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* SectionB = NewSection();

	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryOther = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionA, EntryB);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionB, EntryOther);

	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(SectionA);

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	bOk &= TestFalse(TEXT("Other section untouched"), EntryOther->bStopped);

	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(SectionA);
	bOk &= TestTrue(TEXT("EntryA still stopped"), EntryA->bStopped);
	bOk &= TestFalse(TEXT("Other section still untouched"), EntryOther->bStopped);

	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(SectionB);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryRegisterIdempotentTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_RegisterIdempotent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryRegisterIdempotentTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, Entry);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, Entry);

	bool bOk = TestEqual(TEXT("Single registered entry"),
	                     FKawaiiPhysicsSequencerOverrideRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section);
	bOk &= TestTrue(TEXT("Entry stopped"), Entry->bStopped);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section);
	bOk &= TestTrue(TEXT("Entry still stopped"), Entry->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryGetQueuedNodeCountTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_GetQueuedNodeCount",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryGetQueuedNodeCountTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	USkeletalMeshComponent* ComponentA = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	USkeletalMeshComponent* ComponentB = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	USkeletalMeshComponent* ComponentC = NewObject<USkeletalMeshComponent>(GetTransientPackage());

	// (a) Entry が一つも登録されていないセクションは bOutHasLiveEntry=false（未評価と 0 件を区別）
	bool bHasLiveEntry = true;
	int32 Count = FKawaiiPhysicsSequencerOverrideRegistry::Get().GetQueuedNodeCount(
		Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
	bool bOk = TestFalse(TEXT("No entries: bOutHasLiveEntry"), bHasLiveEntry);
	bOk &= TestEqual(TEXT("No entries: count"), Count, 0);

	// FilterTags がセクションと異なる Entry（フィルタ変更前の残留想定）
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> MismatchedFilterEntry =
		MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	MismatchedFilterEntry->Component = ComponentC;
	MismatchedFilterEntry->LastQueuedNodeCount = 11;
	MismatchedFilterEntry->FilterTags.AddTag(TAG_KawaiiPhysics_WindPreset_Breeze);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, MismatchedFilterEntry);

	// (b) フィルタが一致しない Entry しか無い場合は合算対象外→ bOutHasLiveEntry=false
	bHasLiveEntry = true;
	Count = FKawaiiPhysicsSequencerOverrideRegistry::Get().GetQueuedNodeCount(
		Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
	bOk &= TestFalse(TEXT("Filter mismatch only: bOutHasLiveEntry"), bHasLiveEntry);
	bOk &= TestEqual(TEXT("Filter mismatch only: count"), Count, 0);

	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> InvalidComponentEntry =
		MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	EntryA->Component = ComponentA;
	EntryA->LastQueuedNodeCount = 3;
	EntryB->Component = ComponentB;
	EntryB->LastQueuedNodeCount = 2;
	InvalidComponentEntry->LastQueuedNodeCount = 7;

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, EntryA);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, EntryB);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, InvalidComponentEntry);

	// (c) フィルタが一致し生存している Entry があれば合算されて bOutHasLiveEntry=true
	//     （FilterTags 不一致の MismatchedFilterEntry と Component 無効な InvalidComponentEntry は除外される）
	bHasLiveEntry = false;
	Count = FKawaiiPhysicsSequencerOverrideRegistry::Get().GetQueuedNodeCount(
		Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
	bOk &= TestTrue(TEXT("Matching entries: bOutHasLiveEntry"), bHasLiveEntry);
	bOk &= TestEqual(TEXT("Matching entries: count"), Count, 5);

	EntryA->Component = nullptr;
	EntryB->Component = nullptr;
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryRemoveSectionAtStopsOnlyRemovedTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_RemoveSectionAtStopsOnlyRemoved",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryRemoveSectionAtStopsOnlyRemovedTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* SectionA =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(Track->CreateNewSection());
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* SectionB =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsOverrideSection>(Track->CreateNewSection());
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();

	Track->AddSection(*SectionA);
	Track->AddSection(*SectionB);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionB, EntryB);

	Track->RemoveSectionAt(1);

	bool bOk = TestFalse(TEXT("EntryA not stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(SectionA);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEntryStopIdempotentTest,
                                 "KawaiiPhysics.Sequencer.Section.Entry_StopIdempotent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEntryStopIdempotentTest::RunTest(const FString& Parameters)
{
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();

	Entry->Stop();
	bool bOk = TestTrue(TEXT("Entry stopped"), Entry->bStopped);
	Entry->Stop();
	bOk &= TestTrue(TEXT("Entry still stopped"), Entry->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEntryStopImmediateOverrideTest,
                                 "KawaiiPhysics.Sequencer.Section.Entry_StopImmediateOverride",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEntryStopImmediateOverrideTest::RunTest(const FString& Parameters)
{
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();

	Entry->Stop(0.0f);
	bool bOk = TestTrue(TEXT("Entry stopped immediately"), Entry->bStopped);
	Entry->Stop(0.0f);
	bOk &= TestTrue(TEXT("Entry still stopped"), Entry->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryStopForSectionsNotInTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_StopForSectionsNotIn",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryStopForSectionsNotInTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>(GetTransientPackage());
	UMovieSceneSection* SectionA = Track->CreateNewSection();
	UMovieSceneSection* SectionB = Track->CreateNewSection();
	Track->AddSection(*SectionA);
	Track->AddSection(*SectionB);

	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionB, EntryB);

	TArray<UMovieSceneSection*> LiveSections;
	LiveSections.Add(SectionA);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSectionsNotIn(Track, LiveSections);

	bool bOk = TestFalse(TEXT("EntryA still active"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);

	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(SectionA);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryPruneInvalidEntriesTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_PruneInvalidEntries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryPruneInvalidEntriesTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	{
		TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
		FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, Entry);
		TestEqual(TEXT("Entry registered"),
		          FKawaiiPhysicsSequencerOverrideRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	}

	FKawaiiPhysicsSequencerOverrideRegistry::Get().PruneInvalidEntries();
	return TestEqual(TEXT("Invalid entry pruned"),
	                 FKawaiiPhysicsSequencerOverrideRegistry::Get().CountEntriesForSectionForTesting(Section), 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryStopAllTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_StopAll",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryStopAllTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* SectionA = NewSection();
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* SectionB = NewSection();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(SectionB, EntryB);

	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopAll();

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	bOk &= TestEqual(TEXT("SectionA entries removed"),
	                 FKawaiiPhysicsSequencerOverrideRegistry::Get().CountEntriesForSectionForTesting(SectionA), 0);
	bOk &= TestEqual(TEXT("SectionB entries removed"),
	                 FKawaiiPhysicsSequencerOverrideRegistry::Get().CountEntriesForSectionForTesting(SectionB), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerPreAnimatedRestoreStopsRecreatedEntryTest,
                                 "KawaiiPhysics.Sequencer.Section.PreAnimated_RestoreStopsRecreatedEntry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerPreAnimatedRestoreStopsRecreatedEntryTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	TSharedRef<uint8> Owner = MakeShared<uint8>(0);
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry1 = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry2 = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	Entry1->Owner = Owner;
	Entry2->Owner = Owner;

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, Entry1);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, Entry2);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section, Owner);

	bool bOk = TestTrue(TEXT("Entry1 stopped"), Entry1->bStopped);
	bOk &= TestTrue(TEXT("Entry2 stopped"), Entry2->bStopped);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerPreAnimatedRestoreDoesNotStopOtherOwnerTest,
                                 "KawaiiPhysics.Sequencer.Section.PreAnimated_RestoreDoesNotStopOtherOwner",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerPreAnimatedRestoreDoesNotStopOtherOwnerTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	TSharedRef<uint8> OwnerA = MakeShared<uint8>(0);
	TSharedRef<uint8> OwnerB = MakeShared<uint8>(0);
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	EntryA->Owner = OwnerA;
	EntryB->Owner = OwnerB;

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, EntryA);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, EntryB);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section, OwnerA);

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestFalse(TEXT("EntryB still active"), EntryB->bStopped);
	bOk &= TestEqual(TEXT("Other owner remains"),
	                 FKawaiiPhysicsSequencerOverrideRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerPreAnimatedRestoreStopsOnlyRestoredComponentTest,
                                 "KawaiiPhysics.Sequencer.Section.PreAnimated_RestoreStopsOnlyRestoredComponent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerPreAnimatedRestoreStopsOnlyRestoredComponentTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	TSharedRef<uint8> Owner = MakeShared<uint8>(0);
	USkeletalMeshComponent* ComponentA = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	USkeletalMeshComponent* ComponentB = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	EntryA->Owner = Owner;
	EntryA->Component = ComponentA;
	EntryB->Owner = Owner;
	EntryB->Component = ComponentB;

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, EntryA);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, EntryB);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section, Owner, ComponentA);

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestFalse(TEXT("EntryB still active"), EntryB->bStopped);
	bOk &= TestEqual(TEXT("Other component remains"),
	                 FKawaiiPhysicsSequencerOverrideRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerPreAnimatedRestoreExpiredOwnerIsNoopTest,
                                 "KawaiiPhysics.Sequencer.Section.PreAnimated_RestoreExpiredOwnerIsNoop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerPreAnimatedRestoreExpiredOwnerIsNoopTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	TWeakPtr<uint8> ExpiredOwner;
	TSharedRef<FKawaiiPhysicsSequencerOverrideEntry> Entry = MakeShared<FKawaiiPhysicsSequencerOverrideEntry>();
	{
		TSharedRef<uint8> Owner = MakeShared<uint8>(0);
		ExpiredOwner = Owner;
		Entry->Owner = Owner;
	}

	FKawaiiPhysicsSequencerOverrideRegistry::Get().Register(Section, Entry);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section, ExpiredOwner);

	bool bOk = TestFalse(TEXT("Entry still active"), Entry->bStopped);
	bOk &= TestEqual(TEXT("Entry remains"),
	                 FKawaiiPhysicsSequencerOverrideRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopForSection(Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerSectionDefaultsTest,
                                 "KawaiiPhysics.Sequencer.Section.Section_Defaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerSectionDefaultsTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsOverrideSection* Section = NewSection();
	UMovieSceneKawaiiPhysicsSettingsOverrideTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsOverrideTrack>(GetTransientPackage());

	const FOptionalMovieSceneBlendType BlendType = Section->GetBlendType();
	bool bOk = TestTrue(TEXT("Section blend valid"), BlendType.IsValid());
	bOk &= TestTrue(TEXT("Section blend absolute"), BlendType.IsValid() && BlendType.Get() == EMovieSceneBlendType::Absolute);
	bOk &= TestTrue(TEXT("Completion mode"), Section->GetCompletionMode() == EMovieSceneCompletionMode::RestoreState);
	bOk &= TestFalse(TEXT("Completion mode locked"), Section->EvalOptions.bCanEditCompletionMode);
	bOk &= TestChannelDefaultNear(*this, TEXT("Damping"), Section->Damping, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("Stiffness"), Section->Stiffness, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("WorldDampingLocation"), Section->WorldDampingLocation, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("WorldDampingRotation"), Section->WorldDampingRotation, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("Radius"), Section->Radius, 1.0f);
	bOk &= TestChannelDefaultNear(*this, TEXT("LimitAngle"), Section->LimitAngle, 1.0f);
	bOk &= TestTrue(TEXT("Track supports absolute"),
	                Track->GetSupportedBlendTypes().Contains(EMovieSceneBlendType::Absolute));
	return bOk;
}

#endif
