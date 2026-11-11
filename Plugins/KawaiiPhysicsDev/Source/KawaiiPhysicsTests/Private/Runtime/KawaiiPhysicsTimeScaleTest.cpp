// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsHairNodeFixture.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Misc/MemStack.h"

#include <limits>

// TimeScale が本番の評価経路（UpdateInternal → EvaluateSkeletalControl_AnyThread）で dt に掛かり、
// シミュレーションの時間だけを遅く・速くすることを確かめる。
namespace KawaiiPhysicsTimeScaleTestPrivate
{
	using namespace KawaiiPhysicsHairNodeFixture;

	// 髪は根元から -Z に垂れているので、横向きの重力で振らせて動きのある状態を作る
	const FVector GSideGravity(980.0, 0.0, 0.0);

	/** bUseFixedSubstepping をテスト中だけ切り替え、終了時に戻す */
	struct FScopedFixedSubstepping
	{
		UKawaiiPhysicsDeveloperSettings* Settings = GetMutableDefault<UKawaiiPhysicsDeveloperSettings>();
		bool bOld = Settings->bUseFixedSubstepping;

		explicit FScopedFixedSubstepping(const bool bEnable) { Settings->bUseFixedSubstepping = bEnable; }
		~FScopedFixedSubstepping() { Settings->bUseFixedSubstepping = bOld; }
	};

	void Run(FFixture& Fixture, const int32 NumFrames)
	{
		FComponentSpacePoseContext Context(Fixture.Proxy.Get());
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			Fixture.Evaluate(Context);
		}
	}

	TArray<FVector> Locations(const FFixture& Fixture)
	{
		TArray<FVector> Result;
		Result.Reserve(Fixture.Node.ModifyBones.Num());
		for (const FKawaiiPhysicsModifyBone& Bone : Fixture.Node.ModifyBones)
		{
			Result.Add(Bone.Location);
		}
		return Result;
	}

	/** 物理で動いた量（ポーズからの最大のずれ）。テストが静止状態の比較で素通りしていないかの確認に使う */
	double MaxDeviationFromPose(const FFixture& Fixture)
	{
		double Max = 0.0;
		for (const FKawaiiPhysicsModifyBone& Bone : Fixture.Node.ModifyBones)
		{
			Max = FMath::Max(Max, FVector::Dist(Bone.Location, Bone.PoseLocation));
		}
		return Max;
	}

	bool ExpectSameLocations(FAutomationTestBase& Test, const FString& What, const TArray<FVector>& Actual,
		const TArray<FVector>& Expected, const double Tolerance)
	{
		if (!Test.TestEqual(FString::Printf(TEXT("%s: bone count"), *What), Actual.Num(), Expected.Num()))
		{
			return false;
		}
		double MaxDiff = 0.0;
		int32 MaxIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Actual.Num(); ++Index)
		{
			const double Diff = FVector::Dist(Actual[Index], Expected[Index]);
			if (Diff > MaxDiff)
			{
				MaxDiff = Diff;
				MaxIndex = Index;
			}
		}
		return Test.TestTrue(FString::Printf(TEXT("%s: max bone difference %.6f cm (bone %d) <= %.6f"),
			*What, MaxDiff, MaxIndex, Tolerance), MaxDiff <= Tolerance);
	}
}

// TimeScale=s・フレーム dt=d の結果が、TimeScale=1・フレーム dt=s*d と一致する。
// 入口で一度だけ掛けていれば、外力や寿命・サブステップ蓄積も含めて同じ時間で進むはず。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTimeScaleMatchesScaledDeltaTest,
	"KawaiiPhysics.TimeScale.MatchesScaledFrameDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTimeScaleMatchesScaledDeltaTest::RunTest(const FString& Parameters)
{
	using namespace KawaiiPhysicsTimeScaleTestPrivate;

	bool bOk = true;
	for (const bool bFixed : {true, false})
	{
		FScopedFixedSubstepping Scoped(bFixed);
		FMemMark MemMark(FMemStack::Get());
		const FString Label = bFixed ? TEXT("FixedSubstep") : TEXT("Legacy");

		// 60fps × 0.25 = 1/240 秒。2 の冪の倍率なので 240fps の dt と float で一致する
		FFixture Scaled(FCase{TEXT("Scaled"), 60, 0, EFilterLayout::None});
		Scaled.Node.TimeScale = 0.25f;
		Scaled.Node.Gravity = GSideGravity;
		FFixture Reference(FCase{TEXT("Reference"), 240, 0, EFilterLayout::None});
		Reference.Node.Gravity = GSideGravity;

		Run(Scaled, 40);
		Run(Reference, 40);

		bOk &= TestTrue(FString::Printf(TEXT("%s: bones actually moved"), *Label),
			MaxDeviationFromPose(Reference) > 0.5);
		bOk &= ExpectSameLocations(*this, Label, Locations(Scaled), Locations(Reference), 1.0e-3);
	}
	return bOk;
}

// 固定サブステップでは、TimeScale=0.5 で 2N フレーム進めた結果が TimeScale=1 で N フレーム進めた結果と一致する
// （同じ動きが時間をかけて再生される）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTimeScaleSlowMotionTest,
	"KawaiiPhysics.TimeScale.SlowMotionReplaysSameMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTimeScaleSlowMotionTest::RunTest(const FString& Parameters)
{
	using namespace KawaiiPhysicsTimeScaleTestPrivate;

	FScopedFixedSubstepping Scoped(true);
	FMemMark MemMark(FMemStack::Get());

	// TargetFramerate=120 に対し、Slow は 1 フレーム 1 ステップ、Normal は 1 フレーム 2 ステップ（どちらも端数なし）
	FFixture Slow(FCase{TEXT("Slow"), 60, 0, EFilterLayout::None});
	Slow.Node.TimeScale = 0.5f;
	Slow.Node.Gravity = GSideGravity;
	FFixture Normal(FCase{TEXT("Normal"), 60, 0, EFilterLayout::None});
	Normal.Node.Gravity = GSideGravity;

	constexpr int32 NormalFrames = 20;
	Run(Slow, NormalFrames * 2);
	Run(Normal, NormalFrames);

	bool bOk = TestTrue(TEXT("bones actually moved"), MaxDeviationFromPose(Normal) > 0.5);
	bOk &= ExpectSameLocations(*this, TEXT("SlowMotion"), Locations(Slow), Locations(Normal), 1.0e-3);
	return bOk;
}

// TimeScale=0 で物理が止まり、1 に戻すと再開する。負の値は 0、非有限は 1 として扱う。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTimeScalePauseTest,
	"KawaiiPhysics.TimeScale.ZeroPausesAndResumes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTimeScalePauseTest::RunTest(const FString& Parameters)
{
	using namespace KawaiiPhysicsTimeScaleTestPrivate;

	bool bOk = true;
	{
		FAnimNode_KawaiiPhysics Node;
		Node.TimeScale = -1.0f;
		bOk &= TestEqual(TEXT("negative TimeScale is treated as 0"), Node.GetEffectiveTimeScale(), 0.0f);
		Node.TimeScale = std::numeric_limits<float>::infinity();
		bOk &= TestEqual(TEXT("infinite TimeScale is treated as 1"), Node.GetEffectiveTimeScale(), 1.0f);
		Node.TimeScale = std::numeric_limits<float>::quiet_NaN();
		bOk &= TestEqual(TEXT("NaN TimeScale is treated as 1"), Node.GetEffectiveTimeScale(), 1.0f);
		Node.TimeScale = 2.0f;
		bOk &= TestEqual(TEXT("TimeScale above 1 is kept"), Node.GetEffectiveTimeScale(), 2.0f);
	}

	for (const bool bFixed : {true, false})
	{
		FScopedFixedSubstepping Scoped(bFixed);
		FMemMark MemMark(FMemStack::Get());
		const FString Label = bFixed ? TEXT("FixedSubstep") : TEXT("Legacy");

		FFixture Fixture(FCase{TEXT("Pause"), 60, 0, EFilterLayout::None});
		Fixture.Node.Gravity = GSideGravity;
		Run(Fixture, 10);
		const TArray<FVector> BeforePause = Locations(Fixture);

		Fixture.Node.TimeScale = 0.0f;
		Run(Fixture, 10);
		bOk &= ExpectSameLocations(*this, Label + TEXT(".Paused"), Locations(Fixture), BeforePause, 0.0);

		Fixture.Node.TimeScale = -0.5f;
		Run(Fixture, 5);
		bOk &= ExpectSameLocations(*this, Label + TEXT(".NegativePaused"), Locations(Fixture), BeforePause, 0.0);

		Fixture.Node.TimeScale = 1.0f;
		Run(Fixture, 5);
		double MaxMoved = 0.0;
		const TArray<FVector> AfterResume = Locations(Fixture);
		for (int32 Index = 0; Index < AfterResume.Num(); ++Index)
		{
			MaxMoved = FMath::Max(MaxMoved, FVector::Dist(AfterResume[Index], BeforePause[Index]));
		}
		bOk &= TestTrue(FString::Printf(TEXT("%s: simulation resumes after the pause (moved %.4f cm)"), *Label, MaxMoved),
			MaxMoved > 0.01);
	}
	return bOk;
}

// TimeScale はプリセットには含めないが、プロジェクト側から制御できるようにノードプロパティ API からは読み書きできる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTimeScalePropertyAccessTest,
	"KawaiiPhysics.TimeScale.PropertyAccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTimeScalePropertyAccessTest::RunTest(const FString& Parameters)
{
	const FName TimeScaleName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TimeScale);
	const FProperty* Property = FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), TimeScaleName);
	bool bOk = TestNotNull(TEXT("TimeScale property exists"), Property);
	if (!Property)
	{
		return false;
	}
	bOk &= TestTrue(TEXT("TimeScale is classified as runtime only"),
		UKawaiiPhysicsPresetDataAsset::ClassifyNodeProperty(*Property) == EKawaiiPhysicsPresetPropertyClass::RuntimeOnly);
	bOk &= TestTrue(TEXT("TimeScale is accessible through the node property API"),
		UKawaiiPhysicsLibrary::IsNodePropertyAccessible(TimeScaleName));

	FAnimNode_KawaiiPhysics Node;
	bOk &= TestTrue(TEXT("TimeScale can be set from a string"),
		UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(Node, TimeScaleName, TEXT("0.25")));
	bOk &= TestEqual(TEXT("TimeScale holds the set value"), Node.TimeScale, 0.25f);
	FString Text;
	bOk &= TestTrue(TEXT("TimeScale can be read as a string"),
		UKawaiiPhysicsLibrary::GetNodePropertyValueAsString(Node, TimeScaleName, Text));
	bOk &= TestEqual(TEXT("TimeScale string value"), FCString::Atof(*Text), 0.25f);
	return bOk;
}

// 停止中（dt=0）の Component 移動は全量破棄され、再開フレームに慣性として持ち越されない。
// 直前のフレームでサブステップが走らず消費割合が 0 のままでも、停止フレームで PreSkelCompTransform が追いつく。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTimeScalePauseDiscardsMoveTest,
	"KawaiiPhysics.TimeScale.PauseDiscardsComponentMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTimeScalePauseDiscardsMoveTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor A;
	A.BuildVerticalChain(4, 10.0f);
	A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	A.SetGravityInSimSpace(FVector::ZeroVector);
	A.SetFixedSubstepping(true, 60, 8);
	A.SetPreSkelCompTransform(FTransform::Identity);

	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);

	// 60Hz 固定ステップに対し半ステップの dt。ステップが走らず、消費割合 0 で移動を繰り越す状態にする
	A.StepFrameWithComponentTransform(Output, 1.0f / 120.0f, FTransform::Identity);

	const FTransform Moved(FVector(50.0, 0.0, 0.0));
	A.StepFrameWithComponentTransform(Output, 0.0f, Moved);

	return TestTrue(TEXT("PreSkelCompTransform catches up to the component during the pause"),
		A.GetPreSkelCompTransform().GetLocation().Equals(Moved.GetLocation(), KINDA_SMALL_NUMBER));
}

#endif
