// Editor-only console commands for driving a headset test session without touching the toolbar.
//
// The Python API (LevelEditorSubsystem) can only start a normal in-viewport PIE; VR Preview is a
// toolbar-only action. TSPlayInVR makes the exact same request the toolbar's VR Preview button
// makes, so a scripted test (Monolith run_console_command) can launch the headset session and then
// seat the player with TSPlayMode / TSTeam / TSRole. Compiled out of every non-editor build.
#if WITH_EDITOR

#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "PlayInEditorDataTypes.h"
#include "Tank_Sim_V2.h"

static FAutoConsoleCommand GTSPlayInVRCommand(
	TEXT("TSPlayInVR"),
	TEXT("Editor only: starts Play > VR Preview on the open level, like the toolbar button."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (!GEditor)
		{
			return;
		}

		if (GEditor->PlayWorld || GEditor->IsPlaySessionRequestQueued())
		{
			UE_LOG(LogTankSim, Warning, TEXT("TSPlayInVR: a play session is already running or queued."));
			return;
		}

		FRequestPlaySessionParams Params;
		Params.SessionPreviewTypeOverride = EPlaySessionPreviewType::VRPreview;
		GEditor->RequestPlaySession(Params);
		UE_LOG(LogTankSim, Log, TEXT("TSPlayInVR: VR Preview requested."));
	}));

#endif // WITH_EDITOR
