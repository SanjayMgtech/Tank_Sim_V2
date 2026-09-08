// Owns the "which widgets belong to which map" rule for the framework.
//
// The main-menu level's Login / Session Browser widgets have no business being on screen once the
// host has travelled into the WarZone map, but they can outlive the travel: a widget created with
// the GameInstance (or a persistent LocalPlayer) in its ownership chain is not torn down with the
// old UWorld, and seamless travel keeps the viewport contents alive by design. This subsystem
// sweeps them away on every non-menu map load instead of relying on the level Blueprint to
// remember to remove them.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TSUISubsystem.generated.h"

class APlayerController;
class UUserWidget;

// Where this local player's UI is rendered. Deliberately NOT "is this the host" - see
// UTSUISubsystem::GetPresentationMode.
UENUM(BlueprintType)
enum class ETSUIPresentationMode : uint8
{
	// AddToViewport. The only mode that works on a flat screen, and the only one a headset cannot see.
	Flat,
	// A UWidgetComponent in the world, pointed at with a UWidgetInteractionComponent. The only mode a
	// headset CAN see: a screen-space widget draws to the flat viewport, which does not exist in stereo.
	World
};

// Manual override of the device-derived presentation mode. None = follow the headset.
UENUM(BlueprintType)
enum class ETSUIPresentationOverride : uint8
{
	None,
	ForceFlat,
	ForceWorld
};

UCLASS(Config = Game)
class UTSUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UTSUISubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Removes every menu widget currently parented anywhere in this GameInstance and returns how many
	// were removed. Safe to call repeatedly and safe to call on a menu map (it is simply the caller's
	// job not to). Menu widgets are identified by MenuWidgetClasses first, then by
	// MenuWidgetNameFragments for widgets that are pure Blueprint and derive straight from UUserWidget.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	int32 RemoveMenuWidgets();

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	bool IsMenuMap(const FString& MapName) const;

	// PIE-safe: strips the UEDPIE_N_ prefix before matching against MenuMapNames.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	bool IsCurrentMapMenuMap() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	FString GetCurrentMapName() const;

	// --- UI presentation routing ------------------------------------------------------------------
	// The single place that answers "flat or world-space" for a local player.
	//
	// PRESENTATION AND CONTENT ARE SEPARATE AXES. What a player SEES (host lobby console vs Driver /
	// Gunner / Commander HUD) comes from ATSTankPlayerState's role and IsHost(). WHERE it renders
	// comes from the display device, and nothing else. Keying presentation off host-vs-client is the
	// tempting shortcut and it is wrong: it breaks the moment anyone changes which players may wear a
	// headset.
	//
	// Today UTSVRModeLibrary already resolves to flat for the host, because that class treats the host
	// as a flat-screen match admin by policy. This router does not re-implement that decision - it
	// asks, so there is one owner of it.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	ETSUIPresentationMode GetPresentationMode() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	bool ShouldUseWorldSpaceUI() const;

	// FALSE in VR. bShowMouseCursor and FInputModeGameAndUI are meaningless in a headset - there is no
	// OS cursor to show - but they still CAPTURE input, so setting them in VR is a silent input sink
	// that looks like nothing happening. Every site that touches the cursor must ask this first.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	bool ShouldUseMouseCursor() const;

	// Registers an extra widget class to treat as a menu widget (for menu UI added later without
	// touching this class or the ini).
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void RegisterMenuWidgetClass(TSubclassOf<UUserWidget> WidgetClass);

protected:
	// Maps on which menu widgets are allowed to stay. Override in DefaultGame.ini under
	// [/Script/Tank_Sim_V2.TSUISubsystem] with +MenuMapNames=YourMenuMap.
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Tank Simulation|UI")
	TArray<FName> MenuMapNames;

	// Case-insensitive substrings matched against a widget's class name, for menu widgets that do not
	// derive from one of the framework's C++ widget classes.
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Tank Simulation|UI")
	TArray<FString> MenuWidgetNameFragments;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|UI")
	TArray<TSubclassOf<UUserWidget>> MenuWidgetClasses;

	// Overrides the device-derived presentation mode.
	//
	// Two-directional on purpose. ForceFlat is the escape hatch if world-space UI regresses on a VR
	// machine. ForceWorld is how world-space panels get authored and reviewed on a desktop at all -
	// without it the world path can only ever be exercised inside a headset, which is also the one
	// place none of this can be measured from a log.
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Tank Simulation|UI")
	ETSUIPresentationOverride PresentationOverride = ETSUIPresentationOverride::None;

private:
	void HandlePostLoadMap(UWorld* LoadedWorld);
	bool IsMenuWidget(const UUserWidget* Widget) const;
	bool BelongsToThisGameInstance(const UUserWidget* Widget) const;

	FDelegateHandle PostLoadMapHandle;
};
