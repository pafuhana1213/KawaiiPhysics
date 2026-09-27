// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysics.h"

#include "AnimationRuntime.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Animation/AnimInstanceProxy.h"
#include "Curves/CurveFloat.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsSettings.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Animation/AnimInstance.h"
#endif

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

#include "KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsInternal.h"

void FAnimNode_KawaiiPhysics::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	auto Initialize = [&RequiredBones](auto& Targets)
	{
		for (auto& Target : Targets)
		{
			Target.DrivingBone.Initialize(RequiredBones);
		}
		return true;
	};

	RootBone.Initialize(RequiredBones);
	for (auto& AdditionalRootBone : AdditionalRootBones)
	{
		AdditionalRootBone.RootBone.Initialize(RequiredBones);
	}
	for (auto& Bone : ModifyBones)
	{
		Bone.BoneRef.Initialize(RequiredBones);
	}

	SimulationBaseBone.Initialize(RequiredBones);

	Initialize(SphericalLimits);
	Initialize(CapsuleLimits);
	Initialize(TaperedCapsuleLimits);
	Initialize(BoxLimits);
	Initialize(PlanarLimits);

	for (auto& BoneConstraint : BoneConstraints)
	{
		BoneConstraint.InitializeBone(RequiredBones);
	}

	for (auto& SyncBone : SyncBones)
	{
		SyncBone.Bone.Initialize(RequiredBones);
		for (auto& Target : SyncBone.TargetRoots)
		{
			Target.Bone.Initialize(RequiredBones);
		}
	}
}

void FAnimNode_KawaiiPhysics::InitModifyBones(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitModifyBones);

	// https://github.com/pafuhana1213/KawaiiPhysics/issues/174
	// SkeletonAssetがnull（クック失敗/参照切れ）の場合はBoneContainerのRefSkeletonにフォールバックしてクラッシュを避ける
	const USkeleton* SkeletonAsset =
		CVarAnimNodeKawaiiPhysicsUseBoneContainerRefSkeletonWhenInit.GetValueOnAnyThread()
			? nullptr
			: BoneContainer.GetSkeletonAsset();
	const FReferenceSkeleton& RefSkeleton =
		SkeletonAsset ? SkeletonAsset->GetReferenceSkeleton() : BoneContainer.GetReferenceSkeleton();

	auto InitRootBone = [&](const FName& RootBoneName, const TArray<FBoneReference>& InExcludeBones)
	{
		TArray<FKawaiiPhysicsModifyBone> Bones;
		AddModifyBone(Bones, Output, BoneContainer, RefSkeleton, RefSkeleton.FindBoneIndex(RootBoneName),
		              InExcludeBones);
		if (Bones.Num() > 0)
		{
			float TotalBoneLength = 0.0f;
			CalcBoneLength(Bones[0], Bones, BoneContainer.GetRefPoseArray(), TotalBoneLength);

			for (auto& Bone : Bones)
			{
				if (Bone.LengthFromRoot > 0.0f)
				{
					Bone.LengthRateFromRoot = Bone.LengthFromRoot / TotalBoneLength;
				}

				Bone.Index += ModifyBones.Num();
				if (Bone.ParentIndex >= 0)
				{
					Bone.ParentIndex += ModifyBones.Num();
				}
				for (auto& ChildIndex : Bone.ChildIndices)
				{
					ChildIndex += ModifyBones.Num();
				}
				if (Bone.InterBoneRealParentIndex >= 0)
				{
					Bone.InterBoneRealParentIndex += ModifyBones.Num();
				}
				if (Bone.InterBoneRealChildIndex >= 0)
				{
					Bone.InterBoneRealChildIndex += ModifyBones.Num();
				}
			}
			ModifyBones.Append(Bones);
		}
	};

	ModifyBones.Empty();
	InitRootBone(RootBone.BoneName, ExcludeBones);
	for (auto& AdditionalRootBone : AdditionalRootBones)
	{
		InitRootBone(AdditionalRootBone.RootBone.BoneName,
		             AdditionalRootBone.bUseOverrideExcludeBones
			             ? AdditionalRootBone.OverrideExcludeBones
			             : ExcludeBones);
	}
}

int32 FAnimNode_KawaiiPhysics::AddModifyBone(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                             FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                             const FReferenceSkeleton& RefSkeleton, int32 BoneIndex,
                                             const TArray<FBoneReference>& InExcludeBones)
{
	if (BoneIndex < 0 || RefSkeleton.GetNum() <= BoneIndex)
	{
		return INDEX_NONE;
	}

	FBoneReference BoneRef;
	BoneRef.BoneName = RefSkeleton.GetBoneName(BoneIndex);

	if (InExcludeBones.Num() > 0 && InExcludeBones.Find(BoneRef) >= 0)
	{
		return INDEX_NONE;
	}

	FKawaiiPhysicsModifyBone NewModifyBone;
	NewModifyBone.BoneRef = BoneRef;
	NewModifyBone.BoneRef.Initialize(BoneContainer);
	if (NewModifyBone.BoneRef.CachedCompactPoseIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	FTransform RefBonePoseTransform =
		GetBoneTransformInSimSpace(Output, NewModifyBone.BoneRef.CachedCompactPoseIndex);

	NewModifyBone.Location = RefBonePoseTransform.GetLocation();
	NewModifyBone.PrevLocation = NewModifyBone.Location;
	NewModifyBone.PoseLocation = NewModifyBone.Location;
	NewModifyBone.PrevRotation = RefBonePoseTransform.GetRotation();
	NewModifyBone.PoseRotation = NewModifyBone.PrevRotation;
	NewModifyBone.PoseScale = RefBonePoseTransform.GetScale3D();

	int32 ModifyBoneIndex = InModifyBones.Add(NewModifyBone);
	InModifyBones[ModifyBoneIndex].Index = ModifyBoneIndex;

	TArray<int32> ChildBoneIndices;
	CollectChildBones(RefSkeleton, BoneIndex, ChildBoneIndices);
	bool AddedChildBone = false;
	if (ChildBoneIndices.Num() > 0)
	{
		// スキニングウェイトを持たない末端ボーンでは ChildBoneIndices > 0 でも実子は生成されない
		for (auto ChildBoneIndex : ChildBoneIndices)
		{
			TArray<int32> InsertedInterBoneDummyIndices;
			const int32 EffectiveParentIndex = InsertInterBoneDummyBones(InModifyBones, Output, BoneContainer,
			                                                            RefSkeleton, ModifyBoneIndex, ChildBoneIndex,
			                                                            InsertedInterBoneDummyIndices);

			// 子ボーンの再帰追加
			int32 ChildModifyBoneIndex = AddModifyBone(InModifyBones, Output, BoneContainer, RefSkeleton,
			                                           ChildBoneIndex,
			                                           InExcludeBones);
			if (ChildModifyBoneIndex >= 0)
			{
				InModifyBones[EffectiveParentIndex].ChildIndices.Add(ChildModifyBoneIndex);
				InModifyBones[ChildModifyBoneIndex].ParentIndex = EffectiveParentIndex;
				AddedChildBone = true;

				FinalizeInterBoneDummyBones(InModifyBones, InsertedInterBoneDummyIndices, ChildModifyBoneIndex);
			}
			else if (InsertedInterBoneDummyIndices.Num() > 0)
			{
				RollbackInterBoneDummyBones(InModifyBones, ModifyBoneIndex, InsertedInterBoneDummyIndices);
			}
		}
	}

	if (!AddedChildBone && DummyBoneLength > 0.0f)
	{
		// 末端ダミーの位置（実ボーンから前方へ DummyBoneLength）
		const FVector TipLocation = NewModifyBone.Location + GetBoneForwardVector(NewModifyBone.PrevRotation) *
			DummyBoneLength;

		// 実ボーンと末端ダミーの間にもインターボーンダミーを挿入（実ボーン区間と同様に分割）
		TArray<int32> InsertedInterBoneDummyIndices;
		const int32 EffectiveParentIndex = InsertInterBoneDummyBonesCore(
			InModifyBones, ModifyBoneIndex, TipLocation, NewModifyBone.PrevRotation,
			RefBonePoseTransform.GetScale3D(), DummyBoneLength, InsertedInterBoneDummyIndices);
		const int32 InsertedCount = InsertedInterBoneDummyIndices.Num();

		// 末端ダミーの ModifyBone を追加
		FKawaiiPhysicsModifyBone DummyModifyBone;
		DummyModifyBone.bDummy = true;
		DummyModifyBone.Location = TipLocation;
		DummyModifyBone.PrevLocation = DummyModifyBone.Location;
		DummyModifyBone.PoseLocation = DummyModifyBone.Location;
		DummyModifyBone.PrevRotation = NewModifyBone.PrevRotation;
		DummyModifyBone.PoseRotation = DummyModifyBone.PrevRotation;
		DummyModifyBone.PoseScale = RefBonePoseTransform.GetScale3D();
		if (InsertedCount > 0)
		{
			// 分割時: 末端ダミーは最後のインターボーンダミーの子。BoneLength は最終セグメント長
			DummyModifyBone.InterBoneRealParentIndex = ModifyBoneIndex;
			DummyModifyBone.BoneLength = DummyBoneLength / (InsertedCount + 1);
		}

		int32 DummyBoneIndex = InModifyBones.Add(DummyModifyBone);
		InModifyBones[EffectiveParentIndex].ChildIndices.Add(DummyBoneIndex);
		InModifyBones[DummyBoneIndex].Index = DummyBoneIndex;
		InModifyBones[DummyBoneIndex].ParentIndex = EffectiveParentIndex;

		if (InsertedCount > 0)
		{
			// 挿入したインターボーンダミーの InterBoneRealChildIndex を末端ダミーに向ける
			FinalizeInterBoneDummyBones(InModifyBones, InsertedInterBoneDummyIndices, DummyBoneIndex);
		}
	}


	return ModifyBoneIndex;
}

int32 FAnimNode_KawaiiPhysics::InsertInterBoneDummyBones(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                                         FComponentSpacePoseContext& Output,
                                                         const FBoneContainer& BoneContainer,
                                                         const FReferenceSkeleton& RefSkeleton,
                                                         const int32 ParentModifyBoneIndex,
                                                         const int32 ChildBoneIndex,
                                                         TArray<int32>& OutInsertedInterBoneDummyIndices) const
{
	OutInsertedInterBoneDummyIndices.Reset();

	int32 EffectiveParentIndex = ParentModifyBoneIndex;
	if (BoneSubdivisionCount <= 0)
	{
		return EffectiveParentIndex;
	}

	// 子ボーンのRefPose位置を取得（バリデーションはAddModifyBoneに任せる）
	FBoneReference ChildRef;
	ChildRef.BoneName = RefSkeleton.GetBoneName(ChildBoneIndex);
	ChildRef.Initialize(BoneContainer);

	if (ChildRef.CachedCompactPoseIndex == INDEX_NONE)
	{
		return EffectiveParentIndex;
	}

	const FTransform ChildTransform = GetBoneTransformInSimSpace(Output, ChildRef.CachedCompactPoseIndex);
	const FVector ChildLocation = ChildTransform.GetLocation();
	const FVector ParentLocation = InModifyBones[ParentModifyBoneIndex].Location;
	const float Distance = (ChildLocation - ParentLocation).Size();

	const FQuat ChildRotation = ChildTransform.GetRotation();
	const FVector ChildScale = ChildTransform.GetScale3D();

	// 実子の位置・回転・スケールを明示的に渡してコア処理に委譲
	return InsertInterBoneDummyBonesCore(InModifyBones, ParentModifyBoneIndex, ChildLocation, ChildRotation, ChildScale,
	                                     Distance, OutInsertedInterBoneDummyIndices);
}

int32 FAnimNode_KawaiiPhysics::InsertInterBoneDummyBonesCore(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                                             const int32 ParentModifyBoneIndex,
                                                             const FVector& ChildLocation,
                                                             const FQuat& ChildRotation,
                                                             const FVector& ChildScale,
                                                             const float Distance,
                                                             TArray<int32>& OutInsertedInterBoneDummyIndices) const
{
	// 縦方向ダミーボーン（BoneSubdivision）挿入コスト（初期化時のみ）。
	// 両呼び出し元が通る共通Coreにのみ計測を置き、同一STATの二重計上を避ける。
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InsertInterBoneDummyBones);

	OutInsertedInterBoneDummyIndices.Reset();

	int32 EffectiveParentIndex = ParentModifyBoneIndex;
	if (BoneSubdivisionCount <= 0)
	{
		return EffectiveParentIndex;
	}

	// 最小配置数 = 指定数。bBoneSubdivisionCollisionOnly は積分挙動のみに作用し、配置数には影響しない。
	// 0距離区間（座標が重なる実ボーン間）はダミーが同一点に乗るだけなので 0。
	int32 EffectiveCount = (Distance > KINDA_SMALL_NUMBER) ? FMath::Clamp(BoneSubdivisionCount, 0, 10) : 0;

	// bBoneSubdivisionDensifyByRadius: 半径に対しボーン間が離れた区間では、コリジョン球が隙間なく並ぶよう
	// BoneSubdivisionCount を最小として追加配置する（coverage確保）。近接区間は最小のまま。
	if (bBoneSubdivisionDensifyByRadius && Distance > KINDA_SMALL_NUMBER)
	{
		// 被覆漏れを防ぐため実効半径は保守的に見積もる（隙間より過剰配置側に倒す）。
		// LengthRateFromRoot が未確定なため、両端＋[0,1]内の全キーを走査しカーブ全体の最小スケールを採る。
		const FRichCurve* RadiusCurve = RadiusCurveData.GetRichCurveConst();
		float MinRadiusCurveScale = FMath::Min(RadiusCurve->Eval(0.0f, 1.0f), RadiusCurve->Eval(1.0f, 1.0f));
		for (const FRichCurveKey& Key : RadiusCurve->Keys)
		{
			if (Key.Time >= 0.0f && Key.Time <= 1.0f)
			{
				MinRadiusCurveScale = FMath::Min(MinRadiusCurveScale, Key.Value);
			}
		}
		const float AvgRadius = PhysicsSettings.Radius * FMath::Max(MinRadiusCurveScale, 0.0f);

		const int32 CoverageCount = CalcInterBoneDummyCoverageCount(Distance, AvgRadius);
		EffectiveCount = FMath::Max(EffectiveCount, CoverageCount);
	}

	// 暴走防止の上限（半径が極端に小さい場合の過剰生成を抑える）。
	constexpr int32 MaxInterBoneSubdivisionPerSegment = 50;
	EffectiveCount = FMath::Min(EffectiveCount, MaxInterBoneSubdivisionPerSegment);

	const FVector ParentLocation = InModifyBones[ParentModifyBoneIndex].Location;
	const FQuat ParentRotation = InModifyBones[ParentModifyBoneIndex].PrevRotation;
	const FVector ParentScale = InModifyBones[ParentModifyBoneIndex].PoseScale;

	for (int32 j = 0; j < EffectiveCount; j++)
	{
		const float LerpAlpha = static_cast<float>(j + 1) / (EffectiveCount + 1);

		FKawaiiPhysicsModifyBone InterDummy;
		InterDummy.bDummy = true;
		InterDummy.bInterBoneDummy = true;
		InterDummy.InterBoneAlpha = LerpAlpha;
		InterDummy.InterBoneRealParentIndex = ParentModifyBoneIndex;
		// InterBoneRealChildIndex は子追加後に設定
		InterDummy.Location = FMath::Lerp(ParentLocation, ChildLocation, LerpAlpha);
		InterDummy.PrevLocation = InterDummy.Location;
		InterDummy.PoseLocation = InterDummy.Location;
		InterDummy.PrevRotation = FQuat::Slerp(ParentRotation, ChildRotation, LerpAlpha);
		InterDummy.PoseRotation = InterDummy.PrevRotation;
		InterDummy.PoseScale = FMath::Lerp(ParentScale, ChildScale, LerpAlpha);
		InterDummy.BoneLength = Distance / (EffectiveCount + 1);

		const int32 DummyIdx = InModifyBones.Add(InterDummy);
		InModifyBones[DummyIdx].Index = DummyIdx;
		InModifyBones[EffectiveParentIndex].ChildIndices.Add(DummyIdx);
		InModifyBones[DummyIdx].ParentIndex = EffectiveParentIndex;
		EffectiveParentIndex = DummyIdx;
		OutInsertedInterBoneDummyIndices.Add(DummyIdx);
	}

	return EffectiveParentIndex;
}

void FAnimNode_KawaiiPhysics::FinalizeInterBoneDummyBones(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                                          const TArray<int32>& InsertedInterBoneDummyIndices,
                                                          const int32 ChildModifyBoneIndex) const
{
	// InterBoneRealChildIndexを全ダミーに設定
	for (const int32 DummyIdx : InsertedInterBoneDummyIndices)
	{
		InModifyBones[DummyIdx].InterBoneRealChildIndex = ChildModifyBoneIndex;
	}
}

void FAnimNode_KawaiiPhysics::RollbackInterBoneDummyBones(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                                          const int32 ParentModifyBoneIndex,
                                                          const TArray<int32>& InsertedInterBoneDummyIndices) const
{
	// 子ボーンが無効（Excluded等） → 挿入済みダミーをクリーンアップ
	// ダミーは配列末尾に連続しているため逆順で安全に削除可能
	for (int32 k = InsertedInterBoneDummyIndices.Num() - 1; k >= 0; k--)
	{
		const int32 DummyIdx = InsertedInterBoneDummyIndices[k];
		if (DummyIdx == InModifyBones.Num() - 1)
		{
			InModifyBones.RemoveAt(DummyIdx);
		}
	}
	InModifyBones[ParentModifyBoneIndex].ChildIndices.RemoveAll([&](int32 Idx)
	{
		return InsertedInterBoneDummyIndices.Contains(Idx);
	});
}

int32 FAnimNode_KawaiiPhysics::CollectChildBones(const FReferenceSkeleton& RefSkeleton, const int32 ParentBoneIndex,
                                                 TArray<int32>& Children) const
{
	Children.Reset();

	const int32 NumBones = RefSkeleton.GetNum();
	for (int32 ChildIndex = ParentBoneIndex + 1; ChildIndex < NumBones; ChildIndex++)
	{
		if (ParentBoneIndex == RefSkeleton.GetParentIndex(ChildIndex))
		{
			Children.Add(ChildIndex);
		}
	}

	return Children.Num();
}

void FAnimNode_KawaiiPhysics::CalcBoneLength(FKawaiiPhysicsModifyBone& Bone,
                                             TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                             const TArray<FTransform>& RefBonePose,
                                             float& TotalBoneLength)
{
	if (Bone.ParentIndex < 0)
	{
		Bone.LengthFromRoot = 0.0f;
		Bone.BoneLength = 0.0f;
	}
	else
	{
		if (!Bone.bDummy)
		{
			Bone.BoneLength = RefBonePose.IsValidIndex(Bone.BoneRef.BoneIndex)
				                  ? RefBonePose[Bone.BoneRef.BoneIndex].GetLocation().Size()
				                  : 0.0f;
		}
		else if (!Bone.bInterBoneDummy)
		{
			// tip dummy: 親がインターボーンダミー(=末端区間が分割済み)の場合、BoneLengthは
			// AddModifyBoneで最終セグメント長に設定済みなので上書きしない（LengthFromRootの二重計上防止）
			if (!InModifyBones[Bone.ParentIndex].bInterBoneDummy)
			{
				Bone.BoneLength = DummyBoneLength; // 非分割 tip dummy
			}
		}
		// else: inter-bone dummy → BoneLengthはAddModifyBoneで設定済み
		Bone.LengthFromRoot = InModifyBones[Bone.ParentIndex].LengthFromRoot + Bone.BoneLength;

		TotalBoneLength = FMath::Max(TotalBoneLength, Bone.LengthFromRoot);
	}

	for (const int32 ChildIndex : Bone.ChildIndices)
	{
		CalcBoneLength(InModifyBones[ChildIndex], InModifyBones, RefBonePose, TotalBoneLength);
	}
}


void FAnimNode_KawaiiPhysics::UpdateTipDummyPose(FKawaiiPhysicsModifyBone& Bone)
{
	// tip dummy: 分割時は即時親がインターボーンダミー(Pass2でしか確定しない)になるため、
	// 実親(InterBoneRealParentIndex)を基準に計算して循環依存を回避。非分割時はParentIndex。
	const int32 RealAncestorIndex = (Bone.InterBoneRealParentIndex >= 0)
		                                ? Bone.InterBoneRealParentIndex
		                                : Bone.ParentIndex;
	if (!ensureMsgf(ModifyBones.IsValidIndex(RealAncestorIndex),
	                TEXT("KawaiiPhysics: invalid tip-dummy real ancestor index.")))
	{
		return;
	}
	const FKawaiiPhysicsModifyBone& RealAncestor = ModifyBones[RealAncestorIndex];
	Bone.PoseLocation = RealAncestor.PoseLocation +
		GetBoneForwardVector(RealAncestor.PoseRotation) * DummyBoneLength;
	Bone.PoseRotation = RealAncestor.PoseRotation;
	Bone.PoseScale = RealAncestor.PoseScale;
}

void FAnimNode_KawaiiPhysics::UpdateInterBoneDummyPose(FKawaiiPhysicsModifyBone& Bone,
                                                       const FBoneContainer& BoneContainer)
{
	if (!ensureMsgf(ModifyBones.IsValidIndex(Bone.InterBoneRealParentIndex) &&
	                ModifyBones.IsValidIndex(Bone.InterBoneRealChildIndex),
	                TEXT("KawaiiPhysics: invalid inter-bone dummy endpoint index.")))
	{
		return;
	}

	const FKawaiiPhysicsModifyBone& RealParent = ModifyBones[Bone.InterBoneRealParentIndex];
	const FKawaiiPhysicsModifyBone& RealChild = ModifyBones[Bone.InterBoneRealChildIndex];

	// 末端ダミーを実子とする場合、tip dummyはBoneRef空でCompactPose<0になるが
	// PoseLocationは確定済みなのでLODフォールバック判定から除外する
	const bool bRealChildIsTipDummy = RealChild.bDummy && !RealChild.bInterBoneDummy;

	// LOD安全チェック: RealChildがLODで無効な場合、親のPoseにフォールバック
	const FCompactPoseBoneIndex RealChildCompactPose = RealChild.BoneRef.GetCompactPoseIndex(BoneContainer);
	if (!bRealChildIsTipDummy && RealChildCompactPose < 0)
	{
		const FKawaiiPhysicsModifyBone& ParentBone = ModifyBones[Bone.ParentIndex];
		Bone.PoseLocation = ParentBone.PoseLocation;
		Bone.PoseRotation = ParentBone.PoseRotation;
		Bone.PoseScale = ParentBone.PoseScale;
	}
	else
	{
		Bone.PoseLocation = FMath::Lerp(RealParent.PoseLocation, RealChild.PoseLocation, Bone.InterBoneAlpha);
		Bone.PoseRotation = FQuat::Slerp(RealParent.PoseRotation, RealChild.PoseRotation, Bone.InterBoneAlpha);
		Bone.PoseScale = FMath::Lerp(RealParent.PoseScale, RealChild.PoseScale, Bone.InterBoneAlpha);
	}
}

void FAnimNode_KawaiiPhysics::UpdateModifyBonesPoseTransform(FComponentSpacePoseContext& Output,
                                                             const FBoneContainer& BoneContainer)
{
	// 1パス目: 実ボーンとtip dummyのPoseLocationを更新（inter-bone dummyは両端の確定が必要）
	for (auto& Bone : ModifyBones)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateModifyBonesPoseTransform);

		// bridge dummyはPoseを更新しない（生成時のLERP値を据え置き）。
		// このガードが無いと下のtip-dummy分岐に誤って入り、DummyBoneLength分の誤ったforward-offset poseになる。
		if (Bone.bBridgeDummy)
		{
			continue;
		}

		if (Bone.bInterBoneDummy)
		{
			continue; // 2パス目で処理
		}

		if (Bone.bDummy)
		{
			UpdateTipDummyPose(Bone);
		}
		else
		{
			const auto CompactPoseIndex = Bone.BoneRef.GetCompactPoseIndex(BoneContainer);
			if (CompactPoseIndex < 0)
			{
				// ボーンの位置・回転をリセットすると、スケルトンのLOD切り替え時に問題が起きることがある #44
				if (ResetBoneTransformWhenBoneNotFound)
				{
					Bone.PoseLocation = FVector::ZeroVector;
					Bone.PoseRotation = FQuat::Identity;
					Bone.PoseScale = FVector::OneVector;
				}
				continue;
			}

			const FTransform BoneTransform = GetBoneTransformInSimSpace(Output, CompactPoseIndex);
			Bone.PoseLocation = BoneTransform.GetLocation();
			Bone.PoseRotation = BoneTransform.GetRotation();
			Bone.PoseScale = BoneTransform.GetScale3D();
		}
	}

	// 2パス目: inter-bone dummyのPoseLocationを補間（実親・実子のPoseLocationが確定済み）
	for (auto& Bone : ModifyBones)
	{
		if (!Bone.bInterBoneDummy)
		{
			continue;
		}

		UpdateInterBoneDummyPose(Bone, BoneContainer);
	}
}

void FAnimNode_KawaiiPhysics::UpdateSkelCompMove(FComponentSpacePoseContext& Output,
                                                 const FTransform& ComponentTransform)
{
	SkelCompMoveVector = ComponentTransform.InverseTransformPosition(PreSkelCompTransform.GetLocation());
	SkelCompMoveVector *= SkelCompMoveScale;
	SkelCompMoveRotation = ComponentTransform.InverseTransformRotation(PreSkelCompTransform.GetRotation());

	if (TeleportDistanceThreshold > 0 &&
		SkelCompMoveVector.SizeSquared() > TeleportDistanceThreshold * TeleportDistanceThreshold)
	{
		TeleportType = ETeleportType::TeleportPhysics;
	}

	if (TeleportRotationThreshold > 0 &&
		FMath::RadiansToDegrees(SkelCompMoveRotation.GetAngle()) > TeleportRotationThreshold)
	{
		TeleportType = ETeleportType::TeleportPhysics;
	}
}

void FAnimNode_KawaiiPhysics::AdvancePreSkelCompTransform(const FTransform& ComponentTransform,
                                                          bool bTeleportedThisFrame)
{
	// テレポートと判定したフレームは SimulationSpace やサブステップ設定によらず Component 移動を全量破棄する。
	// 消費割合だけ前進させると、未消費の残りが次フレームで閾値未満の通常移動として world move follow に適用されてしまう。
	if (bTeleportedThisFrame)
	{
		PreSkelCompTransformConsumeFraction = 1.0f;
	}

	// サブステップで未消費の実時間がある場合、PreSkelCompTransform を消費割合だけ前進させ、
	// 未適用のComponent移動を次にステップが走るフレームへ繰り越す（NumSteps==0 では割合0で据え置き）。
	const float PreSkelCompConsumeFrac = FMath::Clamp(PreSkelCompTransformConsumeFraction, 0.0f, 1.0f);
	if (PreSkelCompConsumeFrac >= 1.0f - KINDA_SMALL_NUMBER)
	{
		PreSkelCompTransform = ComponentTransform;
	}
	else
	{
		PreSkelCompTransform.SetLocation(
			FMath::Lerp(PreSkelCompTransform.GetLocation(), ComponentTransform.GetLocation(), PreSkelCompConsumeFrac));
		PreSkelCompTransform.SetRotation(
			FQuat::Slerp(PreSkelCompTransform.GetRotation(), ComponentTransform.GetRotation(), PreSkelCompConsumeFrac).GetNormalized());
		PreSkelCompTransform.SetScale3D(
			FMath::Lerp(PreSkelCompTransform.GetScale3D(), ComponentTransform.GetScale3D(), PreSkelCompConsumeFrac));
	}
}

int32 FAnimNode_KawaiiPhysics::CalcInterBoneDummyCoverageCount(float Distance, float AvgRadius) const
{
	if (Distance <= KINDA_SMALL_NUMBER || AvgRadius <= KINDA_SMALL_NUMBER)
	{
		return 0;
	}

	// N個のDummyBone → (N+1)セグメント。各セグメント長 <= 2*AvgRadius なら隣接コリジョン球が重なり隙間なく被覆。
	// Distance / (N+1) <= 2*AvgRadius → N+1 >= Distance/(2*AvgRadius) → N >= Distance/(2*AvgRadius) - 1
	const int32 CoverageCount = FMath::CeilToInt(Distance / (2.0f * AvgRadius)) - 1;

	return FMath::Max(CoverageCount, 0);
}
