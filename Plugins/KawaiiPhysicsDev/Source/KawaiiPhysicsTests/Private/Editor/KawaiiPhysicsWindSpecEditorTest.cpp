// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsAnimBlueprintFixture.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "AnimNotifies/AnimNotify_KawaiiPhysicsTriggerGust.h"
#include "AnimNotifies/AnimNotify_KawaiiPhysicsAddExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeLock.h"

namespace KawaiiPhysicsWindSpecEditorTestPrivate
{

KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(GWindTagA, "KawaiiPhysics.WindSpec.A");
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(GWindTagAChild, "KawaiiPhysics.WindSpec.A.Child");
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(GWindTagB, "KawaiiPhysics.WindSpec.B");

KawaiiPhysicsAnimBlueprintFixture::FOptions MakeTaggedWindOptions()
{
	KawaiiPhysicsAnimBlueprintFixture::FOptions BlueprintOptions;
	BlueprintOptions.Bones = KawaiiPhysicsNodeFixture::BuildHair(3, 2);
	BlueprintOptions.NodeCount = 3;
	BlueprintOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode, const int32 NodeIndex)
	{
		AnimNode.RootBone.BoneName = KawaiiPhysicsNodeFixture::GetHairBoneName(NodeIndex, 0);
		AnimNode.KawaiiPhysicsTag = NodeIndex == 0 ? GWindTagA.GetTag() :
			(NodeIndex == 1 ? GWindTagAChild.GetTag() : GWindTagB.GetTag());
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
	};
	return BlueprintOptions;
}

// TW-GU-39。生き残り mutant なし。FilterTags を無視すると B の件数が一になり、向きの正規化を忘れると (0,0.6,0.8) で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecNotifyTargetFilterTest,
	"KawaiiPhysics.WindSpecEditor.NotifyTargetFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecNotifyTargetFilterTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const bool bExactMatch : {true, false})
	{
		KawaiiPhysicsAnimBlueprintFixture::FFixture BlueprintFixture(MakeTaggedWindOptions());
		if (!TestTrue(TEXT("対照: AnimInstance が生存"), BlueprintFixture.IsReady())) return false;
		const auto References = BlueprintFixture.GetNodes();
		if (!TestEqual(TEXT("対照: 三ノードを収集"), References.Num(), 3)) return false;
		// ノードの初期化（最初の評価）は要求の列を空にするので、先に 1 フレーム評価してから Notify を出す。本番の Notify はアニメ再生中に出る。
		if (!TestTrue(TEXT("対照: 初期化のフレームを評価"), BlueprintFixture.StepFrame())) return false;
		UAnimNotify_KawaiiPhysicsTriggerGust* GustNotify = NewObject<UAnimNotify_KawaiiPhysicsTriggerGust>();
		GustNotify->GustTarget = EKawaiiPhysicsGustTarget::Nodes;
		GustNotify->Strength = 100.0f;
		GustNotify->DecayTime = 2.0f;
		GustNotify->GustDirection = FVector(0.0f, 3.0f, 4.0f);
		GustNotify->FilterTags.AddTag(GWindTagA.GetTag());
		GustNotify->bFilterExactMatch = bExactMatch;
		GustNotify->Notify(BlueprintFixture.GetComponent(), nullptr, FAnimNotifyEventReference());
		bOk &= TestTrue(TEXT("Notify 後の一フレームを評価"), BlueprintFixture.StepFrame());
		for (const auto& Reference : References)
		{
			const auto& TargetNode = Reference.GetAnimNode<FAnimNode_KawaiiPhysics>();
			const bool bShouldReceive = TargetNode.KawaiiPhysicsTag == GWindTagA.GetTag() ||
				(!bExactMatch && TargetNode.KawaiiPhysicsTag == GWindTagAChild.GetTag());
			const int32 ExpectedCount = bShouldReceive ? 1 : 0;
			bOk &= TestEqual(TEXT("Tag と完全一致の指定で一時突風を選ぶ"),
				TargetNode.TransientForceStore.Items.Num(), ExpectedCount);
			if (bShouldReceive && TargetNode.TransientForceStore.Items.Num() == 1)
			{
				const auto& GustWind = TargetNode.TransientForceStore.Items[0].Force.Get<FKawaiiPhysics_ExternalForce_ProceduralWind>();
				// (0,3,4) の長さは 5 なので、単位方向は (0,0.6,0.8)。float 正規化の丸めを 1e-4 許す。
				bOk &= TestTrue(TEXT("突風の入力方向が届く"), GustWind.WindDirection == FVector(0.0f, 3.0f, 4.0f));
				bOk &= TestTrue(TEXT("評価に使う突風の方向を正規化"),
					GustWind.RuntimeState.IsValid() &&
					GustWind.RuntimeState->CachedWindVector.Equals(FVector(0.0f, 0.6f, 0.8f), 1e-4f));
			}
		}
	}
	return bOk;
}

// TW-GU-06。3129: Notify の Duration から Hold=1 を落とすと、Publisher の HoldTime が 0 となり t=1 の Gust は 100 から 50 になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecNotifySharedHoldTest,
	"KawaiiPhysics.WindSpecEditor.NotifySharedHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecNotifySharedHoldTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsAnimBlueprintFixture::FOptions BlueprintOptions;
	BlueprintOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	BlueprintOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode, const int32)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
	};
	KawaiiPhysicsAnimBlueprintFixture::FFixture BlueprintFixture(BlueprintOptions);
	if (!TestTrue(TEXT("対照: AnimInstance が生存"), BlueprintFixture.IsReady())) return false;
	FAnimNode_KawaiiPhysicsSharedPublisher WindPublisher;
	WindPublisher.SharedGroupTag = GWindTagA.GetTag();
	WindPublisher.SharedWind.TimeScale = 1.0f;
	WindPublisher.SharedWind.EnsureRuntimeState();
	const auto PublishAt = [&BlueprintFixture, &WindPublisher](const float WorldTime)
	{
		BlueprintFixture.World->TimeSeconds = WorldTime;
		WindPublisher.PreUpdate(BlueprintFixture.GetAnimInstance());
		FAnimInstanceProxy PublisherProxy(BlueprintFixture.GetAnimInstance());
		FAnimationUpdateContext UpdateContext(&PublisherProxy, 0.0f);
		WindPublisher.Update_AnyThread(UpdateContext);
	};
	PublishAt(0.0f);
	UAnimNotify_KawaiiPhysicsTriggerGust* GustNotify = NewObject<UAnimNotify_KawaiiPhysicsTriggerGust>();
	GustNotify->GustTarget = EKawaiiPhysicsGustTarget::SharedPublisher;
	GustNotify->SharedPublisherTag = GWindTagA.GetTag();
	GustNotify->Strength = 100.0f;
	GustNotify->RiseTime = 0.0f;
	GustNotify->HoldTime = 1.0f;
	GustNotify->DecayTime = 2.0f;
	GustNotify->Notify(BlueprintFixture.GetComponent(), nullptr, FAnimNotifyEventReference());
	PublishAt(0.0f);
	bool bOk = TestTrue(TEXT("対照: Shared Publisher に突風が届く"),
		WindPublisher.SharedWind.RuntimeState->Gusts.Num > 0);
	if (WindPublisher.SharedWind.RuntimeState->Gusts.Num == 0) return false;
	bOk &= TestTrue(TEXT("Notify の Hold は一秒"),
		FMath::IsNearlyEqual(WindPublisher.SharedWind.RuntimeState->Gusts.Items[0].HoldTime, 1.0f, 1e-6f));
	PublishAt(1.0f);
	// R=0,H=1,D=2 なので τ=1 は Hold の終点で 100、τ=2 は Decay の中点で 50。
	bOk &= TestTrue(TEXT("Hold 終点の強さ"),
		FMath::IsNearlyEqual(WindPublisher.SharedWind.ComputeWindSample(1.0f).Gust, 100.0f, 1e-4f));
	PublishAt(2.0f);
	bOk &= TestTrue(TEXT("Decay 中点の強さ"),
		FMath::IsNearlyEqual(WindPublisher.SharedWind.ComputeWindSample(2.0f).Gust, 50.0f, 1e-4f));
	return bOk;
}

// TW-RI-01・TW-RI-03。3153: 四つの追加経路で初期化印を戻さなければ、追加直後の未初期化確認で落ちる。
// Initialize を MarkRuntimeInitialized に置き換える mutant は 5 秒の時計が残り初回 dt×TimeScale で落ち、毎フレーム呼ぶ mutant は十フレームの増分で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindSpecLibraryForceResetTest,
	"KawaiiPhysics.WindSpecEditor.LibraryForceReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindSpecLibraryForceResetTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (int32 PathIndex = 0; PathIndex < 4; ++PathIndex)
	{
		KawaiiPhysicsAnimBlueprintFixture::FOptions BlueprintOptions;
		BlueprintOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
		BlueprintOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode, const int32)
		{
			AnimNode.RootBone.BoneName = TEXT("root");
			AnimNode.WarmUpFrames = 0;
			AnimNode.Gravity = FVector::ZeroVector;
		};
		KawaiiPhysicsAnimBlueprintFixture::FFixture BlueprintFixture(BlueprintOptions);
		if (!TestTrue(TEXT("対照: AnimInstance が生存"), BlueprintFixture.IsReady())) return false;
		const auto References = BlueprintFixture.GetNodes();
		if (!TestEqual(TEXT("対照: 対象ノードが一つ"), References.Num(), 1)) return false;
		// ノード初期化で追加要求が消えないよう、最初の評価を終えてから各 API を呼ぶ。
		if (!TestTrue(TEXT("対照: ノード初期化を評価"), BlueprintFixture.StepFrame())) return false;
		auto& AddedNode = References[0].GetAnimNode<FAnimNode_KawaiiPhysics>();
		bOk &= TestTrue(TEXT("対照: ModifyBones がある"), !AddedNode.ModifyBones.IsEmpty());
		FInstancedStruct AddedForce = FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>();
		auto& InputWind = AddedForce.GetMutable<FKawaiiPhysics_ExternalForce_ProceduralWind>();
		InputWind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
		InputWind.ConstantForce = 100.0f;
		InputWind.TimeScale = 1.25f;
		InputWind.EnsureRuntimeState();
		InputWind.RuntimeState->Time = 5.0f;
		InputWind.MarkRuntimeInitialized();
		bOk &= TestTrue(TEXT("対照: 入力は初期化済み"), InputWind.IsRuntimeInitialized());
		if (PathIndex == 0)
		{
			bOk &= TestTrue(TEXT("AddExternalForce が追加"),
				UKawaiiPhysicsLibrary::AddExternalForce(References[0], AddedForce, BlueprintFixture.GetComponent()));
		}
		else if (PathIndex == 1)
		{
			TArray<FInstancedStruct> AddedForces{AddedForce};
			FGameplayTagContainer FilterTags;
			bOk &= TestTrue(TEXT("AddExternalForcesOnComponent が追加"),
				UKawaiiPhysicsLibrary::AddExternalForcesOnComponent(BlueprintFixture.GetComponent(),
					AddedForces, BlueprintFixture.GetComponent(), FilterTags));
		}
		else if (PathIndex == 2)
		{
			UAnimNotify_KawaiiPhysicsAddExternalForce* ForceNotify =
				NewObject<UAnimNotify_KawaiiPhysicsAddExternalForce>();
			ForceNotify->AdditionalExternalForces.Add(AddedForce);
			ForceNotify->Notify(BlueprintFixture.GetComponent(), nullptr, FAnimNotifyEventReference());
		}
		else
		{
			EKawaiiPhysicsAccessExternalForceResult ExecResult = EKawaiiPhysicsAccessExternalForceResult::NotValid;
			FKawaiiPhysicsTransientHandle ForceHandle;
			UKawaiiPhysicsLibrary::AddTransientExternalForce(ExecResult, ForceHandle, References[0], AddedForce, 3.0f);
			bOk &= TestTrue(TEXT("AddTransientExternalForce が有効なハンドルを発行"),
				ExecResult == EKawaiiPhysicsAccessExternalForceResult::Valid && ForceHandle.Id != 0);
			// Library は GenerateTransientHandleId で採番し、最初の評価までは Items でなく共有キューに置く。
			FScopeLock QueueLock(&AddedNode.TransientForceStore.Queue->Mutex);
			if (!TestEqual(TEXT("対照: 一時外力がキューに一件"),
				AddedNode.TransientForceStore.Queue->PendingForces.Num(), 1)) return false;
			bOk &= TestFalse(TEXT("一時外力はキュー投入直後に未初期化"),
				AddedNode.TransientForceStore.Queue->PendingForces[0].Force.Get<FKawaiiPhysics_ExternalForce>().IsRuntimeInitialized());
			// ProceduralWind のコピーは RuntimeState を引き継がないため、追加先のキューに独立した番兵を入れる。
			auto& QueuedWind = AddedNode.TransientForceStore.Queue->PendingForces[0].Force.GetMutable<FKawaiiPhysics_ExternalForce_ProceduralWind>();
			QueuedWind.EnsureRuntimeState();
			QueuedWind.RuntimeState->Time = 5.0f;
			bOk &= TestEqual(TEXT("対照: 一時外力の初回評価前の時計は 5 秒"), QueuedWind.RuntimeState->Time, 5.0f);
		}
		if (PathIndex != 3)
		{
			if (!TestEqual(TEXT("対照: 通常外力が一件"), AddedNode.ExternalForces.Num(), 1)) return false;
			bOk &= TestFalse(TEXT("通常外力は追加直後に未初期化"),
				AddedNode.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce>().IsRuntimeInitialized());
			// 各 API の追加で RuntimeState はコピーされない。格納先の実体を初回評価前に汚して Initialize を観測する。
			auto& QueuedWind = AddedNode.ExternalForces[0].GetMutable<FKawaiiPhysics_ExternalForce_ProceduralWind>();
			QueuedWind.EnsureRuntimeState();
			QueuedWind.RuntimeState->Time = 5.0f;
			bOk &= TestEqual(TEXT("対照: 通常外力の初回評価前の時計は 5 秒"), QueuedWind.RuntimeState->Time, 5.0f);
			if (PathIndex == 2)
			{
				// Notify は本来一回限り。追加経路の初期化と時計を観測するため、このコピーを保持する。
				AddedNode.ExternalForces[0].GetMutable<FKawaiiPhysics_ExternalForce>().bIsOneShot = false;
			}
		}
		if (!TestTrue(TEXT("追加後の一フレームを評価"), BlueprintFixture.StepFrame())) return false;
		if (PathIndex == 3 &&
			!TestEqual(TEXT("対照: 一時外力が Items に移った"), AddedNode.TransientForceStore.Items.Num(), 1)) return false;
		const auto& StoredForce = PathIndex == 3
			? AddedNode.TransientForceStore.Items[0].Force : AddedNode.ExternalForces[0];
		const auto& StoredWind = StoredForce.Get<FKawaiiPhysics_ExternalForce_ProceduralWind>();
		bOk &= TestTrue(TEXT("使用前に初期化済み"), StoredWind.IsRuntimeInitialized());
		if (!TestTrue(TEXT("対照: RuntimeState がある"), StoredWind.RuntimeState.IsValid())) return false;
		constexpr float ExpectedAdvance = 1.25f / 60.0f;
		// 60fps の dt×TimeScale=1.25/60。float の十一回の逐次加算に 1e-4 秒を許す。
		bOk &= TestTrue(TEXT("初回の風時計が一フレーム進む"),
			FMath::IsNearlyEqual(StoredWind.RuntimeState->Time, ExpectedAdvance, 1e-4f));
		float PreviousTime = StoredWind.RuntimeState->Time;
		for (int32 FrameIndex = 0; FrameIndex < 10; ++FrameIndex)
		{
			if (!TestTrue(TEXT("追加後の連続フレームを評価"), BlueprintFixture.StepFrame())) return false;
			const float CurrentTime = StoredWind.RuntimeState->Time;
			bOk &= TestTrue(FString::Printf(TEXT("経路 %d の風時計が連続して進む %d"), PathIndex, FrameIndex),
				FMath::IsNearlyEqual(CurrentTime - PreviousTime, ExpectedAdvance, 1e-4f));
			PreviousTime = CurrentTime;
		}
	}
	return bOk;
}

} // namespace KawaiiPhysicsWindSpecEditorTestPrivate

#endif
