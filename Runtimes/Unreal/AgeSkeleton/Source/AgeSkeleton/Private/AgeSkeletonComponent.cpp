// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
#include "AgeSkeletonComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ageskeleton/batcher.hpp"
IMPLEMENT_MODULE(FDefaultModuleImpl,AgeSkeleton)

UAgeSkeletonComponent::UAgeSkeletonComponent(){PrimaryComponentTick.bCanEverTick=true;Player=MakeUnique<ageskeleton::Player>();Batcher=MakeUnique<ageskeleton::Batcher>();}
UAgeSkeletonComponent::~UAgeSkeletonComponent()=default;
void UAgeSkeletonComponent::BeginPlay(){Super::BeginPlay();if(!JsonFile.IsEmpty()&&LoadFile(JsonFile)&&!InitialAnimation.IsEmpty())Play(InitialAnimation);}
void UAgeSkeletonComponent::EndPlay(const EEndPlayReason::Type Reason){Release();Super::EndPlay(Reason);}
void UAgeSkeletonComponent::Release(){for(auto &Part:Parts)if(Part)Part->DestroyComponent();Parts.Empty();Materials.Empty();Vertices.Empty();UVs.Empty();Colors.Empty();}
bool UAgeSkeletonComponent::LoadFile(const FString &Path){
    FString Json;FString Full=FPaths::IsRelative(Path)?FPaths::Combine(FPaths::ProjectContentDir(),Path):Path;
    if(!FFileHelper::LoadFileToString(Json,*Full)||!BaseMaterial||!GetOwner()||!FMath::IsFinite(PixelsPerCentimeter)||PixelsPerCentimeter<=0)return false;
    auto Next=MakeUnique<ageskeleton::Player>();std::string Error;if(!Next->load_json(TCHAR_TO_UTF8(*Json),Error)){UE_LOG(LogTemp,Error,TEXT("AgeSkeleton: %s"),UTF8_TO_TCHAR(Error.c_str()));return false;}
    if(TexturePages.Num()!=int32(Next->data().textures.size()))return false;for(auto &Texture:TexturePages)if(!Texture)return false;
    Release();Player=MoveTemp(Next);Batcher=MakeUnique<ageskeleton::Batcher>();
    for(auto &Texture:TexturePages){auto *Material=UMaterialInstanceDynamic::Create(BaseMaterial,this);Material->SetTextureParameterValue(TEXT("MainTexture"),Texture);Materials.Add(Material);}
    if(!GetOwner()->GetRootComponent()){auto *Root=NewObject<USceneComponent>(GetOwner());GetOwner()->SetRootComponent(Root);GetOwner()->AddInstanceComponent(Root);Root->RegisterComponent();}
    ApplyPose();return true;
}
void UAgeSkeletonComponent::ApplyPose(){
    bool Changed=Batcher->update(*Player);
    if(Changed){
        for(auto &Part:Parts)if(Part)Part->DestroyComponent();Parts.Empty();Vertices.Empty();UVs.Empty();Colors.Empty();
        for(const auto &B:Batcher->batches){
            auto *Part=NewObject<UProceduralMeshComponent>(GetOwner());Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetCastShadow(false);Part->SetupAttachment(GetOwner()->GetRootComponent());GetOwner()->AddInstanceComponent(Part);Part->RegisterComponent();Part->SetMaterial(0,Materials[B.texture]);Part->SetTranslucentSortPriority(Parts.Num());Parts.Add(Part);
            int Count=int(B.positions.size()/2);Vertices.AddDefaulted_GetRef().SetNumZeroed(Count);auto &UV=UVs.AddDefaulted_GetRef();UV.SetNum(Count);Colors.AddDefaulted_GetRef().Init(FLinearColor::White,Count);
            for(int V=0;V<Count;++V)UV[V]=FVector2D(B.uv[V*2],B.uv[V*2+1]);
        }
    }
    for(int I=0;I<Parts.Num();++I){const auto &B=Batcher->batches[I];
        for(int V=0;V<Vertices[I].Num();++V){Vertices[I][V]=FVector(B.positions[V*2]/PixelsPerCentimeter,0,-B.positions[V*2+1]/PixelsPerCentimeter);Colors[I][V]=FLinearColor(B.colors[V*4],B.colors[V*4+1],B.colors[V*4+2],B.colors[V*4+3]);}
        if(Changed){TArray<int32> Indices;for(int Index:B.indices)Indices.Add(Index);Parts[I]->CreateMeshSection_LinearColor(0,Vertices[I],Indices,TArray<FVector>(),UVs[I],Colors[I],TArray<FProcMeshTangent>(),false);}
        else Parts[I]->UpdateMeshSection_LinearColor(0,Vertices[I],TArray<FVector>(),UVs[I],Colors[I],TArray<FProcMeshTangent>());
    }
}
int32 UAgeSkeletonComponent::GetBatchCount()const{return int32(Batcher->batches.size());}
void UAgeSkeletonComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction *Tick){Super::TickComponent(Delta,Type,Tick);if(Player->playing){if(!Player->update(Delta)){UE_LOG(LogTemp,Warning,TEXT("AgeSkeleton playback step rejected"));return;}ApplyPose();const auto Events=Player->events;for(const auto &E:Events){if(!IsValid(this)||!IsValid(GetOwner()))break;FAgeSkeletonAnimationEvent Event;Event.Name=UTF8_TO_TCHAR(E.name.c_str());Event.Animation=UTF8_TO_TCHAR(E.animation.c_str());Event.Time=E.time;Event.IntValue=E.int_value;Event.FloatValue=E.float_value;Event.StringValue=UTF8_TO_TCHAR(E.string_value.c_str());OnAnimationEvent.Broadcast(Event);}}}
bool UAgeSkeletonComponent::Play(const FString &Name){bool Ok=Player->play(TCHAR_TO_UTF8(*Name));if(Ok)ApplyPose();return Ok;}
void UAgeSkeletonComponent::Pause(){Player->playing=false;}void UAgeSkeletonComponent::Resume(){Player->playing=true;}void UAgeSkeletonComponent::Stop(){Player->stop();ApplyPose();}
bool UAgeSkeletonComponent::Seek(float Time){bool Ok=Player->seek(Time);if(Ok)ApplyPose();return Ok;}
bool UAgeSkeletonComponent::SetSkin(const FString &Name){bool Ok=Player->set_skin(TCHAR_TO_UTF8(*Name));if(Ok)ApplyPose();return Ok;}
bool UAgeSkeletonComponent::SetWardrobe(const FString &Group,const FString &Skin){bool Ok=Player->set_wardrobe(TCHAR_TO_UTF8(*Group),TCHAR_TO_UTF8(*Skin));if(Ok)ApplyPose();return Ok;}
bool UAgeSkeletonComponent::SetSlotVisible(const FString &Slot,bool Visible){bool Ok=Player->set_slot_visible(TCHAR_TO_UTF8(*Slot),Visible);if(Ok)ApplyPose();return Ok;}
bool UAgeSkeletonComponent::SetAttachment(const FString &Slot,const FString &Key){bool Ok=Player->set_attachment(TCHAR_TO_UTF8(*Slot),TCHAR_TO_UTF8(*Key));if(Ok)ApplyPose();return Ok;}
bool UAgeSkeletonComponent::RestoreAttachment(const FString &Slot){bool Ok=Player->set_attachment(TCHAR_TO_UTF8(*Slot),"",true);if(Ok)ApplyPose();return Ok;}
void UAgeSkeletonComponent::SetPlaybackSpeed(float Speed){if(FMath::IsFinite(Speed))Player->speed=Speed;}float UAgeSkeletonComponent::GetPlaybackTime()const{return Player->time;}

bool UAgeSkeletonComponent::SetIKTarget(const FString &Bone,FVector2D Target,int32 ChainLength,float Mix,int32 Iterations,float Tolerance){bool Ok=Player->set_ik_target(TCHAR_TO_UTF8(*Bone),float(Target.X),float(Target.Y),ChainLength,Mix,Iterations,Tolerance);if(Ok)ApplyPose();return Ok;}
bool UAgeSkeletonComponent::ClearIKTarget(const FString &Bone){bool Ok=Player->clear_ik_target(TCHAR_TO_UTF8(*Bone));if(Ok)ApplyPose();return Ok;}
bool UAgeSkeletonComponent::GetBoneTip(const FString &Bone,FVector2D &Tip)const{std::array<float,2> Value;if(!Player->bone_tip(TCHAR_TO_UTF8(*Bone),Value))return false;Tip=FVector2D(Value[0],Value[1]);return true;}
