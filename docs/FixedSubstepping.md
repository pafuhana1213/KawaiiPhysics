# Fixed Substepping（固定タイムステップ・サブステップ化）設計ドキュメント

> 対象バージョン: KawaiiPhysics 1.20 系 / ステータス: **設計提案（未実装）**
> 本書は「Damping のフレームレート依存（#81）」を起点に、シミュレーション全体を真にフレームレート非依存化するための設計メモです。本文は日本語、クラス名・関数名・専門用語は英語併記。
> コード参照は `Plugins/KawaiiPhysics/Source/KawaiiPhysics/` を基点とした短縮表記。行番号は執筆時点（A-1〜D 実装後の作業ツリー）のもの。
> 設計の妥当性は Codex(gpt-5.4) にも照会済み（本書「付録」参照）。

---

## 1. 背景 / 解決する課題

GitHub Issue [#81](https://github.com/pafuhana1213/KawaiiPhysics/issues/81)「Damping is frame rate dependent」。
当初は「Damping を `Pow(1-Damping, Exponent)` で正規化すれば解決」と考え、opt-in フラグ `bUseFrameRateIndependentDamping`（`UKawaiiPhysicsDeveloperSettings`）を実装した（→ `docs` 外、`Private/AnimNode_KawaiiPhysicsSimulation.cpp:469-476`）。

しかし **実機検証で、40fps と 15fps で挙動が一致しない（むしろ ON の方が悪化する場合がある）** ことが判明。

### 1自由度モデルによる数値検証（実際の更新順を正確に再現）

fps 間スプレッド（= 各時刻の位置の max−min、小さいほど fps 非依存）：

| ケース | 条件 | OFF(legacy) | ON(Pow) |
|--------|------|-------------|---------|
| A | 純粋な減衰のみ（重力0・初速あり） | 39.6 | **4.2** ✅ |
| B | **重力のみ（減衰ゼロ）** | **30.2** | 30.2（同一） |
| C | 重力＋減衰 | 67.0 | 19.8 |
| D | フル（重力＋減衰＋Stiffness） | 19.8 | 16.0 |

**結論**：
- `Pow` 正規化は「自由減衰」単体には劇的に効く（A: 39.6→4.2）。数式自体は正しい。
- だが **Case B が決定的** — 減衰をゼロにしても重力だけで spread=30。つまり**支配的な fps 依存は重力項（＝明示的単一ステップ積分の O(dt) 誤差）**で、Damping 正規化では触れられない。
- 実シーン（D）では Pow は legacy よりわずかに良い程度。Damping フラグは巨大な重力依存性に対する小さなレバーで、結合系では過渡応答をズラすため**ONで悪く見える**ことがある（実機観察と一致）。

→ **個別項の解析的正規化では不十分。シミュレーション全体を固定 dt で回す「サブステップ化」が唯一の本質的解決策**（Codex も同結論）。コリジョン・XPBD 拘束・サンプリングされるポーズ目標は、固定 dt でないと fps 不変にできない。

---

## 2. 設計方針

実フレーム時間 `DeltaTime` をアキュムレータに溜め、**固定内部ステップ `FixedDt = 1 / TargetFramerate`** でソルバ全体を必要回数だけ回す（standard な fixed-timestep accumulator パターン。AnimDynamics / PhysX / 一般的 cloth と同じ）。

- **既定 OFF・opt-in**（挙動と CPU コストが変わるため）。`UKawaiiPhysicsDeveloperSettings` に追加。
- `FixedDt` は既存の per-node `TargetFramerate`（`Public/AnimNode_KawaiiPhysics.h:154`, 既定60, ClampMin=1）を流用。
- legacy パス（非サブステップ）は**完全に現状維持**（既存 Exponent 正規化・D フラグも温存）。

### 🔑 重要な簡略化：固定ステップでは Exponent=1 になる

サブステップ中は `Exponent = TargetFramerate * FixedDt = TargetFramerate * (1/TargetFramerate) = 1`。
よって既存の `Pow(1-Stiffness, Exponent)` / `Pow(1-Damping, Exponent)` は **自動的に生の `(1-Stiffness)` / `(1-Damping)` に collapse** する。
→ **per-bone の `Simulate()` 本体（`Private/AnimNode_KawaiiPhysicsSimulation.cpp:453`）はほぼ無改造で再利用可能**。固定 dt で N 回呼ぶだけで、減衰・Stiffness は本質的に fps 一定になる。新規ロジックの大半は「ループ・アキュムレータ・ポーズ補間・各種状態管理」に集中する。

---

## 3. データ構造（追加メンバ）

### 3.1 `UKawaiiPhysicsDeveloperSettings`（`Public/KawaiiPhysicsDeveloperSettings.h`）
```cpp
// サブステップ化の ON/OFF（既定OFF）
UPROPERTY(EditAnywhere, config, Category = "Simulation")
bool bUseFixedSubstepping = false;

// 1フレームあたり最大サブステップ数（スパイラル防止・低fps時の上限）
UPROPERTY(EditAnywhere, config, Category = "Simulation", meta=(ClampMin="1", UIMin="1", ClampMax="16"))
int32 MaxSubsteps = 4;
```
※ 既存 `bUseFrameRateIndependentDamping`（D で追加）は**撤去**する（§10）。

### 3.2 `FAnimNode_KawaiiPhysics`（`Public/AnimNode_KawaiiPhysics.h`、Transient 群、`DeltaTimeOld:695` 付近）
```cpp
float SubstepAccumulator = 0.0f;          // 未消費の実時間
float FrameDeltaTime = 0.0f;              // このフレームの実dt（DeltaTimeとは別に保持）
bool  bUseFixedSubsteppingCached = false; // 毎フレーム1回キャッシュ
int32 MaxSubstepsCached = 4;
// dt供給アクセサ用（④）
bool  bInSubstep = false;                 // サブステップ実行中フラグ
float StepDeltaTime = 0.0f;               // サブステップ中の固定dt（= FixedDt）
```
※ D で追加した `bUseFrameRateIndependentDampingCached` は撤去（§10）。

### 3.3 `FKawaiiPhysicsModifyBone`（`Public/KawaiiPhysicsTypes.h`）— ポーズ補間用
```cpp
// 前フレームのポーズ目標（サブステップ間補間の始点）
FVector PrevPoseLocation = FVector::ZeroVector;
FQuat   PrevPoseRotation = FQuat::Identity;
// このフレームのポーズ目標（補間の終点）。既存 PoseLocation/PoseRotation を「現フレーム値」として使い、
// サブステップ中は補間結果を一時的に PoseLocation へ書き戻して既存コードに供給する。
```

---

## 4. 実行フロー

`SimulateModifyBones()`（`Private/AnimNode_KawaiiPhysicsSimulation.cpp:76`）を 3 段に分割：

```
SimulateModifyBones(Output, ComponentTransform):
    if DeltaTime <= 0: return                       // 既存ガード :81
    FrameDeltaTime = DeltaTime

    PrepareFrame(Output)                            // ★毎フレーム1回（§6）

    if !bUseFixedSubsteppingCached:                 // ── legacy パス（現状維持）──
        StepOnce(DeltaTime)                          // 既存処理そのまま（Exponent=TargetFramerate*DeltaTime）
        DeltaTimeOld = DeltaTime
        return

    // ── 固定サブステップ・パス ──
    const float FixedDt = 1.0f / GetEffectiveTargetFramerate();
    SubstepAccumulator += FrameDeltaTime
    SubstepAccumulator = min(SubstepAccumulator, MaxSubstepsCached * FixedDt)   // 超過は捨てる
    const int32 NumSteps = floor(SubstepAccumulator / FixedDt)
    SubstepAccumulator -= NumSteps * FixedDt

    StepDeltaTime = FixedDt                          // ★GetStepDeltaTime() が返す値（§7-A）
    for (i = 0; i < NumSteps; ++i):
        const float Alpha = (NumSteps > 0) ? float(i+1)/NumSteps : 1.0f
        InterpolatePoseTargets(Alpha)               // §5：PoseLocation/Rotation を補間値に
        DistributeWorldMove(FixedDt / FrameDeltaTime) // §7-D：world追従を分配
        Step(FixedDt)                                // 既存の per-bone Simulate + dummy + collision + XPBD + length

    CommitPoseTargets()                              // PrevPose ← CurrentPose（次フレーム補間用）
    DeltaTimeOld = FixedDt                           // §7-C
    // NumSteps==0（高fps）の時は何もしない＝状態維持
```

> **dt 供給（④確定）**：`float GetStepDeltaTime() const { return bInSubstep ? StepDeltaTime : DeltaTime; }` を新設。`Simulate()` 内の重力 `GravityInSimSpace * GetStepDeltaTime()`（:468-469）・積分 `Velocity * GetStepDeltaTime()`・**全外力クラスの `Node.DeltaTime` 参照（Basic/Curve/Wind/Gravity）を `Node.GetStepDeltaTime()` に置換**。`Exponent = TargetFramerate * GetStepDeltaTime()`（サブステップ時は自動的に 1）。legacy パスでは `GetStepDeltaTime()==DeltaTime` なので完全に現状維持。

---

## 5. ポーズ目標のサブステップ補間

入力アニメーションのポーズ（`PoseLocation`/`PoseRotation`）は**毎フレーム1回しか更新されない**。サブステップ間で固定すると、アニメで動く親ボーンに対し遅延・段差が出て、結局 fps 依存が残る。

→ 各サブステップ `i/N` で `Lerp(PrevPose, CurrentPose, Alpha)`（回転は `Slerp`）した値を**一時的に `Bone.PoseLocation`/`PoseRotation` へ書き込み**、既存コード（dummy 配置・`BaseLocation`・length 復元、:625, :445 等）にそのまま供給する。フレーム末で `PrevPose ← CurrentPose` を確定（`CommitPoseTargets`）。

- 簡易版（v1 候補）：補間せず現フレーム値を固定 → 実装は軽いが低fpsで駆動運動の一致が落ちる。**Codex は補間を推奨**（→ §11 で要判断）。

---

## 6. 毎フレーム1回 vs 毎サブステップ の切り分け（Codex 指針）

**`PrepareFrame()`（毎フレーム1回）**：
- `Output.Pose` 読取・SkipSimulate 判定・現フレームのポーズ目標確定（既存「Save Prev/Pose Info」ループ :89〜）
- per-bone authored settings 更新（`UpdatePhysicsSettingsOfModifyBones`）
- `GravityInSimSpace` / `SimpleExternalForceInSimSpace` 計算（:118-148）
- コライダー/limit のトランスフォーム更新、共有コリジョンのスナップショット取得
- 外力 `PreApply`（:151-166）／デバッグ描画／最終ボーン出力

**`Step(FixedDt)`（毎サブステップ）**：
- 速度再構成・積分（`Simulate` :453）、wind/gravity/外力 `Apply`・`ApplyToVelocity`、world追従、pull-to-pose
- dummy 配置（inter-bone / bridge）、コリジョン/limits、bone-length 復元、XPBD 拘束

> 外力の `Apply`/`ApplyToVelocity` は**毎サブステップ**実行（さもなくば外力が再び dt 依存になる）。`PreApply` のみ毎フレーム。

---

## 7. 重要な技術ポイント

- **A. dt の供給（④確定）**：`GetStepDeltaTime()` アクセサ経由。サブステップ中は `bInSubstep=true` で `StepDeltaTime(=FixedDt)` を返す。`Simulate()`・全外力クラスの `Node.DeltaTime` 参照を `Node.GetStepDeltaTime()` に置換。legacy 時は `DeltaTime` を返し現状維持。
- **B. Exponent**：`= TargetFramerate * FixedDt = 1` に自動でなる（§2）。既存正規化式は no-op 化して生係数に。
- **C. `DeltaTimeOld`**：サブステップ中は常に `FixedDt`（速度再構成 `(Location-PrevLocation)/FixedDt`）。速度連続性は `PrevLocation` を毎サブステップ更新することで担保。`NumSteps==0` のフレームは状態を一切変えない。
- **D. World 移動追従（`SkelCompMoveVector`/`Rotation` :641, :518-547）**：1フレームの総移動量。各サブステップへ**分配**する。並進は `* (FixedDt / FrameDeltaTime)`、回転は frame 回転の**増分割合**を `Slerp` で（毎サブステップ full 回転を当てない）。
- **E. Wind（実装済み）**：`GetWindParameters`（Scene 問い合わせ）は**AnyThread からの game-thread 風データ参照**で、毎サブステップ呼ぶのは望ましくない。→ **wind 入力は外力の `PreApply`（毎フレーム1回）で各ボーン分を取得・キャッシュ**し、サブステップでは使い回す（`KawaiiPhysicsExternalForce_Wind`）。
  - 実装メモ：キャッシュは **`Bone.Index` をキー**に「方向」と「風速」を**分離**して保持（`TArray<FVector> CachedWindDirection` / `TArray<float> CachedWindSpeed`）。ダミーボーンは `BoneRef` が空＝`NAME_None` のため**名前キーだと全ダミーが衝突する**（添字キーで回避）。`Apply` 側は `VRandCone(dir) * speed` の順で復元し、legacy の数式と一致させる。
  - **達成範囲の明確化**：本対応のゴールは「**毎サブステップの Scene 参照を排除**」すること。`PreApply` 自体は `EvaluateSkeletalControl_AnyThread()` 配下なので、Scene 問い合わせは「**毎フレーム1回・AnyThread**」に縮小される（legacy も毎フレーム AnyThread で問い合わせていたのと同等。回数はむしろ削減）。使用 API は `GetWindParameters`（`_GameThread` 版ではない任意スレッド読み取り用。AnimDynamics と同パターン）。
  - **将来オプション**：AnyThread から Scene を一切触らない完全分離が要件なら、風入力の取得を `PreUpdate`（GameThread）へ移し、AnyThread はスナップショットのみ読む形にする（→ §9）。本タスクの範囲外。

---

## 8. Reset / Teleport / WarmUp / Accumulator

- **`ResetDynamics`（`:715`）**：`SubstepAccumulator = 0`、`PrevLocation = Location`、`PrevPose = CurrentPose`、再初期化。
- **`TeleportPhysics`（`TeleportType` :651）**：アキュムレータを flush、そのフレームはステップせず、履歴を整合スナップ。
- **`WarmUp`（`:665`、`WarmUpFrames`）**：固定 `FixedDt` でポーズ固定のまま所定回数回す（既存ループを FixedDt 化）。
- **低fps/ヒッチ**：`MaxSubsteps * FixedDt` でアキュムレータをクランプし**超過分は破棄**（巨大キャッチアップ＝spiral of death 禁止）。
- **高fps（`DeltaTime < FixedDt`）**：余りを繰越、`NumSteps==0` で何もしない（前回状態を保持）。より滑らかにするなら前後状態の補間レンダリング（`Accumulator/FixedDt`）を将来追加（→ §11）。
- **URO/LOD スキップ**：大きな `DeltaTime` が来る → クランプ方針で吸収。

---

## 9. ワーカースレッド上のリスク（Codex 指摘）

- サブステップ中に**ミュータブルな World/GameThread 状態を参照しない**（全て事前キャッシュ）。特に上記 wind。粒度は2段階で区別する：
  - **現実的なゴール（達成済み）＝「毎サブステップの Scene/GameThread 参照を禁止」**。事前キャッシュは外力の `PreApply`（毎フレーム1回、`EvaluateSkeletalControl_AnyThread()` 配下）で行う。これにより「サブステップ間で入力が変わらない」ことは保証されるが、`PreApply` 自体は依然 AnyThread で1回だけ Scene を読む（legacy と同等で回数は削減）。wind は任意スレッド読み取り用 API `GetWindParameters`（AnimDynamics 同等）を使うため、この毎フレーム1回の AnyThread 参照は許容する。
  - **より厳格なゴール（将来オプション・未対応）＝「AnyThread で GameThread 状態を一切読まない」**。これを満たすには取得を `PreUpdate`（GameThread）へ移し、AnyThread はスナップショットのみ読む形にする。現状は要件化していない。
- カスタム外力オブジェクトが「毎フレーム1回」前提や非スレッドセーフの可能性 → ドキュメントで明示。
- コスト変動：低fpsでサブステップ数が増える（最大 `MaxSubsteps` 倍）。プロファイル必須（`stat KawaiiPhysics`）。

---

## 10. 後方互換と既存「Damping正規化フラグ」の扱い

- サブステップ化は **opt-in・既定 OFF**。legacy パスは完全現状維持。
- 既に追加した `bUseFrameRateIndependentDamping`（Pow-damping, D）は **撤去**（⑤確定）。中途半端で誤解を招くため、`DeveloperSettings` のプロパティ・ノードのキャッシュメンバ・`Simulate()` の Damping 分岐を削除し、legacy の生 `(1-Damping)` に戻す。実装はサブステップ化に一本化。
  - 手順：①先に D を revert（クリーンな legacy ベースへ）→ ②サブステップ化を載せる。A-1/A-2/B-1/C-2 の安全修正は残す。

---

## 11. 決定事項（確定）

| # | 項目 | 決定 |
|---|------|------|
| ① | トグルの所在 | **project-wide `DeveloperSettings`**（`bUseFixedSubstepping`）。per-node UPROPERTY は採用せず。`CopyNodeData` 不要 |
| ② | ポーズ補間 | **フル補間（`Lerp`/`Slerp`）**。`FKawaiiPhysicsModifyBone` に `PrevPoseLocation`/`PrevPoseRotation` を追加 |
| ③ | 高fps状態補間レンダリング | **将来送り**。`NumSteps==0` のフレームは前回状態を保持（超高fpsの微小ジャダーは許容） |
| ④ | dt 供給方式 | **`GetStepDeltaTime()` アクセサ新設**。`Simulate()` 内の重力・積分、および**全外力クラス（Basic/Curve/Wind/Gravity）の `Node.DeltaTime` 参照を置換**。legacy 時は `DeltaTime`、サブステップ時は `FixedDt` を返す |
| ⑤ | 既存 Damping フラグ(D) | **撤去**。`bUseFrameRateIndependentDamping`（DeveloperSettings）と `bUseFrameRateIndependentDampingCached`（ノード）、および `Simulate()` の Damping 分岐を削除し、legacy の生 `(1-Damping)` に戻す。サブステップ化に一本化 |
| ⑥ | `MaxSubsteps` / `FixedDt` | **`MaxSubsteps=4`**、**`FixedDt = 1/TargetFramerate`**（per-node `TargetFramerate` 流用、専用設定は新設しない） |

> ④の帰結：`Node.DeltaTime` を直接読む全外力クラス（`Private/ExternalForces/*.cpp` の `Apply`/`PreApply`/`ApplyToVelocity`）を `Node.GetStepDeltaTime()` 経由に置換する。`Simulate()` 内の `GravityInSimSpace * DeltaTime`・`Velocity * DeltaTime`・`0.5*Gravity*DeltaTime*DeltaTime` も同様。`Exponent` も `TargetFramerate * GetStepDeltaTime()` で算出（サブステップ時は自動的に 1）。
> ⑤の帰結：先に「D 実装の revert」を行ってから（=クリーンな legacy ベースに戻してから）サブステップ化を載せる。A-1/A-2/B-1/C-2 の安全修正は残す。

---

## 12. 実装フェーズ（Codex の順序を具体化）

0. **D の revert（⑤）**：`bUseFrameRateIndependentDamping`（`DeveloperSettings`）・`bUseFrameRateIndependentDampingCached`（ノード）・`Simulate()` の Damping 分岐を削除し、生 `(1-Damping)` に戻す。**A-1/A-2/B-1/C-2 は残す**。`DeveloperSettings` クラス自体は次段で再利用するので残す。
1. **`GetStepDeltaTime()` アクセサ（④）**：新設し、`Simulate()` 内の重力/積分、および**全外力クラスの `Node.DeltaTime` 参照を置換**。`Exponent` も `TargetFramerate * GetStepDeltaTime()` に。この時点では `bInSubstep=false` なので挙動不変＝回帰なしを先に確認。
2. **データ追加**：`DeveloperSettings`（`bUseFixedSubstepping`/`MaxSubsteps`）、ノードの accumulator/FrameDeltaTime/StepDeltaTime/bInSubstep/cache、`FKawaiiPhysicsModifyBone` の PrevPose。
3. **関数分割**：`SimulateModifyBones` を `PrepareFrame()` / `Step(FixedDt)` / （`ApplyResult` は当面 §6 の「毎フレーム1回」末尾処理に内包）へ分離。legacy パスは `Step` を1回呼ぶだけ。
4. **アキュムレータ・ループ**実装（§4）。`bInSubstep`/`StepDeltaTime` 設定。
5. **ポーズ補間**（§5）・**world移動分配**（§7-D）・**wind キャッシュ**（§7-E）。
6. **Reset/Teleport/WarmUp/クランプ**（§8）。
7. **検証**（§13）。

---

## 13. 検証方法

1. **ビルド**：UE 5.7（`E:\Launcher`）の Editor ターゲット。`git config core.quotePath false` 必須。
   `"E:\Launcher\UE_5.7\Engine\Build\BatchFiles\Build.bat" KawaiiPhysicsSampleEditor Win64 Development -Project="F:\Github\KawaiiPhysics\KawaiiPhysicsSample.uproject"`
2. **数値前検証（任意）**：本書 §1 の 1-DOF モデルに「固定 FixedDt で N 回」を追加し、15/40/60/120fps でスプレッドがほぼ 0 になることを確認してから実装に入ると安全。
3. **PIE**：`KawaiiPhysicsSample.umap`。
   - 既定 OFF：従来挙動と完全一致（後方互換）。
   - ON：`t.MaxFPS` を 15/30/40/60/120 に変えて、**全fpsで揺れ・収束・sag がほぼ一致**することを目視＋（可能なら）特定ボーンの軌跡ログで定量確認。
   - Teleport / ResetDynamics / WarmUp / キャラ高速移動（world追従）で破綻しないこと。
   - 低fpsでサブステップ数が `MaxSubsteps` でクランプされ、ヒッチ後に暴れないこと。
4. **コスト**：`stat KawaiiPhysics` で低fps時のサブステップ増による負荷を確認。

---

## 付録：Codex(gpt-5.4) 照会の要旨
- 「真の fps 非依存はソルバ全体の固定ステップ化が唯一解。個別項の解析正規化や RK/半陰的は、コリジョン・拘束・サンプリングポーズを fps 不変にできない」と本書 §1 の結論を支持。
- 設計の骨子（accumulator / FixedDt / ポーズ補間 / 毎サブステップ・毎フレーム分割 / クランプ / reset・teleport・warmup / opt-in 既定OFF）は本書 §2〜§10 に反映。
- ワーカースレッドの注意（game-thread 参照禁止・wind キャッシュ・外力の前提）も §7-E, §9 に反映。
