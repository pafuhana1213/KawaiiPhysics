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

	bool BuildLocalLimitsForSimpleWorldComponent(
		UPrimitiveComponent& Component,
		const FTransform& ComponentTM,
		const FVector& Scale3D,
		const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc,
		int32 MaxPhysicsAssetBodies,
		int32 MaxConvexPlanes,
		bool bBuildConvexDebugGeometry,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits,
		TArray<KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding>& OutBodyBindings);

	EKawaiiPhysicsSimpleWorldSkeletalBuildResult BuildSkeletalLocalLimitsForSimpleWorldComponent(
		const USkeletalMeshComponent& SkelComp,
		const FVector& Scale3D,
		const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc,
		int32 MaxPhysicsAssetBodies,
		int32 MaxConvexPlanes,
		bool bBuildConvexDebugGeometry,
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
			MaxConvexPlanes,
			bBuildConvexDebugGeometry,
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
		int32 MaxConvexPlanes,
		bool bBuildConvexDebugGeometry,
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
						*SkelComp,
						Scale3D,
						Desc,
						MaxPhysicsAssetBodies,
						MaxConvexPlanes,
						bBuildConvexDebugGeometry,
						OutLocalLimits,
						OutBodyBindings);
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
		if (BodySetup
			&& BodySetup->GetCollisionTraceFlag() != CTF_UseComplexAsSimple
			&& !IsSimpleWorldAggGeomEmpty(BodySetup->AggGeom))
		{
			// Complex-as-Simple はエンジンが simple 形状を使わないため収集しない。床はトレース経路が担当。
			KawaiiPhysicsSimpleWorldCollision::ConvertAggGeomToLocalLimits(
				BodySetup->AggGeom,
				Scale3D,
				Desc.ConvexFallbackShape,
				MaxConvexPlanes,
				bBuildConvexDebugGeometry,
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

	bool IsSimpleWorldComponentStatic(const UPrimitiveComponent& Component)
	{
		// USceneComponent::GetMobilityは5.6で追加されたため、それ以前は公開UPROPERTYのMobilityを直接読む。
#if UE_VERSION_OLDER_THAN(5, 6, 0)
		return Component.Mobility == EComponentMobility::Static;
#else
		return Component.GetMobility() == EComponentMobility::Static;
#endif
	}

	bool TryGetSimpleWorldGroundFloorTransform(
		const UPrimitiveComponent& FloorComponent,
		int32 InstanceIndex,
		FTransform& OutFloorTM)
	{
		OutFloorTM = GetScaleStrippedComponentTransform(FloorComponent);
		if (InstanceIndex == INDEX_NONE)
		{
			return true;
		}

		const UInstancedStaticMeshComponent* ISMComponent = Cast<const UInstancedStaticMeshComponent>(&FloorComponent);
		if (!ISMComponent
			|| InstanceIndex < 0
			|| InstanceIndex >= ISMComponent->GetInstanceCount())
		{
			return false;
		}

		FTransform InstanceTM;
		if (!ISMComponent->GetInstanceTransform(InstanceIndex, InstanceTM, true))
		{
			return false;
		}

		OutFloorTM = GetScaleStrippedKawaiiPhysicsSimpleWorldInstanceTransform(InstanceTM);
		return true;
	}

	void InitializeSimpleWorldTraceGroundFloor(
		const UPrimitiveComponent& FloorComponent,
		int32 HitItem,
		FTransform& OutFloorTM,
		int32& OutInstanceIndex,
		bool& bOutStatic)
	{
		OutFloorTM = GetScaleStrippedComponentTransform(FloorComponent);
		OutInstanceIndex = INDEX_NONE;
		bOutStatic = IsSimpleWorldComponentStatic(FloorComponent);

		if (const UInstancedStaticMeshComponent* ISMComponent = Cast<const UInstancedStaticMeshComponent>(&FloorComponent))
		{
			if (HitItem >= 0 && HitItem < ISMComponent->GetInstanceCount())
			{
				FTransform InstanceTM;
				if (ISMComponent->GetInstanceTransform(HitItem, InstanceTM, true))
				{
					OutFloorTM = GetScaleStrippedKawaiiPhysicsSimpleWorldInstanceTransform(InstanceTM);
					OutInstanceIndex = HitItem;
				}
			}
		}
	}

	void ClearSimpleWorldGroundBox(FKawaiiPhysicsSimpleWorldCollisionEntry& Entry, bool& bOutChanged)
	{
		bOutChanged = Entry.bHasGroundBox;
		Entry.bHasGroundBox = false;
		Entry.GroundComponent.Reset();
		Entry.GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::None;
		Entry.GroundBoxLocal = FBoxLimit();
		Entry.GroundComponentTM = FTransform::Identity;
		Entry.GroundInstanceIndex = INDEX_NONE;
		Entry.bGroundComponentStatic = true;
		if (bOutChanged)
		{
			Entry.bGroundBoxDirty = true;
		}
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
			Entry.GroundBoxLocal = FBoxLimit();
			Entry.GroundComponentTM = FTransform::Identity;
			Entry.GroundInstanceIndex = INDEX_NONE;
			Entry.bGroundComponentStatic = true;
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
		Entry.GroundBoxLocal = FBoxLimit();
		Entry.GroundComponentTM = FTransform::Identity;
		Entry.GroundInstanceIndex = INDEX_NONE;
		Entry.bGroundComponentStatic = true;

		bOutChanged = !bHadGroundBox
			|| !IsSimpleWorldGroundBoxNearlyEqual(PreviousBox, Entry.GroundBox)
			|| PreviousGroundComponent != NewGroundComponent;
		if (bOutChanged)
		{
			Entry.bGroundBoxDirty = true;
		}
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
			if (Hit.bNoGround)
			{
				ClearSimpleWorldGroundBox(Entry, bOutChanged);
				return true;
			}
			else if (Hit.bHit)
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
			if (CharacterMovement->IsFalling())
			{
				// 落下中は CharacterMovement/Provider 由来の旧床 Box を即座に外す（崖から落ちた時に旧床高さで髪が跳ねるのを防ぐ）。
				// 収集フレームのトレースが作った Trace 由来の Box は着地先の床なので次の収集まで残す。
				// false を返すので収集フレームでは従来どおり下方向トレースへフォールバックする。
				if (Entry.GroundBoxSource != EKawaiiPhysicsSimpleWorldGroundSource::Trace)
				{
					ClearSimpleWorldGroundBox(Entry, bOutChanged);
				}
				return false;
			}
			else if (CharacterMovement->CurrentFloor.IsWalkableFloor())
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

	// Trace 由来の地面 Box を Movable な床コンポーネントの Transform に追従させる。床が消えた／インスタンスが無効なら Box を外して次 Tick で再収集する。
	// 戻り値は Publish の更新が必要か（Box の再変換またはクリアが起きたか）。
	bool UpdateSimpleWorldTraceGroundBoxFromFloor(FKawaiiPhysicsSimpleWorldCollisionEntry& Entry)
	{
		// Trace ソースの地面 Box は床が Static なら次の収集まで使い回し、Movable なら床コンポーネントの Transform に追従する（エレベーター・動く足場）。
		// Provider / CharacterMovement ソースの Box は毎 Tick 値を作り直しているので対象外。
		if (Entry.GroundBoxSource != EKawaiiPhysicsSimpleWorldGroundSource::Trace
			|| !Entry.bHasGroundBox
			|| Entry.bGroundComponentStatic)
		{
			return false;
		}

		const UPrimitiveComponent* FloorComponent = Entry.GroundComponent.Get();
		bool bFloorValid = FloorComponent != nullptr;
		FTransform CurrentFloorTM = FTransform::Identity;
		if (bFloorValid)
		{
			bFloorValid = TryGetSimpleWorldGroundFloorTransform(
				*FloorComponent,
				Entry.GroundInstanceIndex,
				CurrentFloorTM);
		}

		if (!bFloorValid)
		{
			// 床が消えた/インスタンスが無効なら Box を外し、次 Tick で再収集する。
			bool bGroundChanged = false;
			ClearSimpleWorldGroundBox(Entry, bGroundChanged);
			Entry.TimeSinceLastGather = FLT_MAX;
			return bGroundChanged;
		}

		if (!CurrentFloorTM.Equals(Entry.GroundComponentTM, KINDA_SMALL_NUMBER))
		{
			Entry.GroundBox = KawaiiPhysicsSimpleWorldCollision::TransformSimpleWorldGroundBox(
				Entry.GroundBoxLocal,
				CurrentFloorTM);
			Entry.GroundComponentTM = CurrentFloorTM;
			return true;
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
		int32 ConvexOffset,
		int32 NumConvexes,
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
		for (int32 LimitIndex = 0; LimitIndex < NumConvexes; ++LimitIndex)
		{
			const FKawaiiPhysicsConvexLimit& Limit = LocalLimits.ConvexLimits[ConvexOffset + LimitIndex];
#if !UE_BUILD_SHIPPING
			if (!Limit.LocalVertices.IsEmpty() && !Limit.LocalEdges.IsEmpty())
			{
				const FTransform LimitTransform(Limit.Rotation, Limit.Location);
				for (int32 EdgeIndex = 0; EdgeIndex + 1 < Limit.LocalEdges.Num(); EdgeIndex += 2)
				{
					const int32 IndexA = Limit.LocalEdges[EdgeIndex];
					const int32 IndexB = Limit.LocalEdges[EdgeIndex + 1];
					if (!Limit.LocalVertices.IsValidIndex(IndexA) || !Limit.LocalVertices.IsValidIndex(IndexB))
					{
						continue;
					}

					const FVector LocationAWS =
						Transform.TransformPosition(LimitTransform.TransformPosition(Limit.LocalVertices[IndexA]));
					const FVector LocationBWS =
						Transform.TransformPosition(LimitTransform.TransformPosition(Limit.LocalVertices[IndexB]));
					DrawDebugLine(&World, LocationAWS, LocationBWS, ShapeColor, false, -1.0f, DepthPriority,
						ShapeThickness);
				}
				continue;
			}
#endif

			// DebugDraw CVar を後から有効にした場合、次回収集まで頂点/エッジが空のため LocalBounds で近似表示する。
			const FVector LocationWS = Transform.TransformPosition(Limit.Location);
			const FQuat RotationWS = Transform.TransformRotation(Limit.Rotation);
			DrawDebugBox(&World, LocationWS, Limit.LocalBounds.GetExtent(), RotationWS, ShapeColor, false, -1.0f,
				DepthPriority, ShapeThickness);
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
					0,
					Component.LocalLimits.ConvexLimits.Num(),
					Component.LastComponentTM,
					ShapeColor);
				continue;
			}

			int32 SphereOffset = 0;
			int32 CapsuleOffset = 0;
			int32 TaperedCapsuleOffset = 0;
			int32 BoxOffset = 0;
			int32 ConvexOffset = 0;
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
					ConvexOffset,
					Binding.NumConvexLimits,
					Component.LastBodyWorldTMs[BodyIndex],
					ShapeColor);

				SphereOffset += Binding.NumSphericalLimits;
				CapsuleOffset += Binding.NumCapsuleLimits;
				TaperedCapsuleOffset += Binding.NumTaperedCapsuleLimits;
				BoxOffset += Binding.NumBoxLimits;
				ConvexOffset += Binding.NumConvexLimits;
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
extern TAutoConsoleVariable<int32> CVarSharedCollisionEnableInPreviewWorld;

// SimpleWorldCollision CVars（AnimNode_KawaiiPhysics.cpp で定義）
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionEnable;
extern TAutoConsoleVariable<float> CVarSimpleWorldCollisionGatherIntervalScale;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionMaxComponents;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionMaxPhysicsAssetBodies;
extern TAutoConsoleVariable<int32> CVarSimpleWorldCollisionMaxConvexPlanes;
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
	PublishSerial.fetch_add(1, std::memory_order_release);

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
	OutData.ConvexLimits.Append(Buffer.ConvexLimits);
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
		&& bGatherRadiusAllOverridden == Other.bGatherRadiusAllOverridden
		&& CollisionChannel == Other.CollisionChannel
		&& ObjectTypes == Other.ObjectTypes
		&& ConvexFallbackShape == Other.ConvexFallbackShape
		&& SkeletalMeshCollision == Other.SkeletalMeshCollision
		&& bGroundCollision == Other.bGroundCollision;
}

bool FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(
	const FKawaiiPhysicsSimpleWorldCollisionDesc& Old,
	const FKawaiiPhysicsSimpleWorldCollisionDesc& New)
{
	return Old.ObjectTypes != New.ObjectTypes
		|| Old.ConvexFallbackShape != New.ConvexFallbackShape
		|| Old.SkeletalMeshCollision != New.SkeletalMeshCollision
		|| Old.bGroundCollision != New.bGroundCollision
		|| Old.GatherRadiusOverride != New.GatherRadiusOverride
		|| Old.bGatherRadiusAllOverridden != New.bGatherRadiusAllOverridden
		|| Old.CollisionChannel != New.CollisionChannel;
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

		if (Merged.CollisionChannel == ECC_MAX && Desc.CollisionChannel != ECC_MAX)
		{
			Merged.CollisionChannel = Desc.CollisionChannel;
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

	// 自動半径はSkelComp Bounds依存でここでは解決できない。
	// 全DescがOverrideならTickはOverrideをそのまま使い、1つでも自動ならmax(自動半径, MaxOverride)の二段解決を行う。
	Merged.GatherRadiusOverride = MaxGatherRadiusOverride;
	Merged.bGatherRadiusAllOverridden = bAllGatherRadiusOverridden;

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
	const bool bIsNewSource = !DescSlots.Contains(SourceID);

	// 新規登録時のみ、同一設定のスロットが既にあるかをFindOrAddの前に調べる。
	bool bHasEquivalentDesc = false;
	if (bIsNewSource)
	{
		for (const auto& Pair : DescSlots)
		{
			if (Pair.Value.Desc == InDesc)
			{
				bHasEquivalentDesc = true;
				break;
			}
		}
	}

	FDescSlot& DescSlotRef = DescSlots.FindOrAdd(SourceID);
	if (bIsNewSource)
	{
		// 登録順は新規登録時にだけ確定する（同じノードが設定を変えても順位は維持する）。
		DescSlotRef.RegistrationOrdinal = NextDescRegistrationOrdinal++;
	}
	DescSlotRef.LastReadFrame = GFrameCounter;
	if (bIsNewSource || !(DescSlotRef.Desc == InDesc))
	{
		// 新規登録と収集内容に影響する変更だけ再収集する。
		// GatherIntervalだけの変更（ピン駆動で毎フレーム変わり得る）では収集済み形状とフェード状態を維持する。
		// 新規登録でも同一設定のノードが既に登録済みならMerge結果が変わらないため再収集しない。
		const bool bRequiresRegather = (bIsNewSource && !bHasEquivalentDesc)
			|| (!bIsNewSource
				&& FKawaiiPhysicsSimpleWorldCollisionDesc::DoesChangeRequireRegather(DescSlotRef.Desc, InDesc));
		DescSlotRef.Desc = InDesc;
		if (bRequiresRegather)
		{
			bRegatherRequested.store(true, std::memory_order_release);
		}
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
	// TMap の反復順は SourceID（ノードアドレス）のハッシュ順で非決定的なため、登録順に並べてから Merge する
	// （CollisionChannel の「最初の非 ECC_MAX を採用」を決定的にする）。
	TArray<TPair<uint64, FKawaiiPhysicsSimpleWorldCollisionDesc>> OrderedDescs;
	{
		FReadScopeLock ReadLock(DescLock);
		if (DescSlots.IsEmpty())
		{
			return false;
		}

		OrderedDescs.Reserve(DescSlots.Num());
		for (const auto& Pair : DescSlots)
		{
			OrderedDescs.Emplace(Pair.Value.RegistrationOrdinal, Pair.Value.Desc);
		}
	}

	// RegistrationOrdinal は一意なので安定ソートは不要。
	OrderedDescs.Sort([](const TPair<uint64, FKawaiiPhysicsSimpleWorldCollisionDesc>& Lhs,
	                     const TPair<uint64, FKawaiiPhysicsSimpleWorldCollisionDesc>& Rhs)
	{
		return Lhs.Key < Rhs.Key;
	});

	TArray<FKawaiiPhysicsSimpleWorldCollisionDesc> Descs;
	Descs.Reserve(OrderedDescs.Num());
	for (const TPair<uint64, FKawaiiPhysicsSimpleWorldCollisionDesc>& OrderedDesc : OrderedDescs)
	{
		Descs.Add(OrderedDesc.Value);
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
	OutInfo.bGroundComponentStatic = Entry.bGroundComponentStatic;
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

void UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldShapeLimits(
	FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
	float BoxEnableThreshold)
{
	Entry.PublishScratch.Reset();
	for (const FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent& Component : Entry.GatheredComponents)
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
				Entry.PublishScratch,
				BoxEnableThreshold);
		}
		else
		{
			KawaiiPhysicsSimpleWorldCollision::AppendFadedLocalLimits(
				Component.LocalLimits,
				Component.FadeAlpha,
				Component.LastComponentTM,
				Entry.PublishScratch,
				BoxEnableThreshold);
		}
	}

	Entry.Slot.Publish(Entry.PublishScratch);
	Entry.bWorldLimitsDirty = false;
}

void UKawaiiPhysicsSharedCollisionSubsystem::PublishSimpleWorldGroundBox(
	FKawaiiPhysicsSimpleWorldCollisionEntry& Entry)
{
	Entry.GroundPublishScratch.Reset();
	if (Entry.bHasGroundBox)
	{
		Entry.GroundPublishScratch.BoxLimits.Add(Entry.GroundBox);
	}

	Entry.GroundSlot.Publish(Entry.GroundPublishScratch);
	Entry.bGroundBoxDirty = false;
}

void UKawaiiPhysicsSharedCollisionSubsystem::GatherSimpleWorldEntry(
	UWorld& World,
	FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
	const USkeletalMeshComponent& SkelComp,
	const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc,
	const FVector& Center,
	float Radius,
	float GroundTraceLength,
	ECollisionChannel CollisionChannel,
	int32 EffectiveMaxGatheredComponents,
	int32 EffectiveMaxPhysicsAssetBodies,
	int32 EffectiveMaxConvexPlanes,
	bool bUseMovementGround,
	bool bBuildConvexDebugGeometry)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimpleWorldCollision_Gather);
	Entry.LastGatherRadius = Radius;

	const FCollisionResponseParams ResponseParams =
		KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldResponseParams(Desc.ObjectTypes);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(KawaiiPhysicsSimpleWorldCollision), false);
	// Overlap応答（トリガー等）は物理クエリのPreFilterで除外し、結果配列に返さない（ループ側のbBlockingHit判定は防御として残す）。
	QueryParams.bIgnoreTouches = true;
	QueryParams.AddIgnoredActor(SkelComp.GetOwner());
	// Owner が無いプレビューコンポーネントでも自分自身を収集しない。
	QueryParams.AddIgnoredComponent(&SkelComp);

	Entry.OverlapScratch.Reset();
	World.OverlapMultiByChannel(
		Entry.OverlapScratch,
		Center,
		FQuat::Identity,
		CollisionChannel,
		FCollisionShape::MakeSphere(Radius),
		QueryParams,
		ResponseParams);

	const bool bUseGatherOrder = KawaiiPhysicsSimpleWorldCollision::ShouldUseSimpleWorldGatherOrder(Entry.OverlapScratch.Num(), EffectiveMaxGatheredComponents);
	if (bUseGatherOrder)
	{
		Entry.GatherDistanceScratch.Reset(Entry.OverlapScratch.Num());
		for (const FOverlapResult& Overlap : Entry.OverlapScratch)
		{
			float DistanceSquared = TNumericLimits<float>::Max();
			if (const UPrimitiveComponent* Component = Overlap.GetComponent())
			{
				FVector GatherLocation = Component->Bounds.Origin;
				if (const UInstancedStaticMeshComponent* ISMComponent =
					Cast<const UInstancedStaticMeshComponent>(Component))
				{
					// FOverlapResult::GetItemIndexは5.7で追加されたため、それ以前は公開メンバのItemIndexを直接読む。
#if UE_VERSION_OLDER_THAN(5, 7, 0)
					const int32 OverlapItemIndex = Overlap.ItemIndex;
#else
					const int32 OverlapItemIndex = Overlap.GetItemIndex();
#endif
					if (OverlapItemIndex >= 0 && OverlapItemIndex < ISMComponent->GetInstanceCount())
					{
						FTransform InstanceTM;
						if (ISMComponent->GetInstanceTransform(OverlapItemIndex, InstanceTM, true))
						{
							GatherLocation = InstanceTM.GetLocation();
						}
					}
				}
				DistanceSquared = FVector::DistSquared(Center, GatherLocation);
			}
			Entry.GatherDistanceScratch.Add(DistanceSquared);
		}
		KawaiiPhysicsSimpleWorldCollision::SortSimpleWorldGatherOrderByDistance(
			MakeArrayView(Entry.GatherDistanceScratch),
			Entry.GatherOrderScratch);
	}

	TSet<FKawaiiPhysicsSimpleWorldGatherKey> UniqueGatherKeys;
	TArray<FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent> NewGatheredComponents;
	NewGatheredComponents.Reserve(
		FMath::Max(0, FMath::Min(Entry.OverlapScratch.Num(), EffectiveMaxGatheredComponents)));

	const int32 NumOverlapVisits = bUseGatherOrder ? Entry.GatherOrderScratch.Num() : Entry.OverlapScratch.Num();
	for (int32 VisitIndex = 0; VisitIndex < NumOverlapVisits; ++VisitIndex)
	{
		if (NewGatheredComponents.Num() >= EffectiveMaxGatheredComponents)
		{
			break;
		}

		const int32 OverlapIndex = bUseGatherOrder ? Entry.GatherOrderScratch[VisitIndex] : VisitIndex;
		const FOverlapResult& Overlap = Entry.OverlapScratch[OverlapIndex];
		if (!Overlap.bBlockingHit)
		{
			// レスポンスが Overlap/Ignore のコンポーネント（トリガー等）は収集対象外。
			continue;
		}

		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Component)
		{
			continue;
		}

		const AActor* ComponentOwner = Component->GetOwner();
		if ((ComponentOwner && ComponentOwner->ActorHasTag(GSimpleWorldIgnoreTagName))
			|| Component->ComponentHasTag(GSimpleWorldIgnoreTagName))
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
		NewComponent.bStatic = IsSimpleWorldComponentStatic(*Component);
		NewComponent.FadeAlpha = Entry.bHasGatheredOnce ? 0.0f : 1.0f;
		NewComponent.GatheredScale3D = Scale3D;

		if (const FKawaiiPhysicsSimpleWorldCollisionEntry::FGatheredComponent* ExistingComponent =
			Entry.GatheredComponents.FindByPredicate(
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
				EffectiveMaxConvexPlanes,
				bBuildConvexDebugGeometry,
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

	Entry.GatheredComponents = MoveTemp(NewGatheredComponents);
	ResolveSimpleWorldGroundSource(Entry, SkelComp, bUseMovementGround);

	if (Desc.bGroundCollision)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimpleWorldCollision_Ground);
		bool bGroundChanged = false;
		if (!TryUpdateSimpleWorldGroundBoxFromSource(Entry, SkelComp, Radius, bGroundChanged))
		{
			FHitResult Hit;
			const bool bHitGround = World.LineTraceSingleByChannel(
				Hit,
				Center,
				Center - FVector(0.0f, 0.0f, GroundTraceLength),
				CollisionChannel,
				QueryParams,
				ResponseParams);

			FBoxLimit NewGroundBox;
			if (bHitGround
				&& KawaiiPhysicsSimpleWorldCollision::BuildSimpleWorldGroundBox(
					Hit.ImpactPoint, Hit.ImpactNormal, Radius, NewGroundBox))
			{
				Entry.GroundBox = NewGroundBox;
				Entry.bHasGroundBox = true;
				Entry.GroundComponent = Hit.GetComponent();
				Entry.GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::Trace;
				Entry.GroundComponentTM = FTransform::Identity;
				Entry.GroundInstanceIndex = INDEX_NONE;
				Entry.bGroundComponentStatic = true;

				if (const UPrimitiveComponent* GroundHitComponent = Hit.GetComponent())
				{
					InitializeSimpleWorldTraceGroundFloor(
						*GroundHitComponent,
						Hit.Item,
						Entry.GroundComponentTM,
						Entry.GroundInstanceIndex,
						Entry.bGroundComponentStatic);
					Entry.GroundBoxLocal =
						KawaiiPhysicsSimpleWorldCollision::MakeSimpleWorldGroundBoxLocal(
							Entry.GroundBox,
							Entry.GroundComponentTM);
				}
			}
			else
			{
				ClearSimpleWorldGroundBox(Entry, bGroundChanged);
			}
		}
	}
	else
	{
		bool bGroundChanged = false;
		ClearSimpleWorldGroundBox(Entry, bGroundChanged);
	}

	Entry.bHasGatheredOnce = true;
	Entry.bWorldLimitsDirty = true;
	Entry.bGroundBoxDirty = true;
	Entry.TimeSinceLastGather = 0.0f;
}

void UKawaiiPhysicsSharedCollisionSubsystem::UpdateSimpleWorldGround(
	FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
	const USkeletalMeshComponent& SkelComp,
	const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc,
	float Radius,
	bool bGatherInputValid)
{
	if (!Desc.bGroundCollision)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimpleWorldCollision_Ground);
	// Provider / CharacterMovement の再取得は収集中心・半径が有効なフレームだけ行う。
	// Trace ソースの床追従（F14）は Center/Radius を使わないため、入力が不正な間も動き続ける。
	if (bGatherInputValid
		&& (Entry.GroundSource == EKawaiiPhysicsSimpleWorldGroundSource::Provider
			|| Entry.GroundSource == EKawaiiPhysicsSimpleWorldGroundSource::CharacterMovement))
	{
		bool bGroundChanged = false;
		const bool bGroundBoxUpdated =
			TryUpdateSimpleWorldGroundBoxFromSource(Entry, SkelComp, Radius, bGroundChanged);
		if (!bGroundChanged && !bGroundBoxUpdated)
		{
			// Provider / CharacterMovement が床を返さない間（落下中など）は、収集フレームのトレースが作った Trace 由来の Box を動く床に追従させる。
			if (UpdateSimpleWorldTraceGroundBoxFromFloor(Entry))
			{
				Entry.bGroundBoxDirty = true;
			}
		}
		return;
	}

	if (UpdateSimpleWorldTraceGroundBoxFromFloor(Entry))
	{
		Entry.bGroundBoxDirty = true;
	}
}

bool UKawaiiPhysicsSharedCollisionSubsystem::UpdateSimpleWorldTransforms(
	FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
	float DeltaTime,
	bool bRegatherOnScaleChange,
	float FadeInTime)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimpleWorldCollision_UpdateTransforms);
	bool bDirty = Entry.bWorldLimitsDirty;
	for (auto ComponentIt = Entry.GatheredComponents.CreateIterator(); ComponentIt; ++ComponentIt)
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
			ComponentIt->FadeAlpha = FadeInTime > KINDA_SMALL_NUMBER
				? FMath::Min(1.0f, ComponentIt->FadeAlpha + DeltaTime / FadeInTime)
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
				Entry.TimeSinceLastGather = FLT_MAX;
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
				Entry.TimeSinceLastGather = FLT_MAX;
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
					Entry.TimeSinceLastGather = FLT_MAX;
					bDirty = true;
					continue;
				}

				FTransform InstanceTM;
				if (!ISMComponent->GetInstanceTransform(ComponentIt->InstanceIndex, InstanceTM, true))
				{
					ComponentIt.RemoveCurrent();
					Entry.TimeSinceLastGather = FLT_MAX;
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
				Entry.TimeSinceLastGather = FLT_MAX;
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
	return bDirty;
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
	const int32 MaxConvexPlanesCVarValue = CVarSimpleWorldCollisionMaxConvexPlanes.GetValueOnGameThread();
	const int32 EffectiveMaxConvexPlanes = MaxConvexPlanesCVarValue >= 0
		? MaxConvexPlanesCVarValue
		: KawaiiSettings->SimpleWorldCollisionMaxConvexPlanes;
	const int32 RegatherOnScaleChangeCVarValue = CVarSimpleWorldCollisionRegatherOnScaleChange.GetValueOnGameThread();
	const bool bRegatherOnScaleChange = RegatherOnScaleChangeCVarValue >= 0
		? RegatherOnScaleChangeCVarValue != 0
		: KawaiiSettings->bSimpleWorldCollisionRegatherOnScaleChange;
	const bool bUseMovementGround = CVarSimpleWorldCollisionUseMovementGround.GetValueOnGameThread() != 0;
	const bool bDebugDraw = CVarSimpleWorldCollisionDebugDraw.GetValueOnGameThread() != 0;
#if ENABLE_DRAW_DEBUG
	const bool bDrawSimpleWorldDebug = bDebugDraw;
#endif
	// Convex のデバッグ頂点/エッジは DebugDraw 有効時だけ構築し、通常時の転送用コピーを避ける。
	const bool bBuildConvexDebugGeometry = bDebugDraw;

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

		// 再収集要求は Desc スナップショットを取る前に消費する。消費後に届いた SetDesc は次 Tick で再収集され、
		// 消費前の変更はこの Tick のスナップショットに含まれるため、変更が取りこぼされる窓が無い。
		if (Entry->ConsumeRegatherRequested())
		{
			Entry->GatheredComponents.Reset();
			bool bGroundChanged = false;
			ClearSimpleWorldGroundBox(*Entry, bGroundChanged);
			Entry->GroundSource = EKawaiiPhysicsSimpleWorldGroundSource::None;
			Entry->GroundProvider.Reset();
			Entry->GroundCharacterMovement.Reset();
			Entry->bHasGatheredOnce = false;
			Entry->TimeSinceLastGather = FLT_MAX;
			Entry->bWorldLimitsDirty = true;
			Entry->bGroundBoxDirty = true;
		}

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

		const FVector Center = SkelComp->Bounds.Origin;

		// 収集半径はゲート/デバッグ描画の両方で使うため、bShouldGather判定より前に確定させる
		const float AutoRadius = SkelComp->Bounds.SphereRadius * KawaiiSettings->SimpleWorldCollisionAutoGatherRadiusScale;
		// 全ノードがOverride指定ならOverrideをそのまま使う（自動半径より小さくできる）。1つでも自動指定があれば自動半径を下限にする。
		const float Radius = Desc.bGatherRadiusAllOverridden && Desc.GatherRadiusOverride > KINDA_SMALL_NUMBER
			? Desc.GatherRadiusOverride
			: (Desc.GatherRadiusOverride > KINDA_SMALL_NUMBER ? FMath::Max(AutoRadius, Desc.GatherRadiusOverride) : AutoRadius);
		// 地面トレースの長さは収集半径の縮小に連動させない（Bounds原点から床までの距離は半径Overrideと無関係なため）。
		// 地面BoxのXY半径はOverrideどおり。
		const float GroundTraceLength = FMath::Max(Radius, AutoRadius);
		const bool bGatherInputValid = KawaiiPhysicsSimpleWorldCollision::IsSimpleWorldGatherInputValid(Center, Radius);

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
		const bool bShouldGather = bGatherInputValid
			&& bAllowGather
			&& (bGatherEveryFrame || Entry->TimeSinceLastGather >= EffectiveGatherInterval);

		if (bShouldGather)
		{
			const ECollisionChannel CollisionChannel = Desc.CollisionChannel != ECC_MAX
				? Desc.CollisionChannel.GetValue()
				: SkelComp->GetCollisionObjectType();
			GatherSimpleWorldEntry(
				*World,
				*Entry,
				*SkelComp,
				Desc,
				Center,
				Radius,
				GroundTraceLength,
				CollisionChannel,
				EffectiveMaxGatheredComponents,
				EffectiveMaxPhysicsAssetBodies,
				EffectiveMaxConvexPlanes,
				bUseMovementGround,
				bBuildConvexDebugGeometry);
		}
		else if (bAllowGather)
		{
			UpdateSimpleWorldGround(*Entry, *SkelComp, Desc, Radius, bGatherInputValid);
		}

		const bool bDirty = UpdateSimpleWorldTransforms(
			*Entry,
			DeltaTime,
			bRegatherOnScaleChange,
			KawaiiSettings->SimpleWorldCollisionFadeInTime);

		if (bDirty)
		{
			PublishSimpleWorldShapeLimits(*Entry, GSimpleWorldFadeBoxEnableThreshold);
		}
		if (Entry->bGroundBoxDirty)
		{
			PublishSimpleWorldGroundBox(*Entry);
		}

#if ENABLE_DRAW_DEBUG
		if (bDrawSimpleWorldDebug)
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

bool UKawaiiPhysicsSharedCollisionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Persona等のプレビューワールドでもSubsystemを生成し、ABP上でSharedCollision/SimpleWorldCollisionをプレビューできるようにする。
	// GamePreview（ゲーム側FPreviewScene）とInactiveは従来どおり対象外。
	if (WorldType == EWorldType::EditorPreview)
	{
		return CVarSharedCollisionEnableInPreviewWorld.GetValueOnGameThread() != 0;
	}
	return Super::DoesSupportWorldType(WorldType);
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
