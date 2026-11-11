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
// TimeScale は入口（UpdateInternal）で dt に一度だけ掛けている。ここでは横向きの重力で振らせた髪で、積分とサブステップ蓄積を確かめる。
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

// TimeScale は 0.01〜10 に丸め、非有限は 1 として扱う。0 以下でも物理は止まらず、0.01 と同じ速さで進む。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTimeScaleClampTest,
	"KawaiiPhysics.TimeScale.ClampsToRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTimeScaleClampTest::RunTest(const FString& Parameters)
{
	using namespace KawaiiPhysicsTimeScaleTestPrivate;

	bool bOk = true;
	{
		struct FExpect
		{
			float Value;
			float Expected;
			const TCHAR* What;
		};
		const FExpect Cases[] = {
			{-1.0f, 0.01f, TEXT("negative")},
			{0.0f, 0.01f, TEXT("zero")},
			{0.001f, 0.01f, TEXT("below the minimum")},
			{0.25f, 0.25f, TEXT("in range")},
			{2.0f, 2.0f, TEXT("above 1")},
			{100.0f, 10.0f, TEXT("above the maximum")},
			{std::numeric_limits<float>::infinity(), 1.0f, TEXT("infinite")},
			{std::numeric_limits<float>::quiet_NaN(), 1.0f, TEXT("NaN")},
		};
		FAnimNode_KawaiiPhysics Node;
		for (const FExpect& Case : Cases)
		{
			Node.TimeScale = Case.Value;
			bOk &= TestEqual(FString::Printf(TEXT("%s TimeScale becomes %.2f"), Case.What, Case.Expected),
				Node.GetEffectiveTimeScale(), Case.Expected);
		}
	}

	for (const bool bFixed : {true, false})
	{
		FScopedFixedSubstepping Scoped(bFixed);
		FMemMark MemMark(FMemStack::Get());
		const FString Label = bFixed ? TEXT("FixedSubstep") : TEXT("Legacy");

		// どちらも 10 フレーム通常速度で振らせてから、片方は 0、もう片方は 0.01 で進める
		FFixture Zero(FCase{TEXT("Zero"), 60, 0, EFilterLayout::None});
		Zero.Node.Gravity = GSideGravity;
		FFixture Minimum(FCase{TEXT("Minimum"), 60, 0, EFilterLayout::None});
		Minimum.Node.Gravity = GSideGravity;
		Run(Zero, 10);
		Run(Minimum, 10);
		const TArray<FVector> BeforeSlow = Locations(Zero);

		Zero.Node.TimeScale = 0.0f;
		Minimum.Node.TimeScale = 0.01f;
		Run(Zero, 200);
		Run(Minimum, 200);

		double MaxMoved = 0.0;
		const TArray<FVector> AfterSlow = Locations(Zero);
		for (int32 Index = 0; Index < AfterSlow.Num(); ++Index)
		{
			MaxMoved = FMath::Max(MaxMoved, FVector::Dist(AfterSlow[Index], BeforeSlow[Index]));
		}
		bOk &= TestTrue(FString::Printf(TEXT("%s: TimeScale 0 keeps simulating (moved %.4f cm)"), *Label, MaxMoved),
			MaxMoved > 0.01);
		bOk &= ExpectSameLocations(*this, Label + TEXT(".ZeroMatchesMinimum"), AfterSlow, Locations(Minimum), 0.0);
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

// テレポートの閾値は 1 フレームの Component の移動量と比べる。TimeScale が小さいとステップが数フレームに 1 回になり、
// 繰り越した移動が溜まるが、1 フレームずつは閾値未満のふつうの旋回をテレポート扱いにしない（慣性として揺れに残る）。
// 1 フレームで閾値を超える回転は、従来どおりテレポートとして破棄する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTimeScaleSlowTurnIsNotTeleportTest,
	"KawaiiPhysics.TimeScale.SlowTurnIsNotTeleport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTimeScaleSlowTurnIsNotTeleportTest::RunTest(const FString& Parameters)
{
	// 縦チェーン（約 140cm）を X 軸まわりに回す。WorldDamping=0・重力なしなので、反映された回転はそのまま揺れになり、
	// テレポートで破棄されればチェーンはポーズのまま動かない
	struct FTurnResult
	{
		int32 Teleports = 0;
		float MaxTipDeviation = 0.0f;
	};
	auto RunTurn = [](const float DegreesPerFrame, const int32 TurnFrames)
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildVerticalChain(6, 28.0f);

		FKawaiiPhysicsSettings S;
		S.Damping = 0.1f;
		S.Stiffness = 0.05f;
		S.WorldDampingLocation = 0.0f;
		S.WorldDampingRotation = 0.0f;
		S.LimitAngle = 0.0f;
		S.Radius = 0.0f;
		A.SetAllPhysicsSettings(S);

		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector::ZeroVector);
		A.SetFixedSubstepping(true, 60, 8);
		A.Node.TeleportDistanceThreshold = 0.0f;
		A.Node.TeleportRotationThreshold = 10.0f;
		A.SetPreSkelCompTransform(FTransform::Identity);

		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext Output(&Proxy);

		// 60fps で TimeScale 0.1 のときと同じ 1/600 秒。60Hz の固定ステップは 10 フレームに 1 回だけ走る
		const float FrameDt = 1.0f / 600.0f;
		FTurnResult Result;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			const float Degrees = DegreesPerFrame * FMath::Min(Frame + 1, TurnFrames);
			const FTransform Component(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Degrees)));
			Result.Teleports += A.StepFrameWithComponentTransform(Output, FrameDt, Component) ? 1 : 0;
			const FKawaiiPhysicsModifyBone& Tip = A.Bone(A.Num() - 1);
			Result.MaxTipDeviation = FMath::Max(Result.MaxTipDeviation,
				static_cast<float>((Tip.Location - Tip.PoseLocation).Size()));
		}
		return Result;
	};

	// 1 フレーム 5 度で 60 度まで回す。ステップ間（10 フレーム）では 50 度溜まるが、1 フレームずつは閾値 10 度未満
	const FTurnResult SlowTurn = RunTurn(5.0f, 12);
	bool bOk = TestEqual(TEXT("slow turn is never detected as a teleport"), SlowTurn.Teleports, 0);
	bOk &= TestTrue(FString::Printf(TEXT("slow turn is kept as inertia: tip deviation %.4f cm"), SlowTurn.MaxTipDeviation),
		SlowTurn.MaxTipDeviation > 5.0f);

	// 1 フレームで 25 度回すのはテレポートとして破棄される
	const FTurnResult Snap = RunTurn(25.0f, 1);
	bOk &= TestEqual(TEXT("a 25 degree snap in one frame is one teleport"), Snap.Teleports, 1);
	bOk &= TestTrue(FString::Printf(TEXT("a 25 degree snap is discarded: tip deviation %.4f cm"), Snap.MaxTipDeviation),
		Snap.MaxTipDeviation < 0.01f);
	return bOk;
}

#endif
