// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

// Project-wide log category. Filter the Output Log (or run `Log LogTankSim Verbose` in the console)
// to see only framework messages - session flow, per-team tank spawning, role assignment, UI sweeps.
DECLARE_LOG_CATEGORY_EXTERN(LogTankSim, Log, All);
