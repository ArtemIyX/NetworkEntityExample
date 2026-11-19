#pragma once
#include "CoreMinimal.h"
class UWorld;
namespace ExampleMovementDebug
{
 /** Returns false without changing OutPosition when no local controller exists. */
 bool TryGetFocalPosition(const UWorld* World, FVector& OutPosition);
}
