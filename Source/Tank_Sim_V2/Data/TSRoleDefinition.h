// Data Asset for role/permission configuration (Section 4). FTSPermissions (Core/TSTypes.h) is the
// authoritative, code-side permission matrix used for server validation; this asset exists for
// designer-facing data (display names, icons, HUD colour) that doesn't affect authority decisions.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/TSTypes.h"
#include "TSRoleDefinition.generated.h"

UCLASS(BlueprintType)
class UTSRoleDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Role")
	ETSCrewRole CrewRole = ETSCrewRole::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Role")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Role")
	FLinearColor HUDColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Role")
	TObjectPtr<UTexture2D> RoleIcon;
};
