// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "AnimNotifies/AnimNotifyState_KawaiiPhysicsSettingsMultiplier.h"
#include "KawaiiPhysicsAnimBlueprintFixture.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Wind.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

namespace KawaiiPhysicsControlSpecEditorSetupTestPrivate
{
	KawaiiPhysicsAnimBlueprintFixture::FOptions MakeEditorOptions()
	{
		KawaiiPhysicsAnimBlueprintFixture::FOptions EditorOptions;
		EditorOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(4);
		EditorOptions.GetComponentTransform = [](int32 Frame)
		{
			return FTransform(FVector(0.0, FMath::Sin(Frame * 0.18) * 8.0, 0.0));
		};
		EditorOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& ConfiguredNode, int32 NodeIndex)
		{
			ConfiguredNode.PhysicsSettings.Damping = 0.4f;
			ConfiguredNode.bNeedWarmUp = false;
		};
		return EditorOptions;
	}

	// SM-N03・SM-U5。mutant なし。二度目の Tick で経過時間が止まれば、上りと異なる下りの α が一致せず落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecEditorSetupNotifyEnvelopeTest,
		"KawaiiPhysics.ControlSpecEditor.Setup.NotifyEnvelope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecEditorSetupNotifyEnvelopeTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture EditorFixture(MakeEditorOptions());
		if (!TestTrue(TEXT("AnimBlueprint を初期化できる"), EditorFixture.IsReady()) ||
			!TestTrue(TEXT("初回評価が通る"), EditorFixture.StepFrame()))
		{
			return false;
		}
		const TArray<FKawaiiPhysicsReference> NodeReferences = EditorFixture.GetNodes();
		if (!TestEqual(TEXT("Notify の対象が一件ある"), NodeReferences.Num(), 1))
		{
			return false;
		}
		bool bOk = true;
		struct FEnvelopeCase { float Length; float Rise; float Decay; float FirstTick; float FirstAlpha; float SecondTick; float SecondAlpha; };
		// Tick の時刻は経過時間への加算量。圧縮時の I′/O′ は 1 秒で 0.5/0.5、0.4 秒で 0.3/0.1。
		// L=1,I=O=0.6: τ=0.2 は 0.2/0.5=0.4、τ=0.9 は (1-0.9)/0.5=0.2。
		// L=1,I=0.6,O=0.2: τ=0.3 は 0.3/0.6=0.5、τ=0.95 は (1-0.95)/0.2=0.25。
		// L=0.4,I=0.6,O=0.2: τ=0.12 は 0.12/0.3=0.4、τ=0.37 は (0.4-0.37)/0.1=0.3。
		for (const FEnvelopeCase& EnvelopeCase : {
			FEnvelopeCase{1.0f, 0.6f, 0.6f, 0.2f, 0.4f, 0.7f, 0.2f},
			FEnvelopeCase{1.0f, 0.6f, 0.2f, 0.3f, 0.5f, 0.65f, 0.25f},
			FEnvelopeCase{0.4f, 0.6f, 0.2f, 0.12f, 0.4f, 0.25f, 0.3f}})
		{
			TStrongObjectPtr<UAnimNotifyState_KawaiiPhysicsSettingsMultiplier> NotifyObject(
				NewObject<UAnimNotifyState_KawaiiPhysicsSettingsMultiplier>());
			NotifyObject->BlendInTime = EnvelopeCase.Rise;
			NotifyObject->BlendOutTime = EnvelopeCase.Decay;
			NotifyObject->SettingsScale.Damping = 0.5f;
			const FAnimNotifyEventReference EventReference;
			NotifyObject->NotifyBegin(EditorFixture.GetComponent(), nullptr, EnvelopeCase.Length, EventReference);
			bOk &= TestTrue(TEXT("Begin の要求が評価に届く"), EditorFixture.StepFrame());
			NotifyObject->NotifyTick(EditorFixture.GetComponent(), nullptr, EnvelopeCase.FirstTick, EventReference);
			bOk &= TestTrue(TEXT("最初の Tick を評価"), EditorFixture.StepFrame());
			const FAnimNode_KawaiiPhysics& FirstNode = NodeReferences[0].GetAnimNode<FAnimNode_KawaiiPhysics>();
			bOk &= TestEqual(TEXT("最初の倍率は一件"), FirstNode.TransientForceStore.SettingsMultiplierItems.Num(), 1);
			if (FirstNode.TransientForceStore.SettingsMultiplierItems.Num() == 1)
			{
				// Notify の台形を実時間で評価する。float の加算と除算に 1e-3 を許す。
				bOk &= TestTrue(TEXT("最初の適用率"),
					FMath::Abs(FirstNode.TransientForceStore.SettingsMultiplierItems[0].DrivenAlpha - EnvelopeCase.FirstAlpha) <= 0.001f);
			}
			NotifyObject->NotifyTick(EditorFixture.GetComponent(), nullptr, EnvelopeCase.SecondTick, EventReference);
			bOk &= TestTrue(TEXT("二度目の Tick を評価"), EditorFixture.StepFrame());
			const FAnimNode_KawaiiPhysics& SecondNode = NodeReferences[0].GetAnimNode<FAnimNode_KawaiiPhysics>();
			bOk &= TestEqual(TEXT("二度目の Tick 後も倍率は一件"), SecondNode.TransientForceStore.SettingsMultiplierItems.Num(), 1);
			if (SecondNode.TransientForceStore.SettingsMultiplierItems.Num() == 1)
			{
				// 台形の式から求めたリテラルとの float の加算・除算誤差に 1e-3 を許す。
				bOk &= TestTrue(TEXT("減衰側の適用率"),
					FMath::Abs(SecondNode.TransientForceStore.SettingsMultiplierItems[0].DrivenAlpha - EnvelopeCase.SecondAlpha) <= 0.001f);
			}
			NotifyObject->NotifyEnd(EditorFixture.GetComponent(), nullptr, EventReference);
			for (int32 Frame = 0; Frame < 50; ++Frame) { EditorFixture.StepFrame(); }
			bOk &= TestEqual(TEXT("End 後に倍率は消える"), SecondNode.TransientForceStore.SettingsMultiplierItems.Num(), 0);
		}
		return bOk;
	}

	// SM-N09・SM-U4。mutant なし。NotifyEnd が無くても四評価でリースが切れ、Notify の 0.5 秒でフェードする。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecEditorSetupNotifyLeaseTest,
		"KawaiiPhysics.ControlSpecEditor.Setup.NotifyLease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecEditorSetupNotifyLeaseTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture EditorFixture(MakeEditorOptions());
		if (!TestTrue(TEXT("AnimBlueprint を初期化できる"), EditorFixture.IsReady()) || !EditorFixture.StepFrame()) { return false; }
		const TArray<FKawaiiPhysicsReference> NodeReferences = EditorFixture.GetNodes();
		if (!TestEqual(TEXT("Notify の対象が一件ある"), NodeReferences.Num(), 1)) { return false; }
		TStrongObjectPtr<UAnimNotifyState_KawaiiPhysicsSettingsMultiplier> NotifyObject(
			NewObject<UAnimNotifyState_KawaiiPhysicsSettingsMultiplier>());
		NotifyObject->BlendInTime = 0.0f;
		NotifyObject->BlendOutTime = 0.5f;
		NotifyObject->SettingsScale.Damping = 0.5f;
		const FAnimNotifyEventReference EventReference;
		NotifyObject->NotifyBegin(EditorFixture.GetComponent(), nullptr, 2.0f, EventReference);
		NotifyObject->NotifyTick(EditorFixture.GetComponent(), nullptr, 0.1f, EventReference);
		EditorFixture.StepFrame();
		FAnimNode_KawaiiPhysics& TargetNode = NodeReferences[0].GetAnimNode<FAnimNode_KawaiiPhysics>();
		bool bOk = TestEqual(TEXT("Begin と Tick で倍率が一件"), TargetNode.TransientForceStore.SettingsMultiplierItems.Num(), 1);
		if (!bOk) { return false; }
		bOk &= TestTrue(TEXT("初期重みは 1"), FMath::Abs(TargetNode.TransientForceStore.SettingsMultiplierItems[0].DrivenAlpha - 1.0f) <= 0.001f);
		for (int32 Frame = 0; Frame < 4; ++Frame) { EditorFixture.StepFrame(); }
		bOk &= TestEqual(TEXT("四評価でフェードへ移る"), TargetNode.TransientForceStore.SettingsMultiplierItems.Num(), 1);
		if (TargetNode.TransientForceStore.SettingsMultiplierItems.Num() == 1)
		{
			bOk &= TestFalse(TEXT("リース失効で外部駆動が終わる"), TargetNode.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
			bOk &= TestEqual(TEXT("Notify の BlendOutTime を使う"), TargetNode.TransientForceStore.SettingsMultiplierItems[0].DecayTime, 0.5f);
		}
		for (int32 Frame = 0; Frame < 15; ++Frame) { EditorFixture.StepFrame(); }
		// α=0.5 なら Damping=0.4×(1−0.5×0.5)=0.3。60fps の端点ずれ二フレームに 0.03 を許す。
		bOk &= TestTrue(TEXT("0.25 秒後は約半分の重み"),
			TargetNode.ModifyBones.Num() > 1 && FMath::Abs(TargetNode.ModifyBones[1].PhysicsSettings.Damping - 0.3f) <= 0.03f);
		for (int32 Frame = 0; Frame < 17; ++Frame) { EditorFixture.StepFrame(); }
		bOk &= TestEqual(TEXT("0.5 秒後に倍率が消える"), TargetNode.TransientForceStore.SettingsMultiplierItems.Num(), 0);
		return bOk;
	}

	// FX-31・FX-35。mutant なし。SetRootBoneName の実経路で名前変更と同名設定を確認する。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecEditorSetupRootSetterTest,
		"KawaiiPhysics.ControlSpecEditor.Setup.RootSetter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecEditorSetupRootSetterTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture EditorFixture(MakeEditorOptions());
		if (!TestTrue(TEXT("AnimBlueprint を初期化できる"), EditorFixture.IsReady()) || !EditorFixture.StepFrame()) { return false; }
		const TArray<FKawaiiPhysicsReference> NodeReferences = EditorFixture.GetNodes();
		if (!TestEqual(TEXT("setter の対象が一件ある"), NodeReferences.Num(), 1)) { return false; }
		FAnimNode_KawaiiPhysics& TargetNode = NodeReferences[0].GetAnimNode<FAnimNode_KawaiiPhysics>();
		bool bOk = TestTrue(TEXT("元のチェーンがある"), TargetNode.ModifyBones.Num() > 1);
		FName NewRootName(TEXT("chain_1"));
		UKawaiiPhysicsLibrary::SetRootBoneName(NodeReferences[0], NewRootName);
		bOk &= TestTrue(TEXT("名前変更で再構築を要求"), FKawaiiPhysicsTestAccessor::HasPendingRuntimeApplyRequest(TargetNode));
		EditorFixture.StepFrame();
		bOk &= TestTrue(TEXT("新しい根元を構築"), TargetNode.ModifyBones.Num() > 0 &&
			TargetNode.ModifyBones[0].BoneRef.BoneName == NewRootName);
		FVector PreviousTip = TargetNode.ModifyBones.Num() > 1 ? TargetNode.ModifyBones.Last().Location : FVector::ZeroVector;
		bool bSawTipMove = false;
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			UKawaiiPhysicsLibrary::SetRootBoneName(NodeReferences[0], NewRootName);
			bOk &= TestFalse(TEXT("同名設定で再構築を要求しない"), FKawaiiPhysicsTestAccessor::HasPendingRuntimeApplyRequest(TargetNode));
			EditorFixture.StepFrame();
			bOk &= TestTrue(TEXT("同名設定後も根元を評価できる"), TargetNode.ModifyBones.Num() > 0 &&
				TargetNode.ModifyBones[0].BoneRef.BoneName == NewRootName);
			if (TargetNode.ModifyBones.Num() > 1)
			{
				bSawTipMove |= FVector::Dist(PreviousTip, TargetNode.ModifyBones.Last().Location) > 0.001;
				PreviousTip = TargetNode.ModifyBones.Last().Location;
			}
		}
		bOk &= TestTrue(TEXT("同名設定後も先端位置を更新する"), bSawTipMove);
		return bOk;
	}

	// FX-32。mutant なし。専用 setter による五種類の形状の追加と nullptr による除去を確認する。
	// 形状が二重になる（評価ごとの再適用で足し直す）変更や、nullptr で外さない変更は件数で落ちる。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecEditorSetupLimitsSetterTest,
		"KawaiiPhysics.ControlSpecEditor.Setup.LimitsSetter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecEditorSetupLimitsSetterTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture EditorFixture(MakeEditorOptions());
		if (!TestTrue(TEXT("AnimBlueprint を初期化できる"), EditorFixture.IsReady()) || !EditorFixture.StepFrame()) { return false; }
		const TArray<FKawaiiPhysicsReference> NodeReferences = EditorFixture.GetNodes();
		if (!TestEqual(TEXT("setter の対象が一件ある"), NodeReferences.Num(), 1)) { return false; }
		TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> LimitAsset(NewObject<UKawaiiPhysicsLimitsDataAsset>());
		LimitAsset->SphericalLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
		LimitAsset->CapsuleLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
		LimitAsset->TaperedCapsuleLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
		LimitAsset->BoxLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
		LimitAsset->PlanarLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
		// エディタで配列に要素を足したとき（PostEditChangeChainProperty の ArrayAdd）と同じく SourceType = DataAsset を付ける。
		// 付けないとノードは DataAsset 由来と判定できず、評価のたびに足し直したり nullptr で外せなかったりする。
		auto MarkLimits = [](auto& Limits)
		{
			for (auto& Limit : Limits)
			{
				Limit.SourceType = ECollisionSourceType::DataAsset;
			}
		};
		MarkLimits(LimitAsset->SphericalLimits);
		MarkLimits(LimitAsset->CapsuleLimits);
		MarkLimits(LimitAsset->TaperedCapsuleLimits);
		MarkLimits(LimitAsset->BoxLimits);
		MarkLimits(LimitAsset->PlanarLimits);
		FAnimNode_KawaiiPhysics& TargetNode = NodeReferences[0].GetAnimNode<FAnimNode_KawaiiPhysics>();
		UKawaiiPhysicsLibrary::SetLimitsDataAsset(NodeReferences[0], LimitAsset.Get());
		bool bOk = TestTrue(TEXT("差し替えの要求が届く"), FKawaiiPhysicsTestAccessor::HasPendingRuntimeApplyRequest(TargetNode));
		EditorFixture.StepFrame();
		bOk &= TestEqual(TEXT("球の件数"), TargetNode.SphericalLimitsData.Num(), 1);
		bOk &= TestEqual(TEXT("カプセルの件数"), TargetNode.CapsuleLimitsData.Num(), 1);
		bOk &= TestEqual(TEXT("テーパードの件数"), TargetNode.TaperedCapsuleLimitsData.Num(), 1);
		bOk &= TestEqual(TEXT("ボックスの件数"), TargetNode.BoxLimitsData.Num(), 1);
		bOk &= TestEqual(TEXT("平面の件数"), TargetNode.PlanarLimitsData.Num(), 1);
		UKawaiiPhysicsLibrary::SetLimitsDataAsset(NodeReferences[0], nullptr);
		EditorFixture.StepFrame();
		bOk &= TestEqual(TEXT("球を除去"), TargetNode.SphericalLimitsData.Num(), 0);
		bOk &= TestEqual(TEXT("カプセルを除去"), TargetNode.CapsuleLimitsData.Num(), 0);
		bOk &= TestEqual(TEXT("テーパードを除去"), TargetNode.TaperedCapsuleLimitsData.Num(), 0);
		bOk &= TestEqual(TEXT("ボックスを除去"), TargetNode.BoxLimitsData.Num(), 0);
		bOk &= TestEqual(TEXT("平面を除去"), TargetNode.PlanarLimitsData.Num(), 0);
		return bOk;
	}

	// PR-06。mutant なし。ABP 経由のプリセット適用でも、実行時の二種類の外力配列を中身まで保つ。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecEditorSetupPresetForcesTest,
		"KawaiiPhysics.ControlSpecEditor.Setup.PresetForces", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FKawaiiPhysicsControlSpecEditorSetupPresetForcesTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture EditorFixture(MakeEditorOptions());
		if (!TestTrue(TEXT("AnimBlueprint を初期化できる"), EditorFixture.IsReady()) || !EditorFixture.StepFrame()) { return false; }
		const TArray<FKawaiiPhysicsReference> NodeReferences = EditorFixture.GetNodes();
		if (!TestEqual(TEXT("プリセットの対象が一件ある"), NodeReferences.Num(), 1)) { return false; }
		FAnimNode_KawaiiPhysics& TargetNode = NodeReferences[0].GetAnimNode<FAnimNode_KawaiiPhysics>();
		TargetNode.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Wind>());
		TargetNode.CustomExternalForces.Add(nullptr);
		TargetNode.CustomExternalForces.Add(nullptr);
		const FAnimNode_KawaiiPhysics BeforeApply = TargetNode;
		TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset> PresetAsset(NewObject<UKawaiiPhysicsPresetDataAsset>());
		PresetAsset->CopyFromNode(TargetNode);
		PresetAsset->Node.ExternalForces.Reset();
		PresetAsset->Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>());
		PresetAsset->Node.CustomExternalForces.SetNum(1);
		PresetAsset->Node.PhysicsSettings.Stiffness = 0.71f;
		EKawaiiPhysicsAccessResult AccessResult = EKawaiiPhysicsAccessResult::NotValid;
		UKawaiiPhysicsLibrary::ApplyPresetDataAsset(AccessResult, NodeReferences[0], PresetAsset.Get(), {});
		bool bOk = TestTrue(TEXT("ABP 版の適用が届く"), AccessResult == EKawaiiPhysicsAccessResult::Valid);
		bOk &= TestEqual(TEXT("通常の設定は更新される"), TargetNode.PhysicsSettings.Stiffness, 0.71f);
		for (const FName PropertyName : {GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CustomExternalForces)})
		{
			const FProperty* ArrayProperty = FAnimNode_KawaiiPhysics::StaticStruct()->FindPropertyByName(PropertyName);
			bOk &= TestTrue(TEXT("外力配列の内容は適用前と Identical"),
				ArrayProperty && ArrayProperty->Identical_InContainer(&TargetNode, &BeforeApply));
		}
		return bOk;
	}
}

#endif
