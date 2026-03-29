#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UToolMenu;
struct FToolMenuSection;

class FEpicUnrealMCPModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static inline FEpicUnrealMCPModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FEpicUnrealMCPModule>("UnrealMCP");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("UnrealMCP");
	}

private:
#if WITH_EDITOR
	void RegisterMenus();
	void AddAssetContextMenuEntry(FToolMenuSection& Section);
	void ExecuteExportSelectedTargetMeta();
	bool HasSelectedBlueprintAssets() const;
#endif
}; 
