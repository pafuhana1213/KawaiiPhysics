// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsTypes.h"
#include "KawaiiPhysicsCollisionLimits.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"

/** Test accessor backed by production simulation and a minimal pose. */
struct FKawaiiPhysicsTestAccessor
{
	FAnimNode_KawaiiPhysics Node;
	TStrongObjectPtr<USkeleton> PoseSkeleton;
	TUniquePtr<FAnimInstanceProxy> PoseProxy;
	bool bHasSteppedFrame = false;

	void ApplyPhysicsAsset(const FBoneContainer& RequiredBones) { Node.ApplyPhysicsAsset(RequiredBones); }
	void ApplyLimitsDataAsset(const FBoneContainer& RequiredBones) { Node.ApplyLimitsDataAsset(RequiredBones); }
	/** LOD 変更時に CacheBones から呼ばれるボーン参照の初期化 / Bone reference initialization called from CacheBones on LOD change */
	void CallInitializeBoneReferences(const FBoneContainer& RequiredBones) { Node.InitializeBoneReferences(RequiredBones); }
	/** 評価の冒頭で行う、要求されたボーン参照の初期化とデータアセットの再適用 / Pending re-initialization done at the start of an evaluation */
	void CallApplyPendingRuntimeReinit(const FBoneContainer& RequiredBones) { Node.ApplyPendingRuntimeReinit(RequiredBones); }
	bool IsBoneReferencesReinitPending() const { return Node.bBoneReferencesNeedReinit; }
	bool IsLimitAssetsReapplyPending() const { return Node.bLimitAssetsNeedReapply; }
	bool IsModifyBonesReinitPending() const { return Node.bModifyBonesNeedsReinit; }
	bool IsSyncBonesReinitPending() const { return Node.bSyncBonesNeedReinit; }
	bool IsSharedCollisionReinitPending() const { return Node.bSharedCollisionNeedsReinit; }
	bool IsPhysicsSettingsRefreshPending() const { return Node.bPhysicsSettingsRefreshRequested; }
	static bool HasPendingRuntimeApplyRequest(const FAnimNode_KawaiiPhysics& TargetNode)
	{
		return TargetNode.bModifyBonesNeedsReinit || TargetNode.bBoneReferencesNeedReinit ||
			TargetNode.bLimitAssetsNeedReapply || TargetNode.bSyncBonesNeedReinit ||
			TargetNode.bSharedCollisionNeedsReinit || TargetNode.bPhysicsSettingsRefreshRequested;
	}
	void ClearPendingReinitForTest()
	{
		Node.bBoneReferencesNeedReinit = false;
		Node.bLimitAssetsNeedReapply = false;
		Node.bModifyBonesNeedsReinit = false;
		Node.bSyncBonesNeedReinit = false;
	}

#if WITH_EDITOR
	void SetMirrorTableCacheForPIE(bool bEnabled) { Node.bCacheMirrorTablesForPIE = bEnabled; }
	void ApplyMirrorLimits(const FBoneContainer& RequiredBones) { Node.ApplyMirrorLimits(RequiredBones); }
	const FKawaiiPhysicsMirrorTableCache* GetMirrorTableCache() const { return Node.CachedMirrorTables.Get(); }
#endif

	// ========================================================================
	//  セットアップ
	// ========================================================================

	/**
	 * 直線の縦チェーンを生成。index0 = root(kinematic)、Origin から GravityAxisDir の逆へ Spacing 間隔。
	 * デフォルトは -Z 方向（重力で垂れ下がる素直な向き）。
	 */
	void BuildVerticalChain(int32 NumBones, float Spacing, const FVector& Origin = FVector::ZeroVector,
	                        const FVector& Dir = FVector(0.0f, 0.0f, -1.0f))
	{
		Node.ModifyBones.Reset();
		const FVector UnitDir = Dir.GetSafeNormal();
		for (int32 i = 0; i < NumBones; ++i)
		{
			FKawaiiPhysicsModifyBone Bone;
			Bone.Index = i;
			Bone.ParentIndex = i - 1;
			const FVector Loc = Origin + UnitDir * (Spacing * i);
			Bone.PoseLocation = Loc;
			Bone.Location = Loc;
			Bone.PrevLocation = Loc;
			Bone.PrevPoseLocation = Loc;
			Bone.CurrentPoseLocation = Loc;
			Bone.BoneLength = (i > 0) ? Spacing : 0.0f;
			Node.ModifyBones.Add(Bone);
		}
		for (int32 i = 1; i < NumBones; ++i)
		{
			Node.ModifyBones[i - 1].ChildIndices.Add(i);
		}
		InitializeHarnessState();
	}

	/**
	 * 横に並んだ2本の縦チェーンを生成。index 0..N-1 が左、N..2N-1 が右。
	 */
	void BuildTwoVerticalChains(int32 NumBonesPerChain, float Spacing, float LateralSpacing,
	                            const FVector& Origin = FVector::ZeroVector)
	{
		Node.ModifyBones.Reset();
		for (int32 ChainIndex = 0; ChainIndex < 2; ++ChainIndex)
		{
			const int32 BaseIndex = ChainIndex * NumBonesPerChain;
			const FVector ChainOrigin = Origin + FVector(LateralSpacing * ChainIndex, 0.0f, 0.0f);
			for (int32 i = 0; i < NumBonesPerChain; ++i)
			{
				FKawaiiPhysicsModifyBone Bone;
				Bone.Index = BaseIndex + i;
				Bone.ParentIndex = (i > 0) ? (BaseIndex + i - 1) : -1;
				const FVector Loc = ChainOrigin + FVector(0.0f, 0.0f, -Spacing * i);
				Bone.PoseLocation = Loc;
				Bone.Location = Loc;
				Bone.PrevLocation = Loc;
				Bone.PrevPoseLocation = Loc;
				Bone.CurrentPoseLocation = Loc;
				Bone.BoneLength = (i > 0) ? Spacing : 0.0f;
				Node.ModifyBones.Add(Bone);
			}
			for (int32 i = 1; i < NumBonesPerChain; ++i)
			{
				Node.ModifyBones[BaseIndex + i - 1].ChildIndices.Add(BaseIndex + i);
			}
		}
		InitializeHarnessState();
	}

	/**
	 * SyncBone + BoneSubdivision の回帰テスト用フィクスチャ。
	 * index 0 = 実root, 1 = inter-bone dummy, 2 = 実child,
	 * 3 = 末端 inter-bone dummy, 4 = 分割 tip dummy, 5 = legacy の直接 tip dummy。
	 */
	void BuildSyncBoneSubdivisionFixture()
	{
		Node.ModifyBones.Reset();
		Node.DummyBoneLength = 4.0f;

		auto AddBone = [&](int32 Index, int32 ParentIndex, const FVector& Loc, float LengthFromRoot,
		                   float BoneLength, bool bDummy, bool bInterBoneDummy,
		                   int32 RealParentIndex = -1, int32 RealChildIndex = -1, float Alpha = 0.0f,
		                   FName BoneName = NAME_None)
		{
			FKawaiiPhysicsModifyBone Bone;
			Bone.Index = Index;
			Bone.ParentIndex = ParentIndex;
			Bone.BoneRef.BoneName = BoneName;
			if (!bDummy)
			{
				// 実ボーンは有効な CompactPoseIndex を持たせ、LODフォールバック判定が誤発火しないようにする。
				Bone.BoneRef.CachedCompactPoseIndex = FCompactPoseBoneIndex(Index);
			}
			Bone.Location = Loc;
			Bone.PrevLocation = Loc;
			Bone.PoseLocation = Loc;
			Bone.PrevPoseLocation = Loc;
			Bone.CurrentPoseLocation = Loc;
			Bone.PoseRotation = FQuat::Identity;
			Bone.PrevPoseRotation = FQuat::Identity;
			Bone.CurrentPoseRotation = FQuat::Identity;
			Bone.PoseScale = FVector::OneVector;
			Bone.BoneLength = BoneLength;
			Bone.LengthFromRoot = LengthFromRoot;
			Bone.bDummy = bDummy;
			Bone.bInterBoneDummy = bInterBoneDummy;
			Bone.InterBoneRealParentIndex = RealParentIndex;
			Bone.InterBoneRealChildIndex = RealChildIndex;
			Bone.InterBoneAlpha = Alpha;
			Node.ModifyBones.Add(Bone);
		};

		AddBone(0, -1, FVector(0.0f, 0.0f, 0.0f), 0.0f, 0.0f, false, false, -1, -1, 0.0f,
		        FName(TEXT("Root")));
		AddBone(1, 0, FVector(5.0f, 0.0f, 0.0f), 5.0f, 5.0f, true, true, 0, 2, 0.5f);
		AddBone(2, 1, FVector(10.0f, 0.0f, 0.0f), 10.0f, 5.0f, false, false, -1, -1, 0.0f,
		        FName(TEXT("Child")));
		AddBone(3, 2, FVector(12.0f, 0.0f, 0.0f), 12.0f, 2.0f, true, true, 2, 4, 0.5f);
		AddBone(4, 3, FVector(14.0f, 0.0f, 0.0f), 14.0f, 2.0f, true, false, 2);
		AddBone(5, 0, FVector(0.0f, 4.0f, 0.0f), 4.0f, 4.0f, true, false);

		Node.ModifyBones[0].ChildIndices = {1, 5};
		Node.ModifyBones[1].ChildIndices = {2};
		Node.ModifyBones[2].ChildIndices = {3};
		Node.ModifyBones[3].ChildIndices = {4};
		InitializeHarnessState();
	}

	int32 BuildSyncBoneSubdivisionLengthFixture(int32 SubdivisionCount, float SegmentLength, float TipLength)
	{
		Node.ModifyBones.Reset();
		Node.BoneSubdivisionCount = SubdivisionCount;
		Node.bBoneSubdivisionDensifyByRadius = false;
		Node.DummyBoneLength = TipLength;

		FKawaiiPhysicsModifyBone Root;
		Root.Index = 0;
		Root.BoneRef.BoneName = FName(TEXT("Root"));
		Root.BoneRef.BoneIndex = 0;
		Root.BoneRef.CachedCompactPoseIndex = FCompactPoseBoneIndex(0);
		Node.ModifyBones.Add(Root);

		const FVector ChildLocation(SegmentLength, 0.0f, 0.0f);
		TArray<int32> InsertedIndices;
		const int32 ChildParentIndex = Node.InsertInterBoneDummyBonesCore(
			Node.ModifyBones, 0, ChildLocation, FQuat::Identity, FVector::OneVector,
			SegmentLength, InsertedIndices);

		FKawaiiPhysicsModifyBone Child;
		Child.Index = Node.ModifyBones.Num();
		Child.ParentIndex = ChildParentIndex;
		Child.BoneRef.BoneName = FName(TEXT("Child"));
		Child.BoneRef.BoneIndex = 1;
		Child.BoneRef.CachedCompactPoseIndex = FCompactPoseBoneIndex(1);
		Child.Location = ChildLocation;
		Child.PrevLocation = ChildLocation;
		Child.PoseLocation = ChildLocation;
		const int32 ChildIndex = Node.ModifyBones.Add(Child);
		Node.ModifyBones[ChildParentIndex].ChildIndices.Add(ChildIndex);
		Node.FinalizeInterBoneDummyBones(Node.ModifyBones, InsertedIndices, ChildIndex);

		const FVector TipLocation = ChildLocation + FVector(TipLength, 0.0f, 0.0f);
		const int32 TipParentIndex = Node.InsertInterBoneDummyBonesCore(
			Node.ModifyBones, ChildIndex, TipLocation, FQuat::Identity, FVector::OneVector,
			TipLength, InsertedIndices);

		FKawaiiPhysicsModifyBone Tip;
		Tip.Index = Node.ModifyBones.Num();
		Tip.ParentIndex = TipParentIndex;
		Tip.bDummy = true;
		Tip.InterBoneRealParentIndex = ChildIndex;
		Tip.BoneLength = TipLength / (InsertedIndices.Num() + 1);
		Tip.Location = TipLocation;
		Tip.PrevLocation = TipLocation;
		Tip.PoseLocation = TipLocation;
		const int32 TipIndex = Node.ModifyBones.Add(Tip);
		Node.ModifyBones[TipParentIndex].ChildIndices.Add(TipIndex);
		Node.FinalizeInterBoneDummyBones(Node.ModifyBones, InsertedIndices, TipIndex);
		Node.UpdateHasInterBoneDummies();

		TArray<FTransform> RefBonePose;
		RefBonePose.Add(FTransform::Identity);
		RefBonePose.Add(FTransform(FQuat::Identity, ChildLocation));
		float TotalBoneLength = 0.0f;
		Node.CalcBoneLength(Node.ModifyBones[0], Node.ModifyBones, RefBonePose, TotalBoneLength);
		for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
		{
			if (Bone.LengthFromRoot > 0.0f)
			{
				Bone.LengthRateFromRoot = Bone.LengthFromRoot / TotalBoneLength;
			}
		}

		InitializeHarnessState();
		return ChildIndex;
	}

	/** 全ボーンに同一の PhysicsSettings を適用 */
	void SetAllPhysicsSettings(const FKawaiiPhysicsSettings& Settings)
	{
		for (FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
		{
			Bone.PhysicsSettings = Settings;
		}
	}

	void SetGravityInSimSpace(const FVector& Gravity) { Node.GravityInSimSpace = Gravity; }
	void SetSimpleExternalForceInSimSpace(const FVector& Force) { Node.SimpleExternalForceInSimSpace = Force; }
	void SetSimulationSpace(EKawaiiPhysicsSimulationSpace Space) { Node.SimulationSpace = Space; }
	/** 評価の最後に行う風の問い合わせ位置の保存を呼ぶ / Calls the end-of-evaluation store of wind query positions. */
	void CallStoreWindSamplePositions(FComponentSpacePoseContext& Output) { Node.StoreWindSamplePositions(Output); }
	/** PreUpdate（GameThread）の風の問い合わせを呼ぶ / Calls the PreUpdate (game thread) wind query. */
	void CallSampleWindOnGameThread(const UWorld* World, const USkeletalMeshComponent* SkelComp = nullptr)
	{
		Node.SampleWindOnGameThread(World, SkelComp);
	}
	/** 従来の Wind の速度（WindScale・突風倍率込み）を返す / Returns the legacy Wind velocity (with WindScale and gust factor). */
	FVector CallGetWindVelocity(FComponentSpacePoseContext& Output, int32 BoneIndex) const
	{
		return Node.GetWindVelocity(Output, Node.ModifyBones[BoneIndex]);
	}
	/** 直近の PreUpdate で取得した、ボーンごとの風の数 / Number of per-bone wind samples from the last PreUpdate. */
	int32 GetWindBoneSampleCount() const { return Node.WindSample.BoneSpeeds.Num(); }
	/** 直近の PreUpdate でノード中心の風を取得したか / Whether the last PreUpdate sampled the wind at the node center. */
	bool HasWindCenterSample() const { return Node.WindSample.bHasCenterSample; }
	FTransform ResolveWindSampleComponentTransform(const USkeletalMeshComponent* Component = nullptr) const
	{
		return Node.ResolveWindSampleComponentTransform(Component);
	}
	FVector ComputeWindSampleCenterPosition(const FTransform& Transform) const
	{
		return Node.ComputeWindSampleCenterPosition(Transform);
	}
	bool ShouldSampleWindPerBone() const { return Node.ShouldSampleWindPerBone(); }
	FVector ComputeWindSampleBonePosition(const FTransform& Transform, int32 BoneIndex) const
	{
		return Node.ComputeWindSampleBonePosition(Transform, BoneIndex);
	}
	void SetWindCenterSample(const FVector& Direction, float Speed)
	{
		Node.WindSample.CenterDirection = Direction;
		Node.WindSample.CenterSpeed = Speed;
		Node.WindSample.bHasCenterSample = true;
	}
	void SetWindBoneSamples(const TArray<FVector>& Directions, const TArray<float>& Speeds)
	{
		Node.WindSample.BoneDirections = Directions;
		Node.WindSample.BoneSpeeds = Speeds;
	}
	/** テスト用のコンポーネント変換を評価中のワールド空間キャッシュへ設定する。 / Sets a test component transform in the evaluation world-space cache. */
	void SetWorldSpaceTransformForTest(const FTransform& ComponentToWorld)
	{
		Node.CurrentEvalWorldSpaceCache.ComponentToTargetSpace = ComponentToWorld;
		Node.CurrentEvalWorldSpaceCache.TargetSpaceToComponent = ComponentToWorld.Inverse();
		Node.bHasCurrentEvalWorldSpaceCache = true;
		Node.bHasCurrentEvalSimSpaceCache = false;
	}

	// コンポーネント空間の衝突テスト用に、評価時と同じワールド変換キャッシュを設定する。
	void SetComponentSpaceCollisionTransform(const FTransform& ComponentTransform)
	{
		Node.SimulationSpace = EKawaiiPhysicsSimulationSpace::ComponentSpace;
		Node.bHasCurrentEvalSimSpaceCache = false;
		Node.CurrentEvalWorldSpaceCache.ComponentToTargetSpace = ComponentTransform;
		Node.CurrentEvalWorldSpaceCache.TargetSpaceToComponent = ComponentTransform.Inverse();
		Node.bHasCurrentEvalWorldSpaceCache = true;
	}

	// 読み取り経路の衝突テスト用に、任意のシミュレーション空間の評価時キャッシュを設定する。
	void SetSimulationSpaceCollisionTransform(
		EKawaiiPhysicsSimulationSpace Space, const FTransform& TargetToComponent)
	{
		Node.SimulationSpace = Space;
		Node.CurrentEvalSimSpaceCache.TargetSpaceToComponent = TargetToComponent;
		Node.CurrentEvalSimSpaceCache.ComponentToTargetSpace = TargetToComponent.Inverse();
		Node.bHasCurrentEvalSimSpaceCache = true;
	}

	void SetSharedCollisionSourceSlot(const TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>& Slot)
	{
		Node.CachedSourceSlot = Slot;
	}

	void WriteSharedCollisionToSubsystem(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform)
	{
		Node.WriteSharedCollisionToSubsystem(Output, ComponentTransform);
	}

	void SetSharedCollisionEntry(const TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>& Entry)
	{
		Node.CachedSharedCollisionEntry = Entry;
	}

	void UpdateSharedCollisionLimits(FComponentSpacePoseContext& Output)
	{
		Node.UpdateSharedCollisionLimits(Output);
	}

	const TArray<FSphericalLimit>& GetSharedSphericalLimits() const
	{
		return Node.SharedSphericalLimits;
	}

	void SetSharedCapsuleLimits(const TArray<FCapsuleLimit>& Limits)
	{
		Node.SharedCapsuleLimits = Limits;
	}

	void SetUseLegacyGravity(bool bUse) { Node.bUseLegacyGravity = bUse; }
	void SetSkelCompMove(const FVector& MoveVec, const FQuat& MoveRot = FQuat::Identity)
	{
		Node.SkelCompMoveVector = MoveVec;
		Node.SkelCompMoveRotation = MoveRot;
	}

	/**
	 * SimpleWorld コリジョン配列（Subsystem が本来収集する形状と地面 Box）を直接注入し、bUseSimpleWorldCollision も true にする。
	 * Injects the SimpleWorld collision arrays (the shapes and ground box the Subsystem normally gathers) directly and enables bUseSimpleWorldCollision.
	 */
	void SetSimpleWorldLimits(const TArray<FSphericalLimit>& Spherical, const TArray<FCapsuleLimit>& Capsule,
	                          const TArray<FTaperedCapsuleLimit>& TaperedCapsule, const TArray<FBoxLimit>& Box,
	                          const TArray<FKawaiiPhysicsConvexLimit>& Convex,
	                          const TArray<FBoxLimit>& GroundBox = TArray<FBoxLimit>())
	{
		Node.SimpleWorldSphericalLimits = Spherical;
		Node.SimpleWorldCapsuleLimits = Capsule;
		Node.SimpleWorldTaperedCapsuleLimits = TaperedCapsule;
		Node.SimpleWorldBoxLimits = Box;
		Node.SimpleWorldGroundBoxLimits = GroundBox;
		Node.SimpleWorldConvexLimits = Convex;
		Node.bUseSimpleWorldCollision = true;
	}

	/**
	 * SimpleWorld コリジョン Entry を Subsystem 経由なしで直接注入し、読み取り経路を有効化する。
	 * Injects a SimpleWorld collision Entry directly without the Subsystem and enables the read path.
	 */
	void SetSimpleWorldEntry(const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>& Entry)
	{
		Node.CachedSimpleWorldEntry = Entry;
		Node.SimpleWorldAutomationLocalEntry = Entry;
		Node.bUseSimpleWorldCollision = true;
		Node.bSimpleWorldCollisionInitialized = true;
		Node.InitializedSimpleWorldSource = Node.SimpleWorldCollisionSource;
		Node.InitializedSimpleWorldSharedTag = Node.SimpleWorldCollisionSharedTag;
		Node.bSimpleWorldDescSent = false;
	}

	void SetSimpleWorldCollisionSource(EKawaiiPhysicsSimpleWorldCollisionSource Source)
	{
		Node.SimpleWorldCollisionSource = Source;
		Node.RequestSimpleWorldCollisionReinit();
	}

	void SetSimpleWorldCollisionSharedTag(const FGameplayTag& SharedTag)
	{
		Node.SimpleWorldCollisionSharedTag = SharedTag;
		Node.RequestSimpleWorldCollisionReinit();
	}

	void SetSimpleWorldSharedEntryForAuto(const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>& Entry)
	{
		Node.SimpleWorldAutomationSharedEntry = Entry;
		Node.SimpleWorldSharedKey.KeyObject = GetTransientPackage();
		Node.SimpleWorldSharedKey.Tag = Node.SimpleWorldCollisionSharedTag;
	}

	void SetSimpleWorldLocalEntryForAuto(const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>& Entry)
	{
		Node.SimpleWorldAutomationLocalEntry = Entry;
	}

	/** SimpleWorld 初期化に使う Subsystem を設定する / Sets the subsystem used for SimpleWorld initialization. */
	void SetSimpleWorldSubsystem(UKawaiiPhysicsSharedCollisionSubsystem* Subsystem)
	{
		Node.CachedSharedCollisionSubsystem = Subsystem;
	}

	/** SimpleWorld 初期化済みかを返す / Returns whether SimpleWorld is initialized. */
	bool IsSimpleWorldCollisionInitialized() const { return Node.bSimpleWorldCollisionInitialized; }

	void InitializeSimpleWorldCollision()
	{
		Node.InitializeSimpleWorldCollision();
	}

	// Uses the exact registration/retry path called by EvaluateSkeletalControl_AnyThread.
	void UpdateSharedCollisionRegistration() { Node.UpdateSharedCollisionRegistration(); }
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> GetSharedCollisionEntry() const
	{
		return Node.CachedSharedCollisionEntry;
	}
	TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> GetSharedCollisionSourceSlot() const
	{
		return Node.CachedSourceSlot;
	}
	bool IsSharedCollisionInitialized() const { return Node.bSharedCollisionInitialized; }
	int32 GetSharedCollisionRetryCount() const { return Node.SharedCollisionInitRetryCount; }
	bool HasSharedCollisionWarning() const { return Node.bSharedCollisionInitWarningLogged; }
	void SetSharedCollisionRetryState(int32 RetryCount, bool bWarningLogged)
	{
		Node.SharedCollisionInitRetryCount = RetryCount;
		Node.bSharedCollisionInitWarningLogged = bWarningLogged;
	}

	/**
	 * SimpleWorld の reader キーを直接注入する（Shared Publisher 無しで Subsystem 経由の reader 経路へ向ける）。
	 * Injects the SimpleWorld reader key directly, pointing the node at the subsystem reader path without a Shared Publisher.
	 */
	void SetSimpleWorldReaderKey(const FKawaiiPhysicsSimpleWorldRegistryKey& Key)
	{
		Node.SetSimpleWorldReaderKey(Key);
		Node.bUseSimpleWorldCollision = true;
	}

	/**
	 * InitializeSimpleWorldCollision が実処理に入った回数を返す（reader 再試行スロットルの検証用）。
	 * Returns how many times InitializeSimpleWorldCollision entered its real work (used to verify the reader retry throttle).
	 */
	int32 GetNumSimpleWorldInitializeAttempts() const
	{
		return Node.NumSimpleWorldInitializeAttempts;
	}

	/** Calls production SimpleWorld preparation. */
	void EvaluateSimpleWorldCollision(FComponentSpacePoseContext& Output)
	{
		Node.PrepareSimpleWorldCollision(Output);
	}
	void InjectSharedPublisherState(const FKawaiiPhysicsSharedPublisherState& State,
	                                const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>& Entry,
	                                uint64 ProviderID = 0xFFFF0001,
	                                const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry>& PublisherEntry = nullptr)
	{
		if (PublisherEntry.IsValid())
		{
			PublisherEntry->PublishState(State, ProviderID, GFrameCounter, 60);
		}

		if (Entry.IsValid())
		{
			Entry->SetDesc(ProviderID, State.SimpleWorldDesc, GFrameCounter,
			               TWeakObjectPtr<const USkeletalMeshComponent>(), true);
		}

		FKawaiiPhysicsSimpleWorldRegistryKey Key;
		Key.KeyObject = GetTransientPackage();
		Node.CachedSimpleWorldEntry.Reset();
		Node.SetSimpleWorldReaderKey(Key);
		Node.CachedSimpleWorldEntry = Entry;
		Node.bUseSimpleWorldCollision = true;
		Node.bSimpleWorldCollisionInitialized = Entry.IsValid();
		Node.InitializedSimpleWorldSource = Node.SimpleWorldCollisionSource;
		Node.InitializedSimpleWorldSharedTag = Node.SimpleWorldCollisionSharedTag;
		Node.bSimpleWorldDescSent = false;

		if (Entry.IsValid())
		{
			Entry->AddReaderMember(
				reinterpret_cast<uint64>(&Node),
				Node.CachedSimpleWorldCollisionSkelComp,
				GFrameCounter);
		}

		if (PublisherEntry.IsValid())
		{
			for (int32 Index = 0; Index < Node.ExternalForces.Num(); ++Index)
			{
				FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetMutableProceduralWind(Index);
				if (Wind && Wind->WindSource != EKawaiiPhysicsProceduralWindSource::Local)
				{
					BindSharedWindEntry(*Wind, PublisherEntry);
				}
			}
		}
	}

	void InjectSharedWindEntry(int32 ExternalForceIndex,
	                           const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry>& PublisherEntry)
	{
		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetMutableProceduralWind(ExternalForceIndex);
		if (!Wind)
		{
			return;
		}

		BindSharedWindEntry(*Wind, PublisherEntry);
	}

	/**
	 * ProceduralWind の共有風 Entry を Worker の解決結果と同じ形で束ねる（Entry / ResolvedSource / Serial / 解決 Tag）。
	 * Binds the shared wind Entry on a ProceduralWind exactly as the worker-side resolve does (entry, resolved source, serial, resolved tag).
	 */
	static void BindSharedWindEntry(FKawaiiPhysics_ExternalForce_ProceduralWind& Wind,
	                                const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry>& PublisherEntry)
	{
		Wind.EnsureRuntimeState();
		Wind.RuntimeState->SharedPublisherEntry = PublisherEntry;
		Wind.RuntimeState->ResolvedSource = PublisherEntry.IsValid()
			? EKawaiiPhysicsProceduralWindSource::Shared
			: EKawaiiPhysicsProceduralWindSource::Local;
		Wind.RuntimeState->LastAppliedSharedSerial = 0;
		Wind.RuntimeState->ResolvedSharedTag = PublisherEntry.IsValid() ? Wind.SharedWindTag : FGameplayTag();
	}

	static void PublishSharedPublisherState(
		const TSharedPtr<FKawaiiPhysicsSharedPublisherEntry>& PublisherEntry,
		const FKawaiiPhysicsSharedPublisherState& State,
		uint64 ProviderID = 0xFFFF0001)
	{
		if (PublisherEntry.IsValid())
		{
			PublisherEntry->PublishState(State, ProviderID, GFrameCounter, 60);
		}
	}

	void SetSimpleWorldOwnSkelComp(const USkeletalMeshComponent* SkelComp)
	{
		Node.CachedSimpleWorldCollisionSkelComp = SkelComp;
	}

	bool IsSimpleWorldReaderMode() const
	{
		return Node.bSimpleWorldReaderMode;
	}

	EKawaiiPhysicsSimpleWorldCollisionSource GetSimpleWorldResolvedSource() const
	{
		return Node.GetSimpleWorldResolvedSource();
	}

	const FKawaiiPhysicsSimpleWorldRegistryKey& GetSimpleWorldReaderKey() const
	{
		return Node.SimpleWorldReaderKey;
	}

	int32 GetSimpleWorldReaderRetryCount() const
	{
		return Node.SimpleWorldReaderRetryCount;
	}

	bool IsSimpleWorldReaderWarningLogged() const
	{
		return Node.bSimpleWorldReaderWarningLogged;
	}

	uint64 GetLastReadSimpleWorldMemberSerialSum() const
	{
		return Node.LastReadSimpleWorldMemberSerialSum;
	}

	const TArray<FSphericalLimit>& GetSimpleWorldSphericalLimits() const
	{
		return Node.SimpleWorldSphericalLimits;
	}

	const TArray<FCapsuleLimit>& GetSimpleWorldCapsuleLimits() const
	{
		return Node.SimpleWorldCapsuleLimits;
	}

	const TArray<FTaperedCapsuleLimit>& GetSimpleWorldTaperedCapsuleLimits() const
	{
		return Node.SimpleWorldTaperedCapsuleLimits;
	}

	const TArray<FBoxLimit>& GetSimpleWorldBoxLimits() const
	{
		return Node.SimpleWorldBoxLimits;
	}

	const TArray<FBoxLimit>& GetSimpleWorldGroundBoxLimits() const
	{
		return Node.SimpleWorldGroundBoxLimits;
	}

	const TArray<FKawaiiPhysicsConvexLimit>& GetSimpleWorldConvexLimits() const
	{
		return Node.SimpleWorldConvexLimits;
	}

	bool HasSimpleWorldEntry() const
	{
		return Node.CachedSimpleWorldEntry.IsValid();
	}

	/**
	 * SimpleWorld コリジョンのワーカー側読み取り更新を呼び出す。
	 * Calls the worker-side SimpleWorld collision read update.
	 */
	void UpdateSimpleWorldCollisionLimits(FComponentSpacePoseContext& Output)
	{
		Node.UpdateSimpleWorldCollisionLimits(Output);
	}

	/**
	 * 現在読み込んでいる SimpleWorld コリジョン形状数を返す。
	 * Returns the number of SimpleWorld collision shapes currently read.
	 */
	int32 GetNumSimpleWorldColliders() const
	{
		return Node.GetNumSimpleWorldColliders();
	}

	/**
	 * 最後に読み取った SimpleWorld 形状 Slot の Publish serial を返す。
	 * Returns the last read publish serial of the SimpleWorld shape Slot.
	 */
	uint64 GetLastReadSimpleWorldShapeSerial() const
	{
		return Node.LastReadSimpleWorldShapeSerial;
	}

	/** SimpleWorld Desc の地面コリジョン有無を切り替える（Desc 変化を起こすため） */
	void SetSimpleWorldGroundCollision(bool bEnable)
	{
		Node.bSimpleWorldCollisionGroundCollision = bEnable;
	}

	bool GetSimpleWorldGroundCollision() const
	{
		return Node.bSimpleWorldCollisionGroundCollision;
	}

	void SetSimpleWorldGatherRadiusOverride(float Radius)
	{
		Node.bOverrideSimpleWorldCollisionGatherRadius = true;
		Node.SimpleWorldCollisionGatherRadius = Radius;
		Node.bSimpleWorldRadiusChecked = false;
		Node.SimpleWorldRadiusCheckDeferrals = 0;
	}

	void CheckSimpleWorldGatherRadius(FComponentSpacePoseContext& Output)
	{
		Node.CheckSimpleWorldGatherRadius(Output);
	}

	bool IsSimpleWorldRadiusChecked() const
	{
		return Node.bSimpleWorldRadiusChecked;
	}

	uint8 GetSimpleWorldRadiusCheckDeferrals() const { return Node.SimpleWorldRadiusCheckDeferrals; }

	/** 固定サブステッピング設定（DeveloperSettings の代わりに直接指定） */
	void SetFixedSubstepping(bool bEnable, int32 TargetFps, int32 MaxSubsteps = 8)
	{
		Node.bUseFixedSubsteppingCached = bEnable;
		Node.TargetFramerate = FMath::Max(1, TargetFps);
		Node.MaxSubstepsCached = FMath::Max(1, MaxSubsteps);
		if (!bHasSteppedFrame)
		{
			Node.DeltaTimeOld = 1.0f / static_cast<float>(Node.GetEffectiveTargetFramerate());
		}
	}

	auto ComputeSubstepPlanForTest(float Accumulator, float FrameDeltaTime, int32 MaxSubsteps,
		int32 TargetFramerate) const
	{
		return FAnimNode_KawaiiPhysics::ComputeSubstepPlan(
			Accumulator, FrameDeltaTime, MaxSubsteps, TargetFramerate);
	}

	void SetBoneConstraintIterations(int32 BeforeCollision, int32 AfterCollision)
	{
		Node.BoneConstraintIterationCountBeforeCollision = FMath::Max(0, BeforeCollision);
		Node.BoneConstraintIterationCountAfterCollision = FMath::Max(0, AfterCollision);
	}

	void SetBoneConstraintGlobalComplianceType(EXPBDComplianceType ComplianceType)
	{
		Node.BoneConstraintGlobalComplianceType = ComplianceType;
	}

	void ClearRuntimeBoneConstraints()
	{
		Node.MergedBoneConstraints.Reset();
	}

	void AddRuntimeBoneConstraint(int32 ModifyBoneIndex1, int32 ModifyBoneIndex2, float Length,
	                              bool bOverrideCompliance = false,
	                              EXPBDComplianceType ComplianceType = EXPBDComplianceType::Leather)
	{
		FModifyBoneConstraint Constraint;
		Constraint.ModifyBoneIndex1 = ModifyBoneIndex1;
		Constraint.ModifyBoneIndex2 = ModifyBoneIndex2;
		Constraint.Length = Length;
		Constraint.bOverrideCompliance = bOverrideCompliance;
		Constraint.ComplianceType = ComplianceType;
		Node.MergedBoneConstraints.Add(Constraint);
	}

	// ========================================================================
	//  ステップ実行
	// ========================================================================

	/** Advances a frame using production skip resolution and substeps. */
	void StepFrame(float FrameDt)
	{
		StepFrameInternal(FrameDt, FTransform::Identity);
	}

	/**
	 * コンポーネント変換から world 移動を求めて1フレーム進める（EvaluateSkeletalControl_AnyThread の
	 * UpdateSkelCompMove → SimulateModifyBones → TeleportType リセット → AdvancePreSkelCompTransform の順序を複製）。
	 * テレポート判定・閾値・PreSkelCompTransform の繰り越しは本番と同一関数を呼ぶ。
	 * WorldSpace のテレポート時シミュレーションスキップは Output 依存のため非対応（ComponentSpace のみ）。
	 * Steps one frame deriving the world move from a component transform, duplicating the Evaluate order
	 * (UpdateSkelCompMove -> SimulateModifyBones -> TeleportType reset -> AdvancePreSkelCompTransform).
	 * Teleport detection, thresholds and the PreSkelCompTransform carry-over call the production functions.
	 * The WorldSpace teleport simulation skip needs Output and is not supported (ComponentSpace only).
	 * @return このフレームをテレポートと判定したか / Whether this frame was detected as a teleport
	 */
	bool StepFrameWithComponentTransform(FComponentSpacePoseContext& Output, float FrameDt,
	                                     const FTransform& ComponentTransform)
	{
		if (!ensureMsgf(Node.SimulationSpace == EKawaiiPhysicsSimulationSpace::ComponentSpace,
		                TEXT("FKawaiiPhysicsTestAccessor: StepFrameWithComponentTransform supports ComponentSpace only.")))
		{
			return false;
		}
		Node.UpdateSkelCompMove(Output, ComponentTransform);
		StepFrameInternal(FrameDt, ComponentTransform);
		const bool bTeleportedThisFrame = (Node.TeleportType == ETeleportType::TeleportPhysics);
		Node.TeleportType = ETeleportType::None;
		Node.AdvancePreSkelCompTransform(ComponentTransform, bTeleportedThisFrame);
		return bTeleportedThisFrame;
	}

	/** 前フレームのコンポーネント変換を設定する / Sets the previous-frame component transform. */
	void SetPreSkelCompTransform(const FTransform& Transform)
	{
		Node.PreSkelCompTransform = Transform;
		Node.PrevFrameSkelCompTransform = Transform;
	}
	const FTransform& GetPreSkelCompTransform() const { return Node.PreSkelCompTransform; }
	float GetSubstepAccumulator() const { return Node.SubstepAccumulator; }

	/** 本番の ApplySimulateResult が使う出力位置を計算する / Computes the output locations used by ApplySimulateResult. */
	void CallUpdateOutputLocations() { Node.UpdateOutputLocations(); }
	void CallWorldSpaceTeleport(const FTransform& ComponentTransform) { Node.ApplyWorldSpaceTeleport(ComponentTransform); }
	/** 取り込み済みの一時外力の数と、その外力 / Number of consumed transient forces, and one of them */
	int32 GetTransientForceCount() const { return Node.TransientForceStore.Items.Num(); }
	FInstancedStruct& GetTransientForce(int32 Index) { return Node.TransientForceStore.Items[Index].Force; }
	void SetSubstepPoseInitialized(bool bInitialized) { Node.bSubstepPoseInitialized = bInitialized; }
	void CallResetSubstepPoseOffsets(FKawaiiPhysicsModifyBone& Bone) { FAnimNode_KawaiiPhysics::ResetSubstepPoseOffsets(Bone); }
	void CallCaptureSubstepPoseOffsets() { Node.CaptureSubstepPoseOffsets(); }
	/** 空間切り替え時の本番変換を評価キャッシュで実行する。 / Runs the production space-switch conversion with evaluation caches. */
	void ConvertSimulationSpaceForTest(const FTransform& FromToComponent, const FTransform& ToToComponent,
	                                   EKawaiiPhysicsSimulationSpace From, EKawaiiPhysicsSimulationSpace To)
	{
		FAnimNode_KawaiiPhysics::FSimulationSpaceCache CacheFrom;
		CacheFrom.TargetSpaceToComponent = FromToComponent;
		CacheFrom.ComponentToTargetSpace = FromToComponent.Inverse();
		FAnimNode_KawaiiPhysics::FSimulationSpaceCache CacheTo;
		CacheTo.TargetSpaceToComponent = ToToComponent;
		CacheTo.ComponentToTargetSpace = ToToComponent.Inverse();
		Node.ConvertSimulationSpaceCached(CacheFrom, CacheTo, From, To);
		Node.SimulationSpace = To;
	}

	/** 固定フレーム dt で N フレーム進める */
	void StepFrames(int32 NumFrames, float FrameDt)
	{
		for (int32 i = 0; i < NumFrames; ++i)
		{
			StepFrame(FrameDt);
		}
	}

	// ========================================================================
	//  個別関数の直接呼び出し（コリジョン単体テスト用）
	// ========================================================================

	void CallSphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits)
	{
		Node.AdjustBySphereCollision(Bone, Limits);
	}
	void CallCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits)
	{
		for (FCapsuleLimit& Limit : Limits)
		{
			Limit.UpdateRuntimeCache();
		}
		Node.AdjustByCapsuleCollision(Bone, Limits);
	}
	void CallTaperedCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FTaperedCapsuleLimit>& Limits)
	{
		for (FTaperedCapsuleLimit& Limit : Limits)
		{
			Limit.UpdateRuntimeCache();
		}
		Node.AdjustByTaperedCapsuleCollision(Bone, Limits);
	}
	void CallBoxCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FBoxLimit>& Limits)
	{
		for (FBoxLimit& Limit : Limits)
		{
			Limit.UpdateRuntimeCache();
		}
		Node.AdjustByBoxCollision(Bone, Limits);
	}
	void CallConvexCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FKawaiiPhysicsConvexLimit>& Limits)
	{
		for (FKawaiiPhysicsConvexLimit& Limit : Limits)
		{
			Limit.UpdateRuntimeCache();
		}
		Node.AdjustByConvexCollision(Bone, Limits);
	}
	void CallPlanarCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits)
	{
		for (FPlanarLimit& Limit : Limits)
		{
			Limit.UpdateRuntimeCache();
		}
		Node.AdjustByPlanarCollision(Bone, Limits);
		for (FPlanarLimit& Limit : Limits)
		{
			Limit.PreviousStepPlane = Limit.Plane;
			Limit.bHasPreviousStepPlane = Limit.bEnable;
		}
	}
	void CallAngleLimit(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone)
	{
		Node.AdjustByAngleLimit(Bone, ParentBone);
	}

	// 物理計算関数の直接呼び出し（抽出した処理を解析的に検証する用）
	FVector CallComputeVerletStepVelocity(FKawaiiPhysicsModifyBone& Bone, const FVector& WindVelocity)
	{
		return Node.ComputeVerletStepVelocity(Bone, WindVelocity);
	}
	void CallIntegrateVerletStepPosition(FKawaiiPhysicsModifyBone& Bone, const FVector& Velocity)
	{
		Node.IntegrateVerletStepPosition(Bone, Velocity);
	}
	void CallSimpleExternalForce(FKawaiiPhysicsModifyBone& Bone)
	{
		Node.ApplySimpleExternalForce(Bone);
	}
	void CallWorldMoveFollow(FKawaiiPhysicsModifyBone& Bone)
	{
		Node.ApplyWorldMoveFollowNonBaseBone(Bone);
	}
	FTransform CallComputeBaseBoneStepTransform(const FTransform& Previous, const FTransform& Current, float Fraction)
	{
		return Node.ComputeBaseBoneStepTransform(Previous, Current, Fraction);
	}
	void SetPreviousBaseBoneTransform(const FTransform& Previous)
	{
		Node.PrevBaseBoneSpace2ComponentSpace = Previous;
	}
	FTransform GetPreviousBaseBoneTransform() const { return Node.PrevBaseBoneSpace2ComponentSpace; }
	void CallAdvanceBaseBoneConsumedTransform(const FTransform& Current, float Fraction, bool bDiscard = false)
	{
		Node.AdvanceBaseBoneConsumedTransform(Current, Fraction, bDiscard);
	}
	void CallStiffnessPull(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone, float Exponent)
	{
		Node.ApplyStiffnessPull(Bone, ParentBone, Exponent);
	}
	void CallBoneConstraints()
	{
		Node.AdjustByBoneConstraints();
	}
	void CallUpdatePhysicsSettings()
	{
		Node.UpdatePhysicsSettingsOfModifyBones();
	}

	void CallResetTransientRuntimeState()
	{
		Node.ResetTransientRuntimeState();
	}

	FKawaiiPhysicsSettingsMultiplier CallComputeEffectiveSettingsMultiplierScale() const
	{
		return Node.ComputeEffectivePhysicsSettingsMultiplierScale();
	}

	void SetInitPhysicsSettings(bool bInit) { Node.bInitPhysicsSettings = bInit; }
	bool IsPhysicsSettingsMultiplierAppliedLastUpdate() const { return Node.bPhysicsSettingsMultiplierAppliedLastUpdate; }
	void SetPhysicsSettingsMultiplierAppliedLastUpdate(const bool bValue) { Node.bPhysicsSettingsMultiplierAppliedLastUpdate = bValue; }

	/** Calls the production physics settings update gate. */
	bool RunPhysicsSettingsUpdateGate(float FrameDt)
	{
		Node.DeltaTime = FrameDt;
		return Node.ApplyPhysicsSettingsUpdateGate();
	}
	FKawaiiPhysicsSyncTargetRoot CollectSyncChildTargetsForRoot(int32 RootIndex)
	{
		FKawaiiPhysicsSyncTargetRoot TargetRoot;
		TargetRoot.ModifyBoneIndex = RootIndex;
		Node.CollectSyncBoneChildTargets(TargetRoot);
		return TargetRoot;
	}

	/** 本番の InitSyncBone を呼ぶ / Calls the production InitSyncBone. */
	void CallInitSyncBone(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	                      FKawaiiPhysicsSyncBone& SyncBone)
	{
		Node.InitSyncBone(Output, BoneContainer, SyncBone);
	}

	// ApplySyncBones の target 適用部（root → child targets）を Output 無しで再現。
	void ApplySyncTargetsForRoot(FKawaiiPhysicsSyncTargetRoot& TargetRoot, const FVector& Translation)
	{
		TargetRoot.Apply(Node.ModifyBones, Translation);
		for (FKawaiiPhysicsSyncTarget& Target : TargetRoot.ChildTargets)
		{
			Target.Apply(Node.ModifyBones, Translation);
		}
	}

	// 非剛体ケース用：root と child で異なる translation（attenuation/curve相当）を適用。
	void ApplySyncTargetsForRootSplit(FKawaiiPhysicsSyncTargetRoot& TargetRoot,
	                                  const FVector& RootTranslation, const FVector& ChildTranslation)
	{
		TargetRoot.Apply(Node.ModifyBones, RootTranslation);
		for (FKawaiiPhysicsSyncTarget& Target : TargetRoot.ChildTargets)
		{
			Target.Apply(Node.ModifyBones, ChildTranslation);
		}
	}

	void CallUpdateSubdivisionDummyPoseAfterSyncBones()
	{
		// GetCompactPoseIndex は bUseSkeletonIndex=false 時 CachedCompactPoseIndex を返す（コンテナ非依存）ため空でよい。
		FBoneContainer EmptyContainer;
		Node.UpdateSubdivisionDummyPoseAfterSyncBones(EmptyContainer);
	}

	void CallRestoreBoneLengthsAndLimits() { Node.RestoreBoneLengthsAndLimits(); }
	void CallApplyBridgeDummyCollisionFeedback() { Node.ApplyBridgeDummyCollisionFeedback(); }
	void CallApplyCollisionOnlyDummyFeedback() { Node.ApplyCollisionOnlyDummyFeedback(); }
	void CallCaptureCollisionOnlyDummyPreCollision() { Node.CaptureCollisionOnlyDummyPreCollision(); }
	int32 GetCollisionOnlyDummyPreCollisionScratchNum() const { return Node.CollisionOnlyDummyPreCollisionScratch.Num(); }

	// 直接呼び出しテスト用の時間状態（bInSubstep=false なので GetStepDeltaTime()==Dt）。
	void SetTimeState(float Dt, float DtOld)
	{
		Node.DeltaTime = Dt;
		Node.DeltaTimeOld = DtOld;
		Node.bInSubstep = false;
	}

	// サブステップ中の直接呼び出しテスト用の時間状態。
	void SetSubstepTimeState(float FrameDt, float StepDt)
	{
		Node.DeltaTime = FrameDt;
		Node.FrameDeltaTime = FrameDt;
		Node.StepDeltaTime = StepDt;
		Node.DeltaTimeOld = StepDt;
		Node.bInSubstep = true;
	}

	// WarmUp() のループ中と同じ状態にする（外力を直接呼ぶテストで warm-up 抑制を再現する）。
	void SetWarmingUpForTest(bool bWarmingUp) { Node.bIsWarmingUp = bWarmingUp; }

	// ========================================================================
	//  アクセサ
	// ========================================================================

	int32 Num() const { return Node.ModifyBones.Num(); }
	FKawaiiPhysicsModifyBone& Bone(int32 Index) { return Node.ModifyBones[Index]; }
	const FKawaiiPhysicsModifyBone& Bone(int32 Index) const { return Node.ModifyBones[Index]; }
	FVector TipLocation() const { return Node.ModifyBones.Last().Location; }

	FKawaiiPhysics_ExternalForce_ProceduralWind* GetMutableProceduralWind(int32 ExternalForceIndex)
	{
		if (!Node.ExternalForces.IsValidIndex(ExternalForceIndex) ||
			!Node.ExternalForces[ExternalForceIndex].IsValid() ||
			Node.ExternalForces[ExternalForceIndex].GetScriptStruct() !=
			FKawaiiPhysics_ExternalForce_ProceduralWind::StaticStruct())
		{
			return nullptr;
		}

		return Node.ExternalForces[ExternalForceIndex].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	}

	/** 外力のボーンフィルタ判定表が有効か（キャッシュが実際に使われているかの確認用） */
	static bool IsBoneFilterCacheValid(const FKawaiiPhysics_ExternalForce& Force)
	{
		return Force.bBoneFilterCacheValid;
	}

	/** 外力が PreApply で計算した力（基底クラスの protected メンバー） */
	static FVector GetExternalForceVector(const FKawaiiPhysics_ExternalForce& Force)
	{
		return Force.Force;
	}

	/** 全ボーン位置が有限（NaN/Inf 無し）か */
	bool AllFinite() const
	{
		for (const FKawaiiPhysicsModifyBone& B : Node.ModifyBones)
		{
			if (B.Location.ContainsNaN())
			{
				return false;
			}
		}
		return true;
	}

	/** 全ボーン位置が絶対値 Bound 内に収まっているか（発散検出） */
	bool AllWithin(float Bound) const
	{
		for (const FKawaiiPhysicsModifyBone& B : Node.ModifyBones)
		{
			if (FMath::Abs(B.Location.X) > Bound || FMath::Abs(B.Location.Y) > Bound ||
				FMath::Abs(B.Location.Z) > Bound)
			{
				return false;
			}
		}
		return true;
	}

private:
	void StepFrameInternal(float FrameDt, const FTransform& ComponentTransform)
	{
		if (FrameDt <= 0.0f)
		{
			return;
		}
		if (!ensureMsgf(Node.SimulationSpace != EKawaiiPhysicsSimulationSpace::BaseBoneSpace,
		                TEXT("FKawaiiPhysicsTestAccessor: BaseBoneSpace is not supported headlessly (needs Output-side "
			                "space conversion). Use ComponentSpace/WorldSpace, or a real-mesh integration test.")))
		{
			return;
		}
		if (!PoseProxy)
		{
			InitializeHarnessState();
		}
		PrepareFrame();
		Node.DeltaTime = FrameDt;
		Node.FrameDeltaTime = FrameDt;
		// InitPose は MemStack から確保するので、本番の評価と同じく Mark を張る
		FMemMark MemMark(FMemStack::Get());
		FComponentSpacePoseContext Output(PoseProxy.Get());
		Output.Pose.InitPose(&PoseProxy->GetRequiredBones());
		Node.StepSubsteps(Output, ComponentTransform, nullptr);
		bHasSteppedFrame = true;
	}

	void InitializeHarnessState()
	{
		PoseSkeleton.Reset(NewObject<USkeleton>());
		TArray<FBoneIndexType> RequiredIndices;
		TArray<int32> CompactIndices;
		CompactIndices.Init(INDEX_NONE, Node.ModifyBones.Num());
		{
			FReferenceSkeletonModifier Modifier(PoseSkeleton.Get());
			Modifier.Add(FMeshBoneInfo(TEXT("HarnessRoot"), TEXT("HarnessRoot"), INDEX_NONE),
				FTransform::Identity);
			RequiredIndices.Add(0);
			for (int32 Index = 0; Index < Node.ModifyBones.Num(); ++Index)
			{
				FKawaiiPhysicsModifyBone& Bone = Node.ModifyBones[Index];
				if (Bone.bDummy || Bone.bBridgeDummy)
				{
					continue;
				}
				int32 ParentIndex = Bone.ParentIndex;
				while (Node.ModifyBones.IsValidIndex(ParentIndex) && CompactIndices[ParentIndex] == INDEX_NONE)
				{
					ParentIndex = Node.ModifyBones[ParentIndex].ParentIndex;
				}
				const int32 CompactIndex = RequiredIndices.Num();
				const int32 CompactParent = Node.ModifyBones.IsValidIndex(ParentIndex)
					? CompactIndices[ParentIndex] : 0;
				const FName Name = Bone.BoneRef.BoneName.IsNone()
					? FName(*FString::Printf(TEXT("HarnessBone_%d"), CompactIndex)) : Bone.BoneRef.BoneName;
				Modifier.Add(FMeshBoneInfo(Name, Name.ToString(), CompactParent), FTransform::Identity);
				RequiredIndices.Add(static_cast<FBoneIndexType>(CompactIndex));
				CompactIndices[Index] = CompactIndex;
				Bone.BoneRef.BoneName = Name;
				Bone.BoneRef.BoneIndex = CompactIndex;
				Bone.BoneRef.CachedCompactPoseIndex = FCompactPoseBoneIndex(CompactIndex);
			}
		}
		PoseProxy = MakeUnique<FAnimInstanceProxy>();
		PoseProxy->GetRequiredBones().InitializeTo(
			RequiredIndices, UE::Anim::FCurveFilterSettings(), *PoseSkeleton);
		Node.DeltaTimeOld = 1.0f / static_cast<float>(Node.GetEffectiveTargetFramerate());
		Node.SubstepAccumulator = 0.0f;
		Node.bSubstepPoseInitialized = false;
		bHasSteppedFrame = false;
	}

	void PrepareFrame()
	{
		Node.ResolveSkipSimulateFlags();
	}

	void StepOnce()
	{
		if (!PoseProxy)
		{
			InitializeHarnessState();
		}
		FMemMark MemMark(FMemStack::Get());
		FComponentSpacePoseContext Output(PoseProxy.Get());
		Output.Pose.InitPose(&PoseProxy->GetRequiredBones());
		Node.SimulateOnce(Output, FTransform::Identity, nullptr);
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS
