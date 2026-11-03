// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysics.h"
#include "Modules/ModuleManager.h"
#include "AnimNode_KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsInternal.h"

#define LOCTEXT_NAMESPACE "FKawaiiPhysicsModule"

namespace KawaiiPhysicsModulePrivate
{
#if KAWAIIPHYSICS_CSV_STATS
	static FDelegateHandle CsvProfileFirstFrameHandle;
	static FDelegateHandle CsvProfileEndHandle;
#endif
}

void FKawaiiPhysicsModule::StartupModule()
{
#if KAWAIIPHYSICS_CSV_STATS
	// CSV の記録中フラグを最初のフレーム・終了デリゲート（GameThread）で追う。Evaluate（AnyThread）は量の stat を数えるかをこれで決める
	FCsvProfiler* CsvProfiler = FCsvProfiler::Get();
	// OnCSVProfileStart は出力ファイルの作成に失敗しても先に呼ばれ、終了通知が来ないため、開始が成功した最初のフレームの通知を使う
	KawaiiPhysicsModulePrivate::CsvProfileFirstFrameHandle = CsvProfiler->OnCSVProfileFirstFrame().AddLambda([]()
	{
		GKawaiiPhysicsCsvCapturing.store(true, std::memory_order_relaxed);
	});
	KawaiiPhysicsModulePrivate::CsvProfileEndHandle = CsvProfiler->OnCSVProfileEnd().AddLambda([]()
	{
		GKawaiiPhysicsCsvCapturing.store(false, std::memory_order_relaxed);
	});
	// モジュールの読み込みより前に始まった記録（起動直後からの記録）に備え、現在の状態も取り込む
	GKawaiiPhysicsCsvCapturing.store(CsvProfiler->IsCapturing(), std::memory_order_relaxed);
#endif
}

void FKawaiiPhysicsModule::ShutdownModule()
{
#if KAWAIIPHYSICS_CSV_STATS
	FCsvProfiler* CsvProfiler = FCsvProfiler::Get();
	CsvProfiler->OnCSVProfileFirstFrame().Remove(KawaiiPhysicsModulePrivate::CsvProfileFirstFrameHandle);
	CsvProfiler->OnCSVProfileEnd().Remove(KawaiiPhysicsModulePrivate::CsvProfileEndHandle);
	KawaiiPhysicsModulePrivate::CsvProfileFirstFrameHandle.Reset();
	KawaiiPhysicsModulePrivate::CsvProfileEndHandle.Reset();
	GKawaiiPhysicsCsvCapturing.store(false, std::memory_order_relaxed);
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKawaiiPhysicsModule, KawaiiPhysics)
