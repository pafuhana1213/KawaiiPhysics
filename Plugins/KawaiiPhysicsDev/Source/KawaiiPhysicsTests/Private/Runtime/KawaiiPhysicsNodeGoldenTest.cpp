// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimNode_KawaiiPhysics.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Basic.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformMisc.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/Parse.h"
#include "UObject/StrongObjectPtr.h"

// 積分結果の解析的な基準値は用意できないため、このテストは本番経路の回帰検出専用とする。
// 【運用注意】全軌跡の位置と回転はビット厳密一致、最終フレームは位置だけを比較する。
// CPU・CRT・コンパイラが変わると最下位ビットが変わり得るため、同じ PC での変更前後の比較に使う。
// 環境が変わったら -KPGoldenCapture で採り直して期待値を更新する。
// 採取環境が未記録なら失敗し、実行環境と違う場合は警告を出して比較を省く。
namespace KawaiiPhysicsNodeGoldenTestPrivate
{
	static_assert(sizeof(double) == sizeof(uint64));
	static const TCHAR* const Golden_Environment = TEXT("5.8.4-58902052+++UE5+Release-5.8|MSVC 194435222|AMD Ryzen 9 3950X 16-Core Processor            ");

	FString BuildNodeGoldenEnvironment()
	{
		FString Compiler;
#if defined(_MSC_FULL_VER)
		Compiler = FString::Printf(TEXT("MSVC %d"), _MSC_FULL_VER);
#elif defined(__clang__)
		Compiler = FString::Printf(TEXT("Clang %d.%d.%d"),
			__clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
		Compiler = FString::Printf(TEXT("GCC %d.%d.%d"),
			__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
		Compiler = TEXT("unknown");
#endif
		return FString::Printf(TEXT("%s|%s|%s"), *FEngineVersion::Current().ToString(),
			*Compiler, *FPlatformMisc::GetCPUBrand());
	}

	constexpr int32 GBoneCount = 6;
	constexpr int32 GFrameCount = 120;
	constexpr int32 GComponentsPerBone = 3;
	constexpr int32 GHashComponentsPerBone = 7;

	// 採取した値はシナリオごとにこの配列へ貼り付ける（先頭が全軌跡のハッシュ、続く 18 要素が最終フレーム）。
	static const TArray<uint64> Golden_Fixed60 = { 0x59b95d7d6a9d7584ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022cec7f9318056ULL, 0x0000000000000000ULL, 0xc00b35614fa3fd1eULL, 0x4032cec7f09bc099ULL, 0x0000000000000000ULL, 0xc01b3561ae96e540ULL, 0x403c3624985a35c2ULL, 0x0000000000000000ULL, 0xc0246831b595c6caULL, 0x4042ceb3ede55dbcULL, 0x0000000000000000ULL, 0xc02b363ef42e8fe8ULL, 0x4047828ef0e6ff04ULL, 0x0000000000000000ULL, 0xc03100e8ae24267bULL, };
	static const TArray<uint64> Golden_Fixed30 = { 0x2f3e1f52ea6e5480ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022cec7e03db63fULL, 0x0000000000000000ULL, 0xc00b3562639c5c23ULL, 0x4032cec7e03dba12ULL, 0x0000000000000000ULL, 0xc01b3562639c31d5ULL, 0x403c362bd05ba401ULL, 0x0000000000000000ULL, 0xc0246809caba65b6ULL, 0x4042cec7e037ee0cULL, 0x0000000000000000ULL, 0xc02b356263dc4efaULL, 0x40478279d8586247ULL, 0x0000000000000000ULL, 0xc031015d7e038bc4ULL, };
	static const TArray<uint64> Golden_Fixed144 = { 0x3cda55c9f5b71750ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022d08a9184a656ULL, 0x0000000000000000ULL, 0xc00b21e1df513092ULL, 0x4032d28ad2e46e12ULL, 0x0000000000000000ULL, 0xc01b0b99bb7ce01fULL, 0x403c2a66671e0f82ULL, 0x0000000000000000ULL, 0xc024a6c1bdd43547ULL, 0x4042a33d03731edfULL, 0x0000000000000000ULL, 0xc02ce6a53f727b65ULL, 0x404731230186951dULL, 0x0000000000000000ULL, 0xc03293e289bd5d51ULL, };
	static const TArray<uint64> Golden_ComponentMove = { 0x53a3bf91ada2558bULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x40215540deb2cecbULL, 0x3fe4151cbc3840caULL, 0xc013cc4ecd8f7d37ULL, 0x4031268519d9b428ULL, 0x3fc366dca0073a84ULL, 0xc024718292aefae6ULL, 0x4039591c15767da5ULL, 0xbff7de395dda195eULL, 0xc02f6a73b42cddcfULL, 0x404093a83b28aee3ULL, 0xc01140ee5ecaf146ULL, 0xc03549292a056bfcULL, 0x4044404240ade829ULL, 0xc0207a1d1f4bdc04ULL, 0xc03ad12458b2c114ULL, };
	static const TArray<uint64> Golden_PoseMove = { 0x528d86baf574ecbaULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022982c601ab9b0ULL, 0x3ff5bd0e95bc0455ULL, 0xc00b61fcebc512ceULL, 0x40326d3c8039e7aeULL, 0x400c7fe7b1b02c6aULL, 0xc01b6de79e5b1dc7ULL, 0x403b5cc20711e113ULL, 0x401a22c7e94a2934ULL, 0xc02471f0145037daULL, 0x40421374c5fee465ULL, 0x4024289a40e88031ULL, 0xc02ad2eb28e4a0b0ULL, 0x404672110f58a0a8ULL, 0x402bcf805e2682e8ULL, 0xc03069006286a5faULL, };
	static const TArray<uint64> Golden_BaseBoneMove = { 0x7aeed179d3f2ca42ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x401e5aab60ab2776ULL, 0x4013b70d98a4e407ULL, 0xc0110713aec8eb7eULL, 0x402ef252eef99d32ULL, 0x40216bd55be34590ULL, 0xc022370d00825479ULL, 0x403609f6e54571f0ULL, 0x401742f0d6dcc756ULL, 0xc030128b196e9671ULL, 0x4034bb010101420cULL, 0xc005d0bcf886a089ULL, 0xc0351aa341a6f7e7ULL, 0x402a6922f983802aULL, 0xc0229fe6fb944aaaULL, 0xc0352caf6d43501cULL, };
	static const TArray<uint64> Golden_WorldMove = { 0x3b8cf18131881b31ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x402289373405e60cULL, 0xbfd9ee5dc1539280ULL, 0xc00dde7b8a39ff08ULL, 0x4032824c22600b20ULL, 0xbfed87a320860bc0ULL, 0xc01e14766a092a50ULL, 0x403bb8d98b6bd57cULL, 0xbff8f296ac734e00ULL, 0xc026b6317c310f9aULL, 0x40427426bb1ff958ULL, 0xc0028f3e29f62a6cULL, 0xc02e787db3e38c62ULL, 0x404708881ef9d080ULL, 0xc009a8b5ce1bf050ULL, 0xc033264918ceaae6ULL, };
	static const TArray<uint64> Golden_LegacyFixedPose = { 0x24be5d94f3c23dc4ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022cec7f7a47cd0ULL, 0x0000000000000000ULL, 0xc00b356160caed22ULL, 0x4032cec82445940eULL, 0x0000000000000000ULL, 0xc01b355f73324446ULL, 0x403c36266c8d22e5ULL, 0x0000000000000000ULL, 0xc024682798acf665ULL, 0x4042ceb2daee7e19ULL, 0x0000000000000000ULL, 0xc02b364ad2bbf917ULL, 0x40478271a6169c4dULL, 0x0000000000000000ULL, 0xc031018ac72ee3ddULL, };
	static const TArray<uint64> Golden_LegacyPoseMove = { 0xd903c061a875f0b3ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022981c571a9b9dULL, 0x3ff6240063a9cf80ULL, 0xc00b4e0512be4284ULL, 0x403269e53d315adcULL, 0x400d34f20df9b503ULL, 0xc01b5ce96b089f5cULL, 0x403b4cad5b636f5bULL, 0x401b0a4a059c1330ULL, 0xc0246d37559f83bcULL, 0x4041fd8b99feb807ULL, 0x40251b43224e2222ULL, 0xc02ad4257f02fe37ULL, 0x404645bfeccd5bfdULL, 0x402d8b420cdc0b50ULL, 0xc0306402ad1e7074ULL, };
	static const TArray<uint64> Golden_BasicBoneForce = { 0x573f367110bd192eULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4021e1455c1cb867ULL, 0x4010afd64c7fa602ULL, 0xbffa294ed3d60ec4ULL, 0x4031b67842dd5ae1ULL, 0x4021463706a047f6ULL, 0xc00b2974824b361aULL, 0x403a46dcd3d8879eULL, 0x402ae154442a8ac5ULL, 0xc01528a2fcb38492ULL, 0x40414ae0c669231eULL, 0x40329ec73811200cULL, 0xc01d4c468ae46208ULL, 0x40454cc28ede3958ULL, 0x403830d8f9eb7562ULL, 0xc02300089656216fULL, };
	static const TArray<uint64> Golden_BasicWorldForce = { 0xf308b4145000225cULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022ee0812b0a873ULL, 0x3fee1e822d35992dULL, 0xc008b2351d6e0e9aULL, 0x4032e411a683e976ULL, 0x3ffecc67a0ee28c4ULL, 0xc0191cc9c2d0de2eULL, 0x403c458d443f559eULL, 0x4007b38948490683ULL, 0xc0232a9cea063fceULL, 0x4042cd1e21bf06dcULL, 0x40103da95244ea94ULL, 0xc02a0449edd0ca42ULL, 0x404770c3421a452aULL, 0x4014de2c25380bc4ULL, 0xc0308dd6395bbdedULL, };
	static const TArray<uint64> Golden_SphereCapsule = { 0x36c51b46d9f88040ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022cec7f9318056ULL, 0x0000000000000000ULL, 0xc00b35614fa3fd1eULL, 0x402c9102bb32b426ULL, 0x0000000000000000ULL, 0x40154f99238a5399ULL, 0x403843dd8eb0d05cULL, 0x0000000000000000ULL, 0x4017b7d7161103e7ULL, 0x403f37baf52e5ac6ULL, 0x0000000000000000ULL, 0xbff420fdf595373cULL, 0x403efd4c1d985deeULL, 0x0000000000000000ULL, 0xc02682ca42578b6cULL, };

	static const TArray<uint64> Golden_BoneConstraint = { 0x6afac41f6d191026ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x401e6fd1b43bdf5cULL, 0x0000000000000000ULL, 0x4019f42e967da3edULL, 0x402e30538c29db09ULL, 0x0000000000000000ULL, 0xbfc2433173640360ULL, 0x4030ae556f2d8c62ULL, 0x0000000000000000ULL, 0xc02408322ba11992ULL, 0x402d0521714c1e38ULL, 0x0000000000000000ULL, 0xc033c70ac481f416ULL, 0x401e320444cfcb36ULL, 0x0000000000000000ULL, 0xc03af4efe3c7dda8ULL, };
	static const TArray<uint64> Golden_SubdivisionDynamic = { 0x6ffbd3f7bdcca0d1ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x40217ddab7aba213ULL, 0x0000000000000000ULL, 0x3ff2ed9c52307f2dULL, 0x4030cad5b1efb16fULL, 0x0000000000000000ULL, 0xc01303d9019c19e3ULL, 0x4038e54411d77768ULL, 0x0000000000000000ULL, 0xc0253a25bab73c74ULL, 0x40407e557166ba81ULL, 0x0000000000000000ULL, 0xc0307d5b53a190fcULL, 0x4044897f07d228dcULL, 0x0000000000000000ULL, 0xc0365eeabc08cb72ULL, };
	static const TArray<uint64> Golden_SubdivisionCollisionOnly = { 0x2e1e9b3fd419b3d6ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x40203ede7ff35c53ULL, 0x0000000000000000ULL, 0x401754ac70ed2cfdULL, 0x403039ada74a0552ULL, 0x0000000000000000ULL, 0xbf9ccf59ad6cdc00ULL, 0x403853eacd68f7efULL, 0x0000000000000000ULL, 0xc0178e521474b898ULL, 0x40403719ced0cf06ULL, 0x0000000000000000ULL, 0xc0277fca26eb0252ULL, 0x4044445b72a83642ULL, 0x0000000000000000ULL, 0xc0319be4c8fab714ULL, };
	static const TArray<uint64> Golden_WorldLimits = { 0x30fe9c596f76ff5aULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x40235bac1402215cULL, 0x3ff7d7b92fe8ce40ULL, 0xc0003094bf92a0a8ULL, 0x4030aae6135f01eaULL, 0x4008fabdfc7c7020ULL, 0x4013c2c410490121ULL, 0x4036914791ff86e4ULL, 0xc013c132ee89f41cULL, 0x40159774718d9387ULL, 0x403e61080f52f334ULL, 0xc00d90045e1c8880ULL, 0xbfe70dca34ab41b0ULL, 0x404425114e2d3902ULL, 0xc00f99c03fbe6878ULL, 0xc0003e269b725a84ULL, };
	static const TArray<uint64> Golden_LimitAngle = { 0xc587bae199fb4924ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022cec7f9318056ULL, 0x0000000000000000ULL, 0xc00b35614fa3fd1eULL, 0x4032cec7f15775feULL, 0x0000000000000000ULL, 0xc01b3561a67adb1dULL, 0x403c3625d3df7452ULL, 0x0000000000000000ULL, 0xc024682ae4ef3ad3ULL, 0x4042ceb80ca170a6ULL, 0x0000000000000000ULL, 0xc02b36116722de1eULL, 0x40478284136bd882ULL, 0x0000000000000000ULL, 0xc0310124d9b407f4ULL, };
	static const TArray<uint64> Golden_Fixed10 = { 0x7471c66faffcb7eeULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022cec7e03db542ULL, 0x0000000000000000ULL, 0xc00b3562639c6714ULL, 0x4032cec7e03db541ULL, 0x0000000000000000ULL, 0xc01b3562639c6720ULL, 0x403c362bd05c8fe2ULL, 0x0000000000000000ULL, 0xc0246809cab54d50ULL, 0x4042cec7e03db540ULL, 0x0000000000000000ULL, 0xc02b3562639c6720ULL, 0x40478279d84d2291ULL, 0x0000000000000000ULL, 0xc031015d7e41c070ULL, };
	static const TArray<uint64> Golden_DummyBone = { 0xdb35ab0dd5486f10ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022cec7f9318056ULL, 0x0000000000000000ULL, 0xc00b35614fa3fd1eULL, 0x4032cec7f09bc099ULL, 0x0000000000000000ULL, 0xc01b3561ae96e540ULL, 0x403c3624985a35c2ULL, 0x0000000000000000ULL, 0xc0246831b595c6caULL, 0x4042ceb3ede55dbcULL, 0x0000000000000000ULL, 0xc02b363ef42e8fe8ULL, 0x4047828ef0e6ff04ULL, 0x0000000000000000ULL, 0xc03100e8ae24267bULL, };
	static const TArray<uint64> Golden_DroppedComponentMove = { 0x4ad69a8668d73bb6ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x4022908359ed6762ULL, 0xbfdf2589bf5e3bbcULL, 0xc00d817865fe377bULL, 0x40328b5800b65f8aULL, 0xbff09fa13d5fefb9ULL, 0xc01dabbfc0f309ceULL, 0x403bc92bbc1f51aeULL, 0xbffa813a7522ff1eULL, 0xc026601ffae3912dULL, 0x404280d062cdf650ULL, 0xc002b728c5d6672aULL, 0xc02dfecc124b6772ULL, 0x40471a4db2922012ULL, 0xc008b3896007403cULL, 0xc032d8c898ca9890ULL, };
	static const TArray<uint64> Golden_DistanceTeleportComponent = { 0xcf052f4fe754df58ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x3fe916a0a13e7e8fULL, 0xc00c4fa05f184db5ULL, 0xc022a3d1c8c80ae7ULL, 0x400de03949282799ULL, 0xc00f01a8058f5f86ULL, 0xc032de6c9492960cULL, 0x400f18f84320b7e6ULL, 0xc008f1bf0b09616cULL, 0xc03cd6c3a68fa09dULL, 0x3fbee7497a5bda00ULL, 0xc018e28acf4b538dULL, 0xc042c89bf49b1ce8ULL, 0xc01297be0530986bULL, 0xc02a3dfe15da0ae8ULL, 0xc0458196b7cd2e5fULL, };
	static const TArray<uint64> Golden_DistanceTeleportBaseBone = { 0x4ad4639a59a1738dULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x3fe400763aa260c0ULL, 0x4003871a7aadf7a6ULL, 0xc0235accc4cabce1ULL, 0x401a1415e7f8c6f4ULL, 0xbfcddf688047de74ULL, 0xc0314cc0892c7669ULL, 0x40267fa765231e26ULL, 0xc01870460b62e391ULL, 0xc037dd653d579a18ULL, 0x402bf2584bb64954ULL, 0xc02cad7665b74322ULL, 0xc03cd9d40eeccd77ULL, 0x402d214bab96660eULL, 0xc037a91e322c83a4ULL, 0xc0403607460ab31fULL, };
	static const TArray<uint64> Golden_RotationTeleport = { 0x80caebb5ce86eb09ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x401a31248291c10bULL, 0xc0175696427927e5ULL, 0xc01337a6d864150bULL, 0x40234d51e7a99cb8ULL, 0xc02c298272b66fbaULL, 0xc02311395aaa3230ULL, 0x4021e199a20b8d44ULL, 0xc036f36e398d74c8ULL, 0xc02c3182a7c7fd17ULL, 0x401291193c3e2adfULL, 0xc03ed658929833aaULL, 0xc0327e1c421f7df6ULL, 0xc0056b0704297ab2ULL, 0xc0423e693e636f8cULL, 0xc0364d2a221eebadULL, };
	static const TArray<uint64> Golden_YawWrap = { 0x01f25c323ff460aaULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x40221d97f4c49b22ULL, 0x3fddcb46fd44ddc3ULL, 0xc010d8ead4abae18ULL, 0x40310d1c1052d182ULL, 0xc00165394270e1ecULL, 0xc023381f38e15526ULL, 0x4035a489fb60e6a6ULL, 0xc01ab80f8be98b3aULL, 0xc031441789be997cULL, 0x40359ae8eb073caaULL, 0xc0226c5458faa8b0ULL, 0xc03af0a9ffb51db8ULL, 0x40336debd14c6827ULL, 0xc018902b2e901850ULL, 0xc0421a39c591ee04ULL, };
	static const TArray<uint64> Golden_SlowRotation = { 0xb2285821998215d7ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x3fd03f2a8fba7500ULL, 0xc023f8672be8a060ULL, 0x3fded5300a0708b8ULL, 0x40076686ecd77300ULL, 0xc0339ecce0484996ULL, 0x3fe4d1ce14ac0033ULL, 0x401c021dd1f360e9ULL, 0xc03cc0052c467f62ULL, 0x3fdf6f86a0207b1eULL, 0x4027eaeb77f54854ULL, 0xc042b6b1880403a2ULL, 0x3fbf2aed84dea680ULL, 0x4031812a957a71e4ULL, 0xc046de06bc705a25ULL, 0xbfd7288deed23e87ULL, };
	static const TArray<uint64> Golden_BaseBoneSubsteps = { 0xfd3d2fb8362bc7d1ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x402199c3705d4ef2ULL, 0x4002b4a4cc303905ULL, 0xc01088df42b63822ULL, 0x40311ddb9a0a6ff7ULL, 0x401798e900a48cd6ULL, 0xc020ca2463c20452ULL, 0x4038e7c7c2ee511eULL, 0x4024ed5ed2f30deeULL, 0xc029647c736731cbULL, 0x40401cc651896253ULL, 0x402fa0b507674656ULL, 0xc030ea55b4b16f08ULL, 0x40439c5107e6a15aULL, 0x4035be977848cdfaULL, 0xc034e66b528cbff0ULL, };
	static const TArray<uint64> Golden_SpaceSwitch = { 0xf82d42719a5450b0ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x402199c5ebd4d2f1ULL, 0x4002b4a5da845797ULL, 0xc01088d4647fb329ULL, 0x40311db80eaa45afULL, 0x401799fe6bef4a14ULL, 0xc020ca3ad13dcc15ULL, 0x4038e6a185bbc941ULL, 0x4024f031812d0192ULL, 0xc02965cf7268b677ULL, 0x40401c0ae05d77e6ULL, 0x402fa1fec0f3e2feULL, 0xc030ec83e893dff2ULL, 0x4043a094a0ceb26eULL, 0x4035b26f7b2498b4ULL, 0xc034e9f9c33d9a07ULL, };
	static const TArray<uint64> Golden_DummyNearParent = { 0xc65e7eabc1ad64fcULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x402383439c6827deULL, 0x0000000000000000ULL, 0x40018d14f27f6edaULL, 0x4033c17cfa4f7285ULL, 0x0000000000000000ULL, 0x40026631ed128734ULL, 0x403dc11ee02d7b69ULL, 0x0000000000000000ULL, 0x4003c13e1a9be041ULL, 0x4043e06d9e6e89c4ULL, 0x0000000000000000ULL, 0x40029b02df22e7bdULL, 0x4048de108e1870b0ULL, 0x0000000000000000ULL, 0x3ffb7e064dc18c1aULL, };
	static const TArray<uint64> Golden_DummyOblique = { 0xa0839b7f71810f03ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x402395998730623bULL, 0x3ffef8037164a28eULL, 0x3fe360dd762d05d8ULL, 0x4033caccc39827e8ULL, 0x3ffef7f8b054fc05ULL, 0x3fe360cce93d6160ULL, 0x403dcaccc3974221ULL, 0x3ffef7bccea1471cULL, 0x3fe3608d5bef4cd3ULL, 0x4043e566612bb9e3ULL, 0x3ffefbc7b0d8f477ULL, 0x3fe3666e94a60072ULL, 0x4048e5665dbaf891ULL, 0x3fff05cd392e5b98ULL, 0x3fe372a0be5a0011ULL, };
	static const TArray<uint64> Golden_DummyResolved = { 0xaf34ed3e829cf8c4ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x40231867d278f2cbULL, 0x0000000000000000ULL, 0x4007c9d948ed9c0dULL, 0x40338c33e93c70cdULL, 0x0000000000000000ULL, 0x4007c9d2ba967a25ULL, 0x403d8c33e93b5d4bULL, 0x0000000000000000ULL, 0x4007c9ad9d14ac83ULL, 0x4043c619f3fbaa29ULL, 0x0000000000000000ULL, 0x4007cc31a2d12521ULL, 0x4048c619f047b413ULL, 0x0000000000000000ULL, 0x4007d24772e6a901ULL, };

	enum class EScenarioFeature : uint8
	{
		None,
		BoneConstraint,
		SubdivisionDynamic,
		SubdivisionCollisionOnly,
		WorldLimits,
		LimitAngle,
		DummyBone,
		DistanceTeleport,
		RotationTeleport,
		YawWrap,
		SlowRotation,
		SpaceSwitch,
		DummyNearParent,
		DummyOblique,
		DummyResolved,
	};

	struct FScenario
	{
		const TCHAR* Name;
		int32 Fps;
		EKawaiiPhysicsSimulationSpace Space;
		bool bMoveComponent;
		bool bMovePose;
		bool bFixedSubstep;
		bool bUseForce;
		EExternalForceSpace ForceSpace;
		bool bUseLimits;
		const TArray<uint64>* Expected;
		EScenarioFeature Feature;
	};

	struct FScopedSubstepping
	{
		UKawaiiPhysicsDeveloperSettings* Settings = GetMutableDefault<UKawaiiPhysicsDeveloperSettings>();
		const bool bPreviousFixed = Settings->bUseFixedSubstepping;
		const int32 PreviousMaxSubsteps = Settings->MaxSubsteps;

		explicit FScopedSubstepping(const bool bFixed)
		{
			Settings->bUseFixedSubstepping = bFixed;
			Settings->MaxSubsteps = 4;
		}

		~FScopedSubstepping()
		{
			Settings->bUseFixedSubstepping = bPreviousFixed;
			Settings->MaxSubsteps = PreviousMaxSubsteps;
		}
	};

	struct FProxy : FAnimInstanceProxy
	{
		using FAnimInstanceProxy::FAnimInstanceProxy;
		using FAnimInstanceProxy::PreUpdate;
	};

	struct FFixture
	{
		TStrongObjectPtr<USkeleton> Skeleton{NewObject<USkeleton>()};
		UWorld* World = nullptr;
		USkeletalMeshComponent* Component = nullptr;
		UAnimInstance* Anim = nullptr;
		TUniquePtr<FProxy> Proxy;
		FKawaiiPhysicsTestAccessor Accessor;
		FAnimNode_KawaiiPhysics& Node = Accessor.Node;
		TArray<FBoneTransform> OutTransforms;
		float FrameDt = 1.0f / 60.0f;

		explicit FFixture(const FScenario& Scenario)
		{
			FrameDt = 1.0f / static_cast<float>(Scenario.Fps);
			TArray<FBoneIndexType> RequiredIndices;
			{
				FReferenceSkeletonModifier Modifier(Skeleton.Get());
				Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
				RequiredIndices.Add(0);
				for (int32 BoneIndex = 1; BoneIndex < GBoneCount; ++BoneIndex)
				{
					const FName Name(*FString::Printf(TEXT("chain_%d"), BoneIndex));
					Modifier.Add(FMeshBoneInfo(Name, Name.ToString(), BoneIndex - 1),
						FTransform(FVector(10.0, 0.0, 0.0)));
					RequiredIndices.Add(static_cast<FBoneIndexType>(BoneIndex));
				}
			}

			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &Values);
			AActor* Actor = World->SpawnActor<AActor>();
			Component = NewObject<USkeletalMeshComponent>(Actor);
			Actor->AddInstanceComponent(Component);
			Actor->SetRootComponent(Component);
			Anim = NewObject<UAnimInstance>(Component);
			Proxy = MakeUnique<FProxy>(Anim);
			Proxy->PreUpdate(Anim, FrameDt);
			Proxy->GetRequiredBones().InitializeTo(RequiredIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);

			Node.RootBone.BoneName = TEXT("root");
			Node.SimulationSpace = Scenario.Space;
			Node.SimulationBaseBone.BoneName = Scenario.Space == EKawaiiPhysicsSimulationSpace::BaseBoneSpace ||
				Scenario.Feature == EScenarioFeature::SpaceSwitch
				? TEXT("chain_1") : TEXT("root");
			Node.TargetFramerate = 60;
			Node.BoneSubdivisionCount = 0;
			Node.DummyBoneLength = 0.0f;
			Node.WarmUpFrames = 0;
			Node.bNeedWarmUp = false;
			Node.bAllowWorldCollision = false;
			Node.bUseSimpleWorldCollision = false;
			Node.Gravity = FVector(0.0, 0.0, -980.0);
			Node.PhysicsSettings.Damping = 0.15f;
			Node.PhysicsSettings.WorldDampingLocation = 0.2f;
			Node.PhysicsSettings.WorldDampingRotation = 0.3f;
			Node.PhysicsSettings.Stiffness = 0.07f;
			Node.PhysicsSettings.Radius = 2.0f;
			Node.PhysicsSettings.LimitAngle = 0.0f;
			Node.TeleportRotationThreshold = 180.0f;

			if (Scenario.bUseForce)
			{
				FInstancedStruct& Item = Node.ExternalForces.Add_GetRef(
					FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Basic>());
				FKawaiiPhysics_ExternalForce_Basic& Force =
					Item.GetMutable<FKawaiiPhysics_ExternalForce_Basic>();
				Force.ExternalForceSpace = Scenario.ForceSpace;
				Force.ForceDir = FVector(60.0, 25.0, 0.0);
				Force.Interval = 0.0f;
				Force.RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);
			}

			if (Scenario.bUseLimits)
			{
				FSphericalLimit& Sphere = Node.SphericalLimits.AddDefaulted_GetRef();
				Sphere.DrivingBone.BoneName = TEXT("root");
				Sphere.OffsetLocation = FVector(20.0, 0.0, -3.0);
				Sphere.Radius = 8.0f;
				Sphere.LimitType = ESphericalLimitType::Outer;
				FCapsuleLimit& Capsule = Node.CapsuleLimits.AddDefaulted_GetRef();
				Capsule.DrivingBone.BoneName = TEXT("root");
				Capsule.OffsetLocation = FVector(40.0, 0.0, -3.0);
				Capsule.Radius = 7.0f;
				Capsule.Length = 18.0f;
#if WITH_EDITORONLY_DATA
				Sphere.Guid = FGuid(1, 0, 0, 0);
				Capsule.Guid = FGuid(2, 0, 0, 0);
#endif
			}

			switch (Scenario.Feature)
			{
			case EScenarioFeature::BoneConstraint:
			{
				// 拘束前後の反復と bridge dummy の配置・押し戻しを通す。
				Node.BoneConstraintIterationCountBeforeCollision = 2;
				Node.BoneConstraintIterationCountAfterCollision = 2;
				Node.BoneConstraintSubdivisionCount = 1;
				FModifyBoneConstraint& Constraint = Node.BoneConstraints.AddDefaulted_GetRef();
				Constraint.Bone1.BoneName = TEXT("chain_1");
				Constraint.Bone2.BoneName = TEXT("chain_4");
				FSphericalLimit& BridgeSphere = Node.SphericalLimits.AddDefaulted_GetRef();
				BridgeSphere.DrivingBone.BoneName = TEXT("root");
				BridgeSphere.OffsetLocation = FVector(25.0, 0.0, -2.0);
				BridgeSphere.Radius = 3.0f;
				BridgeSphere.LimitType = ESphericalLimitType::Outer;
#if WITH_EDITORONLY_DATA
				BridgeSphere.Guid = FGuid(3, 0, 0, 0);
#endif
				break;
			}
			case EScenarioFeature::SubdivisionDynamic:
			case EScenarioFeature::SubdivisionCollisionOnly:
			{
				// 前者は中間ダミーの積分、後者は補間配置と衝突変位の実ボーンへの転送を通す。
				Node.BoneSubdivisionCount = 1;
				Node.bBoneSubdivisionCollisionOnly =
					Scenario.Feature == EScenarioFeature::SubdivisionCollisionOnly;
				FSphericalLimit& MidpointSphere = Node.SphericalLimits.AddDefaulted_GetRef();
				MidpointSphere.DrivingBone.BoneName = TEXT("root");
				MidpointSphere.OffsetLocation = FVector(5.0, 0.0, -2.0);
				MidpointSphere.Radius = 3.0f;
				MidpointSphere.LimitType = ESphericalLimitType::Outer;
#if WITH_EDITORONLY_DATA
				MidpointSphere.Guid = FGuid(4, 0, 0, 0);
#endif
				break;
			}
			case EScenarioFeature::WorldLimits:
			{
				// WorldSpace の Box・Tapered Capsule・Planar と長さ復元後の Planar 再押し出しを通す。
				FBoxLimit& Box = Node.BoxLimits.AddDefaulted_GetRef();
				Box.DrivingBone.BoneName = TEXT("root");
				Box.OffsetLocation = FVector(20.0, 0.0, -3.0);
				Box.Extent = FVector(6.0, 6.0, 6.0);
				FTaperedCapsuleLimit& Tapered = Node.TaperedCapsuleLimits.AddDefaulted_GetRef();
				Tapered.DrivingBone.BoneName = TEXT("root");
				Tapered.OffsetLocation = FVector(35.0, 0.0, -3.0);
				Tapered.Radius0 = 7.0f;
				Tapered.Radius1 = 3.0f;
				Tapered.Length = 14.0f;
				FPlanarLimit& Planar = Node.PlanarLimits.AddDefaulted_GetRef();
				Planar.DrivingBone.BoneName = TEXT("root");
				Planar.OffsetLocation = FVector(0.0, 0.0, -4.0);
#if WITH_EDITORONLY_DATA
				Box.Guid = FGuid(5, 0, 0, 0);
				Tapered.Guid = FGuid(6, 0, 0, 0);
				Planar.Guid = FGuid(7, 0, 0, 0);
#endif
				break;
			}
			case EScenarioFeature::LimitAngle:
				// 角度制限とボーン長復元を通す。
				Node.PhysicsSettings.LimitAngle = 25.0f;
				break;
			case EScenarioFeature::DummyBone:
				// 末端ダミーの積分と末端ボーンの回転出力を通す。
				Node.DummyBoneLength = 7.0f;
				break;
			case EScenarioFeature::DistanceTeleport:
				Node.TeleportDistanceThreshold = 15.0f;
				break;
			case EScenarioFeature::RotationTeleport:
			case EScenarioFeature::YawWrap:
				Node.TeleportRotationThreshold = 20.0f;
				break;
			case EScenarioFeature::SlowRotation:
				Node.TeleportRotationThreshold = 12.0f;
				Node.TimeScale = 0.1f;
				break;
			case EScenarioFeature::DummyNearParent:
				Node.BoneSubdivisionCount = 10;
				Node.bBoneSubdivisionCollisionOnly = true;
				Node.PhysicsSettings.Radius = 0.1f;
				Node.Gravity = FVector::ZeroVector;
				{
					FSphericalLimit& Sphere = Node.SphericalLimits.AddDefaulted_GetRef();
					Sphere.DrivingBone.BoneName = TEXT("root");
					Sphere.OffsetLocation = FVector(10.0 / 11.0, 0.0, -0.4);
					Sphere.Radius = 0.5f;
					Sphere.LimitType = ESphericalLimitType::Outer;
#if WITH_EDITORONLY_DATA
					Sphere.Guid = FGuid(8, 0, 0, 0);
#endif
				}
				break;
			case EScenarioFeature::DummyOblique:
			case EScenarioFeature::DummyResolved:
				Node.BoneSubdivisionCount = 1;
				Node.bBoneSubdivisionCollisionOnly = true;
				Node.PhysicsSettings.Radius = 0.1f;
				Node.Gravity = FVector::ZeroVector;
				{
					FSphericalLimit& Midpoint = Node.SphericalLimits.AddDefaulted_GetRef();
					Midpoint.DrivingBone.BoneName = TEXT("root");
					Midpoint.OffsetLocation = FVector(5.0, 0.0, -2.0);
					Midpoint.Radius = 2.4f;
					Midpoint.LimitType = ESphericalLimitType::Outer;
#if WITH_EDITORONLY_DATA
					Midpoint.Guid = FGuid(9, 0, 0, 0);
#endif
					FSphericalLimit& Endpoint = Node.SphericalLimits.AddDefaulted_GetRef();
					Endpoint.DrivingBone.BoneName = TEXT("root");
					Endpoint.OffsetLocation = Scenario.Feature == EScenarioFeature::DummyOblique
						? FVector(10.0, -1.0, 0.0) : FVector(10.0, 0.0, -1.0);
					Endpoint.Radius = Scenario.Feature == EScenarioFeature::DummyOblique ? 2.9f : 3.9f;
					Endpoint.LimitType = ESphericalLimitType::Outer;
#if WITH_EDITORONLY_DATA
					Endpoint.Guid = FGuid(10, 0, 0, 0);
#endif
				}
				break;
			case EScenarioFeature::SpaceSwitch:
			case EScenarioFeature::None:
				break;
			}

			Node.OnInitializeAnimInstance(Proxy.Get(), Anim);
			Node.Initialize_AnyThread(FAnimationInitializeContext(Proxy.Get()));
			Node.CacheBones_AnyThread(FAnimationCacheBonesContext(Proxy.Get()));
			Node.PreUpdate(Anim);
			OutTransforms.Reserve(GBoneCount);
		}

		~FFixture()
		{
			Proxy.Reset();
			World->DestroyWorld(false);
		}

		void EvaluateFrame(const FScenario& Scenario, const int32 Frame)
		{
			if (Scenario.Feature == EScenarioFeature::SpaceSwitch)
			{
				Node.SimulationSpace = Frame < 40 ? EKawaiiPhysicsSimulationSpace::ComponentSpace
					: Frame < 80 ? EKawaiiPhysicsSimulationSpace::WorldSpace
					: EKawaiiPhysicsSimulationSpace::BaseBoneSpace;
			}
			if (Scenario.bMoveComponent)
			{
				const double Phase = static_cast<double>(Frame) * 0.07;
				FVector Location(Frame * 0.35, FMath::Sin(Phase) * 2.0, 0.0);
				FRotator Rotation(
					Frame * 0.11 + FMath::Sin(Phase * 0.7) * 2.0,
					Frame * 0.15,
					Frame * -0.09 + FMath::Cos(Phase * 0.5) * 1.5);
				if (Scenario.Feature == EScenarioFeature::DistanceTeleport && Frame >= 40)
				{
					Location.X += 100.0;
				}
				else if (Scenario.Feature == EScenarioFeature::RotationTeleport)
				{
					Rotation = FRotator(0.0, Frame * 2.0 + (Frame >= 40 ? 90.0 : 0.0), 0.0);
				}
				else if (Scenario.Feature == EScenarioFeature::YawWrap)
				{
					Rotation = FRotator(0.0, FMath::UnwindDegrees(179.0 + Frame * 2.0), 0.0);
				}
				else if (Scenario.Feature == EScenarioFeature::SlowRotation)
				{
					Rotation = FRotator(0.0, Frame * 3.0, 0.0);
				}
				Component->SetWorldLocation(Location);
				Component->SetWorldRotation(Rotation);
			}
			Proxy->PreUpdate(Anim, FrameDt);
			Node.PreUpdate(Anim);

			// ポーズの一時領域は各フレームで解放する。
			FMemMark MemMark(FMemStack::Get());
			FComponentSpacePoseContext Context(Proxy.Get());
			// FCSPose::GetPose() は const なので、入力ポーズはローカル空間で組んでから InitPose に渡す。
			FCompactPose LocalPose;
			LocalPose.SetBoneContainer(&Proxy->GetRequiredBones());
			LocalPose.ResetToRefPose();
			if (Scenario.bMovePose)
			{
				const double Angle = 0.25 * FMath::Sin(static_cast<double>(Frame) * 0.13);
				LocalPose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::UpVector, Angle));
			}
			Context.Pose.InitPose(LocalPose);
			Node.UpdateInternal(FAnimationUpdateContext(Proxy.Get(), FrameDt));
			OutTransforms.Reset();
			Node.EvaluateSkeletalControl_AnyThread(Context, OutTransforms);
		}
	};

	uint64 GetDoubleBits(const double Value)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	bool CollectFrame(FAutomationTestBase& Test, const FScenario& Scenario, const int32 Frame,
		const TArray<FBoneTransform>& OutTransforms, TArray<uint64>& Bits)
	{
		if (OutTransforms.Num() != GBoneCount)
		{
			Test.AddError(FString::Printf(TEXT("%s frame %d: expected %d output bones, got %d"),
				Scenario.Name, Frame, GBoneCount, OutTransforms.Num()));
			return false;
		}
		bool Seen[GBoneCount] = {};
		FVector Locations[GBoneCount];
		FQuat Rotations[GBoneCount];
		for (const FBoneTransform& Bone : OutTransforms)
		{
			const int32 Index = Bone.BoneIndex.GetInt();
			if (Index < 0 || Index >= GBoneCount || Seen[Index] || Bone.Transform.ContainsNaN())
			{
				Test.AddError(FString::Printf(TEXT("%s frame %d: invalid output bone %d"),
					Scenario.Name, Frame, Index));
				return false;
			}
			Seen[Index] = true;
			Locations[Index] = Bone.Transform.GetLocation();
			Rotations[Index] = Bone.Transform.GetRotation();
		}
		for (int32 Index = 0; Index < GBoneCount; ++Index)
		{
			if (!Seen[Index])
			{
				Test.AddError(FString::Printf(TEXT("%s frame %d: missing output bone %d"),
						Scenario.Name, Frame, Index));
				return false;
			}
			Bits.Add(GetDoubleBits(Locations[Index].X));
			Bits.Add(GetDoubleBits(Locations[Index].Y));
			Bits.Add(GetDoubleBits(Locations[Index].Z));
			Bits.Add(GetDoubleBits(Rotations[Index].X));
			Bits.Add(GetDoubleBits(Rotations[Index].Y));
			Bits.Add(GetDoubleBits(Rotations[Index].Z));
			Bits.Add(GetDoubleBits(Rotations[Index].W));
		}
		return true;
	}

	bool RunScenario(FAutomationTestBase& Test, const FScenario& Scenario, TArray<uint64>& Bits)
	{
		FScopedSubstepping Substepping(Scenario.bFixedSubstep);
		FFixture Fixture(Scenario);
		Bits.Reserve(GFrameCount * GBoneCount * GHashComponentsPerBone);
		for (int32 Frame = 0; Frame < GFrameCount; ++Frame)
		{
			Fixture.EvaluateFrame(Scenario, Frame);
			if (!CollectFrame(Test, Scenario, Frame, Fixture.OutTransforms, Bits))
			{
				return false;
			}
		}
		return true;
	}

	// 全フレームの位置・回転をハッシュし、最終フレームの位置だけを別に保持する。
	constexpr int32 GValuesPerFrame = GBoneCount * GHashComponentsPerBone;
	constexpr int32 GExpectedCount = 1 + GBoneCount * GComponentsPerBone;

	uint64 ComputeTrajectoryHash(const TArray<uint64>& Bits)
	{
		uint64 Hash = 0xcbf29ce484222325ULL;
		for (const uint64 Value : Bits)
		{
			for (int32 Byte = 0; Byte < 8; ++Byte)
			{
				Hash ^= (Value >> (Byte * 8)) & 0xffULL;
				Hash *= 0x100000001b3ULL;
			}
		}
		return Hash;
	}

	TArray<uint64> BuildExpectedSummary(const TArray<uint64>& Bits)
	{
		TArray<uint64> Summary;
		Summary.Reserve(GExpectedCount);
		Summary.Add(ComputeTrajectoryHash(Bits));
		const int32 FrameStart = Bits.Num() - GValuesPerFrame;
		for (int32 BoneIndex = 0; BoneIndex < GBoneCount; ++BoneIndex)
		{
			for (int32 Component = 0; Component < GComponentsPerBone; ++Component)
			{
				Summary.Add(Bits[FrameStart + BoneIndex * GHashComponentsPerBone + Component]);
			}
		}
		return Summary;
	}

	void LogCapture(FAutomationTestBase& Test, const FScenario& Scenario, const TArray<uint64>& Bits)
	{
		const TArray<uint64> Summary = BuildExpectedSummary(Bits);
		FString Source = TEXT("{ ");
		for (const uint64 Value : Summary)
		{
			Source += FString::Printf(TEXT("0x%016llxULL, "), static_cast<unsigned long long>(Value));
		}
		Source += TEXT("}");
		Test.AddInfo(FString::Printf(TEXT("GOLDEN_NODE %s %s"), Scenario.Name, *Source));
	}

	bool CheckScenario(FAutomationTestBase& Test, const FScenario& Scenario, const bool bCapture,
		uint64& TrajectoryHash, bool& bHasTrajectory)
	{
		bHasTrajectory = false;
		TArray<uint64> First;
		TArray<uint64> Second;
		if (!RunScenario(Test, Scenario, First) || !RunScenario(Test, Scenario, Second))
		{
			return false;
		}
		if (First.Num() != Second.Num())
		{
			Test.AddError(FString::Printf(TEXT("%s: repeated run count differs"), Scenario.Name));
			return false;
		}
		for (int32 Index = 0; Index < First.Num(); ++Index)
		{
			if (First[Index] != Second[Index])
			{
				Test.AddError(FString::Printf(TEXT("%s: repeated run differs at value %d"), Scenario.Name, Index));
				return false;
			}
		}
		TrajectoryHash = ComputeTrajectoryHash(First);
		bHasTrajectory = true;
		if (First.Num() != GFrameCount * GValuesPerFrame)
		{
			Test.AddError(FString::Printf(TEXT("%s: expected %d trajectory values, got %d"),
				Scenario.Name, GFrameCount * GValuesPerFrame, First.Num()));
			return false;
		}
		if (bCapture)
		{
			LogCapture(Test, Scenario, First);
			return true;
		}
		if (Scenario.Expected->Num() != GExpectedCount)
		{
			Test.AddError(FString::Printf(
				TEXT("%s: expected %d values, got %d; rerun with -KPGoldenCapture"),
				Scenario.Name, GExpectedCount, Scenario.Expected->Num()));
			return false;
		}
		const TArray<uint64> Actual = BuildExpectedSummary(First);
		if ((*Scenario.Expected)[0] == Actual[0])
		{
			return true;
		}
		// ハッシュが違う。最終フレームの差分があれば位置を示す（無ければ途中のフレームだけが違う）。
		for (int32 Index = 1; Index < GExpectedCount; ++Index)
		{
			if ((*Scenario.Expected)[Index] != Actual[Index])
			{
				Test.AddError(FString::Printf(
					TEXT("%s final frame bone %d component %d: expected 0x%016llx actual 0x%016llx"),
					Scenario.Name, (Index - 1) / GComponentsPerBone, (Index - 1) % GComponentsPerBone,
					static_cast<unsigned long long>((*Scenario.Expected)[Index]),
					static_cast<unsigned long long>(Actual[Index])));
				return false;
			}
		}
		Test.AddError(FString::Printf(TEXT("%s: trajectory hash differs (final frame matches)"), Scenario.Name));
		return false;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsNodeGoldenPositionsTest,
	"KawaiiPhysics.Simulation.NodeGoldenPositions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsNodeGoldenPositionsTest::RunTest(const FString& Parameters)
{
	const bool bCapture = FParse::Param(FCommandLine::Get(), TEXT("KPGoldenCapture"));
	const FString CurrentEnvironment = BuildNodeGoldenEnvironment();
	if (bCapture)
	{
		AddInfo(FString::Printf(TEXT("GOLDEN_ENV NodeGoldenPositions \"%s\""), *CurrentEnvironment));
	}
	else if (Golden_Environment[0] == 0)
	{
		AddError(TEXT("採取環境が空のため比較できない。-KPGoldenCapture で取り直す"));
		return false;
	}
	else if (CurrentEnvironment != Golden_Environment)
	{
		AddWarning(FString::Printf(TEXT("採取環境 \"%s\" と実行環境 \"%s\" が違うため比較しない"),
			Golden_Environment, *CurrentEnvironment));
		return true;
	}
	const FScenario Scenarios[] = {
		// 固定刻みの通常積分を通す。
		{TEXT("Fixed60"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_Fixed60, EScenarioFeature::None},
		// 固定刻みで一フレームに二回の SimulateOnce を通す。
		{TEXT("Fixed30"), 30, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_Fixed30, EScenarioFeature::None},
		// 固定刻みで SimulateOnce を呼ばないフレームと呼ぶフレームを通す。
		{TEXT("Fixed144"), 144, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_Fixed144, EScenarioFeature::None},
		// Component の移動・三軸回転の分配と通常積分を通す。
		{TEXT("ComponentMove"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, true, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_ComponentMove, EScenarioFeature::None},
		// 入力ポーズの回転補間と通常積分を通す。
		{TEXT("PoseMove"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, true, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_PoseMove, EScenarioFeature::None},
		// 動く chain_1 を基準とした BaseBoneSpace の変換と通常積分を通す。
		{TEXT("BaseBoneMove"), 60, EKawaiiPhysicsSimulationSpace::BaseBoneSpace, true, true, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_BaseBoneMove, EScenarioFeature::None},
		// WorldSpace の重力変換と通常積分を通す。
		{TEXT("WorldMove"), 60, EKawaiiPhysicsSimulationSpace::WorldSpace, true, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_WorldMove, EScenarioFeature::None},
		// 固定刻みなしの一回だけの SimulateOnce を通す。
		{TEXT("LegacyFixedPose"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, false, false,
			EExternalForceSpace::WorldSpace, false, &Golden_LegacyFixedPose, EScenarioFeature::None},
		// 固定刻みなしで入力ポーズ追従と通常積分を通す。
		{TEXT("LegacyPoseMove"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, true, false, false,
			EExternalForceSpace::WorldSpace, false, &Golden_LegacyPoseMove, EScenarioFeature::None},
		// 動く Component とポーズの中で BoneSpace 外力の適用と通常積分を通す。
		{TEXT("BasicBoneForce"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, true, true, true, true,
			EExternalForceSpace::BoneSpace, false, &Golden_BasicBoneForce, EScenarioFeature::None},
		// 動く Component とポーズの中で WorldSpace 外力の適用と通常積分を通す。
		{TEXT("BasicWorldForce"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, true, true, true, true,
			EExternalForceSpace::WorldSpace, false, &Golden_BasicWorldForce, EScenarioFeature::None},
		// 球と Capsule の衝突補正と通常積分を通す。
		{TEXT("SphereCapsule"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, true, &Golden_SphereCapsule, EScenarioFeature::None},
		// BoneConstraint の衝突前後反復と bridge dummy の衝突転送を通す。
		{TEXT("BoneConstraint"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_BoneConstraint, EScenarioFeature::BoneConstraint},
		// 中間ダミーの積分と球 Limit との衝突を通す。
		{TEXT("SubdivisionDynamic"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_SubdivisionDynamic, EScenarioFeature::SubdivisionDynamic},
		// 中間ダミーの積分スキップ、補間配置、球 Limit の押し戻しを通す。
		{TEXT("SubdivisionCollisionOnly"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_SubdivisionCollisionOnly, EScenarioFeature::SubdivisionCollisionOnly},
		// WorldSpace の三形状の衝突と Planar 再押し出しを通す。
		{TEXT("WorldLimits"), 60, EKawaiiPhysicsSimulationSpace::WorldSpace, true, true, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_WorldLimits, EScenarioFeature::WorldLimits},
		// 角度制限とボーン長復元を通す。
		{TEXT("LimitAngle"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_LimitAngle, EScenarioFeature::LimitAngle},
		// 四ステップ超の時間破棄と四回の SimulateOnce を通す。
		{TEXT("Fixed10"), 10, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_Fixed10, EScenarioFeature::None},
		// 末端ダミーの積分と末端ボーンの回転出力を通す。
		{TEXT("DummyBone"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_DummyBone, EScenarioFeature::DummyBone},
		// 10fps は 0.1 秒で、60Hz の 4 ステップ上限 0.0667 秒を超える。Component 移動の破棄時間分を次へ繰り越す改変を検出する。
		{TEXT("DroppedComponentMove"), 10, EKawaiiPhysicsSimulationSpace::ComponentSpace, true, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_DroppedComponentMove, EScenarioFeature::None},
		// 144fps でフレーム 40 の X 移動を 100.35cm に跳ねさせる。閾値 15cm、未消費時間のあるフレームで Component 移動の全量破棄漏れを検出する。
		{TEXT("DistanceTeleportComponent"), 144, EKawaiiPhysicsSimulationSpace::ComponentSpace, true, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_DistanceTeleportComponent, EScenarioFeature::DistanceTeleport},
		// 同じ 144fps・100.35cm の一回の移動を BaseBoneSpace で与え、基準ボーン変換を消費割合だけ進める改変を検出する。
		{TEXT("DistanceTeleportBaseBone"), 144, EKawaiiPhysicsSimulationSpace::BaseBoneSpace, true, true, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_DistanceTeleportBaseBone, EScenarioFeature::DistanceTeleport},
		// ヨー 2度/フレームにフレーム 40 で 90度を加え、閾値 20度を超える一回の回転テレポートを通す。
		{TEXT("RotationTeleport"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, true, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_RotationTeleport, EScenarioFeature::RotationTeleport},
		// ヨー +179度から -179度へ 2度ずつまたぐ。閾値 20度に対し長い側の角度でテレポート判定する改変を検出する。
		{TEXT("YawWrap"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, true, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_YawWrap, EScenarioFeature::YawWrap},
		// TimeScale 0.1、ヨー 3度/フレーム、閾値 12度。約 10 フレームで 30度を繰り越すため、累積回転で判定する改変を検出する。
		{TEXT("SlowRotation"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, true, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_SlowRotation, EScenarioFeature::SlowRotation},
		// 30fps は 60Hz の 2 ステップ/フレーム。動く chain_1 とポーズで基準ボーン追従を分割しない改変を検出する。
		{TEXT("BaseBoneSubsteps"), 30, EKawaiiPhysicsSimulationSpace::BaseBoneSpace, true, true, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_BaseBoneSubsteps, EScenarioFeature::None},
		// フレーム 40 と 80 で ComponentSpace → WorldSpace → BaseBoneSpace。Component とポーズが動く中で前ポーズ目標と出力オフセットの変換漏れを検出する。
		{TEXT("SpaceSwitch"), 30, EKawaiiPhysicsSimulationSpace::ComponentSpace, true, true, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_SpaceSwitch, EScenarioFeature::SpaceSwitch},
		// SimpleWorld の地面 Box / Convex はここでは扱わない。bUseSimpleWorldCollision を有効にすると、評価のたびに
		// UpdateSimpleWorldCollisionLimits が Subsystem の公開値（テストの World では空）を読み、注入した Limit を上書きするため。
		// 押し出しの順序はハーネス経由の SimpleWorld テストが本番の SimulateOnce を通して守る。
		// 分割数 10 で最初のダミーは α=1/11<0.1。球中心 (0.909,0,-0.4)、半径 0.5+骨 0.1cm で押し、除数の下限削除を検出する。
		{TEXT("DummyNearParent"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_DummyNearParent, EScenarioFeature::DummyNearParent},
		// α=0.5 のダミーを Z に 0.5cm、子端点を Y に 2cm 押す。残差には Y=-1cm が残り、射影を残差長に替える改変を検出する。
		{TEXT("DummyOblique"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_DummyOblique, EScenarioFeature::DummyOblique},
		// α=0.5 のダミーを Z に 0.5cm、子端点を Z に 3cm 押す。残差の射影は -1cm となり、解消済みでも返す改変を検出する。
		{TEXT("DummyResolved"), 60, EKawaiiPhysicsSimulationSpace::ComponentSpace, false, false, true, false,
			EExternalForceSpace::WorldSpace, false, &Golden_DummyResolved, EScenarioFeature::DummyResolved},
	};
	if (bCapture)
	{
		AddWarning(TEXT("採取モードのため比較していない"));
	}
	bool bOk = true;
	TArray<uint64> TrajectoryHashes;
	TrajectoryHashes.SetNumZeroed(UE_ARRAY_COUNT(Scenarios));
	TArray<bool> bHasTrajectory;
	bHasTrajectory.Init(false, UE_ARRAY_COUNT(Scenarios));
	for (int32 ScenarioIndex = 0; ScenarioIndex < UE_ARRAY_COUNT(Scenarios); ++ScenarioIndex)
	{
		bOk &= CheckScenario(*this, Scenarios[ScenarioIndex], bCapture,
			TrajectoryHashes[ScenarioIndex], bHasTrajectory[ScenarioIndex]);
	}
	// 採取モードでも全シナリオ間の軌跡ハッシュを照合する。
	for (int32 FirstIndex = 0; FirstIndex < UE_ARRAY_COUNT(Scenarios); ++FirstIndex)
	{
		if (!bHasTrajectory[FirstIndex])
		{
			continue;
		}
		for (int32 SecondIndex = FirstIndex + 1; SecondIndex < UE_ARRAY_COUNT(Scenarios); ++SecondIndex)
		{
			if (bHasTrajectory[SecondIndex] && TrajectoryHashes[FirstIndex] == TrajectoryHashes[SecondIndex])
			{
				AddError(FString::Printf(TEXT("シナリオ %s と %s が同じ軌跡で、区別できていない"),
					Scenarios[FirstIndex].Name, Scenarios[SecondIndex].Name));
				bOk = false;
			}
		}
	}
	return bOk;
}

} // namespace KawaiiPhysicsNodeGoldenTestPrivate

#endif
