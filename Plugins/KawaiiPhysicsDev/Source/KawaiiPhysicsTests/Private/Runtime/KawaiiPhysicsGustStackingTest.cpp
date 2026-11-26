// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "KawaiiPhysicsLibrary.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include <limits>

namespace KawaiiPhysicsGustStackingTestPrivate
{
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsGustStacking, "KawaiiPhysics.Test.Regression.A");

static FKawaiiPhysics_ExternalForce_ProceduralWind MakeWind()
{
	FKawaiiPhysics_ExternalForce_ProceduralWind Result;
	Result.TimeScale = 1.0f;
	return Result;
}

// GU-8 / TW-GU-19: 修正前は 2 件目が 1 件目を置き換え、減衰中の寄与が消える。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingDecayTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.DecayOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingDecayTest::RunTest(const FString& Parameters)
{
	auto GustWind = MakeWind();
	GustWind.RequestGust(8.0f, 0.0f, 4.0f);
	GustWind.ConsumePendingRequests();
	GustWind.AdvanceWindTime(2.0f);
	bool bOk = TestEqual(TEXT("1 件目の減衰中は 4"), GustWind.ComputeWindSample(2.0f).Gust, 4.0f);
	GustWind.RequestGust(6.0f, 0.0f, 4.0f);
	GustWind.ConsumePendingRequests();
	GustWind.AdvanceWindTime(1.0f);
	// 8*(1-3/4) + 6*(1-1/4) = 6.5
	bOk &= TestEqual(TEXT("2 件の寄与を足す"), GustWind.ComputeWindSample(3.0f).Gust, 6.5f);
	bOk &= TestEqual(TEXT("2 件が残る"), GustWind.RuntimeState->Gusts.Num, 2);
	return bOk;
}

// GU-8 / GU-8 補足: 修正前は同フレームの Start が最後の 1 件になり、Stop→Start も停止が勝つ。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingQueueOrderTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.QueueOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingQueueOrderTest::RunTest(const FString& Parameters)
{
	auto GustWind = MakeWind();
	GustWind.RequestGust(2.0f, 0.0f, 0.0f, 2.0f);
	GustWind.RequestGust(3.0f, 0.0f, 0.0f, 2.0f);
	GustWind.ConsumePendingRequests();
	bool bOk = TestEqual(TEXT("同フレームに 2 件"), GustWind.ComputeWindSample(0.1f).Gust, 5.0f);
	GustWind.RequestGustStop(0.0f);
	GustWind.RequestGust(7.0f, 0.0f, 0.0f, 2.0f);
	GustWind.ConsumePendingRequests();
	bOk &= TestEqual(TEXT("Stop の後の Start は吹く"), GustWind.ComputeWindSample(0.1f).Gust, 7.0f);
	GustWind.RequestGust(9.0f, 0.0f, 0.0f, 2.0f);
	GustWind.RequestGustStop(0.0f);
	GustWind.ConsumePendingRequests();
	bOk &= TestEqual(TEXT("Start の後の Stop は全件止める"), GustWind.RuntimeState->Gusts.Num, 0);
	for (int32 Index = 0; Index < 17; ++Index) { GustWind.RequestGustStop(0.0f); }
	GustWind.RequestGust(4.0f, 0.0f, 0.0f, 2.0f);
	GustWind.ConsumePendingRequests();
	bOk &= TestEqual(TEXT("満杯の Stop 列の後の Start を保持"), GustWind.ComputeWindSample(0.1f).Gust, 4.0f);
	return bOk;
}

// GU-8 / TW-PW-44: 修正前は同フレームの 2 要求の最後だけがノード評価で吹く。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingNodeTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.NodeEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingNodeTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	NodeOptions.FrameDt = 0.1f;
	NodeOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
		auto& GustWind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode);
		GustWind.WindDirection = FVector::ForwardVector;
		GustWind.RequestGust(2.0f, 0.0f, 0.0f, 2.0f);
		GustWind.RequestGust(3.0f, 0.0f, 0.0f, 2.0f);
	};
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(NodeOptions);
	NodeFixture.EvaluateFrame();
	bool bOk = TestTrue(TEXT("ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
	auto* GustWind = NodeFixture.Node.ExternalForces[0].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	bOk &= TestTrue(TEXT("対照の風がある"), GustWind != nullptr);
	if (GustWind)
	{
		bOk &= TestEqual(TEXT("2 件の突風が吹く"), GustWind->RuntimeState->CachedGust, 5.0f);
		bOk &= TestEqual(TEXT("2 件のスロット"), GustWind->RuntimeState->Gusts.Num, 2);
	}
	return bOk;
}

// GU-8: 修正前は 9 件のうち最後の 1 件しか残らない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingCapacityTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.Capacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingCapacityTest::RunTest(const FString& Parameters)
{
	auto GustWind = MakeWind();
	GustWind.RequestGust(1.0f, 0.0f, 0.0f, 3.0f);
	GustWind.ConsumePendingRequests();
	bool bOk = TestEqual(TEXT("対照の 1 件"), GustWind.ComputeWindSample(0.1f).Gust, 1.0f);
	for (int32 Index = 2; Index <= 9; ++Index)
	{
		if (Index == 9)
		{
			bOk &= TestEqual(TEXT("9 件目の前は 1～8 の合計"), GustWind.ComputeWindSample(0.1f).Gust, 36.0f);
		}
		GustWind.RequestGust(static_cast<float>(Index), 0.0f, 0.0f, 3.0f);
		GustWind.ConsumePendingRequests();
	}
	bOk &= TestEqual(TEXT("上限は 8"), GustWind.RuntimeState->Gusts.Num, 8);
	bOk &= TestEqual(TEXT("最古 1 を除く 2～9 の合計"), GustWind.ComputeWindSample(0.1f).Gust, 44.0f);
	return bOk;
}

// GU-8 / TW-GU-23: 修正前は実秒の Stop も風の時計に切り替わり、TimeScale=2 で半分の実時間で終わる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingStopClocksTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.StopClocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingStopClocksTest::RunTest(const FString& Parameters)
{
	auto GustWind = MakeWind();
	GustWind.TimeScale = 2.0f;
	GustWind.RequestGust(10.0f, 0.0f, 0.0f, 10.0f, true);
	GustWind.RequestGust(20.0f, 0.0f, 0.0f, 10.0f, false);
	GustWind.ConsumePendingRequests();
	GustWind.AdvanceWindTime(1.0f);
	bool bOk = TestEqual(TEXT("Stop 前は 30"), GustWind.ComputeWindSample(GustWind.RuntimeState->Time).Gust, 30.0f);
	GustWind.RequestGustStop(2.0f);
	GustWind.ConsumePendingRequests();
	bOk &= TestEqual(TEXT("Stop 時に飛ばない"), GustWind.ComputeWindSample(GustWind.RuntimeState->Time).Gust, 30.0f);
	GustWind.AdvanceWindTime(0.5f);
	bOk &= TestEqual(TEXT("実秒 7.5 + 風時間 10"), GustWind.ComputeWindSample(GustWind.RuntimeState->Time).Gust, 17.5f);
	GustWind.AdvanceWindTime(0.5f);
	bOk &= TestEqual(TEXT("風時間だけ終わる"), GustWind.ComputeWindSample(GustWind.RuntimeState->Time).Gust, 5.0f);
	return bOk;
}

// F2: 修正前は Inf Stop が全力のまま残り、NaN Strength が正常な系列も消す。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingFiniteTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.FiniteInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingFiniteTest::RunTest(const FString& Parameters)
{
	const float Infinity = std::numeric_limits<float>::infinity();
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	auto GustWind = MakeWind();
	GustWind.RequestGust(5.0f, 0.0f, 0.0f, 4.0f);
	GustWind.RequestGust(NaN, 0.0f, 0.0f, 4.0f);
	GustWind.ConsumePendingRequests();
	bool bOk = TestEqual(TEXT("正常な突風だけ残る"), GustWind.ComputeWindSample(0.1f).Gust, 5.0f);
	bOk &= TestEqual(TEXT("異常な突風はスロットを取らない"), GustWind.RuntimeState->Gusts.Num, 1);
	GustWind.RequestGust(2.0f, 0.0f, 0.0f, 4.0f);
	GustWind.ConsumePendingRequests();
	GustWind.RuntimeState->Gusts.Items[1].Strength = Infinity;
	bOk &= TestEqual(TEXT("評価中に 1 件だけ壊れても正常な寄与を保つ"), GustWind.ComputeWindSample(0.1f).Gust, 5.0f);
	GustWind.RequestGustStop(Infinity);
	GustWind.ConsumePendingRequests();
	bOk &= TestEqual(TEXT("Inf Stop は即時"), GustWind.RuntimeState->Gusts.Num, 0);
	GustWind.RequestGust(5.0f, 0.0f, 0.0f, 4.0f);
	GustWind.ConsumePendingRequests();
	GustWind.RequestGustStop(NaN);
	GustWind.ConsumePendingRequests();
	bOk &= TestEqual(TEXT("NaN Stop は即時"), GustWind.RuntimeState->Gusts.Num, 0);
	return bOk;
}

// GU-7 / TW-GU-12 / TW-GU-13: 修正前は 2 件目が 1 件目を置き換え、停止中の 2 件を保持できない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingCleanupTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.Cleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingCleanupTest::RunTest(const FString& Parameters)
{
	auto GustWind = MakeWind();
	GustWind.TimeScale = 0.0f;
	GustWind.RequestGust(4.0f, 0.0f, 1.0f, 1.0f);
	GustWind.RequestGust(6.0f, 0.0f, 1.0f, 1.0f);
	GustWind.ConsumePendingRequests();
	bool bOk = TestEqual(TEXT("対照の突風"), GustWind.ComputeWindSample(0.0f).Gust, 10.0f);
	for (int32 Frame = 0; Frame < 100; ++Frame) { GustWind.AdvanceWindTime(0.1f); GustWind.RemoveExpiredGusts(); }
	bOk &= TestEqual(TEXT("風の時計が止まる間は残る"), GustWind.RuntimeState->Gusts.Num, 2);
	GustWind.TimeScale = 1.0f;
	GustWind.AdvanceWindTime(2.0f);
	GustWind.RemoveExpiredGusts();
	bOk &= TestEqual(TEXT("終了後は空"), GustWind.RuntimeState->Gusts.Num, 0);
	bOk &= TestEqual(TEXT("基準風へ戻る"), GustWind.ComputeWindSample(GustWind.RuntimeState->Time).Gust, 0.0f);
	return bOk;
}

// SH-7 / TW-SH-18 / TW-SH-20 / TW-SH-21 / F2: 修正前は Publisher の 2 件目が 1 件目を置き換え、Inf Stop は全力のまま残る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingSharedTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.SharedPublisher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingSharedTest::RunTest(const FString& Parameters)
{
	UWorld* TestWorld = KawaiiPhysicsNodeFixture::MakeWorld();
	AActor* TestActor = TestWorld->SpawnActor<AActor>();
	USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(TestActor);
	TestActor->AddInstanceComponent(MeshComponent);
	TestActor->SetRootComponent(MeshComponent);
	UAnimInstance* AnimInstance = NewObject<UAnimInstance>(MeshComponent);
	FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
	Publisher.SharedGroupTag = TAG_KawaiiPhysicsGustStacking;
	Publisher.SharedWind.EnsureRuntimeState();
	Publisher.SharedWind.TimeScale = 2.0f;
	auto Publish = [&](const double Time)
	{
		TestWorld->TimeSeconds = Time;
		Publisher.PreUpdate(AnimInstance);
		FAnimInstanceProxy PublisherProxy(AnimInstance);
		FAnimationUpdateContext Context(&PublisherProxy, 0.0f);
		Publisher.Update_AnyThread(Context);
	};
	Publish(0.0);
	auto Entry = Publisher.GetSharedPublisherEntry();
	bool bOk = TestTrue(TEXT("対照: Publisher を解決"), Entry.IsValid());
	if (!Entry.IsValid()) { TestWorld->DestroyWorld(false); return false; }
	Entry->RequestGust(3.0f, 0.0f, 0.0f, 4.0f, true);
	Entry->RequestGust(5.0f, 0.0f, 0.0f, 4.0f, false);
	Publish(0.1);
	FKawaiiPhysicsTestAccessor AccessorA;
	FKawaiiPhysicsTestAccessor AccessorB;
	FKawaiiPhysics_ExternalForce_ProceduralWind ConsumerA;
	FKawaiiPhysics_ExternalForce_ProceduralWind ConsumerB;
	for (auto* Consumer : {&ConsumerA, &ConsumerB})
	{
		Consumer->WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
		Consumer->SharedWindTag = TAG_KawaiiPhysicsGustStacking;
	}
	auto Consume = [&](FKawaiiPhysicsTestAccessor& Accessor, FKawaiiPhysics_ExternalForce_ProceduralWind& Consumer)
	{
		Accessor.Node.PreUpdate(AnimInstance);
		Accessor.SetTimeState(0.1f, 0.1f);
		FAnimInstanceProxy ConsumerProxy(AnimInstance);
		FComponentSpacePoseContext Context(&ConsumerProxy);
		Consumer.PreApply(Accessor.Node, Context);
	};
	Consume(AccessorA, ConsumerA);
	Consume(AccessorB, ConsumerB);
	bOk &= TestEqual(TEXT("対照: Publisher で 2 件が吹く"), Publisher.SharedWind.RuntimeState->Gusts.Num, 2);
	bOk &= TestEqual(TEXT("消費側 A が 2 件採用"), ConsumerA.RuntimeState->Gusts.Num, 2);
	bOk &= TestEqual(TEXT("消費側 B が 2 件採用"), ConsumerB.RuntimeState->Gusts.Num, 2);
	bOk &= TestEqual(TEXT("2 消費側の寄与が一致"), ConsumerA.RuntimeState->CachedGust, ConsumerB.RuntimeState->CachedGust);
	bOk &= TestEqual(TEXT("Publisher と寄与が一致"), ConsumerA.RuntimeState->CachedGust,
		Publisher.SharedWind.ComputeWindSample(Publisher.SharedWind.RuntimeState->Time).Gust);
	Entry->RequestGustStop(2.0f);
	Publish(0.2);
	Consume(AccessorA, ConsumerA);
	Consume(AccessorB, ConsumerB);
	bOk &= TestEqual(TEXT("Stop の瞬間は飛ばない"), ConsumerA.RuntimeState->CachedGust, 8.0f);
	Publish(1.2);
	Consume(AccessorA, ConsumerA);
	Consume(AccessorB, ConsumerB);
	bOk &= TestEqual(TEXT("実秒だけ半分残る"), ConsumerA.RuntimeState->CachedGust, 1.5f);
	bOk &= TestEqual(TEXT("消費側は同じフェード"), ConsumerB.RuntimeState->CachedGust, 1.5f);
	bOk &= TestTrue(TEXT("Stop API が Publisher を解決"), UKawaiiPhysicsLibrary::StopProceduralWindGustOnSharedPublisher(
		TestActor, TAG_KawaiiPhysicsGustStacking, std::numeric_limits<float>::infinity()));
	Publish(1.3);
	Consume(AccessorA, ConsumerA);
	Consume(AccessorB, ConsumerB);
	bOk &= TestEqual(TEXT("F2: Inf Stop は Publisher で即時"), Publisher.SharedWind.RuntimeState->Gusts.Num, 0);
	bOk &= TestEqual(TEXT("F2: 消費側 A も即時"), ConsumerA.RuntimeState->CachedGust, 0.0f);
	bOk &= TestEqual(TEXT("F2: 消費側 B も即時"), ConsumerB.RuntimeState->CachedGust, 0.0f);
	Entry->RequestGust(4.0f, 0.0f, 0.0f, 2.0f);
	Entry->RequestGustStop(0.0f);
	Publish(1.4);
	Consume(AccessorA, ConsumerA);
	bOk &= TestEqual(TEXT("同フレーム Start→Stop は吹かない"), ConsumerA.RuntimeState->CachedGust, 0.0f);
	Entry->RequestGustStop(0.0f);
	Entry->RequestGust(4.0f, 0.0f, 0.0f, 2.0f);
	Publish(1.5);
	Consume(AccessorA, ConsumerA);
	bOk &= TestEqual(TEXT("同フレーム Stop→Start は吹く"), ConsumerA.RuntimeState->CachedGust, 4.0f);
	ConsumerA.RequestGustStop(0.0f);
	ConsumerA.RequestGust(6.0f, 0.0f, 0.0f, 2.0f);
	Consume(AccessorA, ConsumerA);
	Publish(1.6);
	Consume(AccessorB, ConsumerB);
	bOk &= TestEqual(TEXT("消費側からの Stop→Start も到着順で転送"), ConsumerB.RuntimeState->CachedGust, 6.0f);
	TestWorld->DestroyWorld(false);
	return bOk;
}

// F2（PR-5b レビュー F2）: 非有限の Start は列の空きを作る前に弾く。
// 修正前は満杯の列で、正常な Start が最古の Start として押し出され、NaN も取り込み時に捨てられて何も吹かない（寄与 0）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingInvalidStartKeepsQueueTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.InvalidStartKeepsQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingInvalidStartKeepsQueueTest::RunTest(const FString& Parameters)
{
	const float Infinity = std::numeric_limits<float>::infinity();
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	auto GustWind = MakeWind();
	// 要求の列を Stop（突風が無いので何もしない）と正常な Start 1 件で満杯にする
	for (int32 Index = 0; Index < FKawaiiProceduralWindPendingGusts::MaxRequests - 1; ++Index)
	{
		GustWind.RequestGustStop(0.5f);
	}
	GustWind.RequestGust(5.0f, 0.0f, 0.0f, 2.0f);
	bool bOk = TestEqual(TEXT("対照: 列が満杯"), GustWind.RuntimeState->PendingGusts.Num,
		FKawaiiProceduralWindPendingGusts::MaxRequests);
	GustWind.RequestGust(NaN, 0.0f, 0.0f, 2.0f);
	GustWind.RequestGust(1.0f, Infinity, 0.0f, 2.0f);
	bOk &= TestEqual(TEXT("非有限の Start は列を変えない"), GustWind.RuntimeState->PendingGusts.Num,
		FKawaiiProceduralWindPendingGusts::MaxRequests);
	GustWind.ConsumePendingRequests();
	// Rise=Decay=0、Hold=2 の矩形なので τ=0.1 で強さ 5 のまま
	bOk &= TestEqual(TEXT("正常な Start が吹く"), GustWind.ComputeWindSample(0.1f).Gust, 5.0f);
	return bOk;
}

// GU-8（PR-5b レビュー F1）: Publisher は Update の冒頭で時計を進めるので、取り込みの前に終わった突風を外す。
// 修正前は 8 件のうち 2 件目が終わったフレームに 9 件目が来ると、上限の判定で吹いている最古の 1 件目を捨て、
// その後で 2 件目を片付けるので 7 件（合計 3+…+8+100 = 133）になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustStackingSharedCapacityAfterExpiryTest,
	"KawaiiPhysics.ProceduralWind.GustStacking.SharedCapacityAfterExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustStackingSharedCapacityAfterExpiryTest::RunTest(const FString& Parameters)
{
	UWorld* TestWorld = KawaiiPhysicsNodeFixture::MakeWorld();
	AActor* TestActor = TestWorld->SpawnActor<AActor>();
	USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(TestActor);
	TestActor->AddInstanceComponent(MeshComponent);
	TestActor->SetRootComponent(MeshComponent);
	UAnimInstance* AnimInstance = NewObject<UAnimInstance>(MeshComponent);
	FAnimNode_KawaiiPhysicsSharedPublisher Publisher;
	Publisher.SharedGroupTag = TAG_KawaiiPhysicsGustStacking;
	Publisher.SharedWind.EnsureRuntimeState();
	Publisher.SharedWind.TimeScale = 1.0f;
	auto Publish = [&](const double Time)
	{
		TestWorld->TimeSeconds = Time;
		Publisher.PreUpdate(AnimInstance);
		FAnimInstanceProxy PublisherProxy(AnimInstance);
		FAnimationUpdateContext Context(&PublisherProxy, 0.0f);
		Publisher.Update_AnyThread(Context);
	};
	Publish(0.0);
	auto Entry = Publisher.GetSharedPublisherEntry();
	bool bOk = TestTrue(TEXT("対照: Publisher を解決"), Entry.IsValid());
	if (!Entry.IsValid())
	{
		TestWorld->DestroyWorld(false);
		return false;
	}
	// 実秒の矩形（Rise=Decay=0）の突風 8 件。強さは 1〜8、2 件目だけ Hold 0.5、他は Hold 10
	for (int32 Index = 1; Index <= 8; ++Index)
	{
		Entry->RequestGust(static_cast<float>(Index), 0.0f, 0.0f, Index == 2 ? 0.5f : 10.0f, true);
	}
	Publish(0.1);
	const FKawaiiProceduralWindRuntimeState& PublisherState = *Publisher.SharedWind.RuntimeState;
	bOk &= TestEqual(TEXT("対照: 8 件"), PublisherState.Gusts.Num, 8);
	bOk &= TestEqual(TEXT("対照: 1〜8 の合計"), Publisher.SharedWind.ComputeWindSample(PublisherState.Time).Gust, 36.0f);
	// 2 件目は 0.1 + 0.5 = 0.6 秒で終わる。1.0 秒のフレームで 9 件目（強さ 100）
	Entry->RequestGust(100.0f, 0.0f, 0.0f, 10.0f, true);
	Publish(1.0);
	bOk &= TestEqual(TEXT("終わった 2 件目だけが外れ 8 件"), PublisherState.Gusts.Num, 8);
	// 36 − 2 + 100 = 134
	bOk &= TestEqual(TEXT("吹いている 1 件目は残る"), Publisher.SharedWind.ComputeWindSample(PublisherState.Time).Gust, 134.0f);
	TestWorld->DestroyWorld(false);
	return bOk;
}

} // namespace KawaiiPhysicsGustStackingTestPrivate

#endif
