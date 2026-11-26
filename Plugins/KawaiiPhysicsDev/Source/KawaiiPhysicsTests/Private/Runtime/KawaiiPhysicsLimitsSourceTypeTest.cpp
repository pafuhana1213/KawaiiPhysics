// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

// PR-14・BX-11。修正前は既定の AnimNode がコピーに残り、再取り込みで五種類の件数が増える。
// 以下の件数・DataAsset の SourceType・nullptr での除去の主張が落ちることをコードから確認（実行なし）。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsNodeFixture.h"

namespace KawaiiPhysicsLimitsSourceTypeTestPrivate
{

TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> MakeFiveShapeAsset()
{
	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> Asset(NewObject<UKawaiiPhysicsLimitsDataAsset>());
	Asset->SphericalLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
	Asset->CapsuleLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
	Asset->TaperedCapsuleLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
	Asset->BoxLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
	FPlanarLimit& Plane = Asset->PlanarLimits.AddDefaulted_GetRef();
	Plane.DrivingBone.BoneName = TEXT("root");
	Plane.OffsetLocation = FVector(0.0, 0.0, -1000.0);
	return Asset;
}

KawaiiPhysicsNodeFixture::FOptions MakeOptions(UKawaiiPhysicsLimitsDataAsset* Asset)
{
	KawaiiPhysicsNodeFixture::FOptions Options;
	Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(3);
	Options.ConfigureNode = [Asset](FAnimNode_KawaiiPhysics& Node)
	{
		Node.RootBone.BoneName = TEXT("root");
		Node.LimitsDataAsset = Asset;
		Node.bNeedWarmUp = false;
	};
	return Options;
}

void AddAuthoredSphere(KawaiiPhysicsNodeFixture::FFixture& Fixture)
{
	// Initialize は実行時配列を空にするので、その後に AnimNode 由来の球を置いて除去対象との区別を確かめる。
	FSphericalLimit& Sphere = Fixture.Node.SphericalLimitsData.AddDefaulted_GetRef();
	Sphere.DrivingBone.BoneName = TEXT("root");
	Sphere.DrivingBone.Initialize(Fixture.Proxy->GetRequiredBones());
	Sphere.OffsetLocation = FVector(2000.0, 0.0, 0.0);
	Sphere.Radius = 17.0f;
}

template <typename TLimit>
bool CheckImportedSources(FAutomationTestBase& Test, const TArray<TLimit>& Limits)
{
	bool bOk = true;
	for (const TLimit& Limit : Limits)
	{
		bOk &= Test.TestTrue(TEXT("取り込んだ形状は DataAsset 由来"), Limit.SourceType == ECollisionSourceType::DataAsset);
	}
	return bOk;
}

bool CheckSources(FAutomationTestBase& Test, const FAnimNode_KawaiiPhysics& Node)
{
	int32 AuthoredCount = 0;
	bool bOk = true;
	for (const FSphericalLimit& Sphere : Node.SphericalLimitsData)
	{
		if (Sphere.Radius == 17.0f)
		{
			++AuthoredCount;
			bOk &= Test.TestTrue(TEXT("ノード自身の球は AnimNode 由来のまま"), Sphere.SourceType == ECollisionSourceType::AnimNode);
		}
		else
		{
			bOk &= Test.TestTrue(TEXT("アセットの球は DataAsset 由来"), Sphere.SourceType == ECollisionSourceType::DataAsset);
		}
	}
	bOk &= Test.TestEqual(TEXT("ノード自身の球は一個だけ"), AuthoredCount, 1);
	bOk &= CheckImportedSources(Test, Node.CapsuleLimitsData);
	bOk &= CheckImportedSources(Test, Node.TaperedCapsuleLimitsData);
	bOk &= CheckImportedSources(Test, Node.BoxLimitsData);
	bOk &= CheckImportedSources(Test, Node.PlanarLimitsData);
	return bOk;
}

bool CheckAssetA(FAutomationTestBase& Test, const FAnimNode_KawaiiPhysics& Node)
{
	bool bOk = Test.TestEqual(TEXT("A: 球は authored とアセットの二個"), Node.SphericalLimitsData.Num(), 2);
	bOk &= Test.TestEqual(TEXT("A: カプセルは一個"), Node.CapsuleLimitsData.Num(), 1);
	bOk &= Test.TestEqual(TEXT("A: テーパードは一個"), Node.TaperedCapsuleLimitsData.Num(), 1);
	bOk &= Test.TestEqual(TEXT("A: ボックスは一個"), Node.BoxLimitsData.Num(), 1);
	bOk &= Test.TestEqual(TEXT("A: 平面は一個"), Node.PlanarLimitsData.Num(), 1);
	bOk &= CheckSources(Test, Node);
	return bOk;
}

bool CheckAssetUnchanged(FAutomationTestBase& Test, const UKawaiiPhysicsLimitsDataAsset& Asset)
{
	const auto CheckLimits = [&Test](const auto& Limits)
	{
		bool bOk = true;
		for (const auto& Limit : Limits)
		{
			bOk &= Test.TestTrue(TEXT("元アセットの SourceType は既定の AnimNode のまま"),
				Limit.SourceType == ECollisionSourceType::AnimNode);
		}
		return bOk;
	};
	bool bOk = CheckLimits(Asset.SphericalLimits);
	bOk &= CheckLimits(Asset.CapsuleLimits);
	bOk &= CheckLimits(Asset.TaperedCapsuleLimits);
	bOk &= CheckLimits(Asset.BoxLimits);
	bOk &= CheckLimits(Asset.PlanarLimits);
	return bOk;
}

// PR-14(a,c,d)・BX-11「全経路に同じ規則」: SourceType で出どころを区別し、毎評価の取り込み直しで重複しない。
// 修正前は二回目の平面一個と、30 フレーム後の五種類の件数・DataAsset の SourceType の主張が落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsLimitsSourceTypeRepeatedEvaluationTest,
	"KawaiiPhysics.Collision.LimitsDataAssetSourceType.RepeatedEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsLimitsSourceTypeRepeatedEvaluationTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> Asset = MakeFiveShapeAsset();
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeOptions(Asset.Get()));
	AddAuthoredSphere(Fixture);
	Fixture.EvaluateFrame();
	Fixture.EvaluateFrame();
	bool bOk = TestEqual(TEXT("二回目の評価後も同じ平面は一個"), Fixture.Node.PlanarLimitsData.Num(), 1);
	if (Fixture.Node.PlanarLimitsData.Num() == 1)
	{
		bOk &= TestTrue(TEXT("二回目の評価後も平面の前ステップ履歴がある"),
			Fixture.Node.PlanarLimitsData[0].bHasPreviousStepPlane);
	}
	for (int32 Frame = 2; Frame < 30; ++Frame)
	{
		Fixture.EvaluateFrame();
	}
	bOk &= TestTrue(TEXT("対照: ModifyBones を実際に評価している"), Fixture.Accessor.Num() > 0);
	bOk &= CheckAssetA(*this, Fixture.Node);
	bOk &= CheckAssetUnchanged(*this, *Asset);
	return bOk;
}

// PR-14(b,d)・BX-11「全経路に同じ規則」: SourceType で出どころを区別し、実行時の取り込み直しも重複しない。
// 修正前は A→B→A の件数・DataAsset の SourceType と、nullptr 後に authored の球だけ残る主張が落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsLimitsSourceTypeReplaceAndClearTest,
	"KawaiiPhysics.Collision.LimitsDataAssetSourceType.ReplaceAndClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsLimitsSourceTypeReplaceAndClearTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> AssetA = MakeFiveShapeAsset();
	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> AssetB(NewObject<UKawaiiPhysicsLimitsDataAsset>());
	AssetB->SphericalLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("root");
	AssetB->SphericalLimits.AddDefaulted_GetRef().DrivingBone.BoneName = TEXT("chain_1");
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeOptions(AssetA.Get()));
	AddAuthoredSphere(Fixture);
	Fixture.EvaluateFrame();
	bool bOk = TestTrue(TEXT("対照: ModifyBones が一個以上ある"), Fixture.Accessor.Num() > 0);
	bOk &= CheckAssetA(*this, Fixture.Node);
	const FName AssetName = GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset);
	const FObjectProperty* AssetProperty = CastField<FObjectProperty>(
		FAnimNode_KawaiiPhysics::StaticStruct()->FindPropertyByName(AssetName));
	if (!TestNotNull(TEXT("既存の汎用 setter でアセットを差し替えられる"), AssetProperty))
	{
		return false;
	}
	const auto ReplaceAsset = [this, &Fixture, AssetName, AssetProperty](UKawaiiPhysicsLimitsDataAsset* Asset)
	{
		bool bChanged = TestTrue(TEXT("アセットの setter が成功する"),
			UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(Fixture.Node, AssetName, AssetProperty, &Asset));
		bChanged &= TestTrue(TEXT("実行時の再適用を要求する"), Fixture.Accessor.IsLimitAssetsReapplyPending());
		// Editor の毎評価同期で実行時経路の不備が隠れないよう、評価の前にも再適用結果を検査する。
		Fixture.Accessor.CallApplyPendingRuntimeReinit(Fixture.Proxy->GetRequiredBones());
		return bChanged;
	};
	const auto CheckAssetB = [this, &Fixture]()
	{
		bool bStageOk = TestEqual(TEXT("B: 球は authored 一個とアセット二個"), Fixture.Node.SphericalLimitsData.Num(), 3);
		bStageOk &= TestEqual(TEXT("B: A のカプセルは残らない"), Fixture.Node.CapsuleLimitsData.Num(), 0);
		bStageOk &= TestEqual(TEXT("B: A のテーパードは残らない"), Fixture.Node.TaperedCapsuleLimitsData.Num(), 0);
		bStageOk &= TestEqual(TEXT("B: A のボックスは残らない"), Fixture.Node.BoxLimitsData.Num(), 0);
		bStageOk &= TestEqual(TEXT("B: A の平面は残らない"), Fixture.Node.PlanarLimitsData.Num(), 0);
		bStageOk &= CheckSources(*this, Fixture.Node);
		return bStageOk;
	};
	bOk &= ReplaceAsset(AssetB.Get());
	bOk &= CheckAssetB();
	Fixture.EvaluateFrame();
	bOk &= CheckAssetB();
	bOk &= ReplaceAsset(AssetA.Get());
	bOk &= CheckAssetA(*this, Fixture.Node);
	Fixture.EvaluateFrame();
	bOk &= CheckAssetA(*this, Fixture.Node);
	bOk &= ReplaceAsset(nullptr);
	const auto CheckCleared = [this, &Fixture]()
	{
		bool bStageOk = TestEqual(TEXT("nullptr: authored の球だけ残る"), Fixture.Node.SphericalLimitsData.Num(), 1);
		bStageOk &= TestEqual(TEXT("nullptr: カプセルを除去"), Fixture.Node.CapsuleLimitsData.Num(), 0);
		bStageOk &= TestEqual(TEXT("nullptr: テーパードを除去"), Fixture.Node.TaperedCapsuleLimitsData.Num(), 0);
		bStageOk &= TestEqual(TEXT("nullptr: ボックスを除去"), Fixture.Node.BoxLimitsData.Num(), 0);
		bStageOk &= TestEqual(TEXT("nullptr: 平面を除去"), Fixture.Node.PlanarLimitsData.Num(), 0);
		bStageOk &= CheckSources(*this, Fixture.Node);
		return bStageOk;
	};
	bOk &= CheckCleared();
	Fixture.EvaluateFrame();
	bOk &= CheckCleared();
	bOk &= CheckAssetUnchanged(*this, *AssetA);
	bOk &= CheckAssetUnchanged(*this, *AssetB);
	return bOk;
}

} // namespace KawaiiPhysicsLimitsSourceTypeTestPrivate

#endif
