// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "Animation/MirrorDataTable.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace KawaiiPhysicsControlSpecSetupTestPrivate
{
	KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsControlSpecCopy, "KawaiiPhysics.Test.ControlSpecCopy");

	KawaiiPhysicsNodeFixture::FOptions MakeChainOptions(const float TimeScale = 1.0f)
	{
		KawaiiPhysicsNodeFixture::FOptions ChainOptions;
		ChainOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(4);
		ChainOptions.bPreUpdateEveryFrame = true;
		ChainOptions.GetComponentTransform = [](int32 Frame)
		{
			return FTransform(FVector(0.0, FMath::Sin(Frame * 0.18) * 8.0, 0.0));
		};
		ChainOptions.ConfigureNode = [TimeScale](FAnimNode_KawaiiPhysics& ConfiguredNode)
		{
			ConfiguredNode.RootBone.BoneName = TEXT("root");
			ConfiguredNode.bNeedWarmUp = false;
			ConfiguredNode.TimeScale = TimeScale;
			ConfiguredNode.PhysicsSettings.Damping = 0.34f;
			ConfiguredNode.PhysicsSettings.Stiffness = 0.21f;
			ConfiguredNode.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
			ConfiguredNode.PhysicsSettings.PerAxisLimitAngle.X.Min = -75.0f;
			ConfiguredNode.PhysicsSettings.PerAxisLimitAngle.Y.Max = 80.0f;
			// 実行時に直接作ったカーブでも、Transient の KeyHandle は設定の比較に影響しない。
			ConfiguredNode.DampingCurveData.GetRichCurve()->AddKey(0.0f, 0.25f);
			ConfiguredNode.DampingCurveData.GetRichCurve()->AddKey(1.0f, 0.75f);
			ConfiguredNode.StiffnessCurveData.GetRichCurve()->AddKey(0.0f, 0.4f);
			ConfiguredNode.StiffnessCurveData.GetRichCurve()->AddKey(1.0f, 0.9f);
		};
		return ChainOptions;
	}

	// エディタで配列に要素を足したときの PostEditChangeChainProperty（ArrayAdd）と同じく、
	// DataAsset の形状に SourceType = DataAsset を付ける。付けないとノードは DataAsset 由来と判定できない。
	void MarkLimitsAsDataAssetSource(UKawaiiPhysicsLimitsDataAsset& LimitAsset)
	{
		auto MarkLimits = [](auto& Limits)
		{
			for (auto& Limit : Limits)
			{
				Limit.SourceType = ECollisionSourceType::DataAsset;
			}
		};
		MarkLimits(LimitAsset.SphericalLimits);
		MarkLimits(LimitAsset.CapsuleLimits);
		MarkLimits(LimitAsset.TaperedCapsuleLimits);
		MarkLimits(LimitAsset.BoxLimits);
		MarkLimits(LimitAsset.PlanarLimits);
	}

	// FRuntimeFloatCurve の設定が同じか。FIndexedCurve の KeyHandlesToIndices は Transient で、
	// 値のコピーでは写らない（FKeyHandleMap の代入は何もしない）ので、キー・既定値・外挿・外部カーブで比べる。
	bool AreCurveSettingsEqual(const FRuntimeFloatCurve& LeftCurve, const FRuntimeFloatCurve& RightCurve)
	{
		const FRichCurve& LeftRichCurve = LeftCurve.EditorCurveData;
		const FRichCurve& RightRichCurve = RightCurve.EditorCurveData;
		return LeftCurve.ExternalCurve == RightCurve.ExternalCurve &&
			LeftRichCurve.DefaultValue == RightRichCurve.DefaultValue &&
			LeftRichCurve.PreInfinityExtrap == RightRichCurve.PreInfinityExtrap &&
			LeftRichCurve.PostInfinityExtrap == RightRichCurve.PostInfinityExtrap &&
			LeftRichCurve.GetConstRefOfKeys() == RightRichCurve.GetConstRefOfKeys();
	}

	bool AreSettingsEqual(const FKawaiiPhysicsSettings& LeftSettings, const FKawaiiPhysicsSettings& RightSettings)
	{
		return LeftSettings.Damping == RightSettings.Damping &&
			LeftSettings.Stiffness == RightSettings.Stiffness &&
			LeftSettings.WorldDampingLocation == RightSettings.WorldDampingLocation &&
			LeftSettings.WorldDampingRotation == RightSettings.WorldDampingRotation &&
			LeftSettings.Radius == RightSettings.Radius &&
			LeftSettings.LimitAngle == RightSettings.LimitAngle &&
			LeftSettings.LimitAngleMode == RightSettings.LimitAngleMode &&
			LeftSettings.PerAxisLimitAngle.X.Min == RightSettings.PerAxisLimitAngle.X.Min &&
			LeftSettings.PerAxisLimitAngle.X.Max == RightSettings.PerAxisLimitAngle.X.Max &&
			LeftSettings.PerAxisLimitAngle.Y.Min == RightSettings.PerAxisLimitAngle.Y.Min &&
			LeftSettings.PerAxisLimitAngle.Y.Max == RightSettings.PerAxisLimitAngle.Y.Max &&
			LeftSettings.PerAxisLimitAngle.Z.Min == RightSettings.PerAxisLimitAngle.Z.Min &&
			LeftSettings.PerAxisLimitAngle.Z.Max == RightSettings.PerAxisLimitAngle.Z.Max;
	}

	bool AreBonesEqual(const FAnimNode_KawaiiPhysics& LeftNode, const FAnimNode_KawaiiPhysics& RightNode)
	{
		if (LeftNode.ModifyBones.Num() == 0 || LeftNode.ModifyBones.Num() != RightNode.ModifyBones.Num())
		{
			return false;
		}
		for (int32 BoneIndex = 0; BoneIndex < LeftNode.ModifyBones.Num(); ++BoneIndex)
		{
			if (!AreSettingsEqual(LeftNode.ModifyBones[BoneIndex].PhysicsSettings,
				RightNode.ModifyBones[BoneIndex].PhysicsSettings) ||
				LeftNode.ModifyBones[BoneIndex].Location != RightNode.ModifyBones[BoneIndex].Location)
			{
				return false;
			}
		}
		return true;
	}

	bool AreBoneSettingsEqual(const FAnimNode_KawaiiPhysics& LeftNode, const FAnimNode_KawaiiPhysics& RightNode)
	{
		if (LeftNode.ModifyBones.Num() == 0 || LeftNode.ModifyBones.Num() != RightNode.ModifyBones.Num())
		{
			return false;
		}
		for (int32 BoneIndex = 0; BoneIndex < LeftNode.ModifyBones.Num(); ++BoneIndex)
		{
			if (!AreSettingsEqual(LeftNode.ModifyBones[BoneIndex].PhysicsSettings,
				RightNode.ModifyBones[BoneIndex].PhysicsSettings))
			{
				return false;
			}
		}
		return true;
	}

	FKawaiiPhysicsSettingsMultiplier MakeNontrivialScale()
	{
		FKawaiiPhysicsSettingsMultiplier MultiplierScale;
		MultiplierScale.Damping = 0.5f;
		MultiplierScale.Stiffness = 0.4f;
		MultiplierScale.WorldDampingLocation = 0.75f;
		MultiplierScale.WorldDampingRotation = 0.6f;
		MultiplierScale.Radius = 1.5f;
		MultiplierScale.LimitAngle = 0.5f;
		return MultiplierScale;
	}

	// LA-22・SM-U3・LA-21。mutant なし。正の 30 度は倍率 0 でも正に残り、元の 0 度は無制限のまま。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSetupConeZeroTest,
		"KawaiiPhysics.ControlSpec.Setup.ConeZero", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecSetupConeZeroTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const float BaseAngle : {30.0f, 0.0f})
		{
			FKawaiiPhysicsTestAccessor AngleAccessor;
			AngleAccessor.BuildVerticalChain(2, 10.0f);
			AngleAccessor.Node.PhysicsSettings.LimitAngle = BaseAngle;
			FKawaiiPhysicsSettingsMultiplier ZeroAngleScale;
			ZeroAngleScale.LimitAngle = 0.0f;
			AngleAccessor.Node.RequestPushPhysicsSettingsMultiplier(ZeroAngleScale, 1.0f, 101);
			AngleAccessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
			AngleAccessor.CallUpdatePhysicsSettings();
			FKawaiiPhysicsModifyBone ParentBone;
			ParentBone.Location = FVector::ZeroVector;
			ParentBone.PoseLocation = FVector::ZeroVector;
			ParentBone.PoseRotation = FQuat::Identity;
			FKawaiiPhysicsModifyBone ChildBone;
			ChildBone.PoseLocation = FVector(10.0, 0.0, 0.0);
			ChildBone.Location = FVector(0.0, 10.0, 0.0);
			ChildBone.PhysicsSettings = AngleAccessor.Bone(1).PhysicsSettings;
			bOk &= TestEqual(TEXT("倍率の要求が届く"), AngleAccessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
			if (BaseAngle > 0.0f)
			{
				bOk &= TestTrue(TEXT("正の Cone 制限は正に残る"), ChildBone.PhysicsSettings.LimitAngle > 0.0f);
				AngleAccessor.CallAngleLimit(ChildBone, ParentBone);
				// +X と制限後方向の角度。浮動小数の角度演算に 1e-3 度を許す。
				bOk &= TestTrue(TEXT("ポーズ方向へほぼ完全に戻る"),
					FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(ChildBone.Location.GetSafeNormal().X, -1.0, 1.0))) <= 0.001);
			}
			else
			{
				bOk &= TestEqual(TEXT("元のゼロはゼロ"), ChildBone.PhysicsSettings.LimitAngle, 0.0f);
				AngleAccessor.CallAngleLimit(ChildBone, ParentBone);
				bOk &= TestTrue(TEXT("無制限では +Y のまま"), ChildBone.Location == FVector(0.0, 10.0, 0.0));
			}
		}
		return bOk;
	}

	// LA-20・LA-21・LA-U1。mutant なし。±180 度へ 0.5 を掛けると ±90 度、0 なら全軸をロックする。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSetupAxisMultiplierTest,
		"KawaiiPhysics.ControlSpec.Setup.AxisMultiplier", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecSetupAxisMultiplierTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const float AngleScale : {0.5f, 0.0f})
		{
			FKawaiiPhysicsTestAccessor AxisAccessor;
			AxisAccessor.BuildVerticalChain(2, 10.0f);
			AxisAccessor.Node.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
			for (FKawaiiPhysicsAxisAngleRange* AxisRange : {
				&AxisAccessor.Node.PhysicsSettings.PerAxisLimitAngle.X,
				&AxisAccessor.Node.PhysicsSettings.PerAxisLimitAngle.Y,
				&AxisAccessor.Node.PhysicsSettings.PerAxisLimitAngle.Z})
			{
				AxisRange->Min = -180.0f;
				AxisRange->Max = 180.0f;
			}
			FKawaiiPhysicsSettingsMultiplier MultiplierScale;
			MultiplierScale.LimitAngle = AngleScale;
			AxisAccessor.Node.RequestPushPhysicsSettingsMultiplier(MultiplierScale, 1.0f, 102);
			AxisAccessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
			AxisAccessor.CallUpdatePhysicsSettings();
			const float ExpectedMagnitude = AngleScale == 0.5f ? 90.0f : 0.0f;
			for (const FKawaiiPhysicsAxisAngleRange* AxisRange : {
				&AxisAccessor.Bone(1).PhysicsSettings.PerAxisLimitAngle.X,
				&AxisAccessor.Bone(1).PhysicsSettings.PerAxisLimitAngle.Y,
				&AxisAccessor.Bone(1).PhysicsSettings.PerAxisLimitAngle.Z})
			{
				bOk &= TestEqual(TEXT("負側の範囲"), AxisRange->Min, -ExpectedMagnitude);
				bOk &= TestEqual(TEXT("正側の範囲"), AxisRange->Max, ExpectedMagnitude);
			}
			bOk &= TestEqual(TEXT("倍率は一件"), AxisAccessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
			if (AngleScale == 0.0f)
			{
				FKawaiiPhysicsModifyBone ParentBone;
				ParentBone.PoseRotation = FQuat::Identity;
				FKawaiiPhysicsModifyBone ChildBone;
				ChildBone.PoseLocation = FVector(10.0, 0.0, 0.0);
				ChildBone.Location = FVector(0.0, 10.0, 0.0);
				ChildBone.PhysicsSettings = AxisAccessor.Bone(1).PhysicsSettings;
				AxisAccessor.CallAngleLimit(ChildBone, ParentBone);
				// +X への角度。浮動小数の方向正規化に 1e-3 度を許す。
				bOk &= TestTrue(TEXT("全軸ロックはポーズ方向"),
					FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(ChildBone.Location.GetSafeNormal().X, -1.0, 1.0))) <= 0.001);
			}
		}
		return bOk;
	}

	// SM-01。mutant なし。倍率 1 は α=0・0.37・1 のどれでも位置とボーン別設定を変えない。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSetupUnityScaleTest,
		"KawaiiPhysics.ControlSpec.Setup.UnityScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecSetupUnityScaleTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const float Weight : {0.0f, 0.37f, 1.0f})
		{
			const KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeChainOptions();
			KawaiiPhysicsNodeFixture::FFixture ScaledFixture(ChainOptions);
			KawaiiPhysicsNodeFixture::FFixture ReferenceFixture(ChainOptions);
			for (int32 Frame = 0; Frame < 30; ++Frame)
			{
				if (Frame == 1)
				{
					ScaledFixture.Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 1.0f, 0.0f, 201);
				}
				if (Frame >= 12)
				{
					ScaledFixture.Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), Weight, 202);
				}
				ScaledFixture.EvaluateFrame();
				ReferenceFixture.EvaluateFrame();
				bOk &= TestTrue(TEXT("倍率 1 の全ボーン設定と位置はビット一致"), AreBonesEqual(ScaledFixture.Node, ReferenceFixture.Node));
			}
			bOk &= TestTrue(TEXT("倍率は本当に届いた"), ScaledFixture.Node.TransientForceStore.SettingsMultiplierItems.Num() > 0);
		}
		return bOk;
	}

	// SM-06・SM-07。mutant なし。Start はベース設定を書き換えず、満了後の全ボーンは対照へ戻る。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSetupRestoreTest,
		"KawaiiPhysics.ControlSpec.Setup.Restore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecSetupRestoreTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (int32 EndingPath = 0; EndingPath < 3; ++EndingPath)
		{
			const KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeChainOptions();
			KawaiiPhysicsNodeFixture::FFixture ScaledFixture(ChainOptions);
			KawaiiPhysicsNodeFixture::FFixture ReferenceFixture(ChainOptions);
			ScaledFixture.EvaluateFrame();
			ReferenceFixture.EvaluateFrame();
			const FKawaiiPhysicsSettings OriginalSettings = ScaledFixture.Node.PhysicsSettings;
			const FKawaiiPhysicsSettingsMultiplier MultiplierScale = MakeNontrivialScale();
			if (EndingPath == 0)
			{
				ScaledFixture.Node.RequestStartPhysicsSettingsMultiplier(MultiplierScale, 0.0f, 0.1f, 0.0f, 301);
			}
			else if (EndingPath == 1)
			{
				ScaledFixture.Node.RequestPushPhysicsSettingsMultiplier(MultiplierScale, 1.0f, 302);
			}
			else
			{
				ScaledFixture.Node.RequestPushPhysicsSettingsMultiplier(MultiplierScale, 1.0f, 303, 2, 0.3f);
			}
			bool bSawActiveDifference = false;
			for (int32 Frame = 0; Frame < 50; ++Frame)
			{
				if (EndingPath == 1 && Frame == 3)
				{
					ScaledFixture.Node.RequestStopPhysicsSettingsMultiplier(302, 0.3f);
				}
				ScaledFixture.EvaluateFrame();
				ReferenceFixture.EvaluateFrame();
				bOk &= TestTrue(TEXT("Start 中も終了後もノードのベース設定は不変"),
					AreSettingsEqual(ScaledFixture.Node.PhysicsSettings, OriginalSettings));
				if (Frame == 0 && ScaledFixture.Node.ModifyBones.Num() > 1)
				{
					bSawActiveDifference = !AreSettingsEqual(ScaledFixture.Node.ModifyBones[1].PhysicsSettings,
						ReferenceFixture.Node.ModifyBones[1].PhysicsSettings);
				}
				if (Frame == 49)
				{
					bOk &= TestEqual(TEXT("終了した倍率の件数"), ScaledFixture.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
					bOk &= TestTrue(TEXT("終了の次の評価で全ボーンの設定が復元"), AreBoneSettingsEqual(ScaledFixture.Node, ReferenceFixture.Node));
				}
			}
			bOk &= TestTrue(TEXT("有効な倍率で設定が対照から変わった"), bSawActiveDifference);
		}
		return bOk;
	}

	// PR-03・PR-09・PR-11・FX-34・PR-U4。構成だけが変われば再構築、物理設定だけなら再構築しない。
	// 修正前は直接 AddKey した二つのカーブも変更と数え、同じプリセットの変更数 0・要求なし・MatchesNode と差分 0、
	// 単独変更の件数 1 が落ちる。比較は件数と真偽値の完全一致。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSetupPresetTopologyTest,
		"KawaiiPhysics.ControlSpec.Setup.PresetTopology", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecSetupPresetTopologyTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (int32 ChangeKind = 0; ChangeKind < 4; ++ChangeKind)
		{
			KawaiiPhysicsNodeFixture::FFixture TargetFixture(MakeChainOptions());
			TargetFixture.EvaluateFrame();
			bOk &= TestTrue(TEXT("構成比較の対照にボーンがある"), TargetFixture.Node.ModifyBones.Num() > 0);
			TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset> PresetAsset(NewObject<UKawaiiPhysicsPresetDataAsset>());
			PresetAsset->CopyFromNode(TargetFixture.Node);
			FKawaiiPhysicsPresetApplyOptions ApplyOptions;
			ApplyOptions.bApplyBoneAssignment = true;
			bOk &= TestEqual(TEXT("同じプリセットの再適用は変更なし"),
				UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(TargetFixture.Node, *PresetAsset, ApplyOptions), 0);
			bOk &= TestFalse(TEXT("同じプリセットでは要求なし"),
				FKawaiiPhysicsTestAccessor::HasPendingRuntimeApplyRequest(TargetFixture.Node));
			TArray<FName> DiffProperties;
			bOk &= TestTrue(TEXT("同じプリセットは MatchesNode で一致"),
				PresetAsset->MatchesNode(TargetFixture.Node, ApplyOptions, DiffProperties));
			bOk &= TestEqual(TEXT("同じプリセットの差分は空"), DiffProperties.Num(), 0);
			if (ChangeKind == 0) { PresetAsset->Node.RootBone.BoneName = TEXT("chain_1"); }
			if (ChangeKind == 1) { PresetAsset->Node.ExcludeBones.Add(FBoneReference(TEXT("chain_3"))); }
			if (ChangeKind == 2) { ++PresetAsset->Node.BoneSubdivisionCount; }
			if (ChangeKind == 3) { PresetAsset->Node.PhysicsSettings.Damping = 0.75f; }
			bOk &= TestEqual(TEXT("単独の変更が適用される"),
				UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(TargetFixture.Node, *PresetAsset, ApplyOptions), 1);
			bOk &= TestEqual(TEXT("構成変更の場合だけ再構築する"),
				TargetFixture.Accessor.IsModifyBonesReinitPending(), ChangeKind < 3);
			bOk &= TestTrue(TEXT("変更適用後は MatchesNode で一致"),
				PresetAsset->MatchesNode(TargetFixture.Node, ApplyOptions, DiffProperties));
			bOk &= TestEqual(TEXT("変更適用後の差分は空"), DiffProperties.Num(), 0);
		}
		KawaiiPhysicsNodeFixture::FFixture LimitFixture(MakeChainOptions());
		LimitFixture.EvaluateFrame();
		TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset> LimitPreset(NewObject<UKawaiiPhysicsPresetDataAsset>());
		LimitPreset->CopyFromNode(LimitFixture.Node);
		TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> LimitAsset(NewObject<UKawaiiPhysicsLimitsDataAsset>());
		LimitAsset->SphericalLimits.AddDefaulted(2);
		for (FSphericalLimit& SphereLimit : LimitAsset->SphericalLimits)
		{
			SphereLimit.DrivingBone.BoneName = TEXT("root");
			SphereLimit.OffsetLocation = FVector(100000.0, 0.0, 0.0);
		}
		MarkLimitsAsDataAssetSource(*LimitAsset);
		LimitPreset->Node.LimitsDataAsset = LimitAsset.Get();
		bOk &= TestEqual(TEXT("LimitsDataAsset だけを適用"),
			UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(LimitFixture.Node, *LimitPreset, {}), 1);
		bOk &= TestTrue(TEXT("形状の再適用を要求"), LimitFixture.Accessor.IsLimitAssetsReapplyPending());
		LimitFixture.EvaluateFrame();
		bOk &= TestEqual(TEXT("球の件数はプリセット側と一致"),
			LimitFixture.Node.SphericalLimitsData.Num(), LimitAsset->SphericalLimits.Num());
		return bOk;
	}

	// SQ-06（ノード部分）・TS-U4。mutant なし。Stop(0.5) のフェードを物理時間で進め、中間時刻の Damping は約 0.85 倍
	// （s=0.5 で α=0.31、s=2 で α=0.32）。実時間で進めると s=0.5 は α≈0.02、s=2 は α≈0.46 になって落ちる。
	// mutant 2049 は Sequencer のレジストリ（StopForSection）の中なので、ノードへ直接 Stop するこのテストでは落とせない。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSetupScaledStopTest,
		"KawaiiPhysics.ControlSpec.Setup.ScaledStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecSetupScaledStopTest::RunTest(const FString& Parameters)
	{
		bool bOk = true;
		for (const float TimeScale : {0.5f, 2.0f})
		{
			KawaiiPhysicsNodeFixture::FFixture TargetFixture(MakeChainOptions(TimeScale));
			KawaiiPhysicsNodeFixture::FFixture ReferenceFixture(MakeChainOptions(TimeScale));
			TargetFixture.EvaluateFrame();
			ReferenceFixture.EvaluateFrame();
			FKawaiiPhysicsSettingsMultiplier MultiplierScale;
			MultiplierScale.Damping = 0.5f;
			TargetFixture.Node.RequestPushPhysicsSettingsMultiplier(MultiplierScale, 0.6f, 501);
			TargetFixture.EvaluateFrame();
			ReferenceFixture.EvaluateFrame();
			bOk &= TestEqual(TEXT("Push が届く"), TargetFixture.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
			TargetFixture.Node.RequestStopPhysicsSettingsMultiplier(501, 0.5f);
			const int32 HalfFrame = TimeScale == 0.5f ? 30 : 8;
			for (int32 Frame = 1; Frame <= HalfFrame; ++Frame)
			{
				TargetFixture.EvaluateFrame();
				ReferenceFixture.EvaluateFrame();
			}
			if (TargetFixture.Node.TransientForceStore.SettingsMultiplierItems.Num() == 1)
			{
				// α0=0.6、倍率は 1−0.5α。float の時刻加算に実効値 1e-3 を許す。
				// τ の原点は Stop を取り込んだ評価（τ=0。取り込み前に経過時間を進める）なので、
				// HalfFrame 回目の評価では τ=(HalfFrame−1)×s/60。
				const float PhysicalTime = (HalfFrame - 1) * TimeScale / 60.0f;
				const float ExpectedAlpha = 0.6f * FMath::Max(0.0f, 1.0f - PhysicalTime / 0.5f);
				bOk &= TestTrue(TEXT("中間時刻の実効 Damping"),
					TargetFixture.Node.ModifyBones.Num() > 1 && ReferenceFixture.Node.ModifyBones.Num() > 1 &&
					FMath::Abs(TargetFixture.Node.ModifyBones[1].PhysicsSettings.Damping -
						ReferenceFixture.Node.ModifyBones[1].PhysicsSettings.Damping * (1.0f - 0.5f * ExpectedAlpha)) <= 0.001f);
			}
			else
			{
				bOk &= TestTrue(TEXT("中間時刻には倍率が残る"), false);
			}
			for (int32 Frame = HalfFrame; Frame < (TimeScale == 0.5f ? 62 : 17); ++Frame)
			{
				TargetFixture.EvaluateFrame();
				ReferenceFixture.EvaluateFrame();
			}
			bOk &= TestEqual(TEXT("実時間 0.5/s 以内に終了"), TargetFixture.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
			bOk &= TestTrue(TEXT("終了後の実効設定が対照へ戻る"), AreBoneSettingsEqual(TargetFixture.Node, ReferenceFixture.Node));
		}
		return bOk;
	}

	// FX-34。mutant なし。同じ四アセットをピンから毎フレーム書いても要求と位置に差が出ず、派生データも作り直さない。
	// 評価をまたぐ目印は WITH_EDITOR の評価ごとの同期で作り直されるため、評価の冒頭で走る
	// ApplyPendingRuntimeReinit を直接呼び、その一回で DataAsset 由来の形状を取り込み直したかを目印で見る。
	// 同じポインタでも再適用する退行（HaveLimitAssetsChangedSinceApplied が常に真、比較が Applied* を見ない等）では
	// 「同じピン値なら目印が残る」が落ちる。逆に比較が常に偽になる退行では対照の「ピンを替えると作り直す」が落ちる。
	// MirrorDataTable には行の型（FMirrorTableRow）を持たせる。無いと評価ごとに LogDataTable のエラーが出てテストが落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSetupStablePinsTest,
		"KawaiiPhysics.ControlSpec.Setup.StablePins", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecSetupStablePinsTest::RunTest(const FString& Parameters)
	{
		// 目印の基準になる DataAsset 側の半径と、取り込み後に書く目印の半径（アセットの値と区別できればよい）。
		constexpr float AssetSphereRadius = 2.0f;
		constexpr float MarkerSphereRadius = 123.0f;
		TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> LimitAsset(NewObject<UKawaiiPhysicsLimitsDataAsset>());
		FSphericalLimit& AssetSphere = LimitAsset->SphericalLimits.AddDefaulted_GetRef();
		AssetSphere.DrivingBone.BoneName = TEXT("root");
		AssetSphere.Radius = AssetSphereRadius;
		MarkLimitsAsDataAssetSource(*LimitAsset);
		TStrongObjectPtr<UPhysicsAsset> PhysicsAsset(NewObject<UPhysicsAsset>());
		TStrongObjectPtr<UPhysicsAsset> OtherPhysicsAsset(NewObject<UPhysicsAsset>());
		TStrongObjectPtr<UMirrorDataTable> MirrorTable(NewObject<UMirrorDataTable>());
		// アセットとして作った MirrorDataTable と同じく行の型を持たせる。無いと評価のたびに LogDataTable のエラーになる。
		MirrorTable->RowStruct = FMirrorTableRow::StaticStruct();
		TStrongObjectPtr<UKawaiiPhysicsBoneConstraintsDataAsset> ConstraintAsset(NewObject<UKawaiiPhysicsBoneConstraintsDataAsset>());
		KawaiiPhysicsNodeFixture::FOptions ChainOptions = MakeChainOptions();
		const auto ConfigureBase = ChainOptions.ConfigureNode;
		ChainOptions.ConfigureNode = [ConfigureBase, &LimitAsset, &PhysicsAsset, &MirrorTable, &ConstraintAsset](FAnimNode_KawaiiPhysics& ConfiguredNode)
		{
			ConfigureBase(ConfiguredNode);
			ConfiguredNode.LimitsDataAsset = LimitAsset.Get();
			ConfiguredNode.PhysicsAssetForLimits = PhysicsAsset.Get();
			ConfiguredNode.MirrorDataTableForLimits = MirrorTable.Get();
			ConfiguredNode.BoneConstraintsDataAsset = ConstraintAsset.Get();
		};
		KawaiiPhysicsNodeFixture::FFixture TargetFixture(ChainOptions);
		KawaiiPhysicsNodeFixture::FFixture ReferenceFixture(ChainOptions);
		bool bOk = true;
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			TargetFixture.Node.LimitsDataAsset = LimitAsset.Get();
			TargetFixture.Node.PhysicsAssetForLimits = PhysicsAsset.Get();
			TargetFixture.Node.MirrorDataTableForLimits = MirrorTable.Get();
			TargetFixture.Node.BoneConstraintsDataAsset = ConstraintAsset.Get();
			TargetFixture.EvaluateFrame();
			ReferenceFixture.EvaluateFrame();
			bOk &= TestFalse(TEXT("同じピン値では再適用要求なし"), FKawaiiPhysicsTestAccessor::HasPendingRuntimeApplyRequest(TargetFixture.Node));
			bOk &= TestTrue(TEXT("位置列は対照と 1e-3 cm 以内"), TargetFixture.Node.ModifyBones.Num() > 0 &&
				TargetFixture.Node.ModifyBones.Num() == ReferenceFixture.Node.ModifyBones.Num());
			for (int32 BoneIndex = 0; BoneIndex < TargetFixture.Node.ModifyBones.Num() &&
				BoneIndex < ReferenceFixture.Node.ModifyBones.Num(); ++BoneIndex)
			{
				// 同じ初期状態の Verlet 浮動小数計算の丸めを 1e-3 cm まで許す。
				bOk &= TestTrue(TEXT("同じピン値のボーン位置"), FVector::Dist(
					TargetFixture.Node.ModifyBones[BoneIndex].Location,
					ReferenceFixture.Node.ModifyBones[BoneIndex].Location) <= 0.001);
			}
		}

		// DataAsset 由来の球を一つ取り込んでいる（SourceType = DataAsset なので評価ごとの同期でも増えない）。
		FAnimNode_KawaiiPhysics& TargetNode = TargetFixture.Node;
		auto FindDataAssetSphere = [&TargetNode]() -> FSphericalLimit*
		{
			return TargetNode.SphericalLimitsData.FindByPredicate([](const FSphericalLimit& Limit)
			{
				return Limit.SourceType == ECollisionSourceType::DataAsset;
			});
		};
		auto CountDataAssetSpheres = [&TargetNode]()
		{
			int32 Count = 0;
			for (const FSphericalLimit& Limit : TargetNode.SphericalLimitsData)
			{
				Count += Limit.SourceType == ECollisionSourceType::DataAsset ? 1 : 0;
			}
			return Count;
		};
		bOk &= TestEqual(TEXT("DataAsset の球を一つ取り込む"), CountDataAssetSpheres(), 1);
		FSphericalLimit* ImportedSphere = FindDataAssetSphere();
		if (!TestNotNull(TEXT("取り込んだ球"), ImportedSphere))
		{
			return false;
		}
		bOk &= TestEqual(TEXT("取り込んだ球はアセットの半径"), ImportedSphere->Radius, AssetSphereRadius);
		const FBoneContainer& RequiredBones = TargetFixture.Proxy->GetRequiredBones();

		// 主張: 同じ四つのポインタを書き直して評価冒頭の口を呼んでも、取り込み直さないので目印が残る。
		ImportedSphere->Radius = MarkerSphereRadius;
		TargetNode.LimitsDataAsset = LimitAsset.Get();
		TargetNode.PhysicsAssetForLimits = PhysicsAsset.Get();
		TargetNode.MirrorDataTableForLimits = MirrorTable.Get();
		TargetNode.BoneConstraintsDataAsset = ConstraintAsset.Get();
		TargetFixture.Accessor.CallApplyPendingRuntimeReinit(RequiredBones);
		bOk &= TestFalse(TEXT("同じピン値では再適用要求が残らない"), FKawaiiPhysicsTestAccessor::HasPendingRuntimeApplyRequest(TargetNode));
		bOk &= TestEqual(TEXT("同じピン値では球の件数が変わらない"), CountDataAssetSpheres(), 1);
		ImportedSphere = FindDataAssetSphere();
		bOk &= TestTrue(TEXT("同じピン値では目印が残る（再適用しない）"),
			ImportedSphere != nullptr && ImportedSphere->Radius == MarkerSphereRadius);

		// 対照 1: ポインタを一つ別のアセットへ替えると、LimitsDataAsset 由来の球もアセットの値で作り直す。
		TargetNode.PhysicsAssetForLimits = OtherPhysicsAsset.Get();
		TargetFixture.Accessor.CallApplyPendingRuntimeReinit(RequiredBones);
		bOk &= TestEqual(TEXT("ピン変更後も球は一つ"), CountDataAssetSpheres(), 1);
		ImportedSphere = FindDataAssetSphere();
		bOk &= TestTrue(TEXT("ピン変更では目印が作り直される"),
			ImportedSphere != nullptr && ImportedSphere->Radius == AssetSphereRadius);

		// 対照 2: ポインタを変えなくても、setter と同じ再適用要求があれば作り直す。
		if (ImportedSphere)
		{
			ImportedSphere->Radius = MarkerSphereRadius;
		}
		TargetNode.RequestLimitAssetsReapply();
		TargetFixture.Accessor.CallApplyPendingRuntimeReinit(RequiredBones);
		bOk &= TestFalse(TEXT("再適用要求は消費される"), TargetFixture.Accessor.IsLimitAssetsReapplyPending());
		ImportedSphere = FindDataAssetSphere();
		bOk &= TestTrue(TEXT("再適用要求では目印が作り直される"),
			ImportedSphere != nullptr && ImportedSphere->Radius == AssetSphereRadius);
		return bOk;
	}

	// FX-30。mutant なし。五種類とも縮小 LOD の PoseBone から作った CompactPose 番号へ追従する。
	// DataAsset の形状にはエディタで足したときと同じ SourceType = DataAsset を付ける（無いと評価ごとに足し直されて件数が増える）。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSetupLimitLodTest,
		"KawaiiPhysics.ControlSpec.Setup.LimitLod", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecSetupLimitLodTest::RunTest(const FString& Parameters)
	{
		TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> LimitAsset(NewObject<UKawaiiPhysicsLimitsDataAsset>());
		LimitAsset->SphericalLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("hair_b");
		LimitAsset->CapsuleLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("hair_b");
		LimitAsset->TaperedCapsuleLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("hair_b");
		LimitAsset->BoxLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("hair_b");
		LimitAsset->PlanarLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("hair_b");
		MarkLimitsAsDataAssetSource(*LimitAsset);
		KawaiiPhysicsNodeFixture::FOptions ChainOptions;
		ChainOptions.Bones = {
			{TEXT("root"), INDEX_NONE, FTransform::Identity},
			{TEXT("spine"), 0, FTransform(FVector(0.0, 0.0, -10.0))},
			{TEXT("hair_a"), 0, FTransform(FVector(0.0, 0.0, -10.0))},
			{TEXT("hair_b"), 2, FTransform(FVector(0.0, 0.0, -10.0))}};
		ChainOptions.ConfigureNode = [&LimitAsset](FAnimNode_KawaiiPhysics& ConfiguredNode)
		{
			ConfiguredNode.RootBone.BoneName = TEXT("hair_a");
			ConfiguredNode.LimitsDataAsset = LimitAsset.Get();
		};
		KawaiiPhysicsNodeFixture::FFixture TargetFixture(ChainOptions);
		TargetFixture.EvaluateFrame();
		bool bOk = TestTrue(TEXT("五種類を適用"),
			TargetFixture.Node.SphericalLimitsData.Num() == 1 && TargetFixture.Node.CapsuleLimitsData.Num() == 1 &&
			TargetFixture.Node.TaperedCapsuleLimitsData.Num() == 1 && TargetFixture.Node.BoxLimitsData.Num() == 1 &&
			TargetFixture.Node.PlanarLimitsData.Num() == 1);
		if (!bOk) { return false; }
		const FBoneContainer& FullBones = TargetFixture.Proxy->GetRequiredBones();
		const int32 FullIndex = FullBones.MakeCompactPoseIndex(
			FMeshPoseBoneIndex(FullBones.GetPoseBoneIndexForBoneName(TEXT("hair_b")))).GetInt();
		TargetFixture.SetRequiredBones({0, 2, 3});
		const FBoneContainer& ReducedBones = TargetFixture.Proxy->GetRequiredBones();
		const int32 ExpectedIndex = ReducedBones.MakeCompactPoseIndex(
			FMeshPoseBoneIndex(ReducedBones.GetPoseBoneIndexForBoneName(TEXT("hair_b")))).GetInt();
		// 対照: spine を外すので hair_b の CompactPose 番号は変わる（変わらない入力では作り直しの漏れを判別できない）。
		bOk &= TestNotEqual(TEXT("対照: LOD で hair_b の番号が変わる"), ExpectedIndex, FullIndex);
		// エディタビルドでは評価ごとの DataAsset の同期が形状を作り直して番号を直してしまうので、
		// LOD の作り直し（SetRequiredBones → CacheBones）の直後、評価の前に確かめる。パッケージ版にはこの同期が無い。
		const auto CheckIndices = [this, &TargetFixture, &ReducedBones, ExpectedIndex](const TCHAR* Stage)
		{
			bool bStageOk = TestEqual(FString::Printf(TEXT("%s: 球の番号"), Stage), TargetFixture.Node.SphericalLimitsData[0].DrivingBone.GetCompactPoseIndex(ReducedBones).GetInt(), ExpectedIndex);
			bStageOk &= TestEqual(FString::Printf(TEXT("%s: カプセルの番号"), Stage), TargetFixture.Node.CapsuleLimitsData[0].DrivingBone.GetCompactPoseIndex(ReducedBones).GetInt(), ExpectedIndex);
			bStageOk &= TestEqual(FString::Printf(TEXT("%s: テーパードの番号"), Stage), TargetFixture.Node.TaperedCapsuleLimitsData[0].DrivingBone.GetCompactPoseIndex(ReducedBones).GetInt(), ExpectedIndex);
			bStageOk &= TestEqual(FString::Printf(TEXT("%s: ボックスの番号"), Stage), TargetFixture.Node.BoxLimitsData[0].DrivingBone.GetCompactPoseIndex(ReducedBones).GetInt(), ExpectedIndex);
			bStageOk &= TestEqual(FString::Printf(TEXT("%s: 平面の番号"), Stage), TargetFixture.Node.PlanarLimitsData[0].DrivingBone.GetCompactPoseIndex(ReducedBones).GetInt(), ExpectedIndex);
			return bStageOk;
		};
		bOk &= CheckIndices(TEXT("LOD の作り直しの直後"));
		TargetFixture.EvaluateFrame();
		bOk &= CheckIndices(TEXT("評価の後"));
		return bOk;
	}

	bool ChangePropertyFromDefault(FProperty& TargetProperty, void* ValueAddress, int32 Depth = 0)
	{
		if (Depth > 8) { return false; }
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(&TargetProperty))
		{
			BoolProperty->SetPropertyValue(ValueAddress, !BoolProperty->GetPropertyValue(ValueAddress));
			return true;
		}
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(&TargetProperty))
		{
			FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
			UnderlyingProperty->SetIntPropertyValue(ValueAddress, UnderlyingProperty->GetSignedIntPropertyValue(ValueAddress) + 1);
			return true;
		}
		if (FNumericProperty* NumberProperty = CastField<FNumericProperty>(&TargetProperty))
		{
			if (NumberProperty->IsFloatingPoint())
			{
				NumberProperty->SetFloatingPointPropertyValue(ValueAddress, NumberProperty->GetFloatingPointPropertyValue(ValueAddress) + 1.0);
			}
			else
			{
				NumberProperty->SetIntPropertyValue(ValueAddress, NumberProperty->GetSignedIntPropertyValue(ValueAddress) + 1);
			}
			return true;
		}
		if (FNameProperty* NameProperty = CastField<FNameProperty>(&TargetProperty))
		{
			NameProperty->SetPropertyValue(ValueAddress, FName(TEXT("ControlSpecChanged")));
			return true;
		}
		if (FStrProperty* StringProperty = CastField<FStrProperty>(&TargetProperty))
		{
			StringProperty->SetPropertyValue(ValueAddress, TEXT("ControlSpecChanged"));
			return true;
		}
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(&TargetProperty))
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, ValueAddress);
			ArrayHelper.AddValue();
			return true;
		}
		if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(&TargetProperty))
		{
			ObjectProperty->SetObjectPropertyValue(ValueAddress, NewObject<UObject>(GetTransientPackage(), ObjectProperty->PropertyClass));
			return true;
		}
		if (FStructProperty* StructProperty = CastField<FStructProperty>(&TargetProperty))
		{
			if (StructProperty->Struct == FGameplayTag::StaticStruct())
			{
				*static_cast<FGameplayTag*>(ValueAddress) = TAG_KawaiiPhysicsControlSpecCopy.GetTag();
				return true;
			}
			if (StructProperty->Struct == FRuntimeFloatCurve::StaticStruct())
			{
				static_cast<FRuntimeFloatCurve*>(ValueAddress)->GetRichCurve()->AddKey(0.5f, 0.3f);
				return true;
			}
			for (TFieldIterator<FProperty> MemberProperty(StructProperty->Struct); MemberProperty; ++MemberProperty)
			{
				if (ChangePropertyFromDefault(**MemberProperty, MemberProperty->ContainerPtrToValuePtr<void>(ValueAddress), Depth + 1))
				{
					return true;
				}
			}
		}
		return false;
	}

	// PR-03・PR-04・PR-U2。固定表の 73 CopyTarget を全部既定値から変えて往復させる。
	// 修正前は六つのカーブの KeyHandle の有無が異なるため、元ノードの MatchesNode と差分 0 が落ちる。
	// カーブの値は独立した比較でも検査する。件数・設定値は完全一致。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSetupPresetCopyAllTargetsTest,
		"KawaiiPhysics.ControlSpec.Setup.PresetCopyAllTargets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecSetupPresetCopyAllTargetsTest::RunTest(const FString& Parameters)
	{
		const TCHAR* CopyTargetNames = TEXT("DummyBoneLength,BoneSubdivisionCount,bBoneSubdivisionCollisionOnly,bBoneSubdivisionDensifyByRadius,"
			"BoneConstraintSubdivisionCount,BoneConstraintSubdivisionFeedbackScale,BoneForwardAxis,PhysicsSettings,"
			"SimulationSpace,SimulationBaseBone,TargetFramerate,WarmUpFrames,bUseWarmUpWhenResetDynamics,bNeedWarmUp,"
			"TeleportDistanceThreshold,TeleportRotationThreshold,PlanarConstraint,SkelCompMoveScale,"
			"bUpdatePhysicsSettingsInGame,ResetBoneTransformWhenBoneNotFound,DampingCurveData,StiffnessCurveData,"
			"WorldDampingLocationCurveData,WorldDampingRotationCurveData,RadiusCurveData,LimitAngleCurveData,"
			"SphericalLimits,CapsuleLimits,TaperedCapsuleLimits,BoxLimits,PlanarLimits,LimitsDataAsset,"
			"PhysicsAssetForLimits,MirrorDataTableForLimits,bSkipMirroredBoneWithExistingCollision,"
			"bSharedCollisionSource,bUseSharedCollision,SharedCollisionGroupTag,BoneConstraintGlobalComplianceType,"
			"BoneConstraintIterationCountBeforeCollision,BoneConstraintIterationCountAfterCollision,"
			"bAutoAddChildDummyBoneConstraint,BoneConstraints,BoneConstraintsDataAsset,SyncBones,Gravity,bUseLegacyGravity,"
			"bUseDefaultGravityZProjectSetting,bUseWorldSpaceGravity,bEnableWind,WindScale,WindDirectionNoiseAngle,"
			"WindSampleMode,SimpleExternalForce,bUseWorldSpaceSimpleExternalForce,ExternalForces,CustomExternalForces,"
			"bAllowWorldCollision,bOverrideCollisionParams,CollisionChannelSettings,bIgnoreSelfComponent,IgnoreBones,"
			"IgnoreBoneNamePrefix,bUseSimpleWorldCollision,SimpleWorldCollisionGatherInterval,SimpleWorldCollisionObjectTypes,"
			"SimpleWorldCollisionConvexFallbackShape,bOverrideSimpleWorldCollisionGatherRadius,SimpleWorldCollisionGatherRadius,"
			"bSimpleWorldCollisionGroundCollision,SimpleWorldCollisionSkeletalMeshCollision,SimpleWorldCollisionSource,"
			"SimpleWorldCollisionSharedTag");
		TArray<FString> PropertyNames;
		FString(CopyTargetNames).ParseIntoArray(PropertyNames, TEXT(","), true);
		bool bOk = TestEqual(TEXT("固定表の件数"), PropertyNames.Num(), 73);
		FAnimNode_KawaiiPhysics SourceNode;
		const FAnimNode_KawaiiPhysics DefaultNode;
		for (const FString& PropertyName : PropertyNames)
		{
			FProperty* TargetProperty = FAnimNode_KawaiiPhysics::StaticStruct()->FindPropertyByName(FName(*PropertyName));
			if (!TestNotNull(*PropertyName, TargetProperty)) { bOk = false; continue; }
			bOk &= TestTrue(FString::Printf(TEXT("%s を変更できる"), *PropertyName),
				ChangePropertyFromDefault(*TargetProperty, TargetProperty->ContainerPtrToValuePtr<void>(&SourceNode)));
			bOk &= TestFalse(FString::Printf(TEXT("%s は既定値と違う"), *PropertyName),
				TargetProperty->Identical_InContainer(&SourceNode, &DefaultNode));
			if (const FStructProperty* CurveProperty = CastField<FStructProperty>(TargetProperty);
				CurveProperty && CurveProperty->Struct == FRuntimeFloatCurve::StaticStruct())
			{
				// 変更したキーそのものが既定値と違うことを独立した比較で確かめる。
				bOk &= TestFalse(FString::Printf(TEXT("%s のキーは既定値と違う"), *PropertyName), AreCurveSettingsEqual(
					*CurveProperty->ContainerPtrToValuePtr<FRuntimeFloatCurve>(&SourceNode),
					*CurveProperty->ContainerPtrToValuePtr<FRuntimeFloatCurve>(&DefaultNode)));
			}
		}
		TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset> PresetAsset(NewObject<UKawaiiPhysicsPresetDataAsset>());
		PresetAsset->CopyFromNode(SourceNode);
		FAnimNode_KawaiiPhysics AppliedNode;
		PresetAsset->ApplyToNode(AppliedNode, {}, PresetAsset.Get());
		for (const FString& PropertyName : PropertyNames)
		{
			FProperty* TargetProperty = FAnimNode_KawaiiPhysics::StaticStruct()->FindPropertyByName(FName(*PropertyName));
			if (!TargetProperty)
			{
				continue;
			}
			if (const FStructProperty* CurveProperty = CastField<FStructProperty>(TargetProperty);
				CurveProperty && CurveProperty->Struct == FRuntimeFloatCurve::StaticStruct())
			{
				// キー・既定値・外挿・外部カーブを独立した比較で検査し、MatchesNode の結果も下で検査する。
				bOk &= TestTrue(FString::Printf(TEXT("%s は往復後も一致"), *PropertyName), AreCurveSettingsEqual(
					*CurveProperty->ContainerPtrToValuePtr<FRuntimeFloatCurve>(&SourceNode),
					*CurveProperty->ContainerPtrToValuePtr<FRuntimeFloatCurve>(&AppliedNode)));
				continue;
			}
			bOk &= TestTrue(FString::Printf(TEXT("%s は往復後も一致"), *PropertyName),
				TargetProperty->Identical_InContainer(&SourceNode, &AppliedNode));
		}
		TArray<FName> DiffProperties;
		bOk &= TestTrue(TEXT("KeyHandle のある元ノードも MatchesNode で一致"),
			PresetAsset->MatchesNode(SourceNode, {}, DiffProperties));
		bOk &= TestEqual(TEXT("元ノードの差分は空"), DiffProperties.Num(), 0);
		bOk &= TestTrue(TEXT("適用後のノードは MatchesNode で一致"),
			PresetAsset->MatchesNode(AppliedNode, {}, DiffProperties));
		bOk &= TestEqual(TEXT("適用後の差分は空"), DiffProperties.Num(), 0);
		return bOk;
	}
}

#endif
