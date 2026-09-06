// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysicsSharedPublisher.h"

#include "AnimNode_KawaiiPhysicsInternal.h"
#include "AnimNode_KawaiiPhysicsSharedPublisherInternal.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsWindPresetDataAsset.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_KawaiiPhysicsSharedPublisher)

namespace
{
	bool AreSimpleWorldSettingsEqual(
		const FKawaiiPhysicsSimpleWorldCollisionSettings& Lhs,
		const FKawaiiPhysicsSimpleWorldCollisionSettings& Rhs)
	{
		return Lhs.bEnabled == Rhs.bEnabled
			&& Lhs.GatherScope == Rhs.GatherScope
			&& Lhs.GatherInterval == Rhs.GatherInterval
			&& Lhs.ObjectTypes == Rhs.ObjectTypes
			&& Lhs.ConvexFallbackShape == Rhs.ConvexFallbackShape
			&& Lhs.bOverrideGatherRadius == Rhs.bOverrideGatherRadius
			&& Lhs.GatherRadius == Rhs.GatherRadius
			&& Lhs.bGroundCollision == Rhs.bGroundCollision
			&& Lhs.SkeletalMeshCollision == Rhs.SkeletalMeshCollision
			&& Lhs.bOverrideCollisionChannel == Rhs.bOverrideCollisionChannel
			&& Lhs.CollisionChannel == Rhs.CollisionChannel
			&& Lhs.bGatherFamilyMembers == Rhs.bGatherFamilyMembers;
	}

	FKawaiiPhysicsSharedPublishInputs MakePublishInputs(
		bool bEnabled,
		const FKawaiiPhysicsSimpleWorldCollisionSettings& SimpleWorld,
		bool bWindEnabled,
		float WindTimeScale,
		FKawaiiPhysics_ExternalForce_ProceduralWind* SharedWind = nullptr)
	{
		FKawaiiPhysicsSharedPublishInputs Inputs;
		Inputs.bEnabled = bEnabled;
		Inputs.SimpleWorld = SimpleWorld;
		Inputs.bWindEnabled = bWindEnabled;
		Inputs.WindTimeScale = WindTimeScale;
		Inputs.SharedWind = SharedWind;
		return Inputs;
	}
}

void FKawaiiPhysicsSharedPublishHelper::SetSourceID(uint64 InSourceID)
{
	SourceID = InSourceID;
}

void FKawaiiPhysicsSharedPublishHelper::SetDebugTag(FGameplayTag InDebugTag)
{
	DebugTag = InDebugTag;
}

void FKawaiiPhysicsSharedPublishHelper::SetEntries(
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> InPublisherEntry,
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> InSimpleWorldEntry,
	TWeakObjectPtr<const USkeletalMeshComponent> InSkelComp)
{
	PublisherEntry = MoveTemp(InPublisherEntry);
	SimpleWorldEntry = MoveTemp(InSimpleWorldEntry);
	SkelComp = InSkelComp;
	LastSentDesc.Reset();
	// Entry が変わると消費側も新しい serial で採用し直すので、LastPublishedState を「消費側が持っている値」として使わない
	bHasPublishedToCurrentEntry = false;
	bNeedsEntryReacquire = false;
#if !UE_BUILD_SHIPPING
	bProviderConflictWarningLogged = false;
#endif
}

void FKawaiiPhysicsSharedPublishHelper::ReleaseEntries()
{
	if (SimpleWorldEntry.IsValid())
	{
		SimpleWorldEntry->RemoveDesc(SourceID);
	}

	if (PublisherEntry.IsValid())
	{
		// 所有権確認と期限切れは同じロック区間で行う（別々だと claim の割り込みで新 provider の Entry を expire してしまう）
		PublisherEntry->MarkExpiredIfProvider(SourceID);
	}

	PublisherEntry.Reset();
	SimpleWorldEntry.Reset();
	SkelComp.Reset();
	LastSentDesc.Reset();
	// Entry を手放した後に古い累積を持ち越さない
	PendingDeltaTime = 0.0f;
	bHasPublishedToCurrentEntry = false;
	bNeedsEntryReacquire = false;
#if !UE_BUILD_SHIPPING
	bProviderConflictWarningLogged = false;
#endif
}

void FKawaiiPhysicsSharedPublishHelper::ResetEffectiveValues(const FKawaiiPhysicsSharedPublishInputs& Defaults)
{
	bEffectiveEnabled = Defaults.bEnabled;
	EffectiveSimpleWorldSettings = Defaults.SimpleWorld;
	LastInputs = Defaults;
	// PendingDeltaTime は触らない。同じ Entry を保持したままの reinit（Persona のプリセット変更等）では消費側が serial を持ったまま外挿を続けているので、停止区間の累積は再開時にそのまま追いつきへ使う（累積を捨てるのは Entry を手放す ReleaseEntries と Entry 無しの早期 return だけ）
}

// PreUpdate は枝の relevance に関係なく毎フレーム走るので、Update が飛んだフレームの時間はここに溜まる
void FKawaiiPhysicsSharedPublishHelper::AccumulatePendingDeltaTime(const float DeltaSeconds)
{
	PendingDeltaTime += FMath::Max(DeltaSeconds, 0.0f);
}

bool FKawaiiPhysicsSharedPublishHelper::ApplyInputChanges(const FKawaiiPhysicsSharedPublishInputs& Inputs)
{
	if (!LastInputs.IsSet())
	{
		LastInputs = Inputs;
		return false;
	}

	const FKawaiiPhysicsSharedPublishInputs& Previous = LastInputs.GetValue();
	bool bChanged = false;
	if (Previous.bEnabled != Inputs.bEnabled)
	{
		bEffectiveEnabled = Inputs.bEnabled;
		bChanged = true;
	}
	if (!AreSimpleWorldSettingsEqual(Previous.SimpleWorld, Inputs.SimpleWorld))
	{
		EffectiveSimpleWorldSettings = Inputs.SimpleWorld;
		bChanged = true;
	}

	LastInputs = Inputs;
	return bChanged;
}

void FKawaiiPhysicsSharedPublishHelper::UnregisterProviderDesc()
{
	if (SimpleWorldEntry.IsValid() && LastSentDesc.IsSet())
	{
		SimpleWorldEntry->RemoveDesc(SourceID);
	}
	LastSentDesc.Reset();
}

// 所有権を確定してから Desc を登録し Pending を消費する
//（拒否された Publisher が収集設定や BP 要求を汚染しないため）。
// 所有権が前フレームから自分にあるフレームは publish 前に Pending 消費と風の更新を済ませ、publish を 1 回で終わらせる。
// claim フレーム（provider 不在・期限切れからの取り直し）だけは受理後に消費し、変化があれば同フレームに publish し直す
// 所有権判定は ReadProviderSnapshot の 1 回読みで行う（別々の読み取りだと claim の割り込みで所有権を誤認する）。
bool FKawaiiPhysicsSharedPublishHelper::Update(
	const FKawaiiPhysicsSharedPublishInputs& Inputs,
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>& WindRuntimeState,
	float DeltaTime,
	uint64 CurrentFrame,
	uint64 ProviderMaxAgeFrames)
{
	if (!PublisherEntry.IsValid() || !SimpleWorldEntry.IsValid() || SourceID == 0)
	{
		// Entry を持たない間の累積は捨てる（取り直したときは消費側も新しい serial で採用し直すため）
		PendingDeltaTime = 0.0f;
		bNeedsEntryReacquire = true;
		return false;
	}

	// PreUpdate は枝が Update されないフレームも走るので、累積分の方が大きければそれを採用し、
	// 再開時に消費側の外挿へ追いつかせる（消費側が先に進んだ Time へ巻き戻さない）。
	// Update が毎フレーム走っていれば両者は同じ値になる
	const float EffectiveDeltaTime = FMath::Max(FMath::Max(DeltaTime, 0.0f), PendingDeltaTime);
	PendingDeltaTime = 0.0f;

	// クロックの前進は所有権判定より前に済ませる。
	// claim フレームは受理判定のために BuildDescAndState → PublishState を先に行うので、ここで進めておかないと
	// 最初に見える serial には前フレームの Time が載り、別 AnimBlueprint の消費側が並列評価でその中間 publish を読むと巻き戻る。
	// Pending 要求の消費は従来どおり受理後に行う（拒否された Publisher が BP 要求を消費しないため）。
	// 所有権に関係なく自分の SharedWind のクロックだけを進める
	//（別 provider に所有されていても自分の publish は行われず、消費側は他 provider の Time を読むので害は無い）。
	// 消費側は serial 未変化のフレームを、最後に publish した PublisherTimeScale / bPublisherWindEnabled
	//（BuildDescAndState が State.Wind へ入れる SharedWind->TimeScale と bEffectiveEnabled && SharedWind->bIsEnabled と同じ値）
	// で外挿する。別 AnimBlueprint の消費側は同じフレームで Publisher より先に評価されることがあるので、
	// Publisher も当フレーム分を同じ値で進めてから新しい要求を取り込み、新しい scale は次フレーム以降の外挿用として publish する。
	// こうすると評価順に関係なく publish 値 ≥ 消費側の外挿値になる（位相のポップ・突風エンベロープの巻き戻しを防ぐ）。
	// 停止中に UPROPERTY の TimeScale / Enabled が変わった場合も、ApplyInputChanges より前に進めるので同じ規則になる。
	// AdvanceWindTime は struct の現在 TimeScale を掛けてしまうので使わず、Time へ直接加算する
	//（Helper は Worker 上で SharedWind の唯一の書き手なので lock は要らない）
	if (Inputs.SharedWind && Inputs.SharedWind->RuntimeState.IsValid() && EffectiveDeltaTime > 0.0f)
	{
		const bool bCarriedEnabled = bHasPublishedToCurrentEntry
			? LastPublishedState.Wind.bPublisherWindEnabled
			: (Inputs.SharedWind->bIsEnabled && bEffectiveEnabled);
		const float CarriedTimeScale = bHasPublishedToCurrentEntry
			? LastPublishedState.Wind.PublisherTimeScale
			: Inputs.SharedWind->TimeScale;
		if (bCarriedEnabled)
		{
			Inputs.SharedWind->RuntimeState->Time += EffectiveDeltaTime * CarriedTimeScale;
		}
	}

	// 所有権を先に確認する。生存中の別 provider がいる間は Desc も Pending も触らない。
	// ロックは PublisherEntry / SimpleWorldEntry を 1 つずつ取り、同時に 2 つ以上保持しない。
	// 所有権と期限切れは同じロック区間のスナップショットで判定する（別々に読むと claim の割り込みで旧 provider が自分を生存 provider と誤認する）。
	const FKawaiiPhysicsSharedPublisherEntry::FProviderSnapshot Snapshot =
		PublisherEntry->ReadProviderSnapshot(CurrentFrame, ProviderMaxAgeFrames);
	const bool bOwnedByOther = Snapshot.ProviderID != 0
		&& Snapshot.ProviderID != SourceID
		&& !Snapshot.bExpired;
	// 前フレームから自分が provider のままなら、publish 前に Pending を消費して 1 回だけ publish できる
	const bool bAlreadyProvider = Snapshot.ProviderID == SourceID && !Snapshot.bExpired;

	if (!bOwnedByOther)
	{
		// UPROPERTY 入力の変化は共有状態に触らないので publish 前に実効値へ取り込む
		//（風の Time 積算が UPROPERTY の Enabled 変化に同フレームで追従するため）。
		ApplyInputChanges(Inputs);

		if (!Inputs.SharedWind && WindRuntimeState.IsValid() && Inputs.bWindEnabled && bEffectiveEnabled)
		{
			// SharedWind 本体が無い既存テスト互換経路では、従来どおり publish 前に Time だけを直接進める。
			WindRuntimeState->Time += EffectiveDeltaTime * Inputs.WindTimeScale;
		}

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		FKawaiiPhysicsSharedPublisherState State;
		const auto BuildDescAndState = [&]()
		{
			const bool bProviderDisabled = !(bEffectiveEnabled && EffectiveSimpleWorldSettings.bEnabled);
			const bool bEffectiveWindEnabled = bEffectiveEnabled &&
				(Inputs.SharedWind ? Inputs.SharedWind->bIsEnabled : Inputs.bWindEnabled);
			Desc = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldCollisionDesc(EffectiveSimpleWorldSettings);
			Desc.bProviderDisabled = bProviderDisabled;

			State.bPublisherEnabled = bEffectiveEnabled;
			State.bSimpleWorldEnabled = !bProviderDisabled;
			State.GatherScope = Desc.GatherScope;
			State.SimpleWorldDesc = Desc;
			State.SimpleWorldSettings = EffectiveSimpleWorldSettings;
			State.Wind.bPublisherWindEnabled = bEffectiveWindEnabled;
			State.Wind.Time = WindRuntimeState.IsValid() ? WindRuntimeState->Time : 0.0f;
			// 消費側が次フレーム以降の外挿に使う scale として、SharedWind があればその現在値を配る
			State.Wind.PublisherTimeScale = Inputs.SharedWind ? Inputs.SharedWind->TimeScale : Inputs.WindTimeScale;
			if (Inputs.SharedWind)
			{
				State.Wind.Params = Inputs.SharedWind->BuildSharedWindParams();
				// ActiveGust はこの直前の ConsumePendingRequests と同じ Worker 上でのみ更新されるため、ここでは Mutex を取らない。
				State.Wind.ActiveGust = Inputs.SharedWind->RuntimeState.IsValid()
					? Inputs.SharedWind->RuntimeState->ActiveGust
					: FKawaiiProceduralWindActiveGust();
			}
		};

		// BP からの Pending 要求の消費と SharedWind の gust / Time / Scope 更新をまとめて行う（何か処理したら true）
		const auto ProcessPendingAndWind = [&]() -> bool
		{
			bool bProcessed = false;

			FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests Pending;
			if (PublisherEntry->ConsumePendingPublisherRequests(Pending))
			{
				if (Pending.Enabled.IsSet())
				{
					bEffectiveEnabled = Pending.Enabled.GetValue();
					bProcessed = true;
				}
				if (Pending.SimpleWorldSettings.IsSet())
				{
					EffectiveSimpleWorldSettings = Pending.SimpleWorldSettings.GetValue();
					bProcessed = true;
				}
				if (Pending.WindParams.IsSet())
				{
					if (Inputs.SharedWind)
					{
						// 風の live 値と publish 値の真実を SharedWind の struct 1 つに揃えるため、リクエストは永続上書きとして扱う。
						Inputs.SharedWind->RequestDynamicParams(Pending.WindParams.GetValue());
						bProcessed = true;
					}
					else
					{
						// SharedWind 本体が無い互換経路では publish 先が無いため、風パラメータ要求はここで破棄する。
					}
				}
			}

			if (Inputs.SharedWind)
			{
				PendingGustBuffer.Reset();
				PublisherEntry->ConsumePendingGustRequests(PendingGustBuffer);
				for (const FKawaiiPhysicsSharedPublisherGustRequest& Gust : PendingGustBuffer)
				{
					if (Gust.bStop)
					{
						Inputs.SharedWind->RequestGustStop(Gust.BlendOutTime);
					}
					else
					{
						Inputs.SharedWind->RequestGust(Gust.Strength, Gust.RiseTime, Gust.DecayTime, Gust.HoldTime);
					}
				}

				Inputs.SharedWind->ConsumePendingRequests();
				// Time は当フレーム分まで Update の冒頭で進め済み。ここで新しい TimeScale / 有効フラグを掛け直すと、
				// 先に評価された消費側が旧 scale で外挿した値より手前を publish して巻き戻す
				Inputs.SharedWind->RecordScopeSample();
				bProcessed = true;
			}

			return bProcessed;
		};

		if (bAlreadyProvider)
		{
			// 所有権が確定済みのフレームは、Time 積算まで済ませた最新状態を 1 回だけ publish する
			//（publish 前に消費しても、他 provider に横取りされる余地は前フレームの所有権で塞がれている）
			ProcessPendingAndWind();
		}

		BuildDescAndState();

		if (PublisherEntry->PublishState(State, SourceID, CurrentFrame, ProviderMaxAgeFrames))
		{
#if WITH_DEV_AUTOMATION_TESTS
			// claim フレームの最初の publish（受理判定）に当フレーム分までの追いつきが載っているかをテストから確認する
			bLastUpdateWasClaim = !bAlreadyProvider;
			if (!bAlreadyProvider)
			{
				LastClaimPublishedWindTime = State.Wind.Time;
			}
#endif

			// claim フレームはここで初めて自分が provider になるので、受理後に Pending を消費する
			//（拒否された Publisher が BP 要求を消費しないため）。反映があれば最新値で publish し直す
			if (!bAlreadyProvider && ProcessPendingAndWind())
			{
				BuildDescAndState();
				PublisherEntry->PublishState(State, SourceID, CurrentFrame, ProviderMaxAgeFrames);
			}

			LastPublishedState = State;
			LastPublishSerial = PublisherEntry->GetPublishSerial();
			// 次のフレームの前進は、消費側の外挿と同じくこの State の TimeScale / Enabled で行う
			bHasPublishedToCurrentEntry = true;
			bNeedsEntryReacquire = false;
#if !UE_BUILD_SHIPPING
			bProviderConflictWarningLogged = false;
#endif

			// provider として受理されてから SimpleWorld Entry へ Desc を登録・heartbeat する
			if (!LastSentDesc.IsSet() || !(LastSentDesc.GetValue() == Desc))
			{
				SimpleWorldEntry->SetDesc(SourceID, Desc, CurrentFrame, SkelComp, true);
				LastSentDesc = Desc;
#if WITH_DEV_AUTOMATION_TESTS
				++NumSetDescCalls;
#endif
			}
			else if (!SimpleWorldEntry->MarkRead(SourceID, CurrentFrame))
			{
				SimpleWorldEntry->SetDesc(SourceID, Desc, CurrentFrame, SkelComp, true);
				LastSentDesc = Desc;
#if WITH_DEV_AUTOMATION_TESTS
				++NumSetDescCalls;
#endif
			}

			return true;
		}

		if (PublisherEntry->IsExpired(CurrentFrame, ProviderMaxAgeFrames))
		{
			// Entry ごと期限切れ。provider Desc も取り下げてから再取得を待つ
			//（publish されない設定が幽霊 Desc として収集側に残らないようにする）
			UnregisterProviderDesc();
			bNeedsEntryReacquire = true;
			return false;
		}

		// 同一フレーム内で別 provider に先を越された
	}

	// 負けた側は provider Desc を取り下げて、収集設定への混入を止める
	UnregisterProviderDesc();
#if !UE_BUILD_SHIPPING
	if (!bProviderConflictWarningLogged)
	{
		UE_LOG(LogKawaiiPhysics, Warning,
		       TEXT("Kawaii Physics Shared Publisher rejected publish for Tag '%s' because another provider is alive."),
		       *DebugTag.ToString());
		bProviderConflictWarningLogged = true;
	}
#endif
	bNeedsEntryReacquire = false;
	return false;
}

FAnimNode_KawaiiPhysicsSharedPublisher::FAnimNode_KawaiiPhysicsSharedPublisher()
{
	SharedGroupTag = TAG_KawaiiPhysics_Shared_Default;
	InitializeHelper();
}

FAnimNode_KawaiiPhysicsSharedPublisher::FAnimNode_KawaiiPhysicsSharedPublisher(
	const FAnimNode_KawaiiPhysicsSharedPublisher& Other)
	: FAnimNode_Base(Other)
	, Source(Other.Source)
	, bEnabled(Other.bEnabled)
	, SharedGroupTag(Other.SharedGroupTag)
	, SimpleWorldCollision(Other.SimpleWorldCollision)
	, SharedWind(Other.SharedWind)
	, WindPresetDataAsset(Other.WindPresetDataAsset)
	, WindPresetTag(Other.WindPresetTag)
	, ResolvedTag(Other.ResolvedTag)
	, PreUpdateFrame(Other.PreUpdateFrame)
	, ProviderMaxAgeFrames(Other.ProviderMaxAgeFrames)
#if !UE_BUILD_SHIPPING
	, bInvalidTagWarningLogged(Other.bInvalidTagWarningLogged)
	, bInvalidWindPresetWarningLogged(Other.bInvalidWindPresetWarningLogged)
#endif
#if WITH_EDITORONLY_DATA
	, LastUpdatedTime(Other.LastUpdatedTime)
#endif
{
	bReinitRequested.store(true, std::memory_order_release);
	InitializeHelper();
}

FAnimNode_KawaiiPhysicsSharedPublisher& FAnimNode_KawaiiPhysicsSharedPublisher::operator=(
	const FAnimNode_KawaiiPhysicsSharedPublisher& Other)
{
	if (this == &Other)
	{
		return *this;
	}

	if (Helper.IsValid())
	{
		Helper->ReleaseEntries();
	}
	Helper.Reset();

	FAnimNode_Base::operator=(Other);
	Source = Other.Source;
	bEnabled = Other.bEnabled;
	SharedGroupTag = Other.SharedGroupTag;
	SimpleWorldCollision = Other.SimpleWorldCollision;
	SharedWind = Other.SharedWind;
	WindPresetDataAsset = Other.WindPresetDataAsset;
	WindPresetTag = Other.WindPresetTag;
	ResolvedTag = Other.ResolvedTag;
	PreUpdateFrame = Other.PreUpdateFrame;
	ProviderMaxAgeFrames = Other.ProviderMaxAgeFrames;
#if WITH_EDITORONLY_DATA
	LastUpdatedTime = Other.LastUpdatedTime;
#endif
#if !UE_BUILD_SHIPPING
	bInvalidTagWarningLogged = Other.bInvalidTagWarningLogged;
	bInvalidWindPresetWarningLogged = Other.bInvalidWindPresetWarningLogged;
#endif
	CachedSubsystem.Reset();
	CachedSkelComp.Reset();
	CachedFamilyRoot.Reset();
	CachedWindPresetDataAsset.Reset();
	CachedWindPresetTag = FGameplayTag();
	// SharedWind ごと代入し直したので、代入前のノードで取った退避値は捨てる（次の適用で新しい authored 値を退避する）
	SharedWindBeforePreset.Reset();
	bReinitRequested.store(true, std::memory_order_release);
	InitializeHelper();
	return *this;
}

FAnimNode_KawaiiPhysicsSharedPublisher::~FAnimNode_KawaiiPhysicsSharedPublisher()
{
	if (Helper.IsValid())
	{
		Helper->ReleaseEntries();
	}
}

void FAnimNode_KawaiiPhysicsSharedPublisher::InitializeHelper()
{
	if (!Helper.IsValid())
	{
		Helper = MakeUnique<FKawaiiPhysicsSharedPublishHelper>();
	}
	Helper->SetSourceID(GetSourceID());
	Helper->SetDebugTag(SharedGroupTag);
}

void FAnimNode_KawaiiPhysicsSharedPublisher::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
	bReinitRequested.store(true, std::memory_order_release);
	InitializeHelper();
	Helper->ResetEffectiveValues(
		MakePublishInputs(bEnabled, SimpleWorldCollision, SharedWind.bIsEnabled, SharedWind.TimeScale, &SharedWind));
}

void FAnimNode_KawaiiPhysicsSharedPublisher::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
}

void FAnimNode_KawaiiPhysicsSharedPublisher::PreUpdate(const UAnimInstance* InAnimInstance)
{
	PreUpdateFrame = GFrameCounter;
	// 枝が blend weight 0 で Update されないフレームの時間も累積し、Update_AnyThread が再開したときに
	// まとめて Time を進める（消費側の外挿より Publisher が遅れて Time が巻き戻るのを防ぐ）
	if (InAnimInstance && Helper.IsValid())
	{
		Helper->AccumulatePendingDeltaTime(InAnimInstance->GetDeltaSeconds());
	}
	ProviderMaxAgeFrames = static_cast<uint64>(
		FMath::Max(0, GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge()));

	const USkeletalMeshComponent* SkelComp = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;
	AActor* Owner = SkelComp ? SkelComp->GetOwner() : nullptr;
	UWorld* World = SkelComp ? SkelComp->GetWorld() : nullptr;
	UKawaiiPhysicsSharedCollisionSubsystem* Subsystem =
		World ? World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>() : nullptr;
	if (!SkelComp || !Owner || !World || !Subsystem)
	{
		// 供給元を失った状態なので、provider Desc の削除と Publisher Entry の期限切れ化まで行ってから待機する
		Helper->ReleaseEntries();
		CachedSubsystem.Reset();
		CachedSkelComp.Reset();
		CachedFamilyRoot.Reset();
		ResolvedTag = FGameplayTag();
		return;
	}

	const bool bReinit = bReinitRequested.exchange(false, std::memory_order_acq_rel);
	const bool bNeedsReacquire = Helper->NeedsEntryReacquire();
	const bool bTagChanged = ResolvedTag != SharedGroupTag;
	const bool bWindPresetChanged =
		CachedWindPresetDataAsset.Get() != WindPresetDataAsset.Get() || CachedWindPresetTag != WindPresetTag;
	if (!bReinit && !bNeedsReacquire && !bTagChanged && !bWindPresetChanged)
	{
		return;
	}

	ApplySharedWindPreset();
	if (bWindPresetChanged && !bReinit && !bNeedsReacquire && !bTagChanged)
	{
		return;
	}

	if (!SharedGroupTag.IsValid())
	{
		Helper->ReleaseEntries();
		CachedSubsystem.Reset();
		CachedSkelComp.Reset();
		CachedFamilyRoot.Reset();
		ResolvedTag = FGameplayTag();
#if !UE_BUILD_SHIPPING
		if (!bInvalidTagWarningLogged)
		{
			UE_LOG(LogKawaiiPhysics, Warning,
			       TEXT("Kawaii Physics Shared Publisher skipped initialization because Shared Group Tag is invalid."));
			bInvalidTagWarningLogged = true;
		}
#endif
		return;
	}

#if !UE_BUILD_SHIPPING
	bInvalidTagWarningLogged = false;
#endif

	AActor* FamilyRoot = UKawaiiPhysicsSharedCollisionSubsystem::GetFamilyRoot(Owner);
	if (!FamilyRoot)
	{
		Helper->ReleaseEntries();
		CachedSubsystem.Reset();
		CachedSkelComp.Reset();
		CachedFamilyRoot.Reset();
		ResolvedTag = FGameplayTag();
		return;
	}

	// 同じキーへの reinit では Entry を保持して reader の解放・再登録を起こさない。
	// ここで再取得すると provider Desc が一瞬消え、消費側が登録し直すうえ Publisher Entry も期限切れ扱いになる。
	// reinit の意味は「実効値を UPROPERTY 値へ戻す」ことなので、ResetEffectiveValues だけ行う。
	// Cached* は GameThread（PreUpdate）からしか触らないので Get() での比較で足りる。
	const bool bSameKey = !bTagChanged
		&& CachedFamilyRoot.Get() == FamilyRoot
		&& CachedSubsystem.Get() == Subsystem
		&& CachedSkelComp.Get() == SkelComp;
	if (bSameKey && !bNeedsReacquire)
	{
		const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> CurrentPublisherEntry = Helper->GetSharedPublisherEntry();
		if (CurrentPublisherEntry.IsValid() && !CurrentPublisherEntry->IsExpired(PreUpdateFrame, ProviderMaxAgeFrames))
		{
			Helper->ResetEffectiveValues(
				MakePublishInputs(bEnabled, SimpleWorldCollision, SharedWind.bIsEnabled, SharedWind.TimeScale, &SharedWind));
			return;
		}
	}

	// Tag 変更・FamilyRoot / Subsystem / SkelComp の差し替え・期限切れ・publish 拒否・Entry 消失のいずれかなので取り直す
	Helper->ReleaseEntries();
	CachedSubsystem = Subsystem;
	CachedSkelComp = SkelComp;
	CachedFamilyRoot.Reset();
	ResolvedTag = FGameplayTag();

	FKawaiiPhysicsSimpleWorldCollisionDesc InitialDesc =
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldCollisionDesc(SimpleWorldCollision);
	InitialDesc.bProviderDisabled = !(bEnabled && SimpleWorldCollision.bEnabled);

	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> PublisherEntry =
		Subsystem->FindOrCreateSharedPublisherEntry(FamilyRoot, SharedGroupTag);
	if (!PublisherEntry.IsValid())
	{
		return;
	}

	// 生存中の別 provider がいる間は SimpleWorld へ provider Desc を登録しない。
	// 登録すると負け側の設定が BuildMergedDesc に混ざるため、参照だけ持って勝った時点で Helper::Update が SetDesc する。
	// MarkExpired 済みの Entry を掴んだ場合（FindOrCreateSharedPublisherEntry の置き換えと競合した等）も publish が必ず拒否されるので、
	// 同じく Desc を登録せず次フレームの再取得を待つ。
	const FKawaiiPhysicsSimpleWorldRegistryKey SimpleWorldKey =
		FKawaiiPhysicsSimpleWorldRegistryKey::MakeSharedKey(FamilyRoot, SharedGroupTag);
	const uint64 ExistingProviderID = PublisherEntry->GetProviderID();
	const bool bOwnedByOther = (ExistingProviderID != 0 && ExistingProviderID != GetSourceID()
			&& !PublisherEntry->IsExpired(PreUpdateFrame, ProviderMaxAgeFrames))
		|| PublisherEntry->IsMarkedExpired();
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry = bOwnedByOther
		? Subsystem->FindSimpleWorldEntry(SimpleWorldKey)
		: Subsystem->FindOrCreateSimpleWorldEntry(
			SimpleWorldKey,
			GetSourceID(),
			InitialDesc,
			TWeakObjectPtr<const USkeletalMeshComponent>(SkelComp),
			true);

	if (!SimpleWorldEntry.IsValid())
	{
		if (bOwnedByOther)
		{
			// 勝ち側がまだ Entry を作っていないので、Publisher Entry だけ持って次フレームに取り直す
			//（SimpleWorldEntry が null のままなので NeedsEntryReacquire() は true）
			Helper->SetEntries(PublisherEntry, nullptr, TWeakObjectPtr<const USkeletalMeshComponent>(SkelComp));
		}
		return;
	}

	CachedFamilyRoot = FamilyRoot;
	ResolvedTag = SharedGroupTag;
	Helper->SetDebugTag(SharedGroupTag);
	Helper->SetEntries(PublisherEntry, SimpleWorldEntry, TWeakObjectPtr<const USkeletalMeshComponent>(SkelComp));
	Helper->ResetEffectiveValues(
		MakePublishInputs(bEnabled, SimpleWorldCollision, SharedWind.bIsEnabled, SharedWind.TimeScale, &SharedWind));
}

void FAnimNode_KawaiiPhysicsSharedPublisher::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);
	Source.Update(Context);

	Helper->Update(
		MakePublishInputs(bEnabled, SimpleWorldCollision, SharedWind.bIsEnabled, SharedWind.TimeScale, &SharedWind),
		SharedWind.RuntimeState,
		Context.GetDeltaTime(),
		PreUpdateFrame,
		ProviderMaxAgeFrames);

#if WITH_EDITORONLY_DATA
	LastUpdatedTime = FPlatformTime::Seconds();
#endif
}

void FAnimNode_KawaiiPhysicsSharedPublisher::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
}

void FAnimNode_KawaiiPhysicsSharedPublisher::ResetDynamics(ETeleportType InTeleportType)
{
	if (InTeleportType == ETeleportType::ResetPhysics)
	{
		SharedWind.ResetRuntimeState();
	}
	else if (InTeleportType == ETeleportType::TeleportPhysics)
	{
		if (TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> SimpleWorldEntry = Helper->GetSimpleWorldEntry())
		{
			SimpleWorldEntry->RequestRegather();
		}
	}
}

void FAnimNode_KawaiiPhysicsSharedPublisher::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(FString::Printf(
		TEXT("%s(Tag: %s, Enabled: %d, PresetTag: %s, Serial: %llu)"),
		*DebugData.GetNodeName(this),
		*SharedGroupTag.ToString(),
		bEnabled ? 1 : 0,
		*WindPresetTag.ToString(),
		static_cast<unsigned long long>(Helper->GetLastPublishSerial())));
	Source.GatherDebugData(DebugData);
}

void FAnimNode_KawaiiPhysicsSharedPublisher::ApplySharedWindPreset()
{
	if (!IsInGameThread())
	{
		return;
	}

	CachedWindPresetDataAsset = WindPresetDataAsset;
	CachedWindPresetTag = WindPresetTag;

	if (!WindPresetDataAsset)
	{
		// プリセットを外したら authored 値へ戻す（UPROPERTY の契約）。RuntimeState（Time / 突風）は保つ
		if (SharedWindBeforePreset.IsSet())
		{
			SharedWind.ApplyDynamicParams(SharedWindBeforePreset.GetValue());
			SharedWindBeforePreset.Reset();
		}
#if !UE_BUILD_SHIPPING
		bInvalidWindPresetWarningLogged = false;
#endif
		return;
	}

	FKawaiiProceduralWindDynamicParams Params;
	if (UKawaiiPhysicsWindPresetDataAsset::ResolvePresetParamsByTag(WindPresetDataAsset, WindPresetTag, Params))
	{
		// 最初の適用の直前だけ authored 値を退避する（プリセット A → B の切り替えでは A 適用前の値を保つ）
		if (!SharedWindBeforePreset.IsSet())
		{
			SharedWindBeforePreset = SharedWind.BuildDynamicParamsSnapshot();
		}
		Params.bOverrideIsEnabled = true;
		Params.bIsEnabled = true;
		Params.bOverrideTimeScale = true;
		Params.TimeScale = 1.0f;
		SharedWind.ApplyDynamicParams(Params);
#if !UE_BUILD_SHIPPING
		bInvalidWindPresetWarningLogged = false;
#endif
		return;
	}

	// Tag が引けなくなった場合もプリセット無しと同じ扱いにして authored 値へ戻す（適用済みの値が残り続けないように）
	if (SharedWindBeforePreset.IsSet())
	{
		SharedWind.ApplyDynamicParams(SharedWindBeforePreset.GetValue());
		SharedWindBeforePreset.Reset();
	}

#if !UE_BUILD_SHIPPING
	if (!bInvalidWindPresetWarningLogged)
	{
		UE_LOG(LogKawaiiPhysics, Warning,
		       TEXT("Kawaii Physics Shared Publisher failed to apply Wind Preset Tag '%s' from DataAsset '%s'."),
		       *WindPresetTag.ToString(),
		       *GetNameSafe(WindPresetDataAsset));
		bInvalidWindPresetWarningLogged = true;
	}
#endif
}

void FAnimNode_KawaiiPhysicsSharedPublisher::ResetSharedWindPresetSnapshot()
{
	// SharedWind を外から authored 値で置き換えられた後なので、置き換え前の値で取った退避は捨てる
	//（次の ApplySharedWindPreset が新しい authored 値を退避してからプリセットを適用し直す）
	SharedWindBeforePreset.Reset();
}

void FAnimNode_KawaiiPhysicsSharedPublisher::RequestSharedPublisherReinit()
{
	bReinitRequested.store(true, std::memory_order_release);
}

const FKawaiiPhysicsSharedPublishHelper& FAnimNode_KawaiiPhysicsSharedPublisher::GetPublishHelper() const
{
	return *Helper;
}

bool FAnimNode_KawaiiPhysicsSharedPublisher::IsEffectiveEnabled() const
{
	return Helper->IsEffectiveEnabled();
}

const FKawaiiPhysicsSimpleWorldCollisionSettings&
FAnimNode_KawaiiPhysicsSharedPublisher::GetEffectiveSimpleWorldCollisionSettings() const
{
	return Helper->GetEffectiveSimpleWorldSettings();
}

TSharedPtr<FKawaiiPhysicsSharedPublisherEntry>
FAnimNode_KawaiiPhysicsSharedPublisher::GetSharedPublisherEntry() const
{
	return Helper->GetSharedPublisherEntry();
}

TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>
FAnimNode_KawaiiPhysicsSharedPublisher::GetSimpleWorldEntry() const
{
	return Helper->GetSimpleWorldEntry();
}
