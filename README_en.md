# Kawaii Physics

Japanese doc:   
[Japanese Documentation](https://github.com/pafuhana1213/KawaiiPhysics)

Download (for those without a C++ build environment):  
Github : https://github.com/pafuhana1213/KawaiiPhysics/releases/  
Booth : https://pafuhana1213.booth.pm/items/5943534

## Discussions
- [Update Information](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/announcements-%E3%82%A2%E3%83%8A%E3%82%A6%E3%83%B3%E3%82%B9)
- [Requests & Ideas](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/ideas-%E8%A6%81%E6%9C%9B-%E3%82%A2%E3%82%A4%E3%83%87%E3%82%A2)
- [Questions & Q&A](https://github.com/pafuhana1213/KawaiiPhysics/discussions/categories/q-a)
- [Share Your Work!](https://github.com/pafuhana1213/KawaiiPhysics/discussions/65)  
- [General Discussion & Advice](https://github.com/pafuhana1213/KawaiiPhysics/discussions/66)  
- [Feature and Sample Requests](https://github.com/pafuhana1213/KawaiiPhysics/discussions/67)

## Bug Reports
[Report Bugs](https://github.com/pafuhana1213/KawaiiPhysics/issues)

## Adoption Records
[Adoption Records](https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E6%8E%A1%E7%94%A8%E5%AE%9F%E7%B8%BE-Adoption)

## Introduction
Kawaii Physics is a pseudo-physics plugin for Unreal Engine 4 and 5. It allows you to create simple and cute animations for objects like hair, skirts, and breasts.

![Kawaii Physics 1](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics1.gif)  
Character used: Gray-chan [Gray-chan](http://rarihoma.xvs.jp/products/graychan)

![Kawaii Physics 0](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics0.gif)  
[YouTube Video](https://www.youtube.com/watch?v=UvpEIBGegvs)  
Character used: Mirai Komachi [Mirai Komachi](https://www.bandainamcostudios.com/works/miraikomachi/)

## Features
![Kawaii Physics 2](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics2.jpg)  
![Kawaii Physics 4](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics4.gif)  
- Respects the original shape while controlling physics based on the animation and movement/rotation of the SkeletalMeshComponent.
- Use the "KawaiiPhysics Node" included in the plugin in the AnimGraph of AnimationBP.
- You can make specified bones and their child bones move according to the character's motion.
- With only two types of physics control parameters, it is easier to set up compared to the engine's standard [AnimDynamics](https://docs.unrealengine.com/en-US/Engine/Animation/NodeReference/SkeletalControls/AnimDynamics/index.html).
- You can add sphere, capsule, and plane collisions.
- Adjust the position, orientation, and size of each collision in the animation editor's viewport.
- Maintains bone length, preventing bones from stretching or shrinking even if calculations fail.
- Uses a simple algorithm without PhysX, which should result in lower overhead compared to the engine's standard physics system.

## Reference Material for Implementing Physics Behavior
[Next Generation Idolmaster Graphics & Animation Programming Preview](https://cedil.cesa.or.jp/cedil_sessions/view/416)

## Operating Environment
- UE5.2 ~ 5.4  
[KawaiiPhysics Releases](https://github.com/pafuhana1213/KawaiiPhysics/releases/)
- UE4.27  
[KawaiiPhysics Release for UE4.27](https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20230830-v1.11.1)
- UE4.26 and earlier (Plugin only)  
[KawaiiPhysics Release for UE4.26 and earlier](https://github.com/pafuhana1213/KawaiiPhysics/releases/tag/20201202-v1.7.0)

When building for UE4.26 and earlier, you need to modify GetSkeleton() to Skeleton.

When building for UE4.21 and earlier, you need to modify GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy() to GEngine->ConstraintLimitMaterialPrismatic->GetRenderProxy(false) in KawaiiPhysicsEditMode.cpp.

When building for UE4.23 and earlier, you need to modify "Type": "UncookedOnly" to "Type": "DeveloperTool" in KawaiiPhysics.uplugin (probably).

## How to Use
- Place the KawaiiPhysics folder in your project's Plugins folder.
- About each parameter: [Parameter Details]([https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E5%90%84%E3%83%91%E3%83%A9%E3%83%A1%E3%83%BC%E3%82%BF%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6](https://github.com/pafuhana1213/KawaiiPhysics/wiki/%E5%90%84%E3%83%91%E3%83%A9%E3%83%A1%E3%83%BC%E3%82%BF%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6--About-each-parameters))

- For BP projects, errors occur during packaging. Please either convert to a C++ project or try the method introduced in the following article.  
[How to Package UE5 KawaiiPhysics for Blueprint Projects - PaperSloth’s Diary](https://papersloth.hatenablog.com/entry/2024/02/14/201629)

### Converting BP Projects to C++ Projects
You can convert to a C++ project by adding C++ code from the "C++ Class Wizard".  
[C++ Class Wizard Documentation](https://docs.unrealengine.com/5.0/en-US/using-the-cplusplus-class-wizard-in-unreal-engine/)

However, you need to prepare an environment that can build C++, such as Visual Studio.  
[Setting Up Visual Studio Development Environment for C++ Projects](https://docs.unrealengine.com/5.0/en-US/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine/)

## Samples
![Kawaii Physics 3](https://github.com/pafuhana1213/Screenshot/blob/master/KawaiiPhysics3.jpg)  
- Content/KawaiiPhysicsSample/KawaiiPhysicsSample
- Character used: Gray-chan [Gray-chan](http://rarihoma.xvs.jp/products/graychan)

## Articles & Videos
### Official
- [Explanation of the Internal Implementation of Custom AnimNode "Kawaii Physics" Part 1](http://pafuhana1213.hatenablog.com/entry/2019/07/26/171046)
- [Test of Using #ControlRig to Prevent Skirt Clipping and Combining with #KawaiiPhysics](https://twitter.com/pafuhana1213/status/1300454762542817280)

### Unofficial (Thank you!)
- [How a Complete Amateur in Jiggle Physics Used KawaiiPhysics to Make a Female Character Model More Kawaii Based on Intuition](https://qiita.com/YuukiOgino/items/7f3198a90dab43019f26)
- [Jiggle Physics and Control Rig Examples from a Virtual Live Using UE](https://www.docswell.com/s/indieusgames/K4Q2XJ-2024-06-14-175045)
- [Setting Jiggle Physics with Kawaii Physics in UE](https://techblog.sumelagi.co.jp/unrealengine/147/)
  - Note: You can adjust the influence per bone to some extent with the "◯◯◯ by bone length rate" property.
- [Kawaii Physics Tutorial #1](https://www.youtube.com/watch?v=hlgXuVML_is)
- [How to Set Up Kawaii Physics in Unreal Engine 5](https://dev.epicgames.com/community/learning/tutorials/d1Z9/unreal-engine-how-to-setup-kawaii-physics-in-unreal)

## License
MIT

## Author
[Okazu @pafuhana1213](https://twitter.com/pafuhana1213)

## Hashtag
[#KawaiiPhysics](https://twitter.com/search?q=%23kawaiiphysics&src=typed_query&f=live)

## History
- 2024/8/16 v1.17.0　[Details Here](https://github.com/pafuhana1213/KawaiiPhysics/discussions/137)
  - Additional RootBones
  - Box Limit
  - PhysicsAsset for Limits
  - AnimNotify / AnimNotifyState for NewExternalForce
  - BP nodes for NewExternalForce
  - NewExternalForce Wind
  - Export BoneConstraint DataAsset
- 2024/7/19 v1.16.0 [Details Here](https://github.com/pafuhana1213/KawaiiPhysics/discussions/128)
  - New External Force
  - Organized and added functions to the Details Panel
  - Added console variables for debugging at the level
  - DataAsset Update
- 2024/2/8 v1.15.0 [Details Here](https://github.com/pafuhana1213/KawaiiPhysics/discussions/117)
  - Added support for UE5.4
- 2024/2/8 v1.14.0
  - Experimental addition of BoneConstraint function to constrain distances between bones
    - ![image](https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/af5845e4-65aa-41c1-ba0e-ae466e90d19f)
    - Maintains the distance between specified bones (BoneConstraints/Bone1, 2) using XPBD. Note that the specified bones must be controlled by KawaiiPhysics.
    - You can specify the number of iterations for constraint processing with Bone Constrain Iteration Count Before(After) Collision.
    - Now configurable in DataAsset. Added experimental feature to set bone sets for constraints using regular expressions.
      ![image](https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/a43faf14-181e-4b95-8d16-5eb4b687dd12)
  - Exposed the DataAsset held by KawaiiPhysics AnimNode to the pin
  - Fixed inefficiencies in Planar Limit processing [PR 108](https://github.com/pafuhana1213/KawaiiPhysics/pull/108)
  - Refactored the code of the physics processing part to accommodate BoneConstraint
- 2023/9/15 v1.13.0
  - Added warm-up function and sample for initial physical calculations
    - [Warm Up Video](https://www.youtube.com/watch?v=stIOjZQh3Qw)
    - ![image](https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/e140f3b9-232a-4026-a0b0-c8e1e54a492f)
    - Warm Up is enabled when Need Warm Up is true and Warm Up Frame (number of frames for warm-up) is 1 or more.
    - Need Warm Up becomes false after execution.
    - Larger Warm Up Frame results in more stability, but increases the load due to more physical calculations.
    - ![image](https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/ef55a7fd-699b-48ba-9bd8-13be62d23961)  
      Warm Up can also be enabled from the AnimNode function since v1.12.0.
- 2023/9/8 v1.12.0
  - Support for UE5.3
    - Nothing specific was done.
  - Parameters can now be changed and retrieved from AnimNode functions.
    - ![image](https://github.com/pafuhana1213/KawaiiPhysics/assets/8957600/6ae6098a-64f3-4138-8822-704426dd70f4)
    - Added sample (AnimBP_GrayChanKawaiiPhysicsAnimNodeFunction).
  - Ended support for UE4.27 due to support for AnimNode functions.
- 2023/8/30 v1.11.1
  - Fixed crash when used with LevelSequence and ControlRig [Commit](https://github.com/pafuhana1213/KawaiiPhysics/commit/5a2fd868d9ccbd87b3727614faeb861cd7854d8a)
  - Normalize AdjustByAngleLimit [Commit](https://github.com/pafuhana1213/KawaiiPhysics/commit/9a0576cfa06d37eb0c1b35f57757737ac61288b8)
  - Fixed issue where LimitAngle was not exposed to the pin [Commit](https://github.com/pafuhana1213/KawaiiPhysics/commit/937a1247f96190700fe06ca7559274059e62d111)
- 2023/5/28 v1.11.0
  - Support for UE5.2
    - Nothing specific was done.
  - Added sample combining KawaiiPhysics and Sequencer.
    - Check if the SkeletalMeshComponent's AnimClass is set to AnimBlueprint and if there is a Slot node in the AnimGraph.
  - Made property changes reflect in behavior without compilation.
    - Added support for CopyNodeDataToPreviewNode which was missing.
  - Fixed issue where collider (Limits) preview did not work properly in UE5.1 (second and subsequent colliders did not appear without compilation) [Issue 84](https://github.com/pafuhana1213/KawaiiPhysics/issues/84)
  - Fixed crash when editing rotation of collider (Limits) [Issue 86](https://github.com/pafuhana1213/KawaiiPhysics/issues/86)
  - Fixed crash when deleting Limits from DataAsset [Issue 72](https://github.com/pafuhana1213/KawaiiPhysics/issues/72)
  - Fixed crash when DrivingBone of collider (Limits) is removed by LOD [Issue 87](https://github.com/pafuhana1213/KawaiiPhysics/issues/87)
  - (Probably) fixed crash when used with Control Rig and AnimationLayer [Issue 56](https://github.com/pafuhana1213/KawaiiPhysics/issues/56)
  - Added LOD1 and 2 to sample model for operation check.
- 2022/12/03 v1.10.0
  - Support for UE5.1
  - Fixed crash when bones are removed by LOD.
  - Some code refactored by Rider.
- 2022/4/11 v1.9.0
  - Parameters can now be edited with curves without creating a curve asset [PR 32](https://github.com/pafuhana1213/KawaiiPhysics/pull/32)
  - Added world collision feature and sample [PR 26](https://github.com/pafuhana1213/KawaiiPhysics/pull/26)
  - Fixed issue where DummyBone is not generated when the terminal bone does not function due to LOD settings [PR 26](https://github.com/pafuhana1213/KawaiiPhysics/pull/26)
  - Fixed issue where physics settings are reset during ResetPhysics [Issue 38](https://github.com/pafuhana1213/KawaiiPhysics/issues/38)
  - Added process to suppress erratic behavior during LOD switching. ResetBoneTransformWhenBoneNotFound can be enabled to revert to the previous process [Issue 44](https://github.com/pafuhana1213/KawaiiPhysics/issues/44)
- 2022/4/11 v1.8.0
  - Support for UE5 official release.
  - Temporarily dropped support for UE4.26 and earlier.
- 2020/12/2 v1.7.0
  - Added support for UAnimInstance::ResetDynamics(ETeleportType::ResetPhysics).
    - Explanation: [Twitter](https://twitter.com/pafuhana1213/status/1334141624666910722)
    - [PR 30](https://github.com/pafuhana1213/KawaiiPhysics/pull/30)
      - Thanks to KazumasaOhashi!
    - You can test with key 2 in the sample level.
- 2020/11/17 v.1.6.2
  - Fixed issue where FKawaiiPhysicsEditMode::DrawHUD was not functioning correctly.
- 2020/9/4 v1.6.1  
  - Fixed error in package creation due to missing property category specification.
- 2020/8/29 v1.6.0
  - Added functionality to manage and reuse collision settings with a dedicated DataAsset (KawaiiPhysicsLimitsDataAsset).
    - Usage: [Video](https://youtu.be/jnJqRu7zn3c)
    - Not much operational experience, so consider it experimental.
  - Set UE4.25.3 as default.
- 2020/7/28 v1.5.3
  - Fixed issue where correct length could not be obtained and parameters with curves did not work properly when BoneIndex of Mesh and Skeleton differ [PR 21](https://github.com/pafuhana1213/KawaiiPhysics/pull/21)
    - Thanks to nkinta!
- 2020/5/29 v1.5.2  
  - Fixed issue where response in v1.5.1 did not consider DummyBone.
- 2020/5/29 v1.5.1
  - Fixed issue where debug display by bEnableDebugBoneLengthRate was misaligned.
  - Unified text format of debug display with engine standard bone name display [Twitter](https://twitter.com/pafuhana1213/status/1266264162788765696)
- 2020/5/29 v1.5.0
  - Added BoneForwardAxis property to set bone orientation, affecting DummyBone behavior only.  
  (Thanks to SAM-tak!)
    - [Twitter](https://twitter.com/pafuhana1213/status/1266224108657762307)
- 2020/5/15 v1.4.6  
  - Fixed crash when the same bone as Root is specified in ExcludeBone.
  - Added process to display node as Warning or Error in case of invalid settings.
    - Technical explanation: [Blog](http://pafuhana1213.hatenablog.com/entry/2020/05/16/173837)
- 2020/5/15 v1.4.5
  - Changed Gravity to operate in ComponentSpace.  
  (Thanks to dxd39!)
- 2020/3/13 v1.4.4  
  - Fixed build failure on MacOS.  
  (Thanks to melMass!)
- 2020/3/13 v1.4.3  
  - Fixed issue with package creation failure.
- 2020/2/16 v1.4.2  
  - Fixed issue where it did not function properly during standalone execution.
- 2020/1/31 v1.4.1  
  - Fixed issue where Radius setting of bone was not considered when LimitType of SphereLimit was Inner.
  Set CVarEnableOldPhysicsMethodSphereLimit to 1 to revert to behavior before v1.3.1.  
- 2020/1/31 v1.4.0  
  - Fixed calculation of position update of gravity variable to gt instead of gt^2.
  Set p.KawaiiPhysics.EnableOldPhysicsMethodGravity to 1 to use gravity calculation before v1.3.1.  
- 2020/1/31 v1.3.1  
  - Fixed issue where radian and degree were mixed in the calculation of TeleportRotationThreshold, causing almost no function of WorldRotation teleport support.
  (Thanks to monguri!)  
- 2020/1/31 v1.3.0  
  - Support for UE4.24.2.  
  (Thanks to TheHoodieGuy02!)  
- 2019/10/29 v1.2.1  
  - Reverted World Damping to previous processing, making behavior more stable at 30fps.  
  (Thanks to @seiko_dev!)  
- 2019/10/26 v1.2.0  
  - Added simple variable frame rate support. Behavior should be slightly more stable during frame rate drops.  
  (Thanks to @seiko_dev!)  
  - You can now set the reference frame rate (default: 60). (Honestly, this is under-tested. If there are any issues, please report them to the issue tracker.)
  - Added debug display of the percentage of length from the starting point of each bone for curve adjustment.
- 2019/10/19 v1.1.2   
  - Fixed issue where Bone.Scale was not supported for scales other than (1,1,1).  
  (Thanks to shop-0761!)  
- 2019/9/11 v1.1.1  
  - Fixed incorrect calculation of Bone.LengthFromRoot.  
  (Thanks to KazumasaOhashi!)  
- 2019/8/26 v1.1   
  - Added support for WindDirectionalSource (Note: destructive changes to existing Wind settings).
- 2019/7/20 v1.0.1   
  - Fixed issue where Collision's OffsetLocation did not consider bone rotation.  
  (Behavior of OffsetLocation changes significantly from v1.0.)  
- 2019/7/2 v1.0 Initial release.
