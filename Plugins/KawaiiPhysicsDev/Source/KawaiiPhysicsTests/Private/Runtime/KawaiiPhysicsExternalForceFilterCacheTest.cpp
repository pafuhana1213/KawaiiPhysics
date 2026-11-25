// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsHairNodeFixture.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Basic.h"
#include "Misc/MemStack.h"
#include "Math/RandomStream.h"

namespace KawaiiPhysicsExternalForceFilterCacheTestPrivate
{
// 外力のボーンフィルタ判定表（RefreshBoneFilterCache）が、線形探索と同じ結果を返し続けるかを確かめる。
	FName FilterBoneName(const int32 Index)
	{
		return FName(TEXT("FilterBone"), Index + 1);
	}

	FBoneReference MakeBoneRef(const FName Name)
	{
		FBoneReference Ref;
		Ref.BoneName = Name;
		return Ref;
	}

	struct FFilterProbe : FKawaiiPhysics_ExternalForce_Basic
	{
		bool Allows(const FKawaiiPhysicsModifyBone& Bone) const { return CanApply(Bone); }
	};

	// 期待値は外力の解決関数を使わず、仕様の index 優先順位から求める。
	static int32 ExpectedFilterIndex(const TArray<FKawaiiPhysicsModifyBone>& Bones, const int32 StartIndex)
	{
		int32 CurrentIndex = StartIndex;
		for (int32 Step = 0; Step < Bones.Num(); ++Step)
		{
			const FKawaiiPhysicsModifyBone& Current = Bones[CurrentIndex];
			if (!Current.bDummy)
			{
				break;
			}
			int32 NextIndex = Current.InterBoneRealParentIndex;
			if (!Bones.IsValidIndex(NextIndex) && !Current.bBridgeDummy)
			{
				NextIndex = Current.ParentIndex;
			}
			if (!Bones.IsValidIndex(NextIndex))
			{
				break;
			}
			CurrentIndex = NextIndex;
			if (Current.bBridgeDummy || Bones[CurrentIndex].bBridgeDummy)
			{
				break;
			}
		}
		return CurrentIndex;
	}

	static bool ExpectedAllows(const FFilterProbe& Force, const TArray<FKawaiiPhysicsModifyBone>& Bones,
		const int32 BoneIndex)
	{
		const FName Name = Bones[ExpectedFilterIndex(Bones, BoneIndex)].BoneRef.BoneName;
		const auto ContainsName = [Name](const TArray<FBoneReference>& Filter)
		{
			return Filter.ContainsByPredicate([Name](const FBoneReference& Ref) { return Ref.BoneName == Name; });
		};
		return (Force.ApplyBoneFilter.IsEmpty() || ContainsName(Force.ApplyBoneFilter))
			&& !ContainsName(Force.IgnoreBoneFilter);
	}

	void ConfigureWind(FKawaiiPhysics_ExternalForce_ProceduralWind& Wind)
	{
		Wind.WindSource = EKawaiiPhysicsProceduralWindSource::Local;
		Wind.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
		Wind.WindDirection = FVector(0.0f, 1.0f, 0.0f);
		Wind.ConstantForce = 10.0f;
	}

	/** Super::PreApply の後でフィルタを差し替える独自外力（ユーザーの派生クラスを模す） */
	struct FFilterSwitchingWind : FKawaiiPhysics_ExternalForce_ProceduralWind
	{
		TArray<FBoneReference> NextFilter;

		virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override
		{
			FKawaiiPhysics_ExternalForce_ProceduralWind::PreApply(Node, PoseContext);
			ApplyBoneFilter = NextFilter;
		}
	};

	/** 4 ボーンの縦チェーン（index 0 は固定の根元）に ProceduralWind を 1 本置き、ボーン 1 だけを対象にする */
	struct FFilterFixture
	{
		FKawaiiPhysicsTestAccessor A;
		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext PoseContext{&Proxy};

		explicit FFilterFixture(const int32 NameOffset = 0)
		{
			A.BuildVerticalChain(4, 5.0f);
			A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
			for (int32 Index = 0; Index < A.Num(); ++Index)
			{
				A.Bone(Index).BoneRef.BoneName = FilterBoneName(Index + NameOffset);
			}
			A.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>());
			ConfigureWind(*GetWind());
			GetWind()->ApplyBoneFilter.Add(MakeBoneRef(FilterBoneName(1)));
			A.SetSubstepTimeState(1.0f / 60.0f, 1.0f / 60.0f);
		}

		FKawaiiPhysics_ExternalForce_ProceduralWind* GetWind() { return A.GetMutableProceduralWind(0); }

		/** 本番ノードと同じ順（PreApply → RefreshBoneFilterCache → 各ボーンへ Apply）で 1 フレーム進め、動いたボーンを返す */
		TArray<int32> AppliedBones(FKawaiiPhysics_ExternalForce_ProceduralWind& Wind)
		{
			Wind.PreApply(A.Node, PoseContext);
			Wind.RefreshBoneFilterCache(A.Node);
			TArray<int32> Applied;
			for (int32 Index = 1; Index < A.Num(); ++Index)
			{
				A.Bone(Index).Location = FVector::ZeroVector;
				Wind.Apply(A.Bone(Index), A.Node, PoseContext);
				if (!A.Bone(Index).Location.IsNearlyZero())
				{
					Applied.Add(Index);
				}
			}
			return Applied;
		}

		TArray<int32> AppliedBones() { return AppliedBones(*GetWind()); }
	};

	FString JoinIndices(const TArray<int32>& Values)
	{
		FString Out;
		for (const int32 Value : Values)
		{
			Out += FString::Printf(TEXT("%d "), Value);
		}
		return Out.TrimEnd();
	}

	bool ExpectApplied(FAutomationTestBase& Test, const TCHAR* Scenario, const TArray<int32>& Actual,
	                   const TArray<int32>& Expected)
	{
		Test.AddInfo(FString::Printf(TEXT("FILTER_CACHE %s applied=[%s]"), Scenario, *JoinIndices(Actual)));
		return Test.TestTrue(FString::Printf(TEXT("%s: force applies to [%s] (expected [%s])"),
			Scenario, *JoinIndices(Actual), *JoinIndices(Expected)), Actual == Expected);
	}

// フィルタの書き換え方ごとに、次の Apply が新しいフィルタに従うかを確かめる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceFilterChangeTest,
	"KawaiiPhysics.ExternalForce.BoneFilterChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceFilterChangeTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	const TArray<int32> Before{1};
	const TArray<int32> After{2};

	// 同じ要素数の配列をコピー代入する（TArray は容量が足りれば確保を使い回すので、確保先では変更を検知できない）
	{
		FFilterFixture F;
		bOk &= ExpectApplied(*this, TEXT("CopyAssignSameNum.Before"), F.AppliedBones(), Before);
		const TArray<FBoneReference> NewFilter{MakeBoneRef(FilterBoneName(2))};
		F.GetWind()->ApplyBoneFilter = NewFilter;
		bOk &= ExpectApplied(*this, TEXT("CopyAssignSameNum.After"), F.AppliedBones(), After);
	}
	// 要素を直接書き換える
	{
		FFilterFixture F;
		bOk &= ExpectApplied(*this, TEXT("ElementEdit.Before"), F.AppliedBones(), Before);
		F.GetWind()->ApplyBoneFilter[0].BoneName = FilterBoneName(2);
		bOk &= ExpectApplied(*this, TEXT("ElementEdit.After"), F.AppliedBones(), After);
	}
	// Reset してから作り直す（確保は残る）
	{
		FFilterFixture F;
		bOk &= ExpectApplied(*this, TEXT("ResetAndAdd.Before"), F.AppliedBones(), Before);
		F.GetWind()->ApplyBoneFilter.Reset();
		F.GetWind()->ApplyBoneFilter.Add(MakeBoneRef(FilterBoneName(2)));
		bOk &= ExpectApplied(*this, TEXT("ResetAndAdd.After"), F.AppliedBones(), After);
	}
	// Empty してから作り直す（解放直後の同じアドレスが再び返ることがある）
	{
		FFilterFixture F;
		bOk &= ExpectApplied(*this, TEXT("EmptyAndAdd.Before"), F.AppliedBones(), Before);
		F.GetWind()->ApplyBoneFilter.Empty();
		F.GetWind()->ApplyBoneFilter.Add(MakeBoneRef(FilterBoneName(2)));
		bOk &= ExpectApplied(*this, TEXT("EmptyAndAdd.After"), F.AppliedBones(), After);
	}
	// 要素数が変わる
	{
		FFilterFixture F;
		bOk &= ExpectApplied(*this, TEXT("AddElement.Before"), F.AppliedBones(), Before);
		F.GetWind()->ApplyBoneFilter.Add(MakeBoneRef(FilterBoneName(3)));
		bOk &= ExpectApplied(*this, TEXT("AddElement.After"), F.AppliedBones(), (TArray<int32>{1, 3}));
	}
	// 空 → 非空 → 空（空の間は全ボーンへ掛かり、判定表は使わない）
	{
		FFilterFixture F;
		F.GetWind()->ApplyBoneFilter.Reset();
		bOk &= ExpectApplied(*this, TEXT("EmptyToFilled.Empty"), F.AppliedBones(), (TArray<int32>{1, 2, 3}));
		bOk &= TestFalse(TEXT("EmptyToFilled.Empty: no cache while both filters are empty"),
			FKawaiiPhysicsTestAccessor::IsBoneFilterCacheValid(*F.GetWind()));
		F.GetWind()->ApplyBoneFilter.Add(MakeBoneRef(FilterBoneName(2)));
		bOk &= ExpectApplied(*this, TEXT("EmptyToFilled.Filled"), F.AppliedBones(), After);
		bOk &= TestTrue(TEXT("EmptyToFilled.Filled: cache is built and used"),
			FKawaiiPhysicsTestAccessor::IsBoneFilterCacheValid(*F.GetWind()));
		F.GetWind()->ApplyBoneFilter.Reset();
		bOk &= ExpectApplied(*this, TEXT("EmptyToFilled.EmptyAgain"), F.AppliedBones(), (TArray<int32>{1, 2, 3}));
	}
	// IgnoreBoneFilter だけ、および Apply と Ignore の両方（Ignore が優先）
	{
		FFilterFixture F;
		F.GetWind()->ApplyBoneFilter.Reset();
		F.GetWind()->IgnoreBoneFilter.Add(MakeBoneRef(FilterBoneName(1)));
		bOk &= ExpectApplied(*this, TEXT("Ignore.Only"), F.AppliedBones(), (TArray<int32>{2, 3}));
		F.GetWind()->IgnoreBoneFilter[0].BoneName = FilterBoneName(3);
		bOk &= ExpectApplied(*this, TEXT("Ignore.ElementEdit"), F.AppliedBones(), (TArray<int32>{1, 2}));
		F.GetWind()->ApplyBoneFilter = {MakeBoneRef(FilterBoneName(2)), MakeBoneRef(FilterBoneName(3))};
		bOk &= ExpectApplied(*this, TEXT("Ignore.WithApply"), F.AppliedBones(), (TArray<int32>{2}));
	}
	// PW-6 / RI-3: 実親付きダミーは実親の名前で判定する。修正前は Apply で対象外、Ignore で対象になる。
	{
		FFilterFixture F;
		F.A.Bone(3).bDummy = true;
		F.A.Bone(3).BoneRef.BoneName = NAME_None;
		F.A.Bone(3).InterBoneRealParentIndex = 1;
		bOk &= ExpectApplied(*this, TEXT("Dummy.ApplyFilter"), F.AppliedBones(), (TArray<int32>{1, 3}));
		F.GetWind()->ApplyBoneFilter.Reset();
		F.GetWind()->IgnoreBoneFilter.Add(MakeBoneRef(FilterBoneName(1)));
		bOk &= ExpectApplied(*this, TEXT("Dummy.IgnoreFilter"), F.AppliedBones(), (TArray<int32>{2}));
	}
	// 独自外力が Super::PreApply の後でフィルタを変えても、同じフレームの Apply から反映される
	{
		FFilterFixture F;
		FFilterSwitchingWind Wind;
		ConfigureWind(Wind);
		Wind.ApplyBoneFilter = {MakeBoneRef(FilterBoneName(1))};
		Wind.NextFilter = Wind.ApplyBoneFilter;
		bOk &= ExpectApplied(*this, TEXT("ChangedAfterSuperPreApply.Before"), F.AppliedBones(Wind), Before);
		Wind.NextFilter[0].BoneName = FilterBoneName(2);
		bOk &= ExpectApplied(*this, TEXT("ChangedAfterSuperPreApply.After"), F.AppliedBones(Wind), After);
	}
	// 判定表を持つ外力をコピーし、コピー先でフィルタを書き換える（判定表もコピーされるが、中身の照合で作り直される）
	{
		FFilterFixture F;
		bOk &= ExpectApplied(*this, TEXT("PopulatedCopy.Before"), F.AppliedBones(), Before);
		bOk &= TestTrue(TEXT("PopulatedCopy: source cache is valid"),
			FKawaiiPhysicsTestAccessor::IsBoneFilterCacheValid(*F.GetWind()));
		FKawaiiPhysics_ExternalForce_ProceduralWind Copy = *F.GetWind();
		Copy.ApplyBoneFilter[0].BoneName = FilterBoneName(2);
		bOk &= ExpectApplied(*this, TEXT("PopulatedCopy.EditedCopy"), F.AppliedBones(Copy), After);
		bOk &= ExpectApplied(*this, TEXT("PopulatedCopy.SourceUnchanged"), F.AppliedBones(), Before);
	}
	// 判定表を持つ外力を、ボーン名の違う別ノードへコピーする
	{
		FFilterFixture Source;
		bOk &= ExpectApplied(*this, TEXT("CopyToOtherNode.Source"), Source.AppliedBones(), Before);
		FFilterFixture Other(10);
		*Other.GetWind() = *Source.GetWind();
		Other.GetWind()->ApplyBoneFilter = {MakeBoneRef(FilterBoneName(12))};
		bOk &= ExpectApplied(*this, TEXT("CopyToOtherNode.Other"), Other.AppliedBones(), After);
	}
	// Persona のライブ編集と同じ in-place 同期（CopyNodeDataToPreviewNode の CopyScriptStruct）。
	// ProceduralWind は native の operator= を持つので、PreApply を通らないエディタ側の外力から判定表もコピーされる
	{
		FFilterFixture F;
		bOk &= ExpectApplied(*this, TEXT("PersonaCopyScriptStruct.Before"), F.AppliedBones(), Before);
		FKawaiiPhysics_ExternalForce_ProceduralWind Template;
		ConfigureWind(Template);
		Template.ApplyBoneFilter = {MakeBoneRef(FilterBoneName(2))};
		FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct()->CopyScriptStruct(
			F.A.Node.ExternalForces[0].GetMutableMemory(), &Template);
		bOk &= ExpectApplied(*this, TEXT("PersonaCopyScriptStruct.After"), F.AppliedBones(), After);
	}
	// ボーンリストの作り直し（LOD 変更や再初期化）
	{
		FFilterFixture F;
		bOk &= ExpectApplied(*this, TEXT("BoneListRebuild.Before"), F.AppliedBones(), Before);
		Swap(F.A.Bone(1).BoneRef.BoneName, F.A.Bone(2).BoneRef.BoneName);
		bOk &= ExpectApplied(*this, TEXT("BoneListRebuild.After"), F.AppliedBones(), After);
	}
	// PreApply の後にボーンリストが変わっても、名前の照合で線形探索へ戻る
	{
		FFilterFixture F;
		bOk &= ExpectApplied(*this, TEXT("BoneListChangedAfterRefresh.Before"), F.AppliedBones(), Before);
		FKawaiiPhysics_ExternalForce_ProceduralWind& Wind = *F.GetWind();
		Wind.PreApply(F.A.Node, F.PoseContext);
		Wind.RefreshBoneFilterCache(F.A.Node);
		Swap(F.A.Bone(1).BoneRef.BoneName, F.A.Bone(2).BoneRef.BoneName);
		TArray<int32> Applied;
		for (int32 Index = 1; Index < F.A.Num(); ++Index)
		{
			F.A.Bone(Index).Location = FVector::ZeroVector;
			Wind.Apply(F.A.Bone(Index), F.A.Node, F.PoseContext);
			if (!F.A.Bone(Index).Location.IsNearlyZero())
			{
				Applied.Add(Index);
			}
		}
		bOk &= ExpectApplied(*this, TEXT("BoneListChangedAfterRefresh.After"), Applied, After);
	}
	return bOk;
}

// 本番のノード評価でフィルタ付き外力の判定表が作られ、ProceduralWind が対象のボーンだけを動かすことを確かめる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceFilterCacheNodeTest,
	"KawaiiPhysics.ExternalForce.BoneFilterCacheInNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceFilterCacheNodeTest::RunTest(const FString& Parameters)
{
	using namespace KawaiiPhysicsHairNodeFixture;

	KawaiiPhysicsNodeFixture::FScopedDeveloperSettings Scoped([](UKawaiiPhysicsDeveloperSettings& Settings)
	{
		Settings.bUseFixedSubstepping = true;
	});

	bool bOk = true;
	{
		FMemMark MemMark(FMemStack::Get());
		FFixture Fixture(FCase{TEXT("CacheInNode"), 60, 4, EFilterLayout::RoundRobinChains});
		FComponentSpacePoseContext Context(Fixture.Proxy.Get());
		for (int32 Frame = 0; Frame < 8; ++Frame)
		{
			Fixture.Evaluate(Context);
		}
		bOk &= TestTrue(TEXT("node evaluation outputs all bones with finite values"), Fixture.OutputsAllBonesFinite());
		for (int32 ForceIndex = 0; ForceIndex < Fixture.Node.ExternalForces.Num(); ++ForceIndex)
		{
			const FKawaiiPhysics_ExternalForce& Force =
				Fixture.Node.ExternalForces[ForceIndex].Get<FKawaiiPhysics_ExternalForce>();
			bOk &= TestTrue(FString::Printf(TEXT("force %d: node builds the bone filter cache"), ForceIndex),
				FKawaiiPhysicsTestAccessor::IsBoneFilterCacheValid(Force));
		}
	}
	return bOk;
}

	static KawaiiPhysicsNodeFixture::FOptions MakeDummyNodeOptions(const bool bWithForce,
		const bool bIgnore, const bool bCollisionOnly = false)
	{
		KawaiiPhysicsNodeFixture::FOptions Options;
		Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(3);
		Options.ConfigureNode = [bWithForce, bIgnore, bCollisionOnly](FAnimNode_KawaiiPhysics& Node)
		{
			Node.RootBone.BoneName = TEXT("chain_1");
			Node.BoneSubdivisionCount = 1;
			Node.bBoneSubdivisionDensifyByRadius = false;
			Node.bBoneSubdivisionCollisionOnly = bCollisionOnly;
			Node.DummyBoneLength = 5.0f;
			Node.WarmUpFrames = 0;
			Node.Gravity = FVector::ZeroVector;
			Node.bAllowWorldCollision = false;
			Node.bUseSimpleWorldCollision = false;
			if (bWithForce)
			{
				auto& Basic = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Basic>(Node);
				Basic.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
				Basic.ForceDir = FVector(0.0f, 600.0f, 0.0f);
				Basic.ApplyBoneFilter.Add(MakeBoneRef(TEXT("chain_2")));
				if (bIgnore)
				{
					Basic.IgnoreBoneFilter.Add(MakeBoneRef(TEXT("chain_2")));
				}
			}
		};
		return Options;
	}

// PW-6 / RI-3: 本番構築の分割・末端ダミーを実親で判定する。修正前は両ダミーの Apply 判定が false。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceBoneFilterDummyNodeTest,
	"KawaiiPhysics.ExternalForce.BoneFilterDummyNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceBoneFilterDummyNodeTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture Forced(MakeDummyNodeOptions(true, false));
	Forced.EvaluateFrame();
	FFilterProbe Probe;
	Probe.ApplyBoneFilter.Add(MakeBoneRef(TEXT("chain_2")));
	Probe.RefreshBoneFilterCache(Forced.Node);
	bool bSplitIncluded = false;
	bool bTipIncluded = false;
	bool bAllIgnored = true;
	for (int32 Index = 0; Index < Forced.Node.ModifyBones.Num(); ++Index)
	{
		const FKawaiiPhysicsModifyBone& Bone = Forced.Node.ModifyBones[Index];
		if (!Bone.bDummy || !Forced.Node.ModifyBones.IsValidIndex(Bone.InterBoneRealParentIndex) ||
			Forced.Node.ModifyBones[Bone.InterBoneRealParentIndex].BoneRef.BoneName != TEXT("chain_2"))
		{
			continue;
		}
		bSplitIncluded |= Bone.bInterBoneDummy && Probe.Allows(Bone);
		bTipIncluded |= !Bone.bInterBoneDummy && Probe.Allows(Bone);
	}
	Probe.ApplyBoneFilter.Reset();
	Probe.IgnoreBoneFilter.Add(MakeBoneRef(TEXT("chain_2")));
	Probe.RefreshBoneFilterCache(Forced.Node);
	for (const FKawaiiPhysicsModifyBone& Bone : Forced.Node.ModifyBones)
	{
		if (Bone.bDummy && Forced.Node.ModifyBones.IsValidIndex(Bone.InterBoneRealParentIndex) &&
			Forced.Node.ModifyBones[Bone.InterBoneRealParentIndex].BoneRef.BoneName == TEXT("chain_2"))
		{
			bAllIgnored &= !Probe.Allows(Bone);
		}
	}
	const auto& Basic = Forced.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce_Basic>();
	return TestTrue(TEXT("node built its force cache"), FKawaiiPhysicsTestAccessor::IsBoneFilterCacheValid(Basic))
		& TestTrue(TEXT("real parent's split dummy is included"), bSplitIncluded)
		& TestTrue(TEXT("real parent's tip dummy is included"), bTipIncluded)
		& TestTrue(TEXT("real parent's dummies are ignored"), bAllIgnored);
}

// RI-3: Apply と Ignore に同じ実親があれば親とダミーを除外する。修正前はダミーが Ignore に一致せず対象になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceBoneFilterIgnoreWinsTest,
	"KawaiiPhysics.ExternalForce.BoneFilterIgnoreWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceBoneFilterIgnoreWinsTest::RunTest(const FString& Parameters)
{
	FFilterFixture Fixture;
	Fixture.A.Bone(3).bDummy = true;
	Fixture.A.Bone(3).BoneRef.BoneName = NAME_None;
	Fixture.A.Bone(3).InterBoneRealParentIndex = 1;
	// None を Apply に入れておくと、修正前は名前 None のダミーが Apply に一致し Ignore に一致せず対象になる
	Fixture.GetWind()->ApplyBoneFilter.Add(MakeBoneRef(NAME_None));
	Fixture.GetWind()->ApplyBoneFilter.Add(MakeBoneRef(FilterBoneName(1)));
	Fixture.GetWind()->IgnoreBoneFilter.Add(MakeBoneRef(FilterBoneName(1)));
	return ExpectApplied(*this, TEXT("IgnoreWins"), Fixture.AppliedBones(), {});
}

// PW-6 / RI-3: 分割なし末端、分割あり末端、分割ダミー親の末端を実親で判定する。修正前は Apply が全て false。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceBoneFilterTipPathsTest,
	"KawaiiPhysics.ExternalForce.BoneFilterTipPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceBoneFilterTipPathsTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.ModifyBones.SetNum(4);
	auto& Bones = Accessor.Node.ModifyBones;
	for (int32 Index = 0; Index < Bones.Num(); ++Index) { Bones[Index].Index = Index; }
	Bones[0].BoneRef.BoneName = TEXT("RealParent");
	Bones[1].bDummy = true;
	Bones[1].ParentIndex = 0;
	Bones[1].InterBoneRealParentIndex = 0;
	Bones[2].bDummy = true;
	Bones[2].ParentIndex = 0;
	Bones[3].bDummy = true;
	Bones[3].ParentIndex = 1;
	FFilterProbe Probe;
	Probe.ApplyBoneFilter.Add(MakeBoneRef(TEXT("RealParent")));
	Probe.RefreshBoneFilterCache(Accessor.Node);
	bool bOk = true;
	for (int32 Index = 1; Index < Bones.Num(); ++Index)
	{
		bOk &= TestTrue(FString::Printf(TEXT("tip path %d resolves to real parent"), Index), Probe.Allows(Bones[Index]));
	}
	return bOk;
}

// PW-6 / RI-3: collision-only 分割ダミーは Simulation.cpp:973-984 で Simulate を飛ばし Apply に届かない。修正前も同じ。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceBoneFilterCollisionOnlyTest,
	"KawaiiPhysics.ExternalForce.BoneFilterCollisionOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceBoneFilterCollisionOnlyTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeDummyNodeOptions(true, false, true));
	Fixture.EvaluateFrame();
	bool bSawCollisionOnly = false;
	bool bOk = true;
	for (const FKawaiiPhysicsModifyBone& Bone : Fixture.Node.ModifyBones)
	{
		if (!Bone.bInterBoneDummy) { continue; }
		bSawCollisionOnly = true;
		const auto& Bones = Fixture.Node.ModifyBones;
		if (Bones.IsValidIndex(Bone.InterBoneRealParentIndex) && Bones.IsValidIndex(Bone.InterBoneRealChildIndex))
		{
			const FVector Expected = FMath::Lerp(Bones[Bone.InterBoneRealParentIndex].Location,
				Bones[Bone.InterBoneRealChildIndex].Location, Bone.InterBoneAlpha);
			bOk &= TestTrue(TEXT("collision-only dummy is placed from endpoints"), Bone.Location.Equals(Expected, 0.001f));
		}
	}
	return TestTrue(TEXT("production node created collision-only dummies"), bSawCollisionOnly) & bOk;
}

// TW-RI-13: 固定 Seed の 1000 構成で表と独立した線形判定が一致する。修正前は実親付きダミーで不一致。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceBoneFilterRandomTableTest,
	"KawaiiPhysics.ExternalForce.BoneFilterRandomTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceBoneFilterRandomTableTest::RunTest(const FString& Parameters)
{
	FRandomStream Random(260122);
	FKawaiiPhysicsTestAccessor Accessor;
	FFilterProbe Probe;
	for (int32 Case = 0; Case < 1000; ++Case)
	{
		auto& Bones = Accessor.Node.ModifyBones;
		Bones.SetNum(Random.RandRange(2, 10));
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			FKawaiiPhysicsModifyBone& Bone = Bones[Index];
			Bone = FKawaiiPhysicsModifyBone();
			Bone.Index = Index;
			Bone.bDummy = Index > 0 && Random.RandRange(0, 3) != 0;
			Bone.bBridgeDummy = Bone.bDummy && Random.RandRange(0, 5) == 0;
			Bone.bInterBoneDummy = Bone.bDummy && !Bone.bBridgeDummy && Random.RandRange(0, 1) == 0;
			Bone.BoneRef.BoneName = Bone.bDummy ? NAME_None : FilterBoneName(Random.RandRange(0, 2));
			Bone.ParentIndex = Random.RandRange(-2, Bones.Num());
			Bone.InterBoneRealParentIndex = Random.RandRange(-2, Bones.Num());
		}
		Probe.ApplyBoneFilter.Reset();
		Probe.IgnoreBoneFilter.Reset();
		for (int32 Index = 0, Count = Random.RandRange(0, 3); Index < Count; ++Index)
		{
			const int32 Choice = Random.RandRange(0, 4);
			Probe.ApplyBoneFilter.Add(MakeBoneRef(Choice == 3 ? NAME_None :
				Choice == 4 ? FName(TEXT("Missing")) : FilterBoneName(Choice)));
		}
		for (int32 Index = 0, Count = Random.RandRange(0, 3); Index < Count; ++Index)
		{
			const int32 Choice = Random.RandRange(0, 4);
			Probe.IgnoreBoneFilter.Add(MakeBoneRef(Choice == 3 ? NAME_None :
				Choice == 4 ? FName(TEXT("Missing")) : FilterBoneName(Choice)));
		}
		Probe.RefreshBoneFilterCache(Accessor.Node);
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			if (!TestEqual(FString::Printf(TEXT("case %d bone %d"), Case, Index),
				Probe.Allows(Bones[Index]), ExpectedAllows(Probe, Bones, Index))) { return false; }
		}
	}
	return true;
}

// TW-RI-17: 名前列が同じでもダミーの解決先変更で表を作り直す。修正前は古い判定が残る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceBoneFilterParentKeyTest,
	"KawaiiPhysics.ExternalForce.BoneFilterParentKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceBoneFilterParentKeyTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	auto& Bones = Accessor.Node.ModifyBones;
	Bones.SetNum(3);
	for (int32 Index = 0; Index < Bones.Num(); ++Index) { Bones[Index].Index = Index; }
	Bones[0].BoneRef.BoneName = TEXT("Included");
	Bones[1].BoneRef.BoneName = TEXT("Excluded");
	Bones[2].bDummy = true;
	Bones[2].InterBoneRealParentIndex = 0;
	FFilterProbe Probe;
	Probe.ApplyBoneFilter.Add(MakeBoneRef(TEXT("Included")));
	Probe.RefreshBoneFilterCache(Accessor.Node);
	const bool bBefore = Probe.Allows(Bones[2]);
	Bones[2].InterBoneRealParentIndex = 1;
	Probe.RefreshBoneFilterCache(Accessor.Node);
	return TestTrue(TEXT("initial parent is included"), bBefore)
		& TestFalse(TEXT("new parent is excluded"), Probe.Allows(Bones[2]));
}

	struct FUserFilterForce : FKawaiiPhysics_ExternalForce
	{
		virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override
		{
			FKawaiiPhysics_ExternalForce::PreApply(Node, PoseContext);
		}
		virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
			FComponentSpacePoseContext& PoseContext,
			const FTransform& BoneTM = FTransform::Identity) override
		{
			if (CanApply(Bone)) { Bone.Location.Y += 1.0f; }
		}
	};

// RI-3: 利用者の派生外力も CanApply の表から実親を引く。修正前はダミーの Apply が発火しない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceBoneFilterCustomForceTest,
	"KawaiiPhysics.ExternalForce.BoneFilterCustomForce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceBoneFilterCustomForceTest::RunTest(const FString& Parameters)
{
	FFilterFixture Fixture;
	Fixture.A.Bone(3).bDummy = true;
	Fixture.A.Bone(3).BoneRef.BoneName = NAME_None;
	Fixture.A.Bone(3).InterBoneRealParentIndex = 1;
	FUserFilterForce Custom;
	Custom.ApplyBoneFilter.Add(MakeBoneRef(FilterBoneName(1)));
	Custom.PreApply(Fixture.A.Node, Fixture.PoseContext);
	Custom.Apply(Fixture.A.Bone(3), Fixture.A.Node, Fixture.PoseContext);
	return TestTrue(TEXT("custom force applies through CanApply"), Fixture.A.Bone(3).Location.Y > 0.0f);
}

// RI-4: Refresh 前の CanApply は自分の BoneRef を見る。修正前も同じ判定。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsExternalForceBoneFilterUncachedTest,
	"KawaiiPhysics.ExternalForce.BoneFilterUncached",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsExternalForceBoneFilterUncachedTest::RunTest(const FString& Parameters)
{
	FFilterProbe Probe;
	FKawaiiPhysicsModifyBone Dummy;
	Dummy.bDummy = true;
	Probe.ApplyBoneFilter.Add(MakeBoneRef(TEXT("Parent")));
	const bool bApply = Probe.Allows(Dummy);
	Probe.ApplyBoneFilter.Reset();
	Probe.IgnoreBoneFilter.Add(MakeBoneRef(TEXT("Parent")));
	return TestFalse(TEXT("uncached Apply uses None"), bApply)
		& TestTrue(TEXT("uncached Ignore uses None"), Probe.Allows(Dummy));
}

} // namespace KawaiiPhysicsExternalForceFilterCacheTestPrivate

#endif
