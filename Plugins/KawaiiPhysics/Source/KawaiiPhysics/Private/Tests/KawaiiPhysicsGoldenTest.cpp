// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"

namespace
{
	static_assert(sizeof(double) == sizeof(uint64));

	// 期待値（採取済みリテラル）。空のうちは「採取モード」として貼り付け用のログだけを出す。
	// 要素数0の生配列はC++として不正なため TArray で保持する。
	//
	// 【運用注意】比較は uint64 のビット厳密一致。シナリオは Pow / Slerp 等の超越関数を含み、
	// その実装は CRT / CPU の ISA（FMA3/AVX2 有無等）でディスパッチが変わるため、
	// 別マシンや CRT 更新後は最下位ビットが変わり false positive になりうる（コード退行ではない）。
	// 環境起因の不一致が出たら: 該当 Golden_* 配列を空にする → テスト実行で採取モードになり
	// "GOLDEN <シナリオ名> 0x..." がログに出る → その値でリテラルを貼り直して再キャプチャする。
	// ChainLegacy: 12 bones
	static const TArray<uint64> Golden_ChainLegacy = {
		0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
		0x400cb36886109a0bULL, 0x3fd84b5227aa6490ULL, 0xc022a734f8496675ULL,
		0x401c64444ee9b3e4ULL, 0x3ff206c7630a3310ULL, 0xc032a90d9826a689ULL,
		0x4024fc0cb93b114eULL, 0x4001de346c91e3c5ULL, 0xc03c005574c5ce68ULL,
		0x402b77cb6a41f1f5ULL, 0x400d7a05654f962eULL, 0xc042acb73b60a59aULL,
		0x4030c9ccbe978f78ULL, 0x4015d78e9841be9cULL, 0xc0475a2a1cb82e99ULL,
		0x40339f946dc5e8b9ULL, 0x401e278f44e15106ULL, 0xc04c087a4e75b4deULL,
		0x4036364550098a76ULL, 0x4023c85dcc758517ULL, 0xc0505bce39f8f42cULL,
		0x403886c62e05fb9aULL, 0x4028f5512c45a1e0ULL, 0xc052b3ff19ce6d8bULL,
		0x403a81ee9349fb99ULL, 0x402e8624839a69e8ULL, 0xc0550d7f6824d7a6ULL,
		0x403c0f00aca3e782ULL, 0x40324b1bc7f24e16ULL, 0xc05767401986a3e6ULL,
		0x403d47e342cf0b37ULL, 0x4035cde3eb6a23eaULL, 0xc059b96202d95a7bULL,
	};

	// Chain: 12 bones
	static const TArray<uint64> Golden_Chain = {
		0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
		0x3ff8ffa4a44df976ULL, 0x3fb234162e8d0238ULL, 0xc023c0fd63fc7242ULL,
		0x4008e93bb10ca415ULL, 0x3fcb1304e188803dULL, 0xc033c13dcc056a30ULL,
		0x40129f3cb83d2979ULL, 0x3fdb02009d4ad6c0ULL, 0xc03da21064d86560ULL,
		0x4018bc9eb0e7e69fULL, 0x3fe678073152a192ULL, 0xc043c17aa10327acULL,
		0x401eca1546eea61aULL, 0x3ff0d34dc7b5d9eeULL, 0xc048b1f32620072eULL,
		0x4022637b3b82fb6dULL, 0x3ff77e422a9ed826ULL, 0xc04da266ffa6f07dULL,
		0x4025572f2f7697f0ULL, 0x3fff135ba017d69cULL, 0xc0514976e748807cULL,
		0x40282c71cfd5026eULL, 0x4003b0c25a06cbd2ULL, 0xc053c223af1fd841ULL,
		0x402abdef7182e280ULL, 0x4008af783fe5a74cULL, 0xc0563b94cec6090fULL,
		0x402d41f062791b25ULL, 0x401028b5e8ac2a19ULL, 0xc058b38e2bcd6587ULL,
		0x40307cfc66c717e4ULL, 0x4015cde3469d31a8ULL, 0xc05b21df93351f51ULL,
	};

	// Constraint: 16 bones
	static const TArray<uint64> Golden_Constraint = {
		0x3ff7ffffeb3927eeULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
		0x3ff800001d987181ULL, 0x3ff0d12a2c15900fULL, 0xc023e3a3e0420728ULL,
		0x3ff8000030c14c73ULL, 0x4000caf5099541a6ULL, 0xc033e3b8d71aab28ULL,
		0x3ff800001ad02486ULL, 0x40092d24ce1de03aULL, 0xc03dd5a0608f99f6ULL,
		0x3ff7ffffe2fde461ULL, 0x4010c7a894a70a50ULL, 0xc043e3c3fac2b720ULL,
		0x3ff7ffffca11cc6fULL, 0x4014f8ffda9c5f45ULL, 0xc048dcb6e9b74ca2ULL,
		0x3ff800000d185d1bULL, 0x40192b642843de93ULL, 0xc04dd5a64cd88919ULL,
		0x3ff7ffffd487e69aULL, 0x401d5c8d6607c9f8ULL, 0xc051674ceb858fa5ULL,
		0x402b00000298db03ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
		0x402afffffc4cf1d2ULL, 0x3ff0d12a2c15900fULL, 0xc023e3a3e0420728ULL,
		0x402afffff9e7d66fULL, 0x4000caf5099541a6ULL, 0xc033e3b8d71aab28ULL,
		0x402afffffca5fb65ULL, 0x40092d24ce1de03aULL, 0xc03dd5a0608f99f6ULL,
		0x402b000003a04369ULL, 0x4010c7a894a70a50ULL, 0xc043e3c3fac2b720ULL,
		0x402b000006bdc671ULL, 0x4014f8ffda9c5f45ULL, 0xc048dcb6e9b74ca2ULL,
		0x402afffffe5cf467ULL, 0x40192b642843de93ULL, 0xc04dd5a64cd88919ULL,
		0x402b0000056f0342ULL, 0x401d5c8d6607c9f8ULL, 0xc051674ceb858fa5ULL,
	};

	// Collision: 12 bones
	// Box 内部押し出しの変更（最小貫通面へ penetration + R）で更新（2026-09-11）
	static const TArray<uint64> Golden_Collision = {
		0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
		0x0000000000000000ULL, 0x0000000000000000ULL, 0xc024000000000000ULL,
		0x0000000000000000ULL, 0x0000000000000000ULL, 0xc034000000000000ULL,
		0x0000000000000000ULL, 0xc0131eb4225deaeeULL, 0xc03cc89a7895568aULL,
		0xbff863480c9f1c6bULL, 0xc01f9c1da7f35dc7ULL, 0xc043148c8dd8be77ULL,
		0xc001ef2fec265986ULL, 0xc02304b5789e6cc4ULL, 0xc04800935d0b9b3dULL,
		0xc0019a2e27b8b835ULL, 0xc022d50e8fad31ceULL, 0xc04d0082592cd67aULL,
		0xc00198673ec1e897ULL, 0xc025e99a3b32febeULL, 0xc050f89e5fc53ab0ULL,
		0xc00197cc42c880beULL, 0xc025ff67b9a80bdaULL, 0xc053789ce376bcb4ULL,
		0xc0019962ef72e999ULL, 0xc025fb85ef0e2783ULL, 0xc055f89cd7481e49ULL,
		0x40002f84af5f1449ULL, 0xc02b56100e212587ULL, 0xc05822de70c35520ULL,
		0x3ffcd8cf920b97caULL, 0xc02b0eb42ae5698eULL, 0xc05aa2a6c585900bULL,
	};

	uint64 DoubleToBits(const double Value)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	uint64 VectorComponentToBits(const FVector& V, const int32 ComponentIndex)
	{
		switch (ComponentIndex)
		{
		case 0:
			return DoubleToBits(V.X);
		case 1:
			return DoubleToBits(V.Y);
		default:
			return DoubleToBits(V.Z);
		}
	}

	bool CheckOrCapture(FAutomationTestBase& Test, const TCHAR* ScenarioName, const TArray<FVector>& Actual,
	                    const TArray<uint64>& Expected)
	{
		const int32 ExpectedNum = Expected.Num();
		const int32 ActualNum = Actual.Num() * 3;
		if (ExpectedNum != ActualNum)
		{
			Test.AddWarning(FString::Printf(TEXT("GoldenPositions not captured: %s expected %d actual %d"),
			                                ScenarioName, ExpectedNum, ActualNum));
			for (int32 BoneIndex = 0; BoneIndex < Actual.Num(); ++BoneIndex)
			{
				for (int32 ComponentIndex = 0; ComponentIndex < 3; ++ComponentIndex)
				{
					const uint64 Bits = VectorComponentToBits(Actual[BoneIndex], ComponentIndex);
					Test.AddInfo(FString::Printf(TEXT("GOLDEN %s 0x%016llx"),
					                             ScenarioName, static_cast<unsigned long long>(Bits)));
				}
			}
			return true;
		}

		for (int32 BoneIndex = 0; BoneIndex < Actual.Num(); ++BoneIndex)
		{
			for (int32 ComponentIndex = 0; ComponentIndex < 3; ++ComponentIndex)
			{
				const int32 FlatIndex = BoneIndex * 3 + ComponentIndex;
				const uint64 ActualBits = VectorComponentToBits(Actual[BoneIndex], ComponentIndex);
				if (Expected[FlatIndex] != ActualBits)
				{
					Test.AddError(FString::Printf(
						TEXT("GoldenPositions mismatch: %s bone %d comp %d expected 0x%016llx actual 0x%016llx"),
						ScenarioName, BoneIndex, ComponentIndex,
						static_cast<unsigned long long>(Expected[FlatIndex]),
						static_cast<unsigned long long>(ActualBits)));
					return false;
				}
			}
		}
		return true;
	}

	FKawaiiPhysicsSettings MakeSettings(const float Radius, const float LimitAngle)
	{
		FKawaiiPhysicsSettings Settings;
		Settings.Damping = 0.15f;
		Settings.WorldDampingLocation = 0.2f;
		Settings.WorldDampingRotation = 0.3f;
		Settings.Stiffness = 0.07f;
		Settings.Radius = Radius;
		Settings.LimitAngle = LimitAngle;
		return Settings;
	}

	TArray<FVector> CollectLocations(const FKawaiiPhysicsTestAccessor& A)
	{
		TArray<FVector> Locations;
		Locations.Reserve(A.Num());
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			Locations.Add(A.Bone(Index).Location);
		}
		return Locations;
	}

	void RunGoldenFrames(FKawaiiPhysicsTestAccessor& A)
	{
		for (int32 Frame = 0; Frame < 200; ++Frame)
		{
			A.StepFrame(1.0f / 90.0f);
		}
	}

	TArray<FVector> RunChainScenario()
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildVerticalChain(12, 10.0f);
		A.SetAllPhysicsSettings(MakeSettings(2.0f, 0.0f));
		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector(0.0, 0.0, -980.0));
		A.SetFixedSubstepping(true, 60, 4);
		A.SetSkelCompMove(FVector(0.3f, 0.0f, 0.0f), FQuat(FVector::UpVector, 0.01f));
		RunGoldenFrames(A);
		return CollectLocations(A);
	}

	// legacy（サブステップOFF）。Exponent が 1.0f にならないため Stiffness の Pow 経路が
	// 固定サブステップ時と別のコードパスになる。そちらのビット不変性もここで固定する。
	TArray<FVector> RunChainLegacyScenario()
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildVerticalChain(12, 10.0f);
		A.SetAllPhysicsSettings(MakeSettings(2.0f, 0.0f));
		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector(0.0, 0.0, -980.0));
		A.SetFixedSubstepping(false, 60, 4);
		A.SetSkelCompMove(FVector(0.3f, 0.0f, 0.0f), FQuat(FVector::UpVector, 0.01f));
		RunGoldenFrames(A);
		return CollectLocations(A);
	}

	TArray<FVector> RunConstraintScenario()
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildTwoVerticalChains(8, 10.0f, 15.0f);
		A.SetAllPhysicsSettings(MakeSettings(2.0f, 0.0f));
		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector(0.0, 0.0, -980.0));
		A.SetFixedSubstepping(true, 60, 4);
		A.SetBoneConstraintIterations(2, 2);
		A.SetBoneConstraintGlobalComplianceType(EXPBDComplianceType::Leather);
		// 制約長を実際の横間隔(15)より短くして XPBD に必ず仕事をさせる。
		// 同じ長さだと違反量が0のまま静止し、拘束計算の退行を検出できない。
		for (int32 Depth = 0; Depth < 8; ++Depth)
		{
			A.AddRuntimeBoneConstraint(Depth, 8 + Depth, 12.0f);
		}
		A.SetSkelCompMove(FVector(0.0f, 0.2f, 0.0f), FQuat::Identity);
		RunGoldenFrames(A);
		return CollectLocations(A);
	}

	// コライダー無しの同一チェーン。コリジョンが本当に効いているかの対照群。
	TArray<FVector> RunCollisionScenarioWithoutLimits()
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildVerticalChain(12, 10.0f);
		A.SetAllPhysicsSettings(MakeSettings(3.0f, 30.0f));
		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector(0.0, 0.0, -980.0));
		A.SetFixedSubstepping(true, 60, 4);
		RunGoldenFrames(A);
		return CollectLocations(A);
	}

	TArray<FVector> RunCollisionScenario(FAutomationTestBase& Test)
	{
		FKawaiiPhysicsTestAccessor A;
		A.BuildVerticalChain(12, 10.0f);
		A.SetAllPhysicsSettings(MakeSettings(3.0f, 30.0f));
		A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
		A.SetGravityInSimSpace(FVector(0.0, 0.0, -980.0));
		A.SetFixedSubstepping(true, 60, 4);

		FSphericalLimit Sphere;
		Sphere.bEnable = true;
		Sphere.Location = FVector(2.0, 0.0, -45.0);
		Sphere.Rotation = FQuat::Identity;
		Sphere.Radius = 8.0f;
		Sphere.LimitType = ESphericalLimitType::Outer;
		A.Node.SphericalLimits.Add(Sphere);

		FCapsuleLimit Capsule;
		Capsule.bEnable = true;
		Capsule.Location = FVector(0.0, 2.0, -55.0);
		Capsule.Rotation = FQuat::Identity;
		Capsule.Radius = 4.0f;
		Capsule.Length = 50.0f;
		A.Node.CapsuleLimits.Add(Capsule);

		FBoxLimit Box;
		Box.bEnable = true;
		Box.Location = FVector(0.0, -2.0, -75.0);
		Box.Rotation = FQuat::Identity;
		Box.Extent = FVector(6.0, 6.0, 8.0);
		A.Node.BoxLimits.Add(Box);

		FPlanarLimit Planar;
		Planar.bEnable = true;
		Planar.Location = FVector(0.0, 0.0, -95.0);
		Planar.Rotation = FQuat::Identity;
		Planar.Plane = FPlane(Planar.Location, Planar.Rotation.GetUpVector());
		A.Node.PlanarLimits.Add(Planar);

		RunGoldenFrames(A);
		TArray<FVector> WithLimits = CollectLocations(A);

		// コライダー有無で結果が変わらなければ、このシナリオはコリジョン経路を踏んでいない＝ゴールデン値が無意味。
		const TArray<FVector> WithoutLimits = RunCollisionScenarioWithoutLimits();
		bool bAnyDiffers = false;
		for (int32 Index = 0; Index < WithLimits.Num() && !bAnyDiffers; ++Index)
		{
			bAnyDiffers = !WithLimits[Index].Equals(WithoutLimits[Index], 0.01);
		}
		Test.TestTrue(TEXT("Collision scenario actually pushes bones (differs from a no-collider run)"), bAnyDiffers);

		return WithLimits;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGoldenPositionsTest,
                                 "KawaiiPhysics.Simulation.GoldenPositions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsGoldenPositionsTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	bOk &= CheckOrCapture(*this, TEXT("Chain"), RunChainScenario(), Golden_Chain);
	bOk &= CheckOrCapture(*this, TEXT("ChainLegacy"), RunChainLegacyScenario(), Golden_ChainLegacy);
	bOk &= CheckOrCapture(*this, TEXT("Constraint"), RunConstraintScenario(), Golden_Constraint);
	bOk &= CheckOrCapture(*this, TEXT("Collision"), RunCollisionScenario(*this), Golden_Collision);
	return bOk;
}

#endif
