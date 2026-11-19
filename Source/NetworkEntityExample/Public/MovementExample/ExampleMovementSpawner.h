#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExampleMovementSpawner.generated.h"

/** Place in a map to start the example population on the authoritative world. */
UCLASS()
class NETWORKENTITYEXAMPLE_API AExampleMovementSpawner : public AActor
{
	GENERATED_BODY()
public:
	AExampleMovementSpawner();
	
protected:
	virtual void BeginPlay() override;
};
