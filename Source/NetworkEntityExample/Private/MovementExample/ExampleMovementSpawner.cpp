#include "MovementExample/ExampleMovementSpawner.h"
#include "MovementExample/ExampleMovementServerSubsystem.h"
#include "Engine/World.h"

AExampleMovementSpawner::AExampleMovementSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

void AExampleMovementSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		if (UExampleMovementServerSubsystem* Example = World->GetSubsystem<UExampleMovementServerSubsystem>())
		{
			Example->StartExample();
		}
	}
}
