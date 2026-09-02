#include "Core/TSGameInstance.h"

#include "Networking/TSSessionSubsystem.h"

UTSSessionSubsystem* UTSGameInstance::GetSessionSubsystem() const
{
	return GetSubsystem<UTSSessionSubsystem>();
}
