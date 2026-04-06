// Copyright sonygodx@gmail.com. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "ContentBrowserDelegates.h"

class FUICommandList;
class FMenuBuilder;
class FToolBarBuilder;

// ─── Commands ────────────────────────────────────────────────────────────────

class FExportBpyCommands : public TCommands<FExportBpyCommands>
{
public:
	FExportBpyCommands()
		: TCommands<FExportBpyCommands>(
			TEXT("ExportBpy"),
			NSLOCTEXT("Contexts", "ExportBpy", "Export Blueprint to Python DSL"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> ExportAllToPy;
	TSharedPtr<FUICommandInfo> ExportSelectedToPy;
	TSharedPtr<FUICommandInfo> ExportSelectedFolderToPy;
	TSharedPtr<FUICommandInfo> CompileUpperPackage;
};

// ─── Module ───────────────────────────────────────────────────────────────────

class FExportBpyModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterAssetContextMenu();
	void AddMenuExtension(FMenuBuilder& Builder);

	TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(
		const TArray<FAssetData>& SelectedAssets);
	TSharedRef<FExtender> OnExtendContentBrowserPathSelectionMenu(
		const TArray<FString>& SelectedPaths);

	// Actions
	void ExportAllBlueprintsToPy();
	void ExportSelectedBlueprintsToPy();
	void ExportSelectedFolderToPy();
	void ExportBlueprintsInPathsToPy(const TArray<FString>& InPaths);
	void ExportAssetsToDirectory(const TArray<FAssetData>& Assets);
	void CompileUpperPackageFromDialog();
	bool CompileUpperPackage(const FString& SourceDir, FString& OutError) const;

	TSharedPtr<FUICommandList> PluginCommands;
	FDelegateHandle AssetExtenderDelegateHandle;
	FDelegateHandle PathExtenderDelegateHandle;
};
