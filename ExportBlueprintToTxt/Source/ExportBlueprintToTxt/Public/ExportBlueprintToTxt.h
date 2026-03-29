// Copyright sonygodx@gmail.com. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
//interface

#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "FExportBlueprintToTxtModule"

class FExportBlueprintToTxtCommands : public TCommands<FExportBlueprintToTxtCommands>
{
public:
    FExportBlueprintToTxtCommands()
        : TCommands<FExportBlueprintToTxtCommands>(
              "ExportBlueprintToTxt",
              LOCTEXT("ExportBlueprintToTxt", "Export Blueprint to TXT"),
              NAME_None,
              FAppStyle::GetAppStyleSetName()
          )
    {
    }

    virtual void RegisterCommands() override
    {
        UI_COMMAND(ExportBlueprintsCommand, "ExportAllAssets", "Export supported assets to .txt files", EUserInterfaceActionType::Button, FInputChord());
        UI_COMMAND(ExportSelectedBlueprintsCommand, "ExportSelectedAssets", "Export selected supported assets to .txt files", EUserInterfaceActionType::Button, FInputChord());
        UI_COMMAND(ExportBlueprintsInFolderCommand, "ExportAssetsInFolder", "Export supported assets in selected folder to .txt files", EUserInterfaceActionType::Button, FInputChord());
    }

    TSharedPtr<FUICommandInfo> ExportBlueprintsCommand;
    TSharedPtr<FUICommandInfo> ExportSelectedBlueprintsCommand;
    TSharedPtr<FUICommandInfo> ExportBlueprintsInFolderCommand;
};

class FExportBlueprintToTxtModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    void ExportBlueprintsToText();
    void ExportSelectedBlueprintsToText();
    void ExportBlueprintsInFolderToText();
    void ExportAssetsToText(const TArray<FAssetData>& Assets);
    void AddMenuExtension(FMenuBuilder& Builder);
    void AddToolbarExtension(FToolBarBuilder& Builder);

private:
    void ExportBlueprintsToTextInternal(const TArray<FAssetData>& Assets);
    void ExportBlueprintsInPathsToText(const TArray<FString>& SelectedPaths);

    void RegisterAssetContextMenu();
    TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
    TSharedRef<FExtender> OnExtendContentBrowserPathSelectionMenu(const TArray<FString>& SelectedPaths);
    TSharedPtr<FUICommandList> PluginCommands;
private:
    FDelegateHandle AssetExtenderDelegateHandle;
    FDelegateHandle PathExtenderDelegateHandle;
};

#undef LOCTEXT_NAMESPACE
