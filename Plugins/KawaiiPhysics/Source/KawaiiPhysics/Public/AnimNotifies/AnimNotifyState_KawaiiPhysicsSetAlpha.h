#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_KawaiiPhysicsSetAlpha.generated.h"

UENUM(BlueprintType)
enum class EKawaiiPhysicsSetAlphaSource : uint8
{
	/** Use a float curve from the playing animation. */
	Curve,
	/** Use a constant value. */
	Constant,
};

/**
 * ABPに配置されたKawaiiPhysicsノードのAlphaをNotifyState区間中だけ上書きします。
 * 目的: アニメカーブの値でKawaiiPhysicsのかかり具合をアニメ中に調整する
 */
UCLASS(Blueprintable, meta = (DisplayName = "KawaiiPhyiscs: Set Alpha (Override)"))
class KAWAIIPHYSICS_API UAnimNotifyState_KawaiiPhysicsSetAlpha : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UAnimNotifyState_KawaiiPhysicsSetAlpha(const FObjectInitializer& ObjectInitializer);

	virtual FString GetNotifyName_Implementation() const override;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	                         const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime,
	                        const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                       const FAnimNotifyEventReference& EventReference) override;

public:
	/** Alphaの取得元 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha")
	EKawaiiPhysicsSetAlphaSource Source = EKawaiiPhysicsSetAlphaSource::Curve;

	/** Source=Curveの時に参照するカーブ名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha",
		meta=(EditCondition="Source == EKawaiiPhysicsSetAlphaSource::Curve", EditConditionHides))
	FName CurveName = NAME_None;

	/** カーブが無い/取得できない時のフォールバック値 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha",
		meta=(EditCondition="Source == EKawaiiPhysicsSetAlphaSource::Curve", EditConditionHides, ClampMin="0.0",
			ClampMax="1.0"))
	float DefaultAlphaIfNoCurve = 1.0f;

	/** Source=Constantの時に使う固定値 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha",
		meta=(EditCondition="Source == EKawaiiPhysicsSetAlphaSource::Constant", EditConditionHides, ClampMin="0.0",
			ClampMax="1.0"))
	float ConstantAlpha = 1.0f;

	/** 適用するノードをTagでフィルタ（空なら全ノード対象） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	FGameplayTagContainer FilterTags;

	/** Tagの完全一致でフィルタするか（falseなら親Tagも許容） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bFilterExactMatch = false;

#if WITH_EDITOR
	virtual void ValidateAssociatedAssets() override;
#endif

private:
	float ResolveAlpha(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) const;

	// Saved alpha value at NotifyBegin for restoration.
	UPROPERTY(Transient)
	float SavedAlpha = 1.0f;
	UPROPERTY(Transient)
	bool bHasSavedAlpha = false;
};
