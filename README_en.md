# Kawaii Physics

![UE Version](https://img.shields.io/badge/Unreal%20Engine-5.3--5.6-0e1128?logo=unrealengine&logoColor=white)
[![GitHub release (latest by tag)](https://img.shields.io/github/v/release/pafuhana1213/KawaiiPhysics)](https://github.com/pafuhana1213/KawaiiPhysics/releases)
[![Downloads](https://img.shields.io/github/downloads/pafuhana1213/KawaiiPhysics/total)](https://github.com/pafuhana1213/KawaiiPhysics/releases)
[![Discussions](https://img.shields.io/github/discussions/pafuhana1213/KawaiiPhysics?logo=github)](https://github.com/pafuhana1213/KawaiiPhysics/discussions)
[![GitHub contributors](https://img.shields.io/github/contributors/pafuhana1213/KawaiiPhysics?logo=github)](https://github.com/pafuhana1213/KawaiiPhysics/graphs/contributors)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/pafuhana1213/KawaiiPhysics)
[![X (formerly Twitter) Follow](https://img.shields.io/twitter/follow/pafuhana1213?style=social)](https://twitter.com/pafuhana1213)

[日本語 (Japanese)](https://github.com/pafuhana1213/KawaiiPhysics/blob/master/README.md)

---

Kawaii Physics is a simple pseudo-physics plugin for Unreal Engine.  
It allows you to easily and cutely animate things that sway, such as hair, skirts, and breasts.

<a href="https://youtu.be/0f-l-SP07Mo">
  <img src="https://github.com/user-attachments/assets/0bc33f5c-c7db-49b3-8e98-75dc062a4e2a" alt="Demo Video" width="640px">
</a>

[Demo Video](https://youtu.be/0f-l-SP07Mo)

## 🖼️ Gallery

<img src="https://github.com/user-attachments/assets/fda69859-b60f-4fde-a683-62da3e2839e4" alt="compare" width="640px">

*Character: [Gray-chan](http://rarihoma.xvs.jp/products/graychan)*

<img src="https://github.com/user-attachments/assets/28d72d0c-4423-41c7-bc52-c5c7c3886e02" alt="dance5" width="640px">

*Character: [Original 3D Model "Lzebul"](https://booth.pm/ja/items/4887691) / Motion: [Mirai Komachi](https://www.miraikomachi.com/download/)*

<img src="https://github.com/user-attachments/assets/63faed3c-8aaa-4d4d-ae33-e98f9c8c15fd" alt="danceKano" width="640px">

*Character: [TA-style Kano Saginomiya](https://uzurig.com/ja/uzurig2-rigging-plugin-for-maya-jp/) / Motion: [Shikanokonokonokoshitantan](https://booth.pm/ja/items/5975857) / Setup: [TA Co., Ltd.](https://xta.co.jp/)*

## ✨ Features

<table>
  <tr>
    <td><img src="https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics2.jpg?raw=true" width="320"></td>
    <td><img src="https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics4.gif?raw=true" width="320"></td>
  </tr>
</table>

- Physics control based on animation and character movement
- Easy setup with a single `KawaiiPhysics` node in the AnimGraph
- Supports sphere, capsule, and plane collisions, which can be intuitively adjusted in the viewport
- Stability that prevents the skeleton from stretching or shrinking even if calculations fail
- Lightweight algorithm that does not depend on PhysX
- External forces such as wind and gravity can be applied for artistic purposes
- Parameters can be saved and shared using `DataAsset` and `PhysicsAsset`

### Reference Material

The following materials were used as a reference for implementing the physics behavior:
- [Next Idolmaster Graphics & Animation Programming Preview](https://cedil.cesa.or.jp/cedil_sessions/view/416)

## 🚀 Getting Started

### 1. Download

If you do not have a C++ build environment, please download the plugin from the following links:

- **[GitHub Releases](https://github.com/pafuhana1213/KawaiiPhysics/releases/)**
- **[FAB](https://www.fab.com/ja/listings/f870c07e-0a02-4a78-a888-e52a22794572)** 
- **[Booth](https://pafuhana1213.booth.pm/items/5943534)** (Follow the store to receive update notifications)

### 2. Supported Versions

- **Unreal Engine 5.3 ~ 5.6**
- UE4.27: [v1.11.1](https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20230830-v1.11.1)

### 3. Installation

1. Create a `Plugins` folder in the root of your project.
2. Copy the downloaded `KawaiiPhysics` folder to the `Plugins` folder.

### 4. How to Use

- Add and use the `KawaiiPhysics` node in the AnimGraph of the Animation Blueprint.
- For detailed parameter settings, please refer to the Wiki.
  - **[About Each Parameter](https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E5%90%84%E3%83%91%E3%83%A9%E3%83%A1%E3%83%BC%E3%82%BF%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6--About-each-parameters)**

> **Note:** An error may occur when packaging a project that only uses Blueprints. In that case, please convert it to a C++ project or refer to [this article](https://papersloth.hatenablog.com/entry/2024/02/14/201629).

## 📚 Documentation & Community

- **[DeepWiki](https://deepwiki.com/pafuhana1213/KawaiiPhysics)**: AI-generated documentation
- **[Discussions](https://github.com/pafuhana1213/KawaiiPhysics/discussions)**: For questions, requests, and general chat
- **[Issues](https://github.com/pafuhana1213/KawaiiPhysics/issues)**: Bug reports

## 🎓 Tutorials & Articles

### Official

- [Internal Implementation Explanation of the Self-Made AnimNode "Kawaii Physics" Part 1](http://pafuhana1213.hatenablog.com/entry/2019/07/26/171046)
- [Test of using #ControlRig to prevent skirt penetration in conjunction with #KawaiiPhysics](https://twitter.com/pafuhana1213/status/1300454762542817280)

### Unofficial (Thank you!)

- [How To Setup Kawaii Physics in Unreal Engine 5](https://dev.epicgames.com/community/learning/tutorials/d1Z9/unreal-engine-how-to-setup-kawaii-physics-in-unreal)

- [The Hidden Physics “Engine” Behind Unreal's Most Stylish Games ...](https://www.youtube.com/watch?v=9ThmoMHnHhw)
- In Japanese
  - [A Complete Beginner's Guide to Increasing a Female Character's Kawaii with KawaiiPhysics](https://qiita.com/YuukiOgino/items/7f3198a90dab43019f26)
  - [Swaying Objects and Control Rig: A Case Study in Virtual Live Using UE](https://www.docswell.com/s/indieusgames/K4Q2XJ-2024-06-14-175045)
  - [【UE】Swaying Object Settings (Kawaii Physics)](https://techblog.sumelagi.co.jp/unrealengine/147/)
  - [【UE4】Kawaii Physics Tutorial #1](https://www.youtube.com/watch?v=hlgXuVML_is)
  - [Learn Kawaii Physics in 5 minutes!【UE5】【tutorial】](https://www.youtube.com/watch?v=TliP9vSxm4c)

## 🎮 Sample

<img src="https://github.com/user-attachments/assets/0d866ad2-f803-400b-bd23-2d46ab17b8ae" alt="sample2" width="640px">

This sample project can be downloaded from Github includes a sample level and characters.
- **Sample Level**: `Content/KawaiiPhysicsSample/L_KawaiiPhysicsSample`
- **Characters Used**:
  - **Gray-chan**: http://rarihoma.xvs.jp/products/graychan
  - **TA-style Kano Saginomiya**: Provided by [TA Co., Ltd.](https://xta.co.jp/)
    - Copyright (c) 2025 TA Co., Ltd. All rights reserved
    - Terms of Use: https://uzurig.com/ja/terms_of_use_jp/

## 🌟 Showcase

It has been adopted in many projects!
- **[Showcase List](https://github.com/pafuhana1213/KawaiiPhysics/wiki/Adoption-Record)**
- Please share your work with us [here](https://github.com/pafuhana1213/KawaiiPhysics/discussions/65)!
- When sharing your work on Twitter/X, please use the hashtag **[#KawaiiPhysics](https://twitter.com/search?q=%23kawaiiphysics&src=typed_query&f=live)**!

## 📜 License

[MIT License](https://github.com/pafuhana1213/KawaiiPhysics/blob/master/LICENSE)

## 👨‍💻 Author

[Okazu @pafuhana1213](https://twitter.com/pafuhana1213)

## 📅 Changelog

[Announcements](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/announcements-%E3%82%A2%E3%83%8A%E3%82%A6%E3%83%B3%E3%82%B9)

---

## ✨ Support Me If You’d Like!

If this plugin has been even a little helpful for your UE projects, I’m truly glad to hear that!

I’ve been developing it solo as both a hobby and a practical tool — squeezing out development time and even covering coffee costs out of my own pocket ☕  
If you think “Hey, this is pretty good!”, your support via a purchase on FAB or through GitHub Sponsors would mean a lot to me and help keep development going.  
(You can read more about the background behind the FAB launch [here](https://github.com/pafuhana1213/KawaiiPhysics/discussions/170).)

[💖 **Buy on FAB**](https://www.fab.com/ja/listings/f870c07e-0a02-4a78-a888-e52a22794572)  
[💖 **Support via GitHub Sponsors**](https://github.com/sponsors/pafuhana1213)
