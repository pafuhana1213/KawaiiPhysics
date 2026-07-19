// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsTypes.h"
#include "KawaiiPhysicsCollisionLimits.h"

/**
 * 自動テスト用アクセサ
 *
 * FAnimNode_KawaiiPhysics の friend として private/protected の sim 状態・物理計算・コリジョン関数へアクセスし、Output 無しで物理コアをヘッドレス実行する。
 *
 * StepOnce()/StepFrame() は SimulateOnce()/SimulateModifyBones() の Output 非依存部分を単純な縦チェーン用に複製する（数式は本番と同一関数を呼ぶので数式リグレッションを検出でき、複製は呼び出し順序のみ＝本番と二重管理。ダミー/ブリッジ/LOD/外力/world collision/BaseBoneSpace は非対応）。
 */
struct FKawaiiPhysicsTestAccessor
{
	FAnimNode_KawaiiPhysics Node;

	// ========================================================================
	//  セットアップ
	// ========================================================================

	/**
	 * 直線の縦チェーンを生成。index0 = root(kinematic)、Origin から GravityAxisDir の逆へ Spacing 間隔。
	 * デフォルトは -Z 方向（重力で垂れ下がる素直な向き）。
	 */
	void BuildVerticalChain(int32 NumBones, float Spacing, const FVector& Origin = FVector::ZeroVector,
	                        const FVector& Dir = FVector(0.0f, 0.0f, -1.0f))
	{
		Node.ModifyBones.Reset();
		const FVector UnitDir = Dir.GetSafeNormal();
		for (int32 i = 0; i < NumBones; ++i)
		{
			FKawaiiPhysicsModifyBone Bone;
			Bone.Index = i;
			Bone.ParentIndex = i - 1;
			const FVector Loc = Origin + UnitDir * (Spacing * i);
			Bone.PoseLocation = Loc;
			Bone.Location = Loc;
			Bone.PrevLocation = Loc;
			Bone.PrevPoseLocation = Loc;
			Bone.CurrentPoseLocation = Loc;
			Bone.BoneLength = (i > 0) ? Spacing : 0.0f;
			Node.ModifyBones.Add(Bone);
		}
		for (int32 i = 1; i < NumBones; ++i)
		{
			Node.ModifyBones[i - 1].ChildIndices.Add(i);
		}
	}

	/**
	 * 横に並んだ2本の縦チェーンを生成。index 0..N-1 が左、N..2N-1 が右。
	 */
	void BuildTwoVerticalChains(int32 NumBonesPerChain, float Spacing, float LateralSpacing,
	                            const FVector& Origin = FVector::ZeroVector)
	{
		Node.ModifyBones.Reset();
		for (int32 ChainIndex = 0; ChainIndex < 2; ++ChainIndex)
		{
			const int32 BaseIndex = ChainIndex * NumBonesPerChain;
			const FVector ChainOrigin = Origin + FVector(LateralSpacing * ChainIndex, 0.0f, 0.0f);
			for (int32 i = 0; i < NumBonesPerChain; ++i)
			{
				FKawaiiPhysicsModifyBone Bone;
				Bone.Index = BaseIndex + i;
				Bone.ParentIndex = (i > 0) ? (BaseIndex + i - 1) : -1;
				const FVector Loc = ChainOrigin + FVector(0.0f, 0.0f, -Spacing * i);
				Bone.PoseLocation = Loc;
				Bone.Location = Loc;
				Bone.PrevLocation = Loc;
				Bone.PrevPoseLocation = Loc;
				Bone.CurrentPoseLocation = Loc;
				Bone.BoneLength = (i > 0) ? Spacing : 0.0f;
				Node.ModifyBones.Add(Bone);
			}
			for (int32 i = 1; i < NumBonesPerChain; ++i)
			{
				Node.ModifyBones[BaseIndex + i - 1].ChildIndices.Add(BaseIndex + i);
			}
		}
	}

	/**
	 * SyncBone + BoneSubdivision の回帰テスト用フィクスチャ。
	 * index 0 = 実root, 1 = inter-bone dummy, 2 = 実child,
	 * 3 = 末端 inter-bone dummy, 4 = 分割 tip dummy, 5 = legacy の直接 tip dummy。
	 */
	void BuildSyncBoneSubdivisionFixture()
	{
		Node.ModifyBones.Reset();
		Node.DummyBoneLength = 4.0f;

		auto AddBone = [&](int32 Index, int32 ParentIndex, const FVector& Loc, float LengthFromRoot,
		                   float BoneLength, bool bDummy, bool bInterBoneDummy,
		                   int32 RealParentIndex = -1, int32 RealChildIndex = -1, float Alpha = 0.0f,
		                   FName BoneName = NAME_None)
		{
			FKawaiiPhysicsModifyBone Bone;
			Bone.Index = Index;
			Bone.ParentIndex = ParentIndex;
			Bone.BoneRef.BoneName = BoneName;
			if (!bDummy)
			{
				// 実ボーンは有効な CompactPoseIndex を持たせ、LODフォールバック判定が誤発火しないようにする。
				Bone.BoneRef.CachedCompactPoseIndex = FCompactPoseBoneIndex(Index);
			}
			Bone.Location = Loc;
			Bone.PrevLocation = Loc;
			Bone.PoseLocation = Loc;
			Bone.PrevPoseLocation = Loc;
			Bone.CurrentPoseLocation = Loc;
			Bone.PoseRotation = FQuat::Identity;
			Bone.PrevPoseRotation = FQuat::Identity;
			Bone.CurrentPoseRotation = FQuat::Identity;
			Bone.PoseScale = FVector::OneVector;
			Bone.BoneLength = BoneLength;
			Bone.LengthFromRoot = LengthFromRoot;
			Bone.bDummy = bDummy;
			Bone.bInterBoneDummy = bInterBoneDummy;
			Bone.InterBoneRealParentIndex = RealParentIndex;
			Bone.InterBoneRealChildIndex = RealChildIndex;
			Bone.InterBoneAlpha = Alpha;
			Node.ModifyBones.Add(Bone);
		};

		AddBone(0, -1, FVector(0.0f, 0.0f, 0.0f), 0.0f, 0.0f, false, false, -1, -1, 0.0f,
		        FName(TEXT("Root")));
		AddBone(1, 0, FVector(5.0f, 0.0f, 0.0f), 5.0f, 5.0f, true, true, 0, 2, 0.5f);
		AddBone(2, 1, FVector(10.0f, 0.0f, 0.0f), 10.0f, 5.0f, false, false, -1, -1, 0.0f,
		        FName(TEXT("Child")));
		AddBone(3, 2, FVector(12.0f, 0.0f, 0.0f), 12.0f, 2.0f, true, true, 2, 4, 0.5f);
		AddBone(4, 3, FVector(14.0f, 0.0f, 0.0f), 14.0f, 2.0f, true, false, 2);
		AddBone(5, 0, FVector(0.0f, 4.0f, 0.0f), 4.0f, 4.0f, true, false);

		Node.ModifyBones[0].ChildIndices = {1, 5};
		Node.ModifyBones[1].ChildIndices = {2};
		Node.ModifyBones[2].ChildIndices = {3};
		Node.ModifyBones[3].ChildIndices = {4};
	}

	/** 全ボーンに同一の PhysicsSettings を適用 */
	void SetAllPhysicsSettings(const FKawaiiPhysicsSettings& Settings)
	{
		for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
		{
			Bone.PhysicsSettings = Settings;
		}
	}

	void SetGravityInSimSpace(const FVector& Gravity) { Node.GravityInSimSpace = Gravity; }
	void SetSimpleExternalForceInSimSpace(const FVector& Force) { Node.SimpleExternalForceInSimSpace = Force; }
	void SetSimulationSpace(EKawaiiPhysicsSimulationSpace Space) { Node.SimulationSpace = Space; }
	void SetUseLegacyGravity(bool bUse) { Node.bUseLegacyGravity = bUse; }
	void SetSkelCompMove(const FVector& MoveVec, const FQuat& MoveRot = FQuat::Identity)
	{
		Node.SkelCompMoveVector = MoveVec;
		Node.SkelCompMoveRotation = MoveRot;
	}

	/** 固定サブステッピング設定（DeveloperSettings の代わりに直接指定） */
	void SetFixedSubstepping(bool bEnable, int32 TargetFps, int32 MaxSubsteps = 8)
	{
		Node.bUseFixedSubsteppingCached = bEnable;
		Node.TargetFramerate = FMath::Max(1, TargetFps);
		Node.MaxSubstepsCached = FMath::Max(1, MaxSubsteps);
	}

	void SetBoneConstraintIterations(int32 BeforeCollision, int32 AfterCollision)
	{
		Node.BoneConstraintIterationCountBeforeCollision = FMath::Max(0, BeforeCollision);
		Node.BoneConstraintIterationCountAfterCollision = FMath::Max(0, AfterCollision);
	}

	void SetBoneConstraintGlobalComplianceType(EXPBDComplianceType ComplianceType)
	{
		Node.BoneConstraintGlobalComplianceType = ComplianceType;
	}

	void ClearRuntimeBoneConstraints()
	{
		Node.MergedBoneConstraints.Reset();
	}

	void AddRuntimeBoneConstraint(int32 ModifyBoneIndex1, int32 ModifyBoneIndex2, float Length,
	                              bool bOverrideCompliance = false,
	                              EXPBDComplianceType ComplianceType = EXPBDComplianceType::Leather)
	{
		FModifyBoneConstraint Constraint;
		Constraint.ModifyBoneIndex1 = ModifyBoneIndex1;
		Constraint.ModifyBoneIndex2 = ModifyBoneIndex2;
		Constraint.Length = Length;
		Constraint.bOverrideCompliance = bOverrideCompliance;
		Constraint.ComplianceType = ComplianceType;
		Node.MergedBoneConstraints.Add(Constraint);
	}

	// ========================================================================
	//  ステップ実行
	// ========================================================================

	/**
	 * 1フレーム分を進める（SimulateModifyBones の純粋部分を複製）。
	 * SkelComp 移動量のサブステップ分配は本番と同じ割合で各固定ステップへ割り当てる。
	 */
	void StepFrame(float FrameDt)
	{
		if (FrameDt <= 0.0f)
		{
			return;
		}

		// ハーネスの未対応ケースは黙って通さず、警告を出して即座に中断する（Output依存のため未実装）。
		if (!ensureMsgf(Node.SimulationSpace != EKawaiiPhysicsSimulationSpace::BaseBoneSpace,
		                TEXT("FKawaiiPhysicsTestAccessor: BaseBoneSpace is not supported headlessly (needs Output-side "
			                "space conversion). Use ComponentSpace/WorldSpace, or a real-mesh integration test.")))
		{
			return;
		}
		Node.DeltaTime = FrameDt;
		Node.FrameDeltaTime = FrameDt;
		PrepareFrame();
		// 本番 SimulateModifyBones と同様、形状キャッシュはフレーム毎1回（substep 毎ではない）。
		Node.PrepareCollisionShapeCaches();

		if (!Node.bUseFixedSubsteppingCached)
		{
			// ===== Legacy: 実フレーム時間で1ステップ =====
			Node.bInSubstep = false;
			// 初回フレームの DeltaTimeOld=0 による 0/0 を回避。本番 Initialize と同じ初期値に揃える
			// （AnimNode_KawaiiPhysics.cpp:153 の DeltaTimeOld = 1/TargetFramerate）。
			if (Node.DeltaTimeOld <= 0.0f)
			{
				Node.DeltaTimeOld = 1.0f / Node.GetEffectiveTargetFramerate();
			}
			StepOnce();
			Node.DeltaTimeOld = FrameDt;
		}
		else
		{
			// ===== 固定タイムステップ・サブステップ（フレームレート非依存化） =====
			const float FixedDt = 1.0f / Node.GetEffectiveTargetFramerate();
			const float RawElapsed = FMath::Max(Node.SubstepAccumulator + Node.FrameDeltaTime, KINDA_SMALL_NUMBER);
			Node.SubstepAccumulator = FMath::Min(RawElapsed, Node.MaxSubstepsCached * FixedDt);
			const int32 NumSteps = FMath::FloorToInt(Node.SubstepAccumulator / FixedDt);
			Node.SubstepAccumulator -= NumSteps * FixedDt;
			const float MoveFrac = FixedDt / RawElapsed;
			const FVector FullSkelCompMove = Node.SkelCompMoveVector;
			const FQuat FullSkelCompRot = Node.SkelCompMoveRotation;

			Node.bInSubstep = true;
			Node.StepDeltaTime = FixedDt;
			Node.DeltaTimeOld = FixedDt;
			for (int32 SubstepIndex = 0; SubstepIndex < NumSteps; ++SubstepIndex)
			{
				const float SubstepAlpha = static_cast<float>(SubstepIndex + 1) / static_cast<float>(NumSteps);
				for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
				{
					Bone.PoseLocation = FMath::Lerp(Bone.PrevPoseLocation, Bone.CurrentPoseLocation, SubstepAlpha);
					Bone.PoseRotation =
						FQuat::Slerp(Bone.PrevPoseRotation, Bone.CurrentPoseRotation, SubstepAlpha).GetNormalized();
				}
				Node.SkelCompMoveVector = FullSkelCompMove * MoveFrac;
				Node.SkelCompMoveRotation = FQuat::Slerp(FQuat::Identity, FullSkelCompRot, MoveFrac).GetNormalized();
				StepOnce();
			}
			Node.bInSubstep = false;
			Node.SkelCompMoveVector = FullSkelCompMove;
			Node.SkelCompMoveRotation = FullSkelCompRot;

			for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
			{
				Bone.PoseLocation = Bone.CurrentPoseLocation;
				Bone.PoseRotation = Bone.CurrentPoseRotation;
			}
		}

		for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
		{
			Bone.PrevPoseLocation = Bone.CurrentPoseLocation;
			Bone.PrevPoseRotation = Bone.CurrentPoseRotation;
		}
	}

	/** 固定フレーム dt で N フレーム進める */
	void StepFrames(int32 NumFrames, float FrameDt)
	{
		for (int32 i = 0; i < NumFrames; ++i)
		{
			StepFrame(FrameDt);
		}
	}

	// ========================================================================
	//  個別関数の直接呼び出し（コリジョン単体テスト用）
	// ========================================================================

	void CallSphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits)
	{
		Node.AdjustBySphereCollision(Bone, Limits);
	}
	void CallCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits)
	{
		for (FCapsuleLimit& Limit : Limits)
		{
			Limit.UpdateRuntimeCache();
		}
		Node.AdjustByCapsuleCollision(Bone, Limits);
	}
	void CallBoxCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FBoxLimit>& Limits)
	{
		for (FBoxLimit& Limit : Limits)
		{
			Limit.UpdateRuntimeCache();
		}
		Node.AdjustByBoxCollision(Bone, Limits);
	}
	void CallPlanarCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits)
	{
		for (FPlanarLimit& Limit : Limits)
		{
			Limit.UpdateRuntimeCache();
		}
		Node.AdjustByPlanerCollision(Bone, Limits);
	}
	void CallAngleLimit(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone)
	{
		Node.AdjustByAngleLimit(Bone, ParentBone);
	}

	// 物理計算関数の直接呼び出し（抽出した処理を解析的に検証する用）
	FVector CallComputeVerletStepVelocity(FKawaiiPhysicsModifyBone& Bone, const FVector& WindVelocity)
	{
		return Node.ComputeVerletStepVelocity(Bone, WindVelocity);
	}
	void CallIntegrateVerletStepPosition(FKawaiiPhysicsModifyBone& Bone, const FVector& Velocity)
	{
		Node.IntegrateVerletStepPosition(Bone, Velocity);
	}
	void CallSimpleExternalForce(FKawaiiPhysicsModifyBone& Bone)
	{
		Node.ApplySimpleExternalForce(Bone);
	}
	void CallWorldMoveFollow(FKawaiiPhysicsModifyBone& Bone)
	{
		Node.ApplyWorldMoveFollowNonBaseBone(Bone);
	}
	void CallStiffnessPull(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone, float Exponent)
	{
		Node.ApplyStiffnessPull(Bone, ParentBone, Exponent);
	}
	void CallBoneConstraints()
	{
		Node.AdjustByBoneConstraints();
	}
	void CallUpdatePhysicsSettings()
	{
		Node.UpdatePhysicsSettingsOfModifyBones();
	}

	FKawaiiPhysicsSyncTargetRoot CollectSyncChildTargetsForRoot(int32 RootIndex)
	{
		FKawaiiPhysicsSyncTargetRoot TargetRoot;
		TargetRoot.ModifyBoneIndex = RootIndex;
		Node.CollectSyncBoneChildTargets(TargetRoot);
		return TargetRoot;
	}

	// ApplySyncBones の target 適用部（root → child targets）を Output 無しで再現。
	void ApplySyncTargetsForRoot(FKawaiiPhysicsSyncTargetRoot& TargetRoot, const FVector& Translation)
	{
		TargetRoot.Apply(Node.ModifyBones, Translation);
		for (FKawaiiPhysicsSyncTarget& Target : TargetRoot.ChildTargets)
		{
			Target.Apply(Node.ModifyBones, Translation);
		}
	}

	// 非剛体ケース用：root と child で異なる translation（attenuation/curve相当）を適用。
	void ApplySyncTargetsForRootSplit(FKawaiiPhysicsSyncTargetRoot& TargetRoot,
	                                  const FVector& RootTranslation, const FVector& ChildTranslation)
	{
		TargetRoot.Apply(Node.ModifyBones, RootTranslation);
		for (FKawaiiPhysicsSyncTarget& Target : TargetRoot.ChildTargets)
		{
			Target.Apply(Node.ModifyBones, ChildTranslation);
		}
	}

	void CallUpdateSubdivisionDummyPoseAfterSyncBones()
	{
		// GetCompactPoseIndex は bUseSkeletonIndex=false 時 CachedCompactPoseIndex を返す（コンテナ非依存）ため空でよい。
		FBoneContainer EmptyContainer;
		Node.UpdateSubdivisionDummyPoseAfterSyncBones(EmptyContainer);
	}

	// 直接呼び出しテスト用の時間状態（bInSubstep=false なので GetStepDeltaTime()==Dt）。
	void SetTimeState(float Dt, float DtOld)
	{
		Node.DeltaTime = Dt;
		Node.DeltaTimeOld = DtOld;
		Node.bInSubstep = false;
	}

	// サブステップ中の直接呼び出しテスト用の時間状態。
	void SetSubstepTimeState(float FrameDt, float StepDt)
	{
		Node.DeltaTime = FrameDt;
		Node.FrameDeltaTime = FrameDt;
		Node.StepDeltaTime = StepDt;
		Node.DeltaTimeOld = StepDt;
		Node.bInSubstep = true;
	}

	// ========================================================================
	//  アクセサ
	// ========================================================================

	int32 Num() const { return Node.ModifyBones.Num(); }
	FKawaiiPhysicsModifyBone& Bone(int32 Index) { return Node.ModifyBones[Index]; }
	const FKawaiiPhysicsModifyBone& Bone(int32 Index) const { return Node.ModifyBones[Index]; }
	FVector TipLocation() const { return Node.ModifyBones.Last().Location; }

	/** 全ボーン位置が有限（NaN/Inf 無し）か */
	bool AllFinite() const
	{
		for (const FKawaiiPhysicsModifyBone& B : Node.ModifyBones)
		{
			if (B.Location.ContainsNaN())
			{
				return false;
			}
		}
		return true;
	}

	/** 全ボーン位置が絶対値 Bound 内に収まっているか（発散検出） */
	bool AllWithin(float Bound) const
	{
		for (const FKawaiiPhysicsModifyBone& B : Node.ModifyBones)
		{
			if (FMath::Abs(B.Location.X) > Bound || FMath::Abs(B.Location.Y) > Bound ||
				FMath::Abs(B.Location.Z) > Bound)
			{
				return false;
			}
		}
		return true;
	}

private:
	/**
	 * フレーム冒頭の準備（SimulateModifyBones の skip フラグ設定 + ポーズ・スナップショットを複製）。
	 * 単純チェーン用: root(ParentIndex<0) を kinematic として skip、それ以外を simulate。
	 */
	void PrepareFrame()
	{
		for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
		{
			Bone.bSkipSimulate = (Bone.ParentIndex < 0);

			Bone.CurrentPoseLocation = Bone.PoseLocation;
			Bone.CurrentPoseRotation = Bone.PoseRotation;
			if (!Node.bSubstepPoseInitialized)
			{
				Bone.PrevPoseLocation = Bone.PoseLocation;
				Bone.PrevPoseRotation = Bone.PoseRotation;
			}
		}
		Node.bSubstepPoseInitialized = true;
	}

	/**
	 * 1ステップ分（SimulateOnce の純粋部分を複製）。
	 * 順序: root follow → 物理計算 → BoneConstraint(before) → コリジョン → BoneConstraint(after) → 角度制限+平面拘束+長さ復元。
	 */
	void StepOnce()
	{
		// root bone の kinematic follow（SimulateOnce 274-281）
		for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
		{
			if (Bone.ParentIndex < 0)
			{
				Bone.PrevLocation = Bone.Location;
				Bone.Location = Bone.PoseLocation;
			}
		}

		const float Exponent = Node.GetEffectiveTargetFramerate() * Node.GetStepDeltaTime();

		// 積分（Simulate() の純粋部分。wind/外力なし）
		for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
		{
			if (Bone.bSkipSimulate)
			{
				continue;
			}
			const FVector Velocity = Node.ComputeVerletStepVelocity(Bone, FVector::ZeroVector);
			Node.IntegrateVerletStepPosition(Bone, Velocity);
			Node.ApplySimpleExternalForce(Bone);
			Node.ApplyWorldMoveFollowNonBaseBone(Bone);
			Node.ApplyStiffnessPull(Bone, Node.ModifyBones[Bone.ParentIndex], Exponent);
		}

		// BoneConstraint before collision（SimulateOnce 397-403）
		if (Node.BoneConstraintIterationCountBeforeCollision > 0)
		{
			for (FModifyBoneConstraint& BoneConstraint : Node.MergedBoneConstraints)
			{
				BoneConstraint.Lambda = 0.0f;
			}
			for (int32 i = 0; i < Node.BoneConstraintIterationCountBeforeCollision; ++i)
			{
				Node.AdjustByBoneConstraints();
			}
		}

		// コリジョン（SimulateOnce 413-445、AnimNode 側 limits のみ。形状キャッシュは StepFrame 冒頭で更新済み）
		for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
		{
			if (Bone.bSkipSimulate)
			{
				continue;
			}
			Node.AdjustBySphereCollision(Bone, Node.SphericalLimits);
			Node.AdjustByCapsuleCollision(Bone, Node.CapsuleLimits);
			Node.AdjustByBoxCollision(Bone, Node.BoxLimits);
			Node.AdjustByPlanerCollision(Bone, Node.PlanarLimits);
		}

		// BoneConstraint after collision（SimulateOnce 516-522）
		if (Node.BoneConstraintIterationCountAfterCollision > 0)
		{
			for (FModifyBoneConstraint& BoneConstraint : Node.MergedBoneConstraints)
			{
				BoneConstraint.Lambda = 0.0f;
			}
			for (int32 i = 0; i < Node.BoneConstraintIterationCountAfterCollision; ++i)
			{
				Node.AdjustByBoneConstraints();
			}
		}

		// 角度制限 + 平面拘束 + ボーン長復元（SimulateOnce 528-555）
		for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
		{
			if (Bone.bSkipSimulate)
			{
				continue;
			}
			FKawaiiPhysicsModifyBone& ParentBone = Node.ModifyBones[Bone.ParentIndex];
			Node.AdjustByAngleLimit(Bone, ParentBone);
			Node.AdjustByPlanarConstraint(Bone, ParentBone);
			const float BoneLength = (Bone.PoseLocation - ParentBone.PoseLocation).Size();
			Bone.Location = (Bone.Location - ParentBone.Location).GetSafeNormal() * BoneLength + ParentBone.Location;
		}
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS
