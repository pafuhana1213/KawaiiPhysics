// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsTypes.h"

class UMovieSceneSection;
class UMovieSceneTrack;
class USkeletalMeshComponent;
class UWorld;

// Component 1 つ分の駆動オーバーライド。SectionData / PreAnimated トークン / レジストリで TSharedRef 共有
struct KAWAIIPHYSICSSEQUENCER_API FKawaiiPhysicsSequencerOverrideEntry
{
	TWeakObjectPtr<USkeletalMeshComponent> Component;
	TWeakPtr<uint8> Owner;
	FKawaiiPhysicsTransientForceHandle Handle;
	FGameplayTagContainer FilterTags;
	bool bFilterExactMatch = false;
	float BlendOutTime = 0.2f;
	bool bStopped = false;

	void Stop(float OverrideBlendOutTime = -1.0f);
};

// エディタでのセクション/トラック削除など、TearDown も PreAnimated 復元も来ない経路（親サブシーケンスの ForceKeepState 下など）の保険。
// Owner 限定オーバーロードは PreAnimated 復元を評価インスタンス単位に絞り、1 引数版はセクション破棄用に全 Entry を停止する。
// Component まで指定する版は PreAnimated の object 単位復元用（復元対象の Component の Entry だけ止める）。
// GameThread 限定で使用する。
class KAWAIIPHYSICSSEQUENCER_API FKawaiiPhysicsSequencerOverrideRegistry
{
public:
	static FKawaiiPhysicsSequencerOverrideRegistry& Get();

	void Register(const UMovieSceneSection* Section, const TSharedRef<FKawaiiPhysicsSequencerOverrideEntry>& Entry);
	void StopForSection(const UMovieSceneSection* Section);
	void StopForSection(const UMovieSceneSection* Section, const TWeakPtr<uint8>& OwnerFilter);
	void StopForSection(const UMovieSceneSection* Section, const TWeakPtr<uint8>& OwnerFilter, const USkeletalMeshComponent* ComponentFilter);
	void StopForWorld(const UWorld* World);
	void StopForSectionsNotIn(const UMovieSceneTrack* Track, TConstArrayView<UMovieSceneSection*> LiveSections);
	void PruneInvalidEntries();
	// モジュール終了・アンロード時の最終保険。全 Entry を即時停止して除去する
	void StopAll();
	void RegisterDelegates();
	void UnregisterDelegates();

#if WITH_DEV_AUTOMATION_TESTS
	int32 CountEntriesForSectionForTesting(const UMovieSceneSection* Section) const;
#endif

private:
	void StopForSectionInternal(const UMovieSceneSection* Section, const TWeakPtr<uint8>* OptionalOwnerFilter, const USkeletalMeshComponent* OptionalComponentFilter);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void HandlePostGarbageCollect();

	TMultiMap<TWeakObjectPtr<const UMovieSceneSection>, TWeakPtr<FKawaiiPhysicsSequencerOverrideEntry>> Entries;
	FDelegateHandle WorldCleanupDelegateHandle;
	FDelegateHandle PostGarbageCollectDelegateHandle;
};
