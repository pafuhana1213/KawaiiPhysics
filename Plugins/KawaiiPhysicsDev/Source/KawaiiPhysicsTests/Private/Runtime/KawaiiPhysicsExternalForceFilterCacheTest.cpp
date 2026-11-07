// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsHairNodeFixture.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Misc/MemStack.h"

// 外力のボーンフィルタ判定表（RefreshBoneFilterCache）が、線形探索と同じ結果を返し続けるかを確かめる。
namespace KawaiiPhysicsExternalForceFilterCacheTestPrivate
{
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
	// ダミーボーン（名前を持たない）はフィルタに一致しない
	{
		FFilterFixture F;
		F.A.Bone(3).bDummy = true;
		F.A.Bone(3).BoneRef.BoneName = NAME_None;
		bOk &= ExpectApplied(*this, TEXT("Dummy.ApplyFilter"), F.AppliedBones(), Before);
		F.GetWind()->ApplyBoneFilter.Reset();
		F.GetWind()->IgnoreBoneFilter.Add(MakeBoneRef(FilterBoneName(1)));
		bOk &= ExpectApplied(*this, TEXT("Dummy.IgnoreFilter"), F.AppliedBones(), (TArray<int32>{2, 3}));
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

	UKawaiiPhysicsDeveloperSettings* Settings = GetMutableDefault<UKawaiiPhysicsDeveloperSettings>();
	const bool bOldFixed = Settings->bUseFixedSubstepping;
	Settings->bUseFixedSubstepping = true;

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
	Settings->bUseFixedSubstepping = bOldFixed;
	return bOk;
}

} // namespace KawaiiPhysicsExternalForceFilterCacheTestPrivate

#endif
