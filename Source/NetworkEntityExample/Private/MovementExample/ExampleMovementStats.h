#pragma once
#include "Stats/Stats.h"
DECLARE_STATS_GROUP(TEXT("Flecs Example"), STATGROUP_FlecsExample, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Movement entity update"), STAT_FlecsExampleMovement, STATGROUP_FlecsExample, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Debug actor draw"), STAT_FlecsExampleDebug, STATGROUP_FlecsExample, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Movement entity updates"), STAT_FlecsExampleEntities, STATGROUP_FlecsExample, );
