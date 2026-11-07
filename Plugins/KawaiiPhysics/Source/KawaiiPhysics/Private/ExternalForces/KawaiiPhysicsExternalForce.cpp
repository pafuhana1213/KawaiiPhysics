// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce.h"

#include "AnimNode_KawaiiPhysics.h"

#include "Math/UnrealMathUtility.h"
#include "Misc/AssertionMacros.h"

#if WITH_EDITOR
#include "SceneManagement.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce)

void FKawaiiPhysics_ExternalForce::Initialize(const FAnimationInitializeContext& Context)
{
}

void FKawaiiPhysics_ExternalForce::PreApply(FAnimNode_KawaiiPhysics& Node,
                                            FComponentSpacePoseContext& PoseContext)
{
	ComponentTransform = PoseContext.AnimInstanceProxy->GetComponentTransform();
	// 非対応の外力（ProceduralWind等）ではグローバル乱数を消費せず1固定にする（乱数列への副作用も残さない）
	RandomizedForceScale = bSupportsRandomForceScaleRange
		                       ? FMath::RandRange(RandomForceScaleRange.Min, RandomForceScaleRange.Max)
		                       : 1.0f;
	// 派生の PreApply 内で CanApply を使う外力（Wind など）向けに、ここでも判定表を最新にしておく。
	// ノードは PreApply の後にもう一度呼ぶので、派生側が Super の後でフィルタを変えても同じフレームから反映される
	RefreshBoneFilterCache(Node);
}

namespace KawaiiPhysicsExternalForcePrivate
{
	bool FilterNamesMatch(const TArray<FName>& CachedNames, const TArray<FBoneReference>& Filter)
	{
		if (CachedNames.Num() != Filter.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Filter.Num(); ++Index)
		{
			if (CachedNames[Index] != Filter[Index].BoneName)
			{
				return false;
			}
		}
		return true;
	}

	void CopyFilterNames(TArray<FName>& OutNames, const TArray<FBoneReference>& Filter)
	{
		OutNames.Reset(Filter.Num());
		for (const FBoneReference& BoneRef : Filter)
		{
			OutNames.Add(BoneRef.BoneName);
		}
	}
}

void FKawaiiPhysics_ExternalForce::RefreshBoneFilterCache(const FAnimNode_KawaiiPhysics& Node)
{
	using namespace KawaiiPhysicsExternalForcePrivate;

	// フィルタが空なら CanApply は探索せずに true を返すのでキャッシュは要らない
	if (ApplyBoneFilter.IsEmpty() && IgnoreBoneFilter.IsEmpty())
	{
		bBoneFilterCacheValid = false;
		return;
	}

	// 配列の確保先ではなく中身で比べる（同じ要素数の代入や要素の書き換えでも確保先は変わらないことがある）。
	// 比較はフィルタ長＋ボーン数に比例するだけで、ボーン数×フィルタ長の探索よりずっと軽い
	const TArray<FKawaiiPhysicsModifyBone>& Bones = Node.ModifyBones;
	bool bValid = bBoneFilterCacheValid
		&& BoneFilterCacheBoneNames.Num() == Bones.Num()
		&& FilterNamesMatch(BoneFilterCacheApplyNames, ApplyBoneFilter)
		&& FilterNamesMatch(BoneFilterCacheIgnoreNames, IgnoreBoneFilter);
	for (int32 Index = 0; bValid && Index < Bones.Num(); ++Index)
	{
		bValid = BoneFilterCacheBoneNames[Index] == Bones[Index].BoneRef.BoneName;
	}
	if (bValid)
	{
		return;
	}

	CopyFilterNames(BoneFilterCacheApplyNames, ApplyBoneFilter);
	CopyFilterNames(BoneFilterCacheIgnoreNames, IgnoreBoneFilter);
	BoneFilterCacheBoneNames.Reset(Bones.Num());
	BoneFilterCacheResults.Init(false, Bones.Num());
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		BoneFilterCacheBoneNames.Add(Bones[Index].BoneRef.BoneName);
		BoneFilterCacheResults[Index] = EvaluateBoneFilters(Bones[Index]);
	}
	bBoneFilterCacheValid = true;
}

void FKawaiiPhysics_ExternalForce::ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                   FComponentSpacePoseContext& PoseContext,
                                                   FVector& InOutVelocity)
{
}

void FKawaiiPhysics_ExternalForce::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                         FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM)
{
}

void FKawaiiPhysics_ExternalForce::PostApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext)
{
	if (bIsOneShot)
	{
#if ENABLE_ANIM_DEBUG
		// 1回限りの外力はノードの毎フレーム描画より前に取り除かれるため、ここで描く。
		// このフレームの出力位置はまだ無いのでシミュレーション位置を使う（ApplySimulateResult で上書きされる）
		if (IsDebugEnabled())
		{
			for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
			{
				Bone.OutputLocation = Bone.Location;
				AnimDrawDebug(Bone, Node, PoseContext);
			}
		}
#endif
		bRemovalRequestedAfterPostApply = true;
	}
}

bool FKawaiiPhysics_ExternalForce::IsDebugEnabled(bool bInPersona)
{
	if (bInPersona)
	{
		return bDrawDebug && bIsEnabled;
	}

#if ENABLE_ANIM_DEBUG
	if (CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
	{
		return bDrawDebug && bIsEnabled;
	}
#endif

	return false;
}

FVector FKawaiiPhysics_ExternalForce::ConvertExternalForceToSimulationSpace(FAnimNode_KawaiiPhysics& Node,
                                                                            FComponentSpacePoseContext& PoseContext,
                                                                            const FVector& InForce) const
{
	// BoneSpace はボーンローカルのまま返し、Apply 側の BoneTM で1回だけシミュレーション空間へ変換する
	if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		return InForce;
	}

	// ExternalForceSpaceに対応する変換元のSimulationSpaceを決定
	EKawaiiPhysicsSimulationSpace From = EKawaiiPhysicsSimulationSpace::ComponentSpace;
	if (ExternalForceSpace == EExternalForceSpace::WorldSpace)
	{
		From = EKawaiiPhysicsSimulationSpace::WorldSpace;
	}

	// 変換元からNode.SimulationSpaceへ変換して返す
	return Node.ConvertSimulationSpaceVector(PoseContext, From,
	                                         Node.SimulationSpace, InForce);
}

#if ENABLE_ANIM_DEBUG
FName FKawaiiPhysics_ExternalForce::GetBoneForceKey(const FKawaiiPhysicsModifyBone& Bone)
{
	return Bone.bDummy ? FName(NAME_None, Bone.Index + 1) : Bone.BoneRef.BoneName;
}

void FKawaiiPhysics_ExternalForce::AnimDrawDebug(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                 const FComponentSpacePoseContext& PoseContext)
{
	if (IsDebugEnabled() && CanApply(Bone))
	{
		// BoneForceMapに該当ボーンが無い場合はnull参照を避ける（EditMode版と同様にガード）
		// 風は基底の Force を使わないため、ボーンごとの値で判定する
		const FVector* ForcePtr = BoneForceMap.Find(GetBoneForceKey(Bone));
		if (!ForcePtr || ForcePtr->IsZero())
		{
			return;
		}

		const auto AnimInstanceProxy = PoseContext.AnimInstanceProxy;
		bool bEvaluated = false;
		const FTransform SimulationToWorld =
			Node.GetSimulationSpace2ComponentSpace(bEvaluated) * AnimInstanceProxy->GetComponentTransform();
		// ボーンの表示と揃うよう、シミュレーション状態ではなく出力位置へ描く
		const FVector ModifyRootBoneLocationWS = SimulationToWorld.TransformPosition(Bone.OutputLocation);

		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			ModifyRootBoneLocationWS + DebugArrowOffset,
			ModifyRootBoneLocationWS + DebugArrowOffset + SimulationToWorld.TransformVectorNoScale(*ForcePtr).GetSafeNormal() *
			DebugArrowLength,
			DebugArrowSize, FColor::Red, false, 0.f, 2);
	}
}
#endif

#if WITH_EDITOR
void FKawaiiPhysics_ExternalForce::AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
                                                            const FAnimNode_KawaiiPhysics& Node,
                                                            FPrimitiveDrawInterface* PDI)
{
	// エディタ描画は評価スレッドと別に動くため、作り直し中かもしれないキャッシュではなく線形探索で判定する
	if (IsDebugEnabled(true) && EvaluateBoneFilters(ModifyBone))
	{
		const FVector* ForcePtr = BoneForceMap.Find(GetBoneForceKey(ModifyBone));
		if (!ForcePtr || ForcePtr->IsNearlyZero())
		{
			return;
		}

		FTransform ArrowTransform = FTransform(
			ForcePtr->GetSafeNormal().ToOrientationRotator(),
			ModifyBone.OutputLocation + DebugArrowOffset);
		if (Node.SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
		{
			ArrowTransform = ArrowTransform * Node.GetBaseBoneSpace2ComponentSpace();
		}
		DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
		                     SDPG_Foreground, 1.0f);
	}
}
#endif

bool FKawaiiPhysics_ExternalForce::CanApply(const FKawaiiPhysicsModifyBone& Bone) const
{
	if (ApplyBoneFilter.IsEmpty() && IgnoreBoneFilter.IsEmpty())
	{
		return true;
	}

	// PreApply で作った結果を使う。ボーン名も照合するので、その後にボーンリストが作り直されても誤判定しない
	if (bBoneFilterCacheValid && BoneFilterCacheBoneNames.IsValidIndex(Bone.Index)
		&& BoneFilterCacheBoneNames[Bone.Index] == Bone.BoneRef.BoneName)
	{
		return BoneFilterCacheResults[Bone.Index];
	}
	return EvaluateBoneFilters(Bone);
}

bool FKawaiiPhysics_ExternalForce::EvaluateBoneFilters(const FKawaiiPhysicsModifyBone& Bone) const
{
	if (!ApplyBoneFilter.IsEmpty() && !ApplyBoneFilter.Contains(Bone.BoneRef))
	{
		return false;
	}

	if (!IgnoreBoneFilter.IsEmpty() && IgnoreBoneFilter.Contains(Bone.BoneRef))
	{
		return false;
	}

	return true;
}
