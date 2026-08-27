// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierTrack.h"
#include "Sequencer/KawaiiPhysicsSettingsMultiplierSectionPresets.h"
#include "Sequencer/KawaiiPhysicsSettingsMultiplierSectionSummary.h"

#include "Misc/AutomationTest.h"
#include "MovieSceneTrack.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr float GSequencerTrackEditorTol = 0.000001f;

UMovieSceneKawaiiPhysicsSettingsMultiplierSection* NewSettingsMultiplierSection()
{
	return NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(GetTransientPackage());
}

bool TestFloatNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected)
{
	return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	                     FMath::IsNearlyEqual(Actual, Expected, GSequencerTrackEditorTol));
}

bool TestScaleEqual(FAutomationTestBase& Test, const FKawaiiPhysicsSettingsMultiplier& Actual,
                    const FKawaiiPhysicsSettingsMultiplier& Expected)
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

void AddScaleKeys(UMovieSceneKawaiiPhysicsSettingsMultiplierSection& Section)
{
	Section.Damping.AddLinearKey(FFrameNumber(0), 0.2f);
	Section.Stiffness.AddLinearKey(FFrameNumber(0), 0.3f);
	Section.WorldDampingLocation.AddLinearKey(FFrameNumber(0), 0.4f);
	Section.WorldDampingRotation.AddLinearKey(FFrameNumber(0), 0.5f);
	Section.Radius.AddLinearKey(FFrameNumber(0), 0.6f);
	Section.LimitAngle.AddLinearKey(FFrameNumber(0), 0.7f);
}

bool TestScaleChannelsHaveNoKeys(FAutomationTestBase& Test,
                                 const UMovieSceneKawaiiPhysicsSettingsMultiplierSection& Section)
{
	bool bOk = true;
	bOk &= Test.TestEqual(TEXT("Damping keys removed"), Section.Damping.GetNumKeys(), 0);
	bOk &= Test.TestEqual(TEXT("Stiffness keys removed"), Section.Stiffness.GetNumKeys(), 0);
	bOk &= Test.TestEqual(
		TEXT("WorldDampingLocation keys removed"),
		Section.WorldDampingLocation.GetNumKeys(),
		0);
	bOk &= Test.TestEqual(
		TEXT("WorldDampingRotation keys removed"),
		Section.WorldDampingRotation.GetNumKeys(),
		0);
	bOk &= Test.TestEqual(TEXT("Radius keys removed"), Section.Radius.GetNumKeys(), 0);
	bOk &= Test.TestEqual(TEXT("LimitAngle keys removed"), Section.LimitAngle.GetNumKeys(), 0);
	return bOk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorScaleSummaryNoChangeTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.ScaleSummary_NoChange",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorScaleSummaryNoChangeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FKawaiiPhysicsSettingsMultiplier Scale;

	bool bOk = true;
	bOk &= TestTrue(
		TEXT("全倍率が 1.0 の場合はロケール非依存サマリが空文字列になること"),
		MakeKawaiiPhysicsScaleSummaryString(Scale).IsEmpty());
	bOk &= TestFalse(
		TEXT("全倍率が 1.0 の場合でも表示用テキストは空にならないこと"),
		MakeKawaiiPhysicsScaleSummaryText(Scale).ToString().IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorScaleSummaryPartialTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.ScaleSummary_Partial",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorScaleSummaryPartialTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FKawaiiPhysicsSettingsMultiplier Scale;
	Scale.Damping = 0.5f;
	Scale.Stiffness = 1.2f;

	return TestEqual(
		TEXT("1.0 以外の倍率だけが順番通りに表示されること"),
		MakeKawaiiPhysicsScaleSummaryString(Scale),
		FString(TEXT("D×0.50  S×1.20")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorScaleSummaryAllTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.ScaleSummary_All",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorScaleSummaryAllTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FKawaiiPhysicsSettingsMultiplier Scale;
	Scale.Damping = 0.5f;
	Scale.Stiffness = 1.2f;
	Scale.WorldDampingLocation = 0.8f;
	Scale.WorldDampingRotation = 0.7f;
	Scale.Radius = 1.5f;
	Scale.LimitAngle = 0.25f;

	return TestEqual(
		TEXT("6 成分が D, S, WL, WR, R, LA の順で表示されること"),
		MakeKawaiiPhysicsScaleSummaryString(Scale),
		FString(TEXT("D×0.50  S×1.20  WL×0.80  WR×0.70  R×1.50  LA×0.25")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorScalePresetApplyTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.ScalePreset_Apply",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorScalePresetApplyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSettingsMultiplierSection();
	AddScaleKeys(*Section);

	FKawaiiPhysicsSettingsMultiplier StiffScale;
	StiffScale.Damping = 1.5f;
	StiffScale.Stiffness = 2.0f;
	ApplyKawaiiPhysicsScalePresetToSection(*Section, StiffScale);

	bool bOk = TestScaleEqual(*this, Section->EvaluateScaleAtTime(FFrameTime(0)), StiffScale);
	bOk &= TestScaleChannelsHaveNoKeys(*this, *Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorScalePresetResetTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.ScalePreset_Reset",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorScalePresetResetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section = NewSettingsMultiplierSection();
	AddScaleKeys(*Section);

	ApplyKawaiiPhysicsScalePresetToSection(*Section, FKawaiiPhysicsSettingsMultiplier());

	bool bOk = TestScaleEqual(
		*this,
		Section->EvaluateScaleAtTime(FFrameTime(0)),
		FKawaiiPhysicsSettingsMultiplier());
	bOk &= TestScaleChannelsHaveNoKeys(*this, *Section);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorSupportsTypeTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.SupportsType",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorSupportsTypeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* Track =
		GetDefault<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>();

	bool bOk = true;
	bOk &= TestTrue(
		TEXT("Kawaii Physics Settings Multiplier Track が UMovieSceneTrack 派生であること"),
		UMovieSceneKawaiiPhysicsSettingsMultiplierTrack::StaticClass()->IsChildOf(UMovieSceneTrack::StaticClass()));
	bOk &= TestTrue(
		TEXT("Kawaii Physics Settings Multiplier Track が複数行をサポートすること"),
		Track && Track->SupportsMultipleRows());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSequencerTrackEditorRootTrackDefaultDisplayNameTest,
                                 "KawaiiPhysics.Sequencer.TrackEditor.RootTrack_DefaultDisplayName",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSequencerTrackEditorRootTrackDefaultDisplayNameTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* BoundTrack =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	UMovieSceneKawaiiPhysicsSettingsMultiplierTrack* RootTrack =
		NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage());
	RootTrack->bIsRootTrack = true;

	const FString BoundDisplayName = BoundTrack->GetDefaultDisplayName().ToString();
	const FString RootDisplayName = RootTrack->GetDefaultDisplayName().ToString();

	bool bOk = TestFalse(TEXT("Binding display name empty"), BoundDisplayName.IsEmpty());
	bOk &= TestFalse(TEXT("Root display name empty"), RootDisplayName.IsEmpty());
	bOk &= TestTrue(TEXT("Root display name differs"), BoundDisplayName != RootDisplayName);
	return bOk;
}

#endif
