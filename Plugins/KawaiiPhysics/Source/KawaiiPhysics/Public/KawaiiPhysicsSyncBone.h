#pragma once

#include "BoneContainer.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "KawaiiPhysicsSyncBone.generated.h"

struct FAnimNode_KawaiiPhysics;
struct FKawaiiPhysicsModifyBone;

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
struct KAWAIIPHYSICS_API FKawaiiPhysicsSyncTarget
{
	GENERATED_BODY()

public:
	virtual ~FKawaiiPhysicsSyncTarget() = default;

	FKawaiiPhysicsSyncTarget(const int32 InModifyBoneIndex = -1)
	{
		ModifyBoneIndex = InModifyBoneIndex;
	}
	
	bool operator==(const FKawaiiPhysicsSyncTarget& Other) const
	{
		return ModifyBoneIndex == Other.ModifyBoneIndex;
	}

	virtual bool IsValid(const FBoneContainer& BoneContainer) const
	{
		return ModifyBoneIndex >= 0;
	}

	// TargetRootからの長さ割合に応じてScaleを更新（X: LengthRate、Y: Scale）
	// Update Scale based on length rate from TargetRoot (X: LengthRate, Y: Scale)
	void UpdateScaleByLengthRate(const FRichCurve* ScaleCurveByBoneLengthRate);

	// SyncBoneによる移動処理をボーンに適用
	// Apply the translation from SyncBone to the bone
	void Apply(TArray<FKawaiiPhysicsModifyBone>& ModifyBones, const FVector& Translation);

#if WITH_EDITOR
	void DebugDraw(FPrimitiveDrawInterface* PDI, const FAnimNode_KawaiiPhysics* Node) const;
#endif

#if WITH_EDITORONLY_DATA

	// プレビュー用のボーンを表示するか
	// Whether to show the preview bone
	UPROPERTY(VisibleAnywhere, Category = "SyncTarget", meta=(EditCondition="false", EditConditionHides))
	bool IsShowPreviewBone = true;

	// エディタ上でのプレビュー用
	// For preview in the editor
	UPROPERTY(VisibleAnywhere, Category = "SyncTarget",
		meta=(EditCondition="IsShowPreviewBone", EditConditionHides))
	FBoneReference PreviewBone;

	// SyncBoneにより最終的に受けた移動量
	// Final translation received from SyncBone
	UPROPERTY(VisibleAnywhere, Category = "SyncTarget")
	FVector TranslationBySyncBone = FVector::ZeroVector;
#endif

	// 移動を適用する度合い
	// Degree to apply translation (how much movement is applied)
	UPROPERTY(VisibleAnywhere, Category = "SyncTarget")
	float ScaleByLengthRateCurve = 1.0f;

	// SyncTargetRootからの長さ割合
	// Length rate from TargetRoot for the target bone
	UPROPERTY(VisibleAnywhere, Category = "SyncTarget")
	float LengthRateFromSyncTargetRoot = 0.0f;

	// 適応対象のボーンのModifyBoneにおけるIndex
	// Index in ModifyBone for the target bone
	UPROPERTY()
	int32 ModifyBoneIndex = -1;
};

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSyncTargetRoot : public FKawaiiPhysicsSyncTarget
{
	GENERATED_BODY()

	FKawaiiPhysicsSyncTargetRoot()
		: Super()
	{
#if WITH_EDITORONLY_DATA
		IsShowPreviewBone = false;
#endif
	}

	virtual bool IsValid(const FBoneContainer& BoneContainer) const override
	{
		return Bone.IsValidToEvaluate(BoneContainer);
	}

	// 適用対象のボーン
	// Target bone to apply to
	UPROPERTY(EditAnywhere, Category = "SyncTarget")
	FBoneReference Bone;

	// このボーンの子ボーンもすべて対象にするか
	// Whether to include all child bones of this bone
	UPROPERTY(EditAnywhere, Category = "SyncTarget")
	bool bIncludeChildBones = true;

	// TargetRootからの長さ割合に応じてSyncBoneの影響度にスケールを適応（X: LengthRate、Y: Scale）
	// Curve that scales SyncBone's influence based on length rate from TargetRoot (X: Length, Y: Scale)
	UPROPERTY(EditAnywhere, Category = "SyncBone", meta=(XAxisName="LengthRate", YAxisName="Scale"))
	FRuntimeFloatCurve ScaleCurveByBoneLengthRate;

	// 適用対象である子ボーン
	// Child bones that are targets for application
	UPROPERTY(VisibleAnywhere, Category = "SyncBone", DisplayName="ChildTargets(Preview)", meta=(TitleProperty="PreviewBone"))
	TArray<FKawaiiPhysicsSyncTarget> ChildTargets;
};

// SyncBone：同期元のボーンの移動・回転を物理制御下のボーンに適用します。
// スカートが足などを貫通するのを防ぐのに役立ちます 
// Applies the movement and rotation of the sync source bone to the bone under physics control. 
// Helps prevent skirts from penetrating legs, etc.
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSyncBone
{
	GENERATED_BODY()

	// 同期元のボーン
	// Source bone to sync from
	UPROPERTY(EditAnywhere, Category = "SyncBone")
	FBoneReference Bone;

	// 適用対象のボーンと適用度
	// Target bones and their application alpha
	UPROPERTY(EditAnywhere, Category = "SyncBone", meta=(TitleProperty="{Bone}"))
	TArray<FKawaiiPhysicsSyncTargetRoot> TargetRoots;

	// 全体に適用される移動の度合い
	// Overall translation application amount
	UPROPERTY(EditAnywhere, Category = "SyncBone",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector GlobalScale = FVector::OneVector;
	
	// SyncBoneの移動距離に応じて
	// 各Targetに対しての補正処理にスケールをかけるカーブ（X: 移動距離、Y: スケール）
	// Curve that scales correction for each Target based on SyncBone's movement distance (X: Distance, Y: Scale)
	UPROPERTY(EditAnywhere, Category = "SyncBone", meta=(XAxisName="Distance", YAxisName="Scale"))
	FRuntimeFloatCurve ScaleCurveByDeltaDistance;

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

	// SyncBoneとの距離に応じてTargetへの適用量(Alpha)を減衰させる設定
	// Attenuation of application amount(Alpha) based on distance from SyncBone to each Target
	UPROPERTY(EditAnywhere, Category = "SyncBone|Distance Attenuation", meta=(InlineEditConditionToggle))
	bool bEnableDistanceAttenuation = false;

	// 内半径：この距離以内は減衰しません
	// Inner radius: no attenuation within this radius
	UPROPERTY(EditAnywhere, Category = "SyncBone|Distance Attenuation",
		meta=(EditCondition="bEnableDistanceAttenuation", ClampMin="0.0", UIMin = "0.0", Units="cm"))
	float AttenuationInnerRadius = 0.0f;

	// 外半径：この距離以上は最大減衰量がかかります
	// Outer radius: maximum attenuation is applied outside this radius
	UPROPERTY(EditAnywhere, Category = "SyncBone|Distance Attenuation",
		meta=(EditCondition="bEnableDistanceAttenuation", ClampMin="0.0", UIMin = "0.0", Units="cm"))
	float AttenuationOuterRadius = 0.0f;

	// 最大減衰量(0以上)：0=減衰なし, 1=減衰MAX（SyncBoneの適用量が0になる）
	// Max attenuation amount (>=0): 0=no attenuation, 1=max attenuation (SyncBone application amount becomes 0)
	UPROPERTY(EditAnywhere, Category = "SyncBone|Distance Attenuation",
		meta=(EditCondition="bEnableDistanceAttenuation", ClampMin="0.0", ClampMax="1.0", UIMin = "0.0", UIMax = "1.0"))
	float MaxAttenuationRate = 1.0f;

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
