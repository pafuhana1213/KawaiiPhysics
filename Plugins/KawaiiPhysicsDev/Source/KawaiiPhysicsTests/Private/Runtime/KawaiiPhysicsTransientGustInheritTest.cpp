// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "Animation/AnimInstanceProxy.h"
#include <limits>

namespace KawaiiPhysicsTransientGustInheritTestPrivate
{
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsGustInherit, "KawaiiPhysics.Test.Regression.A");

static KawaiiPhysicsNodeFixture::FOptions MakeOptions(const float SourceScale, const bool bTwoWinds = false)
{
	KawaiiPhysicsNodeFixture::FOptions Result;
	Result.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	Result.FrameDt = 0.1f;
	Result.ConfigureNode = [SourceScale, bTwoWinds](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
		for (int32 Index = 0; Index < (bTwoWinds ? 2 : 1); ++Index)
		{
			auto& SourceWind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode);
			SourceWind.TimeScale = Index == 0 ? SourceScale : 0.5f;
			SourceWind.WindDirection = FVector::ForwardVector;
			SourceWind.WindDirectionNoiseAngle = 20.0f;
			SourceWind.WindDirectionNoisePeriod = 0.3f;
			SourceWind.Seed = Index + 23;
		}
	};
	return Result;
}

static FKawaiiPhysics_ExternalForce_ProceduralWind* GetTransientWind(FAnimNode_KawaiiPhysics& AnimNode,
	const int32 Index = 0)
{
	return AnimNode.TransientForceStore.Items.IsValidIndex(Index)
		? AnimNode.TransientForceStore.Items[Index].Force.GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>() : nullptr;
}

static FKawaiiPhysics_ExternalForce_ProceduralWind* GetSourceWind(FAnimNode_KawaiiPhysics& AnimNode,
	const int32 Index = 0)
{
	return AnimNode.ExternalForces[Index].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
}

// GU-9 / TW-GU-16 / TW-GU-18: 修正前は一時突風の Time が 0 から独立に進み、ノイズ方向が元の風からずれる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritNoiseTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.NoiseAndClocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritNoiseTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const bool bRealTime : {false, true})
	{
		KawaiiPhysicsNodeFixture::FFixture NodeFixture(MakeOptions(2.0f));
		NodeFixture.EvaluateFrame();
		bOk &= TestTrue(TEXT("ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
		NodeFixture.Node.RequestTransientGust(10.0f, 0.0f, 0.5f, FVector::ZeroVector, 0,
			1.0f, 0, bRealTime);
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			NodeFixture.EvaluateFrame();
			auto* SourceWind = GetSourceWind(NodeFixture.Node);
			auto* GustWind = GetTransientWind(NodeFixture.Node);
			bOk &= TestTrue(TEXT("元と一時突風がある"), SourceWind && GustWind);
			if (!SourceWind || !GustWind) { return false; }
			bOk &= TestTrue(TEXT("対照の一時突風が吹く"), GustWind->RuntimeState->CachedGust > 0.0f);
			bOk &= TestTrue(TEXT("風の時刻が一致"), FMath::IsNearlyEqual(
				GustWind->RuntimeState->Time, SourceWind->RuntimeState->Time, 1.0e-6f));
			bOk &= TestTrue(TEXT("実秒の時刻が一致"), FMath::IsNearlyEqual(
				GustWind->RuntimeState->UnscaledTime, SourceWind->RuntimeState->UnscaledTime, 1.0e-6f));
			// 同じ時刻・Seed を通すが、ベクトルの正規化による丸めを 1e-5 だけ許す。
			bOk &= TestTrue(TEXT("ノイズ方向が一致"), GustWind->RuntimeState->CachedWindVector.Equals(
				SourceWind->RuntimeState->CachedWindVector, 1.0e-5f));
		}
	}
	return bOk;
}

// GU-9 / GU-9 補足: 修正前は Shared の新 serial 採用後も一時突風が独立時計で進み、元の時刻と一致しない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritSharedTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.SharedSerial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritSharedTest::RunTest(const FString& Parameters)
{
	auto NodeOptions = MakeOptions(1.0f);
	NodeOptions.bPreUpdateEveryFrame = true;
	NodeOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
		auto& SourceWind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode);
		SourceWind.WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
		SourceWind.SharedWindTag = TAG_KawaiiPhysicsGustInherit;
	};
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(NodeOptions);
	FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
	Publisher.SharedGroupTag = TAG_KawaiiPhysicsGustInherit;
	Publisher.SharedWind.TimeScale = 2.0f;
	Publisher.SharedWind.EnsureRuntimeState();
	auto Publish = [&](const double Time)
	{
		NodeFixture.World->TimeSeconds = Time;
		Publisher.PreUpdate(NodeFixture.Anim);
		FAnimInstanceProxy PublisherProxy(NodeFixture.Anim);
		FAnimationUpdateContext Context(&PublisherProxy, 0.1f);
		Publisher.Update_AnyThread(Context);
	};
	Publish(0.1);
	NodeFixture.EvaluateFrame();
	auto* SourceWind = GetSourceWind(NodeFixture.Node);
	bool bOk = TestTrue(TEXT("対照: Shared が解決"), NodeFixture.Node.ModifyBones.Num() > 0 &&
		SourceWind->RuntimeState->SharedPublisherEntry.IsValid());
	NodeFixture.Node.RequestTransientGust(10.0f, 0.0f, 1.0f, FVector::ZeroVector, 0, 2.0f);
	Publish(0.2);
	NodeFixture.EvaluateFrame();
	auto* GustWind = GetTransientWind(NodeFixture.Node);
	bOk &= TestTrue(TEXT("対照: 突風が吹く"), GustWind && GustWind->RuntimeState->CachedGust > 0.0f);
	if (GustWind)
	{
		bOk &= TestTrue(TEXT("新 serial の Time を写す"), FMath::IsNearlyEqual(
			GustWind->RuntimeState->Time, SourceWind->RuntimeState->Time, 1.0e-6f));
		bOk &= TestTrue(TEXT("新 serial の実秒も写す"), FMath::IsNearlyEqual(
			GustWind->RuntimeState->UnscaledTime, SourceWind->RuntimeState->UnscaledTime, 1.0e-6f));
	}
	return bOk;
}

// GU-9 補足: 修正前は元を追っていないため、無効化後の時刻継続を元との一致から検証できない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritDetachTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.Detach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritDetachTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(MakeOptions(2.0f));
	NodeFixture.EvaluateFrame();
	NodeFixture.Node.RequestTransientGust(10.0f, 0.0f, 1.0f, FVector::ZeroVector, 0, 1.0f);
	NodeFixture.EvaluateFrame();
	auto* GustWind = GetTransientWind(NodeFixture.Node);
	bool bOk = TestTrue(TEXT("対照の一時突風"), NodeFixture.Node.ModifyBones.Num() > 0 && GustWind &&
		GustWind->RuntimeState->CachedGust > 0.0f);
	if (!GustWind) { return false; }
	const float PreviousTime = GustWind->RuntimeState->Time;
	GetSourceWind(NodeFixture.Node)->bIsEnabled = false;
	NodeFixture.EvaluateFrame();
	GustWind = GetTransientWind(NodeFixture.Node);
	bOk &= TestTrue(TEXT("無効化後も一時突風がある"), GustWind != nullptr);
	if (!GustWind) { return false; }
	bOk &= TestTrue(TEXT("最後の時刻から自分の TimeScale で進む"), FMath::IsNearlyEqual(
		GustWind->RuntimeState->Time, PreviousTime + 0.2f, 1.0e-5f));
	bOk &= TestTrue(TEXT("参照を捨てる"), NodeFixture.Node.TransientForceStore.Items[0].SourceWind == nullptr);
	KawaiiPhysicsNodeFixture::FFixture RemovedFixture(MakeOptions(2.0f));
	RemovedFixture.EvaluateFrame();
	RemovedFixture.Node.RequestTransientGust(10.0f, 0.0f, 1.0f, FVector::ZeroVector, 0, 1.0f);
	RemovedFixture.EvaluateFrame();
	auto* RemovedGust = GetTransientWind(RemovedFixture.Node);
	bOk &= TestTrue(TEXT("対照: 除去前の突風"), RemovedGust && RemovedGust->RuntimeState->CachedGust > 0.0f);
	if (!RemovedGust) { return false; }
	const float TimeBeforeRemoval = RemovedGust->RuntimeState->Time;
	RemovedFixture.Node.ExternalForces.RemoveAt(0);
	RemovedFixture.EvaluateFrame();
	RemovedGust = GetTransientWind(RemovedFixture.Node);
	bOk &= TestTrue(TEXT("元の配列要素を除去しても続く"), RemovedGust && FMath::IsNearlyEqual(
		RemovedGust->RuntimeState->Time, TimeBeforeRemoval + 0.2f, 1.0e-5f));
	bOk &= TestTrue(TEXT("除去された参照を捨てる"), RemovedFixture.Node.TransientForceStore.Items[0].SourceWind == nullptr);
	return bOk;
}

// GU-9 補足: 修正前は 2 つの一時突風がそれぞれ元の時刻を写さず、独立の Time から始まる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritAllTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.AllWinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritAllTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(MakeOptions(2.0f, true));
	NodeFixture.EvaluateFrame();
	NodeFixture.Node.RequestTransientGust(10.0f, 0.0f, 1.0f, FVector::ZeroVector,
		FAnimNode_KawaiiPhysics::TransientGustInheritAllWinds, 1.0f);
	NodeFixture.EvaluateFrame();
	bool bOk = TestTrue(TEXT("対照の 2 つの突風"), NodeFixture.Node.ModifyBones.Num() > 0 &&
		NodeFixture.Node.TransientForceStore.Items.Num() == 2);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		auto* GustWind = GetTransientWind(NodeFixture.Node, Index);
		auto* SourceWind = GetSourceWind(NodeFixture.Node, Index);
		bOk &= TestTrue(TEXT("各突風が吹く"), GustWind && GustWind->RuntimeState->CachedGust > 0.0f);
		if (GustWind)
		{
			bOk &= TestTrue(TEXT("それぞれ自分の元の時刻"), FMath::IsNearlyEqual(
				GustWind->RuntimeState->Time, SourceWind->RuntimeState->Time, 1.0e-6f));
		}
	}
	return bOk;
}

// GU-9 補足: 修正前は実秒の一時突風だけ TimeScale=1 で warm-up を進み、元の TimeScale=2 の時刻とずれる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritWarmUpTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.WarmUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritWarmUpTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	NodeOptions.FrameDt = 0.1f;
	NodeOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 3;
		AnimNode.Gravity = FVector::ZeroVector;
		auto& SourceWind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode);
		SourceWind.TimeScale = 2.0f;
		SourceWind.WindDirection = FVector::ForwardVector;
	};
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(NodeOptions);
	NodeFixture.Node.RequestTransientGust(10.0f, 0.0f, 1.0f, FVector::ZeroVector, 0, 2.0f, 0, true);
	NodeFixture.EvaluateFrame();
	auto* GustWind = GetTransientWind(NodeFixture.Node);
	auto* SourceWind = GetSourceWind(NodeFixture.Node);
	bool bOk = TestTrue(TEXT("対照の風と ModifyBones"), NodeFixture.Node.ModifyBones.Num() > 0 && GustWind &&
		GustWind->RuntimeState->CachedGust > 0.0f);
	if (GustWind)
	{
		bOk &= TestTrue(TEXT("warm-up 後も元の Time を写す"), FMath::IsNearlyEqual(
			GustWind->RuntimeState->Time, SourceWind->RuntimeState->Time, 1.0e-6f));
	}
	return bOk;
}

// GU-7: 修正前は TimeScale=0 の巨大な有限寿命が残り、再開後も終了から 0.2 秒で片付かない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritPausedTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.PausedLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritPausedTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(MakeOptions(0.0f));
	NodeFixture.EvaluateFrame();
	NodeFixture.Node.RequestTransientGust(10.0f, 0.0f, 0.5f, FVector::ZeroVector, 0, 0.5f);
	NodeFixture.EvaluateFrame();
	bool bOk = TestTrue(TEXT("対照の突風"), NodeFixture.Node.ModifyBones.Num() > 0 && GetTransientWind(NodeFixture.Node));
	for (int32 Frame = 0; Frame < 100; ++Frame) { NodeFixture.EvaluateFrame(); }
	bOk &= TestEqual(TEXT("停止中は残る"), NodeFixture.Node.TransientForceStore.Items.Num(), 1);
	GetSourceWind(NodeFixture.Node)->TimeScale = 1.0f;
	for (int32 Frame = 0; Frame < 14; ++Frame) { NodeFixture.EvaluateFrame(); }
	bOk &= TestEqual(TEXT("終了と 0.2 秒後に片付く"), NodeFixture.Node.TransientForceStore.Items.Num(), 0);
	return bOk;
}

// F2: 修正前は NaN Stop が RemainingLifetime に入り、一時突風が期限切れにならない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritFiniteStopTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.FiniteStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritFiniteStopTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(MakeOptions(1.0f));
	NodeFixture.EvaluateFrame();
	bool bOk = TestTrue(TEXT("ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
	for (const float Invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		// RequestTransientGust はハンドルを採番しない（0 のままだと Stop が無視される）ので、ここで発行して渡す。
		const int64 HandleId = NodeFixture.Node.RequestTransientGust(10.0f, 0.0f, 1.0f,
			FVector::ZeroVector, 0, 1.0f, FAnimNode_KawaiiPhysics::GenerateTransientHandleId());
		bOk &= TestTrue(TEXT("対照: ハンドルが発行される"), HandleId != 0);
		NodeFixture.EvaluateFrame();
		bOk &= TestTrue(TEXT("対照の突風がある"), GetTransientWind(NodeFixture.Node) != nullptr);
		NodeFixture.Node.RequestStopTransientExternalForce(HandleId, Invalid);
		NodeFixture.EvaluateFrame();
		bOk &= TestEqual(TEXT("非有限 Stop は即時除去"), NodeFixture.Node.TransientForceStore.Items.Num(), 0);
	}
	FInstancedStruct GenericForce = FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>();
	const int64 GenericHandle = NodeFixture.Node.RequestTransientExternalForce(MoveTemp(GenericForce), 5.0f);
	NodeFixture.EvaluateFrame();
	bOk &= TestEqual(TEXT("対照の汎用外力"), NodeFixture.Node.TransientForceStore.Items.Num(), 1);
	NodeFixture.Node.RequestStopTransientExternalForce(GenericHandle, std::numeric_limits<float>::quiet_NaN());
	NodeFixture.EvaluateFrame();
	bOk &= TestEqual(TEXT("汎用外力も NaN Stop で即時"), NodeFixture.Node.TransientForceStore.Items.Num(), 0);
	NodeFixture.Node.RequestTransientGust(std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f,
		FVector::ZeroVector, 0, 1.0f);
	NodeFixture.EvaluateFrame();
	bOk &= TestEqual(TEXT("F2: 異常な開始は一時スロットを取らない"), NodeFixture.Node.TransientForceStore.Items.Num(), 0);
	return bOk;
}

// GU-4 / TW-GU-31 / F2: 修正前は継承元なしの突風は吹くが、NaN Stop 後も寿命が NaN となって残る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritNoSourceTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.NoSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritNoSourceTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	NodeOptions.FrameDt = 0.1f;
	NodeOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
	};
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(NodeOptions);
	const int64 HandleId = NodeFixture.Node.RequestTransientGust(5.0f, 0.0f, 1.0f, FVector::ZeroVector,
		INDEX_NONE, 0.0f, FAnimNode_KawaiiPhysics::GenerateTransientHandleId());
	NodeFixture.EvaluateFrame();
	auto* GustWind = GetTransientWind(NodeFixture.Node);
	bool bOk = TestTrue(TEXT("ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
	bOk &= TestTrue(TEXT("継承元なしでも吹く"), GustWind && GustWind->RuntimeState->CachedGust > 0.0f);
	bOk &= TestTrue(TEXT("参照を持たない"), NodeFixture.Node.TransientForceStore.Items.Num() == 1 &&
		NodeFixture.Node.TransientForceStore.Items[0].SourceWind == nullptr);
	NodeFixture.Node.RequestStopTransientExternalForce(HandleId, std::numeric_limits<float>::quiet_NaN());
	NodeFixture.EvaluateFrame();
	bOk &= TestEqual(TEXT("F2: 継承元なしでも NaN Stop は即時"), NodeFixture.Node.TransientForceStore.Items.Num(), 0);
	return bOk;
}

// F2（PR-5b レビュー F2）: 非有限の一時突風は、満杯時の最古破棄より前に弾く。
// 修正前は正常な 8 件の後の NaN 要求が最古の 1 件を押し出し、NaN も構築時に捨てられて 7 件になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritInvalidRequestTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.InvalidRequestKeepsQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritInvalidRequestTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics AnimNode;
	for (int32 Index = 0; Index < FAnimNode_KawaiiPhysics::MaxTransientExternalForces; ++Index)
	{
		AnimNode.RequestTransientGust(1.0f + Index, 0.0f, 1.0f, FVector::ForwardVector, INDEX_NONE, 0.0f,
			FAnimNode_KawaiiPhysics::GenerateTransientHandleId());
	}
	AnimNode.RequestTransientGust(std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f, FVector::ForwardVector,
		INDEX_NONE, 0.0f, FAnimNode_KawaiiPhysics::GenerateTransientHandleId());
	AnimNode.ConsumeAndRemoveExpiredTransientExternalForces(0.0f);
	bool bOk = TestEqual(TEXT("正常な 8 件が残る"), AnimNode.TransientForceStore.Items.Num(),
		FAnimNode_KawaiiPhysics::MaxTransientExternalForces);
	auto* OldestWind = GetTransientWind(AnimNode, 0);
	bOk &= TestTrue(TEXT("最古の一時突風がある"), OldestWind && OldestWind->RuntimeState.IsValid() &&
		OldestWind->RuntimeState->PendingGusts.Num == 1);
	if (OldestWind && OldestWind->RuntimeState.IsValid() && OldestWind->RuntimeState->PendingGusts.Num == 1)
	{
		bOk &= TestEqual(TEXT("最古（強さ 1）が押し出されていない"),
			OldestWind->RuntimeState->PendingGusts.Items[0].Gust.Strength, 1.0f);
	}
	return bOk;
}

// TW-GU-12 / GU-7（PR-5b レビュー F3・最終検収 C2）: 一時突風の寿命は中の突風が吹き始めてから数え、
// まだ一度も評価されていない（Simulate が飛ばされている）間は減らさない。
// 修正前（master）は構築時に寿命 (0 + 0 + 0.05) / 1 + 0.2 = 0.25 秒を決め打ちし、評価の有無に関係なく減らすので、
// 評価されないフレーム（WorldSpace の TeleportPhysics など）が 0.3 秒続くと一度も吹かずに消える。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritUnevaluatedTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.UnevaluatedGustSurvivesSkippedFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritUnevaluatedTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics AnimNode;
	AnimNode.RequestTransientGust(5.0f, 0.0f, 0.05f, FVector::ForwardVector, INDEX_NONE, 0.0f,
		FAnimNode_KawaiiPhysics::GenerateTransientHandleId());
	AnimNode.ConsumeAndRemoveExpiredTransientExternalForces(0.0f);
	bool bOk = TestEqual(TEXT("対照: 一時突風ができる"), AnimNode.TransientForceStore.Items.Num(), 1);
	// 対照: 汎用の一時外力は寿命どおり減る（0.25 − 0.1 × 3 < 0 で消える）
	AnimNode.RequestTransientExternalForce(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>(), 0.25f);
	AnimNode.ConsumeAndRemoveExpiredTransientExternalForces(0.0f);
	bOk &= TestEqual(TEXT("対照: 汎用外力を足して 2 件"), AnimNode.TransientForceStore.Items.Num(), 2);
	// PreApply を通らない（SimulateModifyBones が飛ばされる）フレームを 0.1 秒 × 3
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		AnimNode.ConsumeAndRemoveExpiredTransientExternalForces(0.1f);
	}
	bOk &= TestEqual(TEXT("未評価の突風だけ残る"), AnimNode.TransientForceStore.Items.Num(), 1);
	bOk &= TestTrue(TEXT("残ったのは一時突風"), AnimNode.TransientForceStore.Items.Num() == 1 &&
		AnimNode.TransientForceStore.Items[0].bIsTransientGust);
	// 残った枠が空でないこと: 要求を取り込むと、Rise=0 なので τ=0 で強さ 5 の突風が吹く
	auto* GustWind = GetTransientWind(AnimNode, 0);
	bOk &= TestTrue(TEXT("一時突風の風がある"), GustWind && GustWind->RuntimeState.IsValid());
	if (GustWind && GustWind->RuntimeState.IsValid())
	{
		GustWind->ConsumePendingRequests();
		bOk &= TestEqual(TEXT("取り込むと突風が吹く"),
			GustWind->ComputeWindSample(GustWind->RuntimeState->Time).Gust, 5.0f);
	}
	return bOk;
}

// GU-9 / TW-PW-44 / TW-GU-09 / TW-GU-18: 継承した一時突風のエンベロープの時計。
// 元の風は TimeScale=2、フレームは 0.1 秒。元を 1 フレーム進めてから（風の時間 0.2・実秒 0.1）突風を要求する。
// (a) 風の時間の突風（Decay 0.5）: 最初の評価で開始時刻 0.2、時刻 0.4 → τ = 0.2 → 10 × (1 − 0.2 / 0.5) = 6。
//     修正前（master）は一時突風の時計が 0 から独立に進み、TimeScale も継承するので τ = 0.2 で同じ 6 になるが、
//     最初の評価で元の時刻から始めないと（開始時刻 0）τ = 0.4 → 2 になる。
// (b) 実秒の突風（Decay 1.0）: 実秒は元の UnscaledTime（TimeScale に依らない）で進むので、5 フレーム後に τ = 0.5 → 5。
//     修正前（master）も一時突風の TimeScale を 1 にして同じ 5 になる。この PR で実秒を UnscaledTime で評価する形に
//     変えたので、その経路を守る（風の時間で評価すると τ = 1.0 で 0。TW-GU-18: 継承した TimeScale は実秒の突風では使わない）。
// どちらも master と同じ値になる回帰の防壁（修正前に落ちるテストではない）。PR5b_FixRevert の 3100 / 8619 を落とす。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransientGustInheritEnvelopeClockTest,
	"KawaiiPhysics.ProceduralWind.GustInherit.EnvelopeClocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransientGustInheritEnvelopeClockTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	{
		KawaiiPhysicsNodeFixture::FFixture NodeFixture(MakeOptions(2.0f));
		NodeFixture.EvaluateFrame();
		bOk &= TestTrue(TEXT("ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
		NodeFixture.Node.RequestTransientGust(10.0f, 0.0f, 0.5f, FVector::ZeroVector, 0, 0.0f,
			FAnimNode_KawaiiPhysics::GenerateTransientHandleId(), false);
		NodeFixture.EvaluateFrame();
		auto* GustWind = GetTransientWind(NodeFixture.Node);
		bOk &= TestTrue(TEXT("風の時間の一時突風がある"), GustWind != nullptr);
		if (GustWind)
		{
			bOk &= TestTrue(TEXT("最初のサンプルは τ = dt × TimeScale"),
				FMath::IsNearlyEqual(GustWind->RuntimeState->CachedGust, 6.0f, 1.0e-4f));
		}
	}
	{
		KawaiiPhysicsNodeFixture::FFixture NodeFixture(MakeOptions(2.0f));
		NodeFixture.EvaluateFrame();
		NodeFixture.Node.RequestTransientGust(10.0f, 0.0f, 1.0f, FVector::ZeroVector, 0, 0.0f,
			FAnimNode_KawaiiPhysics::GenerateTransientHandleId(), true);
		for (int32 Frame = 0; Frame < 5; ++Frame)
		{
			NodeFixture.EvaluateFrame();
		}
		auto* GustWind = GetTransientWind(NodeFixture.Node);
		bOk &= TestTrue(TEXT("実秒の一時突風が残る"), GustWind != nullptr);
		if (GustWind)
		{
			bOk &= TestTrue(TEXT("実秒 0.5 で半分"),
				FMath::IsNearlyEqual(GustWind->RuntimeState->CachedGust, 5.0f, 1.0e-4f));
		}
	}
	return bOk;
}

} // namespace KawaiiPhysicsTransientGustInheritTestPrivate

#endif
