// One place that answers "are we in VR right now, and should we be?".
//
// The project runs the SAME build on a desktop and in a headset - there is no separate VR mode
// switch and no VR-only map. Whether stereo comes on is decided at runtime, per client, from two
// facts: is an HMD actually plugged in, and is this local player allowed to use it. The host is
// not (it is a match admin on a flat screen), so the question is never just "is a headset here".
//
// Everything is static and client-side. Stereo rendering is a property of the local viewport, so
// none of this is replicated and none of it belongs on the server.
#pragma once

#include "CoreMinimal.h"
#include "HeadMountedDisplayTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TSVRModeLibrary.generated.h"

UCLASS()
class UTSVRModeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// True when the engine actually has an XR stereo device to talk to. Distinct from
	// IsHMDAvailable: an XR runtime (Oculus, SteamVR) can be installed and initialised - the log
	// says "Initialized OpenXR on Oculus runtime" - with NO headset plugged into it. Asking that
	// half-present stack to start stereo is what hung the GPU (DXGI_ERROR_DEVICE_HUNG), so both
	// questions have to be answered before stereo is switched on.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR")
	static bool IsStereoDeviceUsable();

	// True when a headset is connected AND the stereo device is usable. This is the "is a headset
	// plugged in" question only - it says nothing about whether we have switched stereo on, and it
	// is false in a plain editor PIE session with no HMD, which is exactly what makes the automatic
	// desktop fallback work.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR")
	static bool IsHMDAvailable();

	// True when stereo rendering is actually running for this client right now.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR")
	static bool IsVRModeActive();

	// Turns stereo on or off for THIS client and, when enabling, sets the tracking origin.
	// Returns the mode that is actually in effect afterwards, which is NOT always what was asked
	// for - enabling with no headset connected cannot succeed, and the caller needs to know that
	// rather than assume it.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR")
	static bool SetVRModeEnabled(bool bEnable, TEnumAsByte<EHMDTrackingOrigin::Type> TrackingOrigin = EHMDTrackingOrigin::Local);

	// Re-centres the headset so "forward" becomes wherever the player is currently looking. Seated
	// in a tank this is what puts their head back on the crew station after they shift in the chair.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR")
	static void RecenterHMD();

	// True while the XR system is feeding head poses. Distinct from IsVRModeActive: head tracking
	// can be live while stereo is off, which is precisely the state the host must not be left in
	// (a flat screen that still swings around with the headset on the desk).
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR")
	static bool IsHeadTrackingActive();
};
