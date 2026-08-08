// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KawaiiPhysicsEditorLibrary.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "UObject/SoftObjectPath.h"

struct FKawaiiPhysicsPresetDiffPropertyRow
{
	/**
	 * プロパティの内部 FName。
	 * Internal FName of the property.
	 */
	FName PropertyName;

	/**
	 * Property->GetDisplayNameText() の表示名。
	 * Display name from Property->GetDisplayNameText().
	 */
	FText DisplayName;

	/**
	 * Category メタデータ文字列。存在しない場合は空。
	 * Category metadata string, empty if absent.
	 */
	FString Category;

	/**
	 * 対象ノード側の値文字列。
	 * Value string from the target node.
	 */
	FString NodeValue;

	/**
	 * プリセット側の値文字列。
	 * Value string from the preset.
	 */
	FString PresetValue;

	/**
	 * ノードとプリセットの値が異なるか。
	 * Whether the node and preset values differ.
	 */
	bool bDiffers = false;
};

struct FKawaiiPhysicsPresetDiffSnapshot
{
	/**
	 * プリセットアセットの表示名。
	 * Preset asset display name.
	 */
	FText PresetDisplayName;

	/**
	 * プリセットアセットへのソフト参照パス。
	 * Soft object path to the preset asset.
	 */
	FSoftObjectPath PresetPath;

	/**
	 * プリセットと対象ノードが一致しているか。
	 * Whether the preset and target node match.
	 */
	bool bMatches = false;

	/**
	 * 差分がある比較対象プロパティ数。
	 * Number of compared properties that differ.
	 */
	int32 DiffCount = 0;

	/**
	 * 比較対象プロパティごとの行。bDiffers が実際に差分のある行を示す。
	 * One row per compared property; bDiffers marks which ones actually differ.
	 */
	TArray<TSharedPtr<FKawaiiPhysicsPresetDiffPropertyRow>> Rows;
};

namespace KawaiiPhysicsPresetDiff
{
	/**
	 * 単一プリセットに対する差分スナップショットを構築する。
	 * Build a diff snapshot for a single preset.
	 */
	TSharedRef<FKawaiiPhysicsPresetDiffSnapshot> BuildSnapshot(const FAnimNode_KawaiiPhysics& Node,
	                                                          const UKawaiiPhysicsPresetDataAsset& Preset,
	                                                          const FKawaiiPhysicsPresetApplyOptions& Options);

	/**
	 * Node.KawaiiPhysicsTag に一致するプリセットの差分スナップショットを構築する。
	 * Build diff snapshots for presets whose target tags match Node.KawaiiPhysicsTag.
	 */
	TArray<TSharedRef<FKawaiiPhysicsPresetDiffSnapshot>> BuildSnapshotsForNode(
		const FAnimNode_KawaiiPhysics& Node,
		const FKawaiiPhysicsPresetApplyOptions& Options);

	/**
	 * 差分行だけをタブ区切りのクリップボード文字列に変換する。
	 * Convert only differing rows to tab-separated clipboard text.
	 */
	FString SnapshotToClipboardText(const FKawaiiPhysicsPresetDiffSnapshot& Snapshot, const FText& ContextLabel);

	/**
	 * 差分プロパティの値ペア配列を生成する。
	 * Build value pairs for differing properties.
	 */
	TArray<FKawaiiPhysicsPresetDiffValue> BuildDiffValues(const FAnimNode_KawaiiPhysics& Node,
	                                                      const UKawaiiPhysicsPresetDataAsset& Preset,
	                                                      const FKawaiiPhysicsPresetApplyOptions& Options);
}
