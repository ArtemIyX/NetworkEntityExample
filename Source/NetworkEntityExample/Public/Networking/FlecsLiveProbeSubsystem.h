#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Networking/FlecsNetworkSessionTypes.h"
#include "FlecsLiveProbeSubsystem.generated.h"

class UFlecsNetworkSessionSubsystem;

/** Optional game-owned observer for the example's live networking probe. */
UCLASS()
class UFlecsLiveProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

private:
	void HandleSessionEvent(const FFlecsSessionEvent& Event);
	void LogProbe(const TCHAR* Stage, const TCHAR* Status) const;
	void MaybeRequestReplacement();

	TWeakObjectPtr<UFlecsNetworkSessionSubsystem> Session;
	TWeakObjectPtr<AFlecsReplicationChannel> InitialChannel;
	TWeakObjectPtr<APlayerController> Controller;
	FString RunId;
	float ElapsedSeconds = 0.0f;
	uint32 ManifestSuccessCount = 0;
	uint64 InitialIncarnation = 0;
	uint64 CommandIncarnation = 0;
	uint64 ChangedTransferIncarnation = 0;
	bool bCommandSubmitted = false;
	bool bCommandFinalObserved = false;
	bool bClientStateChanged = false;
	bool bServerCommandAccepted = false;
	bool bChangedTransferPending = false;
	bool bChangedTransferAcknowledged = false;
	bool bReplacementTransferObserved = false;
	bool bFailed = false;
	bool bComplete = false;
};
