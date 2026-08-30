// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsGroundProvider.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/ActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/EngineVersionComparison.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace
{
	// FadeBoxEnableThresholdは調整の必要性が薄いため意図的に定数のまま維持する。
	// 他のチューニング値は a.AnimNode.KawaiiPhysics.SimpleWorldCollision.* CVar（AnimNode_KawaiiPhysics.cppで定義）
	// および UKawaiiPhysicsDeveloperSettings（Simple World Collisionカテゴリ）へ移行済み。
	constexpr float GSimpleWorldFadeBoxEnableThreshold = 0.5f;
	const FName GSimpleWorldIgnoreTagName(TEXT("KawaiiPhysics.IgnoreSimpleWorldCollision"));

	// SkeletalMeshComponentのPhysicsAsset収集結果。Bounding Boxフォールバック可否を区別する。
	enum class EKawaiiPhysicsSimpleWorldSkeletalBuildResult : uint8
	{
		Built,
		NoPhysicsAssetData,
		NoAcceptedBodies,
	};

	void InitializeGatheredSimpleWorldLimit(FCollisionLimitBase& Limit)
	{
		Limit.bEnable = true;
		Limit.SourceType = ECollisionSourceType::SimpleWorld;
	}

	FTransform GetScaleStrippedComponentTransform(const UPrimitiveComponent& Component)
	{
		FTransform ComponentTM = Component.GetComponentTransform();
		ComponentTM.SetScale3D(FVector::OneVector);
		return ComponentTM;
	}

	FTransform GetScaleStrippedKawaiiPhysicsSimpleWorldInstanceTransform(const FTransform& InstanceTM)
	{
		FTransform ScaleStrippedInstanceTM = InstanceTM;
		ScaleStrippedInstanceTM.SetScale3D(FVector::OneVector);
		return ScaleStrippedInstanceTM;
	}

	struct FKawaiiPhysicsSimpleWorldGatherKey
	{
		const UPrimitiveComponent* Component = nullptr;
		int32 InstanceIndex = INDEX_NONE;

		FKawaiiPhysicsSimpleWorldGatherKey(const UPrimitiveComponent* InComponent, int32 InInstanceIndex)
			: Component(InComponent)
			, InstanceIndex(InInstanceIndex)
		{
		}

		friend bool operator==(
			const FKawaiiPhysicsSimpleWorldGatherKey& Lhs,
			const FKawaiiPhysicsSimpleWorldGatherKey& Rhs)
		{
			return Lhs.Component == Rhs.Component && Lhs.InstanceIndex == Rhs.InstanceIndex;
		}
	};

	uint32 GetTypeHash(const FKawaiiPhysicsSimpleWorldGatherKey& Key)
	{
		// 無名namespace内の同名GetTypeHashに隠されないよう、int32版はグローバルを明示する
		return HashCombine(PointerHash(Key.Component), ::GetTypeHash(Key.InstanceIndex));
	}

	bool IsSimpleWorldAggGeomEmpty(const FKAggregateGeom& AggGeom)
	{
		return AggGeom.SphereElems.IsEmpty()
			&& AggGeom.SphylElems.IsEmpty()
			&& AggGeom.TaperedCapsuleElems.IsEmpty()
			&& AggGeom.BoxElems.IsEmpty()
			&& AggGeom.ConvexElems.IsEmpty();
	}

	FCollisionObjectQueryParams BuildSimpleWorldObjectQueryParams(
		const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes)
	{
		FCollisionObjectQueryParams ObjectQueryParams;
		if (ObjectTypes.IsEmpty())
		{
			ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
			ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
			return ObjectQueryParams;
		}

		for (const TEnumAsByte<EObjectTypeQuery>& ObjectType : ObjectTypes)
		{
			const ECollisionChannel CollisionChannel = UEngineTypes::ConvertToCollisionChannel(ObjectType.GetValue());
			if (CollisionChannel != ECC_MAX)
			{
				ObjectQueryParams.AddObjectTypesToQuery(CollisionChannel);
			}
		}
		return ObjectQueryParams;
	}

	bool BuildLocalLimitsForSimpleWorldComponent(
		UPrimitiveComponent& Component,
		const FTransform& ComponentTM,
		const FVector& Scale3D,
		const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc,
		int32 MaxPhysicsAssetBodies,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits,
		TArray<KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding>& OutBodyBindings);

	EKawaiiPhysicsSimpleWorldSkeletalBuildResult BuildSkeletalLocalLimitsForSimpleWorldComponent(
		const USkeletalMeshComponent& SkelComp,
		const FVector& Scale3D,
		const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc,
		int32 MaxPhysicsAssetBodies,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits,
		TArray<KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding>& OutBodyBindings)
	{
		OutLocalLimits.Reset();
		OutBodyBindings.Reset();

		const UPhysicsAsset* PhysicsAsset = SkelComp.GetPhysicsAsset();
		const USkinnedAsset* SkinnedAsset = SkelComp.GetSkinnedAsset();
		if (!PhysicsAsset || !SkinnedAsset || SkelComp.GetNumComponentSpaceTransforms() == 0)
		{
			return EKawaiiPhysicsSimpleWorldSkeletalBuildResult::NoPhysicsAssetData;
		}

		const int32 NumBodies = KawaiiPhysicsSimpleWorldCollision::AppendPhysicsAssetLocalLimits(
			*PhysicsAsset,
			SkinnedAsset->GetRefSkeleton(),
			Scale3D,
			Desc.ConvexFallbackShape,
			MaxPhysicsAssetBodies,
			OutLocalLimits,
			OutBodyBindings);

		return NumBodies > 0
			? EKawaiiPhysicsSimpleWorldSkeletalBuildResult::Built
			: EKawaiiPhysicsSimpleWorldSkeletalBuildResult::NoAcceptedBodies;
	}

	bool BuildLocalLimitsForSimpleWorldComponent(
		UPrimitiveComponent& Component,
		const FTransform& ComponentTM,
		const FVector& Scale3D,
		const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc,
		int32 MaxPhysicsAssetBodies,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits,
		TArray<KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding>& OutBodyBindings)
	{
		OutLocalLimits.Reset();
		OutBodyBindings.Reset();

		if (const USkeletalMeshComponent* SkelComp = Cast<const USkeletalMeshComponent>(&Component))
		{
			if (Desc.SkeletalMeshCollision == EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::None)
			{
				return false;
			}

			if (Desc.SkeletalMeshCollision == EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::PhysicsAsset
				&& MaxPhysicsAssetBodies <= 0)
			{
				// CVarの0指定はPhysicsAssetモードのSkeletalMesh収集を丸ごと止める安全弁として扱う。
				return false;
			}

			if (Desc.SkeletalMeshCollision == EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::PhysicsAsset)
			{
				const EKawaiiPhysicsSimpleWorldSkeletalBuildResult BuildResult =
					BuildSkeletalLocalLimitsForSimpleWorldComponent(
						*SkelComp, Scale3D, Desc, MaxPhysicsAssetBodies, OutLocalLimits, OutBodyBindings);
				if (BuildResult == EKawaiiPhysicsSimpleWorldSkeletalBuildResult::Built)
				{
					return true;
				}
				if (BuildResult == EKawaiiPhysicsSimpleWorldSkeletalBuildResult::NoAcceptedBodies)
				{
					// PhysicsAsset があるのに採用 body が 0 なのは作者が意図的にコリジョンを切っている状態なので Box 化しない。
					return false;
				}
			}

			// PhysicsAsset未設定、SkinnedAsset未設定、ComponentSpaceTransforms未生成の場合はBounding Boxへフォールバックする。
			KawaiiPhysicsSimpleWorldCollision::AppendBoundsLocalLimits(
				Component.Bounds,
				ComponentTM,
				EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox,
				OutLocalLimits);
			return !OutLocalLimits.IsEmpty();
		}

		const UBodySetup* BodySetup = Component.GetBodySetup();
		if (BodySetup && !IsSimpleWorldAggGeomEmpty(BodySetup->AggGeom))
		{
			KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
				BodySetup->AggGeom,
				Scale3D,
				Desc.ConvexFallbackShape,
				OutLocalLimits);
		}

		return !OutLocalLimits.IsEmpty();
	}

	bool IsSimpleWorldGroundBoxNearlyEqual(const FBoxLimit& Lhs, const FBoxLimit& Rhs)
	{
		return Lhs.Location.Equals(Rhs.Location, KINDA_SMALL_NUMBER)
			&& Lhs.Rotation.Equals(Rhs.Rotation, KINDA_SMALL_NUMBER)
			&& Lhs.Extent.Equals(Rhs.Extent, KINDA_SMALL_NUMBER);
	}

	void ResolveSimpleWorldGroundSource(
		FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
		const USkeletalMeshComponent& SkelComp,
		bool bUseMovementGround)
	{
		if (!bUseMovementGround)
		{
			Entry.GroundSource = EKawaiiPhysicsSimpleWorldGroundSource::Trace;
			Entry.GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::None;
			Entry.GroundProvider.Reset();
			Entry.GroundCharacterMovement.Reset();
			Entry.GroundComponent.Reset();
			return;
		}

		// 収集フレームでは毎回アタッチ連鎖を歩き直す（付け替え先の Owner を掴み続けないため、前回の解決結果によるショートカットはしない）。
		// FindComponentByInterface / FindComponentByClass は割り当て無しで安価なので、収集頻度（既定 5Hz 程度）なら許容できる。
		// Provider と CharacterMovement は独立にキャッシュし、両方見つかるか連鎖が尽きるまで歩く。
		Entry.GroundProvider.Reset();
		Entry.GroundCharacterMovement.Reset();

		AActor* Actor = SkelComp.GetOwner();
		for (int32 Depth = 0; Actor && Depth < 8; ++Depth)
		{
			if (!Entry.GroundProvider.IsValid())
			{
				if (Actor->GetClass()->ImplementsInterface(UKawaiiPhysicsGroundProvider::StaticClass()))
				{
					Entry.GroundProvider = Actor;
				}
				else if (UActorComponent* ProviderComponent =
					Actor->FindComponentByInterface(UKawaiiPhysicsGroundProvider::StaticClass()))
				{
					Entry.GroundProvider = ProviderComponent;
				}
			}

			if (!Entry.GroundCharacterMovement.IsValid())
			{
				if (UCharacterMovementComponent* CharacterMovement =
					Actor->FindComponentByClass<UCharacterMovementComponent>())
				{
					Entry.GroundCharacterMovement = CharacterMovement;
				}
			}

			if (Entry.GroundProvider.IsValid() && Entry.GroundCharacterMovement.IsValid())
			{
				break;
			}

			Actor = Actor->GetAttachParentActor();
		}

		// GroundSource は利用可能な最上位ソース（Provider > CharacterMovement > Trace）。
		// 実際の毎 Tick 更新は TryUpdateSimpleWorldGroundBoxFromSource が Provider → CharacterMovement の順に両方試す。
		if (Entry.GroundProvider.IsValid())
		{
			Entry.GroundSource = EKawaiiPhysicsSimpleWorldGroundSource::Provider;
		}
		else if (Entry.GroundCharacterMovement.IsValid())
		{
			Entry.GroundSource = EKawaiiPhysicsSimpleWorldGroundSource::CharacterMovement;
		}
		else
		{
			Entry.GroundSource = EKawaiiPhysicsSimpleWorldGroundSource::Trace;
		}
	}

	bool ApplySimpleWorldGroundBox(
		FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
		const FBoxLimit& NewBox,
		const UPrimitiveComponent* NewGroundComponent,
		EKawaiiPhysicsSimpleWorldGroundSource BoxSource,
		bool& bOutChanged)
	{
		const bool bHadGroundBox = Entry.bHasGroundBox;
		const FBoxLimit PreviousBox = Entry.GroundBox;
		const UPrimitiveComponent* PreviousGroundComponent = Entry.GroundComponent.Get();

		Entry.GroundBox = NewBox;
		Entry.bHasGroundBox = true;
		Entry.GroundComponent = NewGroundComponent;
		// GroundBoxSource は DebugDraw の色分けにしか使わないため変更判定には含めない
		Entry.GroundBoxSource = BoxSource;

		bOutChanged = !bHadGroundBox
			|| !IsSimpleWorldGroundBoxNearlyEqual(PreviousBox, Entry.GroundBox)
			|| PreviousGroundComponent != NewGroundComponent;
		return true;
	}

	bool TryUpdateSimpleWorldGroundBoxFromSource(
		FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
		const USkeletalMeshComponent& SkelComp,
		float Radius,
		bool& bOutChanged)
	{
		bOutChanged = false;

		// Provider → CharacterMovement の順に毎 Tick 両方試す。Provider が有効でも bHit=false ならその場で
		// CharacterMovement へフォールバックする（ドキュメント上の優先順位 Provider > CharacterMovement > Trace を維持）。
		if (UObject* Provider = Entry.GroundProvider.Get())
		{
			const FKawaiiPhysicsGroundHit Hit =
				IKawaiiPhysicsGroundProvider::Execute_GetKawaiiPhysicsGround(Provider, &SkelComp);
			if (Hit.bHit)
			{
				FBoxLimit NewBox;
				if (KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
					Hit.Location, Hit.Normal, Radius, NewBox))
				{
					return ApplySimpleWorldGroundBox(
						Entry, NewBox, Hit.Component.Get(), EKawaiiPhysicsSimpleWorldGroundSource::Provider, bOutChanged);
				}
			}
		}

		if (const UCharacterMovementComponent* CharacterMovement = Entry.GroundCharacterMovement.Get())
		{
			if (CharacterMovement->CurrentFloor.IsWalkableFloor())
			{
				FVector Location = CharacterMovement->CurrentFloor.HitResult.ImpactPoint;
				if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(CharacterMovement->UpdatedComponent))
				{
					// GetActorFeetLocation と同じ考え方で、カプセルの上方向ではなく重力方向へ投影する（カスタム重力を尊重するため）。
					const FVector GravityDir = CharacterMovement->GetGravityDirection();
					Location = Capsule->GetComponentLocation()
						+ GravityDir * (Capsule->GetScaledCapsuleHalfHeight() + CharacterMovement->CurrentFloor.GetDistanceToFloor());
				}

				FBoxLimit NewBox;
				if (KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
					Location,
					CharacterMovement->CurrentFloor.HitResult.ImpactNormal,
					Radius,
					NewBox))
				{
					return ApplySimpleWorldGroundBox(
						Entry,
						NewBox,
						CharacterMovement->CurrentFloor.HitResult.GetComponent(),
						EKawaiiPhysicsSimpleWorldGroundSource::CharacterMovement,
						bOutChanged);
				}
			}
		}

		return false;
	}

#if ENABLE_DRAW_DEBUG
	FColor GetSimpleWorldGroundDebugColor(EKawaiiPhysicsSimpleWorldGroundSource GroundSource)
	{
		if (GroundSource == EKawaiiPhysicsSimpleWorldGroundSource::Provider)
		{
			return FColor::Magenta;
		}
		if (GroundSource == EKawaiiPhysicsSimpleWorldGroundSource::CharacterMovement)
		{
			return FColor::Cyan;
		}
		return FColor::Green;
	}

	void DrawKawaiiPhysicsSimpleWorldLimitRange(
		const UWorld& World,
		const FKawaiiPhysicsSharedCollisionData& LocalLimits,
		int32 SphereOffset,
		int32 NumSpheres,
		int32 CapsuleOffset,
		int32 NumCapsules,
		int32 TaperedCapsuleOffset,
		int32 NumTaperedCapsules,
		int32 BoxOffset,
		int32 NumBoxes,
		const FTransform& Transform,
		const FColor& ShapeColor)
	{
		constexpr float ShapeThickness = 1.5f;
		constexpr uint8 DepthPriority = 0;

		for (int32 LimitIndex = 0; LimitIndex < NumSpheres; ++LimitIndex)
		{
			const FSphericalLimit& Limit = LocalLimits.SphericalLimits[SphereOffset + LimitIndex];
			const FVector LocationWS = Transform.TransformPosition(Limit.Location);
			DrawDebugSphere(&World, LocationWS, Limit.Radius, 12, ShapeColor, false, -1.0f, DepthPriority,
				ShapeThickness);
		}
		for (int32 LimitIndex = 0; LimitIndex < NumCapsules; ++LimitIndex)
		{
			const FCapsuleLimit& Limit = LocalLimits.CapsuleLimits[CapsuleOffset + LimitIndex];
			const FVector LocationWS = Transform.TransformPosition(Limit.Location);
			const FQuat RotationWS = Transform.TransformRotation(Limit.Rotation);
			DrawDebugCapsule(&World, LocationWS, Limit.Length * 0.5f, Limit.Radius, RotationWS, ShapeColor, false,
				-1.0f, DepthPriority, ShapeThickness);
		}
		for (int32 LimitIndex = 0; LimitIndex < NumTaperedCapsules; ++LimitIndex)
		{
			const FTaperedCapsuleLimit& Limit = LocalLimits.TaperedCapsuleLimits[TaperedCapsuleOffset + LimitIndex];
			const FVector LocationWS = Transform.TransformPosition(Limit.Location);
			const FQuat RotationWS = Transform.TransformRotation(Limit.Rotation);
			const float AverageRadius = (Limit.Radius0 + Limit.Radius1) * 0.5f;
			DrawDebugCapsule(&World, LocationWS, Limit.Length * 0.5f, AverageRadius, RotationWS, ShapeColor, false,
				-1.0f, DepthPriority, ShapeThickness);
		}
		for (int32 LimitIndex = 0; LimitIndex < NumBoxes; ++LimitIndex)
		{
			const FBoxLimit& Limit = LocalLimits.BoxLimits[BoxOffset + LimitIndex];
			const FVector LocationWS = Transform.TransformPosition(Limit.Location);
			const FQuat RotationWS = Transform.TransformRotation(Limit.Rotation);
			DrawDebugBox(&World, LocationWS, Limit.Extent, RotationWS, ShapeColor, false, -1.0f, DepthPriority,
				ShapeThickness);
		}
	}

	// シンプルワールドコリジョンのデバッグ描画（GameThread専用）。収集済み形状はComponentローカル形状+ComponentTMから
	// 描画時に都度ワールド変換する。フェード中はFadeAlphaに応じて薄い色にする（PublishScratchは半径縮小済みで
	// アルファ情報が失われるため使わない）。地面Boxのみ別途Green表示する。
	void DrawSimpleWorldCollisionDebug(
		const UWorld& World,
		const FVector& GatherCenter,
		float GatherRadius,
		const FKawaiiPhysicsSimpleWorldCollisionEntry& Entry)
	{
		constexpr float RadiusSphereThickness = 0.0f; // 細線
		constexpr float ShapeThickness = 1.5f;
		constexpr uint8 DepthPriority = 0;

		// 収集半径球
		DrawDebugSphere(&World, GatherCenter, GatherRadius, 16, FColor::White, false, -1.0f, DepthPriority,
			RadiusSphereThickness);

		// 収集済み各形状（水色。フェード中は薄い色）
		for (const FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& Component : Entry.GatheredComponents)
		{
			const float Alpha = FMath::Clamp(Component.FadeAlpha, 0.0f, 1.0f);
			const FColor ShapeColor = FMath::Lerp(FLinearColor(0.0f, 0.35f, 0.35f), FLinearColor(0.0f, 1.0f, 1.0f), Alpha).
				ToFColor(false);

			if (Component.BodyBindings.IsEmpty())
			{
				DrawKawaiiPhysicsSimpleWorldLimitRange(
					World,
					Component.LocalLimits,
					0,
					Component.LocalLimits.SphericalLimits.Num(),
					0,
					Component.LocalLimits.CapsuleLimits.Num(),
					0,
					Component.LocalLimits.TaperedCapsuleLimits.Num(),
					0,
					Component.LocalLimits.BoxLimits.Num(),
					Component.LastComponentTM,
					ShapeColor);
				continue;
			}

			int32 SphereOffset = 0;
			int32 CapsuleOffset = 0;
			int32 TaperedCapsuleOffset = 0;
			int32 BoxOffset = 0;
			const int32 NumBodies = FMath::Min(Component.BodyBindings.Num(), Component.LastBodyWorldTMs.Num());
			for (int32 BodyIndex = 0; BodyIndex < NumBodies; ++BodyIndex)
			{
				const KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding& Binding =
					Component.BodyBindings[BodyIndex];
				DrawKawaiiPhysicsSimpleWorldLimitRange(
					World,
					Component.LocalLimits,
					SphereOffset,
					Binding.NumSphericalLimits,
					CapsuleOffset,
					Binding.NumCapsuleLimits,
					TaperedCapsuleOffset,
					Binding.NumTaperedCapsuleLimits,
					BoxOffset,
					Binding.NumBoxLimits,
					Component.LastBodyWorldTMs[BodyIndex],
					ShapeColor);

				SphereOffset += Binding.NumSphericalLimits;
				CapsuleOffset += Binding.NumCapsuleLimits;
				TaperedCapsuleOffset += Binding.NumTaperedCapsuleLimits;
				BoxOffset += Binding.NumBoxLimits;
			}
		}

		// 地面Box
		if (Entry.bHasGroundBox)
		{
			DrawDebugBox(&World, Entry.GroundBox.Location, Entry.GroundBox.Extent, Entry.GroundBox.Rotation,
				GetSimpleWorldGroundDebugColor(Entry.GroundBoxSource), false, -1.0f, DepthPriority, ShapeThickness);
		}
	}
#endif
}

// SharedCollision CVars（AnimNode_KawaiiPhysics.cpp で定義）
extern TAutoConsoleVariable<int32> CVarSharedCollisionReadMaxAge;
extern TAutoConsoleVariable<int32> CVarSharedCollisionCleanupMaxAge;
extern TAutoConsoleVariable<float> CVarSharedCollisionCleanupInterval;

// SimpleWorldCollision CVars（AnimNode_KawaiiPhysics.cpp で定義）
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionEnable;
extern TAutoConsoleVariable<float> CVarSimpleWorldCollisionGatherIntervalScale;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionMaxComponents;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionMaxPhysicsAssetBodies;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionRegatherOnScaleChange;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionCleanupMaxAge;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionDebugDraw;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionForceEnableOnServer;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionUseMovementGround;

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_Publish"), STAT_KawaiiPhysics_SharedCollision_Publish, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_GetOrCreateSlot"), STAT_KawaiiPhysics_SharedCollision_GetOrCreateSlot, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_ReadMerged"), STAT_KawaiiPhysics_SharedCollision_ReadMerged, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_FindOrCreateEntry"), STAT_KawaiiPhysics_SharedCollision_FindOrCreateEntry, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_FindEntry"), STAT_KawaiiPhysics_SharedCollision_FindEntry, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_Tick"), STAT_KawaiiPhysics_SharedCollision_Tick, STATGROUP_Anim);
DECLARE_DWORD_COUNTER_STAT(TEXT("KawaiiPhysics_SharedCollision_NumEntries"), STAT_KawaiiPhysics_SharedCollision_NumEntries, STATGROUP_Anim);
DECLARE_DWORD_COUNTER_STAT(TEXT("KawaiiPhysics_SharedCollision_NumSlots"), STAT_KawaiiPhysics_SharedCollision_NumSlots, STATGROUP_Anim);

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SimpleWorldCollision_Gather"), STAT_KawaiiPhysics_SimpleWorldCollision_Gather, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SimpleWorldCollision_UpdateTransforms"), STAT_KawaiiPhysics_SimpleWorldCollision_UpdateTransforms, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SimpleWorldCollision_Ground"), STAT_KawaiiPhysics_SimpleWorldCollision_Ground, STATGROUP_Anim);
DECLARE_DWORD_COUNTER_STAT(TEXT("KawaiiPhysics_SimpleWorldCollision_NumGatheredComponents"), STAT_KawaiiPhysics_SimpleWorldCollision_NumGatheredComponents, STATGROUP_Anim);
DECLARE_DWORD_COUNTER_STAT(TEXT("KawaiiPhysics_SimpleWorldCollision_NumSkeletalBodies"), STAT_KawaiiPhysics_SimpleWorldCollision_NumSkeletalBodies, STATGROUP_Anim);

AActor* UKawaiiPhysicsSharedCollisionSubsystem::GetFamilyRoot(AActor* Actor)
{
	// アタッチポインタを辿るだけのread-only処理（UObject変更なし）。任意スレッドから呼べる
	AActor* Root = Actor;
	while (Root)
	{
		AActor* Parent = Root->GetAttachParentActor();
		if (!Parent)
		{
			Parent = Root->GetParentActor();
		}

		if (!Parent || Parent == Root)
		{
			break;
		}

		Root = Parent;
	}
	return Root;
}

// -------------------------------------------------------------------
// FKawaiiPhysicsSharedCollisionSourceSlot
// -------------------------------------------------------------------

void FKawaiiPhysicsSharedCollisionSourceSlot::Publish(FKawaiiPhysicsSharedCollisionData& InOutData)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_Publish);

	FWriteScopeLock WriteLock(BufferLock);
	// Swapで旧BufferをInOutDataへ返し、呼び出し側が確保済みメモリを再利用できるようにする（ロック区間はSwapのみで最小）
	Swap(Buffer, InOutData);

	// フレーム番号を記録（鮮度チェック用）
	LastPublishFrame.store(GFrameCounter, std::memory_order_release);
}

bool FKawaiiPhysicsSharedCollisionSourceSlot::IsExpired(uint64 CurrentFrame, uint64 MaxAge) const
{
	const uint64 LastFrame = LastPublishFrame.load(std::memory_order_acquire);
	return (LastFrame == 0) || (CurrentFrame - LastFrame > MaxAge);
}

void FKawaiiPhysicsSharedCollisionSourceSlot::MarkExpired()
{
	LastPublishFrame.store(0, std::memory_order_release);
}

void FKawaiiPhysicsSharedCollisionSourceSlot::AppendTo(FKawaiiPhysicsSharedCollisionData& OutData) const
{
	FReadScopeLock ReadLock(BufferLock);
	OutData.SphericalLimits.Append(Buffer.SphericalLimits);
	OutData.CapsuleLimits.Append(Buffer.CapsuleLimits);
	OutData.TaperedCapsuleLimits.Append(Buffer.TaperedCapsuleLimits);
	OutData.BoxLimits.Append(Buffer.BoxLimits);
	OutData.PlanarLimits.Append(Buffer.PlanarLimits);
}

// -------------------------------------------------------------------
// FKawaiiPhysicsSharedCollisionEntry
// -------------------------------------------------------------------

TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> FKawaiiPhysicsSharedCollisionEntry::GetOrCreateSlot(uint64 SourceID)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_GetOrCreateSlot);

	// 既存Slotの検索は読み取りロック
	{
		FReadScopeLock ReadLock(SlotsLock);
		if (TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>* Existing = Slots.Find(SourceID))
		{
			return *Existing;
		}
	}

	// 構造変更は書き込みロック。ロック取得待ちの間に他スレッドが作成済みの可能性があるため再確認
	FWriteScopeLock WriteLock(SlotsLock);
	if (TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>* Existing = Slots.Find(SourceID))
	{
		return *Existing;
	}
	TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> NewSlot = MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>();
	Slots.Add(SourceID, NewSlot);
	return NewSlot;
}

void FKawaiiPhysicsSharedCollisionEntry::ReadMerged(FKawaiiPhysicsSharedCollisionData& OutData) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_ReadMerged);
	OutData.Reset();

	const uint64 CurrentFrame = GFrameCounter;

	FReadScopeLock ReadLock(SlotsLock);
	for (const auto& Pair : Slots)
	{
		// 期限切れスロットをスキップ（Publishが停止したSourceのデータを除外）
		if (Pair.Value->IsExpired(CurrentFrame, CVarSharedCollisionReadMaxAge.GetValueOnAnyThread()))
		{
			continue;
		}

		Pair.Value->AppendTo(OutData);
	}
}

void FKawaiiPhysicsSharedCollisionEntry::RemoveExpiredSlots(uint64 CurrentFrame, uint64 MaxAge)
{
	FWriteScopeLock WriteLock(SlotsLock);
	for (auto SlotIt = Slots.CreateIterator(); SlotIt; ++SlotIt)
	{
		if (SlotIt->Value->IsExpired(CurrentFrame, MaxAge))
		{
			SlotIt.RemoveCurrent();
		}
	}
}

int32 FKawaiiPhysicsSharedCollisionEntry::GetSlotCount() const
{
	FReadScopeLock ReadLock(SlotsLock);
	return Slots.Num();
}

bool FKawaiiPhysicsSharedCollisionEntry::IsEmpty() const
{
	FReadScopeLock ReadLock(SlotsLock);
	return Slots.IsEmpty();
}

// -------------------------------------------------------------------
// FKawaiiPhysicsSimpleWorldCollisionDesc / Entry
// -------------------------------------------------------------------

bool FKawaiiPhysicsSimpleWorldCollisionDesc::operator==(const FKawaiiPhysicsSimpleWorldCollisionDesc& Other) const
{
	return GatherIntervalSec == Other.GatherIntervalSec
		&& GatherRadiusOverride == Other.GatherRadiusOverride
		&& ObjectTypes == Other.ObjectTypes
		&& ConvexFallbackShape == Other.ConvexFallbackShape
		&& SkeletalMeshCollision == Other.SkeletalMeshCollision
		&& bGroundCollision == Other.bGroundCollision;
}

FKawaiiPhysicsSimpleWorldCollisionDesc FKawaiiPhysicsSimpleWorldCollisionDesc::Merge(
	const TArray<FKawaiiPhysicsSimpleWorldCollisionDesc>& Descs)
{
	FKawaiiPhysicsSimpleWorldCollisionDesc Merged;
	if (Descs.IsEmpty())
	{
		return Merged;
	}

	Merged = Descs[0];
	Merged.ObjectTypes.Reset();

	bool bAllGatherRadiusOverridden = true;
	float MaxGatherRadiusOverride = 0.0f;
	bool bHasEmptyObjectTypes = false;

	for (const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc : Descs)
	{
		Merged.GatherIntervalSec = FMath::Min(Merged.GatherIntervalSec, Desc.GatherIntervalSec);

		if (Desc.GatherRadiusOverride > KINDA_SMALL_NUMBER)
		{
			MaxGatherRadiusOverride = FMath::Max(MaxGatherRadiusOverride, Desc.GatherRadiusOverride);
		}
		else
		{
			bAllGatherRadiusOverridden = false;
		}

		if (Desc.ObjectTypes.IsEmpty())
		{
			bHasEmptyObjectTypes = true;
		}
		else
		{
			for (const TEnumAsByte<EObjectTypeQuery>& ObjectType : Desc.ObjectTypes)
			{
				Merged.ObjectTypes.AddUnique(ObjectType);
			}
		}

		if (static_cast<uint8>(Desc.ConvexFallbackShape) < static_cast<uint8>(Merged.ConvexFallbackShape))
		{
			Merged.ConvexFallbackShape = Desc.ConvexFallbackShape;
		}

		if (static_cast<uint8>(Desc.SkeletalMeshCollision) > static_cast<uint8>(Merged.SkeletalMeshCollision))
		{
			Merged.SkeletalMeshCollision = Desc.SkeletalMeshCollision;
		}

		Merged.bGroundCollision = Merged.bGroundCollision || Desc.bGroundCollision;
	}

	if (bHasEmptyObjectTypes)
	{
		Merged.ObjectTypes.AddUnique(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
		Merged.ObjectTypes.AddUnique(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
	}

	// 自動半径はSkelComp Bounds依存でここでは解決できないため、1つでも自動指定があれば 0 を残す。
	// Tick側で max(自動半径, マージ済みOverride) の二段解決を行う。
	Merged.GatherRadiusOverride = bAllGatherRadiusOverridden ? MaxGatherRadiusOverride : 0.0f;

	return Merged;
}

void FKawaiiPhysicsSimpleWorldCollisionEntry::SetDesc(
	uint64 SourceID, const FKawaiiPhysicsSimpleWorldCollisionDesc& InDesc)
{
	if (SourceID == 0)
	{
		return;
	}

	FWriteScopeLock WriteLock(DescLock);
	FDescSlot& DescSlotRef = DescSlots.FindOrAdd(SourceID);
	DescSlotRef.LastReadFrame = GFrameCounter;
	if (!(DescSlotRef.Desc == InDesc))
	{
		DescSlotRef.Desc = InDesc;
		bRegatherRequested.store(true, std::memory_order_release);
	}
}

void FKawaiiPhysicsSimpleWorldCollisionEntry::RemoveDesc(uint64 SourceID)
{
	if (SourceID == 0)
	{
		return;
	}

	FWriteScopeLock WriteLock(DescLock);
	if (DescSlots.Remove(SourceID) > 0)
	{
		bRegatherRequested.store(true, std::memory_order_release);
	}
}

bool FKawaiiPhysicsSimpleWorldCollisionEntry::MarkRead(uint64 SourceID)
{
	// FDescSlotのLastReadFrameはatomicにせず、DescSlotsの構造変更と同じDescLock(write)で保護する。
	// TMap要素を値型で保持でき、期限切れ除去と読み取りマークの整合も同じロック順序で扱える。
	FWriteScopeLock WriteLock(DescLock);
	if (FDescSlot* DescSlotPtr = DescSlots.Find(SourceID))
	{
		DescSlotPtr->LastReadFrame = GFrameCounter;
		return true;
	}
	return false;
}

void FKawaiiPhysicsSimpleWorldCollisionEntry::RemoveExpiredDescs(uint64 CurrentFrame, uint64 MaxAge)
{
	FWriteScopeLock WriteLock(DescLock);
	for (auto DescIt = DescSlots.CreateIterator(); DescIt; ++DescIt)
	{
		const uint64 LastFrame = DescIt->Value.LastReadFrame;
		if ((LastFrame == 0) || (CurrentFrame - LastFrame > MaxAge))
		{
			DescIt.RemoveCurrent();
			bRegatherRequested.store(true, std::memory_order_release);
		}
	}
}

bool FKawaiiPhysicsSimpleWorldCollisionEntry::HasAnyDesc() const
{
	FReadScopeLock ReadLock(DescLock);
	return !DescSlots.IsEmpty();
}

int32 FKawaiiPhysicsSimpleWorldCollisionEntry::GetNumDescs() const
{
	FReadScopeLock ReadLock(DescLock);
	return DescSlots.Num();
}

bool FKawaiiPhysicsSimpleWorldCollisionEntry::BuildMergedDesc(
	FKawaiiPhysicsSimpleWorldCollisionDesc& OutMerged) const
{
	TArray<FKawaiiPhysicsSimpleWorldCollisionDesc> Descs;
	{
		FReadScopeLock ReadLock(DescLock);
		if (DescSlots.IsEmpty())
		{
			return false;
		}

		Descs.Reserve(DescSlots.Num());
		for (const auto& Pair : DescSlots)
		{
			Descs.Add(Pair.Value.Desc);
		}
	}

	OutMerged = FKawaiiPhysicsSimpleWorldCollisionDesc::Merge(Descs);
	return true;
}

void FKawaiiPhysicsSimpleWorldCollisionEntry::RequestRegather()
{
	bRegatherRequested.store(true, std::memory_order_release);
}

bool FKawaiiPhysicsSimpleWorldCollisionEntry::ConsumeRegatherRequested()
{
	return bRegatherRequested.exchange(false, std::memory_order_acq_rel);
}

// -------------------------------------------------------------------
// UKawaiiPhysicsSharedCollisionSubsystem
// -------------------------------------------------------------------

bool UKawaiiPhysicsSharedCollisionSubsystem::TryResolveRegistryKey(
	AActor* Actor, const FGameplayTag& Tag, FRegistryKey& OutKey)
{
	if (!Actor || !Tag.IsValid())
	{
		return false;
	}

	// アタッチ階層を毎回辿り直すことで、ランタイムのアタッチ変更にも追従する（read-only）
	AActor* FamilyRoot = GetFamilyRoot(Actor);
	if (!FamilyRoot)
	{
		return false;
	}

	OutKey = FRegistryKey(FamilyRoot, Tag);
	return true;
}

TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindEntryByKey(
	const FRegistryKey& Key) const
{
	FReadScopeLock ReadLock(RegistryLock);
	if (const TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>* Found = Registry.Find(Key))
	{
		// Actorが無効ならスキップ（Tick()で定期的にクリーンアップ）
		if (Key.Key.IsValid())
		{
			return *Found;
		}
	}
	return nullptr;
}

TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindOrCreateEntry(
	AActor* Actor, const FGameplayTag& Tag)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_FindOrCreateEntry);

	FRegistryKey Key;
	if (!TryResolveRegistryKey(Actor, Tag, Key))
	{
		return nullptr;
	}

	// 既存Entryの検索（読み取りロック）
	if (TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> Existing = FindEntryByKey(Key))
	{
		return Existing;
	}

	// 構造変更は書き込みロック。ロック取得待ちの間に他スレッドが作成済みの可能性があるため再確認
	FWriteScopeLock WriteLock(RegistryLock);
	if (TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>* Existing = Registry.Find(Key))
	{
		return *Existing;
	}
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> NewEntry = MakeShared<FKawaiiPhysicsSharedCollisionEntry>();
	Registry.Add(Key, NewEntry);
	return NewEntry;
}

TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindOrCreateSimpleWorldEntry(
	TWeakObjectPtr<const USkeletalMeshComponent> SkelComp, uint64 SourceID,
	const FKawaiiPhysicsSimpleWorldCollisionDesc& InitialDesc)
{
	if (!SkelComp.IsValid(false, true))
	{
		return nullptr;
	}

	// Entry 作成と初回 Desc 登録は同一ロック内。cleanup は同ロックで HasAnyDesc を見るため、空 Entry が観測される瞬間が無い。
	FWriteScopeLock WriteLock(SimpleWorldRegistryLock);
	if (TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>* Existing = SimpleWorldRegistry.Find(SkelComp))
	{
		(*Existing)->SetDesc(SourceID, InitialDesc);
		return *Existing;
	}
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> NewEntry = MakeShared<FKawaiiPhysicsSimpleWorldCollisionEntry>();
	SimpleWorldRegistry.Add(SkelComp, NewEntry);
	NewEntry->SetDesc(SourceID, InitialDesc);
	return NewEntry;
}

TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindEntry(
	AActor* Actor, const FGameplayTag& Tag) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_FindEntry);

	FRegistryKey Key;
	if (!TryResolveRegistryKey(Actor, Tag, Key))
	{
		return nullptr;
	}
	return FindEntryByKey(Key);
}

void UKawaiiPhysicsSharedCollisionSubsystem::FillSimpleWorldCollisionDebugInfo(
	const FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
	FKawaiiPhysicsSimpleWorldCollisionDebugInfo& OutInfo)
{
	OutInfo = FKawaiiPhysicsSimpleWorldCollisionDebugInfo();
	OutInfo.bHasEntry = true;
	OutInfo.NumDescs = Entry.GetNumDescs();
	OutInfo.NumGatheredComponents = Entry.GatheredComponents.Num();
	OutInfo.bHasGroundBox = Entry.bHasGroundBox;
	OutInfo.GroundSource = Entry.GroundSource;
	OutInfo.GroundBoxSource = Entry.GroundBoxSource;
	OutInfo.GroundBoxLocation = Entry.GroundBox.Location;
	OutInfo.GroundBoxRotation = Entry.GroundBox.Rotation.Rotator();
	OutInfo.GroundBoxExtent = Entry.GroundBox.Extent;
	OutInfo.GatherRadius = Entry.LastGatherRadius;
	OutInfo.TimeSinceLastGather = Entry.TimeSinceLastGather == FLT_MAX ? -1.0f : Entry.TimeSinceLastGather;
	OutInfo.bHasGatheredOnce = Entry.bHasGatheredOnce;

	OutInfo.GatheredComponentNames.Reserve(Entry.GatheredComponents.Num());
	for (const FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& GatheredComponent : Entry.GatheredComponents)
	{
		if (GatheredComponent.bStatic)
		{
			++OutInfo.NumStaticComponents;
		}
		else
		{
			++OutInfo.NumMovableComponents;
		}

		OutInfo.NumSkeletalBodies += GatheredComponent.BodyBindings.Num();
		OutInfo.MinFadeAlpha = FMath::Min(OutInfo.MinFadeAlpha, GatheredComponent.FadeAlpha);

		FString ComponentName;
		if (const UPrimitiveComponent* Component = GatheredComponent.Component.Get())
		{
			const AActor* OwnerActor = Component->GetOwner();
			const FString OwnerName = OwnerActor ? OwnerActor->GetActorNameOrLabel() : FString(TEXT("<no owner>"));
			ComponentName = FString::Printf(TEXT("%s:%s"), *OwnerName, *Component->GetName());
		}
		else
		{
			ComponentName = TEXT("<invalid>");
		}

		if (GatheredComponent.InstanceIndex != INDEX_NONE)
		{
			ComponentName += FString::Printf(TEXT("[%d]"), GatheredComponent.InstanceIndex);
		}
		OutInfo.GatheredComponentNames.Add(MoveTemp(ComponentName));
	}
}

bool UKawaiiPhysicsSharedCollisionSubsystem::BuildSimpleWorldCollisionDebugInfo(
	const USkeletalMeshComponent* SkelComp,
	FKawaiiPhysicsSimpleWorldCollisionDebugInfo& OutInfo) const
{
	OutInfo = FKawaiiPhysicsSimpleWorldCollisionDebugInfo();

#if !UE_BUILD_SHIPPING
	if (!ensure(IsInGameThread()) || !SkelComp)
	{
		return false;
	}

	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry;
	{
		FReadScopeLock ReadLock(SimpleWorldRegistryLock);
		if (const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>* Found =
			SimpleWorldRegistry.Find(TWeakObjectPtr<const USkeletalMeshComponent>(SkelComp)))
		{
			Entry = *Found;
		}
	}

	if (!Entry.IsValid())
	{
		return false;
	}

	FillSimpleWorldCollisionDebugInfo(*Entry, OutInfo);
	return true;
#else
	return false;
#endif
}

void UKawaiiPhysicsSharedCollisionSubsystem::TickSimpleWorldCollision(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!CVarSimpleWorldCollisionEnable.GetValueOnGameThread())
	{
		// CVarでの全体無効化。収集・毎フレーム更新・Publishのすべてを止める。
		return;
	}

	if (World->GetNetMode() == NM_DedicatedServer && !CVarSimpleWorldCollisionForceEnableOnServer.GetValueOnGameThread())
	{
		// 見た目専用機能のためDedicated Serverでは既定で収集しない（ForceEnableOnServer CVarで上書き可能）。
		return;
	}

	struct FSimpleWorldTickEntry
	{
		TWeakObjectPtr<const USkeletalMeshComponent> SkelComp;
		TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> Entry;
	};

	TArray<FSimpleWorldTickEntry> Entries;
	{
		FReadScopeLock ReadLock(SimpleWorldRegistryLock);
		Entries.Reserve(SimpleWorldRegistry.Num());
		for (const auto& Pair : SimpleWorldRegistry)
		{
			FSimpleWorldTickEntry TickEntry;
			TickEntry.SkelComp = Pair.Key;
			TickEntry.Entry = Pair.Value;
			Entries.Add(MoveTemp(TickEntry));
		}
	}

	const uint64 CurrentFrame = GFrameCounter;
	// Descの未読エイジアウト判定もSimpleWorld専用のCleanupMaxAge CVarを使う（従来はSharedCollision用を流用していた）
	const int32 ReadMaxAge = CVarSimpleWorldCollisionCleanupMaxAge.GetValueOnGameThread();

	const UKawaiiPhysicsDeveloperSettings* KawaiiSettings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	const float GatherIntervalScale = FMath::Max(0.0f, CVarSimpleWorldCollisionGatherIntervalScale.GetValueOnGameThread());
	const int32 MaxComponentsCVarValue = CVarSimpleWorldCollisionMaxComponents.GetValueOnGameThread();
	const int32 EffectiveMaxGatheredComponents = MaxComponentsCVarValue >= 0
		? MaxComponentsCVarValue
		: KawaiiSettings->SimpleWorldCollisionMaxGatheredComponents;
	const int32 MaxPhysicsAssetBodiesCVarValue = CVarSimpleWorldCollisionMaxPhysicsAssetBodies.GetValueOnGameThread();
	const int32 EffectiveMaxPhysicsAssetBodies = MaxPhysicsAssetBodiesCVarValue >= 0
		? MaxPhysicsAssetBodiesCVarValue
		: KawaiiSettings->SimpleWorldCollisionMaxPhysicsAssetBodies;
	const int32 RegatherOnScaleChangeCVarValue = CVarSimpleWorldCollisionRegatherOnScaleChange.GetValueOnGameThread();
	const bool bRegatherOnScaleChange = RegatherOnScaleChangeCVarValue >= 0
		? RegatherOnScaleChangeCVarValue != 0
		: KawaiiSettings->bSimpleWorldCollisionRegatherOnScaleChange;
	const bool bUseMovementGround = CVarSimpleWorldCollisionUseMovementGround.GetValueOnGameThread() != 0;
#if ENABLE_DRAW_DEBUG
	const bool bDebugDraw = CVarSimpleWorldCollisionDebugDraw.GetValueOnGameThread() != 0;
#endif

	int32 TotalGatheredComponents = 0;
	int32 TotalSkeletalBodies = 0;
	for (const FSimpleWorldTickEntry& TickEntry : Entries)
	{
		const TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>& Entry = TickEntry.Entry;
		if (!Entry.IsValid())
		{
			continue;
		}

		Entry->RemoveExpiredDescs(CurrentFrame, ReadMaxAge);

		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		if (!Entry->BuildMergedDesc(Desc))
		{
			continue;
		}

		const USkeletalMeshComponent* SkelComp = TickEntry.SkelComp.Get();
		if (!SkelComp)
		{
			continue;
		}

		if (Entry->ConsumeRegatherRequested())
		{
			Entry->GatheredComponents.Reset();
			Entry->bHasGroundBox = false;
			Entry->GroundSource = EKawaiiPhysicsSimpleWorldGroundSource::None;
			Entry->GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::None;
			Entry->GroundProvider.Reset();
			Entry->GroundCharacterMovement.Reset();
			Entry->GroundComponent.Reset();
			Entry->bHasGatheredOnce = false;
			Entry->TimeSinceLastGather = FLT_MAX;
			Entry->bWorldLimitsDirty = true;
		}

		const FVector Center = SkelComp->Bounds.Origin;

		// 収集半径はゲート/デバッグ描画の両方で使うため、bShouldGather判定より前に確定させる
		const float AutoRadius = SkelComp->Bounds.SphereRadius * KawaiiSettings->SimpleWorldCollisionAutoGatherRadiusScale;
		const float Radius = Desc.GatherRadiusOverride > KINDA_SMALL_NUMBER
			? FMath::Max(AutoRadius, Desc.GatherRadiusOverride)
			: AutoRadius;

		bool bAllowGather = true;
		float EffectiveGatherInterval = Desc.GatherIntervalSec * GatherIntervalScale;
		if (const APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
			{
				const float DistanceToCamera = FVector::Dist(CameraManager->GetCameraLocation(), Center);
				if (DistanceToCamera > KawaiiSettings->SimpleWorldCollisionDistanceThrottleStop)
				{
					bAllowGather = false;
				}
				else if (DistanceToCamera > KawaiiSettings->SimpleWorldCollisionDistanceThrottleStart && EffectiveGatherInterval > KINDA_SMALL_NUMBER)
				{
					EffectiveGatherInterval *= 2.0f;
				}
			}
		}

		Entry->TimeSinceLastGather += DeltaTime;
		const bool bGatherEveryFrame = Desc.GatherIntervalSec <= KINDA_SMALL_NUMBER;
		const bool bShouldGather = bAllowGather
			&& (bGatherEveryFrame || Entry->TimeSinceLastGather >= EffectiveGatherInterval);

		if (bShouldGather)
		{
			SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimpleWorldCollision_Gather);
			Entry->LastGatherRadius = Radius;

			const FCollisionObjectQueryParams ObjectQueryParams = BuildSimpleWorldObjectQueryParams(Desc.ObjectTypes);
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(KawaiiPhysicsSimpleWorldCollision), false);
			QueryParams.AddIgnoredActor(SkelComp->GetOwner());

			Entry->OverlapScratch.Reset();
			World->OverlapMultiByObjectType(
				Entry->OverlapScratch,
				Center,
				FQuat::Identity,
				ObjectQueryParams,
				FCollisionShape::MakeSphere(Radius),
				QueryParams);

			TSet<FKawaiiPhysicsSimpleWorldGatherKey> UniqueGatherKeys;
			TArray<FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent> NewGatheredComponents;
			NewGatheredComponents.Reserve(
				FMath::Max(0, FMath::Min(Entry->OverlapScratch.Num(), EffectiveMaxGatheredComponents)));

			for (const FOverlapResult& Overlap : Entry->OverlapScratch)
			{
				if (NewGatheredComponents.Num() >= EffectiveMaxGatheredComponents)
				{
					break;
				}

				UPrimitiveComponent* Component = Overlap.GetComponent();
				if (!Component)
				{
					continue;
				}

				const AActor* ComponentOwner = Component->GetOwner();
				if (ComponentOwner && ComponentOwner->ActorHasTag(GSimpleWorldIgnoreTagName))
				{
					continue;
				}

				int32 InstanceIndex = INDEX_NONE;
				FTransform ComponentTM = GetScaleStrippedComponentTransform(*Component);
				FVector Scale3D = Component->GetComponentScale();
				if (const UInstancedStaticMeshComponent* ISMComponent = Cast<UInstancedStaticMeshComponent>(Component))
				{
					// FOverlapResult::GetItemIndexは5.7で追加されたため、それ以前は公開メンバのItemIndexを直接読む。
#if UE_VERSION_OLDER_THAN(5, 7, 0)
					const int32 OverlapItemIndex = Overlap.ItemIndex;
#else
					const int32 OverlapItemIndex = Overlap.GetItemIndex();
#endif
					if (OverlapItemIndex < 0 || OverlapItemIndex >= ISMComponent->GetInstanceCount())
					{
						continue;
					}

					FTransform InstanceTM;
					if (!ISMComponent->GetInstanceTransform(OverlapItemIndex, InstanceTM, true))
					{
						continue;
					}

					InstanceIndex = OverlapItemIndex;
					ComponentTM = GetScaleStrippedKawaiiPhysicsSimpleWorldInstanceTransform(InstanceTM);
					Scale3D = InstanceTM.GetScale3D();
				}

				const FKawaiiPhysicsSimpleWorldGatherKey GatherKey(Component, InstanceIndex);
				if (UniqueGatherKeys.Contains(GatherKey))
				{
					continue;
				}
				UniqueGatherKeys.Add(GatherKey);

				FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent NewComponent;
				NewComponent.Component = Component;
				NewComponent.InstanceIndex = InstanceIndex;
				// USceneComponent::GetMobilityは5.6で追加されたため、それ以前は公開UPROPERTYのMobilityを直接読む。
#if UE_VERSION_OLDER_THAN(5, 6, 0)
				NewComponent.bStatic = (Component->Mobility == EComponentMobility::Static);
#else
				NewComponent.bStatic = (Component->GetMobility() == EComponentMobility::Static);
#endif
				NewComponent.FadeAlpha = Entry->bHasGatheredOnce ? 0.0f : 1.0f;
				NewComponent.GatheredScale3D = Scale3D;

				if (const FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent* ExistingComponent =
					Entry->GatheredComponents.FindByPredicate(
						[Component, InstanceIndex](
							const FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& Candidate)
						{
							return Candidate.Component.Get() == Component && Candidate.InstanceIndex == InstanceIndex;
						}))
				{
					NewComponent.FadeAlpha = ExistingComponent->FadeAlpha;
				}

				NewComponent.LastComponentTM = ComponentTM;
				if (!BuildLocalLimitsForSimpleWorldComponent(
						*Component,
						NewComponent.LastComponentTM,
						Scale3D,
						Desc,
						EffectiveMaxPhysicsAssetBodies,
						NewComponent.LocalLimits,
						NewComponent.BodyBindings))
				{
					continue;
				}

				if (!NewComponent.BodyBindings.IsEmpty())
				{
					const USkeletalMeshComponent* GatheredSkelComp = Cast<const USkeletalMeshComponent>(Component);
					NewComponent.SkeletalComponent = GatheredSkelComp;
					NewComponent.GatheredSkinnedAsset = GatheredSkelComp ? GatheredSkelComp->GetSkinnedAsset() : nullptr;
					NewComponent.GatheredPhysicsAsset = GatheredSkelComp ? GatheredSkelComp->GetPhysicsAsset() : nullptr;
					NewComponent.bStatic = false;
				}

				NewGatheredComponents.Add(MoveTemp(NewComponent));
			}

			Entry->GatheredComponents = MoveTemp(NewGatheredComponents);
			ResolveSimpleWorldGroundSource(*Entry, *SkelComp, bUseMovementGround);

			if (Desc.bGroundCollision)
			{
				SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimpleWorldCollision_Ground);
				bool bGroundChanged = false;
				if (!TryUpdateSimpleWorldGroundBoxFromSource(*Entry, *SkelComp, Radius, bGroundChanged))
				{
					FHitResult Hit;
					const bool bHitGround = World->LineTraceSingleByObjectType(
						Hit,
						Center,
						Center - FVector(0.0f, 0.0f, Radius),
						ObjectQueryParams,
						QueryParams);

					FBoxLimit NewGroundBox;
					if (bHitGround
						&& KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
							Hit.ImpactPoint, Hit.ImpactNormal, Radius, NewGroundBox))
					{
						Entry->GroundBox = NewGroundBox;
						Entry->bHasGroundBox = true;
						Entry->GroundComponent = Hit.GetComponent();
						Entry->GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::Trace;
					}
					else
					{
						Entry->bHasGroundBox = false;
						Entry->GroundComponent.Reset();
						Entry->GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::None;
					}
				}
			}
			else
			{
				Entry->bHasGroundBox = false;
				Entry->GroundComponent.Reset();
				Entry->GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::None;
			}

			Entry->bHasGatheredOnce = true;
			Entry->bWorldLimitsDirty = true;
			Entry->TimeSinceLastGather = 0.0f;
		}
		else if (bAllowGather
			&& Desc.bGroundCollision
			&& (Entry->GroundSource == EKawaiiPhysicsSimpleWorldGroundSource::Provider
				|| Entry->GroundSource == EKawaiiPhysicsSimpleWorldGroundSource::CharacterMovement))
		{
			SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimpleWorldCollision_Ground);
			bool bGroundChanged = false;
			if (TryUpdateSimpleWorldGroundBoxFromSource(*Entry, *SkelComp, Radius, bGroundChanged) && bGroundChanged)
			{
				Entry->bWorldLimitsDirty = true;
			}
		}

		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimpleWorldCollision_UpdateTransforms);
		bool bDirty = Entry->bWorldLimitsDirty;
		for (auto ComponentIt = Entry->GatheredComponents.CreateIterator(); ComponentIt; ++ComponentIt)
		{
			const UPrimitiveComponent* Component = ComponentIt->Component.Get();
			if (!Component)
			{
				ComponentIt.RemoveCurrent();
				bDirty = true;
				continue;
			}

			if (ComponentIt->FadeAlpha < 1.0f)
			{
				ComponentIt->FadeAlpha = KawaiiSettings->SimpleWorldCollisionFadeInTime > KINDA_SMALL_NUMBER
					? FMath::Min(1.0f, ComponentIt->FadeAlpha + DeltaTime / KawaiiSettings->SimpleWorldCollisionFadeInTime)
					: 1.0f;
				bDirty = true;
			}

			if (!ComponentIt->BodyBindings.IsEmpty())
			{
				const USkeletalMeshComponent* SkeletalComponent = ComponentIt->SkeletalComponent.Get();
				if (!SkeletalComponent)
				{
					ComponentIt.RemoveCurrent();
					bDirty = true;
					continue;
				}

				// SkinnedAsset / PhysicsAsset の差し替え、および（opt-in 時の）コンポーネントスケール変化は
				// 焼き込み済み Limit と食い違うため次 Tick で再収集する
				if (SkeletalComponent->GetSkinnedAsset() != ComponentIt->GatheredSkinnedAsset.Get()
					|| SkeletalComponent->GetPhysicsAsset() != ComponentIt->GatheredPhysicsAsset.Get()
					|| (bRegatherOnScaleChange
						&& !SkeletalComponent->GetComponentScale().Equals(ComponentIt->GatheredScale3D, KINDA_SMALL_NUMBER)))
				{
					ComponentIt.RemoveCurrent();
					Entry->TimeSinceLastGather = FLT_MAX;
					bDirty = true;
					continue;
				}

				// 相手SkelCompのPostAnimEvaluationとSubsystem Tickの順序次第で、ここで読むポーズは1フレーム前の可能性がある。
				// Targetノード側ではPublish/Readの位相差も含めて最大2フレーム遅延し得る。
				const TArray<FTransform>& ComponentSpaceTransforms = SkeletalComponent->GetComponentSpaceTransforms();
				const int32 NumMissingBones = KawaiiPhysicsSimpleWorldCollision::UpdateSkeletalBodyWorldTransforms(
					MakeArrayView(ComponentIt->BodyBindings),
					MakeArrayView(ComponentSpaceTransforms),
					SkeletalComponent->GetComponentTransform(),
					ComponentIt->BodyWorldTMScratch);
				if (NumMissingBones > 0)
				{
					ComponentIt.RemoveCurrent();
					Entry->TimeSinceLastGather = FLT_MAX;
					bDirty = true;
					continue;
				}

				bool bBodyTransformsDirty = ComponentIt->LastBodyWorldTMs.Num() != ComponentIt->BodyWorldTMScratch.Num();
				if (!bBodyTransformsDirty)
				{
					for (int32 BodyIndex = 0; BodyIndex < ComponentIt->LastBodyWorldTMs.Num(); ++BodyIndex)
					{
						if (!ComponentIt->LastBodyWorldTMs[BodyIndex].Equals(
							ComponentIt->BodyWorldTMScratch[BodyIndex], KINDA_SMALL_NUMBER))
						{
							bBodyTransformsDirty = true;
							break;
						}
					}
				}

				if (bBodyTransformsDirty)
				{
					Swap(ComponentIt->LastBodyWorldTMs, ComponentIt->BodyWorldTMScratch);
					bDirty = true;
				}
				continue;
			}

			if (!ComponentIt->bStatic)
			{
				FTransform CurrentComponentTM = GetScaleStrippedComponentTransform(*Component);
				FVector CurrentScale3D = Component->GetComponentScale();
				if (ComponentIt->InstanceIndex != INDEX_NONE)
				{
					const UInstancedStaticMeshComponent* ISMComponent =
						Cast<const UInstancedStaticMeshComponent>(Component);
					if (!ISMComponent
						|| ComponentIt->InstanceIndex < 0
						|| ComponentIt->InstanceIndex >= ISMComponent->GetInstanceCount())
					{
						ComponentIt.RemoveCurrent();
						Entry->TimeSinceLastGather = FLT_MAX;
						bDirty = true;
						continue;
					}

					FTransform InstanceTM;
					if (!ISMComponent->GetInstanceTransform(ComponentIt->InstanceIndex, InstanceTM, true))
					{
						ComponentIt.RemoveCurrent();
						Entry->TimeSinceLastGather = FLT_MAX;
						bDirty = true;
						continue;
					}
					CurrentComponentTM = GetScaleStrippedKawaiiPhysicsSimpleWorldInstanceTransform(InstanceTM);
					CurrentScale3D = InstanceTM.GetScale3D();
				}

				// opt-in 時、スケール変化は焼き込み済み Limit と食い違うため次 Tick で再収集する
				if (bRegatherOnScaleChange && !CurrentScale3D.Equals(ComponentIt->GatheredScale3D, KINDA_SMALL_NUMBER))
				{
					ComponentIt.RemoveCurrent();
					Entry->TimeSinceLastGather = FLT_MAX;
					bDirty = true;
					continue;
				}

				if (!ComponentIt->LastComponentTM.Equals(CurrentComponentTM, KINDA_SMALL_NUMBER))
				{
					ComponentIt->LastComponentTM = CurrentComponentTM;
					bDirty = true;
				}
			}
		}

		if (bDirty)
		{
			Entry->PublishScratch.Reset();
			for (const FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& Component : Entry->GatheredComponents)
			{
				// フェード計算本体は KawaiiPhysicsSimpleWorldCollision 名前空間へ移設済み（単体テスト可能化のため）。
				// しきい値定数はここ（Subsystem側）で保持したまま引数として渡す。
				if (!Component.BodyBindings.IsEmpty())
				{
					KawaiiPhysicsSimpleWorldCollision::AppendFadedSkeletalLocalLimits(
						Component.LocalLimits,
						MakeArrayView(Component.BodyBindings),
						MakeArrayView(Component.LastBodyWorldTMs),
						Component.FadeAlpha,
						Entry->PublishScratch,
						GSimpleWorldFadeBoxEnableThreshold);
				}
				else
				{
					KawaiiPhysicsSimpleWorldCollision::AppendFadedLocalLimits(
						Component.LocalLimits,
						Component.FadeAlpha,
						Component.LastComponentTM,
						Entry->PublishScratch,
						GSimpleWorldFadeBoxEnableThreshold);
				}
			}

			if (Entry->bHasGroundBox)
			{
				Entry->PublishScratch.BoxLimits.Add(Entry->GroundBox);
			}

			Entry->Slot.Publish(Entry->PublishScratch);
			Entry->bWorldLimitsDirty = false;
		}

#if ENABLE_DRAW_DEBUG
		if (bDebugDraw)
		{
			DrawSimpleWorldCollisionDebug(*World, Center, Radius, *Entry);
		}
#endif

		TotalGatheredComponents += Entry->GatheredComponents.Num();
		for (const FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& GatheredComponent :
			Entry->GatheredComponents)
		{
			TotalSkeletalBodies += GatheredComponent.BodyBindings.Num();
		}
	}

	// DWORDカウンタは毎フレームリセットされるため、CleanupゲートではなくSimpleWorldのTickごとに更新する。
	SET_DWORD_STAT(STAT_KawaiiPhysics_SimpleWorldCollision_NumGatheredComponents, TotalGatheredComponents);
	SET_DWORD_STAT(STAT_KawaiiPhysics_SimpleWorldCollision_NumSkeletalBodies, TotalSkeletalBodies);
}

void UKawaiiPhysicsSharedCollisionSubsystem::Deinitialize()
{
	{
		FWriteScopeLock WriteLock(RegistryLock);
		Registry.Empty();
	}
	{
		FWriteScopeLock WriteLock(SimpleWorldRegistryLock);
		SimpleWorldRegistry.Empty();
	}
	Super::Deinitialize();
}

void UKawaiiPhysicsSharedCollisionSubsystem::Tick(float DeltaTime)
{
	TickSimpleWorldCollision(DeltaTime);

	CleanupAccumulator += DeltaTime;
	if (CleanupAccumulator < CVarSharedCollisionCleanupInterval.GetValueOnGameThread())
	{
		return;
	}
	CleanupAccumulator = 0.0f;
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_Tick);

	const uint64 CurrentFrame = GFrameCounter;

	{
		// Registryの構造変更とWorkerスレッドのFind/FindOrCreateの競合を防ぐため書き込みロックで保護。
		// ロック順序は Registry → Slots（Entryメソッドが内部でSlotsLockを取る）。
		FWriteScopeLock WriteLock(RegistryLock);

		for (auto It = Registry.CreateIterator(); It; ++It)
		{
			// Actorが無効 → エントリ除去
			if (!It->Key.Key.IsValid())
			{
				It.RemoveCurrent();
				continue;
			}

			// 期限切れスロットを除去
			FKawaiiPhysicsSharedCollisionEntry& Entry = *It->Value;
			Entry.RemoveExpiredSlots(CurrentFrame, CVarSharedCollisionCleanupMaxAge.GetValueOnGameThread());

			// スロットが空になったエントリも除去
			if (Entry.IsEmpty())
			{
				It.RemoveCurrent();
			}
		}

		// 整数カウンタ更新
		int32 TotalSlots = 0;
		for (const auto& Pair : Registry)
		{
			TotalSlots += Pair.Value->GetSlotCount();
		}
		SET_DWORD_STAT(STAT_KawaiiPhysics_SharedCollision_NumEntries, Registry.Num());
		SET_DWORD_STAT(STAT_KawaiiPhysics_SharedCollision_NumSlots, TotalSlots);
	}

	{
		FWriteScopeLock SimpleWorldWriteLock(SimpleWorldRegistryLock);
		const int32 SimpleWorldCleanupMaxAge = CVarSimpleWorldCollisionCleanupMaxAge.GetValueOnGameThread();

		for (auto It = SimpleWorldRegistry.CreateIterator(); It; ++It)
		{
			if (!It->Key.IsValid() || !It->Value.IsValid())
			{
				It.RemoveCurrent();
				continue;
			}

			It->Value->RemoveExpiredDescs(CurrentFrame, SimpleWorldCleanupMaxAge);
			if (!It->Value->HasAnyDesc())
			{
				It.RemoveCurrent();
				continue;
			}
		}
	}
}

bool UKawaiiPhysicsSharedCollisionSubsystem::IsTickable() const
{
	// WorkerスレッドからRegistryを変更しうるため、空判定も読み取りロックで保護する
	{
		FReadScopeLock ReadLock(RegistryLock);
		if (!Registry.IsEmpty())
		{
			return true;
		}
	}
	{
		FReadScopeLock ReadLock(SimpleWorldRegistryLock);
		return !SimpleWorldRegistry.IsEmpty();
	}
}

TStatId UKawaiiPhysicsSharedCollisionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKawaiiPhysicsSharedCollisionSubsystem, STATGROUP_Tickables);
}
