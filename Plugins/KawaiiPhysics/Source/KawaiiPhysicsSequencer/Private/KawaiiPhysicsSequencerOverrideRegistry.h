// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsTypes.h"

class UMovieSceneSection;
class USkeletalMeshComponent;

// Component 1 つ分の駆動オーバーライド。SectionData / PreAnimated トークン / レジストリで TSharedRef 共有
struct KAWAIIPHYSICSSEQUENCER_API FKawaiiPhysicsSequencerOverrideEntry
{
	TWeakObjectPtr<USkeletalMeshComponent> Component;
	FKawaiiPhysicsTransientForceHandle Handle;
	FGameplayTagContainer FilterTags;
	bool bFilterExactMatch = false;
	float BlendOutTime = 0.2f;
	bool bStopped = false;
	bool bPreAnimatedSaved = false;

	void Stop();
};

// エディタでのセクション/トラック削除など、TearDown も PreAnimated 復元も来ない経路（親サブシーケンスの ForceKeepState 下など）の保険。
// GameThread 限定で使用する。
class KAWAIIPHYSICSSEQUENCER_API FKawaiiPhysicsSequencerOverrideRegistry
{
public:
	static FKawaiiPhysicsSequencerOverrideRegistry& Get();

	void Register(const UMovieSceneSection* Section, const TSharedRef<FKawaiiPhysicsSequencerOverrideEntry>& Entry);
	void StopForSection(const UMovieSceneSection* Section);

#if WITH_DEV_AUTOMATION_TESTS
	int32 CountEntriesForSectionForTesting(const UMovieSceneSection* Section) const;
#endif

private:
	TMultiMap<TWeakObjectPtr<const UMovieSceneSection>, TWeakPtr<FKawaiiPhysicsSequencerOverrideEntry>> Entries;
};
