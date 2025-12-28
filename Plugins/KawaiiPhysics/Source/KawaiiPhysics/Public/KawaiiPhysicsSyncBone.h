#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveFloat.h"
#include "KawaiiPhysicsSyncBone.generated.h"

UENUM(BlueprintType)
enum class ESyncBoneDirection : uint8
{
	// 両方の方向の移動を適用
	// Apply movement in both directions
	Both,
	// 正の方向の移動のみを適用
	// Apply movement only in the positive direction
	Positive,
	// 負の方向の移動のみを適用
	// Apply movement only in the negative direction
	Negative,
	// 移動を適用しない
	// Do not apply movement
	None,
};

USTRUCT(BlueprintType)
struct FKawaiiPhysicsSyncTarget
{
	GENERATED_BODY()

	FKawaiiPhysicsSyncTarget()
	{
	}

	FKawaiiPhysicsSyncTarget(const FBoneReference& InBone,
	                         const FVector& InAlpha,
	                         const bool bInIncludeChildBones = false,
	                         const int32 InModifyBoneIndex = -1)
	{
		Bone = InBone;
		Alpha = InAlpha;
		bIncludeChildBones = bInIncludeChildBones;
		ModifyBoneIndex = InModifyBoneIndex;
	}
	
	bool operator==(const FKawaiiPhysicsSyncTarget& Other) const
	{
		return ModifyBoneIndex == Other.ModifyBoneIndex;
	}

	// 適用対象のボーン
	// Target bone to apply to
	UPROPERTY(EditAnywhere, Category = "SyncTarget")
	FBoneReference Bone;

	// このボーンの子ボーンもすべて対象にするか
	// Whether to include all child bones of this bone
	UPROPERTY(EditAnywhere, Category = "SyncTarget")
	bool bIncludeChildBones = true;

	// 移動を適用する度合い
	// Degree to apply translation (how much movement is applied)
	UPROPERTY(EditAnywhere, Category = "SyncTarget", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector Alpha = FVector::OneVector;

	// 適応対象のボーンのModifyBoneにおけるIndex
	// Index in ModifyBone for the target bone
	UPROPERTY()
	int32 ModifyBoneIndex = -1;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FVector TransitionBySyncBone = FVector::ZeroVector;
#endif
};

// SyncBone：同期元のボーンの移動・回転を物理制御下のボーンに適用します。
// スカートが足などを貫通するのを防ぐのに役立ちます 
// Applies the movement and rotation of the sync source bone to the bone under physics control. 
// Helps prevent skirts from penetrating legs, etc.
USTRUCT(BlueprintType)
struct FKawaiiPhysicsSyncBone
{
	GENERATED_BODY()

	// 同期元のボーン
	// Source bone to sync from
	UPROPERTY(EditAnywhere, Category = "SyncBone")
	FBoneReference Bone;

	// 適用対象のボーンと適用度
	// Target bones and their application alpha
	UPROPERTY(EditAnywhere, Category = "SyncBone", meta=(TitleProperty="{Bone}"))
	TArray<FKawaiiPhysicsSyncTarget> Targets;

	// 全体に適用される移動の度合い
	// Overall translation application amount
	UPROPERTY(EditAnywhere, Category = "SyncBone",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector GlobalAlpha = FVector::OneVector;
	
	// SyncBoneの移動距離に応じて
	// 各Targetに対しての補正処理にスケールをかけるカーブ（X: 移動距離、Y: スケール）
	// Curve that scales correction for each Target based on SyncBone's movement distance (X: Distance, Y: Scale)
	UPROPERTY(EditAnywhere, Category = "SyncBone", meta=(XAxisName="Distance", YAxisName="Scale"))
	FRuntimeFloatCurve DeltaDistanceScaleCurve;

	// X軸の移動を適用する方向
	// Direction to apply movement on the X axis
	UPROPERTY(EditAnywhere, Category = "SyncBone")
	ESyncBoneDirection ApplyDirectionX = ESyncBoneDirection::Both;

	// Y軸の移動を適用する方向
	// Direction to apply movement on the Y axis
	UPROPERTY(EditAnywhere, Category = "SyncBone")
	ESyncBoneDirection ApplyDirectionY = ESyncBoneDirection::Both;

	// Z軸の移動を適用する方向
	// Direction to apply movement on the Z axis
	UPROPERTY(EditAnywhere, Category = "SyncBone")
	ESyncBoneDirection ApplyDirectionZ = ESyncBoneDirection::Both;
	
	// SyncBoneの初期座標
	// Initial pose location of the SyncBone
	UPROPERTY()
	FVector InitialPoseLocation = FVector::ZeroVector;

#if WITH_EDITORONLY_DATA
	
	// SyncBoneの移動距離
	// SyncBone movement distance
	UPROPERTY()
	FVector DeltaDistance = FVector::ZeroVector;
	
	// SyncBoneの移動距離(Alpha, Scale計算後)
	// SyncBone movement distance (after Alpha and Scale calculations)
	UPROPERTY()
	FVector ScaledDeltaDistance = FVector::ZeroVector;
#endif
};
