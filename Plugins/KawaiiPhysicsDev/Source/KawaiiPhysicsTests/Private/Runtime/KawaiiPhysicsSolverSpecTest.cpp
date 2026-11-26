// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsLibrary.h"
#include "Misc/AutomationTest.h"

namespace KawaiiPhysicsSolverSpecTestPrivate
{
	KawaiiPhysicsNodeFixture::FOptions MakeMovingChainOptions(const float FrameSeconds = 1.0f / 60.0f)
	{
		KawaiiPhysicsNodeFixture::FOptions ChainOptions;
		ChainOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(5);
		ChainOptions.FrameDt = FrameSeconds;
		ChainOptions.ConfigureDeveloperSettings = [](UKawaiiPhysicsDeveloperSettings& DeveloperSettings)
		{
			DeveloperSettings.bUseFixedSubstepping = true;
			DeveloperSettings.MaxSubsteps = 4;
		};
		ChainOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.WarmUpFrames = 0;
			PhysicsNode.TargetFramerate = 60;
			PhysicsNode.Gravity = FVector(980.0, 0.0, 0.0);
			PhysicsNode.bAllowWorldCollision = false;
			PhysicsNode.bUseSimpleWorldCollision = false;
		};
		return ChainOptions;
	}

	// TS-11。2003: s=0.5 の寿命 0.5 秒を実時間で減らし、30 フレーム目に消してしまう。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecTransientLifetimeTest,
		"KawaiiPhysics.SolverSpec.TimeScale.TransientLifetime",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecTransientLifetimeTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeMovingChainOptions();
		ChainOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.TimeScale = 0.5f;
		};
		KawaiiPhysicsNodeFixture::FFixture PhysicsFixture(ChainOptions);
		PhysicsFixture.EvaluateFrame();
		bool bOk = TestTrue(TEXT("実際の ModifyBones がある"), PhysicsFixture.Node.ModifyBones.Num() > 0);
		const int64 ForceHandle = PhysicsFixture.Node.RequestTransientExternalForce(
			FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>(), 0.5f);
		bOk &= TestTrue(TEXT("一時外力を要求できる"), ForceHandle > 0);
		for (int32 Frame = 1; Frame <= 62; ++Frame)
		{
			PhysicsFixture.EvaluateFrame();
			if (Frame == 1 || Frame == 30 || Frame == 58)
			{
				bOk &= TestEqual(FString::Printf(TEXT("物理時間 %.3f 秒では保持"), Frame / 120.0f),
					PhysicsFixture.Node.TransientForceStore.Items.Num(), 1);
			}
		}
		// 60 フレーム × (1/60) × 0.5 = 0.5 秒。float の累積丸めに 2 フレーム許す。
		bOk &= TestEqual(TEXT("物理時間が寿命を越えた後は消える"), PhysicsFixture.Node.TransientForceStore.Items.Num(), 0);
		return bOk;
	}

	// TS-12。2004: s=0.5/2 の Duration を実時間で進め、s=0.5 の 30 フレーム時点で項目を消す。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMultiplierDurationTest,
		"KawaiiPhysics.SolverSpec.TimeScale.MultiplierDuration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMultiplierDurationTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const float Scale : {0.5f, 2.0f})
		{
			KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeMovingChainOptions();
			ChainOptions.ConfigureNode = [Scale](FAnimNode_KawaiiPhysics& PhysicsNode)
			{
				PhysicsNode.RootBone.BoneName = TEXT("root");
				PhysicsNode.TimeScale = Scale;
			};
			KawaiiPhysicsNodeFixture::FFixture PhysicsFixture(ChainOptions);
			PhysicsFixture.EvaluateFrame();
			bOk &= TestTrue(TEXT("実際の ModifyBones がある"), PhysicsFixture.Node.ModifyBones.Num() > 0);
			FKawaiiPhysicsSettingsMultiplier Multiplier;
			Multiplier.Damping = 0.5f;
			const int64 MultiplierHandle = PhysicsFixture.Node.RequestStartPhysicsSettingsMultiplier(
				Multiplier, 0.25f, 0.5f, 0.25f);
			bOk &= TestTrue(TEXT("倍率を要求できる"), MultiplierHandle > 0);
			const int32 LastLiveFrame = Scale == 0.5f ? 58 : 28;
			const int32 ExpiredFrame = Scale == 0.5f ? 122 : 32;
			for (int32 Frame = 1; Frame <= ExpiredFrame; ++Frame)
			{
				PhysicsFixture.EvaluateFrame();
				if (Frame == 1 || Frame == LastLiveFrame)
				{
					bOk &= TestEqual(FString::Printf(TEXT("s=%.1f の %d フレームで有効"), Scale, Frame),
						PhysicsFixture.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
				}
			}
			// D/s は 2 秒または 0.5 秒。終了境界の float 丸めには 2 フレーム許す。
			bOk &= TestEqual(FString::Printf(TEXT("s=%.1f の物理時間 1 秒後は終了"), Scale),
				PhysicsFixture.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
		}
		return bOk;
	}

	// FX-21・FX-10。2025: 最終 SubstepAlpha が現フレームに届かず、30fps でルートが 0.5cm 遅れる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMovingPoseSubstepTest,
		"KawaiiPhysics.SolverSpec.Substep.MovingRootPose",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMovingPoseSubstepTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const int32 FramesPerSecond : {30, 60})
		{
			KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeMovingChainOptions(1.0f / FramesPerSecond);
			ChainOptions.SetInputPose = [](FCompactPose& InputPose, const int32 Frame)
			{
				InputPose[FCompactPoseBoneIndex(0)].SetTranslation(FVector(Frame + 1.0, 0.0, 0.0));
			};
			KawaiiPhysicsNodeFixture::FFixture PhysicsFixture(ChainOptions);
			for (int32 Frame = 0; Frame < 20; ++Frame)
			{
				PhysicsFixture.EvaluateFrame();
				bOk &= TestTrue(TEXT("実際の ModifyBones がある"), PhysicsFixture.Node.ModifyBones.Num() > 0);
				if (!PhysicsFixture.Node.ModifyBones.IsEmpty())
				{
					// 根元は固定され、入力ポーズの 1cm/フレームに一致する。float の変換丸めに 1e-4cm。
					bOk &= TestTrue(FString::Printf(TEXT("%dfps のフレーム %d の根元"), FramesPerSecond, Frame),
						FMath::Abs(PhysicsFixture.Node.ModifyBones[0].Location.X - (Frame + 1.0)) <= 0.0001);
				}
			}
		}
		return bOk;
	}

	// TS-01・SP-03。mutant なし。既定値と同じ値を明示した対照は 60 フレームの全出力で一致する。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecDefaultControlTest,
		"KawaiiPhysics.SolverSpec.Defaults.TimeAndCollisionSource",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecDefaultControlTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeMovingChainOptions();
		KawaiiPhysicsNodeFixture::FOptions ExplicitOptions = ChainOptions;
		ExplicitOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.TargetFramerate = 60;
			PhysicsNode.Gravity = FVector(980.0, 0.0, 0.0);
			PhysicsNode.TimeScale = 1.0f;
			PhysicsNode.SimpleWorldCollisionSource = EKawaiiPhysicsSimpleWorldCollisionSource::Local;
		};
		KawaiiPhysicsNodeFixture::FFixture DefaultFixture(ChainOptions);
		KawaiiPhysicsNodeFixture::FFixture ExplicitFixture(ExplicitOptions);
		bool bOk = TestEqual(TEXT("既定の TimeScale"), DefaultFixture.Node.TimeScale, 1.0f);
		bOk &= TestEqual(TEXT("既定の Source"), DefaultFixture.Node.SimpleWorldCollisionSource,
			EKawaiiPhysicsSimpleWorldCollisionSource::Local);
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			DefaultFixture.EvaluateFrame();
			ExplicitFixture.EvaluateFrame();
			bOk &= TestTrue(TEXT("両セルに ModifyBones がある"), DefaultFixture.Node.ModifyBones.Num() > 0 && ExplicitFixture.Node.ModifyBones.Num() > 0);
			bOk &= TestEqual(TEXT("出力数が一致"), DefaultFixture.OutTransforms.Num(), ExplicitFixture.OutTransforms.Num());
			for (int32 Index = 0; Index < FMath::Min(DefaultFixture.OutTransforms.Num(), ExplicitFixture.OutTransforms.Num()); ++Index)
			{
				bOk &= TestTrue(TEXT("明示した既定値と位置がビット一致"),
					DefaultFixture.OutTransforms[Index].Transform.GetLocation() == ExplicitFixture.OutTransforms[Index].Transform.GetLocation());
			}
		}
		return bOk;
	}

	// FX-24・FX-U4。2026（mutation で killed を確認済み）:BaseBone の 1 フレーム移動を 2 Substep に分けないと、重力なしのセルの 30/60fps で
	// Base の p95 が対照の 1.5 倍を超える見込み（30fps の二つのサブステップがどちらもフレーム全体の回転で追従する）。
	// 対照は同じ入力の ComponentSpace。chain_2 をポーズで 30° 曲げてから chain_1 を Z 軸まわりに回すので、
	// ComponentSpace でも子のポーズ位置が円を描いて動き、固定サブステップでも fps 差が出る（まっすぐな鎖を Z 軸まわりに回すと位置が動かず対照にならない）。
	// PR-13 修正前は重力（+X）ありの 30/60fps が 0.82cm > 対照 × 1.5 + 0.001 = 0.664cm で失敗する。
	// 重力もステップの基準ボーンで変換し、この組にも同じ主張を適用する。
	// セル 2 は WorldDamping 0（基準ボーンの回転に全量追従）。ボーンが基準ボーンの位相をそのまま映すので、入力の時刻を
	// 経過時間 (Frame + 1) / Rate に合わせ、最初の 0.1 秒は止めてから回す（全 fps で同じ時刻に同じ角度、始まりの揺れも同じ）。
	// ポーズを Frame / Rate で与えると経過時間より 1 フレーム（fps ごとに長さが違う）遅れ、60/120fps で 1.7cm の見かけの差が出る。
	// 比べるのは出力（Component 空間）。BaseBoneSpace の Location は消費済みの基準ボーンの座標で、ステップの無いフレームでは古い座標になる。
	// 計測（2026-11-27）: この入力で Base の 30/60・60/120fps の p95 は 0.0000cm（対照 0.0108 / 0.0000）。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecBaseBoneFollowRatesTest,
		"KawaiiPhysics.SolverSpec.Substep.BaseBoneFollowAcrossRates",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecBaseBoneFollowRatesTest::RunTest(const FString& Parameters)
	{
		const int32 Rates[3] = {30, 60, 120};
		auto ComputeP95Difference = [](const TArray<FVector>& First, const TArray<FVector>& Second)
		{
			TArray<double> Distances;
			for (int32 Index = 0; Index < FMath::Min(First.Num(), Second.Num()); ++Index)
			{
				Distances.Add(FVector::Dist(First[Index], Second[Index]));
			}
			Distances.Sort();
			return Distances.IsEmpty() ? 0.0 : Distances[FMath::Min(Distances.Num() - 1, 28)];
		};
		bool bOk = true;
		for (int32 CellIndex = 0; CellIndex < 3; ++CellIndex)
		{
			const bool bWithGravity = CellIndex == 1;
			const bool bSynchronizedStart = CellIndex == 2;
			TArray<FVector> BaseLocations[3];
			TArray<FVector> ComponentLocations[3];
			for (int32 RateIndex = 0; RateIndex < 3; ++RateIndex)
			{
				const int32 Rate = Rates[RateIndex];
				for (const bool bBaseBoneSpace : {false, true})
				{
					KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeMovingChainOptions(1.0f / Rate);
					ChainOptions.SimulationSpace = bBaseBoneSpace
						? EKawaiiPhysicsSimulationSpace::BaseBoneSpace : EKawaiiPhysicsSimulationSpace::ComponentSpace;
					ChainOptions.BaseBoneName = TEXT("chain_1");
					const auto ConfigureBase = ChainOptions.ConfigureNode;
					ChainOptions.ConfigureNode = [ConfigureBase, bWithGravity, bSynchronizedStart](FAnimNode_KawaiiPhysics& PhysicsNode)
					{
						ConfigureBase(PhysicsNode);
						PhysicsNode.Gravity = bWithGravity ? FVector(980.0, 0.0, 0.0) : FVector::ZeroVector;
						if (bSynchronizedStart)
						{
							PhysicsNode.PhysicsSettings.WorldDampingLocation = 0.0f;
							PhysicsNode.PhysicsSettings.WorldDampingRotation = 0.0f;
						}
					};
					ChainOptions.SetInputPose = [Rate, bSynchronizedStart](FCompactPose& InputPose, const int32 Frame)
					{
						// 60fps で 3°/フレーム、すなわち 180°/秒を全レートで同じ実時間だけ与える。
						// セル 2 は経過時間に合わせ、最初の 0.1 秒は止める（上の注記）。
						const float ElapsedSeconds = static_cast<float>(Frame + 1) / Rate;
						const float Angle = 180.0f * (bSynchronizedStart ? FMath::Max(0.0f, ElapsedSeconds - 0.1f) : ElapsedSeconds);
						InputPose[FCompactPoseBoneIndex(1)].SetRotation(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Angle)));
						// chain_2 から先を回転軸から外し、Z 軸まわりの回転で子のポーズ位置が動くようにする。
						InputPose[FCompactPoseBoneIndex(2)].SetRotation(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(30.0f)));
					};
					KawaiiPhysicsNodeFixture::FFixture PhysicsFixture(ChainOptions);
					TArray<FVector>& SampleLocations = bBaseBoneSpace ? BaseLocations[RateIndex] : ComponentLocations[RateIndex];
					for (int32 Frame = 0; Frame < Rate; ++Frame)
					{
						PhysicsFixture.EvaluateFrame();
						bOk &= TestTrue(TEXT("実際の ModifyBones がある"), PhysicsFixture.Node.ModifyBones.Num() > 1);
						if ((Frame + 1) % (Rate / 30) == 0 && PhysicsFixture.Node.ModifyBones.Num() > 1)
						{
							// セル 2 は出力（Component 空間）を比べる。OutTransforms の並びは全セルで同じ。
							SampleLocations.Add(bSynchronizedStart && PhysicsFixture.OutTransforms.Num() > 0
								? PhysicsFixture.OutTransforms.Last().Transform.GetLocation()
								: PhysicsFixture.Node.ModifyBones.Last().Location);
						}
					}
				}
			}
			for (int32 RateIndex = 0; RateIndex < 3; ++RateIndex)
			{
				bOk &= TestEqual(TEXT("BaseBone の 30 サンプル"), BaseLocations[RateIndex].Num(), 30);
				bOk &= TestEqual(TEXT("Component の 30 サンプル"), ComponentLocations[RateIndex].Num(), 30);
				if (bSynchronizedStart && BaseLocations[RateIndex].Num() > 0)
				{
					// セル 2 の対照: 入力が効いて出力が動いている（全量追従なので回転の円弧を描く。始まりからの最大距離 > 1cm）。
					double MaxTravel = 0.0;
					for (const FVector& Sample : BaseLocations[RateIndex])
					{
						MaxTravel = FMath::Max(MaxTravel, FVector::Dist(Sample, BaseLocations[RateIndex][0]));
					}
					bOk &= TestTrue(FString::Printf(TEXT("セル 2 の %dfps で BaseBone の出力が動く（%.4f > 1）"),
						Rates[RateIndex], MaxTravel), MaxTravel > 1.0);
				}
			}
			for (int32 PairIndex = 0; PairIndex < 2; ++PairIndex)
			{
				const double BaseP95 = ComputeP95Difference(BaseLocations[PairIndex], BaseLocations[PairIndex + 1]);
				const double ComponentP95 = ComputeP95Difference(ComponentLocations[PairIndex], ComponentLocations[PairIndex + 1]);
				if (!bSynchronizedStart)
				{
					// セル 2 は入力の時刻をそろえたので対照の fps 差がほぼ 0 になる。対照は上の「出力が動く」で見る。
					bOk &= TestTrue(FString::Printf(TEXT("セル %d の %d/%dfps で対照にも fps 差がある"),
						CellIndex, Rates[PairIndex], Rates[PairIndex + 1]), ComponentP95 > 0.001);
				}
				// 異なる fps の積分誤差を ComponentSpace の p95 で正規化し、float の位置丸めに 0.001cm を加える（FX-U4 (b)）。
				bOk &= TestTrue(FString::Printf(TEXT("セル %d の %d/%dfps の Base p95 %.4f <= 対照 %.4f × 1.5 + 0.001"),
					CellIndex, Rates[PairIndex], Rates[PairIndex + 1], BaseP95, ComponentP95),
					BaseP95 <= ComponentP95 * 1.5 + 0.001);
			}
		}
		return bOk;
	}

	// FX-33。2175: ピン相当のアセット差し替えで ModifyBones を再構築せず、MergedBoneConstraints が A の一件のまま残る。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecConstraintAssetPinTest,
		"KawaiiPhysics.SolverSpec.Setter.ConstraintAssetPin",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecConstraintAssetPinTest::RunTest(const FString& Parameters)
	{
		auto AddConstraint = [](UKawaiiPhysicsBoneConstraintsDataAsset& ConstraintAsset,
			const FName FirstName, const FName SecondName)
		{
			FModifyBoneConstraintData& ConstraintData = ConstraintAsset.BoneConstraintsData.AddDefaulted_GetRef();
			ConstraintData.BoneReference1.BoneName = FirstName;
			ConstraintData.BoneReference2.BoneName = SecondName;
		};
		UKawaiiPhysicsBoneConstraintsDataAsset* FirstAsset = NewObject<UKawaiiPhysicsBoneConstraintsDataAsset>();
		UKawaiiPhysicsBoneConstraintsDataAsset* SecondAsset = NewObject<UKawaiiPhysicsBoneConstraintsDataAsset>();
		AddConstraint(*FirstAsset, TEXT("root"), TEXT("chain_2"));
		AddConstraint(*SecondAsset, TEXT("root"), TEXT("chain_3"));
		AddConstraint(*SecondAsset, TEXT("chain_1"), TEXT("chain_4"));
		KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeMovingChainOptions();
		ChainOptions.ConfigureNode = [FirstAsset](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.BoneConstraintsDataAsset = FirstAsset;
			PhysicsNode.bAutoAddChildDummyBoneConstraint = false;
		};
		KawaiiPhysicsNodeFixture::FFixture PhysicsFixture(ChainOptions);
		PhysicsFixture.EvaluateFrame();
		bool bOk = TestTrue(TEXT("実際の ModifyBones がある"), PhysicsFixture.Node.ModifyBones.Num() > 0);
		bOk &= TestEqual(TEXT("初期アセットの一拘束"), PhysicsFixture.Node.MergedBoneConstraints.Num(), 1);
		PhysicsFixture.Node.BoneConstraintsDataAsset = SecondAsset;
		PhysicsFixture.EvaluateFrame();
		bOk &= TestEqual(TEXT("差し替え先の二拘束"), PhysicsFixture.Node.MergedBoneConstraints.Num(), 2);
		if (PhysicsFixture.Node.MergedBoneConstraints.Num() == 2)
		{
			bOk &= TestEqual(TEXT("第一拘束の一方"), PhysicsFixture.Node.MergedBoneConstraints[0].Bone1.BoneName, FName(TEXT("root")));
			bOk &= TestEqual(TEXT("第一拘束の他方"), PhysicsFixture.Node.MergedBoneConstraints[0].Bone2.BoneName, FName(TEXT("chain_3")));
			bOk &= TestEqual(TEXT("第二拘束の一方"), PhysicsFixture.Node.MergedBoneConstraints[1].Bone1.BoneName, FName(TEXT("chain_1")));
			bOk &= TestEqual(TEXT("第二拘束の他方"), PhysicsFixture.Node.MergedBoneConstraints[1].Bone2.BoneName, FName(TEXT("chain_4")));
		}
		return bOk;
	}

	// FX-18・FX-20。2032: CS→BB・WS→BB で PrevBaseBone の更新を飛ばすと、旧空間の値（ここでは Identity）のまま
	// 移動・回転した基準ボーンへ Follow Rotation し、横向き重力で揺らした CS→WS の対照から切替フレームで 0.73cm 跳ぶ（実測）。
	// 固定サブステップの出力は、直前と最新のステップのポーズオフセットを蓄積の端数で補間する。60fps（1 フレームちょうど
	// 1 ステップ）では端数が 0 で、切替フレームの出力に切替ステップの結果が入らず改変を検出できない（差 0、次のフレームで 2.7cm）。
	// 65fps にして 1 フレーム 1 ステップ以下（FX-20）のまま、切替フレームに 1 ステップと端数（約 0.38）を残す。
	// 基準ボーンは構築時に解決しておく。切替と同時に名前を入れると、切替フレームでは未解決で Identity になり、
	// 更新の前後が同じ値になって改変を検出できない。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecSwitchToBaseBoneTest,
		"KawaiiPhysics.SolverSpec.Space.SwitchToBaseBone",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecSwitchToBaseBoneTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeMovingChainOptions(1.0f / 65.0f);
		ChainOptions.BaseBoneName = TEXT("chain_1");
		ChainOptions.SetInputPose = [](FCompactPose& InputPose, const int32 Frame)
		{
			InputPose[FCompactPoseBoneIndex(1)].SetTranslation(FVector(0.0, 0.0, 5.5));
			InputPose[FCompactPoseBoneIndex(1)].SetRotation(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(30.0)));
		};
		bool bOk = true;
		for (const EKawaiiPhysicsSimulationSpace SourceSpace :
			{EKawaiiPhysicsSimulationSpace::ComponentSpace, EKawaiiPhysicsSimulationSpace::WorldSpace})
		{
			const TCHAR* SourceLabel = SourceSpace == EKawaiiPhysicsSimulationSpace::ComponentSpace ? TEXT("CS") : TEXT("WS");
			KawaiiPhysicsNodeFixture::FOptions SwitchedOptions = ChainOptions;
			SwitchedOptions.SimulationSpace = SourceSpace;
			KawaiiPhysicsNodeFixture::FFixture SwitchedFixture(SwitchedOptions);
			KawaiiPhysicsNodeFixture::FFixture ReferenceFixture(ChainOptions);
			for (int32 Frame = 0; Frame < 20; ++Frame)
			{
				SwitchedFixture.EvaluateFrame();
				ReferenceFixture.EvaluateFrame();
			}
			bOk &= TestTrue(FString::Printf(TEXT("%s: 両セルに ModifyBones がある"), SourceLabel),
				SwitchedFixture.Node.ModifyBones.Num() > 0 && ReferenceFixture.Node.ModifyBones.Num() > 0);
			if (!SwitchedFixture.Node.ModifyBones.IsEmpty())
			{
				bOk &= TestTrue(FString::Printf(TEXT("%s: 横向き重力で実際に揺れた"), SourceLabel),
					FVector::Dist(SwitchedFixture.Node.ModifyBones.Last().Location,
						SwitchedFixture.Node.ModifyBones.Last().PoseLocation) > 0.01);
			}
			bOk &= TestTrue(FString::Printf(TEXT("%s: 切替前に基準ボーンが解決済み"), SourceLabel),
				SwitchedFixture.Node.SimulationBaseBone.IsValidToEvaluate(SwitchedFixture.Proxy->GetRequiredBones()));
			const float AccumulatorBeforeSwitch = SwitchedFixture.Accessor.GetSubstepAccumulator();
			SwitchedFixture.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::BaseBoneSpace, TEXT("chain_1"));
			ReferenceFixture.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::WorldSpace, TEXT("chain_1"));
			SwitchedFixture.EvaluateFrame();
			ReferenceFixture.EvaluateFrame();
			// 蓄積が減れば切替フレームで 1 ステップ進んでおり、端数が残れば出力に切替ステップの結果が入る。
			const float AccumulatorAfterSwitch = SwitchedFixture.Accessor.GetSubstepAccumulator();
			bOk &= TestTrue(FString::Printf(TEXT("%s: 切替フレームで 1 ステップ進み端数が残る"), SourceLabel),
				AccumulatorAfterSwitch > 0.0f && AccumulatorAfterSwitch < AccumulatorBeforeSwitch);
			bOk &= TestEqual(FString::Printf(TEXT("%s: 出力ボーン数が一致"), SourceLabel),
				SwitchedFixture.OutTransforms.Num(), ReferenceFixture.OutTransforms.Num());
			for (int32 Index = 0; Index < FMath::Min(SwitchedFixture.OutTransforms.Num(), ReferenceFixture.OutTransforms.Num()); ++Index)
			{
				// 同じ物理の別空間経路による float の変換丸めと 1 ステップの差に 0.01cm。
				bOk &= TestTrue(FString::Printf(TEXT("%s→BB 切替後のボーン %d は CS→WS の対照と 0.01cm 以内"), SourceLabel, Index),
					FVector::Dist(SwitchedFixture.OutTransforms[Index].Transform.GetLocation(),
						ReferenceFixture.OutTransforms[Index].Transform.GetLocation()) <= 0.01);
			}
		}
		return bOk;
	}

	// FX-16・FX-17。2033: 二回目の Initialize 後に PrevBaseBone の更新を飛ばすと、参照ポーズの変換（(0,0,-10)・回転なし）から
	// 移動・回転した基準ボーンへ Follow Rotation し、BB 側だけ CS の対照から最初の評価で 0.47cm 跳ぶ（実測）。
	// 固定サブステップの出力は、直前と最新のステップのポーズオフセットを蓄積の端数で補間し、再初期化の直後は直前のオフセットが 0。
	// 60fps（1 フレームちょうど 1 ステップ、端数 0）では最初の評価の出力がポーズそのものになり、改変を検出できない
	// （差 0、次のフレームで 0.30cm）。45fps にして、最初の評価に 1 ステップと端数 1/3 を残す。
	// まっすぐな鎖は基準ボーンの Z 回転の軸上にあり、軸方向のずれは長さ復元で消える（45fps でも 0.058cm）。
	// chain_2 をポーズで X 軸まわりに 30° 曲げて子を軸から外し、回転のずれが出力に残るようにする。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecBaseBoneReinitializeTest,
		"KawaiiPhysics.SolverSpec.Space.BaseBoneReinitialize",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecBaseBoneReinitializeTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsNodeFixture::FOptions BaseOptions = MakeMovingChainOptions(1.0f / 45.0f);
		BaseOptions.SimulationSpace = EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
		BaseOptions.BaseBoneName = TEXT("chain_1");
		BaseOptions.SetInputPose = [](FCompactPose& InputPose, const int32 Frame)
		{
			InputPose[FCompactPoseBoneIndex(1)].SetTranslation(FVector(0.0, 0.0, 5.5));
			InputPose[FCompactPoseBoneIndex(1)].SetRotation(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(30.0)));
			InputPose[FCompactPoseBoneIndex(2)].SetRotation(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(30.0)));
		};
		KawaiiPhysicsNodeFixture::FOptions ComponentOptions = BaseOptions;
		ComponentOptions.SimulationSpace = EKawaiiPhysicsSimulationSpace::ComponentSpace;
		ComponentOptions.BaseBoneName = NAME_None;
		KawaiiPhysicsNodeFixture::FFixture BaseFixture(BaseOptions);
		KawaiiPhysicsNodeFixture::FFixture ComponentFixture(ComponentOptions);
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			BaseFixture.EvaluateFrame();
			ComponentFixture.EvaluateFrame();
		}
		bool bOk = TestTrue(TEXT("両セルに ModifyBones がある"),
			BaseFixture.Node.ModifyBones.Num() > 0 && ComponentFixture.Node.ModifyBones.Num() > 0);
		BaseFixture.Node.Initialize_AnyThread(FAnimationInitializeContext(BaseFixture.Proxy.Get()));
		ComponentFixture.Node.Initialize_AnyThread(FAnimationInitializeContext(ComponentFixture.Proxy.Get()));
		BaseFixture.EvaluateFrame();
		ComponentFixture.EvaluateFrame();
		bOk &= TestTrue(TEXT("最初の評価で端数が残り、出力に最初のステップが入る"),
			BaseFixture.Accessor.GetSubstepAccumulator() > 0.0f);
		bOk &= TestEqual(TEXT("再初期化後の出力数"), BaseFixture.OutTransforms.Num(), ComponentFixture.OutTransforms.Num());
		for (int32 Index = 0; Index < FMath::Min(BaseFixture.OutTransforms.Num(), ComponentFixture.OutTransforms.Num()); ++Index)
		{
			// 同じ静止姿勢を二空間で解く変換誤差に 0.01cm。
			bOk &= TestTrue(FString::Printf(TEXT("再初期化後のボーン %d は対照と一致"), Index),
				FVector::Dist(BaseFixture.OutTransforms[Index].Transform.GetLocation(),
					ComponentFixture.OutTransforms[Index].Transform.GetLocation()) <= 0.01);
		}
		return bOk;
	}

	// FX-38。2182: SyncBones だけの再初期化を飛ばし、ChildTargets が hair_a_child のままになる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecSyncTargetsTest,
		"KawaiiPhysics.SolverSpec.Setter.SyncTargets",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecSyncTargetsTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeMovingChainOptions();
		ChainOptions.Bones = {
			{TEXT("root"), INDEX_NONE, FTransform::Identity},
			{TEXT("hair_a"), 0, FTransform(FVector(0.0, 5.0, 0.0))},
			{TEXT("hair_a_child"), 1, FTransform(FVector(0.0, 0.0, -10.0))},
			{TEXT("hair_b"), 0, FTransform(FVector(0.0, -5.0, 0.0))},
			{TEXT("hair_b_child"), 3, FTransform(FVector(0.0, 0.0, -10.0))},
		};
		ChainOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.Gravity = FVector(980.0, 0.0, 0.0);
			FKawaiiPhysicsSyncBone& SyncBone = PhysicsNode.SyncBones.AddDefaulted_GetRef();
			SyncBone.Bone.BoneName = TEXT("root");
			SyncBone.TargetRoots.AddDefaulted_GetRef().Bone.BoneName = TEXT("hair_a");
		};
		KawaiiPhysicsNodeFixture::FFixture PhysicsFixture(ChainOptions);
		TArray<FVector> PrevFrameLocations;
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			if (Frame == 9)
			{
				for (const FKawaiiPhysicsModifyBone& SimBone : PhysicsFixture.Node.ModifyBones) { PrevFrameLocations.Add(SimBone.Location); }
			}
			PhysicsFixture.EvaluateFrame();
		}
		bool bOk = TestTrue(TEXT("五本の ModifyBones がある"), PhysicsFixture.Node.ModifyBones.Num() >= 5);
		bOk &= TestTrue(TEXT("初期の子対象がある"), PhysicsFixture.Node.SyncBones.Num() == 1 &&
			PhysicsFixture.Node.SyncBones[0].TargetRoots.Num() == 1 &&
			PhysicsFixture.Node.SyncBones[0].TargetRoots[0].ChildTargets.Num() > 0);
		if (!bOk || PrevFrameLocations.Num() != PhysicsFixture.Node.ModifyBones.Num()) { return false; }
		// 位置の連続性の基準: setter の直前の 1 フレームで各ボーンが動いた量の最大。
		// ModifyBones を作り直すとボーンはポーズへ戻るので、その距離（ポーズからのずれ）がこの基準より十分大きいことを対照で確かめる。
		TArray<FVector> BeforeSetterLocations;
		double MaxFrameStep = 0.0;
		double MaxPoseOffset = 0.0;
		for (int32 Index = 0; Index < PhysicsFixture.Node.ModifyBones.Num(); ++Index)
		{
			const FKawaiiPhysicsModifyBone& SimBone = PhysicsFixture.Node.ModifyBones[Index];
			BeforeSetterLocations.Add(SimBone.Location);
			MaxFrameStep = FMath::Max(MaxFrameStep, FVector::Distance(SimBone.Location, PrevFrameLocations[Index]));
			MaxPoseOffset = FMath::Max(MaxPoseOffset, FVector::Distance(SimBone.Location, SimBone.PoseLocation));
		}
		// 連続とみなす上限は直前の 1 フレームの動きの 3 倍 + 0.01cm（PR-10 の位置の連続と同じ考え方）。
		const double ContinuityBound = 3.0 * MaxFrameStep + 0.01;
		bOk &= TestTrue(FString::Printf(TEXT("対照: 作り直しの跳ね（ポーズからのずれ %.4fcm）は連続の上限 %.4fcm より大きい"), MaxPoseOffset, ContinuityBound),
			MaxPoseOffset > ContinuityBound);
		const FName SyncName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SyncBones);
		bOk &= TestTrue(TEXT("SyncBones の文字列 setter が成功"),
			UKawaiiPhysicsLibrary::SetNodePropertyValueFromString(PhysicsFixture.Node, SyncName,
				TEXT("((Bone=(BoneName=\"root\"),TargetRoots=((Bone=(BoneName=\"hair_b\")))))")));
		bOk &= TestFalse(TEXT("ModifyBones の作り直しを要求しない"), PhysicsFixture.Accessor.IsModifyBonesReinitPending());
		PhysicsFixture.EvaluateFrame();
		// 作り直していなければ、各ボーンの位置は直前のフレームから連続している。
		if (!TestEqual(TEXT("ModifyBones の本数は変わらない"), PhysicsFixture.Node.ModifyBones.Num(), BeforeSetterLocations.Num())) { return false; }
		double MaxStepAfterSetter = 0.0;
		for (int32 Index = 0; Index < BeforeSetterLocations.Num(); ++Index)
		{
			MaxStepAfterSetter = FMath::Max(MaxStepAfterSetter, FVector::Distance(PhysicsFixture.Node.ModifyBones[Index].Location, BeforeSetterLocations[Index]));
		}
		bOk &= TestTrue(FString::Printf(TEXT("位置は直前のフレームから連続（%.4fcm ≤ %.4fcm）"), MaxStepAfterSetter, ContinuityBound),
			MaxStepAfterSetter <= ContinuityBound);
		// 作り直した対象の中身は無条件に確かめる（空になる壊れ方で主張を飛ばさない）。
		if (!TestEqual(TEXT("SyncBones は 1 件"), PhysicsFixture.Node.SyncBones.Num(), 1)) { return false; }
		if (!TestEqual(TEXT("TargetRoots は 1 件"), PhysicsFixture.Node.SyncBones[0].TargetRoots.Num(), 1)) { return false; }
		const FKawaiiPhysicsSyncTargetRoot& TargetRoot = PhysicsFixture.Node.SyncBones[0].TargetRoots[0];
		if (!TestTrue(TEXT("新しい親の子が収集された"), TargetRoot.ChildTargets.Num() > 0)) { return false; }
		if (!TestTrue(TEXT("子対象の添字が有効"), PhysicsFixture.Node.ModifyBones.IsValidIndex(TargetRoot.ChildTargets[0].ModifyBoneIndex))) { return false; }
		bOk &= TestEqual(TEXT("子対象は hair_b_child"),
			PhysicsFixture.Node.ModifyBones[TargetRoot.ChildTargets[0].ModifyBoneIndex].BoneRef.BoneName,
			FName(TEXT("hair_b_child")));
		return bOk;
	}

	// SM-08・SM-07。mutant 2127/2128 は既存ハーネスを迂回するため、この本番 Evaluate の gate で検出する。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSolverSpecMultiplierGateTest,
		"KawaiiPhysics.SolverSpec.Multiplier.UpdateGateRestoreOnce",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsSolverSpecMultiplierGateTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeMovingChainOptions();
		ChainOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& PhysicsNode)
		{
			PhysicsNode.RootBone.BoneName = TEXT("root");
			PhysicsNode.DummyBoneLength = 0.0f;
			PhysicsNode.bUpdatePhysicsSettingsInGame = false;
			PhysicsNode.PhysicsSettings.Damping = 0.4f;
		};
		KawaiiPhysicsNodeFixture::FFixture PhysicsFixture(ChainOptions);
		PhysicsFixture.EvaluateFrame();
		if (!TestTrue(TEXT("実際の ModifyBones がある"), PhysicsFixture.Node.ModifyBones.Num() > 0))
		{
			return false;
		}
		FKawaiiPhysicsSettingsMultiplier Multiplier;
		Multiplier.Damping = 0.5f;
		PhysicsFixture.Node.RequestStartPhysicsSettingsMultiplier(Multiplier, 0.0f, 0.1f, 0.0f);
		bool bOk = true;
		bool bSawActive = false;
		bool bSawRestore = false;
		for (int32 Frame = 0; Frame < 12; ++Frame)
		{
			PhysicsFixture.Node.ModifyBones[1].PhysicsSettings.Damping = 123.0f;
			PhysicsFixture.EvaluateFrame();
			const float ActualDamping = PhysicsFixture.Node.ModifyBones[1].PhysicsSettings.Damping;
			if (!PhysicsFixture.Node.TransientForceStore.SettingsMultiplierItems.IsEmpty())
			{
				bSawActive = true;
				// B=0.4、k=0.5、α=1 なので 0.2。float の乗算丸めに 1e-6。
				bOk &= TestTrue(TEXT("有効中は毎回 0.2"), FMath::Abs(ActualDamping - 0.2f) <= 0.000001f);
			}
			else if (!bSawRestore)
			{
				bSawRestore = true;
				bOk &= TestTrue(TEXT("終了直後は一回だけベースへ戻す"), FMath::Abs(ActualDamping - 0.4f) <= 0.000001f);
			}
			else
			{
				bOk &= TestEqual(TEXT("その後は外からの値を保つ"), ActualDamping, 123.0f);
			}
		}
		bOk &= TestTrue(TEXT("有効なフレームがあった"), bSawActive);
		bOk &= TestTrue(TEXT("終了後の復元を見た"), bSawRestore);
		bOk &= TestEqual(TEXT("ベース設定は不変"), PhysicsFixture.Node.PhysicsSettings.Damping, 0.4f);
		return bOk;
	}
}

#endif
