// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsNodeFixture.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"

namespace KawaiiPhysicsSolverSpecMotionTestPrivate
{
	KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsMotionHandoff, "KawaiiPhysics.Test.Motion.Handoff");

	struct FWarningCapture : FOutputDevice
	{
		FCriticalSection Lock;
		TArray<FString> Lines;
		// 対象カテゴリの全行（Verbosity を問わない）。AddExpectedMessage に一致した警告は、Automation が
		// GLog へ流す前に Verbose へ下げる（AutomationTest.cpp の FAutomationTestMessageFilter）ので、Lines には入らない。
		TArray<FString> AllLines;

		FWarningCapture()
		{
			GLog->AddOutputDevice(this);
		}

		~FWarningCapture() override
		{
			GLog->RemoveOutputDevice(this);
		}

		void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			if (Category == FName(TEXT("LogKawaiiPhysics")) || Category == FName(TEXT("LogAnimation")))
			{
				FScopeLock CaptureLock(&Lock);
				AllLines.Add(Message);
				if (Level <= ELogVerbosity::Warning)
				{
					Lines.Add(Message);
				}
			}
		}

		bool CanBeUsedOnAnyThread() const override { return true; }
		bool CanBeUsedOnMultipleThreads() const override { return true; }

		int32 Count()
		{
			GLog->Flush();
			FScopeLock CaptureLock(&Lock);
			return Lines.Num();
		}

		// Serialize は別スレッドから呼ばれうるので、走査はロックを取って写した行に対して行う。
		TArray<FString> CopyAllLines()
		{
			GLog->Flush();
			FScopeLock CaptureLock(&Lock);
			return AllLines;
		}
	};

	KawaiiPhysicsNodeFixture::FOptions MakeChainOptions(const float FrameSeconds, const int32 TargetRate = 60)
	{
		KawaiiPhysicsNodeFixture::FOptions ChainOptions;
		ChainOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(5);
		ChainOptions.FrameDt = FrameSeconds;
		ChainOptions.ConfigureDeveloperSettings = [](UKawaiiPhysicsDeveloperSettings& DeveloperSettings)
		{
			DeveloperSettings.bUseFixedSubstepping = true;
			DeveloperSettings.MaxSubsteps = 4;
		};
		ChainOptions.ConfigureNode = [TargetRate](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.WarmUpFrames = 0;
			PhysicsNode.TargetFramerate = TargetRate;
			PhysicsNode.Gravity = FVector(980.0, 0.0, 0.0);
			PhysicsNode.bAllowWorldCollision = false;
			PhysicsNode.bUseSimpleWorldCollision = false;
			PhysicsNode.TeleportDistanceThreshold = 0.0f;
			PhysicsNode.TeleportRotationThreshold = 0.0f;
		};
		return ChainOptions;
	}

	FVector GetOutputLocation(const TArray<FBoneTransform>& BoneTransforms, const int32 BoneIndex)
	{
		for (const FBoneTransform& BoneTransform : BoneTransforms)
		{
			if (BoneTransform.BoneIndex.GetInt() == BoneIndex)
			{
				return BoneTransform.Transform.GetLocation();
			}
		}
		return FVector::ZeroVector;
	}

	double ComputeLargestOutputDifference(const TArray<FBoneTransform>& FirstTransforms,
		const TArray<FBoneTransform>& SecondTransforms)
	{
		double LargestDifference = 0.0;
		for (const FBoneTransform& BoneTransform : FirstTransforms)
		{
			LargestDifference = FMath::Max(LargestDifference,
				FVector::Dist(BoneTransform.Transform.GetLocation(),
					GetOutputLocation(SecondTransforms, BoneTransform.BoneIndex.GetInt())));
		}
		return LargestDifference;
	}

	bool IsRotationTeleport(const float ThresholdDegrees, const FQuat& Turn)
	{
		FKawaiiPhysicsTestAccessor RotationAccessor;
		RotationAccessor.BuildVerticalChain(4, 10.0f);
		RotationAccessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		RotationAccessor.SetGravityInSimSpace(FVector::ZeroVector);
		RotationAccessor.Node.TeleportDistanceThreshold = 0.0f;
		RotationAccessor.Node.TeleportRotationThreshold = ThresholdDegrees;
		RotationAccessor.SetPreSkelCompTransform(FTransform::Identity);
		FAnimInstanceProxy RotationProxy;
		FComponentSpacePoseContext RotationOutput(&RotationProxy);
		return RotationAccessor.StepFrameWithComponentTransform(RotationOutput, 1.0f / 60.0f, FTransform(Turn));
	}

	// FX-04。mutant なし。180° 以上の閾値で回転テレポートを起こす変更は、回数と無効対照との差で落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMotionRotationDisabledTest,
		"KawaiiPhysics.SolverSpec.Motion.RotationThresholdDisabledAt180",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMotionRotationDisabledTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const float ThresholdDegrees : {180.0f, 200.0f})
		{
			for (const float TurnDegrees : {179.0f, 180.0f})
			{
				const FQuat Turn(FVector::ZAxisVector, FMath::DegreesToRadians(TurnDegrees));
				bOk &= TestFalse(FString::Printf(TEXT("閾値 %.0f°、回転 %.0f° はテレポート 0 回"),
					ThresholdDegrees, TurnDegrees), IsRotationTeleport(ThresholdDegrees, Turn));
				KawaiiPhysicsNodeFixture::FOptions RotationOptions = MakeChainOptions(1.0f / 60.0f);
				RotationOptions.GetComponentTransform = [Turn](const int32 Frame)
				{
					return Frame < 10 ? FTransform::Identity : FTransform(Turn);
				};
				RotationOptions.ConfigureNode = [ThresholdDegrees](FAnimNode_KawaiiPhysics& PhysicsNode)
				{
					PhysicsNode.RootBone.BoneName = TEXT("root");
					PhysicsNode.DummyBoneLength = 0.0f;
					PhysicsNode.Gravity = FVector(980.0, 0.0, 0.0);
					PhysicsNode.TeleportDistanceThreshold = 0.0f;
					PhysicsNode.TeleportRotationThreshold = ThresholdDegrees;
				};
				KawaiiPhysicsNodeFixture::FOptions DisabledOptions = RotationOptions;
				DisabledOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
				{
					PhysicsNode.RootBone.BoneName = TEXT("root");
					PhysicsNode.DummyBoneLength = 0.0f;
					PhysicsNode.Gravity = FVector(980.0, 0.0, 0.0);
					PhysicsNode.TeleportDistanceThreshold = 0.0f;
					PhysicsNode.TeleportRotationThreshold = 0.0f;
				};
				KawaiiPhysicsNodeFixture::FFixture ThresholdFixture(RotationOptions);
				KawaiiPhysicsNodeFixture::FFixture DisabledFixture(DisabledOptions);
				for (int32 Frame = 0; Frame <= 10; ++Frame)
				{
				ThresholdFixture.EvaluateFrame();
				DisabledFixture.EvaluateFrame();
				}
				bOk &= TestTrue(TEXT("両対照の ModifyBones がある"),
					ThresholdFixture.Node.ModifyBones.Num() > 1 && DisabledFixture.Node.ModifyBones.Num() > 1);
				if (ThresholdFixture.Node.ModifyBones.Num() > 1)
				{
					bOk &= TestTrue(TEXT("回転前に重力で状態が動いた"),
						FVector::Dist(ThresholdFixture.Node.ModifyBones.Last().Location,
							ThresholdFixture.Node.ModifyBones.Last().PoseLocation) > 0.001);
				}
				bOk &= TestEqual(TEXT("出力本数が一致"), ThresholdFixture.OutTransforms.Num(), DisabledFixture.OutTransforms.Num());
				// 同じ回転入力を二つの閾値経路で評価するため、float の変換丸めに 1e-3cm を許す。
				bOk &= TestTrue(TEXT("無効対照と全位置が 1e-3cm 以内"),
					ComputeLargestOutputDifference(ThresholdFixture.OutTransforms, DisabledFixture.OutTransforms) <= 0.001);
			}
		}
		return bOk;
	}

	// FX-01。mutant なし。短い側でない角度や q の符号依存は、閾値を挟む真偽または符号対照で落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMotionShortestRotationTest,
		"KawaiiPhysics.SolverSpec.Motion.ShortestRotationThreshold",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMotionShortestRotationTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const float ThresholdDegrees : {10.0f, 170.0f})
		{
			for (const float TurnDegrees : {ThresholdDegrees - 0.1f, ThresholdDegrees + 0.1f, 179.9f})
			{
				const FQuat PositiveTurn(FVector::ZAxisVector, FMath::DegreesToRadians(TurnDegrees));
				const FQuat NegativeTurn(-PositiveTurn.X, -PositiveTurn.Y, -PositiveTurn.Z, -PositiveTurn.W);
				// q と -q の両方で 2acos(min(|W|,1)) を独立に角度へ戻す。角度の丸めに 0.001°。
				const double ShortAngle = FMath::RadiansToDegrees(2.0 * FMath::Acos(
					FMath::Min(FMath::Abs(static_cast<double>(PositiveTurn.W)), 1.0)));
				bOk &= TestTrue(TEXT("入力の短い側角度が指定値と一致"), FMath::Abs(ShortAngle - TurnDegrees) <= 0.001);
				const bool bExpectedTeleport = ShortAngle > ThresholdDegrees;
				const bool bPositiveTeleport = IsRotationTeleport(ThresholdDegrees, PositiveTurn);
				const bool bNegativeTeleport = IsRotationTeleport(ThresholdDegrees, NegativeTurn);
				bOk &= TestEqual(FString::Printf(TEXT("閾値 %.1f°、角度 %.1f° の判定"), ThresholdDegrees, TurnDegrees),
					bPositiveTeleport, bExpectedTeleport);
				bOk &= TestEqual(TEXT("q と -q の判定が一致"), bNegativeTeleport, bPositiveTeleport);
			}
		}
		return bOk;
	}

	// FX-20・FX-21・FX-U4。2025: 最終 SubstepAlpha が半ステップ遅れ、30fps の入力ルートが 0.1cm 遅れる。
	// 60fps は根元を止め、どの空間でも同じ物理になる入力で、切替後を最初から新しい空間の対照と 0.01cm で比べる（FX-U4 (a)）。
	// 根元を動かすと BaseBoneSpace は基準ボーンの移動を慣性として受けず、対照との差（実測 1.19cm）は切替と関係なく生じる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMotionSpaceSwitchTest,
		"KawaiiPhysics.SolverSpec.Motion.SpaceSwitchAtOneAndTwoSteps",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMotionSpaceSwitchTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		const EKawaiiPhysicsSimulationSpace ComponentSpace = EKawaiiPhysicsSimulationSpace::ComponentSpace;
		const EKawaiiPhysicsSimulationSpace WorldSpace = EKawaiiPhysicsSimulationSpace::WorldSpace;
		const EKawaiiPhysicsSimulationSpace BaseSpace = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
		for (const int32 FramesPerSecond : {60, 30})
		{
			for (const EKawaiiPhysicsSimulationSpace SourceSpace : {ComponentSpace, WorldSpace})
			{
				for (const EKawaiiPhysicsSimulationSpace TargetSpace : {BaseSpace, WorldSpace})
				{
					if (SourceSpace == TargetSpace || (SourceSpace == WorldSpace && TargetSpace == WorldSpace))
					{
						continue;
					}
					KawaiiPhysicsNodeFixture::FOptions SourceOptions = MakeChainOptions(1.0f / FramesPerSecond);
					SourceOptions.SimulationSpace = SourceSpace;
					SourceOptions.BaseBoneName = TEXT("chain_1");
					// 60fps は FX-20 の「同じ物理を別空間で表した対照と 0.01cm」を見るので、どの空間でも同じ物理になるよう根元を止める。
					// 根元が動くと BaseBoneSpace は基準ボーンの移動を慣性として受けないため、独立対照とは物理そのものが違う。
					// 30fps は FX-21 の相対基準（切り替えない二セルの差）と根元の到達を見るので、根元を毎フレーム 0.2cm 動かす。
					const double RootStep = FramesPerSecond == 30 ? 0.2 : 0.0;
					SourceOptions.SetInputPose = [RootStep](FCompactPose& InputPose, const int32 Frame)
					{
						InputPose[FCompactPoseBoneIndex(0)].SetTranslation(FVector(RootStep * Frame, 0.0, 0.0));
						InputPose[FCompactPoseBoneIndex(1)].SetRotation(FQuat(FVector::ZAxisVector,
							FMath::DegreesToRadians(15.0)));
					};
					KawaiiPhysicsNodeFixture::FOptions TargetOptions = SourceOptions;
					TargetOptions.SimulationSpace = TargetSpace;
					KawaiiPhysicsNodeFixture::FFixture SwitchedFixture(SourceOptions);
					KawaiiPhysicsNodeFixture::FFixture SourceControl(SourceOptions);
					KawaiiPhysicsNodeFixture::FFixture TargetControl(TargetOptions);
					for (int32 Frame = 0; Frame < 20; ++Frame)
					{
						SwitchedFixture.EvaluateFrame();
						SourceControl.EvaluateFrame();
						TargetControl.EvaluateFrame();
					}
					if (!TestTrue(TEXT("三セルに ModifyBones がある"),
						SwitchedFixture.Node.ModifyBones.Num() >= 5 && SourceControl.Node.ModifyBones.Num() >= 5 &&
						TargetControl.Node.ModifyBones.Num() >= 5))
					{
						return false;
					}
					bOk &= TestTrue(TEXT("重力で先端がポーズから離れた"),
						FVector::Dist(SourceControl.Node.ModifyBones.Last().Location,
							SourceControl.Node.ModifyBones.Last().PoseLocation) > 0.001);
					SwitchedFixture.SetSimulationSpace(TargetSpace, TEXT("chain_1"));
					SwitchedFixture.EvaluateFrame();
					SourceControl.EvaluateFrame();
					TargetControl.EvaluateFrame();
					bOk &= TestEqual(TEXT("切替セルと独立対照の出力本数"),
						SwitchedFixture.OutTransforms.Num(), TargetControl.OutTransforms.Num());
					const double SwitchedDifference = ComputeLargestOutputDifference(
						SwitchedFixture.OutTransforms, TargetControl.OutTransforms);
					const double CellDifference = ComputeLargestOutputDifference(
						SourceControl.OutTransforms, TargetControl.OutTransforms);
					if (FramesPerSecond == 60)
					{
						// 一ステップの同じ物理を別空間で表す変換丸めに 0.01cm。
						bOk &= TestTrue(TEXT("一ステップ切替は独立対照と 0.01cm 以内"), SwitchedDifference <= 0.01);
						double MinimumSwitchedRatio = 1.0;
						double MinimumControlRatio = 1.0;
						for (int32 BoneIndex = 1; BoneIndex < 5; ++BoneIndex)
						{
							MinimumSwitchedRatio = FMath::Min(MinimumSwitchedRatio,
								FVector::Dist(GetOutputLocation(SwitchedFixture.OutTransforms, BoneIndex),
									GetOutputLocation(SwitchedFixture.OutTransforms, BoneIndex - 1)) / 10.0);
							MinimumControlRatio = FMath::Min(MinimumControlRatio,
								FVector::Dist(GetOutputLocation(TargetControl.OutTransforms, BoneIndex),
									GetOutputLocation(TargetControl.OutTransforms, BoneIndex - 1)) / 10.0);
						}
						// 四リンクの元の長さは各 10cm。比の float 丸めに 0.001。
						bOk &= TestTrue(TEXT("切替フレームの骨長比は対照より 0.001 以上縮まない"),
							MinimumSwitchedRatio >= MinimumControlRatio - 0.001);
					}
					else
					{
						// 二ステップの経路差は、切替しない二セルの差の 1.5 倍と変換丸め 0.001cm を上限とする。
						bOk &= TestTrue(TEXT("二ステップ切替は独立対照の空間差と同水準"),
							SwitchedDifference <= CellDifference * 1.5 + 0.001);
						// 根元の入力は毎フレーム 0.2cm 進む。最終ステップは今のポーズへ届く。
						bOk &= TestTrue(TEXT("二ステップの根元は現フレームのポーズに到達"),
							FMath::Abs(SourceControl.Node.ModifyBones[0].Location.X - 4.0) <= 0.0001);
					}
				}
			}
		}
		return bOk;
	}

	// FX-10・FX-U3。mutant なし。出力補間を止める変更は、無ステップの変位 0 と位相割合 0.15 超で落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMotionSlowOutputTest,
		"KawaiiPhysics.SolverSpec.Motion.SlowOutputEveryFrame",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMotionSlowOutputTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsNodeFixture::FOptions SlowOptions = MakeChainOptions(1.0f / 600.0f);
		SlowOptions.SetInputPose = [](FCompactPose& InputPose, const int32 Frame)
		{
			InputPose[FCompactPoseBoneIndex(0)].SetTranslation(FVector(0.1 * Frame, 0.0, 0.0));
		};
		KawaiiPhysicsNodeFixture::FFixture SlowFixture(SlowOptions);
		TArray<double> FrameDistances;
		double PhaseTotals[10] = {};
		FVector PreviousTip = FVector::ZeroVector;
		bool bOk = true;
		for (int32 Frame = 0; Frame <= 200; ++Frame)
		{
			SlowFixture.EvaluateFrame();
			if (!TestTrue(TEXT("実際の ModifyBones と出力がある"),
				SlowFixture.Node.ModifyBones.Num() >= 5 && SlowFixture.OutTransforms.Num() >= 5))
			{
				return false;
			}
			const FVector CurrentTip = GetOutputLocation(SlowFixture.OutTransforms, 4);
			if (Frame > 0)
			{
				const double Distance = FVector::Dist(CurrentTip, PreviousTip);
				FrameDistances.Add(Distance);
				PhaseTotals[(Frame - 1) % 10] += Distance;
				bOk &= TestTrue(TEXT("ステップのないフレームを含めて毎回変位する"), Distance > 0.0);
			}
			PreviousTip = CurrentTip;
		}
		FrameDistances.Sort();
		const double MedianDistance = (FrameDistances[99] + FrameDistances[100]) * 0.5;
		bOk &= TestTrue(TEXT("入力と重力で十分な変位がある"), MedianDistance > 0.0001);
		// 200 変位の中央二値が中央値。等速入力 0.1cm/フレームを基準に最大は 2 倍まで。
		bOk &= TestTrue(TEXT("最大変位は中央値の 2 倍以内"),
			FrameDistances.Last() <= MedianDistance * 2.0);
		double TotalDistance = 0.0;
		double MaximumPhaseDistance = 0.0;
		for (const double PhaseDistance : PhaseTotals)
		{
			TotalDistance += PhaseDistance;
			MaximumPhaseDistance = FMath::Max(MaximumPhaseDistance, PhaseDistance);
		}
		// 10 位相が均等なら各 0.1。float のサブステップ補間を含むため 1.5 倍の 0.15 まで。
		bOk &= TestTrue(TEXT("位相ごとの変位割合は 0.15 以下"),
			MaximumPhaseDistance <= TotalDistance * 0.15);
		return bOk;
	}

	// FX-11。mutant なし。補間時に親だけ進む変更は、各リンク長 10cm との偏差で落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMotionSlowBoneLengthTest,
		"KawaiiPhysics.SolverSpec.Motion.SlowRotatingPoseBoneLength",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMotionSlowBoneLengthTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsNodeFixture::FOptions SlowOptions = MakeChainOptions(1.0f / 600.0f);
		SlowOptions.SetInputPose = [](FCompactPose& InputPose, const int32 Frame)
		{
			InputPose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::YAxisVector,
				FMath::DegreesToRadians(static_cast<double>(Frame))));
		};
		KawaiiPhysicsNodeFixture::FFixture SlowFixture(SlowOptions);
		bool bOk = true;
		for (int32 Frame = 0; Frame < 200; ++Frame)
		{
			SlowFixture.EvaluateFrame();
			if (!TestTrue(TEXT("実際の五本の出力がある"), SlowFixture.OutTransforms.Num() >= 5))
			{
				return false;
			}
			for (int32 BoneIndex = 1; BoneIndex < 5; ++BoneIndex)
			{
				const double LinkLength = FVector::Dist(GetOutputLocation(SlowFixture.OutTransforms, BoneIndex),
					GetOutputLocation(SlowFixture.OutTransforms, BoneIndex - 1));
				// 元の局所オフセットは各 10cm。float の回転と長さ復元に 1e-3cm。
				bOk &= TestTrue(TEXT("回転中も各リンク長は 10cm ±1e-3cm"),
					FMath::Abs(LinkLength - 10.0) <= 0.001);
			}
		}
		bOk &= TestTrue(TEXT("入力ポーズは実際に回転した"),
			FVector::Dist(GetOutputLocation(SlowFixture.OutTransforms, 4), FVector(0.0, 0.0, -40.0)) > 1.0);
		return bOk;
	}

	// FX-22・FX-U5。mutant なし。移動平面の追い越しは、前ステップで表側だった中心の負距離で落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMotionMovingPlaneTest,
		"KawaiiPhysics.SolverSpec.Motion.MovingPlanarFrontSide",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMotionMovingPlaneTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const int32 FramesPerSecond : {30, 45})
		{
			KawaiiPhysicsNodeFixture::FOptions PlanarOptions = MakeChainOptions(1.0f / FramesPerSecond);
			PlanarOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(5, FVector(10.0, 0.0, 0.0));
			PlanarOptions.SimulationSpace = EKawaiiPhysicsSimulationSpace::WorldSpace;
			PlanarOptions.ConfigureDeveloperSettings = [](UKawaiiPhysicsDeveloperSettings& DeveloperSettings)
			{
				DeveloperSettings.bUseFixedSubstepping = false;
			};
			PlanarOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
			{
				PhysicsNode.RootBone.BoneName = TEXT("root");
				PhysicsNode.DummyBoneLength = 0.0f;
				PhysicsNode.WarmUpFrames = 0;
				PhysicsNode.Gravity = FVector(0.0, 0.0, -980.0);
				PhysicsNode.PhysicsSettings.Radius = 3.0f;
				PhysicsNode.bAllowWorldCollision = false;
				PhysicsNode.bUseSimpleWorldCollision = false;
				FPlanarLimit PlanarLimit;
				PlanarLimit.OffsetLocation = FVector(0.0, 0.0, -4.0);
				PlanarLimit.bEnable = true;
				KawaiiPhysicsNodeFixture::AddPlanarLimit(PhysicsNode, PlanarLimit);
			};
			PlanarOptions.GetComponentTransform = [FramesPerSecond](const int32 Frame)
			{
				const double Seconds = static_cast<double>(Frame) / FramesPerSecond;
				return FTransform(FVector(0.0, 0.0, 20.0 * FMath::Sin(6.0 * UE_DOUBLE_PI * Seconds)));
			};
			KawaiiPhysicsNodeFixture::FFixture PlanarFixture(PlanarOptions);
			TArray<FVector> PreviousStepLocations;
			FPlane PreviousPlane(0.0, 0.0, 1.0, 0.0);
			int32 CheckedSteps = 0;
			int32 CheckedOutputs = 0;
			for (int32 Frame = 0; Frame < FramesPerSecond * 3; ++Frame)
			{
				PlanarFixture.EvaluateFrame();
				if (!TestTrue(TEXT("平面と四本の可動ボーンがある"),
					PlanarFixture.Node.PlanarLimits.Num() == 1 && PlanarFixture.Node.ModifyBones.Num() == 5 &&
					PlanarFixture.OutTransforms.Num() >= 5))
				{
					return false;
				}
				const FPlane CurrentPlane = PlanarFixture.Node.PlanarLimits[0].Plane;
				for (int32 BoneIndex = 1; BoneIndex < 5; ++BoneIndex)
				{
					const FKawaiiPhysicsModifyBone& SimulatedBone = PlanarFixture.Node.ModifyBones[BoneIndex];
					if (PreviousStepLocations.Num() == 5 && PreviousPlane.PlaneDot(PreviousStepLocations[BoneIndex]) > 0.0)
					{
						// WorldSpace の毎フレーム一ステップ直後の中心。中心の符号に 0.01cm の幾何丸めを許す。
						bOk &= TestTrue(TEXT("前ステップで表側だった中心はステップ後も表側"),
							CurrentPlane.PlaneDot(SimulatedBone.Location) >= -0.01);
						++CheckedSteps;
					}
					const FVector WorldOutput = PlanarFixture.Proxy->GetComponentTransform().TransformPosition(
						GetOutputLocation(PlanarFixture.OutTransforms, BoneIndex));
					// 長さ復元が半径の内側へ戻す場合があるため、半径分の重なりは主張しない。
					bOk &= TestTrue(TEXT("毎フレームの出力中心は平面の表側"), CurrentPlane.PlaneDot(WorldOutput) >= -0.01);
					++CheckedOutputs;
				}
				PreviousStepLocations.SetNum(5);
				for (int32 BoneIndex = 0; BoneIndex < 5; ++BoneIndex)
				{
					PreviousStepLocations[BoneIndex] = PlanarFixture.Node.ModifyBones[BoneIndex].Location;
				}
				PreviousPlane = CurrentPlane;
			}
			bOk &= TestTrue(TEXT("前ステップ表側の観測がある"), CheckedSteps > 0);
			bOk &= TestTrue(TEXT("出力中心を毎フレーム観測した"), CheckedOutputs == FramesPerSecond * 3 * 4);
			bOk &= TestTrue(TEXT("平面は実際に動いた"),
				FMath::Abs(PlanarFixture.Proxy->GetComponentTransform().GetLocation().Z) > 0.1);
		}
		return bOk;
	}

	// FX-26。mutant なし。テレポート時の移動を繰り越す変更は、先端ずれ 0.01cm と PreSkelCompTransform で落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMotionBaseTeleportTest,
		"KawaiiPhysics.SolverSpec.Motion.BaseBoneTeleportDiscardsMovement",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMotionBaseTeleportTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const bool bExplicitReset : {false, true})
		{
			KawaiiPhysicsNodeFixture::FOptions TeleportOptions = MakeChainOptions(1.0f / 40.0f);
			TeleportOptions.SimulationSpace = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
			TeleportOptions.BaseBoneName = TEXT("chain_1");
			TeleportOptions.GetComponentTransform = [](const int32 Frame)
			{
				return Frame < 10 ? FTransform::Identity : FTransform(FVector(0.0, 120.0, 0.0));
			};
			TeleportOptions.ConfigureNode = [bExplicitReset](FAnimNode_KawaiiPhysics& PhysicsNode)
			{
				PhysicsNode.RootBone.BoneName = TEXT("root");
				PhysicsNode.DummyBoneLength = 0.0f;
				PhysicsNode.WarmUpFrames = 0;
				PhysicsNode.bUseWarmUpWhenResetDynamics = false;
				PhysicsNode.TargetFramerate = 60;
				PhysicsNode.Gravity = FVector::ZeroVector;
				PhysicsNode.PhysicsSettings.Damping = 0.1f;
				PhysicsNode.PhysicsSettings.Stiffness = 0.05f;
				PhysicsNode.PhysicsSettings.WorldDampingLocation = 0.0f;
				PhysicsNode.PhysicsSettings.WorldDampingRotation = 0.0f;
				PhysicsNode.TeleportDistanceThreshold = bExplicitReset ? 300.0f : 50.0f;
				PhysicsNode.TeleportRotationThreshold = 0.0f;
				PhysicsNode.bAllowWorldCollision = false;
				PhysicsNode.bUseSimpleWorldCollision = false;
			};
			KawaiiPhysicsNodeFixture::FFixture TeleportFixture(TeleportOptions);
			for (int32 Frame = 0; Frame < 10; ++Frame)
			{
				TeleportFixture.EvaluateFrame();
			}
			if (!TestTrue(TEXT("BaseBoneSpace に五本の ModifyBones がある"), TeleportFixture.Node.ModifyBones.Num() >= 5))
			{
				return false;
			}
			if (bExplicitReset)
			{
				TeleportFixture.Node.ResetDynamics(ETeleportType::TeleportPhysics);
			}
			double MaximumTipDeviation = 0.0;
			for (int32 Frame = 10; Frame < 70; ++Frame)
			{
				TeleportFixture.EvaluateFrame();
				const FKawaiiPhysicsModifyBone& TipBone = TeleportFixture.Node.ModifyBones.Last();
				MaximumTipDeviation = FMath::Max(MaximumTipDeviation,
					FVector::Dist(TipBone.Location, TipBone.PoseLocation));
				if (Frame == 10)
				{
					// 120cm > 距離閾値 50cm。テレポートでは移動全量を捨てるためジャンプ後の位置へ一致する。
					bOk &= TestTrue(TEXT("ジャンプ直後の PreSkelCompTransform は 120cm"),
						TeleportFixture.Accessor.GetPreSkelCompTransform().GetLocation().Equals(FVector(0.0, 120.0, 0.0), 0.0001));
				}
			}
			// 重力 0、静止ポーズなので残り移動がなければずれは 0。変換丸めに 0.01cm。
			bOk &= TestTrue(TEXT("60 フレームの先端ずれは 0.01cm 未満"), MaximumTipDeviation < 0.01);
		}
		// 対照: 同じ 120cm 移動をテレポートにしなければ慣性が残る。
		KawaiiPhysicsNodeFixture::FOptions ControlOptions = MakeChainOptions(1.0f / 40.0f);
		ControlOptions.SimulationSpace = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
		ControlOptions.BaseBoneName = TEXT("chain_1");
		ControlOptions.GetComponentTransform = [](const int32 Frame)
		{
			return Frame < 10 ? FTransform::Identity : FTransform(FVector(0.0, 120.0, 0.0));
		};
		ControlOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.Gravity = FVector::ZeroVector;
			PhysicsNode.TargetFramerate = 60;
			PhysicsNode.TeleportDistanceThreshold = 300.0f;
			PhysicsNode.TeleportRotationThreshold = 0.0f;
			PhysicsNode.PhysicsSettings.WorldDampingLocation = 0.0f;
			PhysicsNode.PhysicsSettings.WorldDampingRotation = 0.0f;
		};
		KawaiiPhysicsNodeFixture::FFixture ControlFixture(ControlOptions);
		double ControlDeviation = 0.0;
		for (int32 Frame = 0; Frame < 15; ++Frame)
		{
			ControlFixture.EvaluateFrame();
			if (Frame >= 10 && ControlFixture.Node.ModifyBones.Num() >= 5)
			{
				ControlDeviation = FMath::Max(ControlDeviation, FVector::Dist(
					ControlFixture.Node.ModifyBones.Last().Location, ControlFixture.Node.ModifyBones.Last().PoseLocation));
			}
		}
		bOk &= TestTrue(TEXT("非テレポート対照では先端が実際に動く"), ControlDeviation > 0.01);
		return bOk;
	}

	struct FPublisherHost
	{
		UAnimInstance* HostAnim = nullptr;
		TUniquePtr<FAnimNode_KawaiiPhysicsSharedPublisher> Publisher;
	};

	// SP-U3。mutant なし。MarkExpired 後の再取得を失う変更は、空白 3 フレーム超または後発 30 フレーム連続失敗で落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMotionPublisherHandoffTest,
		"KawaiiPhysics.SolverSpec.Motion.SharedPublisherHandoff",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMotionPublisherHandoffTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const bool bPublishersFirst : {true, false})
		{
			const FGameplayTag SharedTag = TAG_KawaiiPhysicsMotionHandoff;
			KawaiiPhysicsNodeFixture::FOptions ConsumerOptions = MakeChainOptions(1.0f / 60.0f);
			ConsumerOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(3);
			ConsumerOptions.bPreUpdateEveryFrame = true;
			ConsumerOptions.ConsoleVariables.Add({TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), TEXT("1")});
			ConsumerOptions.ConsoleVariables.Add({TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.CleanupInterval"), TEXT("0")});
			ConsumerOptions.ConfigureDeveloperSettings = [](UKawaiiPhysicsDeveloperSettings& DeveloperSettings)
			{
				DeveloperSettings.bUseFixedSubstepping = false;
			};
			ConsumerOptions.ConfigureNode = [SharedTag](FAnimNode_KawaiiPhysics& PhysicsNode)
			{
				PhysicsNode.RootBone.BoneName = TEXT("root");
				PhysicsNode.DummyBoneLength = 0.0f;
				PhysicsNode.WarmUpFrames = 0;
				PhysicsNode.bAllowWorldCollision = false;
				PhysicsNode.Gravity = FVector::ZeroVector;
				PhysicsNode.PhysicsSettings.Damping = 1.0f;
				PhysicsNode.PhysicsSettings.Stiffness = 1.0f;
				PhysicsNode.PhysicsSettings.Radius = 1.0f;
				PhysicsNode.PhysicsSettings.LimitAngle = 0.0f;
				PhysicsNode.bUseSimpleWorldCollision = true;
				PhysicsNode.SimpleWorldCollisionSource = EKawaiiPhysicsSimpleWorldCollisionSource::Shared;
				PhysicsNode.SimpleWorldCollisionSharedTag = SharedTag;
				PhysicsNode.bSimpleWorldCollisionGroundCollision = false;
			};
			KawaiiPhysicsNodeFixture::FFixture ConsumerFixture(ConsumerOptions);
			UKawaiiPhysicsSharedCollisionSubsystem* CollisionSubsystem =
				ConsumerFixture.World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>();
			if (!TestNotNull(TEXT("共有コリジョン Subsystem がある"), CollisionSubsystem))
			{
				return false;
			}
			auto MakePublisherHost = [&ConsumerFixture, SharedTag]()
			{
				FPublisherHost PublisherHost;
				USkeletalMeshComponent* HostComponent = NewObject<USkeletalMeshComponent>(ConsumerFixture.Actor);
				ConsumerFixture.Actor->AddInstanceComponent(HostComponent);
				PublisherHost.HostAnim = NewObject<UAnimInstance>(HostComponent);
				PublisherHost.Publisher = MakeUnique<FAnimNode_KawaiiPhysicsSharedPublisher>();
				PublisherHost.Publisher->SharedGroupTag = SharedTag;
				PublisherHost.Publisher->SimpleWorldCollision.GatherInterval = 1000.0f;
				PublisherHost.Publisher->SimpleWorldCollision.bOverrideGatherRadius = true;
				PublisherHost.Publisher->SimpleWorldCollision.GatherRadius = 200.0f;
				PublisherHost.Publisher->SimpleWorldCollision.bGroundCollision = false;
				return PublisherHost;
			};
			FPublisherHost FirstPublisher = MakePublisherHost();
			// 後発を先に確保してから先発を破棄し、SourceID のアドレス再利用を避ける。
			FPublisherHost SecondPublisher = MakePublisherHost();
			const uint64 SecondProviderID = reinterpret_cast<uint64>(SecondPublisher.Publisher.Get());
			FKawaiiPhysicsSharedCollisionData SphereData;
			FSphericalLimit& SharedSphere = SphereData.SphericalLimits.AddDefaulted_GetRef();
			SharedSphere.Location = FVector(4.0, 0.0, -20.0);
			SharedSphere.Radius = 6.0f;
			SharedSphere.LimitType = ESphericalLimitType::Outer;
			SharedSphere.bEnable = true;
			const FKawaiiPhysicsSimpleWorldRegistryKey SharedKey =
				FKawaiiPhysicsSimpleWorldRegistryKey::MakeSharedKey(ConsumerFixture.Actor, SharedTag);
			FWarningCapture WarningCapture;
			auto StepHandoffFrame = [&](const bool bUpdateSecond)
			{
				constexpr float FrameSeconds = 1.0f / 60.0f;
				ConsumerFixture.World->TimeSeconds += FrameSeconds;
				auto UpdatePublishers = [&]()
				{
					for (FPublisherHost* PublisherHost : {&FirstPublisher, &SecondPublisher})
					{
						if (PublisherHost->Publisher &&
							(PublisherHost == &FirstPublisher || bUpdateSecond))
						{
							PublisherHost->Publisher->PreUpdate(PublisherHost->HostAnim);
							FAnimInstanceProxy UpdateProxy(PublisherHost->HostAnim);
							PublisherHost->Publisher->Update_AnyThread(FAnimationUpdateContext(&UpdateProxy, FrameSeconds));
						}
					}
				};
				if (bPublishersFirst)
				{
					UpdatePublishers();
					ConsumerFixture.EvaluateFrame();
				}
				else
				{
					ConsumerFixture.EvaluateFrame();
					UpdatePublishers();
				}
				const double TipDisplacement = FVector::Dist(
					GetOutputLocation(ConsumerFixture.OutTransforms, 2), FVector(0.0, 0.0, -20.0));
				CollisionSubsystem->Tick(FrameSeconds);
				if (const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> CollisionEntry =
					CollisionSubsystem->FindSimpleWorldEntry(SharedKey))
				{
					if (CollisionEntry->bHasGatheredOnce && CollisionEntry->TimeSinceLastGather == 0.0f)
					{
						FKawaiiPhysicsSharedCollisionData PublishedSphere = SphereData;
						CollisionEntry->Slot.Publish(PublishedSphere);
					}
				}
				++GFrameCounter;
				return TipDisplacement;
			};
			int32 WarmupPushes = 0;
			for (int32 Frame = 0; Frame < 30; ++Frame)
			{
				WarmupPushes += StepHandoffFrame(false) > 0.5 ? 1 : 0;
			}
			bOk &= TestTrue(TEXT("先発による押し出しが成立"), WarmupPushes >= 20);
			bOk &= TestTrue(TEXT("消費ノードの ModifyBones と球がある"),
				ConsumerFixture.Node.ModifyBones.Num() >= 3 && ConsumerFixture.Accessor.GetNumSimpleWorldColliders() > 0);
			const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> FirstEntry =
				CollisionSubsystem->FindSharedPublisherEntry(ConsumerFixture.Actor, SharedTag);
			bOk &= TestTrue(TEXT("先発 provider を解決できた"), FirstEntry.IsValid() &&
				FirstEntry->GetProviderID() == reinterpret_cast<uint64>(FirstPublisher.Publisher.Get()));
			const int32 WarningsBeforeSwap = WarningCapture.Count();
			FirstPublisher.Publisher.Reset();
			int32 ConsecutiveUnpushed = 0;
			int32 MaximumGap = 0;
			int32 LastThirtyPushes = 0;
			for (int32 Frame = 0; Frame < 50; ++Frame)
			{
				const bool bPushed = StepHandoffFrame(true) > 0.5;
				ConsecutiveUnpushed = bPushed ? 0 : ConsecutiveUnpushed + 1;
				MaximumGap = FMath::Max(MaximumGap, ConsecutiveUnpushed);
				if (Frame >= 20)
				{
					LastThirtyPushes += bPushed ? 1 : 0;
				}
			}
			const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> SecondEntry =
				CollisionSubsystem->FindSharedPublisherEntry(ConsumerFixture.Actor, SharedTag);
			bOk &= TestTrue(TEXT("後発 provider が引き継いだ"), SecondEntry.IsValid() &&
				SecondEntry->GetProviderID() == SecondProviderID);
			bOk &= TestTrue(TEXT("押し出しの途切れは連続三フレーム以内"), MaximumGap <= 3);
			bOk &= TestEqual(TEXT("後発で三十フレーム連続して押し出す"), LastThirtyPushes, 30);
			// 警告は期待登録せず、交代後に出れば TestEqual と Automation ログの両方で失敗する。
			bOk &= TestEqual(TEXT("交代後の警告は零"), WarningCapture.Count() - WarningsBeforeSwap, 0);
		}
		return bOk;
	}

	// TS-08・TS-U1。mutant なし。TimeScale 起因の時間破棄を見逃す変更は警告一回、ヒッチ誤警告は零回で落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMotionTimeScaleWarningTest,
		"KawaiiPhysics.SolverSpec.Motion.TimeScaleDropWarningOnly",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMotionTimeScaleWarningTest::RunTest(const FString& Parameters)
	{
#if UE_BUILD_SHIPPING
		return true;
#else
		// M=4、FixedDt=1/60 なので上限は 1/15 秒。d=1/25 は収まり、s=2 の 2/25 は超える。
		KawaiiPhysicsNodeFixture::FOptions ScaledOptions = MakeChainOptions(1.0f / 25.0f);
		ScaledOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.TargetFramerate = 60;
			PhysicsNode.TimeScale = 2.0f;
		};
		KawaiiPhysicsNodeFixture::FOptions HitchOptions = MakeChainOptions(0.1f);
		HitchOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.TargetFramerate = 60;
			PhysicsNode.TimeScale = 1.0f;
		};
		// Warning として 1 回だけ出ることを Automation 側で確かめる（Verbosity を Warning に限る）。
		// UE 5.3 には IsRegex 引数が無く常に正規表現なので、"." をエスケープして文字どおりに一致させる。
		AddExpectedMessage(TEXT("TimeScale 2\\.00 needs more than MaxSubsteps"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, 1);
		FWarningCapture WarningCapture;
		KawaiiPhysicsNodeFixture::FFixture ScaledFixture(ScaledOptions);
		KawaiiPhysicsNodeFixture::FFixture HitchFixture(HitchOptions);
		for (int32 Frame = 0; Frame < 5; ++Frame)
		{
			ScaledFixture.EvaluateFrame();
			HitchFixture.EvaluateFrame();
		}
		bool bOk = TestTrue(TEXT("二セルに ModifyBones がある"),
			ScaledFixture.Node.ModifyBones.Num() > 0 && HitchFixture.Node.ModifyBones.Num() > 0);
		int32 ScaledWarnings = 0;
		int32 HitchWarnings = 0;
		// 期待登録した警告は Verbose に下げて届くので、Verbosity を問わずに出た回数を数える。
		// Warning であることは上の AddExpectedMessage が確かめる。走査はロックを取って写した行に対して行う。
		const TArray<FString> CapturedLines = WarningCapture.CopyAllLines();
		for (const FString& WarningLine : CapturedLines)
		{
			ScaledWarnings += WarningLine.Contains(TEXT("TimeScale 2.00 needs more than MaxSubsteps")) ? 1 : 0;
			HitchWarnings += WarningLine.Contains(TEXT("TimeScale 1.00 needs more than MaxSubsteps")) ? 1 : 0;
		}
		bOk &= TestEqual(TEXT("TimeScale 起因の破棄はノード一回だけ警告"), ScaledWarnings, 1);
		bOk &= TestEqual(TEXT("s=1 のヒッチでは警告しない"), HitchWarnings, 0);
		return bOk;
#endif
	}
}

#endif
