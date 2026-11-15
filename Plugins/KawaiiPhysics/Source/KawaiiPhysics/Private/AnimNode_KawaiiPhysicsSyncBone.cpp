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

void FAnimNode_KawaiiPhysics::InitSyncBones(FComponentSpacePoseContext& Output)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitSyncBone);

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	for (FKawaiiPhysicsSyncBone& SyncBone : SyncBones)
	{
		InitSyncBone(Output, BoneContainer, SyncBone);
	}
}

void FAnimNode_KawaiiPhysics::InitSyncBone(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                           FKawaiiPhysicsSyncBone& SyncBone)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitSyncBone);

	SyncBone.Bone.Initialize(BoneContainer);
	if (!SyncBone.Bone.IsValidToEvaluate(BoneContainer))
	{
		SyncBone.InitialPoseLocation = FVector::ZeroVector;
		return;
	}
	SyncBone.InitialPoseLocation =
			FAnimationRuntime::GetComponentSpaceTransformRefPose(BoneContainer.GetReferenceSkeleton(),
			                                                     SyncBone.Bone.BoneIndex).GetLocation();

	// 無効なターゲットを除去する前に、ターゲットのボーン参照を初期化する。グラフノードからコピーされた直後は
	// ボーン番号が未初期化で、PIE 中はボーン参照の一括初期化も走らないため、存在するボーンまで無効と判定されてしまう
	for (FKawaiiPhysicsSyncTargetRoot& TargetRoot : SyncBone.TargetRoots)
	{
		TargetRoot.Bone.Initialize(BoneContainer);
	}
	SyncBone.TargetRoots.RemoveAll([&](const FKawaiiPhysicsSyncTarget& Target)
	{
		return !Target.IsValid(BoneContainer);
	});

	for (auto& TargetRoot : SyncBone.TargetRoots)
	{
		TargetRoot.ChildTargets.Empty();

		TargetRoot.ModifyBoneIndex = ModifyBones.IndexOfByPredicate(
			[&](const FKawaiiPhysicsModifyBone& ModifyBone) { return ModifyBone.BoneRef == TargetRoot.Bone; });
		if (TargetRoot.ModifyBoneIndex == INDEX_NONE || !TargetRoot.bIncludeChildBones)
		{
			continue;
		}

		CollectSyncBoneChildTargets(TargetRoot);
	}
}

bool FAnimNode_KawaiiPhysics::IsExcludedFromSyncBoneChildTarget(const FKawaiiPhysicsModifyBone& Bone) const
{
	return Bone.bInterBoneDummy || (Bone.bDummy && Bone.InterBoneRealParentIndex >= 0);
}

void FAnimNode_KawaiiPhysics::CollectSyncBoneChildTargets(FKawaiiPhysicsSyncTargetRoot& TargetRoot)
{
	TargetRoot.ChildTargets.Empty();

	if (!ModifyBones.IsValidIndex(TargetRoot.ModifyBoneIndex))
	{
		return;
	}

	// LengthRateFromSyncTargetRoot 計算用
	const float StartLength = ModifyBones[TargetRoot.ModifyBoneIndex].LengthFromRoot;
	float MaxLength = StartLength;

	// 子ボーンを収集する。BoneSubdivision のダミーは内部計算用の点なので、走査はするが
	// SyncBone のターゲットやエディタプレビュー項目としては公開しない。
	TArray<int32> IndicesToProcess = ModifyBones[TargetRoot.ModifyBoneIndex].ChildIndices;
	while (!IndicesToProcess.IsEmpty())
	{
		const int32 CurrentIndex = IndicesToProcess.Pop();
		if (!ModifyBones.IsValidIndex(CurrentIndex))
		{
			continue;
		}

		const FKawaiiPhysicsModifyBone& ModifyBone = ModifyBones[CurrentIndex];
		if (!IsExcludedFromSyncBoneChildTarget(ModifyBone))
		{
			const int32 TargetIndex = TargetRoot.ChildTargets.AddUnique({CurrentIndex});

#if WITH_EDITORONLY_DATA
			TargetRoot.ChildTargets[TargetIndex].PreviewBone = ModifyBone.BoneRef;
#endif

			MaxLength = FMath::Max(MaxLength, ModifyBone.LengthFromRoot);
		}

		IndicesToProcess.Append(ModifyBone.ChildIndices);
	}

	// LengthRateFromSyncTargetRoot を計算
	const float LengthRange = MaxLength - StartLength;
	TargetRoot.LengthRateFromSyncTargetRoot = 0.0f;
	for (auto& Target : TargetRoot.ChildTargets)
	{
		if (LengthRange > KINDA_SMALL_NUMBER)
		{
			Target.LengthRateFromSyncTargetRoot =
				(ModifyBones[Target.ModifyBoneIndex].LengthFromRoot - StartLength) / LengthRange;
		}
		else
		{
			Target.LengthRateFromSyncTargetRoot = 0.0f;
		}
	}

	// 長さ比率とカーブで Alpha を更新
	if (const FRichCurve* ScaleCurve = TargetRoot.ScaleCurveByBoneLengthRate.GetRichCurveConst();
		ScaleCurve && !ScaleCurve->IsEmpty())
	{
		TargetRoot.UpdateScaleByLengthRate(ScaleCurve);
		for (auto& Target : TargetRoot.ChildTargets)
		{
			Target.UpdateScaleByLengthRate(ScaleCurve);
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateSubdivisionDummyPoseAfterSyncBones(const FBoneContainer& BoneContainer)
{
	// Pass 1: 分割された先端ダミーは実在の祖先ボーンのみに依存する。
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (!Bone.bDummy || Bone.bInterBoneDummy || Bone.InterBoneRealParentIndex < 0)
		{
			continue;
		}

		UpdateTipDummyPose(Bone);
	}

	// Pass 2: ボーン間ダミーは SyncBone 適用後の端点同士を補間する点である。
	// 共有ヘルパでLODフォールバックを UpdateModifyBonesPoseTransform と一致させる。
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (!Bone.bInterBoneDummy)
		{
			continue;
		}

		UpdateInterBoneDummyPose(Bone, BoneContainer);
	}
}

void FAnimNode_KawaiiPhysics::ApplySyncBones(FComponentSpacePoseContext& Output,
                                             const FBoneContainer& BoneContainer)
{
	if (SyncBones.Num() == 0)
	{
		return;
	}

	for (auto& SyncBone : SyncBones)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ApplySyncBone);

		if (!SyncBone.Bone.IsValidToEvaluate(BoneContainer))
		{
			continue;
		}

		// Component Space で移動差分を計算
		const FCompactPoseBoneIndex SyncBoneIndex = SyncBone.Bone.GetCompactPoseIndex(BoneContainer);
		FVector DeltaMovement = Output.Pose.GetComponentSpaceTransform(SyncBoneIndex).GetLocation() - SyncBone.
			InitialPoseLocation;

#if WITH_EDITORONLY_DATA
		SyncBone.DeltaDistance = DeltaMovement;
#endif

		// カーブを適用
		if (const FRichCurve* ScaleCurve = SyncBone.ScaleCurveByDeltaDistance.GetRichCurveConst();
			ScaleCurve && !ScaleCurve->IsEmpty())
		{
			DeltaMovement *= ScaleCurve->Eval(DeltaMovement.Length());
		}

		// 全体スケールを適用
		DeltaMovement *= SyncBone.GlobalScale;

#if WITH_EDITORONLY_DATA
		SyncBone.ScaledDeltaDistance = DeltaMovement;
#endif

		// Component Space で SyncBone ごとに一度だけ方向をフィルタリング
		auto CheckDirection = [](const float Val, const ESyncBoneDirection Dir)
		{
			return (Dir == ESyncBoneDirection::Both) ||
				(Dir == ESyncBoneDirection::Positive && Val > 0.0) ||
				(Dir == ESyncBoneDirection::Negative && Val < 0.0);
		};

		FVector FilteredDeltaMovement(
			CheckDirection(DeltaMovement.X, SyncBone.ApplyDirectionX) ? DeltaMovement.X : 0.0,
			CheckDirection(DeltaMovement.Y, SyncBone.ApplyDirectionY) ? DeltaMovement.Y : 0.0,
			CheckDirection(DeltaMovement.Z, SyncBone.ApplyDirectionZ) ? DeltaMovement.Z : 0.0
		);

		if (FilteredDeltaMovement.IsNearlyZero())
		{
			continue;
		}

		// Simulation Space へ変換
		FilteredDeltaMovement = ConvertSimulationSpaceVector(Output,
		                                                     EKawaiiPhysicsSimulationSpace::ComponentSpace,
		                                                     SimulationSpace, FilteredDeltaMovement);

		// 距離減衰用に SyncBone の位置を Simulation Space でキャッシュ
		const FVector SyncBoneLocationInSimulationSpace = ConvertSimulationSpaceLocation(
			Output,
			EKawaiiPhysicsSimulationSpace::ComponentSpace,
			SimulationSpace,
			Output.Pose.GetComponentSpaceTransform(SyncBoneIndex).GetLocation()
		);

		// ヘルパ: 距離に対する減衰 alpha を計算
		auto CalcAttenuationAlpha = [&](const float Distance) -> float
		{
			if (!SyncBone.bEnableDistanceAttenuation)
			{
				return 1.0f;
			}

			const float Inner = SyncBone.AttenuationInnerRadius;
			const float Outer = SyncBone.AttenuationOuterRadius;
			const float MaxAtten = SyncBone.MaxAttenuationRate;

			// 安全策: outer <= inner の場合は inner でのステップ関数として扱う
			const float EffectiveOuter = FMath::Max(Outer, Inner);

			float AttenAmount;
			if (Distance <= Inner)
			{
				AttenAmount = 0.0f;
			}
			else if (Distance >= EffectiveOuter)
			{
				AttenAmount = MaxAtten;
			}
			else
			{
				const float Denom = EffectiveOuter - Inner;
				const float T = (Denom > KINDA_SMALL_NUMBER) ? ((Distance - Inner) / Denom) : 1.0f;
				AttenAmount = T * MaxAtten;
			}

			// 減衰量を alpha 乗算値へ変換
			return FMath::Max(0.0f, 1.0f - AttenAmount);
		};

		// ターゲットへ適用
		for (auto& TargetRoot : SyncBone.TargetRoots)
		{
			// LengthRate curve の Scale は CollectSyncBoneChildTargets で計算済み。
			// editor では曲線のライブ編集に追従するため毎フレーム再計算する。
#if WITH_EDITOR
			if (const FRichCurve* ScaleCurve = TargetRoot.ScaleCurveByBoneLengthRate.GetRichCurveConst();
				ScaleCurve && !ScaleCurve->IsEmpty())
			{
				TargetRoot.UpdateScaleByLengthRate(ScaleCurve);
				for (auto& Target : TargetRoot.ChildTargets)
				{
					Target.UpdateScaleByLengthRate(ScaleCurve);
				}
			}
#endif

			// ルートターゲット
			{
				const int32 ModifyBoneIndex = TargetRoot.ModifyBoneIndex;
				if (ModifyBones.IsValidIndex(ModifyBoneIndex))
				{
					const float Dist = FVector::Dist(SyncBoneLocationInSimulationSpace,
					                                 ModifyBones[ModifyBoneIndex].Location);
					const float AlphaMul = CalcAttenuationAlpha(Dist);
					TargetRoot.Apply(ModifyBones, FilteredDeltaMovement * AlphaMul);
				}
				else
				{
					TargetRoot.Apply(ModifyBones, FilteredDeltaMovement);
				}
			}

			// 子ターゲット
			for (auto& Target : TargetRoot.ChildTargets)
			{
				const int32 ModifyBoneIndex = Target.ModifyBoneIndex;
				if (ModifyBones.IsValidIndex(ModifyBoneIndex))
				{
					const float Dist = FVector::Dist(SyncBoneLocationInSimulationSpace,
					                                 ModifyBones[ModifyBoneIndex].Location);
					const float AlphaMul = CalcAttenuationAlpha(Dist);
					Target.Apply(ModifyBones, FilteredDeltaMovement * AlphaMul);
				}
				else
				{
					Target.Apply(ModifyBones, FilteredDeltaMovement);
				}
			}
		}
	}

	UpdateSubdivisionDummyPoseAfterSyncBones(BoneContainer);
}
