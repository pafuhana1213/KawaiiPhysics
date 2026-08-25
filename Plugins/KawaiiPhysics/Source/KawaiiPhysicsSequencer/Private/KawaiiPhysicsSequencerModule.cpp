// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSequencerModule.h"

#include "KawaiiPhysicsSequencerOverrideRegistry.h"
#include "Modules/ModuleManager.h"

void FKawaiiPhysicsSequencerModule::StartupModule()
{
	FKawaiiPhysicsSequencerOverrideRegistry::Get().RegisterDelegates();
}

void FKawaiiPhysicsSequencerModule::ShutdownModule()
{
	// 無期限リースの Entry がモジュール終了後に残らないよう先に全停止
	FKawaiiPhysicsSequencerOverrideRegistry::Get().StopAll();
	FKawaiiPhysicsSequencerOverrideRegistry::Get().UnregisterDelegates();
}

IMPLEMENT_MODULE(FKawaiiPhysicsSequencerModule, KawaiiPhysicsSequencer)
