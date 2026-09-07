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

class UUserWidget;

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

private:
	void HandlePostLoadMap(UWorld* LoadedWorld);
	bool IsMenuWidget(const UUserWidget* Widget) const;
	bool BelongsToThisGameInstance(const UUserWidget* Widget) const;

	FDelegateHandle PostLoadMapHandle;
};
