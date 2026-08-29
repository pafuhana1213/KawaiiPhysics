// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSequencerModule.h"

#include "KawaiiPhysicsSequencerMultiplierRegistry.h"
#include "Modules/ModuleManager.h"

void FKawaiiPhysicsSequencerModule::StartupModule()
{
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().RegisterDelegates();
}

void FKawaiiPhysicsSequencerModule::ShutdownModule()
{
	// 無期限リースの Entry がモジュール終了後に残らないよう先に全停止
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopAll();
	FKawaiiPhysicsSequencerMultiplierRegistry::Get().UnregisterDelegates();
}

IMPLEMENT_MODULE(FKawaiiPhysicsSequencerModule, KawaiiPhysicsSequencer)
