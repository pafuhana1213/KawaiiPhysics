// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameplayTagContainer.h"

struct FKawaiiPhysicsTestGameplayTag
{
	explicit FKawaiiPhysicsTestGameplayTag(const TCHAR* InTagName);

	FGameplayTag GetTag() const;
	operator FGameplayTag() const { return GetTag(); }

	static void RegisterAllTestTags();

private:
	const TCHAR* TagName;
};

#define KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TagVariable, TagName) \
	static const FKawaiiPhysicsTestGameplayTag TagVariable(TEXT(TagName))

#endif
