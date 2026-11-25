// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/EngineVersionComparison.h"

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
                                      float GravityScale, float& OutCharacterGravityZ,
                                      bool bUseCharacterGravityDirection = false)
{
	Movement.GravityScale = GravityScale;
	OutCharacterGravityZ = Movement.GetGravityZ();

	UAnimInstance* Anim = NewObject<UAnimInstance>(Character.GetMesh());
	FGravityForceTestProxy Proxy(Anim);
	Proxy.PreUpdate(Anim, 1.0f / 60.0f);

	FGravityForceTestAccessor Gravity;
	Gravity.bUseCharacterGravityScale = true;
	Gravity.bUseCharacterGravityDirection = bUseCharacterGravityDirection;
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
	// TW-GR-01/02/04/07: 修正前は通常の符号と Character 方向の期待値が一致しない。
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Values);
	ACharacter* Character = World->SpawnActor<ACharacter>();
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	bool bOk = TestNotNull(TEXT("Character movement exists"), Movement);
	KawaiiPhysicsNodeFixture::FScopedCVar LegacySign(
		TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), TEXT("0"));
	bOk &= TestNotNull(TEXT("Legacy sign CVar exists"), LegacySign.Variable);
	if (Movement && LegacySign.Variable)
	{

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
		{
			KawaiiPhysicsNodeFixture::FScopedCVar LegacyOverride(
				TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), TEXT("1"));
			const FVector Legacy = EvaluateCharacterGravityForce(*Movement, *Character, 1.0f, CharacterGravityZ);
			bOk &= TestTrue(TEXT("Legacy CVar restores the old upward force"),
				Legacy.Equals(FVector(0.0, 0.0, -CharacterGravityZ), GGravityForceTestTol) && Legacy.Z > 0.0);
		}

#if !UE_VERSION_OLDER_THAN(5, 3, 0)
		// Character のカスタム重力方向（横向き）を使うときも、力は重力の方向へ Character の重力の大きさで引く
		const FVector CustomDirection(1.0, 0.0, 0.0);
		Movement->SetGravityDirection(CustomDirection);
		const FVector Sideways = EvaluateCharacterGravityForce(*Movement, *Character, 1.0f, CharacterGravityZ, true);
		bOk &= TestTrue(FString::Printf(TEXT("Custom gravity direction pulls along it by the character gravity %s"),
			*Sideways.ToString()),
			Sideways.Equals(CustomDirection * FMath::Abs(CharacterGravityZ), GGravityForceTestTol));
		Movement->SetGravityDirection(FVector::DownVector);
#endif

	}
	World->DestroyWorld(false);
	return bOk;
}

static KawaiiPhysicsNodeFixture::FOptions MakeGravityOptions(
	const KawaiiPhysicsNodeFixture::EActorKind ActorKind, const bool bUseScale = true)
{
	KawaiiPhysicsNodeFixture::FOptions Options;
	Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	Options.ActorKind = ActorKind;
	Options.SimulationSpace = EKawaiiPhysicsSimulationSpace::WorldSpace;
	Options.bPreUpdateEveryFrame = true;
	Options.ConfigureNode = [bUseScale](FAnimNode_KawaiiPhysics& Node)
	{
		Node.RootBone.BoneName = TEXT("root");
		auto& Gravity = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Gravity>(Node);
		Gravity.bUseCharacterGravityScale = bUseScale;
	};
	return Options;
}

static FVector ReadEvaluatedGravity(FAutomationTestBase& Test, KawaiiPhysicsNodeFixture::FFixture& Fixture)
{
	Fixture.EvaluateFrame();
	// ボーンが組まれていないと PreApply が呼ばれず、力 0 のまま期待値 0 のケースが素通りする
	Test.TestTrue(TEXT("ModifyBones are built"), Fixture.Node.ModifyBones.Num() > 0);
	return FKawaiiPhysicsTestAccessor::GetExternalForceVector(
		Fixture.Node.ExternalForces[0].Get<FKawaiiPhysics_ExternalForce_Gravity>());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGravityFallbackWithoutCharacterTest,
	"KawaiiPhysics.ExternalForce.GravityFallbackWithoutCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsGravityFallbackWithoutCharacterTest::RunTest(const FString& Parameters)
{
	// GR-2 / TW-GR-10: 修正前は Character 不在で力の大きさが 1 になる。
	KawaiiPhysicsNodeFixture::FOptions Options = MakeGravityOptions(KawaiiPhysicsNodeFixture::EActorKind::Actor);
	Options.ConsoleVariables.Add({TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), TEXT("0")});
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	const FVector Actual = ReadEvaluatedGravity(*this, Fixture);
	return TestTrue(TEXT("World gravity is used without a Character"),
		!Actual.ContainsNaN() && Actual.Equals(FVector(0, 0, Fixture.World->GetGravityZ()), GGravityForceTestTol));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGravityCustomWorldTest,
	"KawaiiPhysics.ExternalForce.GravityCustomWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsGravityCustomWorldTest::RunTest(const FString& Parameters)
{
	// GR-2: 修正前はワールド重力を -500 にしても力の大きさが 1 になる。
	KawaiiPhysicsNodeFixture::FOptions Options = MakeGravityOptions(KawaiiPhysicsNodeFixture::EActorKind::Actor);
	Options.ConsoleVariables.Add({TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), TEXT("0")});
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	Fixture.World->GetWorldSettings()->bGlobalGravitySet = true;
	Fixture.World->GetWorldSettings()->GlobalGravityZ = -500.0f;
	Fixture.World->GetWorldSettings()->bWorldGravitySet = false;
	return TestTrue(TEXT("Custom world gravity is cached in PreUpdate"),
		ReadEvaluatedGravity(*this, Fixture).Equals(FVector(0, 0, -500), GGravityForceTestTol));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGravityBeforePreUpdateTest,
	"KawaiiPhysics.ExternalForce.GravityBeforePreUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsGravityBeforePreUpdateTest::RunTest(const FString& Parameters)
{
	// GR-2: 修正前は PreUpdate 未通過かつ Character 不在で力の大きさが 1 になる。
	// Fixture は構築時に PreUpdate を呼ぶため、この経路だけ直接 PreApply を呼ぶ。
	KawaiiPhysicsNodeFixture::FScopedCVar LegacySign(
		TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), TEXT("0"));
	UWorld* World = KawaiiPhysicsNodeFixture::MakeWorld();
	AActor* Actor = World->SpawnActor<AActor>();
	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Actor);
	Actor->AddInstanceComponent(Component);
	UAnimInstance* Anim = NewObject<UAnimInstance>(Component);
	FGravityForceTestProxy Proxy(Anim);
	Proxy.PreUpdate(Anim, 1.0f / 60.0f);
	FGravityForceTestAccessor Gravity;
	Gravity.bUseCharacterGravityScale = true;
	Gravity.Initialize(FAnimationInitializeContext(&Proxy));
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f);
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::WorldSpace);
	Accessor.SetWorldSpaceTransformForTest(FTransform::Identity);
	FComponentSpacePoseContext Context(&Proxy);
	Gravity.PreApply(Accessor.Node, Context);
	const bool bOk = TestTrue(TEXT("Physics default applies before PreUpdate"),
		Gravity.Force.Equals(FVector(0, 0, UPhysicsSettings::Get()->DefaultGravityZ), GGravityForceTestTol));
	World->DestroyWorld(false);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGravityFallbackLegacySignTest,
	"KawaiiPhysics.ExternalForce.GravityFallbackLegacySign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsGravityFallbackLegacySignTest::RunTest(const FString& Parameters)
{
	// GR-2 / TW-GR-04: 修正前は Character 不在で CVar を変えても力が反転しない。
	KawaiiPhysicsNodeFixture::FOptions Options = MakeGravityOptions(KawaiiPhysicsNodeFixture::EActorKind::Actor);
	Options.ConsoleVariables.Add({TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), TEXT("1")});
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	return TestTrue(TEXT("Legacy fallback reverses world gravity"),
		ReadEvaluatedGravity(*this, Fixture).Equals(FVector(0, 0, -Fixture.World->GetGravityZ()), GGravityForceTestTol));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGravityOverrideBeatsCharacterDirectionTest,
	"KawaiiPhysics.ExternalForce.GravityOverrideBeatsCharacterDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsGravityOverrideBeatsCharacterDirectionTest::RunTest(const FString& Parameters)
{
	// GR-1 / TW-GR-07: 修正前は両フラグが ON だと Character の +X が Override の +Y を上書きする。
#if !UE_VERSION_OLDER_THAN(5, 3, 0)
	KawaiiPhysicsNodeFixture::FOptions Options = MakeGravityOptions(KawaiiPhysicsNodeFixture::EActorKind::Character, false);
	Options.ConfigureNode = [](FAnimNode_KawaiiPhysics& Node)
	{
		Node.RootBone.BoneName = TEXT("root");
		auto& Gravity = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Gravity>(Node);
		Gravity.bUseCharacterGravityDirection = true;
		Gravity.bUseOverrideGravityDirection = true;
		Gravity.OverrideGravityDirection = FVector::YAxisVector;
	};
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	ACharacter* Character = CastChecked<ACharacter>(Fixture.Actor);
	Character->GetCharacterMovement()->SetGravityDirection(FVector::XAxisVector);
	const FVector OverrideForce = ReadEvaluatedGravity(*this, Fixture);
	Fixture.Node.ExternalForces[0].GetMutable<FKawaiiPhysics_ExternalForce_Gravity>().bUseOverrideGravityDirection = false;
	const FVector CharacterForce = ReadEvaluatedGravity(*this, Fixture);
	return TestTrue(TEXT("Override has priority"), OverrideForce.Equals(FVector::YAxisVector, GGravityForceTestTol))
		& TestTrue(TEXT("Character direction works alone"), CharacterForce.Equals(FVector::XAxisVector, GGravityForceTestTol));
#else
	return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGravityScaleDisabledTest,
	"KawaiiPhysics.ExternalForce.GravityScaleDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsGravityScaleDisabledTest::RunTest(const FString& Parameters)
{
	// TW-GR-06: 符号変更の誤適用があれば CVar で既定の単位力が変わる。
	bool bOk = true;
	for (const TCHAR* Value : {TEXT("0"), TEXT("1")})
	{
		KawaiiPhysicsNodeFixture::FOptions Options = MakeGravityOptions(KawaiiPhysicsNodeFixture::EActorKind::Actor, false);
		Options.ConsoleVariables.Add({TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), Value});
		KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
		bOk &= TestTrue(TEXT("Scale disabled retains unit force"),
			ReadEvaluatedGravity(*this, Fixture).Equals(FVector(0, 0, -1), GGravityForceTestTol));
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGravityScaleLinearTest,
	"KawaiiPhysics.ExternalForce.GravityScaleLinear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsGravityScaleLinearTest::RunTest(const FString& Parameters)
{
	// TW-GR-03: 符号や倍率が違えば複数の GravityScale でワールド重力との比例が崩れる。
	KawaiiPhysicsNodeFixture::FOptions Options = MakeGravityOptions(KawaiiPhysicsNodeFixture::EActorKind::Character);
	Options.ConsoleVariables.Add({TEXT("a.AnimNode.KawaiiPhysics.Gravity.LegacyCharacterGravityScaleSign"), TEXT("0")});
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	UCharacterMovementComponent* Movement = CastChecked<ACharacter>(Fixture.Actor)->GetCharacterMovement();
	bool bOk = true;
	for (const float Scale : {-2.0f, -1.0f, -0.5f, 0.0f, 0.5f, 2.0f})
	{
		Movement->GravityScale = Scale;
		bOk &= TestTrue(TEXT("Force follows signed GravityScale"),
			ReadEvaluatedGravity(*this, Fixture).Equals(FVector(0, 0, Fixture.World->GetGravityZ() * Scale), 1e-4f));
	}
	return bOk;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGravityDetailsMetadataTest,
	"KawaiiPhysics.ExternalForce.GravityDetailsMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsGravityDetailsMetadataTest::RunTest(const FString& Parameters)
{
	// GR-1 / TW-GR-08: 修正前は Character 方向の EditCondition が無い。
	const UScriptStruct* Struct = FKawaiiPhysics_ExternalForce_Gravity::StaticStruct();
	const FProperty* CharacterDirection = Struct->FindPropertyByName(TEXT("bUseCharacterGravityDirection"));
	const FProperty* OverrideDirection = Struct->FindPropertyByName(TEXT("bUseOverrideGravityDirection"));
	const FProperty* CharacterScale = Struct->FindPropertyByName(TEXT("bUseCharacterGravityScale"));
	if (!TestNotNull(TEXT("Character direction property"), CharacterDirection)
		|| !TestNotNull(TEXT("Override direction property"), OverrideDirection)
		|| !TestNotNull(TEXT("Character scale property"), CharacterScale)) return false;
	return TestEqual(TEXT("Character direction edit condition"), CharacterDirection->GetMetaData(TEXT("EditCondition")), FString(TEXT("!bUseOverrideGravityDirection")))
		& TestFalse(TEXT("Override has no edit condition"), OverrideDirection->HasMetaData(TEXT("EditCondition")))
		& TestEqual(TEXT("Character scale display name"), CharacterScale->GetMetaData(TEXT("DisplayName")), FString(TEXT("Use Character Gravity")));
}
#endif

} // namespace KawaiiPhysicsGravityForceTestPrivate

#endif
