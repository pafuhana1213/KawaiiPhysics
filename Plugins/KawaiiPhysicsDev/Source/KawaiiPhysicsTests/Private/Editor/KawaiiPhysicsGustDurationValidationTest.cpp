// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsAnimBlueprintFixture.h"
#include "KawaiiPhysicsTypes.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace KawaiiPhysicsGustDurationValidationTestPrivate
{

static KawaiiPhysicsAnimBlueprintFixture::FOptions MakeGustOptions()
{
	KawaiiPhysicsAnimBlueprintFixture::FOptions BlueprintOptions;
	BlueprintOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	BlueprintOptions.ConfigureNode = [](FAnimNode_KawaiiPhysics& AnimNode, const int32)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
	};
	return BlueprintOptions;
}

// GU-6 / TW-GU-35: 修正前は Duration 0 と負値でも要求が積まれ、OutHandle が非 0 になり、一時外力のスロットを消費した。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustDurationRejectedTest,
	"KawaiiPhysics.ProceduralWind.GustDurationValidation.Rejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustDurationRejectedTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsAnimBlueprintFixture::FFixture BlueprintFixture(MakeGustOptions());
	if (!TestTrue(TEXT("AnimBlueprint を準備"), BlueprintFixture.IsReady()))
	{
		return false;
	}
	const TArray<FKawaiiPhysicsReference> References = BlueprintFixture.GetNodes();
	if (!TestEqual(TEXT("参照ノードが 1 つ"), References.Num(), 1))
	{
		return false;
	}
	KawaiiPhysics::ResetInvalidGustDurationWarningForTests();
	AddExpectedMessage(TEXT("Invalid gust Duration"), EAutomationExpectedMessageFlags::Contains, 1);
	bool bOk = true;
	for (const float Duration : {0.0f, -1.0f})
	{
		for (int32 Attempt = 0; Attempt < 2; ++Attempt)
		{
			FKawaiiPhysicsTransientHandle GustHandle;
			GustHandle.Id = 987;
			const int32 Count = UKawaiiPhysicsLibrary::StartProceduralWindGustOnComponent(
				BlueprintFixture.GetComponent(), GustHandle, 100.0f, Duration, 0.0f, 0.0f,
				FGameplayTagContainer(), false, FVector::ForwardVector, true);
			bOk &= TestTrue(TEXT("無効な Duration はノードを選ばずハンドルを消す"), Count == 0 && GustHandle.Id == 0);
		}
	}
	for (const float Duration : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		FKawaiiPhysicsTransientHandle GustHandle;
		GustHandle.Id = 987;
		bOk &= TestEqual(TEXT("非有限 Duration は拒否"), UKawaiiPhysicsLibrary::StartProceduralWindGustOnComponent(
			BlueprintFixture.GetComponent(), GustHandle, 100.0f, Duration, 0.0f, 0.0f,
			FGameplayTagContainer(), false, FVector::ForwardVector, true), 0);
		bOk &= TestEqual(TEXT("非有限 Duration でハンドルを消す"), GustHandle.Id, int64(0));
	}
	for (const float Duration : {0.0f, -1.0f})
	{
		EKawaiiPhysicsAccessExternalForceResult ExecResult = EKawaiiPhysicsAccessExternalForceResult::Valid;
		FKawaiiPhysicsTransientHandle GustHandle;
		GustHandle.Id = 987;
		UKawaiiPhysicsLibrary::StartProceduralWindGust(ExecResult, GustHandle, References[0],
			100.0f, Duration, 0.0f, 0.0f, FVector::ForwardVector);
		bOk &= TestTrue(TEXT("参照版も拒否してハンドルを消す"),
			ExecResult == EKawaiiPhysicsAccessExternalForceResult::NotValid && GustHandle.Id == 0);
	}
	bOk &= TestTrue(TEXT("無効な要求後もフレーム評価できる"), BlueprintFixture.StepFrame());
	bOk &= TestEqual(TEXT("一時外力を作らない"),
		References[0].GetAnimNode<FAnimNode_KawaiiPhysics>().TransientForceStore.Items.Num(), 0);

	// 対照: 有効な Duration は同じフィクスチャのノードに要求を積む。
	FKawaiiPhysicsTransientHandle ValidHandle;
	const int32 Count = UKawaiiPhysicsLibrary::StartProceduralWindGustOnComponent(
		BlueprintFixture.GetComponent(), ValidHandle, 100.0f, 1.0f, 0.0f, 0.0f,
		FGameplayTagContainer(), false, FVector::ForwardVector, true);
	bOk &= TestTrue(TEXT("有効な要求はハンドルを発行"), Count > 0 && ValidHandle.Id != 0);
	bOk &= TestTrue(TEXT("有効な要求を評価"), BlueprintFixture.StepFrame());
	bOk &= TestTrue(TEXT("一時外力が作られる"),
		References[0].GetAnimNode<FAnimNode_KawaiiPhysics>().TransientForceStore.Items.Num() > 0);
	KawaiiPhysics::ResetInvalidGustDurationWarningForTests();
	return bOk;
}

// GU-6 / TW-GU-05: 負の Rise の 0 切り詰めは修正前も通る既存挙動の回帰。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGustNegativeRiseTest,
	"KawaiiPhysics.ProceduralWind.GustDurationValidation.NegativeRise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGustNegativeRiseTest::RunTest(const FString& Parameters)
{
	const KawaiiPhysics::FWindGustEnvelope Envelope = KawaiiPhysics::ResolveWindGustEnvelope(1.0f, -1.0f, 0.25f);
	return TestTrue(TEXT("Rise は 0、Hold は 0.75、Decay は 0.25"),
		Envelope.RiseTime == 0.0f && Envelope.HoldTime == 0.75f && Envelope.DecayTime == 0.25f);
}

}

#endif
