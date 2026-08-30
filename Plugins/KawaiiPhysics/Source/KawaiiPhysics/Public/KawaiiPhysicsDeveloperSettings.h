// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "KawaiiPhysicsDeveloperSettings.generated.h"

class UKawaiiPhysicsWindPresetDataAsset;

/** MCPノードの自動配置方向。Auto は非接続=縦積み、bAutoConnect=横並びの従来挙動 / Automatic placement direction for MCP nodes. Auto keeps legacy behavior: vertical when not connected, horizontal with bAutoConnect. */
UENUM(BlueprintType)
enum class EKawaiiPhysicsMcpNodePlacementDirection : uint8
{
	/** 自動（非接続は縦積み、bAutoConnect は横並びの従来挙動） / Auto (legacy behavior: vertical when not connected, horizontal with bAutoConnect). */
	Auto,
	/** 縦方向へ配置 / Place vertically. */
	Vertical,
	/** 横方向へ配置 / Place horizontally. */
	Horizontal,
};

/**
 * KawaiiPhysics のプロジェクト全体設定 / Project-wide settings for KawaiiPhysics.
 * Project Settings > Plugins > Kawaii Physics に表示される。
 */
UCLASS(config = Engine, defaultconfig, BlueprintType, meta = (DisplayName = "Kawaii Physics"))
class KAWAIIPHYSICS_API UKawaiiPhysicsDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	* シミュレーション全体を固定タイムステップ（FixedDt = 1/TargetFramerate）でサブステップ実行し、
	* フレームレート依存の挙動を解消する。（低fps時はサブステップ数が増え負荷が上がる）。
	* Run the whole simulation with fixed-timestep substepping (FixedDt = 1/TargetFramerate) for true
	* frame-rate independence (CPU cost rises at low fps as substep count grows).
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simulation",
		meta = (DisplayName = "Use Fixed Substepping"))
	bool bUseFixedSubstepping = true;

	/**
	* 1フレームあたりの最大サブステップ数。低fps/ヒッチ時の暴走（spiral of death）を防ぐ上限。
	* これを超える分の時間は破棄される。
	* Maximum substeps per frame. Caps catch-up at low fps / hitches (prevents spiral of death);
	* time beyond this is dropped.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simulation",
		meta = (DisplayName = "Max Substeps", ClampMin = "1", UIMin = "1", ClampMax = "16",
			EditCondition = "bUseFixedSubstepping"))
	int32 MaxSubsteps = 4;

	/**
	* シンプルワールドコリジョンの自動収集半径に使う係数。SkeletalMeshComponent の Bounds.SphereRadius に乗算する。
	* Multiplier used for the Simple World Collision auto gather radius, applied to the SkeletalMeshComponent's Bounds.SphereRadius.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simple World Collision",
		meta = (DisplayName = "Auto Gather Radius Scale", ClampMin = "1.0", UIMin = "1.0"))
	float SimpleWorldCollisionAutoGatherRadiusScale = 1.5f;

	/**
	* 新規に収集されたコライダーが押し出しを開始するまでのブレンドイン時間（秒）。0でフェードを無効化(即座に全強度)する。
	* Blend-in time (seconds) before a newly gathered collider starts pushing out at full strength. 0 disables the fade (immediate full strength).
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simple World Collision",
		meta = (DisplayName = "Fade-In Time", ClampMin = "0", Units = "s"))
	float SimpleWorldCollisionFadeInTime = 0.2f;

	/**
	* カメラからの距離がこの値(cm)を超えると収集間隔を間引く。
	* Beyond this camera distance (cm), the gather interval is throttled (doubled).
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simple World Collision",
		meta = (DisplayName = "Distance Throttle Start", ClampMin = "0", Units = "cm"))
	float SimpleWorldCollisionDistanceThrottleStart = 3000.f;

	/**
	* カメラからの距離がこの値(cm)を超えると収集自体を停止する。
	* Beyond this camera distance (cm), gathering is stopped entirely.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simple World Collision",
		meta = (DisplayName = "Distance Throttle Stop", ClampMin = "0", Units = "cm"))
	float SimpleWorldCollisionDistanceThrottleStop = 10000.f;

	/**
	* シンプルワールドコリジョンが SkeletalMeshComponent 1つあたりに保持する最大収集コンポーネント数。
	* a.AnimNode.KawaiiPhysics.SimpleWorldCollision.MaxComponents CVar が 0 以上の場合はそちらが優先される。
	* Maximum number of gathered components Simple World Collision keeps per SkeletalMeshComponent.
	* Overridden by the a.AnimNode.KawaiiPhysics.SimpleWorldCollision.MaxComponents CVar when it is >= 0.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simple World Collision",
		meta = (DisplayName = "Max Gathered Components", ClampMin = "1", UIMin = "1"))
	int32 SimpleWorldCollisionMaxGatheredComponents = 64;

	/**
	* PhysicsAsset モードで SkeletalMeshComponent 1つから採用する最大 body 数。bone index 昇順でこの数まで採用される。
	* a.AnimNode.KawaiiPhysics.SimpleWorldCollision.MaxPhysicsAssetBodies CVar が 0 以上の場合はそちらが優先される。
	* Maximum number of bodies gathered from one SkeletalMeshComponent in PhysicsAsset mode. Bodies are taken in bone-index order up to this count.
	* Overridden by the a.AnimNode.KawaiiPhysics.SimpleWorldCollision.MaxPhysicsAssetBodies CVar when it is >= 0.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simple World Collision",
		meta = (DisplayName = "Max PhysicsAsset Bodies", ClampMin = "1", UIMin = "1"))
	int32 SimpleWorldCollisionMaxPhysicsAssetBodies = 32;

	/**
	* 収集済みコンポーネントのスケールが変化したら次 Tick で再収集する（既定 OFF。ISM はインスタンススケール、PhysicsAsset モードは bone-local に焼き込んだスケールを対象）。
	* Re-gather a gathered component on the next tick when its scale changes (default OFF; instance scale for ISM,
	* the baked bone-local scale for PhysicsAsset mode).
	*/
	UPROPERTY(EditAnywhere, config, Category = "Simple World Collision",
		meta = (DisplayName = "Regather On Scale Change"))
	bool bSimpleWorldCollisionRegatherOnScaleChange = false;

#if WITH_EDITORONLY_DATA
	/**
	* MCPコメント枠のタイトルに付与するプレフィックス。
	* Prefix prepended to MCP comment frame titles.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Editor Scripting",
		meta = (DisplayName = "MCP Comment Prefix"))
	FString McpCommentPrefix = TEXT("[MCP] ");

	/**
	* MCPコメント枠の色。
	* Color of MCP comment frames.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Editor Scripting",
		meta = (DisplayName = "MCP Comment Color"))
	FLinearColor McpCommentColor = FLinearColor(0.55f, 0.45f, 0.85f);

	/** MCPノードの自動配置方向。Auto は非接続=縦積み、bAutoConnect=横並びの従来挙動 / Automatic placement direction for MCP nodes. Auto keeps legacy behavior: vertical when not connected, horizontal with bAutoConnect. */
	UPROPERTY(EditAnywhere, config, Category = "Editor Scripting",
		meta = (DisplayName = "MCP Node Placement Direction"))
	EKawaiiPhysicsMcpNodePlacementDirection McpNodePlacementDirection =
		EKawaiiPhysicsMcpNodePlacementDirection::Auto;

	/** MCPノード自動配置の折り返し数。0で折り返しなし / Wrap count for MCP node auto placement. Set to 0 to disable. */
	UPROPERTY(EditAnywhere, config, Category = "Editor Scripting",
		meta = (DisplayName = "MCP Node Placement Wrap Count", ClampMin = "0", UIMin = "0", UIMax = "32"))
	int32 McpNodePlacementWrapCount = 0;

	/**
	* MCPノード自動配置の横方向間隔（グラフ座標単位）。重なり回避の誤判定を防ぐため、想定ノード幅400未満には設定できない。
	* Horizontal spacing for MCP node auto placement (graph coordinate units). Cannot be set below the
	* expected node width of 400 to avoid false-positive overlap detection.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Editor Scripting",
		meta = (DisplayName = "MCP Node Placement Spacing X", ClampMin = "400", UIMin = "400", UIMax = "1000"))
	int32 McpNodePlacementSpacingX = 420;

	/**
	* MCPノード自動配置の縦方向間隔（グラフ座標単位）。重なり回避の誤判定を防ぐため、想定ノード高さ260未満には設定できない。
	* Vertical spacing for MCP node auto placement (graph coordinate units). Cannot be set below the
	* expected node height of 260 to avoid false-positive overlap detection.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Editor Scripting",
		meta = (DisplayName = "MCP Node Placement Spacing Y", ClampMin = "260", UIMin = "260", UIMax = "1000"))
	int32 McpNodePlacementSpacingY = 260;

	/**
	* BoneSubdivisionCount で生成される inter-bone dummy bone 数の警告しきい値。0で警告を無効化する。
	* Warning threshold for inter-bone dummy bones generated by BoneSubdivisionCount. Set to 0 to disable.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Performance Warnings",
		meta = (DisplayName = "Inter-Bone Dummy Warning Threshold", ClampMin = "0", UIMin = "0", UIMax = "1000"))
	int32 InterBoneDummyWarningThreshold = 100;

	/**
	* BoneConstraintSubdivisionCount で生成される bridge collision-proxy dummy bone 数の警告しきい値。0で警告を無効化する。
	* Warning threshold for bridge collision-proxy dummy bones generated by BoneConstraintSubdivisionCount. Set to 0 to disable.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Performance Warnings",
		meta = (DisplayName = "Bridge Dummy Warning Threshold", ClampMin = "0", UIMin = "0", UIMax = "1000"))
	int32 BridgeDummyWarningThreshold = 200;

	/**
	* Wind Scope で表示する風プリセット DataAsset。未設定または Presets 空なら組み込み3種を表示、設定時は完全置換。
	* Wind preset DataAsset shown by Wind Scope. Built-in three presets are shown when unset or Presets is empty; a configured asset fully replaces them.
	*/
	UPROPERTY(EditAnywhere, config, Category = "Wind Scope", meta = (DisplayName = "Wind Preset Data Asset"))
	TSoftObjectPtr<UKawaiiPhysicsWindPresetDataAsset> WindScopePresetDataAsset;
#endif

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	//~ End of UDeveloperSettings interface
};
