// The crew pawn a player possesses when their assigned ETSPlayMode is VR: same seat, same role
// contexts, same gameplay requests as ATSDesktopPawn, plus stereo rendering and head tracking.
//
// Everything a crew member DOES lives on ATSCrewPawn. All this class adds is the decision to turn
// the headset on and the two knobs that govern it, so the two pawns cannot drift apart in behaviour
// and one Blueprint's worth of input data serves both.
//
// Stereo is a property of ONE local viewport, so nothing here is replicated and none of it belongs
// on the server: the same build runs flat and in a headset, and which one a player gets is decided
// per client from their assigned play mode and whether a headset is actually plugged in.
#pragma once

#include "CoreMinimal.h"
#include "HeadMountedDisplayTypes.h"
#include "Player/TSCrewPawn.h"
#include "TSVRPawn.generated.h"

UCLASS()
class ATSVRPawn : public ATSCrewPawn
{
	GENERATED_BODY()

public:
	virtual ETSPlayMode GetSupportedPlayMode() const override { return ETSPlayMode::VR; }

	// True when this pawn has stereo running. False on every remote copy and on the server's copy of
	// a client's pawn: without the IsLocallyControlled guard, every one of them would answer yes on a
	// machine that merely happens to have a headset attached.
	virtual bool IsVRCrewMode() const override;

	// Turn stereo on only when a headset is actually connected. Off makes this pawn stay flat
	// even in a headset, which is useful for a spectator/debug build.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|VR")
	bool bAutoEnableVRWhenHMDPresent = true;

	// Seated origin (Local) is correct for a tank crew: tracking is centred on where the headset
	// was at start, so the player's head sits at the seat component rather than on the tank floor.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|VR")
	TEnumAsByte<EHMDTrackingOrigin::Type> VRTrackingOrigin = EHMDTrackingOrigin::Local;

protected:
	// Switches stereo ON, but only when this player has actually been ASSIGNED VR. Possessing this
	// pawn is not on its own permission to take over the viewport: a lone crew Blueprint configured
	// for both modes is possessed in Desktop mode too, and a host handed a crew pawn by a test flag
	// must stay flat. When VR is not wanted, this defers to the base, which is the flat-screen path.
	virtual void ApplyDisplayModeDeferred() override;
};
