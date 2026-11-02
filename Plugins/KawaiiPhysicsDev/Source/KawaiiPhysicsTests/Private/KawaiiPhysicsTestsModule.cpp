// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Modules/ModuleManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "KawaiiPhysicsTestGameplayTags.h"
#endif

class FKawaiiPhysicsTestsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_DEV_AUTOMATION_TESTS
		// Editor 種別モジュールで FNativeGameplayTag を定義すると ensure が出るため、従来 API で起動時に登録する。
		// LoadingPhase=Default は PostEngineInit の DoneAddingNativeTags より前なので、5.7 以前の登録時 ensure も避けられる。
		FKawaiiPhysicsTestGameplayTag::RegisterAllTestTags();
#endif
	}
};

IMPLEMENT_MODULE(FKawaiiPhysicsTestsModule, KawaiiPhysicsTests)
