// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"

namespace KawaiiPhysicsProceduralWindGustPerfTestPrivate
{

// GU-8: 修正前は 8 回要求しても最後の 1 件だけになり、8 件の計測基準にならない。
static bool RunCase(FAutomationTestBase& Test, const TCHAR* Name, const int32 WindCount, const int32 GustCount)
{
	KawaiiPhysicsNodeFixture::FOptions NodeOptions;
	NodeOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(32);
	NodeOptions.ConfigureNode = [WindCount, GustCount](FAnimNode_KawaiiPhysics& AnimNode)
	{
		AnimNode.RootBone.BoneName = TEXT("root");
		AnimNode.WarmUpFrames = 0;
		AnimNode.Gravity = FVector::ZeroVector;
		for (int32 WindIndex = 0; WindIndex < WindCount; ++WindIndex)
		{
			auto& GustWind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(AnimNode);
			GustWind.WindDirection = FVector::ForwardVector;
			GustWind.ConstantForce = 10.0f;
			for (int32 GustIndex = 0; GustIndex < GustCount; ++GustIndex)
			{
				GustWind.RequestGust(10.0f, 0.0f, 0.0f, 1000.0f);
			}
		}
	};
	KawaiiPhysicsNodeFixture::FFixture NodeFixture(NodeOptions);
	for (int32 Frame = 0; Frame < 30; ++Frame) { NodeFixture.EvaluateFrame(); }
	bool bOk = Test.TestTrue(TEXT("ModifyBones がある"), NodeFixture.Node.ModifyBones.Num() > 0);
	TArray<double> Samples;
	for (int32 Trial = 0; Trial < 3; ++Trial)
	{
		const double Start = FPlatformTime::Seconds();
		for (int32 Frame = 0; Frame < 300; ++Frame) { NodeFixture.EvaluateFrame(); }
		const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0 / 300.0;
		Samples.Add(Milliseconds);
		Test.AddInfo(FString::Printf(TEXT("PERF_RAW KawaiiPhysics.Perf.ProceduralWindGusts.%s trial=%d ms=%.6f"),
			Name, Trial, Milliseconds));
	}
	Samples.Sort();
	Test.AddInfo(FString::Printf(TEXT("PERF KawaiiPhysics.Perf.ProceduralWindGusts.%s median_ms_per_frame=%.6f min_ms_per_frame=%.6f"),
		Name, Samples[1], Samples[0]));
	bOk &= Test.TestTrue(TEXT("出力が有限"), NodeFixture.OutTransforms.Num() > 0 &&
		!NodeFixture.OutTransforms.Last().Transform.GetLocation().ContainsNaN());
	return bOk;
}

#define KAWAII_GUST_PERF_CASE(TestType, CaseName, Winds, Gusts) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestType, "KawaiiPhysics.Perf.ProceduralWindGusts." CaseName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestType::RunTest(const FString& Parameters) \
	{ return RunCase(*this, TEXT(CaseName), Winds, Gusts); }

KAWAII_GUST_PERF_CASE(FGustPerfWinds1Gusts0, "Winds1_Gusts0", 1, 0)
KAWAII_GUST_PERF_CASE(FGustPerfWinds1Gusts1, "Winds1_Gusts1", 1, 1)
KAWAII_GUST_PERF_CASE(FGustPerfWinds1Gusts8, "Winds1_Gusts8", 1, 8)
KAWAII_GUST_PERF_CASE(FGustPerfWinds4Gusts8, "Winds4_Gusts8", 4, 8)

#undef KAWAII_GUST_PERF_CASE

} // namespace KawaiiPhysicsProceduralWindGustPerfTestPrivate

#endif
