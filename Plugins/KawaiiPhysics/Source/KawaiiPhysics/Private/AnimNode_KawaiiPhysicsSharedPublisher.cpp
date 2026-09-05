// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysicsSharedPublisher.h"

#include "AnimNode_KawaiiPhysicsInternal.h"
#include "AnimNode_KawaiiPhysicsSharedPublisherInternal.h"
#include "KawaiiPhysics.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
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
		float WindTimeScale)
	{
		FKawaiiPhysicsSharedPublishInputs Inputs;
		Inputs.bEnabled = bEnabled;
		Inputs.SimpleWorld = SimpleWorld;
		Inputs.bWindEnabled = bWindEnabled;
		Inputs.WindTimeScale = WindTimeScale;
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

	if (PublisherEntry.IsValid() && PublisherEntry->GetProviderID() == SourceID)
	{
		PublisherEntry->MarkExpired();
	}

	PublisherEntry.Reset();
	SimpleWorldEntry.Reset();
	SkelComp.Reset();
	LastSentDesc.Reset();
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
//（拒否された Publisher が収集設定や BP 要求を汚染しないため）
bool FKawaiiPhysicsSharedPublishHelper::Update(
	const FKawaiiPhysicsSharedPublishInputs& Inputs,
	const TSharedPtr<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>& WindRuntimeState,
	float DeltaTime,
	uint64 CurrentFrame,
	uint64 ProviderMaxAgeFrames)
{
	if (!PublisherEntry.IsValid() || !SimpleWorldEntry.IsValid() || SourceID == 0)
	{
		bNeedsEntryReacquire = true;
		return false;
	}

	// 所有権を先に確認する。生存中の別 provider がいる間は Desc も Pending も触らない。
	// ロックは PublisherEntry / SimpleWorldEntry を 1 つずつ取り、同時に 2 つ以上保持しない。
	const uint64 CurrentProviderID = PublisherEntry->GetProviderID();
	const bool bOwnedByOther = CurrentProviderID != 0
		&& CurrentProviderID != SourceID
		&& !PublisherEntry->IsExpired(CurrentFrame, ProviderMaxAgeFrames);

	if (!bOwnedByOther)
	{
		// UPROPERTY 入力の変化は共有状態に触らないので publish 前に実効値へ取り込む
		//（風の Time 積算が UPROPERTY の Enabled 変化に同フレームで追従するため）。
		ApplyInputChanges(Inputs);

		// RuntimeState->Mutex は PendingParams / PendingGust / PendingGustStop 用。
		// このノードでは ProceduralWind::PreApply が走らないため、Time は Publisher Update が唯一の書き手になる。
		if (WindRuntimeState.IsValid() && Inputs.bWindEnabled && bEffectiveEnabled)
		{
			WindRuntimeState->Time += FMath::Max(DeltaTime, 0.0f) * Inputs.WindTimeScale;
		}

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		FKawaiiPhysicsSharedPublisherState State;
		const auto BuildDescAndState = [&]()
		{
			const bool bProviderDisabled = !(bEffectiveEnabled && EffectiveSimpleWorldSettings.bEnabled);
			Desc = KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldCollisionDesc(EffectiveSimpleWorldSettings);
			Desc.bProviderDisabled = bProviderDisabled;

			State.bPublisherEnabled = bEffectiveEnabled;
			State.bSimpleWorldEnabled = !bProviderDisabled;
			State.GatherScope = Desc.GatherScope;
			State.SimpleWorldDesc = Desc;
			State.SimpleWorldSettings = EffectiveSimpleWorldSettings;
			State.Wind.bPublisherWindEnabled = Inputs.bWindEnabled && bEffectiveEnabled;
			State.Wind.Time = WindRuntimeState.IsValid() ? WindRuntimeState->Time : 0.0f;
			State.Wind.PublisherTimeScale = Inputs.WindTimeScale;
		};
		BuildDescAndState();

		if (PublisherEntry->PublishState(State, SourceID, CurrentFrame, ProviderMaxAgeFrames))
		{
			// ここから先は自分が provider。BP からの Pending 要求はこの段階で初めて消費する。
			bool bEffectiveValuesChanged = false;
			FKawaiiPhysicsSharedPublisherEntry::FPendingPublisherRequests Pending;
			if (PublisherEntry->ConsumePendingPublisherRequests(Pending))
			{
				if (Pending.Enabled.IsSet())
				{
					bEffectiveEnabled = Pending.Enabled.GetValue();
					bEffectiveValuesChanged = true;
				}
				if (Pending.SimpleWorldSettings.IsSet())
				{
					EffectiveSimpleWorldSettings = Pending.SimpleWorldSettings.GetValue();
					bEffectiveValuesChanged = true;
				}
			}

			// Pending で実効値が変わったので、同じフレームのうちに最新値で publish し直す（自分が provider なので受理される）
			if (bEffectiveValuesChanged)
			{
				BuildDescAndState();
				PublisherEntry->PublishState(State, SourceID, CurrentFrame, ProviderMaxAgeFrames);
			}

			LastPublishedState = State;
			LastPublishSerial = PublisherEntry->GetPublishSerial();
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
	, ResolvedTag(Other.ResolvedTag)
	, PreUpdateFrame(Other.PreUpdateFrame)
	, ProviderMaxAgeFrames(Other.ProviderMaxAgeFrames)
#if !UE_BUILD_SHIPPING
	, bInvalidTagWarningLogged(Other.bInvalidTagWarningLogged)
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
	ResolvedTag = Other.ResolvedTag;
	PreUpdateFrame = Other.PreUpdateFrame;
	ProviderMaxAgeFrames = Other.ProviderMaxAgeFrames;
#if WITH_EDITORONLY_DATA
	LastUpdatedTime = Other.LastUpdatedTime;
#endif
#if !UE_BUILD_SHIPPING
	bInvalidTagWarningLogged = Other.bInvalidTagWarningLogged;
#endif
	CachedSubsystem.Reset();
	CachedSkelComp.Reset();
	CachedFamilyRoot.Reset();
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
		MakePublishInputs(bEnabled, SimpleWorldCollision, SharedWind.bIsEnabled, SharedWind.TimeScale));
}

void FAnimNode_KawaiiPhysicsSharedPublisher::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
}

void FAnimNode_KawaiiPhysicsSharedPublisher::PreUpdate(const UAnimInstance* InAnimInstance)
{
	PreUpdateFrame = GFrameCounter;
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
	if (!bReinit && !bNeedsReacquire && !bTagChanged)
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
				MakePublishInputs(bEnabled, SimpleWorldCollision, SharedWind.bIsEnabled, SharedWind.TimeScale));
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
		MakePublishInputs(bEnabled, SimpleWorldCollision, SharedWind.bIsEnabled, SharedWind.TimeScale));
}

void FAnimNode_KawaiiPhysicsSharedPublisher::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);
	Source.Update(Context);

	Helper->Update(
		MakePublishInputs(bEnabled, SimpleWorldCollision, SharedWind.bIsEnabled, SharedWind.TimeScale),
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
		TEXT("%s(Tag: %s, Enabled: %d, Serial: %llu)"),
		*DebugData.GetNodeName(this),
		*SharedGroupTag.ToString(),
		bEnabled ? 1 : 0,
		static_cast<unsigned long long>(Helper->GetLastPublishSerial())));
	Source.GatherDebugData(DebugData);
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
