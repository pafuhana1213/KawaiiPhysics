// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "KawaiiPhysicsAnimBlueprintFixture.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "AnimNotifies/AnimNotifyState_KawaiiPhysicsSettingsMultiplier.h"
#include "AnimNotifies/AnimNotify_KawaiiPhysicsTriggerGust.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "Misc/AutomationTest.h"

namespace KawaiiPhysicsRemutationSpecEditorTestPrivate
{

KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(RemutWindTag, "KawaiiPhysics.Remut.Wind");
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(RemutWindChildTag, "KawaiiPhysics.Remut.Wind.Child");
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(RemutOtherTag, "KawaiiPhysics.Remut.Other");

// SM-N06。5033: NotifyEnd が即時停止になると、終了直後の一件と途中の Damping=0.35 が落ちる。
// このテストの名前空間 KawaiiPhysics.ControlSpecEditor を 5033 の filter に追加する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemutNotifyEndBlendOutTest,
	"KawaiiPhysics.ControlSpecEditor.RemutNotifyEndBlendOutFromPartialAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRemutNotifyEndBlendOutTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FScopedDeveloperSettings NotifyStepScope([](UKawaiiPhysicsDeveloperSettings& DeveloperConfig)
	{
		DeveloperConfig.bUseFixedSubstepping = false;
	});
	bool bPassed = true;
	for (const bool bUseCurveWeight : {false, true})
	{
		KawaiiPhysicsAnimBlueprintFixture::FOptions NotifyOptions;
		NotifyOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(3);
		NotifyOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& ConfiguredPhysics, const int32 PhysicsIndex)
		{
			ConfiguredPhysics.RootBone.BoneName = TEXT("root");
			ConfiguredPhysics.DummyBoneLength = 0.0f;
			ConfiguredPhysics.WarmUpFrames = 0;
			ConfiguredPhysics.Gravity = FVector(980, 0, 0);
			ConfiguredPhysics.bUseDefaultGravityZProjectSetting = false;
			ConfiguredPhysics.PhysicsSettings.Damping = 0.4f;
			ConfiguredPhysics.PhysicsSettings.Stiffness = 0.0f;
		};
		KawaiiPhysicsAnimBlueprintFixture::FFixture NotifyFixture(NotifyOptions);
		if (!TestTrue(TEXT("対照: 生きた AnimInstance がある"), NotifyFixture.IsReady()) ||
			!TestTrue(TEXT("対照: 最初の評価を終える"), NotifyFixture.StepFrame())) return false;
		const auto NotifyReferences = NotifyFixture.GetNodes();
		if (!TestEqual(TEXT("対照: Notify の対象ノードが一件"), NotifyReferences.Num(), 1)) return false;
		auto& NotifyPhysics = NotifyReferences[0].GetAnimNode<FAnimNode_KawaiiPhysics>();
		if (!TestEqual(TEXT("対照: ModifyBones が三本ある"), NotifyPhysics.ModifyBones.Num(), 3)) return false;
		TStrongObjectPtr<UAnimNotifyState_KawaiiPhysicsSettingsMultiplier> MultiplierNotify(
			NewObject<UAnimNotifyState_KawaiiPhysicsSettingsMultiplier>());
		MultiplierNotify->SettingsScale.Damping = 0.5f;
		MultiplierNotify->BlendInTime = 1.0f;
		MultiplierNotify->BlendOutTime = 0.4f;
		if (bUseCurveWeight)
		{
			MultiplierNotify->WeightSource = EKawaiiPhysicsSettingsMultiplierWeightSource::Curve;
			MultiplierNotify->CurveName = NAME_None;
			MultiplierNotify->DefaultWeightIfNoCurve = 0.5f;
		}
		const FAnimNotifyEventReference NotifyReference;
		MultiplierNotify->NotifyBegin(NotifyFixture.GetComponent(), nullptr, 5.0f, NotifyReference);
		MultiplierNotify->NotifyTick(NotifyFixture.GetComponent(), nullptr, 0.5f, NotifyReference);
		if (!TestTrue(TEXT("対照: Begin と Tick の要求を評価"), NotifyFixture.StepFrame()) ||
			!TestEqual(TEXT("対照: 外部駆動の倍率が一件"), NotifyPhysics.TransientForceStore.SettingsMultiplierItems.Num(), 1)) return false;
		// α=0.5、倍率 0.5 なので Damping=0.4×0.75=0.3。float の重みと乗算に 0.001 を許す。
		bPassed &= TestTrue(TEXT("対照: 途中の重み 0.5 が効いている"),
			FMath::Abs(NotifyPhysics.ModifyBones[1].PhysicsSettings.Damping - 0.3f) <= 0.001f);
		bPassed &= TestTrue(TEXT("対照: 重力でチェーンが動いている"),
			FVector::Dist(NotifyPhysics.ModifyBones.Last().Location, FVector(0, 0, -20)) > 0.1);
		MultiplierNotify->NotifyEnd(NotifyFixture.GetComponent(), nullptr, NotifyReference);
		if (!TestTrue(TEXT("NotifyEnd の要求を評価"), NotifyFixture.StepFrame()) ||
			!TestEqual(TEXT("終了直後は即時除去せずフェードする"), NotifyPhysics.TransientForceStore.SettingsMultiplierItems.Num(), 1)) return false;
		bPassed &= TestTrue(TEXT("End 直後も終了時の重みから始まる"),
			FMath::Abs(NotifyPhysics.ModifyBones[1].PhysicsSettings.Damping - 0.3f) <= 0.001f);
		for (int32 FadeFrame = 0; FadeFrame < 12; ++FadeFrame)
		{
			if (!TestTrue(TEXT("BlendOut の前半を評価"), NotifyFixture.StepFrame())) return false;
		}
		// 12/60=0.2 秒は BlendOut=0.4 秒の半分。α=0.25、実効倍率=0.875、Damping=0.35。
		// float の十二回の加算と乗算に 0.001 を許す。
		bPassed &= TestTrue(TEXT("BlendOut の中点は終了時の半分の重み"),
			FMath::Abs(NotifyPhysics.ModifyBones[1].PhysicsSettings.Damping - 0.35f) <= 0.001f);
		for (int32 FadeFrame = 0; FadeFrame < 13; ++FadeFrame)
		{
			if (!TestTrue(TEXT("BlendOut の終わりまで評価"), NotifyFixture.StepFrame())) return false;
		}
		bPassed &= TestEqual(TEXT("BlendOut が終わると倍率は消える"), NotifyPhysics.TransientForceStore.SettingsMultiplierItems.Num(), 0);
		bPassed &= TestTrue(TEXT("最終的に元の設定へ戻る"),
			FMath::Abs(NotifyPhysics.ModifyBones[1].PhysicsSettings.Damping - 0.4f) <= 0.001f);
	}
	return bPassed;
}

KawaiiPhysicsAnimBlueprintFixture::FOptions MakeRemutationWindOptions()
{
	KawaiiPhysicsAnimBlueprintFixture::FOptions TaggedOptions;
	TaggedOptions.Bones = KawaiiPhysicsNodeFixture::BuildHair(3, 2);
	TaggedOptions.NodeCount = 3;
	TaggedOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& ConfiguredPhysics, const int32 PhysicsIndex)
	{
		ConfiguredPhysics.RootBone.BoneName = KawaiiPhysicsNodeFixture::GetHairBoneName(PhysicsIndex, 0);
		ConfiguredPhysics.KawaiiPhysicsTag = PhysicsIndex == 0 ? RemutWindTag.GetTag() :
			(PhysicsIndex == 1 ? RemutWindChildTag.GetTag() : RemutOtherTag.GetTag());
		ConfiguredPhysics.DummyBoneLength = 0.0f;
		ConfiguredPhysics.WarmUpFrames = 0;
		ConfiguredPhysics.Gravity = FVector::ZeroVector;
		ConfiguredPhysics.bUseDefaultGravityZProjectSetting = false;
		ConfiguredPhysics.PhysicsSettings.Stiffness = 0.0f;
		ConfiguredPhysics.PhysicsSettings.Damping = 1.0f;
	};
	return TaggedOptions;
}

// TW-GU-07・TW-GU-39。5064: Hold の既定が 0.1 秒になり、零の主張と半秒後の三角形の強さ 60 が落ちる。
// 5065: 既定宛先が Publisher になるため、このメッシュの対象ノードへの一件の配送が落ちる。
// このテストの名前空間 KawaiiPhysics.WindSpecEditor を 5065 の filter に追加する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemutNotifyLegacyGustDefaultsTest,
	"KawaiiPhysics.WindSpecEditor.RemutNotifyLegacyDefaultsAndMeshDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRemutNotifyLegacyGustDefaultsTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FScopedDeveloperSettings GustStepScope([](UKawaiiPhysicsDeveloperSettings& DeveloperConfig)
	{
		DeveloperConfig.bUseFixedSubstepping = false;
	});
	bool bPassed = true;
	for (const bool bExactTags : {true, false})
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture NotifiedMesh(MakeRemutationWindOptions());
		KawaiiPhysicsAnimBlueprintFixture::FFixture OtherMesh(MakeRemutationWindOptions());
		if (!TestTrue(TEXT("対照: 二つの AnimInstance が生存"), NotifiedMesh.IsReady() && OtherMesh.IsReady()) ||
			!TestTrue(TEXT("対照: 二つのメッシュを初期評価"), NotifiedMesh.StepFrame() && OtherMesh.StepFrame())) return false;
		const auto NotifiedReferences = NotifiedMesh.GetNodes();
		const auto OtherReferences = OtherMesh.GetNodes();
		if (!TestEqual(TEXT("対照: 通知先の三ノード"), NotifiedReferences.Num(), 3) ||
			!TestEqual(TEXT("対照: 別メッシュにも同じタグの三ノード"), OtherReferences.Num(), 3)) return false;
		TArray<FVector> BeforeGustTips;
		for (const auto& PhysicsReference : NotifiedReferences)
		{
			const auto& NotifiedPhysics = PhysicsReference.GetAnimNode<FAnimNode_KawaiiPhysics>();
			if (!TestTrue(TEXT("対照: 通知先に ModifyBones がある"), NotifiedPhysics.ModifyBones.Num() > 1)) return false;
			BeforeGustTips.Add(NotifiedPhysics.ModifyBones.Last().Location);
		}
		for (const auto& PhysicsReference : OtherReferences)
		{
			if (!TestTrue(TEXT("対照: 別メッシュも物理を評価"), PhysicsReference.GetAnimNode<FAnimNode_KawaiiPhysics>().ModifyBones.Num() > 1)) return false;
		}
		TStrongObjectPtr<UAnimNotify_KawaiiPhysicsTriggerGust> DefaultGustNotify(NewObject<UAnimNotify_KawaiiPhysicsTriggerGust>());
		// 旧アセットに無いプロパティは上書きせず、クラスの既定値のまま発火する。
		bPassed &= TestEqual(TEXT("未保存の HoldTime は零"), DefaultGustNotify->HoldTime, 0.0f);
		bPassed &= TestTrue(TEXT("既定の GustTarget はこのメッシュのノード"), DefaultGustNotify->GustTarget == EKawaiiPhysicsGustTarget::Nodes);
		DefaultGustNotify->Strength = 120.0f;
		DefaultGustNotify->RiseTime = 0.0f;
		DefaultGustNotify->DecayTime = 1.0f;
		DefaultGustNotify->GustDirection = FVector(0, 3, 4);
		DefaultGustNotify->FilterTags.AddTag(RemutWindTag.GetTag());
		DefaultGustNotify->bFilterExactMatch = bExactTags;
		DefaultGustNotify->Notify(NotifiedMesh.GetComponent(), nullptr, FAnimNotifyEventReference());
		for (int32 GustFrame = 0; GustFrame < 30; ++GustFrame)
		{
			if (!TestTrue(TEXT("突風を半秒間評価"), NotifiedMesh.StepFrame() && OtherMesh.StepFrame())) return false;
		}
		for (int32 PhysicsIndex = 0; PhysicsIndex < NotifiedReferences.Num(); ++PhysicsIndex)
		{
			const auto& NotifiedPhysics = NotifiedReferences[PhysicsIndex].GetAnimNode<FAnimNode_KawaiiPhysics>();
			const bool bReceivesGust = NotifiedPhysics.KawaiiPhysicsTag == RemutWindTag.GetTag() ||
				(!bExactTags && NotifiedPhysics.KawaiiPhysicsTag == RemutWindChildTag.GetTag());
			const int32 ExpectedGustCount = bReceivesGust ? 1 : 0;
			if (!TestEqual(TEXT("Tag と完全一致指定がこのメッシュの配送先を絞る"),
				NotifiedPhysics.TransientForceStore.Items.Num(), ExpectedGustCount)) return false;
			if (bReceivesGust)
			{
				const auto& DeliveredGust = NotifiedPhysics.TransientForceStore.Items[0].Force.Get<FKawaiiPhysics_ExternalForce_ProceduralWind>();
				if (!TestTrue(TEXT("対照: 届いた突風を初期化済み"), DeliveredGust.RuntimeState.IsValid())) return false;
				bPassed &= TestTrue(TEXT("対照: 突風がチェーンを動かす"),
					FVector::Dist(BeforeGustTips[PhysicsIndex], NotifiedPhysics.ModifyBones.Last().Location) > 0.01);
				// (0,3,4) の長さは 5。正規化の float 丸めに 1e-4 を許す。
				bPassed &= TestTrue(TEXT("既定宛先でも GustDirection が効く"),
					DeliveredGust.RuntimeState->CachedWindVector.Equals(FVector(0, 0.6, 0.8), 1.e-4));
				// Hold=0、Rise=0、Decay=1 の三角形。30/60=0.5 秒では 120 の半分の 60。
				// 風時計の三十回の float 加算とエンベロープ計算の丸めに 0.01 を許す。
				bPassed &= TestTrue(TEXT("旧通知と同じ三角形の半秒後の強さ"),
					FMath::Abs(DeliveredGust.RuntimeState->CachedGust - 60.0f) <= 0.01f);
			}
		}
		for (const auto& PhysicsReference : OtherReferences)
		{
			bPassed &= TestEqual(TEXT("別メッシュには同じ Tag でも配送しない"),
				PhysicsReference.GetAnimNode<FAnimNode_KawaiiPhysics>().TransientForceStore.Items.Num(), 0);
		}
	}
	return bPassed;
}

}

#endif
