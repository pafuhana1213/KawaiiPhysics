# Kawaii Physics

![UE Version](https://img.shields.io/badge/%E8%99%9A%E5%B9%BB%E5%BC%95%E6%93%8E-5.3--5.8-0e1128?logo=unrealengine&logoColor=white) [![Docs](https://img.shields.io/badge/%E6%96%87%E6%A1%A3-Portal-blue)](https://pafuhana1213.github.io/KawaiiPhysics-Portal/en/docs) [![GitHub release (latest by tag)](https://img.shields.io/github/v/release/pafuhana1213/KawaiiPhysics)](https://github.com/pafuhana1213/KawaiiPhysics/releases) [![Downloads](https://img.shields.io/github/downloads/pafuhana1213/KawaiiPhysics/total)](https://github.com/pafuhana1213/KawaiiPhysics/releases) [![Discussions](https://img.shields.io/github/discussions/pafuhana1213/KawaiiPhysics?logo=github)](https://github.com/pafuhana1213/KawaiiPhysics/discussions) [![GitHub contributors](https://img.shields.io/github/contributors/pafuhana1213/KawaiiPhysics?logo=github)](https://github.com/pafuhana1213/KawaiiPhysics/graphs/contributors) [![License: MIT](https://img.shields.io/badge/%E8%AE%B8%E5%8F%AF%E8%AF%81-MIT-yellow.svg)](https://opensource.org/licenses/MIT) [![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/pafuhana1213/KawaiiPhysics) [![X (formerly Twitter) Follow](https://img.shields.io/twitter/follow/pafuhana1213?style=social)](https://twitter.com/pafuhana1213)

* * *

> [!TIP]
> **English → [README.md](README.md) | 日本語 → [README_ja.md](README_ja.md)**

Kawaii Physics 是虚幻引擎的一款简易伪物理插件。  
它可以轻松又可爱地为头发、裙子、胸部等摇曳物体制作动画。

[![演示视频](https://github.com/user-attachments/assets/0bc33f5c-c7db-49b3-8e98-75dc062a4e2a)](https://youtu.be/0f-l-SP07Mo)

[演示视频](https://youtu.be/0f-l-SP07Mo)

目录

-   [画廊](#gallery)
-   [功能特性](#features)
-   [快速开始](#getting-started)
-   [文档与社区](#documentation--community)
-   [教程与文章](#tutorials--articles)
-   [示例](#sample)
-   [案例展示](#showcase)
-   [相关工具](#related-tools)
-   [许可证](#license)
-   [作者](#author)
-   [更新日志](#changelog)

## 画廊

![compare](https://github.com/user-attachments/assets/fda69859-b60f-4fde-a683-62da3e2839e4)

*角色：[Gray-chan](http://rarihoma.xvs.jp/products/graychan)*

![dance5](https://github.com/user-attachments/assets/28d72d0c-4423-41c7-bc52-c5c7c3886e02)

*角色：[原创 3D 模型 “Lzebul”](https://booth.pm/ja/items/4887691) / 动作：[Mirai Komachi](https://www.miraikomachi.com/download/)*

![danceKano](https://github.com/user-attachments/assets/63faed3c-8aaa-4d4d-ae33-e98f9c8c15fd)

*角色：[TA 风格 鹫宫加野](https://uzurig.com/ja/uzurig2-rigging-plugin-for-maya-jp/) / 动作：[Shikanokonokonokoshitantan](https://booth.pm/ja/items/5975857) / 设置：[TA Co., Ltd.](https://xta.co.jp/)*

## 功能特性

<table><tbody><tr><td><img src="https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics2.jpg?raw=true" width="320"></td><td><img src="https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics4.gif?raw=true" width="320"></td></tr></tbody></table>

-   根据角色的动画和移动来驱动摇曳骨骼
-   只需将一个 `KawaiiPhysics` 节点拖入动画图即可完成设置
-   支持球体、胶囊和平面碰撞，全部可在视口中直接调整
-   可在编辑器偏好设置中修改每种碰撞在视口中的颜色、不透明度和轮廓
-   保持骨骼长度固定，即使模拟崩溃，骨骼也不会拉伸或塌缩
-   使用简单的无 PhysX 算法，运行时保持轻量
-   不仅可应用风和重力，还可应用自定义力来实现艺术效果
-   可通过 `DataAsset` 和 `PhysicsAsset` 保存并复用设置
-   可用圆锥限制弯曲角度，或按父骨骼的局部轴逐轴限制，以构建铰链和单向弯曲（逐轴）
-   可约束骨骼间距离，防止裙子穿透腿部（BoneConstraint）
-   可将动画驱动的骨骼（如腿部）同步到模拟中，使布料跟随身体并避免穿透（SyncBone）
-   可在运行时从蓝图或 C++ 驱动参数和外力
-   其他近期功能请参见[更新日志（KawaiiPhysics Portal）](https://pafuhana1213.github.io/KawaiiPhysics-Portal/en/docs/changelog)

### 参考资料

在实现物理行为时，我参考了以下资料：

-   [Next Idolmaster Graphics & Animation Programming Preview](https://cedil.cesa.or.jp/cedil_sessions/view/416)

## 快速开始

### 1\. 下载

如果没有 C++ 构建环境，请从以下链接下载插件：

-   **[GitHub Releases](https://github.com/pafuhana1213/KawaiiPhysics/releases/)**
-   **[FAB](https://www.fab.com/ja/listings/f870c07e-0a02-4a78-a888-e52a22794572)**（与 GitHub 版本内容相同）
-   **[Booth](https://pafuhana1213.booth.pm/items/5943534)**（关注店铺可接收更新通知）

### 2\. 支持版本

-   **虚幻引擎 5.3 ~ 5.8**
-   UE4.27：[v1.11.1](https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20230830-v1.11.1)

### 3\. 安装

1.  在项目根目录创建 `Plugins` 文件夹。
2.  将下载的 `KawaiiPhysics` 文件夹复制到 `Plugins` 文件夹中。

### 4\. 使用方法

-   在动画蓝图的动画图中添加并使用 `KawaiiPhysics` 节点。
-   详细参数设置请参考[文档](https://pafuhana1213.github.io/KawaiiPhysics-Portal/en/docs/parameters/physics)。

> **注意：** 对仅使用蓝图的工程进行打包时可能出错。此时请将其转换为 C++ 工程，或参考[这篇文章](https://papersloth.hatenablog.com/entry/2024/02/14/201629)。

## 文档与社区

-   **[Portal](https://pafuhana1213.github.io/KawaiiPhysics-Portal/en/docs)**：官方文档（最新）
-   **[Wiki](https://github.com/pafuhana1213/KawaiiPhysics/wiki)**：旧版文档（最新信息已迁移至 Portal）
-   **[DeepWiki](https://deepwiki.com/pafuhana1213/KawaiiPhysics)**：AI 生成的文档
-   **[Discussions](https://github.com/pafuhana1213/KawaiiPhysics/discussions)**：用于提问、请求和一般交流
-   **[Issues](https://github.com/pafuhana1213/KawaiiPhysics/issues)**：Bug 报告

## 教程与文章

### 官方

-   [自制 AnimNode “Kawaii Physics” 内部实现解说 第 1 部分](http://pafuhana1213.hatenablog.com/entry/2019/07/26/171046)
-   [尝试使用kongzhibangding配合 KawaiiPhysics 防止裙子穿透](https://twitter.com/pafuhana1213/status/1300454762542817280)

### 非官方（感谢！）

-   [如何在 Unreal Engine 5 中设置 Kawaii Physics](https://dev.epicgames.com/community/learning/tutorials/d1Z9/unreal-engine-how-to-setup-kawaii-physics-in-unreal)
    
-   [Unreal 那些最有型游戏背后隐藏的物理“引擎”…](https://www.youtube.com/watch?v=9ThmoMHnHhw)
    
-   日文内容
    
    -   [一份面向完全新手的指南：用 KawaiiPhysics 提升女性角色的可爱度](https://qiita.com/YuukiOgino/items/7f3198a90dab43019f26)
    -   [摇摆物体与控制绑定：使用 UE 进行虚拟直播的案例研究](https://www.docswell.com/s/indieusgames/K4Q2XJ-2024-06-14-175045)
    -   [【UE】摇曳物体设置（Kawaii Physics）](https://techblog.sumelagi.co.jp/unrealengine/147/)
    -   [【UE4】Kawaii Physics 教程 #1](https://www.youtube.com/watch?v=hlgXuVML_is)
    -   [5 分钟学会 Kawaii Physics！【UE5】【教程】](https://www.youtube.com/watch?v=TliP9vSxm4c)

## 示例

![sample2](https://github.com/user-attachments/assets/0d866ad2-f803-400b-bd23-2d46ab17b8ae)

GitHub 上的示例工程包含示例关卡和角色。

-   **引擎版本**：UE 5.8
-   **功能示例**：`Content/KawaiiPhysicsSample/Examples/` 中每个主题一个关卡。每个展项逐步展示单个功能，并配有各自的动画蓝图。`Content/KawaiiPhysicsSample/L_KPS_AllExamples` 会并排加载所有主题关卡，方便在一个地方浏览全部功能。在编辑器中运行时，按 Z / X 跳转到当前主题的上一个 / 下一个展项，按 PageUp / PageDown 跳转到上一个 / 下一个主题。按 H 可在 [KawaiiPhysics Portal](https://pafuhana1213.github.io/KawaiiPhysics-Portal/en/docs) 中打开当前展项的文档页面。按 B 可在编辑器中打开该展项使用的动画蓝图。在编辑器中，每个标志 Actor 细节面板中的 **Open Docs** 和 **Open AnimBP** 按钮作用相同。标志上的说明文字会跟随当前语言：日文环境显示日文，其他环境显示英文。
    -   `01_BoneChain`（RootBone / DummyBoneLength / ExcludeBones / AdditionalRootBones / 多节点与标签）
    -   `02_PhysicsSettings`（阻尼 / 刚度 / 世界阻尼 / 角度限制 / 曲线 / 瞬移阈值 / 预热 / SkelCompMoveScale / 逐轴角度限制 / 时间缩放）
    -   `03_Collision`（球体 / 胶囊 / 锥形胶囊 / 盒形 / 平面 / LimitsDataAsset / PhysicsAsset / 世界碰撞）
    -   `04_Forces`（重力 / 简易外力 / 外力预设 / AnimNotify / 外力空间 / 按骨骼长度的力强度 / 旧版重力）
    -   `05_Advanced`（骨骼约束 / 骨骼细分 / SyncBone / 预设 / 共享碰撞）
    -   `06_RuntimeControl`（来自蓝图的 Alpha、物理设置倍率和阵风 / 设置倍率 AnimNotifyState / Sequencer 轨道 / Sequencer 用后期处理动画蓝图 / AnimNode 函数 / 外力体积）
    -   `07_SimpleWorldCollision`（自动关卡碰撞 / 收集形状 / 骨骼网格体碰撞模式 / 收集半径）
    -   `08_Wind`（程序化风组件 / 风预设数据资产 / 触发阵风 AnimNotify / 共享发布器）
    -   `09_Showcase`（TA 风格鹫宫加野角色上的完整设置，逐部分展示）
-   **使用的角色**：
    -   **Gray-chan**：[http://rarihoma.xvs.jp/products/graychan](http://rarihoma.xvs.jp/products/graychan)
    -   **TA 风格 鹫宫加野**：由 [TA Co., Ltd.](https://xta.co.jp/) 提供
        -   Copyright (c) 2025 TA Co., Ltd. All rights reserved
        -   使用条款：[https://uzurig.com/ja/terms\_of\_use\_jp/](https://uzurig.com/ja/terms_of_use_jp/)

### MCP（实验性）

示例工程启用了 UE 5.8 的实验性 **Unreal MCP** 插件，因此 MCP 客户端可以操作编辑器。Kawaii Physics 自带工具集（`KawaiiPhysicsToolset`），涵盖创作（添加和编辑 KawaiiPhysics 与共享发布器节点、碰撞限制和外力）、预设与审计、在编辑器中运行时的验证（运行时节点信息、骨骼与碰撞检查、骨骼与穿透采样器、临时设置倍率和阵风），以及裙子检查与调整辅助工具（环形骨骼约束、按深度设置半径、运动记录与分析）。

1.  通过以下任一方式启动 MCP 服务器：
    -   运行控制台命令 `ModelContextProtocol.StartServer`（立即启动）
    -   在 **编辑器偏好设置 > 常规 > Model Context Protocol** 中启用 **Auto Start Server**（从下次启动起生效，按用户保存）
2.  将 MCP 客户端连接到 `http://127.0.0.1:8000/mcp`。项目根目录的 `.mcp.json` 已包含此设置。

该工具集还注册了一个 Agent Skill（`KawaiiPhysicsSetupSkill`），其中包含通过 MCP 设置、调整和验证 KawaiiPhysics 节点的提示。

服务器允许客户端控制编辑器，请仅在需要时启动。

## 案例展示

Kawaii Physics 已被许多项目使用！

-   **[案例列表](https://pafuhana1213.github.io/KawaiiPhysics-Portal/en/adoption)**
-   欢迎在[这里](https://github.com/pafuhana1213/KawaiiPhysics/discussions/65)分享你的作品！
-   在 Twitter/X 分享作品时，请使用话题标签 **[#KawaiiPhysics](https://twitter.com/search?q=%23kawaiiphysics&src=typed_query&f=live)**！

## 相关工具

-   [将 VRM SpringBone 转换为 KawaiiPhysics 的工具](https://yumetengu.booth.pm/items/7943387)

## 许可证

[MIT License](https://github.com/pafuhana1213/KawaiiPhysics/blob/master/LICENSE)

## 作者

[Okazu @pafuhana1213](https://twitter.com/pafuhana1213)

## 更新日志

-   [公告](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/announcements-%E3%82%A2%E3%83%8A%E3%82%A6%E3%83%B3%E3%82%B9)
-   [更新日志（KawaiiPhysics Portal）](https://pafuhana1213.github.io/KawaiiPhysics-Portal/en/docs/changelog)

* * *

## 支持我

如果这个插件对你的 UE 项目有过一点点帮助，我会非常高兴！

我一直以个人爱好和实用工具的形式独自开发它——挤出开发时间，甚至自掏腰包支付咖啡费用 ☕  
如果你觉得**“嘿，这个还挺不错！”**，通过 FAB 购买或 GitHub Sponsors 支持我，对我来说意义重大，也能帮助开发持续下去。  
（关于 FAB 上线的背景可以阅读[这里](https://github.com/pafuhana1213/KawaiiPhysics/discussions/170)。）

[💖 **在 FAB 上购买**](https://www.fab.com/ja/listings/f870c07e-0a02-4a78-a888-e52a22794572)  
[💖 **通过 GitHub Sponsors 支持**](https://github.com/sponsors/pafuhana1213)