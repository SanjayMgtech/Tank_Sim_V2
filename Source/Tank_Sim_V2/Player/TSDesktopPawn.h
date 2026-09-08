// The crew pawn a player possesses when their assigned ETSPlayMode is Desktop: flat screen, mouse
// and keyboard. Everything it does is inherited from ATSCrewPawn - seat, role input contexts,
// driving, the Gunner's mouse-driven aim command - because that is the behaviour BOTH modes share.
//
// It exists as its own class rather than as "the base with nothing added" for two reasons. The
// GameMode needs a concrete class to point Desktop Crew Pawn Class at, and GetSupportedPlayMode has
// to answer Desktop so a pawn the engine spawned during RestartPlayer can be slotted correctly
// without inspecting its class from the outside. Desktop-only tuning belongs here when it appears.
#pragma once

#include "CoreMinimal.h"
#include "Player/TSCrewPawn.h"
#include "TSDesktopPawn.generated.h"

UCLASS()
class ATSDesktopPawn : public ATSCrewPawn
{
	GENERATED_BODY()

public:
	// The base's ApplyDisplayModeDeferred is already the flat-screen path (stereo off, camera reset),
	// so there is nothing to override: this class IS the desktop behaviour.
	virtual ETSPlayMode GetSupportedPlayMode() const override { return ETSPlayMode::Desktop; }
};
