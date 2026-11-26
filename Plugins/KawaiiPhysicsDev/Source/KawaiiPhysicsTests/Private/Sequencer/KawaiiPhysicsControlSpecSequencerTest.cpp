// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "../Editor/KawaiiPhysicsAnimBlueprintFixture.h"
#include "../Runtime/KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Evaluation/MovieSceneExecutionTokens.h"
#include "Evaluation/MovieScenePlayback.h"
#include "Evaluation/PersistentEvaluationData.h"
#include "IMovieScenePlayer.h"
#include "KawaiiPhysicsSequencerMultiplierRegistry.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierSection.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierTemplate.h"
#include "MovieSceneKawaiiPhysicsSettingsMultiplierTrack.h"
#include "UObject/StrongObjectPtr.h"
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "EntitySystem/MovieSceneSharedPlaybackState.h"
#include "Evaluation/MovieSceneEvaluationState.h"
#endif

namespace KawaiiPhysicsControlSpecSequencerTestPrivate
{
	KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsSequencerReentry, "KawaiiPhysics.Test.SequencerReentry");

	// SQ-08。2051: StopForWorld の比較が逆になり、W2 の Entry が停止し W1 の Entry が残る。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecRegistryWorldStopTest,
		"KawaiiPhysics.Sequencer.Section.Registry_StopForWorldSpec",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecRegistryWorldStopTest::RunTest(const FString& Parameters)
	{
		UWorld* FirstWorld = KawaiiPhysicsNodeFixture::MakeWorld();
		UWorld* SecondWorld = KawaiiPhysicsNodeFixture::MakeWorld();
		AActor* FirstActor = FirstWorld->SpawnActor<AActor>();
		AActor* SecondActor = SecondWorld->SpawnActor<AActor>();
		USkeletalMeshComponent* FirstComponent = NewObject<USkeletalMeshComponent>(FirstActor);
		USkeletalMeshComponent* SecondComponent = NewObject<USkeletalMeshComponent>(SecondActor);
		FirstActor->AddInstanceComponent(FirstComponent);
		SecondActor->AddInstanceComponent(SecondComponent);
		FirstActor->SetRootComponent(FirstComponent);
		SecondActor->SetRootComponent(SecondComponent);
		UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section =
			NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(GetTransientPackage());
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> FirstEntry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		TSharedRef<FKawaiiPhysicsSequencerMultiplierEntry> SecondEntry = MakeShared<FKawaiiPhysicsSequencerMultiplierEntry>();
		FirstEntry->Component = FirstComponent;
		SecondEntry->Component = SecondComponent;
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, FirstEntry);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().Register(Section, SecondEntry);
		bool bOk = TestEqual(TEXT("二つの World の Entry が登録された"),
			FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 2);
		bOk &= TestTrue(TEXT("World が異なる"), FirstComponent->GetWorld() != SecondComponent->GetWorld());
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForWorld(FirstWorld);
		bOk &= TestTrue(TEXT("W1 の Entry は停止"), FirstEntry->bStopped);
		bOk &= TestFalse(TEXT("W2 の Entry は生存"), SecondEntry->bStopped);
		bOk &= TestEqual(TEXT("一件だけ残る"),
			FKawaiiPhysicsSequencerMultiplierRegistry::Get().CountEntriesForSectionForTesting(Section), 1);
		FKawaiiPhysicsSequencerMultiplierRegistry::Get().StopForSection(Section);
		FirstWorld->DestroyWorld(false);
		SecondWorld->DestroyWorld(false);
		return bOk;
	}

	// ルートトラックのテンプレートを評価するための最小の Player。束縛の解決は使わず、再生コンテキストだけを返す。
	class FReentryTestPlayer final : public IMovieScenePlayer
	{
	public:
		explicit FReentryTestPlayer(UObject* InPlaybackContext) : PlaybackContext(InPlaybackContext)
		{
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
			// 5.6 以降の FPersistentEvaluationData は再生状態の FMovieSceneEvaluationState から永続データを引く。
			// Linker は作らないので、Linker 前提の Initialize を通らない Capabilities へ直接足す。
			SharedPlaybackState = MakeShared<UE::MovieScene::FSharedPlaybackState>();
			SharedPlaybackState->GetCapabilities().AddCapability<FMovieSceneEvaluationState>();
			EvaluationTemplate.Initialize(SharedPlaybackState.ToSharedRef());
#endif
		}

		virtual FMovieSceneRootEvaluationTemplateInstance& GetEvaluationTemplate() override { return EvaluationTemplate; }
		virtual EMovieScenePlayerStatus::Type GetPlaybackStatus() const override { return EMovieScenePlayerStatus::Playing; }
		virtual void SetPlaybackStatus(EMovieScenePlayerStatus::Type InPlaybackStatus) override {}
		virtual UObject* GetPlaybackContext() const override { return PlaybackContext.Get(); }
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		virtual void SetViewportSettings(const TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) override {}
		virtual void GetViewportSettings(TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) const override {}
#endif
#if UE_VERSION_OLDER_THAN(5, 4, 0)
		using IMovieScenePlayer::UpdateCameraCut;
		virtual void UpdateCameraCut(UObject* CameraObject, const EMovieSceneCameraCutParams& CameraCutParams) override {}
#endif

	private:
		TWeakObjectPtr<UObject> PlaybackContext;
		FMovieSceneRootEvaluationTemplateInstance EvaluationTemplate;
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
		TSharedPtr<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState;
#endif
	};

	// SQ-06・SQ-08。2053: 停止済み（bStopped）の Entry を作り直さず、同じセクションへ戻ったときに古いハンドルで再駆動する。
	// 落ち方: 戻った後のレジストリに生きている Entry が無く（件数 0・bOutHasLiveEntry=false）、ノードを駆動するハンドルが
	// 最初と同じになり、二度目の StopForSection がその項目へ届かないため BlendOutTimeOnEnd を過ぎても倍率が残る。
	// 同じ評価キーでテンプレートを評価し、FSectionData を Apply をまたいで残す（実際の Sequencer 再生と同じ持ち方）。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecSectionReentryTest,
		"KawaiiPhysics.Sequencer.Section.Template_ReentryAfterStopSpec",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecSectionReentryTest::RunTest(const FString& Parameters)
	{
		constexpr float FrameDt = 1.0f / 60.0f;
		constexpr float SectionBlendOutTime = 0.25f;
		const FGameplayTag NodeTag = TAG_KawaiiPhysicsSequencerReentry;
		KawaiiPhysicsAnimBlueprintFixture::FOptions FixtureOptions;
		FixtureOptions.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(4);
		FixtureOptions.FrameDt = FrameDt;
		FixtureOptions.ConfigureNode = [NodeTag](FAnimNode_KawaiiPhysics& ConfiguredNode, const int32 NodeIndex)
		{
			ConfiguredNode.KawaiiPhysicsTag = NodeTag;
			ConfiguredNode.RootBone.BoneName = TEXT("root");
			ConfiguredNode.DummyBoneLength = 0.0f;
			ConfiguredNode.WarmUpFrames = 0;
			ConfiguredNode.bAllowWorldCollision = false;
			ConfiguredNode.bUseSimpleWorldCollision = false;
		};
		KawaiiPhysicsAnimBlueprintFixture::FFixture Fixture(FixtureOptions);
		for (const FString& Message : Fixture.GetMessages())
		{
			AddInfo(Message);
		}
		if (!TestTrue(TEXT("ABP フィクスチャの準備"), Fixture.IsReady()))
		{
			return false;
		}
		TArray<FKawaiiPhysicsReference> Nodes = Fixture.GetNodes();
		if (!TestEqual(TEXT("ノードは一つ"), Nodes.Num(), 1))
		{
			return false;
		}
		const FAnimNode_KawaiiPhysics& EvaluatedNode = Nodes[0].GetAnimNode<FAnimNode_KawaiiPhysics>();
		const TArray<FKawaiiPhysicsActiveSettingsMultiplier>& Items = EvaluatedNode.TransientForceStore.SettingsMultiplierItems;
		// フィクスチャの最初の StepFrame では倍率の取り込みが起きないため、セクションに入る前にノードを動かしておく
		// （実際の再生でもセクションの開始前から評価が続いている）。
		for (int32 WarmUpFrame = 0; WarmUpFrame < 2; ++WarmUpFrame)
		{
			Fixture.StepFrame();
		}
		if (!TestFalse(TEXT("セクション前にノードが評価されている"), EvaluatedNode.ModifyBones.IsEmpty()))
		{
			return false;
		}

		TStrongObjectPtr<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack> Track(
			NewObject<UMovieSceneKawaiiPhysicsSettingsMultiplierTrack>(GetTransientPackage()));
		Track->bIsRootTrack = true;
		UMovieSceneKawaiiPhysicsSettingsMultiplierSection* Section =
			CastChecked<UMovieSceneKawaiiPhysicsSettingsMultiplierSection>(Track->CreateNewSection());
		Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(1000)));
		Section->Damping.SetDefault(0.5f);
		Section->FilterTags.AddTag(NodeTag);
		Section->BlendOutTimeOnEnd = SectionBlendOutTime;
		Track->AddSection(*Section);
		FMovieSceneEvalTemplatePtr TemplatePtr = Track->CreateTemplateForSection(*Section);
		if (!TestTrue(TEXT("テンプレートを作れる"), TemplatePtr.IsValid()))
		{
			return false;
		}
		const FMovieSceneEvalTemplate& BaseTemplate = *TemplatePtr.GetPtr();

		FReentryTestPlayer Player(Fixture.World);
		FMovieSceneTrackIdentifier TrackIdentifier;
		++TrackIdentifier;
		const FMovieSceneEvaluationKey SectionKey(MovieSceneSequenceID::Root, TrackIdentifier, 0);
		const FMovieSceneEvaluationOperand Operand;
		const FMovieSceneContext InRangeContext(
			FMovieSceneEvaluationRange(FFrameTime(FFrameNumber(500)), FFrameRate(60, 1)), EMovieScenePlayerStatus::Playing);
		auto EvaluateAndApply = [&]()
		{
			FMovieSceneExecutionTokens ExecutionTokens;
			ExecutionTokens.SetOperand(Operand);
			ExecutionTokens.SetCurrentScope(FMovieSceneEvaluationScope(SectionKey, EMovieSceneCompletionMode::RestoreState));
			ExecutionTokens.SetContext(InRangeContext);
			{
				const FPersistentEvaluationData EvaluationData(Player);
				BaseTemplate.Evaluate(Operand, InRangeContext, EvaluationData, ExecutionTokens);
			}
			ExecutionTokens.Apply(InRangeContext, Player);
		};
		FKawaiiPhysicsSequencerMultiplierRegistry& Registry = FKawaiiPhysicsSequencerMultiplierRegistry::Get();
		auto HasLiveEntry = [&Registry, Section]()
		{
			// 停止済み Entry は bOutHasLiveEntry に数えない（bStopped=false の Entry があるかを見る）。
			bool bHasLiveEntry = false;
			const int32 QueuedNodeCount =
				Registry.GetQueuedNodeCount(Section, Section->FilterTags, Section->bFilterExactMatch, bHasLiveEntry);
			return bHasLiveEntry && QueuedNodeCount == 1;
		};
		auto FindDrivenHandle = [&Items]() -> int64
		{
			for (const FKawaiiPhysicsActiveSettingsMultiplier& Item : Items)
			{
				if (Item.bExternallyDriven)
				{
					return Item.HandleId;
				}
			}
			return 0;
		};

		// 範囲内の評価で Push が届き、ノードの評価で駆動項目になる。
		EvaluateAndApply();
		bool bOk = TestEqual(TEXT("範囲内で Entry が一件登録"), Registry.CountEntriesForSectionForTesting(Section), 1);
		bOk &= TestTrue(TEXT("範囲内の Entry は生存"), HasLiveEntry());
		bOk &= TestTrue(TEXT("一フレーム目"), Fixture.StepFrame());
		const int64 FirstHandleId = FindDrivenHandle();
		bOk &= TestTrue(TEXT("最初のハンドルでノードを駆動"), FirstHandleId != 0);

		// セクションを抜けて止める。停止は次のノード評価で取り込まれ、駆動項目が無くなる。
		Registry.StopForSection(Section);
		bOk &= TestEqual(TEXT("停止でレジストリから外れる"), Registry.CountEntriesForSectionForTesting(Section), 0);
		bOk &= TestTrue(TEXT("停止を取り込むフレーム"), Fixture.StepFrame());
		bOk &= TestEqual(TEXT("停止後は駆動項目が無い"), FindDrivenHandle(), static_cast<int64>(0));

		// 同じスコープキーで戻ると、停止済み Entry を捨てて新しいハンドルの Entry を作る。
		EvaluateAndApply();
		bOk &= TestEqual(TEXT("戻った後の Entry は一件"), Registry.CountEntriesForSectionForTesting(Section), 1);
		bOk &= TestTrue(TEXT("戻った後の Entry は停止していない"), HasLiveEntry());
		bOk &= TestTrue(TEXT("戻った後のフレーム"), Fixture.StepFrame());
		const int64 SecondHandleId = FindDrivenHandle();
		bOk &= TestTrue(TEXT("戻った後は新しいハンドルで駆動"), SecondHandleId != 0 && SecondHandleId != FirstHandleId);

		// もう一度止めると BlendOutTimeOnEnd のフェードを経て倍率が消える。τ の原点は Stop を取り込んだ評価（τ=0）で、
		// k 回目の評価で τ=(k−1)×FrameDt。途中は残り、BlendOutTimeOnEnd を一フレーム以上過ぎた時点で消える。
		Registry.StopForSection(Section);
		const int32 MidFrameCount = FMath::FloorToInt(SectionBlendOutTime / FrameDt / 2.0f);
		const int32 EndFrameCount = FMath::CeilToInt(SectionBlendOutTime / FrameDt) + 2;
		for (int32 Frame = 1; Frame <= EndFrameCount; ++Frame)
		{
			bOk &= TestTrue(TEXT("フェード中のフレーム"), Fixture.StepFrame());
			if (Frame == MidFrameCount)
			{
				bOk &= TestTrue(TEXT("フェード途中は二つ目のハンドルの項目が残る"), Items.ContainsByPredicate(
					[SecondHandleId](const FKawaiiPhysicsActiveSettingsMultiplier& Item)
					{
						return Item.HandleId == SecondHandleId && !Item.bExternallyDriven;
					}));
			}
		}
		bOk &= TestEqual(TEXT("BlendOutTimeOnEnd を過ぎると倍率が消える"), Items.Num(), 0);
		Registry.StopForSection(Section);
		return bOk;
	}
}

#endif
