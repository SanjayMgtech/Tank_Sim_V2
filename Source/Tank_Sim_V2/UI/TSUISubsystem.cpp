#include "UI/TSUISubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Tank_Sim_V2.h"
#include "UI/TSLoginWidget.h"
#include "UI/TSSessionBrowserWidget.h"
#include "UObject/UObjectIterator.h"

UTSUISubsystem::UTSUISubsystem()
{
	// Levels on which the Login / Session Browser UI is legitimate. Everything else is gameplay.
	MenuMapNames = { FName(TEXT("MainMenu")) };

	// Fallback matching for menu widgets that are pure Blueprint (parented to UUserWidget rather than
	// to one of the classes below). Deliberately does NOT include "RoleSelection"/"TeamSelection" -
	// those are gameplay-map widgets and must survive the travel.
	MenuWidgetNameFragments = { TEXT("Login"), TEXT("SessionBrowser"), TEXT("SessionList"), TEXT("MainMenu"), TEXT("HostMenu") };

	MenuWidgetClasses = { UTSLoginWidget::StaticClass(), UTSSessionBrowserWidget::StaticClass() };
}

void UTSUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UTSUISubsystem::HandlePostLoadMap);
}

void UTSUISubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	PostLoadMapHandle.Reset();

	Super::Deinitialize();
}

void UTSUISubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	// PIE runs several GameInstances in one process; only react to our own world.
	if (!LoadedWorld || LoadedWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	FString MapName = LoadedWorld->GetMapName();
	MapName.RemoveFromStart(LoadedWorld->StreamingLevelsPrefix);

	if (IsMenuMap(MapName))
	{
		return;
	}

	const int32 Removed = RemoveMenuWidgets();
	UE_LOG(LogTankSim, Log, TEXT("UTSUISubsystem: entered gameplay map '%s' - removed %d menu widget(s)."), *MapName, Removed);
}

FString UTSUISubsystem::GetCurrentMapName() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return FString();
	}

	FString MapName = World->GetMapName();
	MapName.RemoveFromStart(World->StreamingLevelsPrefix);
	return MapName;
}

bool UTSUISubsystem::IsMenuMap(const FString& MapName) const
{
	for (const FName& MenuMap : MenuMapNames)
	{
		if (MapName.Equals(MenuMap.ToString(), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool UTSUISubsystem::IsCurrentMapMenuMap() const
{
	return IsMenuMap(GetCurrentMapName());
}

void UTSUISubsystem::RegisterMenuWidgetClass(TSubclassOf<UUserWidget> WidgetClass)
{
	if (WidgetClass)
	{
		MenuWidgetClasses.AddUnique(WidgetClass);
	}
}

bool UTSUISubsystem::BelongsToThisGameInstance(const UUserWidget* Widget) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return false;
	}

	if (Widget->GetWorld() == GameInstance->GetWorld())
	{
		return true;
	}

	// A widget left over from the previous map still points at the old (torn-down) world, so fall
	// back to the owning LocalPlayer - that object survives travel and is what actually makes the
	// stale widget "ours".
	const ULocalPlayer* LocalPlayer = Widget->GetOwningLocalPlayer();
	return LocalPlayer && LocalPlayer->GetGameInstance() == GameInstance;
}

bool UTSUISubsystem::IsMenuWidget(const UUserWidget* Widget) const
{
	for (const TSubclassOf<UUserWidget>& MenuClass : MenuWidgetClasses)
	{
		if (MenuClass && Widget->IsA(MenuClass))
		{
			return true;
		}
	}

	const FString ClassName = Widget->GetClass()->GetName();
	for (const FString& Fragment : MenuWidgetNameFragments)
	{
		if (!Fragment.IsEmpty() && ClassName.Contains(Fragment, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

int32 UTSUISubsystem::RemoveMenuWidgets()
{
	int32 RemovedCount = 0;

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			continue;
		}

		if (!BelongsToThisGameInstance(Widget) || !IsMenuWidget(Widget))
		{
			continue;
		}

		if (!Widget->IsInViewport() && Widget->GetParent() == nullptr)
		{
			continue;
		}

		UE_LOG(LogTankSim, Verbose, TEXT("UTSUISubsystem: removing menu widget '%s'."), *Widget->GetName());
		Widget->RemoveFromParent();
		++RemovedCount;
	}

	return RemovedCount;
}
