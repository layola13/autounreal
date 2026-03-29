#include "UnrealMCPSettings.h"

UUnrealMCPSettings::UUnrealMCPSettings()
{
	CategoryName = TEXT("Plugins");
}

FName UUnrealMCPSettings::GetContainerName() const
{
	return TEXT("Project");
}

FName UUnrealMCPSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UUnrealMCPSettings::GetSectionName() const
{
	return TEXT("UnrealMCP");
}

