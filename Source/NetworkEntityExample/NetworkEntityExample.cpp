// Fill out your copyright notice in the Description page of Project Settings.

#include "NetworkEntityExample.h"
#include "ExampleMod/NativeExampleMod.h"
#include "MovementExample/ExampleMovement.h"
#include "Modules/ModuleManager.h"
#include "Networking/FlecsGenericNetworkSessionSubsystem.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

class FNetworkEntityExampleModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		bool bLiveProbe = false;
#if WITH_DEV_AUTOMATION_TESTS
		FString ProbeRunId;
		if (FParse::Value(FCommandLine::Get(), TEXT("FlecsLiveTest="), ProbeRunId) &&
			!ProbeRunId.IsEmpty() && ProbeRunId.Len() <= 64)
		{
			bLiveProbe = true;
			for (TCHAR Character : ProbeRunId)
			{
				const bool bAlphaNumeric = (Character >= TEXT('A') && Character <= TEXT('Z')) ||
					(Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('0') && Character <= TEXT('9'));
				if (!bAlphaNumeric && Character != TEXT('_') && Character != TEXT('-')) bLiveProbe = false;
			}
		}
#endif
		UFlecsNetworkSessionSubsystem::SetDefaultConfigurator(
			[bLiveProbe](UWorld& World, FFlecsNetworkSessionRegistrar& Registrar, FString& OutError)
			{
				const TSharedRef<FNativeExampleModWorkerState, ESPMode::ThreadSafe> State =
					FNativeExampleModWorkerState::Create();
				FFlecsPeerSpawnFactory SpawnFactory = [bLiveProbe](const FFlecsPeerSpawnContext& Context,
					FFlecsPeerSpawnRequest& OutRequest, FString& Error)
				{
					return FNativeExampleMod::MakePeerSpawnRequest(Context.ViewPosition, bLiveProbe, OutRequest, Error);
				};
				return FNativeExampleMod::RegisterSession(Registrar, State, MoveTemp(SpawnFactory), OutError) &&
					ExampleMovement::Configure(World, Registrar, OutError, ExampleMovement::RegisterDefaultServerSystems);
			}, 1);
	}

	virtual void ShutdownModule() override
	{
		UFlecsNetworkSessionSubsystem::ClearDefaultConfigurator();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FNetworkEntityExampleModule, NetworkEntityExample, "NetworkEntityExample");
