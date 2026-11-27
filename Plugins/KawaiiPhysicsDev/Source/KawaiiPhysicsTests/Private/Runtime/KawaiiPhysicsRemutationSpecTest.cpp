// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsNodeFixture.h"
#include "Animation/MirrorDataTable.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Curve.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "UObject/UnrealType.h"
#include <new>

namespace KawaiiPhysicsRemutationSpecTestPrivate
{

KawaiiPhysicsNodeFixture::FOptions MakeRemutationOptions()
{
	KawaiiPhysicsNodeFixture::FOptions RemutationOptions;
	RemutationOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	RemutationOptions.ConfigureDeveloperSettings = [](UKawaiiPhysicsDeveloperSettings& DeveloperConfig)
	{
		DeveloperConfig.bUseFixedSubstepping = false;
	};
	RemutationOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& ConfiguredPhysics)
	{
		ConfiguredPhysics.RootBone.BoneName = TEXT("root");
		ConfiguredPhysics.DummyBoneLength = 0.0f;
		ConfiguredPhysics.WarmUpFrames = 0;
		ConfiguredPhysics.TargetFramerate = 60;
		ConfiguredPhysics.Gravity = FVector::ZeroVector;
		ConfiguredPhysics.bUseDefaultGravityZProjectSetting = false;
		ConfiguredPhysics.bEnableWind = false;
		ConfiguredPhysics.bAllowWorldCollision = false;
		ConfiguredPhysics.bUseSimpleWorldCollision = false;
		ConfiguredPhysics.PhysicsSettings.Stiffness = 0.0f;
		ConfiguredPhysics.PhysicsSettings.Damping = 1.0f;
	};
	return RemutationOptions;
}

#if WITH_EDITOR
// MR-01。5001: Box の旧 Mirror を消さないため、nullptr 直後の Mirror 件数が 0 でなく 1 になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemutNullMirrorBoxesTest,
	"KawaiiPhysics.CollisionSpec.RemutNullMirrorBoxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRemutNullMirrorBoxesTest::RunTest(const FString& Parameters)
{
	auto BoxOptions = MakeRemutationOptions();
	BoxOptions.Bones = {
		{TEXT("root"), INDEX_NONE, FTransform::Identity},
		{TEXT("chain_0"), 0, FTransform::Identity},
		{TEXT("chain_1"), 1, FTransform(FVector(12, 0, 0))},
		{TEXT("source_l"), 0, FTransform(FVector(0, -10, 0))},
		{TEXT("source_r"), 0, FTransform(FVector(0, 10, 0))}};
	const auto ConfigureBoxes = BoxOptions.ConfigureNode;
	BoxOptions.ConfigureNode = [ConfigureBoxes](FAnimNode_KawaiiPhysics& ConfiguredPhysics)
	{
		ConfigureBoxes(ConfiguredPhysics);
		ConfiguredPhysics.RootBone.BoneName = TEXT("chain_0");
		ConfiguredPhysics.PhysicsSettings.Stiffness = 1.0f;
		ConfiguredPhysics.PhysicsSettings.Radius = 0.0f;
		FBoxLimit ActiveBox;
		ActiveBox.DrivingBone.BoneName = TEXT("root");
		ActiveBox.OffsetLocation = FVector(12, -2, 0);
		ActiveBox.Extent = FVector(3, 3, 3);
		ActiveBox.bEnable = true;
		ConfiguredPhysics.BoxLimits.Add(ActiveBox);
		FBoxLimit SourceBox = ActiveBox;
		SourceBox.DrivingBone.BoneName = TEXT("source_l");
		SourceBox.OffsetLocation = FVector(1000, 0, 0);
		ConfiguredPhysics.BoxLimits.Add(SourceBox);
	};
	KawaiiPhysicsNodeFixture::FFixture UnsetBoxes(BoxOptions);
	KawaiiPhysicsNodeFixture::FFixture ClearedBoxes(BoxOptions);
	TStrongObjectPtr<UMirrorDataTable> BoxMirrorTable(NewObject<UMirrorDataTable>());
	BoxMirrorTable->RowStruct = FMirrorTableRow::StaticStruct();
	BoxMirrorTable->MirrorAxis = EAxis::X;
	FMirrorTableRow BoxMirrorRow;
	BoxMirrorRow.Name = TEXT("source_l");
	BoxMirrorRow.MirroredName = TEXT("source_r");
	BoxMirrorTable->AddRow(BoxMirrorRow.Name, BoxMirrorRow);
	ClearedBoxes.Node.MirrorDataTableForLimits = BoxMirrorTable.Get();
	ClearedBoxes.Accessor.ApplyMirrorLimits(ClearedBoxes.Proxy->GetRequiredBones());
	const auto CountMirrorBoxes = [](const TArray<FBoxLimit>& BoxEntries)
	{
		int32 MirrorCount = 0;
		for (const FBoxLimit& BoxEntry : BoxEntries)
		{
			if (BoxEntry.SourceType == ECollisionSourceType::Mirror) ++MirrorCount;
		}
		return MirrorCount;
	};
	if (!TestEqual(TEXT("対照: Box の Mirror が生成された"), CountMirrorBoxes(ClearedBoxes.Node.BoxLimitsData), 1)) return false;
	ClearedBoxes.Node.MirrorDataTableForLimits = nullptr;
	ClearedBoxes.Accessor.ApplyMirrorLimits(ClearedBoxes.Proxy->GetRequiredBones());
	// 件数とビット一致は離散的な要件なので、許容誤差を設けない。
	bool bPassed = TestEqual(TEXT("テーブルを外した直後の Box Mirror は零"), CountMirrorBoxes(ClearedBoxes.Node.BoxLimitsData), 0);
	bool bBoxMovedChain = false;
	for (int32 SampleIndex = 0; SampleIndex < 20; ++SampleIndex)
	{
		UnsetBoxes.EvaluateFrame();
		ClearedBoxes.EvaluateFrame();
		if (!TestEqual(TEXT("対照: 未設定セルの可動チェーン"), UnsetBoxes.Node.ModifyBones.Num(), 2) ||
			!TestEqual(TEXT("対照: 復元セルの可動チェーン"), ClearedBoxes.Node.ModifyBones.Num(), 2)) return false;
		for (int32 LinkIndex = 0; LinkIndex < 2; ++LinkIndex)
		{
			const FVector& UnsetLocation = UnsetBoxes.Node.ModifyBones[LinkIndex].Location;
			const FVector& ClearedLocation = ClearedBoxes.Node.ModifyBones[LinkIndex].Location;
			bPassed &= TestTrue(TEXT("nullptr 復元後は未設定セルとビット一致"),
				FMemory::Memcmp(&UnsetLocation, &ClearedLocation, sizeof(FVector)) == 0);
		}
		bBoxMovedChain |= FVector::Dist(UnsetBoxes.Node.ModifyBones[1].Location, FVector(12, 0, 0)) > 0.1;
	}
	bPassed &= TestTrue(TEXT("対照: 元の Box はチェーンを押し出す"), bBoxMovedChain);
	return bPassed;
}
#endif

// LA-22・SM-U3。5026: 長さカーブ経路で倍率零が制限なしへ反転し、正値と方向角 1e-3 度の主張が落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemutConeZeroMultiplierTest,
	"KawaiiPhysics.ControlSpec.RemutConeZeroMultiplierWithLengthCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRemutConeZeroMultiplierTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	for (const float AuthoredAngle : {0.0f, 30.0f})
	{
		auto ConeOptions = MakeRemutationOptions();
		const auto ConfigureCone = ConeOptions.ConfigureNode;
		ConeOptions.ConfigureNode = [ConfigureCone, AuthoredAngle](FAnimNode_KawaiiPhysics& ConfiguredPhysics)
		{
			ConfigureCone(ConfiguredPhysics);
			ConfiguredPhysics.Gravity = FVector(3600, 0, 0);
			ConfiguredPhysics.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::Cone;
			ConfiguredPhysics.PhysicsSettings.LimitAngle = AuthoredAngle;
			ConfiguredPhysics.LimitAngleCurveData.GetRichCurve()->AddKey(0.0f, 1.0f);
			ConfiguredPhysics.LimitAngleCurveData.GetRichCurve()->AddKey(1.0f, 1.0f);
		};
		KawaiiPhysicsNodeFixture::FFixture ConeFixture(ConeOptions);
		ConeFixture.EvaluateFrame();
		if (!TestEqual(TEXT("対照: Cone の二本のボーン"), ConeFixture.Node.ModifyBones.Num(), 2)) return false;
		bPassed &= TestTrue(TEXT("対照: 倍率を掛ける前は重力でポーズから離れる"),
			FVector::Dist(ConeFixture.Node.ModifyBones[1].Location, FVector(0, 0, -10)) > 0.1);
		FKawaiiPhysicsSettingsMultiplier ConeScale;
		ConeScale.LimitAngle = 0.0f;
		bPassed &= TestTrue(TEXT("対照: 零倍率の要求が届く"),
			ConeFixture.Node.RequestStartPhysicsSettingsMultiplier(ConeScale, 0.0f, 2.0f, 0.0f) > 0);
		for (int32 SampleIndex = 0; SampleIndex < 4; ++SampleIndex) ConeFixture.EvaluateFrame();
		bPassed &= TestEqual(TEXT("対照: 倍率が一件適用された"), ConeFixture.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
		const auto& ConeChild = ConeFixture.Node.ModifyBones[1];
		const FVector ActualDirection = (ConeChild.Location - ConeFixture.Node.ModifyBones[0].Location).GetSafeNormal();
		const double DirectionAngle = FMath::RadiansToDegrees(FMath::Atan2(
			FVector::CrossProduct(FVector(0, 0, -1), ActualDirection).Size(), -ActualDirection.Z));
		if (AuthoredAngle > 0.0f)
		{
			bPassed &= TestTrue(TEXT("制限ありの実効角度は正値を保つ"), ConeChild.PhysicsSettings.LimitAngle > 0.0f);
			// SM-U3 の仕様値 1e-3 度。具体的なクランプ値を期待値にはしない。
			bPassed &= TestTrue(TEXT("零倍率はほぼポーズへ追従する"), DirectionAngle <= 1.e-3);
		}
		else
		{
			bPassed &= TestEqual(TEXT("ベース零は制限なしのまま"), ConeChild.PhysicsSettings.LimitAngle, 0.0f);
			bPassed &= TestTrue(TEXT("対照: 制限なしでは重力による曲がりが残る"), DirectionAngle > 1.0);
		}
	}
	return bPassed;
}

// FX-19。5044: 根元合わせのオフセットを落とすと、動く入力ポーズに根元が一致せず第一リンクも伸びる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemutMovingWorldTeleportTest,
	"KawaiiPhysics.SolverSpec.RemutMovingWorldTeleport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRemutMovingWorldTeleportTest::RunTest(const FString& Parameters)
{
	auto TeleportOptions = MakeRemutationOptions();
	TeleportOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(4);
	TeleportOptions.FrameDt = 1.0f / 40.0f;
	TeleportOptions.SimulationSpace = EKawaiiPhysicsSimulationSpace::WorldSpace;
	TeleportOptions.ConfigureDeveloperSettings = [](UKawaiiPhysicsDeveloperSettings& DeveloperConfig)
	{
		DeveloperConfig.bUseFixedSubstepping = true;
		DeveloperConfig.MaxSubsteps = 4;
	};
	const auto ConfigureTeleport = TeleportOptions.ConfigureNode;
	TeleportOptions.ConfigureNode = [ConfigureTeleport](FAnimNode_KawaiiPhysics& ConfiguredPhysics)
	{
		ConfigureTeleport(ConfiguredPhysics);
		ConfiguredPhysics.TeleportDistanceThreshold = 50.0f;
		ConfiguredPhysics.TeleportRotationThreshold = 0.0f;
		ConfiguredPhysics.bUseWarmUpWhenResetDynamics = false;
	};
	TeleportOptions.GetComponentTransform = [](const int32 SampleIndex)
	{
		return FTransform(FVector(3.0 * SampleIndex + (SampleIndex >= 10 ? 200.0 : 0.0), 0, 0));
	};
	TeleportOptions.SetInputPose = [](FCompactPose& InputPose, const int32 SampleIndex)
	{
		InputPose[FCompactPoseBoneIndex(0)].SetTranslation(FVector(0, 2.0 * SampleIndex, 0));
	};
	KawaiiPhysicsNodeFixture::FFixture TeleportFixture(TeleportOptions);
	for (int32 SampleIndex = 0; SampleIndex < 10; ++SampleIndex) TeleportFixture.EvaluateFrame();
	if (!TestEqual(TEXT("対照: WorldSpace の四本のボーン"), TeleportFixture.Node.ModifyBones.Num(), 4)) return false;
	bool bPassed = TestTrue(TEXT("対照: テレポート前から実際に移動している"),
		TeleportFixture.Node.ModifyBones[0].Location.Size() > 1.0);
	const FVector BeforeTeleportRoot = TeleportFixture.Node.ModifyBones[0].Location;
	TeleportFixture.EvaluateFrame();
	bPassed &= TestTrue(TEXT("対照: 大きな Component 移動が評価に届く"),
		FVector::Dist(BeforeTeleportRoot, TeleportFixture.Node.ModifyBones[0].PoseLocation) > 100.0);
	// 同じ物理をポーズ根元へ運んだ位置の比較なので FX-U4 (a) の 0.01cm を許す。
	bPassed &= TestTrue(TEXT("テレポート直後の根元はポーズに一致"),
		TeleportFixture.Node.ModifyBones[0].Location.Equals(FVector(230, 20, 0), 0.01));
	for (int32 LinkIndex = 1; LinkIndex < 4; ++LinkIndex)
	{
		const FVector ParentOutput = LinkIndex == 1 ? FVector(230, 20, 0) : TeleportFixture.Node.ModifyBones[LinkIndex - 1].OutputLocation;
		// 参照リンク長 10cm。位置誤差 0.01cm は比にすると 0.001。
		bPassed &= TestTrue(TEXT("全リンク長の比は一"),
			FMath::Abs(FVector::Dist(ParentOutput, TeleportFixture.Node.ModifyBones[LinkIndex].OutputLocation) / 10.0 - 1.0) <= 0.001);
	}
	return bPassed;
}

// FX-22・FX-U5。5048: 補間出力の押し戻しを省くと、対照と同じ z=1 が残り、z=2 の平面の裏側になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemutOvertakenPlanarOutputTest,
	"KawaiiPhysics.SolverSpec.RemutOvertakenPlanarOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRemutOvertakenPlanarOutputTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	double UncorrectedHeight = 0.0;
	for (const bool bEnablePlane : {false, true})
	{
		FKawaiiPhysicsTestAccessor PlanarAccessor;
		PlanarAccessor.BuildVerticalChain(2, 10.0f, FVector(0, 0, 1), FVector(1, 0, 0));
		PlanarAccessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::WorldSpace);
		PlanarAccessor.SetFixedSubstepping(true, 60);
		PlanarAccessor.SetSubstepPoseInitialized(true);
		PlanarAccessor.Bone(0).bSkipSimulate = true;
		PlanarAccessor.Bone(1).bSkipSimulate = false;
		PlanarAccessor.Bone(1).PhysicsSettings.Radius = 1.0f;
		// 前ステップの平面 z=0、今の平面 z=2。ソルバー中心は z=3、補間履歴は z=1。
		PlanarAccessor.Bone(1).Location = FVector(FMath::Sqrt(96.0), 0, 3);
		PlanarAccessor.Bone(1).PrevStepPoseOffset = FVector::ZeroVector;
		PlanarAccessor.Bone(1).LastStepPoseOffset = FVector(FMath::Sqrt(96.0) - 10.0, 0, 2);
		FPlanarLimit MovingPlane;
		MovingPlane.bEnable = bEnablePlane;
		MovingPlane.bHasPreviousStepPlane = true;
		MovingPlane.PreviousStepPlane = FPlane(FVector::ZeroVector, FVector::UpVector);
		MovingPlane.Plane = FPlane(FVector(0, 0, 2), FVector::UpVector);
		MovingPlane.CachedNormal = FVector::UpVector;
		PlanarAccessor.Node.PlanarLimits.Add(MovingPlane);
		bPassed &= TestEqual(TEXT("対照: 出力計算のボーンが二本"), PlanarAccessor.Num(), 2);
		bPassed &= TestTrue(TEXT("対照: 前の平面にも今の平面にもソルバー中心は表側"),
			PlanarAccessor.Bone(1).Location.Z > 2.0);
		// アクセサは本番の出力計算をそのまま呼ぶ。期待値は平面 z=2 に対する中心の符号だけ。
		PlanarAccessor.CallUpdateOutputLocations();
		if (!bEnablePlane)
		{
			UncorrectedHeight = PlanarAccessor.Bone(1).OutputLocation.Z;
			bPassed &= TestTrue(TEXT("対照: 補正なしの補間出力は平面に追い越される"), UncorrectedHeight < 1.5);
		}
		else
		{
			bPassed &= TestTrue(TEXT("対照: コリジョンが補間出力を動かす"),
				PlanarAccessor.Bone(1).OutputLocation.Z - UncorrectedHeight > 0.1);
			// FX-U5 の中心距離の許容値 0.01cm。半径分の押し出し量は要件にしない。
			bPassed &= TestTrue(TEXT("移動平面を越えた補間出力の中心は表側"), PlanarAccessor.Bone(1).OutputLocation.Z - 2.0 >= -0.01);
		}
	}
	return bPassed;
}

// GR-4 はプロセスで一回なので、個別テスト開始より前の警告も観測する。
// テストモジュール読込時から登録し、既存の Gravity テストとの実行順に依存しない。
struct FProcessLegacyWarningCapture : FOutputDevice
{
	FThreadSafeCounter LegacyWarningCount;
	bool bAttached = false;

	FProcessLegacyWarningCapture()
	{
		if (GLog)
		{
			GLog->AddOutputDevice(this);
			bAttached = true;
		}
	}
	~FProcessLegacyWarningCapture() override
	{
		if (bAttached && GLog) GLog->RemoveOutputDevice(this);
	}
	bool CanBeUsedOnAnyThread() const override { return true; }
	bool CanBeUsedOnMultipleThreads() const override { return true; }
	void Serialize(const TCHAR* LogMessage, ELogVerbosity::Type MessageVerbosity, const FName& LogCategory) override
	{
		if (LogCategory == FName(TEXT("LogKawaiiPhysics")) &&
			(MessageVerbosity & ELogVerbosity::VerbosityMask) == ELogVerbosity::Warning &&
			FCString::Strstr(LogMessage, TEXT("LegacyCharacterGravityScaleSign is enabled")))
		{
			LegacyWarningCount.Increment();
		}
	}
	int32 ReadCount() { GLog->Flush(); return LegacyWarningCount.GetValue(); }
};

FProcessLegacyWarningCapture ProcessLegacyWarnings;

// TW-GR-04・GR-4。5072: 初回警告まで抑制するため、互換重力が作用してもプロセスの警告数が 0 となる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemutLegacyGravityWarningTest,
	"KawaiiPhysics.WindSpec.RemutLegacyGravityWarningExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRemutLegacyGravityWarningTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("対照: テストモジュール読込時から警告を観測"), ProcessLegacyWarnings.bAttached)) return false;
	KawaiiPhysicsNodeFixture::FScopedCVar GravitySignScope(
		TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), TEXT("0"));
	if (!TestNotNull(TEXT("互換 CVar が存在"), GravitySignScope.Variable)) return false;
	auto GravityOptions = MakeRemutationOptions();
	GravityOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2, FVector(10, 0, 0));
	GravityOptions.ActorKind = KawaiiPhysicsNodeFixture::EActorKind::Character;
	GravityOptions.SimulationSpace = EKawaiiPhysicsSimulationSpace::WorldSpace;
	KawaiiPhysicsNodeFixture::FFixture GravityFixture(GravityOptions);
	GravityFixture.EvaluateFrame();
	if (!TestEqual(TEXT("対照: Gravity のボーンを構築"), GravityFixture.Node.ModifyBones.Num(), 2)) return false;
	auto& CharacterGravity = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Gravity>(GravityFixture.Node);
	CharacterGravity.bUseCharacterGravityScale = true;
	CharacterGravity.RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);
	GravityFixture.EvaluateFrame();
	const FVector ModernGravity = FKawaiiPhysicsTestAccessor::GetExternalForceVector(CharacterGravity);
	bool bPassed = TestTrue(TEXT("対照: Character の重力は下向きに作用する"), ModernGravity.Z < -100.0);
	bPassed &= TestTrue(TEXT("対照: 横向きのチェーンが重力で参照ポーズから動く"),
		FVector::Dist(GravityFixture.Node.ModifyBones[1].Location, FVector(10, 0, 0)) > 0.1);
	GravitySignScope.Variable->Set(1, ECVF_SetByConsole);
	for (int32 SampleIndex = 0; SampleIndex < 3; ++SampleIndex)
	{
		GravityFixture.EvaluateFrame();
		// 同じ Character の符号反転だけなので、float の乗算丸めに 1e-4 を許す。
		bPassed &= TestTrue(TEXT("互換重力は新重力の反対向き"),
			FKawaiiPhysicsTestAccessor::GetExternalForceVector(CharacterGravity).Equals(-ModernGravity, 1.e-4));
		bPassed &= TestEqual(TEXT("非推奨警告はプロセスで厳密に一回"), ProcessLegacyWarnings.ReadCount(), 1);
	}
	return bPassed;
}

// TW-RI-05。5083: Z だけ長いカーブの終端を取り損ない、最大時刻 3 秒と 3.5 秒後の力 (10,0,5) が落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemutRuntimeLongestZCurveTest,
	"KawaiiPhysics.WindSpec.RemutRuntimeLongestZCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRemutRuntimeLongestZCurveTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture CurveFixture(MakeRemutationOptions());
	CurveFixture.EvaluateFrame();
	if (!TestEqual(TEXT("対照: 追加前にノードを初期化済み"), CurveFixture.Node.ModifyBones.Num(), 2)) return false;
	auto& LongZCurve = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Curve>(CurveFixture.Node);
	LongZCurve.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
	LongZCurve.CurveEvaluateType = EExternalForceCurveEvaluateType::Single;
	LongZCurve.RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		FRichCurve* AxisCurve = LongZCurve.ForceCurve.GetRichCurve(AxisIndex);
		const FKeyHandle StartKey = AxisCurve->AddKey(0.0f, AxisIndex == 0 ? 10.0f : 0.0f);
		const FKeyHandle EndKey = AxisCurve->AddKey(AxisIndex + 1.0f, AxisIndex == 0 ? 10.0f : (AxisIndex == 2 ? 30.0f : 0.0f));
		AxisCurve->SetKeyInterpMode(StartKey, RCIM_Linear);
		AxisCurve->SetKeyInterpMode(EndKey, RCIM_Linear);
	}
	const FFloatProperty* MaximumTimeProperty = FindFProperty<FFloatProperty>(FKawaiiPhysics_ExternalForce_Curve::StaticStruct(), TEXT("MaxCurveTime"));
	if (!TestNotNull(TEXT("最大時刻の反射プロパティ"), MaximumTimeProperty)) return false;
	FKawaiiPhysics_ExternalForce_Curve InitializeOnlyCurve;
	InitializeOnlyCurve.ForceCurve = LongZCurve.ForceCurve;
	InitializeOnlyCurve.Initialize(FAnimationInitializeContext(CurveFixture.Proxy.Get()));
	// PreApply のエディタ補完より前にも検査し、パッケージ版と同じ Initialize 経路を守る。
	bool bPassed = TestEqual(TEXT("Initialize だけで最長の Z 終端 3 秒を採用"),
		MaximumTimeProperty->GetPropertyValue_InContainer(&InitializeOnlyCurve), 3.0f);
	bPassed &= TestFalse(TEXT("対照: 実行時追加の Curve は未初期化"), LongZCurve.IsRuntimeInitialized());
	for (int32 SampleIndex = 0; SampleIndex < 210; ++SampleIndex) CurveFixture.EvaluateFrame();
	bPassed &= TestTrue(TEXT("対照: ノードが追加 Curve を初期化した"), LongZCurve.IsRuntimeInitialized());
	bPassed &= TestTrue(TEXT("対照: 外力でチェーンが参照ポーズから動く"),
		FVector::Dist(CurveFixture.Node.ModifyBones[1].Location, FVector(0, 0, -10)) > 0.1);
	bPassed &= TestEqual(TEXT("実行時の最大時刻も 3 秒"), MaximumTimeProperty->GetPropertyValue_InContainer(&LongZCurve), 3.0f);
	// 210/60=3.5 秒は折り返すと 0.5 秒。Z は 3 秒で 0→30 なので 5、X は常に 10。
	// 210 回の float 加算と折り返しの丸めに、力の 0.01 を許す。
	bPassed &= TestTrue(TEXT("Z の最長終端で折り返した力"),
		FKawaiiPhysicsTestAccessor::GetExternalForceVector(LongZCurve).Equals(FVector(10, 0, 5), 0.01));
	return bPassed;
}

FName RequestedFilterBone;
TArray<FName>* AppliedFilterBones = nullptr;

struct FAfterSuperFilterForce : FKawaiiPhysics_ExternalForce
{
	void PreApply(FAnimNode_KawaiiPhysics& FilterPhysics, FComponentSpacePoseContext& FilterPose) override
	{
		FKawaiiPhysics_ExternalForce::PreApply(FilterPhysics, FilterPose);
		ApplyBoneFilter[0].BoneName = RequestedFilterBone;
	}
	void Apply(FKawaiiPhysicsModifyBone& FilterBone, FAnimNode_KawaiiPhysics& FilterPhysics,
		FComponentSpacePoseContext& FilterPose, const FTransform& FilterTransform) override
	{
		if (CanApply(FilterBone))
		{
			AppliedFilterBones->Add(FilterBone.BoneRef.BoneName);
			FilterBone.Location.X += 1.0;
		}
	}
};

// TW-RI-14。5087: Super 後の同数フィルタ変更を再取得しないため、直後の Apply 集合が一フレーム古くなる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemutFilterAfterSuperTest,
	"KawaiiPhysics.WindSpec.RemutFilterAfterSuperSameFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRemutFilterAfterSuperTest::RunTest(const FString& Parameters)
{
	auto FilterOptions = MakeRemutationOptions();
	FilterOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(3);
	KawaiiPhysicsNodeFixture::FFixture FilterFixture(FilterOptions);
	FilterFixture.EvaluateFrame();
	if (!TestEqual(TEXT("対照: フィルタ対象の実ボーンが三本"), FilterFixture.Node.ModifyBones.Num(), 3)) return false;
	TArray<FName> AppliedNames;
	TGuardValue<TArray<FName>*> AppliedNamesScope(AppliedFilterBones, &AppliedNames);
	TGuardValue<FName> RequestedNameScope(RequestedFilterBone, FName(TEXT("chain_1")));
	FilterFixture.Node.ExternalForces.Reserve(1);
	FilterFixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
	// 既存のサブステップテストと同じく、状態を増やさない派生型を配置し、その後は配列をコピーしない。
	static_assert(sizeof(FAfterSuperFilterForce) == sizeof(FKawaiiPhysics_ExternalForce));
	static_assert(alignof(FAfterSuperFilterForce) == alignof(FKawaiiPhysics_ExternalForce));
	auto* FilterStorage = FilterFixture.Node.ExternalForces[0].GetMutablePtr<FKawaiiPhysics_ExternalForce>();
	FilterStorage->~FKawaiiPhysics_ExternalForce();
	new (FilterStorage) FAfterSuperFilterForce();
	FilterStorage->ApplyBoneFilter.Add(FBoneReference(TEXT("chain_1")));
	FilterFixture.EvaluateFrame();
	if (!TestEqual(TEXT("対照: 最初の Apply が一回"), AppliedNames.Num(), 1)) return false;
	bool bPassed = TestTrue(TEXT("対照: 最初は chain_1 に力が掛かる"), AppliedNames[0] == FName(TEXT("chain_1")));
	bPassed &= TestTrue(TEXT("対照: 力を受けた位置が変わる"), FilterFixture.Node.ModifyBones[1].Location.X > 0.1);
	for (const FName NextFilterName : {FName(TEXT("chain_2")), FName(TEXT("chain_1"))})
	{
		RequestedFilterBone = NextFilterName;
		AppliedNames.Reset();
		FilterFixture.EvaluateFrame();
		if (!TestEqual(TEXT("書き換え直後の Apply は一回"), AppliedNames.Num(), 1)) return false;
		// 集合の要件なので名前を厳密比較し、位置の丸めには依存しない。
		bPassed &= TestTrue(TEXT("同じフレームから新しいフィルタのボーンだけに作用"), AppliedNames[0] == NextFilterName);
	}
	return bPassed;
}

}

#endif
