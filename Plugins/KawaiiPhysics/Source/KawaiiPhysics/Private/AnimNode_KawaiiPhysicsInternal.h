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
