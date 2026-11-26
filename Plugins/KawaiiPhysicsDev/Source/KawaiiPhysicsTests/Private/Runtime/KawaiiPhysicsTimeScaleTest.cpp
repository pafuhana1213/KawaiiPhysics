// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsHairNodeFixture.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Misc/MemStack.h"

#include <limits>

namespace KawaiiPhysicsTimeScaleTestPrivate
{
// TimeScale が本番の評価経路（UpdateInternal → EvaluateSkeletalControl_AnyThread）で dt に掛かり、
// シミュレーションの時間だけを遅く・速くすることを確かめる。
	using KawaiiPhysicsHairNodeFixture::FFixture;
	using KawaiiPhysicsHairNodeFixture::FCase;
	using KawaiiPhysicsHairNodeFixture::EFilterLayout;

	// 髪は根元から -Z に垂れているので、横向きの重力で振らせて動きのある状態を作る
	const FVector GSideGravity(980.0, 0.0, 0.0);


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

// TimeScale=s・フレーム dt=d の結果が、TimeScale=1・フレーム dt=s*d と一致する。
// TimeScale は入口（UpdateInternal）で dt に一度だけ掛けている。ここでは横向きの重力で振らせた髪で、積分とサブステップ蓄積を確かめる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTimeScaleMatchesScaledDeltaTest,
	"KawaiiPhysics.TimeScale.MatchesScaledFrameDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTimeScaleMatchesScaledDeltaTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const bool bFixed : {true, false})
	{
		KawaiiPhysicsNodeFixture::FScopedDeveloperSettings Scoped([bFixed](UKawaiiPhysicsDeveloperSettings& Settings)
		{
			Settings.bUseFixedSubstepping = bFixed;
		});
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
	KawaiiPhysicsNodeFixture::FScopedDeveloperSettings Scoped([](UKawaiiPhysicsDeveloperSettings& Settings)
	{
		Settings.bUseFixedSubstepping = true;
	});
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
		KawaiiPhysicsNodeFixture::FScopedDeveloperSettings Scoped([bFixed](UKawaiiPhysicsDeveloperSettings& Settings)
		{
			Settings.bUseFixedSubstepping = bFixed;
		});
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

// TS-09・TS-10。mutant なし。テレポートの閾値は 1 フレームの Component の移動量と比べる。TimeScale が小さいとステップが数フレームに 1 回になり、
// 繰り越した移動が溜まるが、1 フレームずつは閾値未満のふつうの旋回をテレポート扱いにしない（慣性として揺れに残る）。
// 1 フレームで閾値を超える回転は、従来どおりテレポートとして破棄する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTimeScaleSlowTurnIsNotTeleportTest,
	"KawaiiPhysics.TimeScale.SlowTurnIsNotTeleport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTimeScaleSlowTurnIsNotTeleportTest::RunTest(const FString& Parameters)
{
	auto MakeTurnOptions = [](const float TimeScale, const float Threshold, const float DegreesPerFrame)
	{
		KawaiiPhysicsNodeFixture::FOptions TurnOptions;
		TurnOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(6, FVector(0.0, 0.0, -28.0));
		TurnOptions.FrameDt = 1.0f / 60.0f;
		TurnOptions.ConfigureDeveloperSettings = [](UKawaiiPhysicsDeveloperSettings& DeveloperSettings)
		{
			DeveloperSettings.bUseFixedSubstepping = true;
			DeveloperSettings.MaxSubsteps = 8;
		};
		TurnOptions.ConfigureNode = [TimeScale, Threshold](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.TimeScale = TimeScale;
			PhysicsNode.TargetFramerate = 60;
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.Gravity = FVector::ZeroVector;
			PhysicsNode.TeleportDistanceThreshold = 0.0f;
			PhysicsNode.TeleportRotationThreshold = Threshold;
			PhysicsNode.PhysicsSettings.Damping = 0.1f;
			PhysicsNode.PhysicsSettings.Stiffness = 0.05f;
			PhysicsNode.PhysicsSettings.WorldDampingLocation = 0.0f;
			PhysicsNode.PhysicsSettings.WorldDampingRotation = 0.0f;
			PhysicsNode.PhysicsSettings.LimitAngle = 0.0f;
		};
		TurnOptions.GetComponentTransform = [DegreesPerFrame](const int32 Frame)
		{
			return FTransform(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Frame * DegreesPerFrame)));
		};
		return TurnOptions;
	};

	KawaiiPhysicsNodeFixture::FFixture SlowTurn(MakeTurnOptions(0.1f, 10.0f, 1.0f));
	KawaiiPhysicsNodeFixture::FFixture NoThreshold(MakeTurnOptions(0.1f, 0.0f, 1.0f));
	bool bOk = true;
	double MaxInertia = 0.0;
	SlowTurn.EvaluateFrame();
	NoThreshold.EvaluateFrame();
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		SlowTurn.EvaluateFrame();
		NoThreshold.EvaluateFrame();
		bOk &= TestTrue(TEXT("両セルに ModifyBones がある"), SlowTurn.Node.ModifyBones.Num() > 0 && NoThreshold.Node.ModifyBones.Num() > 0);
		bOk &= TestEqual(TEXT("全ボーンの数が一致"), SlowTurn.Node.ModifyBones.Num(), NoThreshold.Node.ModifyBones.Num());
		for (int32 Index = 0; Index < FMath::Min(SlowTurn.Node.ModifyBones.Num(), NoThreshold.Node.ModifyBones.Num()); ++Index)
		{
			// 同じ入力を二経路で評価する差。float の 60 フレーム分の丸めとして 1e-3cm。
			bOk &= TestTrue(TEXT("閾値 10 は判定なしと同じ位置"),
				FVector::Dist(SlowTurn.Node.ModifyBones[Index].Location, NoThreshold.Node.ModifyBones[Index].Location) <= 0.001);
		}
		if (!SlowTurn.Node.ModifyBones.IsEmpty())
		{
			MaxInertia = FMath::Max(MaxInertia,
				FVector::Dist(SlowTurn.Node.ModifyBones.Last().Location, SlowTurn.Node.ModifyBones.Last().PoseLocation));
		}
	}
	bOk &= TestTrue(TEXT("旋回が実際に慣性を生む"), MaxInertia > 0.1);

	for (const float Scale : {1.0f, 2.0f})
	{
		KawaiiPhysicsNodeFixture::FFixture Snap(MakeTurnOptions(Scale, 10.0f, 25.0f));
		KawaiiPhysicsNodeFixture::FFixture Unsuppressed(MakeTurnOptions(Scale, 0.0f, 25.0f));
		Snap.EvaluateFrame();
		Unsuppressed.EvaluateFrame();
		Snap.EvaluateFrame();
		Unsuppressed.EvaluateFrame();
		bOk &= TestTrue(TEXT("Snap の ModifyBones がある"), Snap.Node.ModifyBones.Num() > 0);
		if (!Snap.Node.ModifyBones.IsEmpty() && !Unsuppressed.Node.ModifyBones.IsEmpty())
		{
			// 25° > 10° でテレポートしたセルは揺れを破棄する。float の変換丸めに 0.01cm。
			bOk &= TestTrue(TEXT("Snap はポーズへ戻る"),
				FVector::Dist(Snap.Node.ModifyBones.Last().Location, Snap.Node.ModifyBones.Last().PoseLocation) < 0.01);
			bOk &= TestTrue(TEXT("閾値なしの対照は揺れる"),
				FVector::Dist(Unsuppressed.Node.ModifyBones.Last().Location, Unsuppressed.Node.ModifyBones.Last().PoseLocation) > 0.1);
		}
	}
	return bOk;
}

} // namespace KawaiiPhysicsTimeScaleTestPrivate

#endif
