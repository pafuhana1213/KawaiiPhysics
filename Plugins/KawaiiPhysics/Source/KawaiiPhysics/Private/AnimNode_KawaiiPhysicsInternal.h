// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

// Shared internal declarations for the split AnimNode_KawaiiPhysics implementation files.
// Stat IDs are DEFINE_STAT'd once in AnimNode_KawaiiPhysics.cpp and referenced here via *_EXTERN.
// NOTE: include this header AFTER "AnimNode_KawaiiPhysics.h" (relies on engine stat headers / STATGROUP_Anim).

#include "Stats/Stats.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "AnimNode_KawaiiPhysics.h" // KAWAIIPHYSICS_CSV_STATS

// PREPROCESSOR_JOIN は 5.8 で非推奨、UE_JOIN は 5.3 に無いため、版を問わない連結マクロを持つ
#define KAWAIIPHYSICS_JOIN_INNER(A, B) A##B
#define KAWAIIPHYSICS_JOIN(A, B) KAWAIIPHYSICS_JOIN_INNER(A, B)

CSV_DECLARE_CATEGORY_MODULE_EXTERN(KAWAIIPHYSICS_API, KawaiiPhysics);

// 既存の SCOPE_CYCLE_COUNTER が同じ範囲の CPU trace を出すか。STATS も ENABLE_STATNAMEDEVENTS も無い構成（Test 等）では
// -statnamedevents で GCycleStatsShouldEmitNamedEvents が立っても STAT はイベントを出さないので、独自イベントを抑止しない
#if STATS || ENABLE_STATNAMEDEVENTS
#define KAWAIIPHYSICS_STAT_EMITS_TRACE (GCycleStatsShouldEmitNamedEvents != 0)
#else
#define KAWAIIPHYSICS_STAT_EMITS_TRACE false
#endif

#if KAWAIIPHYSICS_CSV_STATS
#include <atomic>

// CSV を記録中か。CSV プロファイラの最初のフレーム・終了デリゲート（GameThread）で更新し、Evaluate（AnyThread）から読む。
// FCsvProfiler::IsCapturing は GameThread 専用で、PreUpdate を通らない評価もあるため、モジュール共通の状態を持つ
extern std::atomic<bool> GKawaiiPhysicsCsvCapturing;

#define KAWAIIPHYSICS_SCOPE_TIMING(StatName) \
	CSV_SCOPED_TIMING_STAT(KawaiiPhysics, StatName) \
	TRACE_CPUPROFILER_EVENT_SCOPE(StatName)
#define KAWAIIPHYSICS_SCOPE_TIMING_WITH_STAT(StatName) \
	CSV_SCOPED_TIMING_STAT(KawaiiPhysics, StatName) \
	TRACE_CPUPROFILER_EVENT_DECLARE(KAWAIIPHYSICS_JOIN(KAWAIIPHYSICS_JOIN(KawaiiPhysicsTraceDecl_, StatName), __LINE__)); \
	TRACE_CPUPROFILER_EVENT_SCOPE_USE(KAWAIIPHYSICS_JOIN(KAWAIIPHYSICS_JOIN(KawaiiPhysicsTraceDecl_, StatName), __LINE__), TEXT(#StatName), KAWAIIPHYSICS_JOIN(KAWAIIPHYSICS_JOIN(KawaiiPhysicsTraceScope_, StatName), __LINE__), !KAWAIIPHYSICS_STAT_EMITS_TRACE)
#define KAWAIIPHYSICS_CSV_ACCUMULATE(StatName, Value) \
	CSV_CUSTOM_STAT(KawaiiPhysics, StatName, Value, ECsvCustomStatOp::Accumulate)
#else
#define KAWAIIPHYSICS_SCOPE_TIMING(StatName)
#define KAWAIIPHYSICS_SCOPE_TIMING_WITH_STAT(StatName)
#define KAWAIIPHYSICS_CSV_ACCUMULATE(StatName, Value)
#endif

DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_InitModifyBones"), STAT_KawaiiPhysics_InitModifyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_Eval"), STAT_KawaiiPhysics_Eval, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_SimulateModifyBones"), STAT_KawaiiPhysics_SimulateModifyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_Simulate"), STAT_KawaiiPhysics_Simulate, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_GetWindVelocity"), STAT_KawaiiPhysics_GetWindVelocity, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_WorldCollision"), STAT_KawaiiPhysics_WorldCollision, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_InitSyncBone"), STAT_KawaiiPhysics_InitSyncBone, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_ApplySyncBone"), STAT_KawaiiPhysics_ApplySyncBone, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_AdjustByCollision"), STAT_KawaiiPhysics_AdjustByCollision, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_AdjustByBoneConstraint"), STAT_KawaiiPhysics_AdjustByBoneConstraint, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdateSphericalLimit"), STAT_KawaiiPhysics_UpdateSphericalLimit, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdatePlanarLimit"), STAT_KawaiiPhysics_UpdatePlanarLimit, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_WarmUp"), STAT_KawaiiPhysics_WarmUp, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdatePhysicsSetting"), STAT_KawaiiPhysics_UpdatePhysicsSetting, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdateCapsuleLimit"), STAT_KawaiiPhysics_UpdateCapsuleLimit, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdateTaperedCapsuleLimit"), STAT_KawaiiPhysics_UpdateTaperedCapsuleLimit, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdateBoxLimit"), STAT_KawaiiPhysics_UpdateBoxLimit, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdateModifyBonesPoseTransform"), STAT_KawaiiPhysics_UpdateModifyBonesPoseTransform, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_ApplySimulateResult"), STAT_KawaiiPhysics_ApplySimulateResult, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_ConvertSimulationSpaceTransform"), STAT_KawaiiPhysics_ConvertSimulationSpaceTransform, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_ConvertSimulationSpaceVector"), STAT_KawaiiPhysics_ConvertSimulationSpaceVector, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_ConvertSimulationSpaceLocation"), STAT_KawaiiPhysics_ConvertSimulationSpaceLocation, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_ConvertSimulationSpaceRotation"), STAT_KawaiiPhysics_ConvertSimulationSpaceRotation, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_ConvertSimulationSpace"), STAT_KawaiiPhysics_ConvertSimulationSpace, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_InitializeSharedCollision"), STAT_KawaiiPhysics_InitializeSharedCollision, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_WriteSharedCollisionToSubsystem"), STAT_KawaiiPhysics_WriteSharedCollisionToSubsystem, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdateSharedCollisionLimits"), STAT_KawaiiPhysics_UpdateSharedCollisionLimits, STATGROUP_Anim, KAWAIIPHYSICS_API);
// ノード側のシンプルワールドコリジョン読み取り（UpdateSimpleWorldCollisionLimits）専用 / Node-side Simple World Collision read (UpdateSimpleWorldCollisionLimits)
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdateSimpleWorldCollisionLimits"), STAT_KawaiiPhysics_UpdateSimpleWorldCollisionLimits, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumModifyBones"), STAT_KawaiiPhysics_NumModifyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumInterBoneDummyBones"), STAT_KawaiiPhysics_NumInterBoneDummyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumBridgeDummyBones"), STAT_KawaiiPhysics_NumBridgeDummyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_InsertInterBoneDummyBones"), STAT_KawaiiPhysics_InsertInterBoneDummyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
// bridge dummyの毎フレーム処理（配置LERP + コリジョン変位転送）の合計時間 / Per-frame bridge dummy cost (placement LERP + collision displacement transfer)
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_BridgeDummy"), STAT_KawaiiPhysics_BridgeDummy, STATGROUP_Anim, KAWAIIPHYSICS_API);

// 角度制限 + 平面制約 + ボーン長復元のO(N)ループ（従来SimulateModifyBonesに埋没） / Angle limit + planar constraint + bone-length restore O(N) loop (was hidden in SimulateModifyBones)
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_AdjustByLimitsAndLength"), STAT_KawaiiPhysics_AdjustByLimitsAndLength, STATGROUP_Anim, KAWAIIPHYSICS_API);

// 入力規模カウンタ（負荷=N×L等の相関用） / Input-size counters (correlate load = N×L, etc.)
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumSphereColliders"), STAT_KawaiiPhysics_NumSphereColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumCapsuleColliders"), STAT_KawaiiPhysics_NumCapsuleColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumTaperedCapsuleColliders"), STAT_KawaiiPhysics_NumTaperedCapsuleColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumBoxColliders"), STAT_KawaiiPhysics_NumBoxColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumPlanarColliders"), STAT_KawaiiPhysics_NumPlanarColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumSharedColliders"), STAT_KawaiiPhysics_NumSharedColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumSimpleWorldColliders"), STAT_KawaiiPhysics_NumSimpleWorldColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumMergedBoneConstraints"), STAT_KawaiiPhysics_NumMergedBoneConstraints, STATGROUP_Anim, KAWAIIPHYSICS_API);
// 毎フレームに発行したワールドコリジョンのスイープ回数（anim threadからの同期トレース） / World-collision sweeps issued per frame (sync traces from the anim thread)
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumWorldCollisionChecks"), STAT_KawaiiPhysics_NumWorldCollisionChecks, STATGROUP_Anim, KAWAIIPHYSICS_API);

// ModifyBones / MergedBoneConstraints のアロケーション量（subdivision/bridge dummyによる膨張の可視化） / Allocated size of ModifyBones / MergedBoneConstraints (visualize growth from subdivision/bridge dummies)
DECLARE_MEMORY_STAT_EXTERN(TEXT("KawaiiPhysics_ModifyBonesMemory"), STAT_KawaiiPhysics_ModifyBonesMemory, STATGROUP_Anim, KAWAIIPHYSICS_API);

KAWAIIPHYSICS_API int32 GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge();
KAWAIIPHYSICS_API int32 GetKawaiiPhysicsSharedPublisherAutoResolveInterval();

namespace KawaiiPhysicsSimpleWorldReadPath
{
	// 共有コリジョンデータをシミュレーション空間配列へ追加する / Appends shared collision data to simulation-space arrays
	KAWAIIPHYSICS_API void AppendSharedCollisionDataToSimulationSpace(
		const FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& Output,
		EKawaiiPhysicsSimulationSpace TargetSpace,
		const FKawaiiPhysicsSharedCollisionData& InData,
		TArray<FSphericalLimit>& OutSphericalLimits,
		TArray<FCapsuleLimit>& OutCapsuleLimits,
		TArray<FTaperedCapsuleLimit>& OutTaperedCapsuleLimits,
		TArray<FBoxLimit>& OutBoxLimits,
		TArray<FPlanarLimit>* OutPlanarLimits,
		TArray<FKawaiiPhysicsConvexLimit>* OutConvexLimits);

	// 既存配列の要素数が一致する場合だけ in-place でシミュレーション空間へ再変換する / Refreshes simulation-space arrays in place only when element counts match
	KAWAIIPHYSICS_API bool RefreshSimulationSpaceLimitsInPlace(
		const FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& Output,
		EKawaiiPhysicsSimulationSpace TargetSpace,
		const FKawaiiPhysicsSharedCollisionData& InData,
		TArray<FSphericalLimit>& OutSphericalLimits,
		TArray<FCapsuleLimit>& OutCapsuleLimits,
		TArray<FTaperedCapsuleLimit>& OutTaperedCapsuleLimits,
		TArray<FBoxLimit>& OutBoxLimits,
		TArray<FKawaiiPhysicsConvexLimit>& OutConvexLimits);

	// Box 配列だけを in-place でシミュレーション空間へ再変換する / Refreshes only box limits in simulation space in place
	KAWAIIPHYSICS_API bool RefreshSimulationSpaceLimitsInPlace(
		const FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& Output,
		EKawaiiPhysicsSimulationSpace TargetSpace,
		const TArray<FBoxLimit>& InBoxLimits,
		TArray<FBoxLimit>& OutBoxLimits);
}
