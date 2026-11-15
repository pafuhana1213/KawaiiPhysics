// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace KawaiiPhysicsGravityForceTestPrivate
{
constexpr float GGravityForceTestTol = KINDA_SMALL_NUMBER;

struct FGravityForceTestAccessor : FKawaiiPhysics_ExternalForce_Gravity
{
	FGravityForceTestAccessor()
	{
		bSupportsRandomForceScaleRange = false;
		RandomizedForceScale = 1.0f;
	}

	using FKawaiiPhysics_ExternalForce_Gravity::Force;
	using FKawaiiPhysics_ExternalForce_Gravity::Initialize;
	using FKawaiiPhysics_ExternalForce_Gravity::PreApply;
};

struct FGravityForceTestProxy : FAnimInstanceProxy
{
	using FAnimInstanceProxy::FAnimInstanceProxy;
	using FAnimInstanceProxy::PreUpdate;
};

// Character の GravityScale を使う Gravity 外力の、PreApply 後の力を返す
FVector EvaluateCharacterGravityForce(UCharacterMovementComponent& Movement, ACharacter& Character,
                                      float GravityScale, float& OutCharacterGravityZ)
{
	Movement.GravityScale = GravityScale;
	OutCharacterGravityZ = Movement.GetGravityZ();

	UAnimInstance* Anim = NewObject<UAnimInstance>(Character.GetMesh());
	FGravityForceTestProxy Proxy(Anim);
	Proxy.PreUpdate(Anim, 1.0f / 60.0f);

	FGravityForceTestAccessor Gravity;
	Gravity.bUseCharacterGravityScale = true;
	Gravity.Initialize(FAnimationInitializeContext(&Proxy));

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f);
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::WorldSpace);
	Accessor.SetWorldSpaceTransformForTest(FTransform::Identity);
	FComponentSpacePoseContext PoseContext(&Proxy);
	Gravity.PreApply(Accessor.Node, PoseContext);
	return Gravity.Force;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGravityCharacterScaleDirectionTest,
                                 "KawaiiPhysics.ExternalForce.GravityCharacterScaleDirection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsGravityCharacterScaleDirectionTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Values);
	ACharacter* Character = World->SpawnActor<ACharacter>();
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	bool bOk = TestNotNull(TEXT("Character movement exists"), Movement);
	// 互換用 CVar の今の値に結果が左右されないよう、検証中は最も優先度の高い設定方法（Console）で値を切り替え、
	// 最後に元の値と優先度へ戻す（Code で設定すると、コンソールや ini で設定済みの値に負けて無視される）
	IConsoleVariable* LegacyCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"));
	bOk &= TestNotNull(TEXT("Legacy sign CVar exists"), LegacyCVar);
	if (Movement && LegacyCVar)
	{
		const bool bPrevLegacy = LegacyCVar->GetBool();
		const EConsoleVariableFlags PrevFlags = LegacyCVar->GetFlags();
		LegacyCVar->Set(false, ECVF_SetByConsole);

		// 通常の GravityScale では、ワールドの重力と同じ下向きになる
		float CharacterGravityZ = 0.0f;
		const FVector Down = EvaluateCharacterGravityForce(*Movement, *Character, 1.0f, CharacterGravityZ);
		bOk &= TestTrue(TEXT("World gravity points down"), CharacterGravityZ < 0.0f);
		bOk &= TestTrue(TEXT("GravityScale 1 pulls down by the character gravity"),
			Down.Equals(FVector(0.0, 0.0, CharacterGravityZ), GGravityForceTestTol));

		// 負の GravityScale（Character の重力を反転）では上向きになる
		const FVector Up = EvaluateCharacterGravityForce(*Movement, *Character, -0.5f, CharacterGravityZ);
		bOk &= TestTrue(TEXT("Negative GravityScale pulls up by the character gravity"),
			Up.Equals(FVector(0.0, 0.0, CharacterGravityZ), GGravityForceTestTol) && Up.Z > 0.0);

		// 互換用 CVar を有効にすると、修正前の上向きの挙動に戻る
		LegacyCVar->Set(true, ECVF_SetByConsole);
		const FVector Legacy = EvaluateCharacterGravityForce(*Movement, *Character, 1.0f, CharacterGravityZ);
		bOk &= TestTrue(TEXT("Legacy CVar restores the old upward force"),
			Legacy.Equals(FVector(0.0, 0.0, -CharacterGravityZ), GGravityForceTestTol) && Legacy.Z > 0.0);

		LegacyCVar->Set(bPrevLegacy, ECVF_SetByConsole);
		LegacyCVar->SetFlags(PrevFlags);
	}
	World->DestroyWorld(false);
	return bOk;
}

} // namespace KawaiiPhysicsGravityForceTestPrivate

#endif
