// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysics.h"

#include "AnimationRuntime.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "KawaiiPhysicsDeveloperSettings.h"
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

void FAnimNode_KawaiiPhysics::UpdatePhysicsSettingsOfModifyBones()
{
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdatePhysicsSetting);

		const float LengthRate = Bone.LengthRateFromRoot;

		// Damping
		Bone.PhysicsSettings.Damping = FMath::Clamp(
			PhysicsSettings.Damping * DampingCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// WorldLocationDamping
		Bone.PhysicsSettings.WorldDampingLocation = FMath::Clamp(
			PhysicsSettings.WorldDampingLocation * WorldDampingLocationCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// WorldRotationDamping
		Bone.PhysicsSettings.WorldDampingRotation = FMath::Clamp(
			PhysicsSettings.WorldDampingRotation * WorldDampingRotationCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// Stiffness
		Bone.PhysicsSettings.Stiffness = FMath::Clamp(
			PhysicsSettings.Stiffness * StiffnessCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// Radius
		Bone.PhysicsSettings.Radius = FMath::Max(
			PhysicsSettings.Radius * RadiusCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f);
		
		// LimitAngle
		Bone.PhysicsSettings.LimitAngle = FMath::Max(
			PhysicsSettings.LimitAngle * LimitAngleCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f);
	}
}


void FAnimNode_KawaiiPhysics::SimulateModifyBones(FComponentSpacePoseContext& Output,
                                                  const FTransform& ComponentTransform)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimulateModifyBones);

	if (DeltaTime <= 0.0f)
	{
		return;
	}

	// このフレームの実dtを保持（サブステップ中は DeltaTime を FixedDt として扱うため別保存）
	// Keep this frame's real dt (DeltaTime is treated as FixedDt during substeps)
	FrameDeltaTime = DeltaTime;

	const USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy->GetSkelMeshComponent();

	// Save Prev/Pose Info , Check SkipSimulate
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		// bridge dummyは縦親(ParentIndex<0)を持たないが、コリジョン代理として非skipにする
		// （Simulate()/長さ復元は別途スキップ。下のParentIndex<0分岐に落ちると誤ってskip&Pose固定される）
		// Bridge dummies have no vertical parent but must stay non-skip so collision runs on them
		// (Simulate()/length-restore are skipped separately; the ParentIndex<0 branch below must not catch them)
		if (Bone.bBridgeDummy)
		{
			Bone.bSkipSimulate = false;
			continue;
		}

		if (Bone.BoneRef.BoneIndex < 0 && !Bone.bDummy)
		{
			Bone.bSkipSimulate = true;
			continue;
		}

		if (Bone.ParentIndex < 0)
		{
			// root: kinematic。実ポーズ追従（PrevLocation/Location更新）は SimulateOnce 内で、
			// サブステップ補間後のポーズに対して毎ステップ行う。
			// root: kinematic; the actual pose-follow (PrevLocation/Location update) happens inside
			// SimulateOnce each step, against the per-substep interpolated pose.
			Bone.bSkipSimulate = true;
			continue;
		}

		Bone.bSkipSimulate = false;
	}

	// Gravity
	GravityInSimSpace = ConvertSimulationSpaceVector(Output,
	                                                 bUseWorldSpaceGravity
		                                                 ? EKawaiiPhysicsSimulationSpace::WorldSpace
		                                                 : EKawaiiPhysicsSimulationSpace::ComponentSpace,
	                                                 SimulationSpace, Gravity);
	if (bUseDefaultGravityZProjectSetting)
	{
		GravityInSimSpace *= FMath::Abs(UPhysicsSettings::Get()->DefaultGravityZ);
	}

	// SimpleExternalForce: compute once in SimulationSpace (avoid per-bone conversions)
	if (!SimpleExternalForce.IsNearlyZero())
	{
		if (bUseWorldSpaceSimpleExternalForce)
		{
			SimpleExternalForceInSimSpace = ConvertSimulationSpaceVector(
				Output,
				EKawaiiPhysicsSimulationSpace::WorldSpace,
				SimulationSpace,
				SimpleExternalForce);
		}
		else
		{
			SimpleExternalForceInSimSpace = SimpleExternalForce;
		}
	}
	else
	{
		SimpleExternalForceInSimSpace = FVector::ZeroVector;
	}

	// External Force : PreApply
	// NOTE: if use foreach, you may get issue ( Array has changed during ranged-for iteration )
	for (int i = 0; i < CustomExternalForces.Num(); ++i)
	{
		if (CustomExternalForces[i])
		{
			CustomExternalForces[i]->PreApply(*this, SkelComp);
		}
	}
	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			auto& Force = ExternalForces[i].GetMutable<FKawaiiPhysics_ExternalForce>();
			Force.PreApply(*this, Output);
		}
	}

	// ===== サブステップ設定キャッシュ & Scene 取得（毎フレーム1回） =====
	const UKawaiiPhysicsDeveloperSettings* KawaiiSettings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	bUseFixedSubsteppingCached = KawaiiSettings->bUseFixedSubstepping;
	MaxSubstepsCached = FMath::Max(1, KawaiiSettings->MaxSubsteps);

	const UWorld* World = SkelComp ? SkelComp->GetWorld() : nullptr;
	const FSceneInterface* Scene = World ? World->Scene : nullptr;

	// 現フレームのポーズ目標をスナップショット（サブステップ中 PoseLocation を補間で上書きするため退避）。
	// 初回/リセット後は前フレーム値を現在値で初期化（補間で飛ばないように）。
	// Snapshot this frame's pose target (PoseLocation is overwritten by interpolation during substeps).
	// On first frame / after reset, seed the previous-frame target with the current value (avoid a jump).
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		Bone.CurrentPoseLocation = Bone.PoseLocation;
		Bone.CurrentPoseRotation = Bone.PoseRotation;
		if (!bSubstepPoseInitialized)
		{
			Bone.PrevPoseLocation = Bone.PoseLocation;
			Bone.PrevPoseRotation = Bone.PoseRotation;
		}
	}
	bSubstepPoseInitialized = true;

	if (!bUseFixedSubsteppingCached)
	{
		// ===== Legacy: 実フレーム時間で1ステップ（GetStepDeltaTime()==DeltaTime） =====
		bInSubstep = false;
		SimulateOnce(Output, ComponentTransform, Scene, SkelComp);
		DeltaTimeOld = DeltaTime;
	}
	else
	{
		// ===== 固定タイムステップ・サブステップ（§4） =====
		const float FixedDt = 1.0f / GetEffectiveTargetFramerate();
		SubstepAccumulator += FrameDeltaTime;
		// spiral of death 防止：超過分は破棄 / clamp to avoid spiral of death (drop excess time)
		SubstepAccumulator = FMath::Min(SubstepAccumulator, MaxSubstepsCached * FixedDt);
		const int32 NumSteps = FMath::FloorToInt(SubstepAccumulator / FixedDt);
		SubstepAccumulator -= NumSteps * FixedDt;

		// world移動を各サブステップへ分配（並進=線形分配、回転=増分割合のSlerp）。§7-D
		// Distribute world movement across substeps (translation linear, rotation incremental fraction).
		const float MoveFrac = (FrameDeltaTime > KINDA_SMALL_NUMBER) ? (FixedDt / FrameDeltaTime) : 1.0f;
		const FVector FullSkelCompMove = SkelCompMoveVector;
		const FQuat FullSkelCompRot = SkelCompMoveRotation;

		bInSubstep = true;
		StepDeltaTime = FixedDt;
		DeltaTimeOld = FixedDt;
		for (int32 SubstepIndex = 0; SubstepIndex < NumSteps; ++SubstepIndex)
		{
			// 注: ローカル名は基底クラスのメンバ Alpha（ブレンド係数）を隠さないよう SubstepAlpha とする
			// Note: named SubstepAlpha to avoid shadowing the base-class member Alpha (blend weight)
			const float SubstepAlpha = static_cast<float>(SubstepIndex + 1) / static_cast<float>(NumSteps);

			// ポーズ目標をサブステップ補間（§5）
			for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
			{
				Bone.PoseLocation = FMath::Lerp(Bone.PrevPoseLocation, Bone.CurrentPoseLocation, SubstepAlpha);
				Bone.PoseRotation = FQuat::Slerp(Bone.PrevPoseRotation, Bone.CurrentPoseRotation, SubstepAlpha).GetNormalized();
			}

			// world移動の分配分だけを適用させる（Simulateはメンバを参照するため一時設定）
			SkelCompMoveVector = FullSkelCompMove * MoveFrac;
			SkelCompMoveRotation = FQuat::Slerp(FQuat::Identity, FullSkelCompRot, MoveFrac).GetNormalized();

			SimulateOnce(Output, ComponentTransform, Scene, SkelComp);
		}
		bInSubstep = false;

		// world移動メンバを元へ（次フレームの UpdateSkelCompMove で再計算されるが安全のため）
		SkelCompMoveVector = FullSkelCompMove;
		SkelCompMoveRotation = FullSkelCompRot;

		// PoseLocation を現フレームの真値へ戻す（出力・次フレーム捕捉の整合）
		for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
		{
			Bone.PoseLocation = Bone.CurrentPoseLocation;
			Bone.PoseRotation = Bone.CurrentPoseRotation;
		}
	}

	// 次フレームのポーズ補間用に現フレーム値を確定
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		Bone.PrevPoseLocation = Bone.CurrentPoseLocation;
		Bone.PrevPoseRotation = Bone.CurrentPoseRotation;
	}
}

void FAnimNode_KawaiiPhysics::SimulateOnce(FComponentSpacePoseContext& Output,
                                           const FTransform& ComponentTransform,
                                           const FSceneInterface* Scene,
                                           const USkeletalMeshComponent* SkelComp)
{
	// root bone（ParentIndex<0）の kinematic follow: （補間済み）ポーズへ追従。
	// 元の skip ループから移設。サブステップ毎に補間ポーズへ追従させる。
	// root bones follow the (interpolated) pose. Moved out of the skip loop so it runs every substep.
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (Bone.ParentIndex < 0 && !Bone.bBridgeDummy && !(Bone.BoneRef.BoneIndex < 0 && !Bone.bDummy))
		{
			Bone.PrevLocation = Bone.Location;
			Bone.Location = Bone.PoseLocation;
		}
	}

	// Simulate（Exponent は GetStepDeltaTime ベース。サブステップ時は TargetFramerate*FixedDt=1）
	const int32 EffectiveTargetFramerate = GetEffectiveTargetFramerate();
	const float Exponent = EffectiveTargetFramerate * GetStepDeltaTime();
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (Bone.bSkipSimulate)
		{
			continue;
		}

		// コリジョン専用モード: Simulate()をスキップ（コリジョンとbone length restorationは後で実行）
		// Collision-only mode: skip physics simulation, position from real bones after loop
		if (Bone.bInterBoneDummy && bBoneSubdivisionCollisionOnly)
		{
			continue;
		}

		// bridge dummyは常にSimulate()をスキップ（縦親が無くModifyBones[ParentIndex]参照でクラッシュする）
		// Bridge dummies always skip Simulate() (no vertical parent → ModifyBones[Bone.ParentIndex] would crash)
		if (Bone.bBridgeDummy)
		{
			continue;
		}

		Simulate(Bone, Scene, ComponentTransform, Exponent, SkelComp, Output);
	}

	// コリジョン専用モード: 全実ボーンのシミュレーション完了後、シミュレーション済みのLocation間にダミーを配置
	// Collision-only: position dummies between simulated real bones (not PoseLocation)
	if (bBoneSubdivisionCollisionOnly)
	{
		for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
		{
			if (Bone.bInterBoneDummy)
			{
				if (!ensureMsgf(ModifyBones.IsValidIndex(Bone.InterBoneRealParentIndex) &&
				                ModifyBones.IsValidIndex(Bone.InterBoneRealChildIndex),
				                TEXT("KawaiiPhysics: invalid inter-bone dummy endpoint index.")))
				{
					continue;
				}

				Bone.PrevLocation = Bone.Location;
				Bone.Location = FMath::Lerp(
					ModifyBones[Bone.InterBoneRealParentIndex].Location,
					ModifyBones[Bone.InterBoneRealChildIndex].Location,
					Bone.InterBoneAlpha);
			}
		}
	}

	// bridge dummy（横方向Constraintのコリジョン代理）を端点間で配置。
	// bBoneSubdivisionCollisionOnlyに依らず常に実行し、縦dummyの後（より大きいindex）に走るため端点は配置済み。
	// Position bridge dummies (horizontal-constraint collision proxies) between their two endpoints.
	// Always runs (independent of bBoneSubdivisionCollisionOnly); they have higher indices than vertical dummies, so endpoints are already placed.
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_BridgeDummy);
		const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
		for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
		{
			if (!Bone.bBridgeDummy)
			{
				continue;
			}

			if (!ensureMsgf(ModifyBones.IsValidIndex(Bone.InterBoneRealParentIndex) &&
			                ModifyBones.IsValidIndex(Bone.InterBoneRealChildIndex),
			                TEXT("KawaiiPhysics: invalid bridge dummy endpoint index.")))
			{
				continue;
			}

			const FKawaiiPhysicsModifyBone& EndA = ModifyBones[Bone.InterBoneRealParentIndex];
			const FKawaiiPhysicsModifyBone& EndB = ModifyBones[Bone.InterBoneRealChildIndex];

			// LOD安全: 実ボーン端点がLODでカルされている場合はproxyをスキップ（staleな端点での誤配置を防ぐ）
			// LOD safety: skip when a real-bone endpoint is culled by the current LOD (avoid placing from stale endpoints)
			if ((!EndA.bDummy && EndA.BoneRef.BoneIndex >= 0 && EndA.BoneRef.GetCompactPoseIndex(BoneContainer) < 0) ||
				(!EndB.bDummy && EndB.BoneRef.BoneIndex >= 0 && EndB.BoneRef.GetCompactPoseIndex(BoneContainer) < 0))
			{
				// 端点がLODでカル → proxyを無効化し、後続のコリジョンループからも除外する（stale位置で当たらないように）。
				// 次フレーム冒頭のダミー配置ループでbSkipSimulate=falseに戻り再評価される。
				// Endpoint culled by LOD -> disable the proxy and exclude it from the following collision loop
				// (so it doesn't collide at a stale position). The dummy-placement loop at the start of the next
				// frame resets bSkipSimulate=false and re-evaluates.
				Bone.bSkipSimulate = true;
				continue;
			}

			Bone.PrevLocation = Bone.Location;
			Bone.Location = FMath::Lerp(EndA.Location, EndB.Location, Bone.InterBoneAlpha);
			// LERP基準位置をPoseLocationに退避（コリジョン後の押し出し量 = Location - PoseLocation を測るため。
			// bridge dummyのPoseLocationは他で未使用なので流用）。
			// Stash the LERP base in PoseLocation so the post-collision push = (Location - PoseLocation) can be measured.
			// PoseLocation is otherwise unused for bridge dummies.
			Bone.PoseLocation = Bone.Location;
		}
	}

	// External Force : PostApply
	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			auto& Force = ExternalForces[i].GetMutable<FKawaiiPhysics_ExternalForce>();
			Force.PostApply(*this, Output);
		}
	}

	// Adjust by Bone Constraints Before Collision
	if (BoneConstraintIterationCountBeforeCollision > 0)
	{
		for (FModifyBoneConstraint& BoneConstraint : MergedBoneConstraints)
		{
			BoneConstraint.Lambda = 0.0f;
		}
		for (int i = 0; i < BoneConstraintIterationCountBeforeCollision; ++i)
		{
			AdjustByBoneConstraints();
		}
	}

	// Adjust by collisions
	// NOTE: 形状ごとにループを分けると ModifyBones を複数回走査してキャッシュ効率が落ちるため
	// （ボーン数が多いケースで負荷増）、従来どおりボーン外側の1パスで全形状を処理する。
	// World判定の時間は関数内の既存STAT（STAT_KawaiiPhysics_WorldCollision）で計測する。
	// NOTE: keep a single per-bone pass for all shapes. Splitting per shape would re-traverse
	// ModifyBones multiple times and hurt cache locality (a regression for high bone counts).
	// World-check time is measured by the existing STAT inside AdjustByWorldCollision.
	int32 NumWorldChecks = 0;
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (Bone.bSkipSimulate)
		{
			continue;
		}

		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AdjustByCollision);

		AdjustBySphereCollision(Bone, SphericalLimits);
		AdjustBySphereCollision(Bone, SphericalLimitsData);
		AdjustByCapsuleCollision(Bone, CapsuleLimits);
		AdjustByCapsuleCollision(Bone, CapsuleLimitsData);
		AdjustByBoxCollision(Bone, BoxLimits);
		AdjustByBoxCollision(Bone, BoxLimitsData);
		AdjustByPlanerCollision(Bone, PlanarLimits);
		AdjustByPlanerCollision(Bone, PlanarLimitsData);

		// 共有コリジョン / Shared collision from other KawaiiPhysics nodes
		if (bUseSharedCollision && !bSharedCollisionSource)
		{
			AdjustBySphereCollision(Bone, SharedSphericalLimits);
			AdjustByCapsuleCollision(Bone, SharedCapsuleLimits);
			AdjustByBoxCollision(Bone, SharedBoxLimits);
			AdjustByPlanerCollision(Bone, SharedPlanarLimits);
		}

		if (bAllowWorldCollision)
		{
			AdjustByWorldCollision(Output, Bone, SkelComp);
			++NumWorldChecks; // 発行したワールドスイープ回数 / world sweeps issued this frame
		}
	}
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumWorldCollisionChecks, NumWorldChecks);

	// bridge dummy のコリジョン変位を端点ボーンへ直接転送（実ボーンを押し出すフィードバックの本体）。
	// コリジョン後・Constraint/length復元前に実行。Push = Location(押し出し後) - PoseLocation(上のLERP配置で退避したLERP基準)。
	// 端点へ距離比 (1-α):α で配分し、Scaleで強さを調整。端点が縦dummyの場合も後段のlength復元で実子へ伝播する。
	// Direct displacement transfer: push the bridge dummy's collision displacement into its endpoint bones (this is
	// what makes a collider moving between columns actually move the cloth). Runs after collision, before constraints/
	// length restore. Push = Location - PoseLocation (LERP base stashed above during bridge-dummy placement),
	// distributed (1-alpha):alpha, scaled.
	if (BoneConstraintSubdivisionCount > 0 && BoneConstraintSubdivisionFeedbackScale > 0.0f)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_BridgeDummy);
		// 端点ごとに「重み付き押し出し量の合計」と「重みの合計」を集計し、最後に divisor=max(1,重み合計) で割る。
		// 寄与が少ない（重み合計<=1, 局所接触の通常ケース）ときは単純加算と同一の挙動を保ち、
		// 多数のdummyが同じ端点を押す病的ケースでのみ加重平均化してN倍のオーバーシュート/発振を防ぐ。
		// Accumulate per-endpoint weighted push and total weight, then divide by max(1, total weight).
		// When few dummies contribute (weight <= 1, typical localized contact) this equals plain additive transfer;
		// only when many dummies push the same endpoint does it average out, preventing N-fold overshoot/oscillation.
		TMap<int32, FVector> AccumPush;
		TMap<int32, float> AccumWeight;
		// 端点インデックスは最大でもModifyBones数。事前確保で毎フレームの再ハッシュを回避
		// Endpoint indices are bounded by ModifyBones count; reserve to avoid per-frame rehashing
		AccumPush.Reserve(ModifyBones.Num());
		AccumWeight.Reserve(ModifyBones.Num());

		for (const FKawaiiPhysicsModifyBone& Bone : ModifyBones)
		{
			if (!Bone.bBridgeDummy || Bone.bSkipSimulate)
			{
				continue;
			}
			if (!ModifyBones.IsValidIndex(Bone.InterBoneRealParentIndex) ||
				!ModifyBones.IsValidIndex(Bone.InterBoneRealChildIndex))
			{
				continue;
			}

			const FVector Push = Bone.Location - Bone.PoseLocation; // コリジョンによる押し出し量 / collision displacement
			if (Push.IsNearlyZero())
			{
				continue;
			}

			const float A = Bone.InterBoneAlpha;
			const int32 E1 = Bone.InterBoneRealParentIndex;
			const int32 E2 = Bone.InterBoneRealChildIndex;
			const float W1 = 1.0f - A; // 端点1に近いほど寄与大 / closer to endpoint1 => larger weight
			const float W2 = A;

			AccumPush.FindOrAdd(E1) += Push * W1;
			AccumWeight.FindOrAdd(E1) += W1;
			AccumPush.FindOrAdd(E2) += Push * W2;
			AccumWeight.FindOrAdd(E2) += W2;
		}

		for (const TPair<int32, FVector>& Entry : AccumPush)
		{
			if (ModifyBones.IsValidIndex(Entry.Key))
			{
				const float W = AccumWeight.FindChecked(Entry.Key);
				ModifyBones[Entry.Key].Location += (Entry.Value / FMath::Max(1.0f, W)) * BoneConstraintSubdivisionFeedbackScale;
			}
		}
	}

	// Adjust by Bone Constraints After Collision
	if (BoneConstraintIterationCountAfterCollision > 0)
	{
		for (FModifyBoneConstraint& BoneConstraint : MergedBoneConstraints)
		{
			BoneConstraint.Lambda = 0.0f;
		}
		for (int i = 0; i < BoneConstraintIterationCountAfterCollision; ++i)
		{
			AdjustByBoneConstraints();
		}
	}

	// Adjust by Limits and Bone Length（角度制限 + 平面制約 + ボーン長復元のO(N)ループをまとめて計測）
	// Adjust by limits and bone length (measure the angle-limit + planar-constraint + length-restore O(N) loop)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AdjustByLimitsAndLength);
		for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
		{
			if (Bone.bSkipSimulate)
			{
				continue;
			}

			// bridge dummyは縦親を持たないため長さ/角度復元をスキップ（ParentIndex=-1参照でクラッシュ）。
			// 位置は直前のconstraint solveで確定済みで、次フレーム冒頭で端点間に再LERPされる。
			// Bridge dummies have no vertical parent → skip length/angle restore (would deref ParentIndex=-1).
			// Their position is finalized by the preceding constraint solve and re-LERP'd next frame.
			if (Bone.bBridgeDummy)
			{
				continue;
			}

			auto& ParentBone = ModifyBones[Bone.ParentIndex];

			// Adjust by angle limit
			AdjustByAngleLimit(Bone, ParentBone);

			// Adjust by Planar Constraint
			AdjustByPlanarConstraint(Bone, ParentBone);

			// Restore Bone Length
			const float BoneLength = (Bone.PoseLocation - ParentBone.PoseLocation).Size();
			Bone.Location = (Bone.Location - ParentBone.Location).GetSafeNormal() * BoneLength + ParentBone.Location;
		}
	}
	// 注: DeltaTimeOld は呼び出し元 SimulateModifyBones（legacy=DeltaTime / substep=FixedDt）で設定
	// Note: DeltaTimeOld is set by the caller SimulateModifyBones (legacy=DeltaTime / substep=FixedDt)
}

void FAnimNode_KawaiiPhysics::Simulate(FKawaiiPhysicsModifyBone& Bone, const FSceneInterface* Scene,
                                       const FTransform& ComponentTransform,
                                       const float& Exponent, const USkeletalMeshComponent* SkelComp,
                                       FComponentSpacePoseContext& Output)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_Simulate);

	const FKawaiiPhysicsModifyBone& ParentBone = ModifyBones[Bone.ParentIndex];

	// wind（Output依存）を計算。減衰後の速度に足し込む寄与として ComputeVerletStepVelocity へ渡す。
	// Wind (Output-dependent). Passed to ComputeVerletStepVelocity as a contribution added onto the post-damping velocity.
	FVector WindVelocity = FVector::ZeroVector;
	if (bEnableWind && Scene)
	{
		WindVelocity = GetWindVelocity(Output, Scene, Bone) * GetEffectiveTargetFramerate();
	}

	// このステップの速度を作る（速度再構成 → damping → +wind → gravity）。
	// Build this step's velocity (reconstruct -> damping -> +wind -> gravity).
	FVector Velocity = ComputeVerletStepVelocity(Bone, WindVelocity);

	// ユーザー外力に「実際の速度」を渡す（gravity の後・位置更新の前）。ApplyToVelocity は
	// InOutVelocity を読む実装もあり得るため、集約せずここで実速度に対して呼ぶ。
	// Hand the real velocity to user external forces (after gravity, before position integration).
	// ApplyToVelocity may read InOutVelocity, so it runs on the real velocity here (not on a pre-gathered accumulator).
	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			if (const auto ExForce = ExternalForces[i].GetMutablePtr<FKawaiiPhysics_ExternalForce>();
				ExForce->bIsEnabled)
			{
				ExForce->ApplyToVelocity(Bone, *this, Output, Velocity);
			}
		}
	}

	// 速度のぶんだけ位置を進める。 / Move the position by the velocity.
	IntegrateVerletStepPosition(Bone, Velocity);

	// Simple External Force（速度を経由しない位置オフセット） / position offset, not via velocity.
	ApplySimpleExternalForce(Bone);

	// Follow World Movement
	if (SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace
		&& TeleportType != ETeleportType::TeleportPhysics)
	{
		// BaseBoneSpace は Output 依存の空間変換が必要なため、別関数に切り出さずここで実行。
		// BaseBoneSpace needs Output-dependent space conversions, so it is handled inline here.
		// Follow Translation
		const FVector SkelCompMoveVectorBBS =
			ConvertSimulationSpaceVector(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace,
			                             EKawaiiPhysicsSimulationSpace::BaseBoneSpace, SkelCompMoveVector);
		Bone.Location += SkelCompMoveVectorBBS * (1.0f - Bone.PhysicsSettings.WorldDampingLocation);

		// Follow Rotation
		const FVector PrevLocationCS = PrevBaseBoneSpace2ComponentSpace.TransformPosition(Bone.PrevLocation);
		const FVector RotatedLocationCS = SkelCompMoveRotation.RotateVector(PrevLocationCS);
		const FVector RotatedLocationBase = ConvertSimulationSpaceLocationCached(
			FSimulationSpaceCache(), CurrentEvalSimSpaceCache, RotatedLocationCS);
		Bone.Location += (RotatedLocationBase - Bone.PrevLocation) * (1.0f - Bone.PhysicsSettings.WorldDampingRotation);
	}
	else
	{
		// ComponentSpace / WorldSpace（BaseBoneSpace 以外） / Component & World space (non-BaseBoneSpace).
		ApplyWorldMoveFollowNonBaseBone(Bone);
	}

	// External Force
	// NOTE: if use foreach, you may get issue ( Array has changed during ranged-for iteration )
	for (int i = 0; i < CustomExternalForces.Num(); ++i)
	{
		if (CustomExternalForces[i] && CustomExternalForces[i]->bIsEnabled)
		{
			FTransform BoneTM = FTransform::Identity;
			if (Bone.bDummy)
			{
				// inter-bone dummy / 分割末端dummy は実親ボーンのTransformを使用（親がまた別のdummyでBoneRef空のときにクラッシュ防止）
				// Inter-bone & subdivided tip dummies use the real parent transform (prevents crash when the parent is another dummy with empty BoneRef)
				const FKawaiiPhysicsModifyBone& TransformBone = ModifyBones.IsValidIndex(Bone.InterBoneRealParentIndex)
					? ModifyBones[Bone.InterBoneRealParentIndex]
					: ParentBone;
				const FCompactPoseBoneIndex TransformCPI =
					TransformBone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer());
				if (TransformCPI >= 0)
				{
					BoneTM = GetBoneTransformInSimSpace(Output, TransformCPI);
				}
				// else: 無効CompactPose(LOD等) → BoneTMはIdentityのまま / invalid CompactPose → keep Identity
			}
			else
			{
				BoneTM = GetBoneTransformInSimSpace(
					Output, Bone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
			}

			CustomExternalForces[i]->Apply(*this, Bone.Index, SkelComp, BoneTM);
		}
	}

	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			if (const auto ExForce = ExternalForces[i].GetMutablePtr<FKawaiiPhysics_ExternalForce>();
				ExForce->bIsEnabled)
			{
				if (ExForce->ExternalForceSpace == EExternalForceSpace::BoneSpace)
				{
					FTransform BoneTM = FTransform::Identity;
					if (Bone.bDummy)
					{
						// inter-bone dummy / 分割末端dummy は実親ボーンのTransformを使用（BoneRef空クラッシュ防止）
						// Inter-bone & subdivided tip dummies use the real parent transform (prevents empty-BoneRef crash)
						const FKawaiiPhysicsModifyBone& TransformBone = ModifyBones.IsValidIndex(Bone.InterBoneRealParentIndex)
							? ModifyBones[Bone.InterBoneRealParentIndex]
							: ParentBone;
						const FCompactPoseBoneIndex TransformCPI =
							TransformBone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer());
						if (TransformCPI >= 0)
						{
							BoneTM = GetBoneTransformInSimSpace(Output, TransformCPI);
						}
						// else: 無効CompactPose(LOD等) → BoneTMはIdentityのまま / invalid CompactPose → keep Identity
					}
					else
					{
						BoneTM = GetBoneTransformInSimSpace(
							Output, Bone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
					}

					ExForce->Apply(Bone, *this, Output, BoneTM);
				}
				else
				{
					ExForce->Apply(Bone, *this, Output);
				}
			}
		}
	}

	// Pull to Pose Location（剛性） / Pull toward pose location (stiffness).
	ApplyStiffnessPull(Bone, ParentBone, Exponent);
}

// ============================================================================
//  物理計算の各ステップ（引数に FComponentSpacePoseContext を取らない）。Simulate() から呼ばれる。
//  Each physics step; takes no FComponentSpacePoseContext. Called from Simulate().
//  注: ここを変更したら、Simulate() 内の wind/ApplyToVelocity の呼び出し位置との整合も確認すること。
//  NOTE: if you change these, re-check consistency with the wind/ApplyToVelocity call sites in Simulate().
// ============================================================================

FVector FAnimNode_KawaiiPhysics::ComputeVerletStepVelocity(FKawaiiPhysicsModifyBone& Bone,
                                                     const FVector& WindVelocity)
{
	// Move using Velocity( = movement amount in pre frame ) and Damping
	// 速度は前ステップ変位を DeltaTimeOld で割って再構成。固定サブステップ時 DeltaTimeOld=FixedDt。
	// Velocity is reconstructed from the previous step's displacement / DeltaTimeOld (= FixedDt while substepping).
	FVector Velocity = (Bone.Location - Bone.PrevLocation) / DeltaTimeOld;
	Bone.PrevLocation = Bone.Location;

	// 毎ステップ生の damping 係数（固定サブステップ化でフレームレート依存は解消済み）。
	// Raw per-step damping (frame-rate dependence is resolved by fixed substepping).
	Velocity *= (1.0f - Bone.PhysicsSettings.Damping);

	// wind（呼び出し元で計算済み） / wind (computed by the caller).
	Velocity += WindVelocity;

	// Gravity (apply just after wind; keep legacy compatibility via separate position term)
	const float StepDt = GetStepDeltaTime();
	if (!bUseLegacyGravity)
	{
		// AnimDynamics-like: integrate acceleration into velocity
		Velocity += GravityInSimSpace * StepDt;
	}
	else
	{
		// Legacy gravity: add 0.5 * g * dt^2 to position
		Bone.Location += 0.5 * GravityInSimSpace * StepDt * StepDt;
	}

	return Velocity;
}

void FAnimNode_KawaiiPhysics::IntegrateVerletStepPosition(FKawaiiPhysicsModifyBone& Bone, const FVector& Velocity)
{
	// Integrate position from velocity
	Bone.Location += Velocity * GetStepDeltaTime();
}

void FAnimNode_KawaiiPhysics::ApplySimpleExternalForce(FKawaiiPhysicsModifyBone& Bone)
{
	// Simple External Force（速度を経由しない位置オフセット。SimulateModifyBones でキャッシュ済み）。
	// world-move / stiffness と同じ「位置空間の後処理」なので Verlet ステップから分離している。
	// Simple external force: a position offset that does NOT go through Velocity (cached in SimulateModifyBones).
	// Split out from the Verlet step because it is a position-space post-op like world-move / stiffness.
	if (!SimpleExternalForceInSimSpace.IsNearlyZero())
	{
		Bone.Location += SimpleExternalForceInSimSpace * GetStepDeltaTime();
	}
}

void FAnimNode_KawaiiPhysics::ApplyWorldMoveFollowNonBaseBone(FKawaiiPhysicsModifyBone& Bone)
{
	// Follow World Movement（ComponentSpace/WorldSpace のみ。BaseBoneSpaceは Simulate() で別処理）
	// World-movement follow for Component/World space only (BaseBoneSpace handled inline in Simulate()).
	if (SimulationSpace != EKawaiiPhysicsSimulationSpace::WorldSpace
		&& TeleportType != ETeleportType::TeleportPhysics)
	{
		// Follow Translation
		Bone.Location += SkelCompMoveVector * (1.0f - Bone.PhysicsSettings.WorldDampingLocation);

		// Follow Rotation
		Bone.Location += (SkelCompMoveRotation.RotateVector(Bone.PrevLocation) - Bone.PrevLocation)
			* (1.0f - Bone.PhysicsSettings.WorldDampingRotation);
	}
}

void FAnimNode_KawaiiPhysics::ApplyStiffnessPull(FKawaiiPhysicsModifyBone& Bone,
                                                 const FKawaiiPhysicsModifyBone& ParentBone, float Exponent)
{
	// Pull to Pose Location
	const FVector BaseLocation = ParentBone.Location + (Bone.PoseLocation - ParentBone.PoseLocation);
	Bone.Location += (BaseLocation - Bone.Location) *
		(1.0f - FMath::Pow(1.0f - Bone.PhysicsSettings.Stiffness, Exponent));
}

FVector FAnimNode_KawaiiPhysics::GetWindVelocity(FComponentSpacePoseContext& Output, const FSceneInterface* Scene,
                                                 const FKawaiiPhysicsModifyBone& Bone) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_GetWindVelocity);

	if (WindScale == 0.0f || !Scene)
	{
		return FVector::ZeroVector;
	}

	FVector WindDirection = FVector::ZeroVector;
	float WindSpeed = 0.0f;
	float WindMinGust = 0.0f;
	float WindMaxGust = 0.0f;

	Scene->GetWindParameters_GameThread(
		ConvertSimulationSpaceLocation(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace,
		                               Bone.PoseLocation),
		WindDirection, WindSpeed, WindMinGust, WindMaxGust);

	WindDirection =
		ConvertSimulationSpaceVector(Output, EKawaiiPhysicsSimulationSpace::WorldSpace, SimulationSpace, WindDirection);
	if (WindDirectionNoiseAngle > 0)
	{
		WindDirection = FMath::VRandCone(WindDirection, FMath::DegreesToRadians(WindDirectionNoiseAngle));
	}

	FVector WindVelocity = WindDirection * WindSpeed * WindScale;

	// TODO:Migrate if there are more good method (Currently copying AnimDynamics implementation)
	WindVelocity *= FMath::FRandRange(0.0f, 2.0f);

	return WindVelocity;
}

void FAnimNode_KawaiiPhysics::WarmUp(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                     FTransform& ComponentTransform)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_WarmUp);

	// サブステップ有効時は、各ウォームアップ反復がちょうど1固定ステップになるよう DeltaTime=FixedDt とし、
	// アキュムレータを退避（ウォームアップ分を本流の蓄積に混ぜない）。ポーズは現値固定で安定化させる。
	// When substepping, force DeltaTime = FixedDt so each warm-up iteration runs exactly one fixed step,
	// and isolate the accumulator from the main loop. Pose is held at the current value to settle.
	const UKawaiiPhysicsDeveloperSettings* KawaiiSettings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	const bool bSubstep = KawaiiSettings && KawaiiSettings->bUseFixedSubstepping;
	const float SavedDeltaTime = DeltaTime;
	const float SavedAccumulator = SubstepAccumulator;
	if (bSubstep)
	{
		DeltaTime = 1.0f / GetEffectiveTargetFramerate();
		SubstepAccumulator = 0.0f;
	}

	for (int32 i = 0; i < WarmUpFrames; ++i)
	{
		SimulateModifyBones(Output, ComponentTransform);
	}

	if (bSubstep)
	{
		DeltaTime = SavedDeltaTime;
		SubstepAccumulator = SavedAccumulator;
	}
}

void FAnimNode_KawaiiPhysics::ApplySimulateResult(FComponentSpacePoseContext& Output,
                                                  const FBoneContainer& BoneContainer,
                                                  TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ApplySimulateResult);

	for (int32 i = 0; i < ModifyBones.Num(); ++i)
	{
		FTransform PoseTransform = FTransform(ModifyBones[i].PoseRotation, ModifyBones[i].PoseLocation,
		                                      ModifyBones[i].PoseScale);
		PoseTransform =
			ConvertSimulationSpaceTransform(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::ComponentSpace,
			                                PoseTransform);
		OutBoneTransforms.Add(FBoneTransform(ModifyBones[i].BoneRef.GetCompactPoseIndex(BoneContainer), PoseTransform));
	}


	for (int32 i = 0; i < ModifyBones.Num(); ++i)
	{
		FKawaiiPhysicsModifyBone& Bone = ModifyBones[i];
		if (!Bone.HasParent())
		{
			continue;
		}

		FKawaiiPhysicsModifyBone& ParentBone = ModifyBones[Bone.ParentIndex];

		if (ParentBone.ChildIndices.Num() <= 1)
		{
			if (ParentBone.BoneRef.BoneIndex >= 0)
			{
				FVector PoseVector = Bone.PoseLocation - ParentBone.PoseLocation;
				FVector SimulateVector = Bone.Location - ParentBone.Location;

				if (PoseVector.GetSafeNormal() == SimulateVector.GetSafeNormal())
				{
					continue;
				}

				if (BoneForwardAxis == EBoneForwardAxis::X_Negative || BoneForwardAxis == EBoneForwardAxis::Y_Negative
					|| BoneForwardAxis == EBoneForwardAxis::Z_Negative)
				{
					PoseVector *= -1;
					SimulateVector *= -1;
				}

				FQuat SimulateRotation =
					FQuat::FindBetweenVectors(PoseVector, SimulateVector) * ParentBone.PoseRotation;
				ParentBone.PrevRotation = SimulateRotation;

				SimulateRotation =
					ConvertSimulationSpaceRotation(Output, SimulationSpace,
					                               EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulateRotation);
				OutBoneTransforms[Bone.ParentIndex].Transform.SetRotation(SimulateRotation);
			}
		}

		if (Bone.BoneRef.BoneIndex >= 0 && !Bone.bDummy)
		{
			OutBoneTransforms[i].Transform.SetLocation(
				ConvertSimulationSpaceLocation(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::ComponentSpace,
				                               Bone.Location));
		}
	}

	OutBoneTransforms.RemoveAll([](const FBoneTransform& BoneTransform)
	{
		return BoneTransform.BoneIndex < 0;
	});

	// for check in FCSPose<PoseType>::LocalBlendCSBoneTransforms
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

FTransform FAnimNode_KawaiiPhysics::GetBoneTransformInSimSpace(FComponentSpacePoseContext& Output,
                                                               const FCompactPoseBoneIndex& BoneIndex) const
{
	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(
		Output, EKawaiiPhysicsSimulationSpace::ComponentSpace);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, SimulationSpace);
	return ConvertSimulationSpaceTransformCached(CacheFrom, CacheTo, Output.Pose.GetComponentSpaceTransform(BoneIndex));
}

FAnimNode_KawaiiPhysics::FSimulationSpaceCache FAnimNode_KawaiiPhysics::GetSimulationSpaceCacheFor(
	FComponentSpacePoseContext& Output,
	const EKawaiiPhysicsSimulationSpace Space) const
{
	if (Space == EKawaiiPhysicsSimulationSpace::ComponentSpace)
	{
		return FSimulationSpaceCache();
	}
	if (bHasCurrentEvalSimSpaceCache && Space == SimulationSpace)
	{
		return CurrentEvalSimSpaceCache;
	}
	if (bHasCurrentEvalWorldSpaceCache && Space == EKawaiiPhysicsSimulationSpace::WorldSpace)
	{
		return CurrentEvalWorldSpaceCache;
	}

	const UEnum* EnumPtr = StaticEnum<EKawaiiPhysicsSimulationSpace>();
	UE_LOG(LogKawaiiPhysics, Verbose, TEXT("Building Simulation Space Cache for %s"),
	       *EnumPtr->GetNameStringByValue(static_cast<int64>(Space)));
	return BuildSimulationSpaceCache(Output, Space);
}

FTransform FAnimNode_KawaiiPhysics::ConvertSimulationSpaceTransformCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	const FTransform& InTransform) const
{
	FTransform ResultTransform = InTransform;
	ResultTransform = ResultTransform * CacheFrom.TargetSpaceToComponent;
	ResultTransform = ResultTransform * CacheTo.ComponentToTargetSpace;
	return ResultTransform;
}

FVector FAnimNode_KawaiiPhysics::ConvertSimulationSpaceVectorCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	const FVector& InVector) const
{
	FVector ResultVector = InVector;
	ResultVector = CacheFrom.TargetSpaceToComponent.TransformVector(ResultVector);
	ResultVector = CacheTo.ComponentToTargetSpace.TransformVector(ResultVector);
	return ResultVector;
}

FVector FAnimNode_KawaiiPhysics::ConvertSimulationSpaceLocationCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	const FVector& InLocation) const
{
	FVector ResultLocation = InLocation;
	ResultLocation = CacheFrom.TargetSpaceToComponent.TransformPosition(ResultLocation);
	ResultLocation = CacheTo.ComponentToTargetSpace.TransformPosition(ResultLocation);
	return ResultLocation;
}

FQuat FAnimNode_KawaiiPhysics::ConvertSimulationSpaceRotationCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	const FQuat& InRotation) const
{
	FQuat ResultRotation = InRotation;
	ResultRotation = CacheFrom.TargetSpaceToComponent.TransformRotation(ResultRotation);
	ResultRotation = CacheTo.ComponentToTargetSpace.TransformRotation(ResultRotation);
	return ResultRotation;
}

void FAnimNode_KawaiiPhysics::ConvertSimulationSpaceCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	EKawaiiPhysicsSimulationSpace From,
	EKawaiiPhysicsSimulationSpace To)
{
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		Bone.Location = CacheTo.ComponentToTargetSpace.TransformPosition(
			CacheFrom.TargetSpaceToComponent.TransformPosition(Bone.Location));
		Bone.PrevLocation = CacheTo.ComponentToTargetSpace.TransformPosition(
			CacheFrom.TargetSpaceToComponent.TransformPosition(Bone.PrevLocation));

		Bone.PrevRotation = CacheTo.ComponentToTargetSpace.TransformRotation(
			CacheFrom.TargetSpaceToComponent.TransformRotation(Bone.PrevRotation));
	}
}

FTransform FAnimNode_KawaiiPhysics::ConvertSimulationSpaceTransform(FComponentSpacePoseContext& Output,
                                                                    const EKawaiiPhysicsSimulationSpace From,
                                                                    const EKawaiiPhysicsSimulationSpace To,
                                                                    const FTransform& InTransform) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpaceTransform);
	if (From == To)
	{
		return InTransform;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	return ConvertSimulationSpaceTransformCached(CacheFrom, CacheTo, InTransform);
}

FVector FAnimNode_KawaiiPhysics::ConvertSimulationSpaceVector(FComponentSpacePoseContext& Output,
                                                              const EKawaiiPhysicsSimulationSpace From,
                                                              const EKawaiiPhysicsSimulationSpace To,
                                                              const FVector& InVector) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpaceVector);
	if (From == To)
	{
		return InVector;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	return ConvertSimulationSpaceVectorCached(CacheFrom, CacheTo, InVector);
}

FVector FAnimNode_KawaiiPhysics::ConvertSimulationSpaceLocation(FComponentSpacePoseContext& Output,
                                                                const EKawaiiPhysicsSimulationSpace From,
                                                                const EKawaiiPhysicsSimulationSpace To,
                                                                const FVector& InLocation) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpaceLocation);
	if (From == To)
	{
		return InLocation;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	return ConvertSimulationSpaceLocationCached(CacheFrom, CacheTo, InLocation);
}

FQuat FAnimNode_KawaiiPhysics::ConvertSimulationSpaceRotation(FComponentSpacePoseContext& Output,
                                                              const EKawaiiPhysicsSimulationSpace From,
                                                              const EKawaiiPhysicsSimulationSpace To,
                                                              const FQuat& InRotation) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpaceRotation);
	if (From == To)
	{
		return InRotation;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	return ConvertSimulationSpaceRotationCached(CacheFrom, CacheTo, InRotation);
}

void FAnimNode_KawaiiPhysics::ConvertSimulationSpace(FComponentSpacePoseContext& Output,
                                                     const EKawaiiPhysicsSimulationSpace From,
                                                     const EKawaiiPhysicsSimulationSpace To)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpace);
	if (From == To)
	{
		return;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	ConvertSimulationSpaceCached(CacheFrom, CacheTo, From, To);
}

FAnimNode_KawaiiPhysics::FSimulationSpaceCache FAnimNode_KawaiiPhysics::BuildSimulationSpaceCache(
	FComponentSpacePoseContext& Output,
	const EKawaiiPhysicsSimulationSpace SimulationSpaceForCache) const
{
	FSimulationSpaceCache Cache;

	switch (SimulationSpaceForCache)
	{
	default:
	case EKawaiiPhysicsSimulationSpace::ComponentSpace:
		Cache.ComponentToTargetSpace = FTransform::Identity;
		Cache.TargetSpaceToComponent = FTransform::Identity;
		break;

	case EKawaiiPhysicsSimulationSpace::WorldSpace:
		{
			const FTransform& ComponentToWorld = Output.AnimInstanceProxy->GetComponentTransform();
			Cache.ComponentToTargetSpace = ComponentToWorld; // Component -> World
			Cache.TargetSpaceToComponent = ComponentToWorld.Inverse(); // World -> Component
		}
		break;

	case EKawaiiPhysicsSimulationSpace::BaseBoneSpace:

		if (SimulationBaseBone.IsValidToEvaluate())
		{
			const FCompactPoseBoneIndex BaseBoneIndex =
				SimulationBaseBone.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer());
			Cache.TargetSpaceToComponent =
				Output.Pose.GetComponentSpaceTransform(BaseBoneIndex); // Base -> Component
			Cache.ComponentToTargetSpace = Cache.TargetSpaceToComponent.Inverse(); // Component -> Base
		}
		break;
	}

	return Cache;
}
