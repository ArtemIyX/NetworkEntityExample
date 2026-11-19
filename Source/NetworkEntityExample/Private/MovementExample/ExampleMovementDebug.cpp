#include "MovementExample/ExampleMovementDebug.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool ExampleMovementDebug::TryGetFocalPosition(const UWorld* World, FVector& OutPosition)
{
 if (!World) return false;
 for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
 {
  const APlayerController* Candidate = It->Get();
  if (Candidate && Candidate->IsLocalController()) { OutPosition = Candidate->GetFocalLocation(); return true; }
 }
 return false;
}
