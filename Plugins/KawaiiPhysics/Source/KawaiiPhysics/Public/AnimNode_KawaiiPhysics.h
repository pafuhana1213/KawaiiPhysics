// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Misc/EngineVersionComparison.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "GameplayTagContainer.h"

#include "BoneControllers/AnimNode_AnimDynamics.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"  
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodyInstance.h"
#endif

#include "KawaiiPhysicsSyncBone.h"
#include "KawaiiPhysicsCollisionLimits.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsTypes.h"
#include "KawaiiPhysicsBoneConstraintTypes.h"
#include "AnimNode_KawaiiPhysics.generated.h"

class UKawaiiPhysics_CustomExternalForce;
class UKawaiiPhysicsLimitsDataAsset;
class UKawaiiPhysicsBoneConstraintsDataAsset;

#if ENABLE_ANIM_DEBUG
extern KAWAIIPHYSICS_API TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsEnable;
extern KAWAIIPHYSICS_API TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsDebug;
extern KAWAIIPHYSICS_API TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsDebugLengthRate;
extern KAWAIIPHYSICS_API TAutoConsoleVariable<float> CVarAnimNodeKawaiiPhysicsDebugDrawThickness;
#endif

extern KAWAIIPHYSICS_API TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsUseBoneContainerRefSkeletonWhenInit;

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FAnimNode_KawaiiPhysics : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	/** 
	* 指定ボーンとそれ以下のボーンを制御対象に
	* Control the specified bone and the bones below it
	*/
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference RootBone;

	/** 
	* 指定したボーンとそれ以下のボーンを制御対象から除去
	* Do NOT control the specified bone and the bones below it
	*/
	UPROPERTY(EditAnywhere, Category = "Bones")
	TArray<FBoneReference> ExcludeBones;

	/** 
	* 指定ボーンとそれ以下のボーンを制御対象に(追加用)
	* Control the specified bone and the bones below it (For Addition)
	*/
	UPROPERTY(EditAnywhere, Category = "Bones", meta=(TitleProperty="RootBone"))
	TArray<FKawaiiPhysicsRootBoneSetting> AdditionalRootBones;

	/** 
	* 0より大きい場合は、制御ボーンの末端にダミーボーンを追加。ダミーボーンを追加することで、末端のボーンの物理制御を改善
	* Add a dummy bone to the end bone if it's above 0. It affects end bone rotation. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones", meta = (PinHiddenByDefault, ClampMin = "0"))
	float DummyBoneLength = 0.0f;

	/**
	* 隣接するボーン間に挿入するダミーボーンの分割数。コリジョン検出の精度を向上させる（例: スカートの足貫通防止）
	* Number of DummyBone subdivisions to insert between adjacent physics bones.
	* Improves collision detection (e.g., prevents skirts from penetrating legs).
	* Set to 0 to disable.
	* CollisionOnly=false のときのみボーン間隔と半径で数が自動補正される（重なり不安定化の防止）。CollisionOnly=true は指定数をそのまま配置。
	* Auto-corrected by bone spacing and radius only when CollisionOnly is false; placed as-is when CollisionOnly is true.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones|Bone Subdivision", meta = (PinHiddenByDefault, ClampMin = "0", ClampMax = "10"))
	int32 BoneSubdivisionCount = 0;

	/**
	* ボーン間ダミーボーンの速度積分（重力・風など）をスキップし、実ボーン間の補間位置からコリジョン・制約に参加
	* When true, inter-bone dummy bones skip velocity integration (gravity/wind/etc.) and still participate in collision and constraints from interpolated positions.
	* true=半径間引きせず指定数をそのまま配置 / false=重なり不安定化を防ぐため半径で配置数を間引く
	* true: places the requested count as-is (no radius culling). false: culls the count by radius to avoid overlap instability.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones|Bone Subdivision",
		meta = (PinHiddenByDefault, EditCondition = "BoneSubdivisionCount > 0"))
	bool bBoneSubdivisionCollisionOnly = true;

	/**
	* 横方向BoneConstraintに沿って挿入するコリジョン代理ダミーの分割数。隣接チェーン（列）間の隙間をコリジョン点で埋めて貫通を防ぐ。
	* Number of collision-proxy dummies to insert along each horizontal BoneConstraint. Fills horizontal gaps between adjacent chains (columns) to prevent penetration.
	* BoneSubdivisionCountと組み合わせると布面に2Dグリッド状のコリジョン点が並ぶ。0で無効。端点コリジョンが既に重なる接続(間隔<=2*半径)には挿入しない。BoneSubdivisionCountとは独立。
	* Combine with BoneSubdivisionCount for a 2D grid of collision points. 0 to disable. Skipped where the endpoint collisions already overlap (spacing <= 2*radius). Independent of BoneSubdivisionCount.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones|Bone Subdivision",
		meta = (PinHiddenByDefault, ClampMin = "0", ClampMax = "10"))
	int32 BoneConstraintSubdivisionCount = 0;

	/**
	* bridge dummyが受けたコリジョン変位を端点ボーンへ転送する強さ。0=転送なし、1=端点との距離比に応じた標準的な押し出し。
	* これにより列の間で動くコライダーが実ボーンを押し出せる（貫通防止フィードバックの本体）。剛性が強すぎる場合は下げる。
	* Strength of transferring a bridge dummy's collision displacement to its endpoint bones (0 = none, 1 = standard
	* push weighted by proximity). This is what lets a collider moving between columns actually push the real bones.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones|Bone Subdivision",
		meta = (PinHiddenByDefault, ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "BoneConstraintSubdivisionCount > 0"))
	float BoneConstraintSubdivisionFeedbackScale = 1.0f;

	/**
	* ボーンの前方。物理制御やダミーボーンの配置位置に影響
	* Bone forward direction. Affects the placement of physical controls and dummy bones
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones", meta = (PinHiddenByDefault))
	EBoneForwardAxis BoneForwardAxis = EBoneForwardAxis::X_Positive;

	/** 
	* 物理制御の基本設定
	* Basic physics settings
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, DisplayPriority=0))
	FKawaiiPhysicsSettings PhysicsSettings;

	/**
	* 物理制御を行う座標系（Component以外の場合は微小のパフォーマンス低下が発生しますが、急激なRootボーンの移動・回転の影響を回避することができます）
	* Simulation space for physics control (Using anything other than ComponentSpace may cause a slight performance drop, but it can avoid the impact of sudden Root bone movement and rotation)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	EKawaiiPhysicsSimulationSpace SimulationSpace = EKawaiiPhysicsSimulationSpace::ComponentSpace;

	/**
	* BaseBone座標系時の基準となるボーン
	* BaseBone coordinate system reference bone
	*/
	UPROPERTY(EditAnywhere, Category = "Physics Settings",
		meta = (PinHiddenByDefault, EditCondition= "SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace",
			EditConditionHides))
	FBoneReference SimulationBaseBone;

	/** 
	* ターゲットとなるフレームレート
	* Target Frame Rate
	*/
	UPROPERTY(EditAnywhere, Category = "Physics Settings", meta = (ClampMin = "1", UIMin = "1"))
	int32 TargetFramerate = 60;

	/** 
	* 物理の空回し回数。物理処理が落ち着いてから開始・表示したい際に使用
	* Number of times physics has been idle. Used when you want to start/display after physics processing has settled down
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, EditCondition="bNeedWarmUp", ClampMin = "0"))
	int32 WarmUpFrames = 0;
	/** 
	* ResetDynamics時に物理の空回しを行うフラグ
	* Flags to use warmup physics when ResetDynamics
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, EditCondition="bNeedWarmUp"))
	bool bUseWarmUpWhenResetDynamics = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bNeedWarmUp = false;

	/** 
	* 1フレームにおけるSkeletalMeshComponentの移動量が設定値を超えた場合、その移動量を物理制御に反映しない
	* If the amount of movement of a SkeletalMeshComponent in one frame exceeds the set value, that amount of movement will not be reflected in the physics control.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	float TeleportDistanceThreshold = 300.0f;

	/** 
	* 1フレームにおけるSkeletalMeshComponentの回転量が設定値を超えた場合、その回転量を物理制御に反映しない
	* If the rotation amount of SkeletalMeshComponent in one frame exceeds the set value, the rotation amount will not be reflected in the physics control.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	float TeleportRotationThreshold = 10.0f;

	/** 
	* 指定した軸に応じた平面上に各ボーンを固定
	* Fix the bone on the specified plane 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	EPlanarConstraint PlanarConstraint = EPlanarConstraint::None;

	/**
	* SkeletalMeshComponentの移動量を物理挙動に反映する際に適用されるスケール。
	* Scale to apply when reflecting the movement of the SkeletalMeshComponent in physical behavior.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault))
	FVector SkelCompMoveScale = FVector::One();

	/** 
 	* 各ボーンの物理パラメータを毎フレーム更新するフラグ。
 	* 無効にするとパフォーマンスが僅かに改善するが、実行中に物理パラメータを変更することが不可能に
	* Flag to update the physics parameters of each bone every frame.
	* Disabling this will slightly improve performance, but it will make it impossible to change physics parameters during execution.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault))
	bool bUpdatePhysicsSettingsInGame = true;

	/** 
	* 制御対象のボーンが見つからない場合にTransformをリセットするフラグ。基本的には無効を推奨
	* Flag to reset Transform when the controlled bone is not found. It is generally recommended to disable this.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault))
	bool ResetBoneTransformWhenBoneNotFound = false;

	/** 
	* 各ボーンに適用するPhysics Settings/ Damping パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/Damping parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "Damping Rate by Bone Length Rate"))
	FRuntimeFloatCurve DampingCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ Stiffness パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/Stiffness parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "Stiffness Rate by Bone Length Rate"))
	FRuntimeFloatCurve StiffnessCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ WorldDampingLocation パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/WorldDampingLocation parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	* ※基となる値の意味は WorldDampingLocation を参照（大きいほど移動量を抑制 = 反映率は 1 - 値）
	* Note: see WorldDampingLocation for the base value's meaning (higher = more suppression; reflection factor = 1 - value).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "World Damping Location Rate by Bone Length Rate"))
	FRuntimeFloatCurve WorldDampingLocationCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ WorldDampingRotation パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/WorldDampingRotation parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	* ※基となる値の意味は WorldDampingRotation を参照（大きいほど回転量を抑制 = 反映率は 1 - 値）
	* Note: see WorldDampingRotation for the base value's meaning (higher = more suppression; reflection factor = 1 - value).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "World Damping Rotation Rate by Bone Length Rate"))
	FRuntimeFloatCurve WorldDampingRotationCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ CollisionRadius パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/CollisionRadius parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "Radius Rate by Bone Length Rate"))
	FRuntimeFloatCurve RadiusCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ LimitAngle パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/LimitAngle parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "LimitAngle Rate by Bone Length Rate"))
	FRuntimeFloatCurve LimitAngleCurveData;

	/** 
	* コリジョン（球）
	* Spherical Collision
	*/
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FSphericalLimit> SphericalLimits;
	/** 
	* コリジョン（カプセル）
	* Capsule Collision
	*/
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FCapsuleLimit> CapsuleLimits;
	/** 
	* コリジョン（ボックス）
	* Box Collision
	*/
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FBoxLimit> BoxLimits;
	/** 
	* コリジョン（平面）
	* Planar Collision
	*/
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FPlanarLimit> PlanarLimits;

	/** 
	* コリジョン設定（DataAsset版）。別AnimNode・ABPで設定を流用したい場合はこちらを推奨
	* Collision settings (DataAsset version). This is recommended if you want to reuse the settings for another AnimNode or ABP.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (PinHiddenByDefault))
	TObjectPtr<UKawaiiPhysicsLimitsDataAsset> LimitsDataAsset = nullptr;

	/** 
	* コリジョン設定（PhyiscsAsset版）。別AnimNode・ABPで設定を流用したい場合はこちらを推奨
	* Collision settings (PhyiscsAsset版 version). This is recommended if you want to reuse the settings for another AnimNode or ABP.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (PinHiddenByDefault))
	TObjectPtr<UPhysicsAsset> PhysicsAssetForLimits = nullptr;

	/** 
	* コリジョン設定（DataAsset版）における球コリジョンのプレビュー
	* Preview of sphere collision in collision settings (DataAsset version)
	*/
	UPROPERTY(Transient, VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FSphericalLimit> SphericalLimitsData;
	/** 
	* コリジョン設定（DataAsset版）におけるカプセルコリジョンのプレビュー
	* Preview of capsule collision in collision settings (DataAsset version)
	*/
	UPROPERTY(Transient, VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FCapsuleLimit> CapsuleLimitsData;
	/** 
	* コリジョン設定（DataAsset版）におけるボックスコリジョンのプレビュー
	* Preview of box collision in collision settings (DataAsset version)
	*/
	UPROPERTY(Transient, VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FBoxLimit> BoxLimitsData;
	/** 
	* コリジョン設定（DataAsset版）における平面コリジョンのプレビュー
	* Preview of planar collision in collision settings (DataAsset version)
	*/
	UPROPERTY(Transient, VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FPlanarLimit> PlanarLimitsData;

	/**
	 * コリジョンを同じActor/ChildActorファミリー内のKawaiiPhysicsに共有する
	 * Provide this node's collision limits to KawaiiPhysics nodes in the same attached actor family
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits|Shared Collision", meta = (PinHiddenByDefault))
	bool bSharedCollisionSource = false;

	/**
	 * 同じActor/ChildActorファミリー内のKawaiiPhysicsから共有コリジョンを使用する
	 * 同じAnimGraph内で同一フレームの結果を使うには、SourceノードをTargetノードより先に評価される位置へ配置してください。
	 * Use shared collision limits from source KawaiiPhysics nodes in the same attached actor family.
	 * To use same-frame data in one AnimGraph, place the source node so it evaluates before the target node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits|Shared Collision",
		meta = (PinHiddenByDefault, EditCondition = "!bSharedCollisionSource"))
	bool bUseSharedCollision = false;

	/**
	 * 共有コリジョンのグループタグ（同じActor/ChildActorファミリー内のSource/Target両方で使用）
	 * Group tag for shared collision (used by both source and target in the same attached actor family)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits|Shared Collision",
		meta = (PinHiddenByDefault, EditCondition = "bSharedCollisionSource || bUseSharedCollision"))
	FGameplayTag SharedCollisionGroupTag;

	/** 共有コリジョンの再初期化を要求 / Request shared collision reinitialization */
	void RequestSharedCollisionReinit() { bSharedCollisionNeedsReinit = true; }
	/** ボーン構造に依存する設定変更後の再初期化を要求 / Request modify-bone rebuild after topology-affecting settings change */
	void RequestModifyBonesReinit() { bModifyBonesNeedsReinit = true; }

	/**
	* Bone Constraintで用いる剛性タイプ
	* Stiffness type to use in Bone Constraint
	* http://blog.mmacklin.com/2016/10/12/xpbd-slides-and-stiffness/
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint",
		meta = (PinHiddenByDefault))
	EXPBDComplianceType BoneConstraintGlobalComplianceType = EXPBDComplianceType::Leather;
	/** 
	* Bone Constraintの処理回数（コリジョン処理前）
	* Number of Bone Constraints processed before collision processing
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint",
		meta = (PinHiddenByDefault))
	int32 BoneConstraintIterationCountBeforeCollision = 1;
	/** 
	* Bone Constraintの処理回数（コリジョン処理後）
	* Number of Bone Constraints processed after collision processing
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint",
		meta = (PinHiddenByDefault))
	int32 BoneConstraintIterationCountAfterCollision = 1;
	/** 
	* 末端ボーンをBoneConstraint処理の対象にした場合、自動的にダミーボーンも処理対象にするフラグ
	* Flag to automatically processes dummy bones when the end bones are subject to BoneConstraint processing.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint",
		meta = (PinHiddenByDefault))
	bool bAutoAddChildDummyBoneConstraint = true;

	/** 
	* BoneConstraint処理の対象となるボーンのペアを設定。スカートのように、ボーン間の距離を維持したい場合に使用
	* Sets the bone pair to be processed by BoneConstraint. Used when you want to maintain the distance between bones, such as a skirt.
	*/
	UPROPERTY(EditAnywhere, Category = "Bone Constraint", meta=(TitleProperty="{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraints;

	/** 
	* BoneConstraint処理の対象となるボーンのペアを設定 (DataAsset版）。別AnimNode・ABPで設定を流用したい場合はこちらを推奨
	* Set the bone pairs to be processed by BoneConstraint (DataAsset version). If you want to reuse the settings for another AnimNode or another ABP, this is recommended.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint",
		meta = (PinHiddenByDefault))
	TObjectPtr<UKawaiiPhysicsBoneConstraintsDataAsset> BoneConstraintsDataAsset;

	/** 
	* BoneConstraint処理の対象となるボーンのペアのプレビュー
	* Preview of bone pairs that will be processed by BoneConstraint
	*/
	UPROPERTY(Transient, VisibleAnywhere, Category = "Bone Constraint", AdvancedDisplay,
		meta=(TitleProperty="{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraintsData;
	// ランタイムキャッシュ(BoneConstraints + BoneConstraintsData)。InitBoneConstraints で再構築されるため非シリアライズ。
	// Runtime cache rebuilt in InitBoneConstraints; not serialized.
	UPROPERTY(Transient)
	TArray<FModifyBoneConstraint> MergedBoneConstraints;

	/**
	* 同期元のボーンの移動・回転を物理制御下のボーンに適用します。スカートが足などを貫通するのを防ぐのに役立ちます
	* Applies the movement and rotation of the sync source bone to the bone under physics control. Helps prevent skirts from penetrating legs, etc.
	*/
	UPROPERTY(EditAnywhere, Category = "Sync Bone", meta=(TitleProperty="{Bone}"))
	TArray<FKawaiiPhysicsSyncBone> SyncBones;

	/**
	* 重力
	* Gravity
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (PinHiddenByDefault))
	FVector Gravity = FVector::ZeroVector;

	/**
	* Gravityの適用方式（レガシー互換）
	* true : 従来互換（位置に 0.5 * Gravity * dt^2 を加算）
	* false: AnimDynamics互換（速度に Gravity * dt を加算してから位置更新）
	* Gravity application method (legacy compatibility)
	* true : Legacy compatibility (add 0.5 * Gravity * dt^2 to position)
	* false: AnimDynamics compatibility (add Gravity * dt to velocity before updating position)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (PinHiddenByDefault))
	bool bUseLegacyGravity = false;

	/**
	* Gravityベクトルにプロジェクト設定の DefaultGravityZ（絶対値）を乗算する処理のフラグ
	* Flag to multiply the DefaultGravityZ (absolute value) of the project settings to the Gravity vector
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (PinHiddenByDefault))
	bool bUseDefaultGravityZProjectSetting = false;

	// 
	// 重力をワールド座標系で扱うかどうかのフラグ
	// Flag to handle gravity in world coordinate system
	//
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (PinHiddenByDefault))
	bool bUseWorldSpaceGravity = true;

	// 外力としてWindDirectionalSourceの影響を受けるかどうかのフラグ
	// Flag to receive the influence of WindDirectionalSource as an external force
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (PinHiddenByDefault))
	bool bEnableWind = false;

	/** 
	* WindDirectionalSourceによる風の影響度。ClothやSpeedTreeとの併用目的
	* Influence of wind by WindDirectionalSource. For use with Cloth and SpeedTree
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (EditCondition = "bEnableWind", PinHiddenByDefault))
	float WindScale = 1.0f;

	/** 
    * WindDirectionalSourceによる風方向に与えるノイズ（角度）
    * Noise(Degree) of wind by WindDirectionalSource. For use with Cloth and SpeedTree
    */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (EditCondition = "bEnableWind", Units = "Degrees", ClampMin=0, PinHiddenByDefault))
	float WindDirectionNoiseAngle = 0.0f;

	// 単純な外力ベクトル
	// Simple external force vector
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (PinHiddenByDefault))
	FVector SimpleExternalForce = FVector::ZeroVector;

	// 単純な外力をワールド座標系で扱うかどうかのフラグ
	// Flag to handle simple external forces in world coordinate system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (PinHiddenByDefault))
	bool bUseWorldSpaceSimpleExternalForce = true;
	
	/** 
	* 外力のプリセット。C++で独自のプリセットを追加可能(Instanced Struct)
	* External force presets. You can add your own presets in C++.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (BaseStruct = "/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct))
	TArray<FInstancedStruct> ExternalForces;

	/**
	* !!! VERY VERY EXPERIMENTAL !!!
	* 外力のプリセット。BP・C++で独自のプリセットを追加可能(Instanced Property)
	* 注意：AnimNodeをクリック or ABPをコンパイルしないと正常に動作しません
	* External force presets. You can add your own presets in BP or C++
	* Note: If you do not click on AnimNode or compile ABP, it will not work properly.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "ExternalForce",
		meta=(DisplayName="CustomExternalForces(EXPERIMENTAL)"))
	TArray<TObjectPtr<UKawaiiPhysics_CustomExternalForce>> CustomExternalForces;

	/** 
	* レベル上の各コリジョンとの判定を行うフラグ。有効にすると物理処理の負荷が大幅に上がります
	* Flag for collision detection with each collision on the level. Enabling this will significantly increase the load of physics processing.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (PinHiddenByDefault))
	bool bAllowWorldCollision = false;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bOverrideCollisionParams = false;
	/** 
	* SkeletalMeshComponentが持つコリジョン設定ではなく、独自のコリジョン設定をWorldCollisionで使用する際に設定
	* Use custom collision settings in WorldCollision instead of the collision settings set in SkeletalMeshComponent.
	*/
	UPROPERTY(EditAnywhere, Category = "World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bOverrideCollisionParams", DisplayName=
			"Override SkelComp Collision Params"))
	FBodyInstance CollisionChannelSettings;

	/** 
	* WorldCollisionにて、SkeletalMeshComponentが持つコリジョン(PhysicsAsset)を無視するフラグ
	* In WorldCollision, Flag to ignore collisions for SkeletalMeshComponent(PhysicsAsset) in WorldCollision
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bAllowWorldCollision"))
	bool bIgnoreSelfComponent = true;

	/** 
	* WorldCollisionにて、SkeletalMeshComponentが持つコリジョン(PhysicsAsset)を無視する設定（骨）
	* In WorldCollision, set to ignore collision (PhysicsAsset) of SkeletalMeshComponent using bone
	*/
	UPROPERTY(EditAnywhere, Category = "World Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FBoneReference> IgnoreBones;

	/** 
	* WorldCollisionにて、SkeletalMeshComponentが持つコリジョン(PhysicsAsset)を無視する設定（骨名のプリフィックス）
	* In WorldCollision, set to ignore collision (PhysicsAsset) of SkeletalMeshComponent using bone name prefix
	*/
	UPROPERTY(EditAnywhere, Category = "World Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FName> IgnoreBoneNamePrefix;

	/**
	* ExternalForceなどで使用するフィルタリング用タグ
	* Tag for filtering of ExternalForce etc
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag")
	FGameplayTag KawaiiPhysicsTag;

	/**
	* 物理制御対象ボーンのランタイムキャッシュ。Initialize_AnyThread で毎回再構築されるため非シリアライズ(Transient)。
	* Runtime cache of bones under physics control. Rebuilt every Initialize_AnyThread, so it is Transient (not serialized).
	*/
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Bones")
	TArray<FKawaiiPhysicsModifyBone> ModifyBones;

	UPROPERTY(BlueprintReadOnly, Category = "KawaiiPhysics")
	float DeltaTime = 0.0f;

private:
	/**
	* ボーン間のスペースとRadiusに基づき、挿入可能なダミーボーン数を計算（自動補正）
	* Calculates the effective number of inter-bone dummy bones, auto-corrected based on spacing and radius.
	*/
	int32 CalcInterBoneDummyCount(float Distance, int32 RequestedCount, float AvgRadius) const;

	/**
	* Inserts inter-bone dummy bones before recursively adding the real child bone.
	*/
	int32 InsertInterBoneDummyBones(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
	                               FComponentSpacePoseContext& Output,
	                               const FBoneContainer& BoneContainer,
	                               const FReferenceSkeleton& RefSkeleton,
	                               int32 ParentModifyBoneIndex,
	                               int32 ChildBoneIndex,
	                               TArray<int32>& OutInsertedInterBoneDummyIndices) const;

	/**
	* 親と任意の子位置の間にインターボーンダミーを挿入するコア処理（実子ボーン／末端ダミー共用）
	* Core routine that inserts inter-bone dummies between a parent and an explicit child transform.
	* Used both by the real-child path and the terminal tip-dummy path.
	* @return 最後に挿入したダミーのインデックス（0個なら ParentModifyBoneIndex） / index of the last inserted dummy (or ParentModifyBoneIndex when none)
	*/
	int32 InsertInterBoneDummyBonesCore(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
	                                    int32 ParentModifyBoneIndex,
	                                    const FVector& ChildLocation,
	                                    const FQuat& ChildRotation,
	                                    const FVector& ChildScale,
	                                    float Distance,
	                                    TArray<int32>& OutInsertedInterBoneDummyIndices) const;

	/**
	* Finalizes inter-bone dummy metadata after the real child bone has been added.
	*/
	void FinalizeInterBoneDummyBones(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
	                                 const TArray<int32>& InsertedInterBoneDummyIndices,
	                                 int32 ChildModifyBoneIndex) const;

	/**
	* Removes inserted inter-bone dummy bones when the real child bone cannot be added.
	*/
	void RollbackInterBoneDummyBones(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
	                                 int32 ParentModifyBoneIndex,
	                                 const TArray<int32>& InsertedInterBoneDummyIndices) const;

	/**
	 * Flag indicating whether the physics settings have been initialized.
	 */
	bool bInitPhysicsSettings = false;

	/**
	 * Transform of the skeletal component in last frame.
	 */
	FTransform PreSkelCompTransform;

	/**
	* Vector representing the movement of the skeletal component.
	*/
	FVector SkelCompMoveVector = FVector::ZeroVector;

	/**
	 * Quaternion representing the rotation of the skeletal component.
	 */
	FQuat SkelCompMoveRotation = FQuat::Identity;

	/**
	 * Flag indicating whether to reset or skip the dynamics.
	 */
	ETeleportType TeleportType = ETeleportType::None;

	/**
	 * Flag indicating whether to reset the dynamics.
	 */
	FVector GravityInSimSpace = FVector::ZeroVector;

	// Cached simple external force in current SimulationSpace (computed once per SimulateModifyBones)
	FVector SimpleExternalForceInSimSpace = FVector::ZeroVector;
	
	/**
	 *	 The last simulation space used for the physics simulation.
	 */
	EKawaiiPhysicsSimulationSpace LastSimulationSpace = EKawaiiPhysicsSimulationSpace::ComponentSpace;
	
	/**
	 * Previous frame's Base bone space to component space transform
	 */
	FTransform PrevBaseBoneSpace2ComponentSpace = FTransform::Identity;

	// --- Shared Collision ---
	// 共有コリジョン用キャッシュ（PreUpdateでGameThread初期化、以降はAnyThreadで参照）
	// Cached shared collision pointers (initialized in PreUpdate on GameThread, then referenced on AnyThread)
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> CachedSharedCollisionEntry;
	TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> CachedSourceSlot;
	bool bSharedCollisionInitialized = false;
	bool bSharedCollisionNeedsReinit = false;
	int32 SharedCollisionInitRetryCount = 0;
	bool bSharedCollisionInitWarningLogged = false;

	// 共有コリジョンワーク配列（シミュレーション空間に変換済み）
	// Shared collision working arrays (converted to simulation space)
	TArray<FSphericalLimit> SharedSphericalLimits;
	TArray<FCapsuleLimit> SharedCapsuleLimits;
	TArray<FBoxLimit> SharedBoxLimits;
	TArray<FPlanarLimit> SharedPlanarLimits;

	// ReadMerged結果のキャッシュ（メンバ化によりフレーム間でcapacityを再利用）
	// Cached ReadMerged result (member variable to reuse capacity across frames)
	FKawaiiPhysicsSharedCollisionData SharedCollisionMergedData;

	// --- World Collision ランタイムキャッシュ / World Collision runtime caches ---
	// IgnoreBoneNamePrefix のFString版（ホットパスでのFName::ToString回避。AdjustByWorldCollisionで遅延再構築）
	// FString versions of IgnoreBoneNamePrefix (avoids FName::ToString in the hot path; lazily rebuilt in AdjustByWorldCollision)
	TArray<FString> IgnoreBoneNamePrefixStrings;
	TArray<FName> IgnoreBoneNamePrefixCache;
	// スイープ結果のスクラッチ（フレーム間でcapacityを再利用） / Sweep-result scratch (reuses capacity across frames)
	TArray<FHitResult> WorldCollisionHitsScratch;

	/**
	* Stores the delta time from the previous frame.
	*/
	float DeltaTimeOld = 0.0f;

	// ===== 固定タイムステップ・サブステップ化（UKawaiiPhysicsDeveloperSettings 連動） =====
	// Fixed-timestep substepping (driven by UKawaiiPhysicsDeveloperSettings)
	// 未消費の実時間 / Unconsumed real time
	float SubstepAccumulator = 0.0f;
	// このフレームの実dt（DeltaTimeとは別に保持） / This frame's real dt (kept separate from DeltaTime)
	float FrameDeltaTime = 0.0f;
	// 毎フレーム1回キャッシュする設定 / Settings cached once per frame
	bool bUseFixedSubsteppingCached = false;
	int32 MaxSubstepsCached = 4;
	// サブステップ実行中フラグ＆固定dt（GetStepDeltaTime が参照） / Substep-in-progress flag & fixed dt (read by GetStepDeltaTime)
	bool bInSubstep = false;
	float StepDeltaTime = 0.0f;
	// ポーズ補間用：前フレームのポーズ目標が有効か（初回/リセット後は現在値で初期化）
	// Pose interpolation: whether the previous-frame pose target is valid (init to current on first frame / after reset)
	bool bSubstepPoseInitialized = false;

#if WITH_EDITORONLY_DATA
	bool bEditing = false;
	double LastEvaluatedTime = 0.0;
#endif

public:
	FAnimNode_KawaiiPhysics();

	// FAnimNode_Base interface
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual bool NeedsDynamicReset() const override { return true; }
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	                                               TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual bool HasPreUpdate() const override;
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_SkeletalControlBase interface

#if WITH_EDITORONLY_DATA

	bool IsRecentlyEvaluated() const
	{
		return (FPlatformTime::Seconds() - LastEvaluatedTime) < 0.1;
	}
#endif

	/**
	 * Gets the vector representing the movement of the skeletal component.
	 *
	 * @return The vector representing the movement of the skeletal component.
	 */
	const FVector& GetSkelCompMoveVector() const;

	/**
	 * Gets the quaternion representing the rotation of the skeletal component.
	 *
	 * @return The quaternion representing the rotation of the skeletal component.
	 */
	const FQuat& GetSkelCompMoveRotation() const;

	/**
	 * Gets the delta time from the previous frame.
	 *
	 * @return The delta time from the previous frame.
	 */
	float GetDeltaTimeOld() const;

	int32 GetEffectiveTargetFramerate() const
	{
		return FMath::Max(1, TargetFramerate);
	}

	/**
	 * シミュレーションで使用すべき dt を返す。サブステップ実行中は固定 dt（FixedDt）、
	 * それ以外は実フレーム dt（DeltaTime）。重力・積分・外力クラスはこれを参照する。
	 * Returns the dt the simulation should use: the fixed dt while substepping, otherwise the real
	 * frame dt. Gravity / integration / external-force classes read this instead of DeltaTime.
	 */
	float GetStepDeltaTime() const
	{
		return bInSubstep ? StepDeltaTime : DeltaTime;
	}

	/**
	 * Get Transform from BaseBoneSpace to ComponentSpace.
	 */
	FTransform GetBaseBoneSpace2ComponentSpace() const
	{
		if (SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
		{
			return CurrentEvalSimSpaceCache.TargetSpaceToComponent;
		}

		return FTransform::Identity;
	}

	// Given a bone index, get the transform in the currently selected simulation space
	FTransform GetBoneTransformInSimSpace(FComponentSpacePoseContext& Output,
	                                      const FCompactPoseBoneIndex& BoneIndex) const;

	// Convert a transform from one simulation space to another (internal cache-aware)
	FTransform ConvertSimulationSpaceTransform(FComponentSpacePoseContext& Output,
	                                           EKawaiiPhysicsSimulationSpace From,
	                                           EKawaiiPhysicsSimulationSpace To,
	                                           const FTransform& InTransform) const;

	// Convert a vector from one simulation space to another (internal cache-aware)
	FVector ConvertSimulationSpaceVector(FComponentSpacePoseContext& Output,
	                                     EKawaiiPhysicsSimulationSpace From,
	                                     EKawaiiPhysicsSimulationSpace To,
	                                     const FVector& InVector) const;

	// Convert a location from one simulation space to another (internal cache-aware)
	FVector ConvertSimulationSpaceLocation(FComponentSpacePoseContext& Output,
	                                       EKawaiiPhysicsSimulationSpace From,
	                                       EKawaiiPhysicsSimulationSpace To,
	                                       const FVector& InLocation) const;

	// Convert a rotation from one simulation space to another (internal cache-aware)
	FQuat ConvertSimulationSpaceRotation(FComponentSpacePoseContext& Output,
	                                     EKawaiiPhysicsSimulationSpace From,
	                                     EKawaiiPhysicsSimulationSpace To,
	                                     const FQuat& InRotation) const;

	void ConvertSimulationSpace(FComponentSpacePoseContext& Output,
	                            EKawaiiPhysicsSimulationSpace From,
	                            EKawaiiPhysicsSimulationSpace To);

protected:
	/**
	 * Gets the forward vector of a bone based on its rotation.
	 *
	 * @param Rotation The quaternion representing the bone's rotation.
	 * @return The forward vector of the bone.
	 */
	FVector GetBoneForwardVector(const FQuat& Rotation) const;

	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	/**
	 * Initializes the modify bones array with the current pose context and bone container.
	 *
	 * @param Output The component space pose context.
	 * @param BoneContainer The bone container containing bone hierarchy information.
	 */
	void InitModifyBones(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer);

	/**
	 * Initializes the sync rotation bones for the physics simulation.
	 */
	void InitSyncBones(FComponentSpacePoseContext& Output);

	/**
	* Initializes a sync bone for the physics simulation.
	*/
	void InitSyncBone(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	                  FKawaiiPhysicsSyncBone& SyncBone);

	/**
	 * Initializes the bone constraints for the physics simulation.
	 */
	void InitBoneConstraints();

	/**
	* 横方向Constraintに沿ってコリジョンセンサーとなるbridge dummyをModifyBonesに追加する（元Constraintは温存）。
	* 実ボーンへのフィードバックは毎フレームの直接変位転送(SimulateModifyBones)が担う。
	* Adds bridge collision-SENSOR dummies along horizontal constraints to ModifyBones (original constraints kept intact).
	* Feedback to the real bones is the per-frame direct displacement transfer in SimulateModifyBones.
	* InitBoneConstraints末尾から呼ばれる（ModifyBonesとMergedBoneConstraintsが構築済みであること） / Called at the end of InitBoneConstraints (after ModifyBones & MergedBoneConstraints are built).
	*/
	void InsertBridgeDummiesForConstraints();

	/**
	 * Applies the data asset to LimitData.
	 *
	 * @param RequiredBones The bone container containing the required bones.
	 */
	void ApplyLimitsDataAsset(const FBoneContainer& RequiredBones);

	/**
	 * Applies the physics asset to LimitData.
	 *
	 * @param RequiredBones The bone container containing the required bones.
	 */
	void ApplyPhysicsAsset(const FBoneContainer& RequiredBones);

	/**
	 * Applies the bone constraint data asset to BoneConstraints.
	 *
	 * @param RequiredBones The bone container containing the required bones.
	 */
	void ApplyBoneConstraintDataAsset(const FBoneContainer& RequiredBones);

	/**
	 * Adds a modify bone to the modify bones array.
	 *
	 * @param InModifyBones The array of modify bones to add to.
	 * @param Output The component space pose context.
	 * @param BoneContainer The bone container containing bone hierarchy information.
	 * @param RefSkeleton The reference skeleton containing bone hierarchy information.
	 * @param BoneIndex The index of the bone to add.
	 * @param InExcludeBones The array of bones to exclude.
	 * @return The index of the added modify bone.
	 */
	int32 AddModifyBone(TArray<FKawaiiPhysicsModifyBone>& InModifyBones, FComponentSpacePoseContext& Output,
	                    const FBoneContainer& BoneContainer,
	                    const FReferenceSkeleton& RefSkeleton, int32 BoneIndex,
	                    const TArray<FBoneReference>& InExcludeBones);

	/**
	* Collects the indices of all child bones for a given parent bone index.
	*
	* @param RefSkeleton The reference skeleton containing bone hierarchy information.
	* @param ParentBoneIndex The index of the parent bone.
	* @param Children An array to store the indices of the child bones.
	* @return The number of child bones collected.
	*/
	int32 CollectChildBones(const FReferenceSkeleton& RefSkeleton, int32 ParentBoneIndex,
	                        TArray<int32>& Children) const;
	/**
	 * Calculates the length of a bone from the root and updates the total bone length.
	 *
	 * @param Bone The bone to calculate the length for.
	 * @param InModifyBones An array of bones to modify.
	 * @param RefBonePose The reference bone pose.
	 * @param TotalBoneLength The total length of all bones.
	 */
	void CalcBoneLength(FKawaiiPhysicsModifyBone& Bone, TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
	                    const TArray<FTransform>& RefBonePose, float& TotalBoneLength);

	/**
	 * Updates the physics settings for all modified bones based on the current physics settings and curves.
	 */
	void UpdatePhysicsSettingsOfModifyBones();

	/**
	 * Updates the spherical limits for the given bones.
	 *
	 * @param Limits An array of spherical limits to update.
	 * @param Output The pose context.
	 * @param BoneContainer The bone container.
	 * @param ComponentTransform The component transform.
	 */
	void UpdateSphericalLimits(TArray<FSphericalLimit>& Limits, FComponentSpacePoseContext& Output,
	                           const FBoneContainer& BoneContainer, const FTransform& ComponentTransform) const;

	/**
	 * Updates the capsule limits for the given bones.
	 *
	 * @param Limits An array of capsule limits to update.
	 * @param Output The pose context.
	 * @param BoneContainer The bone container.
	 * @param ComponentTransform The component transform.
	 */
	void UpdateCapsuleLimits(TArray<FCapsuleLimit>& Limits, FComponentSpacePoseContext& Output,
	                         const FBoneContainer& BoneContainer, const FTransform& ComponentTransform) const;


	/**
	 * Updates the box limits for the given bones.
	 *
	 * @param Limits An array of box limits to update.
	 * @param Output The pose context.
	 * @param BoneContainer The bone container.
	 * @param ComponentTransform The component transform.
	 */
	void UpdateBoxLimits(TArray<FBoxLimit>& Limits, FComponentSpacePoseContext& Output,
	                     const FBoneContainer& BoneContainer, const FTransform& ComponentTransform) const;

	/**
	 * Updates the planar limits for the given bones.
	 *
	 * @param Limits An array of planar limits to update.
	 * @param Output The pose context.
	 * @param BoneContainer The bone container.
	 * @param ComponentTransform The component transform.
	 */
	void UpdatePlanerLimits(TArray<FPlanarLimit>& Limits, FComponentSpacePoseContext& Output,
	                        const FBoneContainer& BoneContainer, const FTransform& ComponentTransform) const;

	/**
	 * 共有コリジョンの初期化（PreUpdate経由でGameThreadから呼ばれる）
	 * Initialize shared collision entry and source slot.
	 * Called from PreUpdate() on the GameThread to ensure TMap mutations are thread-safe.
	 */
	void InitializeSharedCollision(const UAnimInstance* InAnimInstance);

	/**
	 * 計算済みコリジョンをSubsystemに公開する（AnyThread）
	 * Write computed collision data to the SharedCollisionSubsystem as source (any thread)
	 */
	void WriteSharedCollisionToSubsystem(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform);

	/**
	 * 共有コリジョンを読み取り、シミュレーション空間に変換する（AnyThread）
	 * Read shared collision and convert to simulation space (any thread)
	 */
	void UpdateSharedCollisionLimits(FComponentSpacePoseContext& Output);

	/**
	 * Updates the pose transform for all modified bones.
	 *
	 * @param Output The pose context.
	 * @param BoneContainer The bone container.
	 */
	void UpdateModifyBonesPoseTransform(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer);

	/**
	 * Applies the movement of SyncBones to target bones.
	 * @param Output The pose context.
	 * @param BoneContainer The bone container.
	 */
	void ApplySyncBones(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer);
	

	/**
	 * Updates the skeletal component movement vector and rotation.
	 *
	 * @param ComponentTransform The current component transform.
	 */
	void UpdateSkelCompMove(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform);

	/**
	 * Simulates the physics for all modified bones.
	 *
	 * @param Output The pose context.
	 * @param ComponentTransform The component transform.
	 */
	void SimulateModifyBones(FComponentSpacePoseContext& Output,
	                         const FTransform& ComponentTransform);

	/**
	 * シミュレーションの1ステップ分（Simulate ループ＋ダミー配置＋コリジョン＋拘束＋長さ復元）を
	 * 現在の GetStepDeltaTime() で1回実行する。SimulateModifyBones から legacy で1回、
	 * サブステップ時は固定 dt で複数回呼ばれる。
	 * Runs ONE simulation step (Simulate loop + dummy placement + collision + constraints + length restore)
	 * at the current GetStepDeltaTime(). Called once (legacy) or N times (fixed substepping) from SimulateModifyBones.
	 */
	void SimulateOnce(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform,
	                  const FSceneInterface* Scene, const USkeletalMeshComponent* SkelComp);

	/**
	 * Simulates the physics for a single bone.
	 *
	 * @param Bone The bone to simulate.
	 * @param Scene The scene interface.
	 * @param ComponentTransform The component transform.
	 * @param Exponent The exponent for the simulation.
	 * @param SkelComp The skeletal mesh component.
	 * @param Output The pose context.
	 */
	void Simulate(FKawaiiPhysicsModifyBone& Bone, const FSceneInterface* Scene, const FTransform& ComponentTransform,
	              const float& Exponent, const USkeletalMeshComponent* SkelComp,
	              FComponentSpacePoseContext& Output);

	// ===== 物理計算の各ステップ（引数に FComponentSpacePoseContext を取らない。Simulate() から呼ばれる）=====
	// Each physics step; takes no FComponentSpacePoseContext. Called from Simulate().

	/** このステップの速度を作る（速度の再構成→減衰→+wind→重力）。ユーザー外力(ApplyToVelocity)の前に呼ぶ。 */
	FVector ComputeVerletStepVelocity(FKawaiiPhysicsModifyBone& Bone, const FVector& WindVelocity);

	/** Verlet ステップ後半。ComputeVerletStepVelocity が作り、外力フック(ApplyToVelocity)で調整された後の
	 *  速度から位置を更新する（Location += Velocity * GetStepDeltaTime()）。 */
	void IntegrateVerletStepPosition(FKawaiiPhysicsModifyBone& Bone, const FVector& Velocity);

	/** simple external force（速度を経由しない位置オフセット。位置空間の後処理）。 */
	void ApplySimpleExternalForce(FKawaiiPhysicsModifyBone& Bone);

	/** ComponentSpace/WorldSpace の world 移動追従（BaseBoneSpace は Simulate() 側で別処理、Output依存）。 */
	void ApplyWorldMoveFollowNonBaseBone(FKawaiiPhysicsModifyBone& Bone);

	/** Pull to Pose Location（剛性）。 */
	void ApplyStiffnessPull(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone, float Exponent);

	// 自動テスト用アクセサ。private/protected の sim 状態・物理計算・コリジョン関数へアクセスする。
	// Shipping/Test（WITH_DEV_AUTOMATION_TESTS==0）では宣言ごと除外し、出荷ビルドにテスト表面を残さない。
	// Test-only accessor for private/protected sim state, core functions, and collision functions.
	// Stripped in Shipping/Test (WITH_DEV_AUTOMATION_TESTS==0); leaves no test surface in shipping builds.
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FKawaiiPhysicsTestAccessor;
#endif

	/**
	 * Adjusts the bone position based on world collision.
	 *
	 * @param Bone The bone to adjust.
	 * @param OwningComp The owning skeletal mesh component.
	 */
	void AdjustByWorldCollision(FComponentSpacePoseContext& Output, FKawaiiPhysicsModifyBone& Bone,
	                            const USkeletalMeshComponent* OwningComp);

	/**
	 * Adjusts the bone position based on spherical collision limits.
	 *
	 * @param Bone The bone to adjust.
	 * @param Limits An array of spherical limits.
	 */
	void AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits);

	/**
	 * Adjusts the bone position based on capsule collision limits.
	 *
	 * @param Bone The bone to adjust.
	 * @param Limits An array of capsule limits.
	 */
	void AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits);

	/**
	 * Adjusts the bone position based on box collision limits.
	 *
	 * @param Bone The bone to adjust.
	 * @param Limits An array of box limits.
	 */
	void AdjustByBoxCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FBoxLimit>& Limits);

	/**
	 * Adjusts the bone position based on planar collision limits.
	 *
	 * @param Bone The bone to adjust.
	 * @param Limits An array of planar limits.
	 */
	void AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits);

	/**
	 * Adjusts the bone position based on angle limits.
	 *
	 * @param Bone The bone to adjust.
	 * @param ParentBone The parent bone.
	 */
	void AdjustByAngleLimit(
		FKawaiiPhysicsModifyBone& Bone,
		const FKawaiiPhysicsModifyBone& ParentBone);

	/**
	 * Adjusts the bone position based on planar constraints.
	 *
	 * @param Bone The bone to adjust.
	 * @param ParentBone The parent bone.
	 */
	void AdjustByPlanarConstraint(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone);

	/**
	 * Adjusts the bone positions based on bone constraints.
	 */
	void AdjustByBoneConstraints();

	/**
	 * Applies the simulation results to the bone transforms.
	 *
	 * @param Output The pose context.
	 * @param BoneContainer The bone container.
	 * @param OutBoneTransforms An array to store the resulting bone transforms.
	 */
	void ApplySimulateResult(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	                         TArray<FBoneTransform>& OutBoneTransforms);

	/**
	 * Warms up the simulation by running it for a specified number of frames.
	 *
	 * @param Output The pose context.
	 * @param BoneContainer The bone container.
	 * @param InOutComponentTransform The component transform.
	 */
	void WarmUp(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	            FTransform& InOutComponentTransform);

	/**
	 * Gets the wind velocity for a given bone.
	 *
	 * @param Scene The scene interface.
	 * @param Bone The bone to get the wind velocity for.
	 * @return The wind velocity vector.
	 */
	FVector GetWindVelocity(FComponentSpacePoseContext& Output, const FSceneInterface* Scene,
	                        const FKawaiiPhysicsModifyBone& Bone) const;

#if ENABLE_ANIM_DEBUG
	void AnimDrawDebug(FComponentSpacePoseContext& Output);

	// Draw debug Box
	void AnimDrawDebugBox(FComponentSpacePoseContext& Output, const FVector& CenterLocationSim,
	                      const FQuat& RotationSim,
	                      const FVector& Extent, const FColor& Color, float Thickness) const;
#endif


private:
	bool bModifyBonesNeedsReinit = false;
	int32 LastInitializedBoneSubdivisionCount = 0;
	int32 LastInitializedBoneConstraintSubdivisionCount = 0;
	// CollisionOnlyは配置数（生成トポロジ）を左右するため再構築判定に含める。既定値はプロパティのデフォルトに合わせる
	// CollisionOnly affects the placed dummy count (generation topology), so it's part of the reinit check. Default matches the property.
	bool LastInitializedBoneSubdivisionCollisionOnly = true;
	float LastInitializedDummyBoneLength = 0.0f;

	// SimulationSpace conversion cache (per-evaluation)
	struct FSimulationSpaceCache
	{
		FTransform ComponentToTargetSpace = FTransform::Identity;
		FTransform TargetSpaceToComponent = FTransform::Identity;

		bool IsIdentity() const
		{
			return ComponentToTargetSpace.Equals(FTransform::Identity) &&
				TargetSpaceToComponent.Equals(FTransform::Identity);
		}
	};

	FSimulationSpaceCache BuildSimulationSpaceCache(FComponentSpacePoseContext& Output,
	                                                const EKawaiiPhysicsSimulationSpace SimulationSpaceForCache) const;

	// Select cache for a given simulation space (Evaluate cache preferred)
	FSimulationSpaceCache GetSimulationSpaceCacheFor(FComponentSpacePoseContext& Output,
	                                                 EKawaiiPhysicsSimulationSpace Space) const;

	// Convert helpers using explicit caches
	FTransform ConvertSimulationSpaceTransformCached(const FSimulationSpaceCache& CacheFrom,
	                                                 const FSimulationSpaceCache& CacheTo,
	                                                 const FTransform& InTransform) const;
	FVector ConvertSimulationSpaceVectorCached(const FSimulationSpaceCache& CacheFrom,
	                                           const FSimulationSpaceCache& CacheTo,
	                                           const FVector& InVector) const;
	FVector ConvertSimulationSpaceLocationCached(const FSimulationSpaceCache& CacheFrom,
	                                             const FSimulationSpaceCache& CacheTo,
	                                             const FVector& InLocation) const;
	FQuat ConvertSimulationSpaceRotationCached(const FSimulationSpaceCache& CacheFrom,
	                                           const FSimulationSpaceCache& CacheTo,
	                                           const FQuat& InRotation) const;

	void ConvertSimulationSpaceCached(const FSimulationSpaceCache& CacheFrom,
	                                  const FSimulationSpaceCache& CacheTo,
	                                  EKawaiiPhysicsSimulationSpace From,
	                                  EKawaiiPhysicsSimulationSpace To);

private:
	// Evaluate中のみ有効なキャッシュ（SimulationSpace<->Component）
	// AnyThread評価なので「フレーム跨ぎで使い回さない」こと
	mutable FSimulationSpaceCache CurrentEvalSimSpaceCache;
	mutable bool bHasCurrentEvalSimSpaceCache = false;

	// Evaluate中のみ有効なWorldSpaceキャッシュ（World<->Component）
	mutable FSimulationSpaceCache CurrentEvalWorldSpaceCache;
	mutable bool bHasCurrentEvalWorldSpaceCache = false;
};




