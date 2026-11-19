#include "Networking/FlecsLiveProbeSubsystem.h"

#include "Engine/World.h"
#include "ExampleMod/NativeExampleMod.h"
#include "FlecsReplicationChannel.h"
#include "Networking/FlecsGenericNetworkSessionSubsystem.h"
#include "Stats/Stats.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

bool UFlecsLiveProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if WITH_DEV_AUTOMATION_TESTS
	FString Value;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("FlecsLiveTest="), Value);
#else
	(void)Outer;
	return false;
#endif
}

void UFlecsLiveProbeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_DEV_AUTOMATION_TESTS
	FString Candidate;
	if (!FParse::Value(FCommandLine::Get(), TEXT("FlecsLiveTest="), Candidate) || Candidate.IsEmpty() || Candidate.Len()
		> 64) return;
	for (TCHAR Character : Candidate)
	{
		const bool bAlphaNumeric = (Character >= TEXT('A') && Character <= TEXT('Z')) ||
			(Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('0') && Character <= TEXT('9'));
		if (!bAlphaNumeric && Character != TEXT('_') && Character != TEXT('-')) return;
	}
	RunId = MoveTemp(Candidate);
	Session = Collection.InitializeDependency<UFlecsNetworkSessionSubsystem>();
	UFlecsNetworkSessionSubsystem* ActiveSession = Session.Get();
	if (!ActiveSession || !ActiveSession->SetClientInspectionComponent(FNativeExampleMod::VitalsComponentId()))
	{
		bFailed = true;
		LogProbe(TEXT("enabled"), TEXT("failed_session_setup"));
		return;
	}
	ActiveSession->OnSessionEvent.AddUObject(this, &UFlecsLiveProbeSubsystem::HandleSessionEvent);
	LogProbe(TEXT("enabled"), TEXT("waiting_for_manifest"));
	FString PacketLag, PacketLoss;
	const bool bHasLag = FParse::Value(FCommandLine::Get(), TEXT("PktLag="), PacketLag);
	const bool bHasLoss = FParse::Value(FCommandLine::Get(), TEXT("PktLoss="), PacketLoss);
	PacketLag = PacketLag.Left(32);
	PacketLoss = PacketLoss.Left(32);
	UE_LOG(LogTemp, Display, TEXT("FlecsLiveTest run=%s stage=network_settings pkt_lag=%s pkt_loss=%s"),
	       *RunId, bHasLag ? *PacketLag : TEXT("engine_default"), bHasLoss ? *PacketLoss : TEXT("engine_default"));
#endif
}

void UFlecsLiveProbeSubsystem::Deinitialize()
{
	if (UFlecsNetworkSessionSubsystem* ActiveSession = Session.Get()) ActiveSession->OnSessionEvent.RemoveAll(this);
	Session.Reset();
	Super::Deinitialize();
}

void UFlecsLiveProbeSubsystem::Tick(float DeltaTime)
{
	if (RunId.IsEmpty() || bFailed || bComplete) return;
	ElapsedSeconds += FMath::Max(0.0f, DeltaTime);
	if (ElapsedSeconds >= 60.0f)
	{
		bFailed = true;
		LogProbe(TEXT("timeout"), TEXT("failed_after_60_seconds"));
	}
}

TStatId UFlecsLiveProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFlecsLiveProbeSubsystem, STATGROUP_Tickables);
}

bool UFlecsLiveProbeSubsystem::IsTickable() const
{
	return !RunId.IsEmpty() && GetWorld() != nullptr;
}

void UFlecsLiveProbeSubsystem::LogProbe(const TCHAR* Stage, const TCHAR* Status) const
{
	if (RunId.IsEmpty()) return;
	const UWorld* World = GetWorld();
	const TCHAR* Side = World && World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server");
	UE_LOG(LogTemp, Display, TEXT("FlecsLiveTest run=%s side=%s stage=%s status=%s"), *RunId, Side, Stage, Status);
}

void UFlecsLiveProbeSubsystem::MaybeRequestReplacement()
{
	if (!bServerCommandAccepted || !bChangedTransferAcknowledged || CommandIncarnation != ChangedTransferIncarnation)
		return;
	bChangedTransferAcknowledged = false;
	LogProbe(TEXT("changed_state_transfer"), TEXT("client_worker_acknowledged"));
	FConnectionIncarnation Incarnation;
	Incarnation.Value = CommandIncarnation;
	if (UFlecsNetworkSessionSubsystem* ActiveSession = Session.Get(); ActiveSession && ActiveSession->
		RequestChannelRecreation(Incarnation))
		LogProbe(TEXT("channel_replacement"), TEXT("requested_same_world_recreate"));
	else
	{
		bFailed = true;
		LogProbe(TEXT("channel_replacement"), TEXT("failed_request"));
	}
}

void UFlecsLiveProbeSubsystem::HandleSessionEvent(const FFlecsSessionEvent& Event)
{
	if (RunId.IsEmpty() || bFailed || bComplete) return;
	UFlecsNetworkSessionSubsystem* ActiveSession = Session.Get();
	AFlecsReplicationChannel* Channel = Event.Channel.Get();
	if (!ActiveSession || !IsValid(Channel)) return;
	if (Event.Kind == EFlecsSessionEventKind::Compatible)
	{
		if (!Controller.IsValid()) Controller = Event.Controller;
		if (Controller != Event.Controller) return;
		++ManifestSuccessCount;
		if (ManifestSuccessCount == 1)
		{
			InitialChannel = Channel;
			InitialIncarnation = Event.Incarnation.Value;
			LogProbe(TEXT("manifest"), TEXT("compatible_initial_channel"));
		}
		else if (InitialChannel.Get() != Channel)
		{
			const bool bNewIncarnation = Event.Incarnation.Value != InitialIncarnation;
			LogProbe(TEXT("manifest"), bNewIncarnation
				                           ? TEXT("compatible_replacement_channel_new_incarnation")
				                           : TEXT("failed_replacement_incarnation_unchanged"));
			bFailed |= !bNewIncarnation;
		}
		return;
	}
	if (Event.Kind == EFlecsSessionEventKind::Rejected)
	{
		bFailed = true;
		LogProbe(ManifestSuccessCount ? TEXT("replication") : TEXT("manifest"),
		         ManifestSuccessCount ? TEXT("failed_after_manifest_compatibility") : TEXT("failed_handshake"));
		return;
	}
	if (Event.Kind == EFlecsSessionEventKind::ServerCommandFinished && Event.CommandSequence == 1 &&
		Event.CommandResult == FlecsNetworkCommandResult::ExecutionAccepted)
	{
		bServerCommandAccepted = true;
		CommandIncarnation = Event.Incarnation.Value;
		LogProbe(TEXT("authorized_command_final"), TEXT("server_worker_execution_accepted"));
		MaybeRequestReplacement();
		return;
	}
	if (Event.Kind == EFlecsSessionEventKind::ServerTransferStarted && Event.Incarnation.Value == InitialIncarnation &&
		Event.InspectedComponentBytes.Num() == 15 &&
		Event.InspectedComponentBytes[13] == 0 && Event.InspectedComponentBytes[14] == 26)
	{
		bChangedTransferPending = true;
		ChangedTransferIncarnation = Event.Incarnation.Value;
		return;
	}
	if (Event.Kind == EFlecsSessionEventKind::ServerTransferAcknowledged)
	{
		if (!bReplacementTransferObserved && ManifestSuccessCount >= 2 && InitialChannel.Get() != Channel)
		{
			bReplacementTransferObserved = true;
			bComplete = true;
			LogProbe(TEXT("replacement_transfer"), TEXT("client_worker_acknowledged"));
			LogProbe(TEXT("complete"), TEXT("server_observed_replacement_revision_ack"));
		}
		else if (bChangedTransferPending && Event.Incarnation.Value == ChangedTransferIncarnation)
		{
			bChangedTransferPending = false;
			bChangedTransferAcknowledged = true;
			MaybeRequestReplacement();
		}
		return;
	}
	if (Event.Kind == EFlecsSessionEventKind::ClientCommandResult && Event.CommandSequence == 1 && !
		bCommandFinalObserved)
	{
		if (Event.CommandResult == FlecsNetworkCommandResult::AcceptedForWorker) return;
		bCommandFinalObserved = Event.CommandResult == FlecsNetworkCommandResult::ExecutionAccepted;
		LogProbe(TEXT("authorized_command_final"), bCommandFinalObserved
			                                           ? TEXT("execution_accepted")
			                                           : TEXT("execution_failed"));
		if (!bCommandFinalObserved) bFailed = true;
		else if (bClientStateChanged) LogProbe(TEXT("command_state_verification"), TEXT("passed"));
		return;
	}
	if (Event.Kind != EFlecsSessionEventKind::ClientStateApplied || !Event.InspectedIdentity.IsValid() ||
		Event.InspectedComponentBytes.Num() != 15)
		return;
	const uint16 Energy = static_cast<uint16>((static_cast<uint16>(Event.InspectedComponentBytes[13]) << 8) |
		Event.InspectedComponentBytes[14]);
	if (!bCommandSubmitted && Energy == 1)
	{
		LogProbe(TEXT("client_worker_component_inspection"), TEXT("initial_energy_1"));
		FlecsModRegistry::FSessionMapping Mapping;
		uint16 CommandWireId = 0;
		TArray<uint8> Payload;
		if (!ActiveSession->GetPeerSessionMapping(Event.Incarnation, Mapping) ||
			!Mapping.FindWireId(FlecsModRegistry::EEntryKind::Command, FNativeExampleMod::RechargeCommandId(),
			                    CommandWireId) ||
			!FNativeExampleMod::EncodeRechargeCommand(25, Payload))
		{
			bFailed = true;
			LogProbe(TEXT("authorized_command_submit"), TEXT("failed_schema_mapping"));
			return;
		}
		FFlecsCommandEnvelope Envelope;
		Envelope.WorldEpoch = Event.ServerWorldEpoch;
		Envelope.Sequence = 1;
		Envelope.TargetNetworkEntityId = Event.InspectedIdentity.NetworkEntityId;
		Envelope.TargetEntityGeneration = Event.InspectedIdentity.Generation;
		Envelope.RequestedSimulationTick = FMath::Max(Event.SimulationTick, Channel->GetServerSimulationTick());
		Envelope.CommandWireId = CommandWireId;
		Envelope.Payload = Payload;
		Channel->SubmitDiscreteCommand(Envelope);
		bCommandSubmitted = true;
		LogProbe(TEXT("authorized_command_submit"), TEXT("submitted_recharge_25"));
	}
	else if (bCommandSubmitted && Energy != 1 && !bClientStateChanged)
	{
		bClientStateChanged = Energy == 26;
		LogProbe(TEXT("client_worker_component_inspection"), bClientStateChanged
			                                                     ? TEXT("energy_changed_to_26")
			                                                     : TEXT("failed_unexpected_energy"));
		if (!bClientStateChanged) bFailed = true;
		else if (bCommandFinalObserved) LogProbe(TEXT("command_state_verification"), TEXT("passed"));
	}
	if (ManifestSuccessCount >= 2 && InitialChannel.Get() != Channel && Event.SnapshotRevision > 0)
	{
		LogProbe(TEXT("replacement_state"), TEXT("worker_applied_new_channel_revision"));
		if (bClientStateChanged && bCommandFinalObserved)
		{
			bComplete = true;
			LogProbe(TEXT("complete"), TEXT("client_observed_command_and_replacement"));
		}
	}
}
