// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsPropertyCompare.h"
#include "PhysicsEngine/BodyInstance.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace KawaiiPhysicsPresetCurveCompareTestPrivate
{

static KawaiiPhysicsNodeFixture::FOptions MakeOptions()
{
	KawaiiPhysicsNodeFixture::FOptions Options;
	Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(4);
	Options.bPreUpdateEveryFrame = true;
	Options.GetComponentTransform = [](int32 Frame)
	{
		return FTransform(FVector(0.0, FMath::Sin(Frame * 0.18) * 8.0, 0.0));
	};
	Options.ConfigureNode = [](FAnimNode_KawaiiPhysics& Node)
	{
		Node.RootBone.BoneName = TEXT("root");
		Node.bNeedWarmUp = false;
		Node.PhysicsSettings.Damping = 0.34f;
		Node.DampingCurveData.GetRichCurve()->AddKey(0.0f, 0.25f);
		Node.DampingCurveData.GetRichCurve()->AddKey(1.0f, 0.75f);
		// RadiusCurveData の誤検出は ModifyBones の再構築も要求する。
		Node.RadiusCurveData.GetRichCurve()->AddKey(0.0f, 0.5f);
		Node.RadiusCurveData.GetRichCurve()->AddKey(1.0f, 1.0f);
	};
	return Options;
}

static bool ArePositionsBitwiseIdentical(const FAnimNode_KawaiiPhysics& A, const FAnimNode_KawaiiPhysics& B)
{
	if (A.ModifyBones.IsEmpty() || A.ModifyBones.Num() != B.ModifyBones.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.ModifyBones.Num(); ++Index)
	{
		const FVector& Left = A.ModifyBones[Index].Location;
		const FVector& Right = B.ModifyBones[Index].Location;
		if (FMemory::Memcmp(&Left.X, &Right.X, sizeof(Left.X)) != 0 ||
			FMemory::Memcmp(&Left.Y, &Right.Y, sizeof(Left.Y)) != 0 ||
			FMemory::Memcmp(&Left.Z, &Right.Z, sizeof(Left.Z)) != 0)
		{
			return false;
		}
	}
	return true;
}

// PR-03・PR-11・FX-34。修正前は変更数 0・要求なし・MatchesNode と差分 0 が落ち、
// RadiusCurveData の誤検出による再構築で位置のビット一致も落ちる。許容誤差は 0。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetCurveKeyHandleReapplyTest,
	"KawaiiPhysics.Preset.CurveKeyHandle.Reapply", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetCurveKeyHandleReapplyTest::RunTest(const FString& Parameters)
{
	const KawaiiPhysicsNodeFixture::FOptions Options = MakeOptions();
	KawaiiPhysicsNodeFixture::FFixture Target(Options);
	KawaiiPhysicsNodeFixture::FFixture Control(Options);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Target.EvaluateFrame();
		Control.EvaluateFrame();
	}
	TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset> Preset(NewObject<UKawaiiPhysicsPresetDataAsset>());
	Preset->CopyFromNode(Target.Node);
	bool bOk = TestTrue(TEXT("適用前の位置はビット一致"), ArePositionsBitwiseIdentical(Target.Node, Control.Node));
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		bOk &= TestEqual(TEXT("同じプリセットは変更数 0"),
			UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Target.Node, *Preset, {}), 0);
		bOk &= TestFalse(TEXT("同じプリセットでは再初期化・再適用・設定更新の要求なし"),
			FKawaiiPhysicsTestAccessor::HasPendingRuntimeApplyRequest(Target.Node));
		TArray<FName> DiffProperties;
		bOk &= TestTrue(TEXT("KeyHandle の有無が違っても MatchesNode は一致"),
			Preset->MatchesNode(Target.Node, {}, DiffProperties));
		bOk &= TestEqual(TEXT("差分は空"), DiffProperties.Num(), 0);
		Target.EvaluateFrame();
		Control.EvaluateFrame();
		bOk &= TestTrue(TEXT("毎フレーム適用しても対照と位置がビット一致"),
			ArePositionsBitwiseIdentical(Target.Node, Control.Node));
	}
	return bOk;
}

// PR-03。キー値・DefaultValue・前方と後方の外挿を個別に変え、DampingCurveData だけを差分とする。
// 修正前でもこの対照は通る。両辺をハンドル無しにして、KeyHandle の違いで対照が通ることを防ぐ。
// 件数・プロパティ名は完全一致で、数値の許容誤差は使わない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetCurveKeyHandleDifferencesTest,
	"KawaiiPhysics.Preset.CurveKeyHandle.Differences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetCurveKeyHandleDifferencesTest::RunTest(const FString& Parameters)
{
	FRuntimeFloatCurve SourceCurve;
	SourceCurve.GetRichCurve()->AddKey(0.0f, 0.25f);
	SourceCurve.GetRichCurve()->AddKey(1.0f, 0.75f);
	FAnimNode_KawaiiPhysics Node;
	Node.DampingCurveData = SourceCurve;
	bool bOk = true;
	for (int32 ChangeKind = 0; ChangeKind < 4; ++ChangeKind)
	{
		TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset> Preset(NewObject<UKawaiiPhysicsPresetDataAsset>());
		Preset->CopyFromNode(Node);
		TArray<FName> DiffProperties;
		bOk &= TestTrue(TEXT("単独変更前は一致"), Preset->MatchesNode(Node, {}, DiffProperties));
		FRichCurve& Curve = Preset->Node.DampingCurveData.EditorCurveData;
		if (ChangeKind == 0) { Curve.Keys[0].Value = 0.5f; }
		if (ChangeKind == 1) { Curve.DefaultValue = 0.125f; }
		if (ChangeKind == 2) { Curve.PreInfinityExtrap = RCCE_Linear; }
		if (ChangeKind == 3) { Curve.PostInfinityExtrap = RCCE_Linear; }
		bOk &= TestFalse(TEXT("永続設定の単独変更は不一致"), Preset->MatchesNode(Node, {}, DiffProperties));
		bOk &= TestEqual(TEXT("差分は一件"), DiffProperties.Num(), 1);
		bOk &= TestTrue(TEXT("差分は DampingCurveData"), DiffProperties.Contains(FName(TEXT("DampingCurveData"))));
	}
	return bOk;
}

// PR-11・FX-34・PR-U4。汎用 setter も同じ AreNodePropertySettingsIdentical 経路を通す。
// 修正前は同じ RadiusCurveData で要求なし・位置のビット一致が落ちる。
// キー値変更は RadiusCurveData だけが再構築を要求し、DampingCurveData は要求しない。許容誤差は 0。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetCurveKeyHandleSetterTest,
	"KawaiiPhysics.Preset.CurveKeyHandle.Setter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetCurveKeyHandleSetterTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const FName PropertyName : {FName(TEXT("DampingCurveData")), FName(TEXT("RadiusCurveData"))})
	{
		const KawaiiPhysicsNodeFixture::FOptions Options = MakeOptions();
		KawaiiPhysicsNodeFixture::FFixture Target(Options);
		KawaiiPhysicsNodeFixture::FFixture Control(Options);
		for (int32 Frame = 0; Frame < 12; ++Frame)
		{
			Target.EvaluateFrame();
			Control.EvaluateFrame();
		}
		const FStructProperty* Property = FindFProperty<FStructProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
		if (!TestNotNull(TEXT("カーブのプロパティがある"), Property)) { bOk = false; continue; }
		FRuntimeFloatCurve Value = *Property->ContainerPtrToValuePtr<FRuntimeFloatCurve>(&Target.Node);
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			bOk &= TestTrue(TEXT("同じカーブを汎用 setter へ渡せる"),
				UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(Target.Node, PropertyName, Property, &Value));
			bOk &= TestFalse(TEXT("同じカーブでは要求なし"),
				FKawaiiPhysicsTestAccessor::HasPendingRuntimeApplyRequest(Target.Node));
			Target.EvaluateFrame();
			Control.EvaluateFrame();
			bOk &= TestTrue(TEXT("同じカーブの setter は位置を変えない"),
				ArePositionsBitwiseIdentical(Target.Node, Control.Node));
		}
		Value.EditorCurveData.Keys[0].Value = 0.875f;
		bOk &= TestTrue(TEXT("キー値を変えたカーブを設定できる"),
			UKawaiiPhysicsLibrary::SetNodeWildcardPropertyValue(Target.Node, PropertyName, Property, &Value));
		bOk &= TestEqual(TEXT("変更したキー値は反映される"),
			Property->ContainerPtrToValuePtr<FRuntimeFloatCurve>(&Target.Node)->EditorCurveData.Keys[0].Value, 0.875f);
		bOk &= TestEqual(TEXT("RadiusCurveData だけが再構築を要求"),
			Target.Accessor.IsModifyBonesReinitPending(), PropertyName == FName(TEXT("RadiusCurveData")));
		bOk &= TestEqual(TEXT("再構築時だけボーン参照の初期化も要求"),
			Target.Accessor.IsBoneReferencesReinitPending(), PropertyName == FName(TEXT("RadiusCurveData")));
		bOk &= TestFalse(TEXT("カーブ変更ではアセットの再適用なし"), Target.Accessor.IsLimitAssetsReapplyPending());
		bOk &= TestFalse(TEXT("カーブ変更では SyncBone の再初期化なし"), Target.Accessor.IsSyncBonesReinitPending());
		bOk &= TestFalse(TEXT("カーブ変更では共有コリジョンの再初期化なし"), Target.Accessor.IsSharedCollisionReinitPending());
		bOk &= TestFalse(TEXT("setter は設定更新要求を追加しない"), Target.Accessor.IsPhysicsSettingsRefreshPending());
	}
	return bOk;
}

// PR-03・PR-11・FX-34。共通比較の単体。修正前の Property->Identical に置き換えると、
// ハンドル付きとコピーしたハンドル無しのカーブが同値という主張が落ちる。キー値と要素数の対照は修正前も通る。
// 真偽値は完全一致。KeyHandle の有無を検証するときだけ生の Identical を使う。
// 飛ばすフィールドの判定は、Transient 全部に広げると ResponseToChannels の主張が落ちる（UE 5.3/5.4 で応答の違いを見落とす退行）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetCurveKeyHandlePropertyValuesTest,
	"KawaiiPhysics.Preset.CurveKeyHandle.PropertyValues", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetCurveKeyHandlePropertyValuesTest::RunTest(const FString& Parameters)
{
	const FStructProperty* CurveProperty = FindFProperty<FStructProperty>(
		FAnimNode_KawaiiPhysics::StaticStruct(), TEXT("DampingCurveData"));
	const FStructProperty* HandlesProperty = FindFProperty<FStructProperty>(FRichCurve::StaticStruct(), TEXT("KeyHandlesToIndices"));
	const FArrayProperty* KeysProperty = FindFProperty<FArrayProperty>(FRichCurve::StaticStruct(), TEXT("Keys"));
	if (!TestNotNull(TEXT("カーブのプロパティ"), CurveProperty) ||
		!TestNotNull(TEXT("親構造体の KeyHandle のプロパティ"), HandlesProperty) ||
		!TestNotNull(TEXT("キー配列のプロパティ"), KeysProperty))
	{
		return false;
	}
	FRuntimeFloatCurve WithHandles;
	WithHandles.GetRichCurve()->AddKey(0.0f, 0.25f);
	WithHandles.GetRichCurve()->AddKey(1.0f, 0.75f);
	FRuntimeFloatCurve WithoutHandles = WithHandles;
	bool bOk = TestTrue(TEXT("KeyHandle は Transient"), HandlesProperty->HasAnyPropertyFlags(CPF_Transient));
	bOk &= TestFalse(TEXT("直接作ったカーブとコピーの KeyHandle は異なる"),
		HandlesProperty->Identical_InContainer(&WithHandles.EditorCurveData, &WithoutHandles.EditorCurveData, 0, PPF_None));
	bOk &= TestTrue(TEXT("ハンドル付きとハンドル無しの同じカーブは同値"),
		KawaiiPhysicsPropertyComparePrivate::ArePropertyValuesIdentical(CurveProperty, &WithHandles, &WithoutHandles, PPF_None));
	WithoutHandles.EditorCurveData.Keys[0].Value = 0.5f;
	bOk &= TestFalse(TEXT("キー値が違うカーブは非同値"),
		KawaiiPhysicsPropertyComparePrivate::ArePropertyValuesIdentical(CurveProperty, &WithHandles, &WithoutHandles, PPF_None));
	WithoutHandles = WithHandles;
	WithoutHandles.EditorCurveData.Keys.Add(FRichCurveKey(2.0f, 1.0f));
	bOk &= TestFalse(TEXT("要素数が違う配列は非同値"),
		KawaiiPhysicsPropertyComparePrivate::ArePropertyValuesIdentical(KeysProperty,
			&WithHandles.EditorCurveData.Keys, &WithoutHandles.EditorCurveData.Keys, PPF_None));

	// 飛ばすのはカーブの KeyHandle の表だけで、ほかの Transient は比べる（UE 5.3/5.4 の
	// FCollisionResponse にはネイティブの同値判定が無く、Transient の ResponseToChannels が実際の応答の設定を持つ）。
	bOk &= TestTrue(TEXT("KeyHandle の表は飛ばす"),
		KawaiiPhysicsPropertyComparePrivate::IsCurveKeyHandleMapProperty(*HandlesProperty));
	const FStructProperty* ResponsesProperty = FindFProperty<FStructProperty>(FBodyInstance::StaticStruct(), TEXT("CollisionResponses"));
	const FProperty* ResponseToChannelsProperty = ResponsesProperty && ResponsesProperty->Struct
		? ResponsesProperty->Struct->FindPropertyByName(TEXT("ResponseToChannels")) : nullptr;
	if (TestNotNull(TEXT("FCollisionResponse::ResponseToChannels のプロパティ"), ResponseToChannelsProperty))
	{
		bOk &= TestTrue(TEXT("ResponseToChannels は Transient"), ResponseToChannelsProperty->HasAnyPropertyFlags(CPF_Transient));
		bOk &= TestFalse(TEXT("KeyHandle の表以外の Transient は飛ばさない"),
			KawaiiPhysicsPropertyComparePrivate::IsCurveKeyHandleMapProperty(*ResponseToChannelsProperty));
	}
	else
	{
		bOk = false;
	}
	return bOk;
}

} // namespace KawaiiPhysicsPresetCurveCompareTestPrivate

#endif
