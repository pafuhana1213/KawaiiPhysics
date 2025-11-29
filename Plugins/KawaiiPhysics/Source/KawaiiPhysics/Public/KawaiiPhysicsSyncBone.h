#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "Curves/CurveVector.h"
#include "KawaiiPhysicsSyncBone.generated.h"

UENUM(BlueprintType)
enum class ESyncBoneDirection : uint8
{
	/** 両方の方向の移動を適用 */
	Both,
	/** 正の方向の移動のみを適用 */
	Positive,
	/** 負の方向の移動のみを適用 */
	Negative,
	/** 移動を適用しない */
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

	/** 適用対象のボーン */
	UPROPERTY(EditAnywhere, Category = "SyncTarget")
	FBoneReference Bone;

	/** このボーンの子ボーンもすべて対象にするか */
	UPROPERTY(EditAnywhere, Category = "SyncTarget")
	bool bIncludeChildBones = true;

	/** 移動を適用する度合い */
	UPROPERTY(EditAnywhere, Category = "SyncTarget", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector Alpha = FVector::OneVector;

	// 適応対象のボーンのModifyBoneにおけるIndex
	UPROPERTY()
	int32 ModifyBoneIndex = -1;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FVector TransitionBySyncBone = FVector::ZeroVector;
#endif
};


USTRUCT(BlueprintType)
struct FKawaiiPhysicsSyncBone
{
	GENERATED_BODY()

	/** 同期元のボーン */
	UPROPERTY(EditAnywhere, Category = "SyncBone")
	FBoneReference Bone;

	/** 適用対象のボーンと適用度 */
	UPROPERTY(EditAnywhere, Category = "SyncBone", meta=(TitleProperty="{Bone}"))
	TArray<FKawaiiPhysicsSyncTarget> Targets;

	/** 全体に適用される移動の度合い */
	UPROPERTY(EditAnywhere, Category = "SyncBone",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector GlobalAlpha = FVector::OneVector;

	// SyncBoneの移動距離に応じて
	// 各Targetに対しての補正処理にスケールをかけるカーブ
	UPROPERTY(EditAnywhere, Category = "SyncBone", meta=(XAxisName="Distance", YAxisName="Scale"))
	FRuntimeFloatCurve DeltaDistanceScaleCurve;
	
	/** X軸の移動を適用する方向 */
	UPROPERTY(EditAnywhere, Category = "SyncBone")
	ESyncBoneDirection ApplyDirectionX = ESyncBoneDirection::Both;

	/** Y軸の移動を適用する方向 */
	UPROPERTY(EditAnywhere, Category = "SyncBone")
	ESyncBoneDirection ApplyDirectionY = ESyncBoneDirection::Both;

	/** Z軸の移動を適用する方向 */
	UPROPERTY(EditAnywhere, Category = "SyncBone")
	ESyncBoneDirection ApplyDirectionZ = ESyncBoneDirection::Both;

	// SyncBoneの初期座標
	UPROPERTY()
	FVector InitialPoseLocation = FVector::ZeroVector;

#if WITH_EDITORONLY_DATA
	// SyncBoneの移動距離
	UPROPERTY()
	FVector DeltaDistance = FVector::ZeroVector;
	// SyncBoneの移動距離(Alpha, Scale計算後)
	UPROPERTY()
	FVector ScaledDeltaDistance = FVector::ZeroVector;
#endif
};
