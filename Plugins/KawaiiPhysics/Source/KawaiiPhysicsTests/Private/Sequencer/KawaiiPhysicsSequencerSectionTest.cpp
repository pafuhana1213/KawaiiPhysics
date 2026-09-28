// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Generators/MovieSceneEasingCurves.h"
#include "KawaiiPhysicsSequencerMultiplierRegistry.h"
#include "KawaiiPhysicsWindPresetTags.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierTemplate.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierTrack.h"
#include "UObject/Package.h"

namespace
{
constexpr float GSequencerSectionTol = 0.000001f;

bool TestFloatNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected)
{
	return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	                     FMath::IsNearlyEqual(Actual, Expected, GSequencerSectionTol));
}

UMovieSceneKawaiiPhysicsSettingsMultiplierSection* NewSection()
{
	return NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(GetTransientPackage());
}

void SetupLinearEaseIn(UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section)
{
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(1000)));
	Section->Easing.bManualEaseIn = true;
	Section->Easing.ManualEaseInDuration = 200;

	UMovieSceneBuiltInEasingFunction* LinearEase = NewObject<UMovieSceneBuiltInEasingFunction>(Section);
	LinearEase->Type = EMovieSceneBuiltInEasing::Linear;
	Section->Easing.EaseIn.SetObject(LinearEase);
	Section->Easing.EaseIn.SetInterface(static_cast<IMovieSceneEasingFunction*>(LinearEase));
}

FKawaiiPhysicsSettingsMultiplier MakeScale()
{
	FKawaiiPhysicsSettingsMultiplier Scale;
	Scale.Damping = 0.5f;
	Scale.Stiffness = 0.25f;
	Scale.WorldDampingLocation = 0.75f;
	Scale.WorldDampingRotation = 0.8f;
	Scale.Radius = 1.5f;
	Scale.LimitAngle = 0.6f;
	return Scale;
}

void ApplyScaleToSection(UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section,
                         const FKawaiiPhysicsSettingsMultiplier& Scale)
{
	Section->Damping.SetDefault(Scale.Damping);
	Section->Stiffness.SetDefault(Scale.Stiffness);
	Section->WorldDampingLocation.SetDefault(Scale.WorldDampingLocation);
	Section->WorldDampingRotation.SetDefault(Scale.WorldDampingRotation);
	Section->Radius.SetDefault(Scale.Radius);
	Section->LimitAngle.SetDefault(Scale.LimitAngle);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateWeightChannelDefaultTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateWeight_ChannelDefault",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateWeightChannelDefaultTest::RunTest(const FString& Parameters)
{
	// Weight の既定値・イーズとの積・上下限クランプを確認する
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	SetupLinearEaseIn(Section);
	Section->Weight.SetDefault(0.5f);

	bool bOk = TestFloatNear(*this, TEXT("Default outside ease"), Section->EvaluateWeightAtTime(FFrameTime(500)),
	                         0.5f);
	bOk &= TestFloatNear(*this, TEXT("Default ease mid"), Section->EvaluateWeightAtTime(FFrameTime(100)), 0.25f);
	// チャンネル既定値と上下限へのクランプを確認する
	Section->Weight.SetDefault(2.0f);
	bOk &= TestFloatNear(*this, TEXT("Clamp high"), Section->EvaluateWeightAtTime(FFrameTime(500)), 1.0f);
	Section->Weight.SetDefault(-1.0f);
	bOk &= TestFloatNear(*this, TEXT("Clamp low"), Section->EvaluateWeightAtTime(FFrameTime(500)), 0.0f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTemplateFromTrackTest,
                                 "KawaiiPhysics.Sequencer.Section.TemplateFromTrack",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTemplateFromTrackTest::RunTest(const FString& Parameters)
{
	// セクション設定と RootTrack フラグが評価テンプレートへ渡ることを確認する
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(Track->CreateNewSection());

	const FKawaiiPhysicsSettingsMultiplier ExpectedScale = MakeScale();
	ApplyScaleToSection(Section, ExpectedScale);
	Section->bFilterExactMatch = true;
	Section->BlendOutTimeOnEnd = 0.75f;

	const FGameplayTag TestTag = TAG_KawaiiPhysics_WindPreset_Breeze;
	Section->FilterTags.AddTag(TestTag);

	Track->bIsRootTrack = true;
	Track->AddSection(*Section);

	FMovieSceneEvalTemplatePtr TemplatePtr = Track->CreateTemplateForSection(*Section);
	bool bOk = TestTrue(TEXT("Template valid"), TemplatePtr.IsValid());
	if (!TemplatePtr.IsValid())
	{
		return false;
	}

	bOk &= TestTrue(TEXT("Template script struct"),
	                &TemplatePtr->GetScriptStruct() ==
	                FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate::StaticStruct());

	const FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate* Template =
		static_cast<const FMovieSceneKawaiiPhysicsSettingsMultiplierSectionTemplate*>(TemplatePtr.GetPtr());
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.Damping"), Template->Damping, ExpectedScale.Damping);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.Stiffness"), Template->Stiffness, ExpectedScale.Stiffness);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.WorldDampingLocation"), Template->WorldDampingLocation,
	                              ExpectedScale.WorldDampingLocation);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.WorldDampingRotation"), Template->WorldDampingRotation,
	                              ExpectedScale.WorldDampingRotation);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.Radius"), Template->Radius, ExpectedScale.Radius);
	bOk &= TestChannelDefaultNear(*this, TEXT("Template.LimitAngle"), Template->LimitAngle, ExpectedScale.LimitAngle);
	bOk &= TestTrue(TEXT("FilterTags copied"), Template->FilterTags == Section->FilterTags);
	bOk &= TestTrue(TEXT("Root track flag copied"), Template->bIsRootTrack);
	bOk &= TestTrue(TEXT("Exact copied"), Template->bFilterExactMatch);
	bOk &= TestFloatNear(*this, TEXT("BlendOut copied"), Template->BlendOutTimeOnEnd, 0.75f);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerEvaluateScaleChannelKeysTest,
                                 "KawaiiPhysics.Sequencer.Section.EvaluateScale_ChannelKeys",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerEvaluateScaleChannelKeysTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();

	Section->Damping.AddLinearKey(FFrameNumber(2000), -1.0f);
	FKawaiiPhysicsSettingsMultiplier Actual = Section->EvaluateScaleAtTime(FFrameTime(2000));
	bool bOk = TestFloatNear(*this, TEXT("Scale.Damping clamp low"), Actual.Damping, 0.0f);
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
	// セクション単位の停止と、同じ Entry の二重登録抑止を確認する
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionA = NewSection();
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionB = NewSection();

	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryOther = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryB);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionB, EntryOther);

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionA);

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	bOk &= TestFalse(TEXT("Other section untouched"), EntryOther->bStopped);

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionA);
	bOk &= TestTrue(TEXT("EntryA still stopped"), EntryA->bStopped);
	bOk &= TestFalse(TEXT("Other section still untouched"), EntryOther->bStopped);

	// 同じ Entry の二重登録は件数を増やさない
	{
		UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry);

		bOk &= TestEqual(TEXT("Single registered entry"),
		                     FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
		bOk &= TestTrue(TEXT("Entry stopped"), Entry->bStopped);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
		bOk &= TestTrue(TEXT("Entry still stopped"), Entry->bStopped);
	}

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionB);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryGetQueuedNodeCountTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_GetQueuedNodeCount",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryGetQueuedNodeCountTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	USkeletalMeshComponent* ComponentA = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	USkeletalMeshComponent* ComponentB = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	USkeletalMeshComponent* ComponentC = NewObject<USkeletalMeshComponent>(GetTransientPackage());

	// (a) Entry が一つも登録されていないセクションは bOutHasLiveEntry=false（未評価と 0 件を区別）
	bool bHasLiveEntry = true;
	int32 Count = FKawaiiPhysicsSequencerMultiplierRegistry::Get().GetQueuedNodeCount(
		Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
	bool bOk = TestFalse(TEXT("No entries: bOutHasLiveEntry"), bHasLiveEntry);
	bOk &= TestEqual(TEXT("No entries: count"), Count, 0);

	// FilterTags がセクションと異なる Entry（フィルタ変更前の残留想定）
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> MismatchedFilterEntry =
		MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	MismatchedFilterEntry->Component = ComponentC;
	MismatchedFilterEntry->LastQueuedNodeCount = 11;
	MismatchedFilterEntry->FilterTags.AddTag(TAG_KawaiiPhysics_WindPreset_Breeze);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, MismatchedFilterEntry);

	// (b) フィルタが一致しない Entry しか無い場合は合算対象外→ bOutHasLiveEntry=false
	bHasLiveEntry = true;
	Count = FKawaiiPhysicsSequencerMultiplierRegistry::Get().GetQueuedNodeCount(
		Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
	bOk &= TestFalse(TEXT("Filter mismatch only: bOutHasLiveEntry"), bHasLiveEntry);
	bOk &= TestEqual(TEXT("Filter mismatch only: count"), Count, 0);

	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> InvalidComponentEntry =
		MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	EntryA->Component = ComponentA;
	EntryA->LastQueuedNodeCount = 3;
	EntryB->Component = ComponentB;
	EntryB->LastQueuedNodeCount = 2;
	InvalidComponentEntry->LastQueuedNodeCount = 7;

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryB);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, InvalidComponentEntry);

	// (c) フィルタが一致し生存している Entry があれば合算されて bOutHasLiveEntry=true
	//     （FilterTags 不一致の MismatchedFilterEntry と Component 無効な InvalidComponentEntry は除外される）
	bHasLiveEntry = false;
	Count = FKawaiiPhysicsSequencerMultiplierRegistry::Get().GetQueuedNodeCount(
		Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
	bOk &= TestTrue(TEXT("Matching entries: bOutHasLiveEntry"), bHasLiveEntry);
	bOk &= TestEqual(TEXT("Matching entries: count"), Count, 5);

	EntryA->Component = nullptr;
	EntryB->Component = nullptr;
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryRemoveSectionAtStopsOnlyRemovedTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_RemoveSectionAtStopsOnlyRemoved",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryRemoveSectionAtStopsOnlyRemovedTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionA =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(Track->CreateNewSection());
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionB =
		CastChecked<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(Track->CreateNewSection());
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

	Track->AddSection(*SectionA);
	Track->AddSection(*SectionB);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionB, EntryB);

	Track->RemoveSectionAt(1);

	bool bOk = TestFalse(TEXT("EntryA not stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionA);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryStopForSectionsNotInTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_StopForSectionsNotIn",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryStopForSectionsNotInTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	UMovieSceneSection* SectionA = Track->CreateNewSection();
	UMovieSceneSection* SectionB = Track->CreateNewSection();
	Track->AddSection(*SectionA);
	Track->AddSection(*SectionB);

	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionB, EntryB);

	TArray<UMovieSceneSection*> LiveSections;
	LiveSections.Add(SectionA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSectionsNotIn(Track, LiveSections);

	bool bOk = TestFalse(TEXT("EntryA still active"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(SectionA);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryRemoveInvalidEntriesTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_RemoveInvalidEntries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryRemoveInvalidEntriesTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	{
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry);
		TestEqual(TEXT("Entry registered"),
		          FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
	}

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().RemoveInvalidEntries();
	return TestEqual(TEXT("Invalid entry removed"),
	                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerRegistryStopAllTest,
                                 "KawaiiPhysics.Sequencer.Section.Registry_StopAll",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerRegistryStopAllTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionA = NewSection();
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* SectionB = NewSection();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
	TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionA, EntryA);
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(SectionB, EntryB);

	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopAll();

	bool bOk = TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
	bOk &= TestTrue(TEXT("EntryB stopped"), EntryB->bStopped);
	bOk &= TestEqual(TEXT("SectionA entries removed"),
	                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(SectionA), 0);
	bOk &= TestEqual(TEXT("SectionB entries removed"),
	                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(SectionB), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerPreAnimatedRestoreFiltersTest,
                                 "KawaiiPhysics.Sequencer.Section.PreAnimated_RestoreFilters",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerPreAnimatedRestoreFiltersTest::RunTest(const FString& Parameters)
{
	// オーナー・コンポーネント・期限切れ参照ごとに復元対象を絞る
	bool bOk = true;
	// オーナーだけを復元し、他のオーナーを残す
	{
		UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
		TSharedRef<uint8> OwnerA = MakeShared<uint8>(0);
		TSharedRef<uint8> OwnerB = MakeShared<uint8>(0);
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		EntryA->Owner = OwnerA;
		EntryB->Owner = OwnerB;

		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryA);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryB);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section, OwnerA);

		bOk &= TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
		bOk &= TestFalse(TEXT("EntryB still active"), EntryB->bStopped);
		bOk &= TestEqual(TEXT("Other owner remains"),
		                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	}

	// 復元したコンポーネントの Entry だけを停止する
	{
		UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
		TSharedRef<uint8> Owner = MakeShared<uint8>(0);
		USkeletalMeshComponent* ComponentA = NewObject<USkeletalMeshComponent>(GetTransientPackage());
		USkeletalMeshComponent* ComponentB = NewObject<USkeletalMeshComponent>(GetTransientPackage());
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryA = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> EntryB = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		EntryA->Owner = Owner;
		EntryA->Component = ComponentA;
		EntryB->Owner = Owner;
		EntryB->Component = ComponentB;

		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryA);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, EntryB);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section, Owner, ComponentA);

		bOk &= TestTrue(TEXT("EntryA stopped"), EntryA->bStopped);
		bOk &= TestFalse(TEXT("EntryB still active"), EntryB->bStopped);
		bOk &= TestEqual(TEXT("Other component remains"),
		                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	}

	// 期限切れのオーナーは Entry を停止しない
	{
		UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
		TWeakPtr<uint8> ExpiredOwner;
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> Entry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		{
			TSharedRef<uint8> Owner = MakeShared<uint8>(0);
			ExpiredOwner = Owner;
			Entry->Owner = Owner;
		}

		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, Entry);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section, ExpiredOwner);

		bOk &= TestFalse(TEXT("Entry still active"), Entry->bStopped);
		bOk &= TestEqual(TEXT("Entry remains"),
		                 FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerSectionDefaultsTest,
                                 "KawaiiPhysics.Sequencer.Section.Section_Defaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerSectionDefaultsTest::RunTest(const FString& Parameters)
{
	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSection();
	const FOptionalMovieSceneBlendType BlendType = Section->GetBlendType();
	bool bOk = TestTrue(TEXT("Section blend valid"), BlendType.IsValid());
	bOk &= TestTrue(TEXT("Section blend absolute"), BlendType.IsValid() && BlendType.Get() == EMovieSceneBlendType::Absolute);
	bOk &= TestTrue(TEXT("Completion mode"), Section->GetCompletionMode() == EMovieSceneCompletionMode::RestoreState);
	bOk &= TestFalse(TEXT("Completion mode locked"), Section->EvalOptions.bCanEditCompletionMode);
	return bOk;
}

#endif
