#include "MovementExample/ExampleMovementDebugActor.h"
#include "MovementExample/ExampleMovement.h"
#include "Subsystems/FlecsGameInstanceSubsystem.h"
#include "Subsystems/FlecsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "MovementExample/ExampleMovementStats.h"
#include "MovementExample/ExampleMovementDebug.h"
#include "Diagnostics/FlecsDebugTier.h"
#include "Settings/FlecsDeveloperSettings.h"
#include "GameFramework/PlayerController.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

struct FExampleMovementDebugBuffer
{
	TArray<FExampleMovementSample> Samples;
	bool bOverflow = false;
};

AExampleMovementDebugActor::AExampleMovementDebugActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
}

void AExampleMovementDebugActor::BeginPlay()
{
	Super::BeginPlay();
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}
	if (UGameInstance* Game = GetGameInstance()) Flecs = Game->GetSubsystem<UFlecsGameInstanceSubsystem>();
	PublisherName = FName(*FString::Printf(TEXT("Example.Movement.Debug.%u"), GetUniqueID()));
	StatsDrawHandle = UDebugDrawService::Register(TEXT("OnScreenDebug"), FDebugDrawDelegate::CreateUObject(this, &AExampleMovementDebugActor::DrawStatsOverlay));
}

void AExampleMovementDebugActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (StatsDrawHandle.IsValid()) UDebugDrawService::Unregister(StatsDrawHandle);
	if (bRegistered && Flecs.IsValid()) Flecs->UnregisterPersistentDataPublisher(Epoch, PublisherName);
	if (bSystemRegistered && GetWorld())
		if (UFlecsSubsystem* Systems = GetWorld()->GetSubsystem<UFlecsSubsystem>())
			Systems->UnregisterSystem(
				PublisherName);
	Histories.Empty();
	Super::EndPlay(Reason);
}

void AExampleMovementDebugActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SCOPE_CYCLE_COUNTER(STAT_FlecsExampleDebug);
	if (!Flecs.IsValid() || !GetWorld()) return;
	const uint64 CurrentEpoch = Flecs->GetActiveEpoch();
	if (!CurrentEpoch) return;
	if (Epoch != CurrentEpoch)
	{
		Epoch = CurrentEpoch;
		LastTick = 0;
		Histories.Empty();
		bRegistered = false;
		bSystemRegistered = false;
		Buffer.Reset();
	}
	if (!bSystemRegistered)
	{
		UFlecsSubsystem* Systems = GetWorld()->GetSubsystem<UFlecsSubsystem>();
		if (!Systems) return;
		Buffer = MakeShared<FExampleMovementDebugBuffer, ESPMode::ThreadSafe>();
		const auto WorkerBuffer = Buffer.ToSharedRef();
		bSystemRegistered = Systems->RegisterOnUpdateSystem<
			const FExampleLocation, const FExampleRotation, const FExampleHealth>(
			PublisherName, [WorkerBuffer](flecs::entity Entity, const FExampleLocation& Location,
			                              const FExampleRotation& Rotation, const FExampleHealth& Health)
			{
				if (WorkerBuffer->Samples.Num() >= 1000)
				{
					WorkerBuffer->bOverflow = true;
					return;
				}
				FExampleMovementSample& Sample = WorkerBuffer->Samples.AddDefaulted_GetRef();
				Sample.Entity = Entity.id();
				Sample.Position = Location.Value;
				Sample.Yaw = Rotation.Yaw;
				Sample.Health = Health.Value;
			});
		if (!bSystemRegistered)
		{
			Buffer.Reset();
			return;
		}
	}
	if (!bRegistered)
	{
		const auto WorkerBuffer = Buffer.ToSharedRef();
		const auto Result = Flecs->RegisterPersistentDataPublisher(Epoch, PublisherName,
		                                                           [WorkerBuffer](
		                                                           flecs::world&, FFlecsSimulationDataBuilder& Builder)
		                                                           {
			                                                           TArray<FExampleMovementSample> Samples =
				                                                           MoveTemp(WorkerBuffer->Samples);
			                                                           if (WorkerBuffer->bOverflow)
			                                                           {
				                                                           WorkerBuffer->bOverflow = false;
				                                                           return;
			                                                           }
			                                                           const int64 ByteCount = static_cast<int64>(
				                                                           Samples.Num()) * sizeof(
				                                                           FExampleMovementSample);
			                                                           if (ByteCount > FlecsMaxDataPublicationItemBytes
				                                                           - sizeof(uint32))
				                                                           return;
			                                                           TArray<uint8> Bytes;
			                                                           Bytes.SetNumUninitialized(
				                                                           sizeof(uint32) + static_cast<int32>(
					                                                           ByteCount));
			                                                           const uint32 Count = Samples.Num();
			                                                           FMemory::Memcpy(
				                                                           Bytes.GetData(), &Count, sizeof(Count));
			                                                           if (ByteCount)
				                                                           FMemory::Memcpy(
					                                                           Bytes.GetData() + sizeof(Count),
					                                                           Samples.GetData(),
					                                                           static_cast<SIZE_T>(ByteCount));
			                                                           Builder.TryAdd(
				                                                           ExampleMovement::PublicationType,
				                                                           TArrayView<const uint8>(Bytes));
		                                                           });
		bRegistered = Result == EFlecsWorkerCommandResult::Accepted;
		if (!bRegistered) return;
	}
	const auto Publication = Flecs->GetDataPublication();
	if (Publication.IsValid() && Publication->LocalAttachmentEpoch == Epoch && Publication->SimulationTick != LastTick)
	{
		LastTick = Publication->SimulationTick;
		for (const auto& Item : Publication->Items)
		{
			if (Item.TypeId != ExampleMovement::PublicationType || Item.Payload.Num() < sizeof(uint32)) continue;
			uint32 Count = 0;
			FMemory::Memcpy(&Count, Item.Payload.GetData(), sizeof(Count));
			if (Count > 1000 || Item.Payload.Num() != sizeof(uint32) + Count * sizeof(FExampleMovementSample)) continue;
			TSet<uint64> Seen;
			for (uint32 Index = 0; Index < Count; ++Index)
			{
				FExampleMovementSample Value;
				FMemory::Memcpy(&Value, Item.Payload.GetData() + sizeof(uint32) + Index * sizeof(Value), sizeof(Value));
				Seen.Add(Value.Entity);
				FHistory& History = Histories.FindOrAdd(Value.Entity);
				FSample Sample;
				Sample.Time = Publication->TimestampSeconds;
				Sample.Position = FVector(Value.Position);
				Sample.Yaw = Value.Yaw;
				Sample.Health = Value.Health;
				if (History.Samples.IsEmpty() || History.Samples.Last().Position != Sample.Position ||
					History.Samples.Last().Yaw != Sample.Yaw || History.Samples.Last().Health != Sample.Health)
				{
					History.Samples.Add(Sample);
					if (History.Samples.Num() > 32) History.Samples.RemoveAt(0, History.Samples.Num() - 32);
				}
			}
			for (auto It = Histories.CreateIterator(); It; ++It) if (!Seen.Contains(It.Key())) It.RemoveCurrent();
			break;
		}
	}
	const double RenderTime = FPlatformTime::Seconds() - FMath::Max(0.0f, InterpolationDelaySeconds);
	for (auto& Pair : Histories)
	{
		auto& Samples = Pair.Value.Samples;
		while (Samples.Num() > 2 && Samples[1].Time <= RenderTime) Samples.RemoveAt(0);
		if (Samples.IsEmpty()) continue;
		FVector Position = Samples[0].Position;
		float Yaw = Samples[0].Yaw;
		if (Samples.Num() > 1 && Samples[1].Time > Samples[0].Time)
		{
			const float Alpha = FMath::Clamp(
				static_cast<float>((RenderTime - Samples[0].Time) / (Samples[1].Time - Samples[0].Time)), 0.0f, 1.0f);
			Position = FMath::Lerp(Samples[0].Position, Samples[1].Position, Alpha);
			Yaw = FMath::Lerp(Samples[0].Yaw,
			                  Samples[0].Yaw + FMath::FindDeltaAngleDegrees(Samples[0].Yaw, Samples[1].Yaw), Alpha);
		}
		FColor Color = FColor::Silver;
		FString DistanceText = TEXT("distance/tier unavailable");
		FVector FocalPosition;
		const UFlecsDeveloperSettings* Settings = GetDefault<UFlecsDeveloperSettings>();
		if (Settings && ExampleMovementDebug::TryGetFocalPosition(GetWorld(), FocalPosition))
		{
			const double Distance = FVector::Distance(FocalPosition, Position);
			const int32 Tier = FlecsDiagnostics::FindEstimatedTier(Settings->DistanceUpdateRates, Distance);
			DistanceText = FString::Printf(TEXT("%.1f m, tier unavailable"), Distance / 100.0);
			if (Settings->DistanceUpdateRates.IsValidIndex(Tier))
			{
				const FFlecsDistanceUpdateRate& Rate = Settings->DistanceUpdateRates[Tier];
				Color = Rate.DebugColor;
				DistanceText = FString::Printf(TEXT("%.1f m, estimated %.2f Hz"), Distance / 100.0, Rate.UpdateRateHz);
			}
		}
		DrawDebugBox(GetWorld(), Position, BoxExtent, FRotator(0, Yaw, 0).Quaternion(), Color, false, 0.0f);
		DrawDebugString(GetWorld(), Position + FVector(0, 0, BoxExtent.Z + 15),
			FString::Printf(TEXT("Entity %llu  HP %.1f  %s"), Pair.Key, Samples.Last().Health, *DistanceText),
			nullptr, FColor::White, 0.0f, false);
	}
}

void AExampleMovementDebugActor::DrawStatsOverlay(UCanvas* Canvas, APlayerController* Controller)
{
 if (!bShowStatsOverlay || !Canvas || !Controller || !Controller->IsLocalController() ||
     Controller->GetWorld() != GetWorld() || !GEngine || !GEngine->GetSmallFont()) return;
 const FString Text = FString::Printf(TEXT("Flecs Example: %d visible entities | worker tick %llu"), Histories.Num(), LastTick);
 Canvas->DrawText(GEngine->GetSmallFont(), Text, 24.0f, 170.0f);
}
