// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

// Shared internal declarations for the split AnimNode_KawaiiPhysics implementation files.
// Stat IDs are DEFINE_STAT'd once in AnimNode_KawaiiPhysics.cpp and referenced here via *_EXTERN.
// NOTE: include this header AFTER "AnimNode_KawaiiPhysics.h" (relies on engine stat headers / STATGROUP_Anim).

#include "Stats/Stats.h"

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
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdatePlanerLimit"), STAT_KawaiiPhysics_UpdatePlanerLimit, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_WarmUp"), STAT_KawaiiPhysics_WarmUp, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdatePhysicsSetting"), STAT_KawaiiPhysics_UpdatePhysicsSetting, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_UpdateCapsuleLimit"), STAT_KawaiiPhysics_UpdateCapsuleLimit, STATGROUP_Anim, KAWAIIPHYSICS_API);
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
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumModifyBones"), STAT_KawaiiPhysics_NumModifyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumInterBoneDummyBones"), STAT_KawaiiPhysics_NumInterBoneDummyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumBridgeDummyBones"), STAT_KawaiiPhysics_NumBridgeDummyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_InsertInterBoneDummyBones"), STAT_KawaiiPhysics_InsertInterBoneDummyBones, STATGROUP_Anim, KAWAIIPHYSICS_API);
// bridge dummyの毎フレーム処理（配置LERP + コリジョン変位転送）の合計時間 / Per-frame bridge dummy cost (placement LERP + collision displacement transfer)
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_BridgeDummy"), STAT_KawaiiPhysics_BridgeDummy, STATGROUP_Anim, KAWAIIPHYSICS_API);

// 角度制限 + 平面制約 + ボーン長復元のO(N)ループ（従来SimulateModifyBonesに埋没） / Angle limit + planar constraint + bone-length restore O(N) loop (was hidden in SimulateModifyBones)
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_AdjustByLimitsAndLength"), STAT_KawaiiPhysics_AdjustByLimitsAndLength, STATGROUP_Anim, KAWAIIPHYSICS_API);
// PreUpdate(GameThread)全体のコスト / Total cost of PreUpdate on the GameThread
DECLARE_CYCLE_STAT_EXTERN(TEXT("KawaiiPhysics_PreUpdate_GameThread"), STAT_KawaiiPhysics_PreUpdate, STATGROUP_Anim, KAWAIIPHYSICS_API);

// 入力規模カウンタ（負荷=N×L等の相関用） / Input-size counters (correlate load = N×L, etc.)
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumSphereColliders"), STAT_KawaiiPhysics_NumSphereColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumCapsuleColliders"), STAT_KawaiiPhysics_NumCapsuleColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumBoxColliders"), STAT_KawaiiPhysics_NumBoxColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumPlanarColliders"), STAT_KawaiiPhysics_NumPlanarColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumSharedColliders"), STAT_KawaiiPhysics_NumSharedColliders, STATGROUP_Anim, KAWAIIPHYSICS_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumMergedBoneConstraints"), STAT_KawaiiPhysics_NumMergedBoneConstraints, STATGROUP_Anim, KAWAIIPHYSICS_API);
// 毎フレームに発行したワールドコリジョンのスイープ回数（anim threadからの同期トレース） / World-collision sweeps issued per frame (sync traces from the anim thread)
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("KawaiiPhysics_NumWorldCollisionChecks"), STAT_KawaiiPhysics_NumWorldCollisionChecks, STATGROUP_Anim, KAWAIIPHYSICS_API);

// ModifyBones / MergedBoneConstraints のアロケーション量（subdivision/bridge dummyによる膨張の可視化） / Allocated size of ModifyBones / MergedBoneConstraints (visualize growth from subdivision/bridge dummies)
DECLARE_MEMORY_STAT_EXTERN(TEXT("KawaiiPhysics_ModifyBonesMemory"), STAT_KawaiiPhysics_ModifyBonesMemory, STATGROUP_Anim, KAWAIIPHYSICS_API);
