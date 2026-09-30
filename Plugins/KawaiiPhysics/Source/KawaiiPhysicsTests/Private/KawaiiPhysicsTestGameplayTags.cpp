// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsTestGameplayTags.h"

#include "Containers/Array.h"
#include "GameplayTagsManager.h"

namespace
{
	TArray<const TCHAR*>& GetTestTagNames()
	{
		static TArray<const TCHAR*> TagNames;
		return TagNames;
	}
}

FKawaiiPhysicsTestGameplayTag::FKawaiiPhysicsTestGameplayTag(const TCHAR* InTagName)
	: TagName(InTagName)
{
	GetTestTagNames().Add(TagName);
}

FGameplayTag FKawaiiPhysicsTestGameplayTag::GetTag() const
{
	return FGameplayTag::RequestGameplayTag(FName(TagName));
}

void FKawaiiPhysicsTestGameplayTag::RegisterAllTestTags()
{
	UGameplayTagsManager& TagsManager = UGameplayTagsManager::Get();
	for (const TCHAR* Name : GetTestTagNames())
	{
		const FName TagNameToRegister(Name);
		// ホットリロードで StartupModule が再実行されたときは初回登録分が残っている。
		// 5.7 以前は登録完了後の AddNativeGameplayTag が ensure になるため、既存のタグは登録し直さない。
		if (TagsManager.RequestGameplayTag(TagNameToRegister, false).IsValid())
		{
			continue;
		}
		TagsManager.AddNativeGameplayTag(TagNameToRegister, TEXT("KawaiiPhysics automation test tag"));
	}
}

#endif
