// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsTypes.h"
#include "KawaiiPhysicsCollisionLimits.h"

/**
 * 自動テスト用アクセサ / Automation test accessor.
 *
 * FAnimNode_KawaiiPhysics の friend として private/protected の sim 状態・物理計算関数・
 * コリジョン関数へアクセスし、FComponentSpacePoseContext(Output) 無しで物理計算を
 * ヘッドレス実行する。
 * Friend of FAnimNode_KawaiiPhysics: reaches the private/protected sim state, integration
 * core functions, and collision functions to drive the physics core headlessly (no Output).
 *
 * StepOnce()/StepFrame() は SimulateOnce()/SimulateModifyBones() の「Output を使わない純粋部分」
 * の処理順序を、単純な縦チェーン（ダミー/ブリッジ/LOD/外力/world collision/BaseBoneSpace を
 * 含まない）に対して複製する。per-step の数式そのものは本番と同一の関数を呼ぶため、
 * 数式へのリグレッションはここで検出できる（順序変更のみ本番と二重管理）。
 * StepOnce()/StepFrame() replicate the Output-free pure subset of SimulateOnce()/SimulateModifyBones()
 * for a simple vertical chain (no dummies/bridges/LOD/external-forces/world-collision/BaseBoneSpace).
 * The per-step math calls the exact same production functions, so math regressions are caught here;
 * only the call ORDER is replicated (kept in sync via cross-referenced comments).
 */
struct FKawaiiPhysicsTestAccessor
{
	FAnimNode_KawaiiPhysics Node;

	// ========================================================================
	//  セットアップ / Setup
	// ========================================================================

	/**
	 * 直線の縦チェーンを生成。index0 = root(kinematic)、Origin から GravityAxisDir の逆へ Spacing 間隔。
	 * Build a straight vertical chain: index 0 = root (kinematic), spaced `Spacing` apart from `Origin`.
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

	/** 全ボーンに同一の PhysicsSettings を適用 / Apply the same PhysicsSettings to every bone. */
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

	/** 固定サブステッピング設定（DeveloperSettings の代わりに直接指定） / Configure fixed substepping directly. */
	void SetFixedSubstepping(bool bEnable, int32 TargetFps, int32 MaxSubsteps = 8)
	{
		Node.bUseFixedSubsteppingCached = bEnable;
		Node.TargetFramerate = FMath::Max(1, TargetFps);
		Node.MaxSubstepsCached = FMath::Max(1, MaxSubsteps);
	}

	// ========================================================================
	//  ステップ実行 / Stepping
	// ========================================================================

	/**
	 * 1フレーム分を進める（SimulateModifyBones の純粋部分を複製）。
	 * Advance one frame (replicates the pure subset of SimulateModifyBones).
	 * SkelComp 移動量のサブステップ分配は SkelCompMoveVector==0 前提のため省略。
	 * SkelComp-move substep distribution is omitted (assumes SkelCompMoveVector == 0).
	 */
	void StepFrame(float FrameDt)
	{
		if (FrameDt <= 0.0f)
		{
			return;
		}

		// ハーネスの未対応ケースを黙って通さない（Output依存のため未実装）。
		// Fail loudly on cases the headless harness does not model (they require Output).
		ensureMsgf(Node.SimulationSpace != EKawaiiPhysicsSimulationSpace::BaseBoneSpace,
		           TEXT("FKawaiiPhysicsTestAccessor: BaseBoneSpace is not supported headlessly (needs Output-side "
			           "space conversion). Use ComponentSpace/WorldSpace, or a real-mesh integration test."));
		ensureMsgf(Node.SkelCompMoveVector.IsNearlyZero() || !Node.bUseFixedSubsteppingCached,
		           TEXT("FKawaiiPhysicsTestAccessor: nonzero SkelCompMoveVector is not distributed across substeps "
			           "(production does at SimulateModifyBones). Use legacy mode or zero SkelCompMove."));

		Node.DeltaTime = FrameDt;
		Node.FrameDeltaTime = FrameDt;
		PrepareFrame();

		if (!Node.bUseFixedSubsteppingCached)
		{
			// ===== Legacy: 実フレーム時間で1ステップ =====
			Node.bInSubstep = false;
			// 初回フレームの DeltaTimeOld=0 による 0/0 を回避。本番 Initialize と同じ初期値に揃える
			// （AnimNode_KawaiiPhysics.cpp:153 の DeltaTimeOld = 1/TargetFramerate）。
			// Avoid 0/0 on the first frame; seed exactly like production's Initialize (1/TargetFramerate).
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
			Node.SubstepAccumulator += Node.FrameDeltaTime;
			Node.SubstepAccumulator = FMath::Min(Node.SubstepAccumulator, Node.MaxSubstepsCached * FixedDt);
			const int32 NumSteps = FMath::FloorToInt(Node.SubstepAccumulator / FixedDt);
			Node.SubstepAccumulator -= NumSteps * FixedDt;

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
				StepOnce();
			}
			Node.bInSubstep = false;

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

	/** N フレーム進める / Advance N frames at a fixed frame dt. */
	void StepFrames(int32 NumFrames, float FrameDt)
	{
		for (int32 i = 0; i < NumFrames; ++i)
		{
			StepFrame(FrameDt);
		}
	}

	// ========================================================================
	//  個別関数の直接呼び出し（コリジョン単体テスト用） / Direct calls for collision unit tests
	// ========================================================================

	void CallSphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits)
	{
		Node.AdjustBySphereCollision(Bone, Limits);
	}
	void CallCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits)
	{
		Node.AdjustByCapsuleCollision(Bone, Limits);
	}
	void CallBoxCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FBoxLimit>& Limits)
	{
		Node.AdjustByBoxCollision(Bone, Limits);
	}
	void CallPlanarCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits)
	{
		Node.AdjustByPlanerCollision(Bone, Limits);
	}
	void CallAngleLimit(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone)
	{
		Node.AdjustByAngleLimit(Bone, ParentBone);
	}

	// 物理計算関数の直接呼び出し（抽出した処理を解析的に検証する用） / Direct calls to the physics functions (to verify the extracted code path analytically).
	void CallVerletStep(FKawaiiPhysicsModifyBone& Bone, const FVector& ExtraVelocity)
	{
		Node.IntegrateVerletStep(Bone, ExtraVelocity);
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

	// 直接呼び出しテスト用の時間状態（bInSubstep=false なので GetStepDeltaTime()==Dt）。
	// Time state for direct core tests (bInSubstep=false => GetStepDeltaTime()==Dt).
	void SetTimeState(float Dt, float DtOld)
	{
		Node.DeltaTime = Dt;
		Node.DeltaTimeOld = DtOld;
		Node.bInSubstep = false;
	}

	// ========================================================================
	//  アクセサ / Accessors
	// ========================================================================

	int32 Num() const { return Node.ModifyBones.Num(); }
	FKawaiiPhysicsModifyBone& Bone(int32 Index) { return Node.ModifyBones[Index]; }
	const FKawaiiPhysicsModifyBone& Bone(int32 Index) const { return Node.ModifyBones[Index]; }
	FVector TipLocation() const { return Node.ModifyBones.Last().Location; }

	/** 全ボーン位置が有限（NaN/Inf 無し）か / True if every bone location is finite (no NaN/Inf). */
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

	/** 全ボーン位置が絶対値 Bound 内に収まっているか（発散検出） / True if all locations stay within |Bound|. */
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
	 * Per-frame prep replicating SimulateModifyBones (skip flags + pose snapshot) for a simple chain:
	 * root (ParentIndex<0) is kinematic => skip; the rest simulate.
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
	 * One simulation step (replicates the pure subset of SimulateOnce).
	 * 順序: root follow → 物理計算（Verlet積分ほか）→ コリジョン(AnimNode limits) → 角度制限+平面拘束+長さ復元。
	 * Order: root follow -> physics steps (Verlet integration etc.) -> collision (AnimNode limits) -> angle/planar/length restore.
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
			Node.IntegrateVerletStep(Bone, FVector::ZeroVector);
			Node.ApplySimpleExternalForce(Bone);
			Node.ApplyWorldMoveFollowNonBaseBone(Bone);
			Node.ApplyStiffnessPull(Bone, Node.ModifyBones[Bone.ParentIndex], Exponent);
		}

		// コリジョン（SimulateOnce 413-445、AnimNode 側 limits のみ）
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
