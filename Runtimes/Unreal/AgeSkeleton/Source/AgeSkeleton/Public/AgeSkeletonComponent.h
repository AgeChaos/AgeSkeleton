// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgeSkeletonComponent.generated.h"
class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
namespace ageskeleton {class Player;class Batcher;}

USTRUCT(BlueprintType)
struct FAgeSkeletonAnimationEvent {
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly,Category="AgeSkeleton") FString Name;
    UPROPERTY(BlueprintReadOnly,Category="AgeSkeleton") FString Animation;
    UPROPERTY(BlueprintReadOnly,Category="AgeSkeleton") float Time=0;
    UPROPERTY(BlueprintReadOnly,Category="AgeSkeleton") int32 IntValue=0;
    UPROPERTY(BlueprintReadOnly,Category="AgeSkeleton") float FloatValue=0;
    UPROPERTY(BlueprintReadOnly,Category="AgeSkeleton") FString StringValue;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAgeSkeletonEvent, const FAgeSkeletonAnimationEvent&, Event);

UCLASS(ClassGroup=(Animation),meta=(BlueprintSpawnableComponent))
class AGESKELETON_API UAgeSkeletonComponent : public UActorComponent {
    GENERATED_BODY()
    TUniquePtr<ageskeleton::Player> Player;
    TUniquePtr<ageskeleton::Batcher> Batcher;
    UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> Parts;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    TArray<TArray<FVector>> Vertices;
    TArray<TArray<FVector2D>> UVs;
    TArray<TArray<FLinearColor>> Colors;
    void ApplyPose();void Release();
public:
    UAgeSkeletonComponent();virtual ~UAgeSkeletonComponent()override;
    // Stage this directory as Non-Asset data when packaging; pages are imported texture assets below.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="AgeSkeleton") FString JsonFile;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="AgeSkeleton") TArray<TObjectPtr<UTexture2D>> TexturePages;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="AgeSkeleton") TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="AgeSkeleton",meta=(ClampMin="0.001")) float PixelsPerCentimeter=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="AgeSkeleton") FString InitialAnimation;
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool LoadFile(const FString &Path);
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool Play(const FString &Animation);
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") void Pause();
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") void Resume();
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") void Stop();
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool Seek(float Time);
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool SetSkin(const FString &Skin);
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool SetWardrobe(const FString &Group,const FString &Skin);
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool SetSlotVisible(const FString &Slot,bool Visible);
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool SetAttachment(const FString &Slot,const FString &Key);
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool RestoreAttachment(const FString &Slot);
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") void SetPlaybackSpeed(float Speed);
    UFUNCTION(BlueprintPure,Category="AgeSkeleton") float GetPlaybackTime()const;
    UFUNCTION(BlueprintPure,Category="AgeSkeleton") int32 GetBatchCount()const;
    UPROPERTY(BlueprintAssignable,Category="AgeSkeleton") FAgeSkeletonEvent OnAnimationEvent;
    // Exported skeleton-local pixel coordinates, Y down.
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool SetIKTarget(const FString &Bone,FVector2D Target,int32 ChainLength=2,float Mix=1,int32 Iterations=24,float Tolerance=0.1f);
    UFUNCTION(BlueprintCallable,Category="AgeSkeleton") bool ClearIKTarget(const FString &Bone);
    UFUNCTION(BlueprintPure,Category="AgeSkeleton") bool GetBoneTip(const FString &Bone,FVector2D &Tip)const;
    virtual void BeginPlay()override;
    virtual void EndPlay(const EEndPlayReason::Type Reason)override;
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction *ThisTick)override;
};
