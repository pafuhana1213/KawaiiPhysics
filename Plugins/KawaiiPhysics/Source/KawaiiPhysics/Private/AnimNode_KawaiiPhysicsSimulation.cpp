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
#include "Misc/Optional.h"
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

void FAnimNode_KawaiiPhysics::UpdatePhysicsSettingsOfModifyBones()
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdatePhysicsSetting);

	const FRichCurve* DampingCurve = DampingCurveData.GetRichCurveConst();
	const FRichCurve* WorldDampingLocationCurve = WorldDampingLocationCurveData.GetRichCurveConst();
	const FRichCurve* WorldDampingRotationCurve = WorldDampingRotationCurveData.GetRichCurveConst();
	const FRichCurve* StiffnessCurve = StiffnessCurveData.GetRichCurveConst();
	const FRichCurve* RadiusCurve = RadiusCurveData.GetRichCurveConst();
	const FRichCurve* LimitAngleCurve = LimitAngleCurveData.GetRichCurveConst();

	const bool bAllCurvesEmpty =
		DampingCurve->IsEmpty() &&
		WorldDampingLocationCurve->IsEmpty() &&
		WorldDampingRotationCurve->IsEmpty() &&
		StiffnessCurve->IsEmpty() &&
		RadiusCurve->IsEmpty() &&
		LimitAngleCurve->IsEmpty();

	if (bAllCurvesEmpty)
	{
		// 空カーブでも FRichCurve::DefaultValue が設定されている場合があるため、1.0f を決め打ちせず Eval の結果を使う。
		const float Damping = FMath::Clamp(
			PhysicsSettings.Damping * DampingCurve->Eval(0.0f, 1.0f), 0.0f, 1.0f);
		const float WorldDampingLocation = FMath::Clamp(
			PhysicsSettings.WorldDampingLocation * WorldDampingLocationCurve->Eval(0.0f, 1.0f), 0.0f, 1.0f);
		const float WorldDampingRotation = FMath::Clamp(
			PhysicsSettings.WorldDampingRotation * WorldDampingRotationCurve->Eval(0.0f, 1.0f), 0.0f, 1.0f);
		const float Stiffness = FMath::Clamp(
			PhysicsSettings.Stiffness * StiffnessCurve->Eval(0.0f, 1.0f), 0.0f, 1.0f);
		const float Radius = FMath::Max(
			PhysicsSettings.Radius * RadiusCurve->Eval(0.0f, 1.0f), 0.0f);
		const float LimitAngle = FMath::Max(
			PhysicsSettings.LimitAngle * LimitAngleCurve->Eval(0.0f, 1.0f), 0.0f);

		for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
		{
			Bone.PhysicsSettings.Damping = Damping;
			Bone.PhysicsSettings.WorldDampingLocation = WorldDampingLocation;
			Bone.PhysicsSettings.WorldDampingRotation = WorldDampingRotation;
			Bone.PhysicsSettings.Stiffness = Stiffness;
			Bone.PhysicsSettings.Radius = Radius;
			Bone.PhysicsSettings.LimitAngle = LimitAngle;
		}

		return;
	}

	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		const float LengthRate = Bone.LengthRateFromRoot;

		// Damping
		Bone.PhysicsSettings.Damping = FMath::Clamp(
			PhysicsSettings.Damping * DampingCurve->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// WorldLocationDamping
		Bone.PhysicsSettings.WorldDampingLocation = FMath::Clamp(
			PhysicsSettings.WorldDampingLocation * WorldDampingLocationCurve->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// WorldRotationDamping
		Bone.PhysicsSettings.WorldDampingRotation = FMath::Clamp(
			PhysicsSettings.WorldDampingRotation * WorldDampingRotationCurve->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// Stiffness
		Bone.PhysicsSettings.Stiffness = FMath::Clamp(
			PhysicsSettings.Stiffness * StiffnessCurve->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// Radius
		Bone.PhysicsSettings.Radius = FMath::Max(
			PhysicsSettings.Radius * RadiusCurve->Eval(
				LengthRate, 1.0f), 0.0f);
		
		// LimitAngle
		Bone.PhysicsSettings.LimitAngle = FMath::Max(
			PhysicsSettings.LimitAngle * LimitAngleCurve->Eval(
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
	FrameDeltaTime = DeltaTime;

	const USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy->GetSkelMeshComponent();

	// Prev/Pose 情報を保存し、SkipSimulate を判定
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		// bridge dummyは縦親(ParentIndex<0)を持たないが、コリジョン代理として非skipにする
		// （Simulate()/長さ復元は別途スキップ。下のParentIndex<0分岐に落ちると誤ってskip&Pose固定される）
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

	// SimpleExternalForce: SimulationSpace で一度だけ計算（ボーンごとの空間変換を避ける）
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
	// 注: foreach を使うと問題が起きうる（ranged-for 中に配列が変化する）
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
		PreSkelCompTransformConsumeFraction = 1.0f; // legacyは毎フレーム全消費
	}
	else
	{
		// ===== 固定タイムステップ・サブステップ（§4） =====
		const float FixedDt = 1.0f / GetEffectiveTargetFramerate();
		// クランプ前の蓄積実時間
		const float RawElapsed = FMath::Max(SubstepAccumulator + FrameDeltaTime, KINDA_SMALL_NUMBER);
		// spiral of death 防止：超過分は破棄
		SubstepAccumulator = FMath::Min(RawElapsed, MaxSubstepsCached * FixedDt);
		const float DroppedTime = RawElapsed - SubstepAccumulator; // クランプで破棄された時間
		const int32 NumSteps = FMath::FloorToInt(SubstepAccumulator / FixedDt);
		SubstepAccumulator -= NumSteps * FixedDt;

		// world移動を各サブステップへ分配。RawElapsed 基準にし、ステップ未消費(NumSteps==0)でも取りこぼし/二重適用しない。
		const float MoveFrac = FixedDt / RawElapsed;
		// PreSkelCompTransform を「消費(NumSteps*FixedDt)＋破棄(DroppedTime)」割合だけ前進させ残りを繰り越す。破棄分は復活させない（超過分破棄と整合）。
		PreSkelCompTransformConsumeFraction =
			FMath::Clamp((NumSteps * FixedDt + DroppedTime) / RawElapsed, 0.0f, 1.0f);
		const FVector FullSkelCompMove = SkelCompMoveVector;
		const FQuat FullSkelCompRot = SkelCompMoveRotation;

		bInSubstep = true;
		StepDeltaTime = FixedDt;
		DeltaTimeOld = FixedDt;
		for (int32 SubstepIndex = 0; SubstepIndex < NumSteps; ++SubstepIndex)
		{
			// 注: ローカル名は基底クラスのメンバ Alpha（ブレンド係数）を隠さないよう SubstepAlpha とする
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
		if (Bone.bInterBoneDummy && bBoneSubdivisionCollisionOnly)
		{
			continue;
		}

		// bridge dummyは常にSimulate()をスキップ（縦親が無くModifyBones[ParentIndex]参照でクラッシュする）
		if (Bone.bBridgeDummy)
		{
			continue;
		}

		Simulate(Bone, Scene, ComponentTransform, Exponent, SkelComp, Output);
	}

	// コリジョン専用モード: 全実ボーンのシミュレーション完了後、シミュレーション済みのLocation間にダミーを配置
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
	// bBoneSubdivisionCollisionOnlyに依らず常に実行。縦dummyの後（大きいindex）に走るため端点は配置済み。
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

			// LOD安全: 実ボーン端点がLODでカルされていればproxyを無効化し後続コリジョンループからも除外（stale位置で誤判定しない）。
			// 次フレーム冒頭のダミー配置ループで bSkipSimulate=false に戻り再評価される。
			if ((!EndA.bDummy && EndA.BoneRef.BoneIndex >= 0 && EndA.BoneRef.GetCompactPoseIndex(BoneContainer) < 0) ||
				(!EndB.bDummy && EndB.BoneRef.BoneIndex >= 0 && EndB.BoneRef.GetCompactPoseIndex(BoneContainer) < 0))
			{
				Bone.bSkipSimulate = true;
				continue;
			}

			Bone.PrevLocation = Bone.Location;
			Bone.Location = FMath::Lerp(EndA.Location, EndB.Location, Bone.InterBoneAlpha);
			// LERP基準位置をPoseLocationに退避（押し出し量 = Location - PoseLocation を測るため。bridge dummyのPoseLocationは他で未使用）。
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
	PrepareCollisionShapeCaches();

	TOptional<FKawaiiWorldCollisionQuerySettings> WorldCollisionQuerySettings;
	if (bAllowWorldCollision && SkelComp)
	{
		WorldCollisionQuerySettings.Emplace();
		if (bIgnoreSelfComponent)
		{
			WorldCollisionQuerySettings->Params.AddIgnoredComponent(SkelComp);
		}

		WorldCollisionQuerySettings->TraceChannel = bOverrideCollisionParams
			                                            ? CollisionChannelSettings.GetObjectType()
			                                            : SkelComp->GetCollisionObjectType();
		WorldCollisionQuerySettings->ResponseParams = bOverrideCollisionParams
			                                               ? FCollisionResponseParams(
				                                               CollisionChannelSettings.GetResponseToChannels())
			                                               : FCollisionResponseParams(
				                                               SkelComp->GetCollisionResponseToChannels());
	}

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

		// 共有コリジョン（他の KawaiiPhysics ノードから）
		if (bUseSharedCollision && !bSharedCollisionSource)
		{
			AdjustBySphereCollision(Bone, SharedSphericalLimits);
			AdjustByCapsuleCollision(Bone, SharedCapsuleLimits);
			AdjustByBoxCollision(Bone, SharedBoxLimits);
			AdjustByPlanerCollision(Bone, SharedPlanarLimits);
		}

		if (bAllowWorldCollision)
		{
			if (WorldCollisionQuerySettings.IsSet())
			{
				AdjustByWorldCollision(Output, Bone, SkelComp, WorldCollisionQuerySettings.GetValue());
			}
			++NumWorldChecks; // 発行したワールドスイープ回数
		}
	}
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumWorldCollisionChecks, NumWorldChecks);

	// bridge dummy のコリジョン変位を端点ボーンへ転送（実ボーンを押し出すフィードバック本体）。コリジョン後・Constraint/length復元前。
	// Push = Location(押し出し後) - PoseLocation(LERP基準)。端点へ距離比 (1-α):α で配分し Scale で強さ調整。端点が縦dummyでも後段length復元で実子へ伝播。
	if (BoneConstraintSubdivisionCount > 0 && BoneConstraintSubdivisionFeedbackScale > 0.0f)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_BridgeDummy);
		// 端点ごとに押し出し量と重みを集計し divisor=max(1,重み合計) で割る。多数のdummyが同じ端点を押す病的ケースのみ加重平均でN倍オーバーシュート/発振を防ぐ。
		// スクラッチ配列は端点index直アクセス。Reset+SetNumZeroed で再利用し TMap 確保/ハッシュをホットパスから排除。
		const int32 NumBones = ModifyBones.Num();
		BridgeFeedbackPushScratch.Reset();
		BridgeFeedbackPushScratch.SetNumZeroed(NumBones);
		BridgeFeedbackWeightScratch.Reset();
		BridgeFeedbackWeightScratch.SetNumZeroed(NumBones);

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

			const FVector Push = Bone.Location - Bone.PoseLocation; // コリジョンによる押し出し量
			if (Push.IsNearlyZero())
			{
				continue;
			}

			const float A = Bone.InterBoneAlpha;
			const int32 E1 = Bone.InterBoneRealParentIndex;
			const int32 E2 = Bone.InterBoneRealChildIndex;
			const float W1 = 1.0f - A; // 端点1に近いほど寄与大
			const float W2 = A;

			BridgeFeedbackPushScratch[E1] += Push * W1;
			BridgeFeedbackWeightScratch[E1] += W1;
			BridgeFeedbackPushScratch[E2] += Push * W2;
			BridgeFeedbackWeightScratch[E2] += W2;
		}

		for (int32 EndpointIdx = 0; EndpointIdx < NumBones; ++EndpointIdx)
		{
			const float W = BridgeFeedbackWeightScratch[EndpointIdx];
			if (W > 0.0f)
			{
				ModifyBones[EndpointIdx].Location +=
					(BridgeFeedbackPushScratch[EndpointIdx] / FMath::Max(1.0f, W)) * BoneConstraintSubdivisionFeedbackScale;
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
}

FTransform FAnimNode_KawaiiPhysics::ResolveExternalForceBoneTransform(
	FComponentSpacePoseContext& Output, const FKawaiiPhysicsModifyBone& Bone,
	const FKawaiiPhysicsModifyBone& ParentBone) const
{
	// dummyは実親ボーンのTransformを使用（親が別dummyでBoneRef空のときのクラッシュ防止）。
	// 非dummyもLODでcompact poseから外れると無効indexになるため両者ガードし、無効時は Identity。
	const FKawaiiPhysicsModifyBone& TransformBone =
		Bone.bDummy
			? (ModifyBones.IsValidIndex(Bone.InterBoneRealParentIndex)
				   ? ModifyBones[Bone.InterBoneRealParentIndex]
				   : ParentBone)
			: Bone;
	const FCompactPoseBoneIndex TransformCPI =
		TransformBone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer());
	if (TransformCPI >= 0)
	{
		return GetBoneTransformInSimSpace(Output, TransformCPI);
	}
	// 無効CompactPose(LOD等) → Identity
	return FTransform::Identity;
}

void FAnimNode_KawaiiPhysics::Simulate(FKawaiiPhysicsModifyBone& Bone, const FSceneInterface* Scene,
                                       const FTransform& ComponentTransform,
                                       const float& Exponent, const USkeletalMeshComponent* SkelComp,
                                       FComponentSpacePoseContext& Output)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_Simulate);

	const FKawaiiPhysicsModifyBone& ParentBone = ModifyBones[Bone.ParentIndex];

	// wind（Output依存）を計算。減衰後の速度に足し込む寄与として ComputeVerletStepVelocity へ渡す。
	FVector WindVelocity = FVector::ZeroVector;
	if (bEnableWind && Scene)
	{
		WindVelocity = GetWindVelocity(Output, Scene, Bone) * GetEffectiveTargetFramerate();
	}

	// このステップの速度を作る（速度再構成 → damping → +wind → gravity）。
	FVector Velocity = ComputeVerletStepVelocity(Bone, WindVelocity);

	// ユーザー外力に実速度を渡す（gravity の後・位置更新の前）。ApplyToVelocity が InOutVelocity を読む実装もあり得るため実速度に対して呼ぶ。
	// 注: Apply 内でユーザーコードが ExternalForces を変更しうるため、生ポインタを持ち回さず毎回 index で取得する。
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

	// 速度のぶんだけ位置を進める。
	IntegrateVerletStepPosition(Bone, Velocity);

	// Simple External Force（速度を経由しない位置オフセット）
	ApplySimpleExternalForce(Bone);

	// Follow World Movement
	if (SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace
		&& TeleportType != ETeleportType::TeleportPhysics)
	{
		// BaseBoneSpace は Output 依存の空間変換が必要なため、別関数に切り出さずここで実行。
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
		// ComponentSpace / WorldSpace（BaseBoneSpace 以外）
		ApplyWorldMoveFollowNonBaseBone(Bone);
	}

	// External Force
	// 注: foreach を使うと問題が起きうる（ranged-for 中に配列が変化する）。ユーザーの Apply が配列を変更しても
	// 安全なよう index ループのまま。BoneTM だけはボーン内で不変なので初回のみ解決して使い回す。
	FTransform BoneTM;
	bool bBoneTMResolved = false;
	for (int i = 0; i < CustomExternalForces.Num(); ++i)
	{
		if (CustomExternalForces[i] && CustomExternalForces[i]->bIsEnabled)
		{
			if (!bBoneTMResolved)
			{
				BoneTM = ResolveExternalForceBoneTransform(Output, Bone, ParentBone);
				bBoneTMResolved = true;
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
					if (!bBoneTMResolved)
					{
						BoneTM = ResolveExternalForceBoneTransform(Output, Bone, ParentBone);
						bBoneTMResolved = true;
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

	// Pull to Pose Location（剛性）
	ApplyStiffnessPull(Bone, ParentBone, Exponent);
}

// ============================================================================
//  物理計算の各ステップ（引数に FComponentSpacePoseContext を取らない）。Simulate() から呼ばれる。
//  注: ここを変更したら、Simulate() 内の wind/ApplyToVelocity の呼び出し位置との整合も確認すること。
// ============================================================================

FVector FAnimNode_KawaiiPhysics::ComputeVerletStepVelocity(FKawaiiPhysicsModifyBone& Bone,
                                                     const FVector& WindVelocity)
{
	// 速度は前ステップ変位を DeltaTimeOld で割って再構成。固定サブステップ時 DeltaTimeOld=FixedDt。
	FVector Velocity = (Bone.Location - Bone.PrevLocation) / FMath::Max(DeltaTimeOld, KINDA_SMALL_NUMBER);
	Bone.PrevLocation = Bone.Location;

	// 毎ステップ生の damping 係数。固定サブステップ経路ではStepDeltaTime一定によりフレームレート依存が解消される。
	// legacy(非サブステップ)経路は後方互換目的で意図的に dt 非正規化のまま（フレームレート依存が残る）。
	Velocity *= (1.0f - Bone.PhysicsSettings.Damping);

	// wind（呼び出し元で計算済み）
	Velocity += WindVelocity;

	// Gravity（wind の直後に適用。位置項を分けることで legacy 互換を保つ）
	const float StepDt = GetStepDeltaTime();
	if (!bUseLegacyGravity)
	{
		// AnimDynamics 風: 加速度を速度へ積分
		Velocity += GravityInSimSpace * StepDt;
	}
	else
	{
		// Legacy gravity: 0.5 * g * dt^2 を位置へ加算
		Bone.Location += 0.5 * GravityInSimSpace * StepDt * StepDt;
	}

	return Velocity;
}

void FAnimNode_KawaiiPhysics::IntegrateVerletStepPosition(FKawaiiPhysicsModifyBone& Bone, const FVector& Velocity)
{
	// 速度から位置を積分
	Bone.Location += Velocity * GetStepDeltaTime();
}

void FAnimNode_KawaiiPhysics::ApplySimpleExternalForce(FKawaiiPhysicsModifyBone& Bone)
{
	// Simple External Force（速度を経由しない位置オフセット。SimulateModifyBones でキャッシュ済み）。
	// world-move / stiffness と同じ「位置空間の後処理」なので Verlet ステップから分離している。
	if (!SimpleExternalForceInSimSpace.IsNearlyZero())
	{
		Bone.Location += SimpleExternalForceInSimSpace * GetStepDeltaTime();
	}
}

void FAnimNode_KawaiiPhysics::ApplyWorldMoveFollowNonBaseBone(FKawaiiPhysicsModifyBone& Bone)
{
	// Follow World Movement（ComponentSpace/WorldSpace のみ。BaseBoneSpaceは Simulate() で別処理）
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
	// ポーズ位置へ引き戻す
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
	
	Scene->GetWindParameters(
		ConvertSimulationSpaceLocation(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace,
		                               Bone.PoseLocation),
		WindDirection, WindSpeed, WindMinGust, WindMaxGust);

	WindDirection =
		ConvertSimulationSpaceVector(Output, EKawaiiPhysicsSimulationSpace::WorldSpace, SimulationSpace, WindDirection);

	// 乱数(gust/cone)はフレーム頭で1回だけサンプルし、サブステップ間で同一値を使う（NumStepsに依存しない＝フレームレート非依存）
	const uint64 CurrentFrame = GFrameCounter;
	if (CachedWindNoiseFrame != CurrentFrame)
	{
		CachedWindNoiseFrame = CurrentFrame;
		// gust: AnimDynamics由来の[0,2)倍率
		CachedWindGustFactor = FMath::FRandRange(0.0f, 2.0f);
		// cone: NoiseAngle内のランダム回転（任意軸×ランダム角）。風向きに適用してノイズを与える
		CachedWindNoiseRotation = (WindDirectionNoiseAngle > 0)
			                          ? FQuat(FMath::VRand(),
			                                  FMath::FRandRange(0.0f, FMath::DegreesToRadians(WindDirectionNoiseAngle)))
			                          : FQuat::Identity;
	}

	if (WindDirectionNoiseAngle > 0)
	{
		WindDirection = CachedWindNoiseRotation.RotateVector(WindDirection);
	}

	const FVector WindVelocity = WindDirection * WindSpeed * WindScale * CachedWindGustFactor;

	return WindVelocity;
}

void FAnimNode_KawaiiPhysics::WarmUp(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                     FTransform& ComponentTransform)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_WarmUp);

	// サブステップ有効時は各反復がちょうど1固定ステップになるよう DeltaTime=FixedDt とし、SubstepAccumulator（次フレームへ繰り越す端数時間）を退避（warmup分を本来のフレームの時間蓄積に混ぜない）。
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

	OutBoneTransforms.Reserve(OutBoneTransforms.Num() + ModifyBones.Num());

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

	// FCSPose<PoseType>::LocalBlendCSBoneTransforms 内のチェック用
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

		if (SimulationBaseBone.IsValidToEvaluate(Output.Pose.GetPose().GetBoneContainer()))
		{
			const FCompactPoseBoneIndex BaseBoneIndex =
				SimulationBaseBone.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer());
			// LODでbase boneがcompact poseから外れるとindexが無効化するためガード（他箇所と同形、無効時はIdentityのまま）
			if (BaseBoneIndex >= 0)
			{
				Cache.TargetSpaceToComponent =
					Output.Pose.GetComponentSpaceTransform(BaseBoneIndex); // Base -> Component
				Cache.ComponentToTargetSpace = Cache.TargetSpaceToComponent.Inverse(); // Component -> Base
			}
		}
		break;
	}

	return Cache;
}
