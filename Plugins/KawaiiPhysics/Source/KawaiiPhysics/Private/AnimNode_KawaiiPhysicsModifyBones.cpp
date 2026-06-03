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

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
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
	const FReferenceSkeleton& RefSkeleton = (CVarAnimNodeKawaiiPhysicsUseBoneContainerRefSkeletonWhenInit.
		                                        GetValueOnAnyThread())
		                                        ? BoneContainer.GetReferenceSkeleton()
		                                        : BoneContainer.GetSkeletonAsset()->GetReferenceSkeleton();

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
	if (BoneIndex < 0 || RefSkeleton.GetNum() < BoneIndex)
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
		//for some mesh where tip bone is empty (without any skinning weight in the mesh), ChildBoneIndices > 0 but no actual child bones are created
		for (auto ChildBoneIndex : ChildBoneIndices)
		{
			TArray<int32> InsertedInterBoneDummyIndices;
			const int32 EffectiveParentIndex = InsertInterBoneDummyBones(InModifyBones, Output, BoneContainer,
			                                                            RefSkeleton, ModifyBoneIndex, ChildBoneIndex,
			                                                            InsertedInterBoneDummyIndices);

			// 子ボーンの再帰追加 / Recursive child addition
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
		// Terminal tip dummy location (forward from the real bone by DummyBoneLength)
		const FVector TipLocation = NewModifyBone.Location + GetBoneForwardVector(NewModifyBone.PrevRotation) *
			DummyBoneLength;

		// 実ボーンと末端ダミーの間にもインターボーンダミーを挿入（実ボーン区間と同様に分割）
		// Subdivide the terminal segment between the real bone and the tip dummy, like real-bone segments
		TArray<int32> InsertedInterBoneDummyIndices;
		const int32 EffectiveParentIndex = InsertInterBoneDummyBonesCore(
			InModifyBones, ModifyBoneIndex, TipLocation, NewModifyBone.PrevRotation,
			RefBonePoseTransform.GetScale3D(), DummyBoneLength, InsertedInterBoneDummyIndices);
		const int32 InsertedCount = InsertedInterBoneDummyIndices.Num();

		// Add dummy modify bone
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
			// Subdivided: tip dummy is child of the last inter-bone dummy; BoneLength is the final segment
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
			// Point inserted inter-bone dummies' real child at the tip dummy
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
	// Delegate to the shared core with the real child transform
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
	OutInsertedInterBoneDummyIndices.Reset();

	int32 EffectiveParentIndex = ParentModifyBoneIndex;
	if (BoneSubdivisionCount <= 0)
	{
		return EffectiveParentIndex;
	}

	// ベースRadiusで自動補正カウントを算出
	// RadiusCurveはCalcBoneLength後にUpdatePhysicsSettingsOfModifyBonesで適用
	const float AvgRadius = PhysicsSettings.Radius;
	const int32 EffectiveCount = CalcInterBoneDummyCount(Distance, BoneSubdivisionCount, AvgRadius);

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
			Bone.BoneLength = RefBonePose[Bone.BoneRef.BoneIndex].GetLocation().Size();
		}
		else if (!Bone.bInterBoneDummy)
		{
			// tip dummy: 親がインターボーンダミー(=末端区間が分割済み)の場合、BoneLengthは
			// AddModifyBoneで最終セグメント長に設定済みなので上書きしない（LengthFromRootの二重計上防止）
			// Tip dummy: when the parent is an inter-bone dummy (terminal segment subdivided), BoneLength is
			// already the final-segment length set in AddModifyBone — don't overwrite (avoids double-counting)
			if (!InModifyBones[Bone.ParentIndex].bInterBoneDummy)
			{
				Bone.BoneLength = DummyBoneLength; // 非分割 tip dummy / non-subdivided tip dummy
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


void FAnimNode_KawaiiPhysics::UpdateModifyBonesPoseTransform(FComponentSpacePoseContext& Output,
                                                             const FBoneContainer& BoneContainer)
{
	// 1パス目: 実ボーンとtip dummyのPoseLocationを更新
	// Pass 1: Update real bones and tip dummies (inter-bone dummies need both endpoints ready)
	for (auto& Bone : ModifyBones)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateModifyBonesPoseTransform);

		if (Bone.bInterBoneDummy)
		{
			continue; // 2パス目で処理 / Deferred to pass 2
		}

		if (Bone.bDummy)
		{
			// tip dummy: 分割時は即時親がインターボーンダミー(Pass2でしか確定しない)になるため、
			// 実親(InterBoneRealParentIndex)を基準に計算して循環依存を回避
			// Tip dummy: when subdivided, the immediate parent is an inter-bone dummy (only set in Pass 2),
			// so compute from the real ancestor (InterBoneRealParentIndex) to avoid a stale/circular pose
			const int32 RealAncestorIndex = (Bone.InterBoneRealParentIndex >= 0)
				                                ? Bone.InterBoneRealParentIndex
				                                : Bone.ParentIndex;
			const auto& RealAncestor = ModifyBones[RealAncestorIndex];
			Bone.PoseLocation = RealAncestor.PoseLocation +
				GetBoneForwardVector(RealAncestor.PoseRotation) * DummyBoneLength;
			Bone.PoseRotation = RealAncestor.PoseRotation;
			Bone.PoseScale = RealAncestor.PoseScale;
		}
		else
		{
			const auto CompactPoseIndex = Bone.BoneRef.GetCompactPoseIndex(BoneContainer);
			if (CompactPoseIndex < 0)
			{
				// Reset bone location and rotation may cause trouble when switching between skeleton LODs #44
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
	// Pass 2: Update inter-bone dummies now that both real parent and child PoseLocations are current
	for (auto& Bone : ModifyBones)
	{
		if (!Bone.bInterBoneDummy)
		{
			continue;
		}

		const auto& RealParent = ModifyBones[Bone.InterBoneRealParentIndex];
		const auto& RealChild = ModifyBones[Bone.InterBoneRealChildIndex];

		// 末端ダミーを実子とする場合、tip dummyはBoneRef空でCompactPose<0になるが
		// PoseLocationはPass1で確定済みなのでLODフォールバック判定から除外する
		// When the real child is a tip dummy (empty BoneRef → CompactPose<0), its PoseLocation is
		// already computed in Pass 1, so exclude it from the LOD fallback check.
		const bool bRealChildIsTipDummy = RealChild.bDummy && !RealChild.bInterBoneDummy;

		// LOD安全チェック: RealChildがLODで無効な場合、親のPoseにフォールバック
		const auto RealChildCompactPose = RealChild.BoneRef.GetCompactPoseIndex(BoneContainer);
		if (!bRealChildIsTipDummy && RealChildCompactPose < 0)
		{
			const auto& ParentBone = ModifyBones[Bone.ParentIndex];
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

int32 FAnimNode_KawaiiPhysics::CalcInterBoneDummyCount(float Distance, int32 RequestedCount, float AvgRadius) const
{
	if (RequestedCount <= 0 || Distance <= KINDA_SMALL_NUMBER)
	{
		return 0;
	}

	if (AvgRadius <= KINDA_SMALL_NUMBER)
	{
		return RequestedCount;
	}

	// N個のDummyBone → (N+1)セグメント。各セグメント >= 2*AvgRadius で重ならない
	// Distance / (N+1) >= 2*AvgRadius → N <= Distance/(2*AvgRadius) - 1
	const int32 MaxCount = FMath::Max(FMath::FloorToInt(Distance / (2.0f * AvgRadius)) - 1, 0);

	return FMath::Min(RequestedCount, MaxCount);
}


