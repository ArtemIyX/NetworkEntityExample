#include "MovementExample/ExampleMovementServerSubsystem.h"
#include "MovementExample/ExampleMovement.h"
#include "Networking/FlecsGenericNetworkSessionSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Subsystems/FlecsGameInstanceSubsystem.h"
#include "Stats/Stats.h"

void UExampleMovementServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UExampleMovementServerSubsystem::Deinitialize()
{
	if (Session.IsValid()) Session->OnSessionEvent.RemoveAll(this);
	if (GetWorld()) ExampleMovement::ForgetServerSystemRegistrar(*GetWorld());
	Pending.Empty();
	Session.Reset();
	Super::Deinitialize();
}

bool UExampleMovementServerSubsystem::StartExample()
{
	check(IsInGameThread());
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;
	bStartRequested = true;
	return true;
}

void UExampleMovementServerSubsystem::SetSystemRegistrar(FFlecsWorkerCommand Registrar)
{
	check(IsInGameThread());
	if (!bSystemsRegistered) SystemRegistrar = MoveTemp(Registrar);
}

void UExampleMovementServerSubsystem::Tick(float)
{
	if (!bStartRequested || !GetWorld()) return;
	if (!Session.IsValid())
	{
		Session = GetWorld()->GetSubsystem<UFlecsNetworkSessionSubsystem>();
		if (!Session.IsValid()) return;
		Session->OnSessionEvent.AddUObject(this, &UExampleMovementServerSubsystem::HandleEvent);
	}
	if (!Session->IsModRegistryFrozen()) return;
	if (!SystemRegistrar) SystemRegistrar = ExampleMovement::TakeServerSystemRegistrar(*GetWorld());
	if (!SystemRegistrar) return;
	if (!bSystemsRegistered)
	{
		UGameInstance* Game = GetWorld()->GetGameInstance();
		UFlecsGameInstanceSubsystem* Flecs = Game ? Game->GetSubsystem<UFlecsGameInstanceSubsystem>() : nullptr;
		if (!Flecs || !Flecs->GetActiveEpoch()) return;
		FFlecsWorkerCommand Register = SystemRegistrar;
		if (Flecs->SubmitWorkerCommand(Flecs->GetActiveEpoch(), MoveTemp(Register)) != EFlecsWorkerCommandResult::Accepted) return;
		bSystemsRegistered = true;
	}
	const int32 Count = FMath::Clamp(EntityCount, 0, 1000);
	while (Submitted < Count && Pending.Num() < 32)
	{
		uint64 RequestId = 0;
		FString Error;
		if (!Session->QueueSharedEntitySpawn(ExampleMovement::MakeSpawn(Submitted), RequestId, Error)) break;
		Pending.Add(RequestId);
		++Submitted;
	}
}

void UExampleMovementServerSubsystem::HandleEvent(const FFlecsSessionEvent& Event)
{
	if (Event.Kind != EFlecsSessionEventKind::SharedSpawnFinished || !Pending.Contains(Event.SpawnRequestId)) return;
	Pending.Remove(Event.SpawnRequestId);
	if (Event.SpawnResult != FlecsSimulation::EModelResult::Accepted)
		UE_LOG(LogTemp, Error, TEXT("Example movement entity spawn failed: %u"), static_cast<uint32>(Event.SpawnResult));
}

bool UExampleMovementServerSubsystem::IsTickable() const
{
	return !IsTemplate() && bStartRequested && GetWorld() && GetWorld()->GetNetMode() != NM_Client;
}

TStatId UExampleMovementServerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UExampleMovementServerSubsystem, STATGROUP_Tickables);
}
