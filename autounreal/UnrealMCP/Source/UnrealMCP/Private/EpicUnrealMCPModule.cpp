#include "EpicUnrealMCPModule.h"

#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "UnrealMCPSettings.h"
#include "Engine/Blueprint.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#define LOCTEXT_NAMESPACE "FEpicUnrealMCPModule"

#if WITH_EDITOR
namespace
{
bool IsBlueprintAssetData(const FAssetData& AssetData)
{
    return AssetData.IsInstanceOf(UBlueprint::StaticClass());
}

void GetSelectedBlueprintAssets(TArray<FAssetData>& OutBlueprintAssets)
{
    OutBlueprintAssets.Reset();

    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    TArray<FAssetData> SelectedAssets;
    ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

    for (const FAssetData& AssetData : SelectedAssets)
    {
        if (IsBlueprintAssetData(AssetData))
        {
            OutBlueprintAssets.Add(AssetData);
        }
    }
}

void ShowModuleNotification(const FText& Text, const SNotificationItem::ECompletionState State)
{
    FNotificationInfo Info(Text);
    Info.ExpireDuration = 6.0f;
    Info.bUseLargeFont = false;

    const TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
    if (NotificationItem.IsValid())
    {
        NotificationItem->SetCompletionState(State);
    }
}
}
#endif

void FEpicUnrealMCPModule::StartupModule()
{
	UE_LOG(LogTemp, Display, TEXT("Epic Unreal MCP Module has started"));

	const UUnrealMCPSettings* Settings = GetDefault<UUnrealMCPSettings>();
	if (Settings)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("UnrealMCP Settings: Neo4jEnabled=%s Uri=%s User=%s Database=%s"),
			Settings->bEnableNeo4j ? TEXT("true") : TEXT("false"),
			*Settings->Neo4jUri,
			*Settings->Neo4jUser,
			*Settings->Neo4jDatabase
		);
	}

#if WITH_EDITOR
    if (UToolMenus::IsToolMenuUIEnabled() && UToolMenus::TryGet())
    {
        RegisterMenus();
    }
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FEpicUnrealMCPModule::RegisterMenus));
#endif
}

void FEpicUnrealMCPModule::ShutdownModule()
{
#if WITH_EDITOR
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
#endif

	UE_LOG(LogTemp, Display, TEXT("Epic Unreal MCP Module has shut down"));
}

#if WITH_EDITOR
void FEpicUnrealMCPModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* AssetContextMenu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UBlueprint::StaticClass());
    if (!AssetContextMenu)
    {
        AssetContextMenu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu"));
    }
    if (!AssetContextMenu)
    {
        return;
    }

    FToolMenuSection& Section = AssetContextMenu->FindOrAddSection(TEXT("UnrealMCP"));
    Section.AddDynamicEntry(
        TEXT("UnrealMCP.ExportSelectedTargetMeta"),
        FNewToolMenuSectionDelegate::CreateRaw(this, &FEpicUnrealMCPModule::AddAssetContextMenuEntry));
}

void FEpicUnrealMCPModule::AddAssetContextMenuEntry(FToolMenuSection& Section)
{
    const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(Section);
    if (!Context)
    {
        return;
    }

    bool bHasBlueprintSelection = false;
    for (const FAssetData& AssetData : Context->SelectedAssets)
    {
        if (IsBlueprintAssetData(AssetData))
        {
            bHasBlueprintSelection = true;
            break;
        }
    }

    if (!bHasBlueprintSelection)
    {
        return;
    }

    Section.AddMenuEntry(
        TEXT("UnrealMCP.ExportSelectedTargetMeta"),
        LOCTEXT("ExportSelectedTargetMeta_Label", "Export Selected Target Meta"),
        LOCTEXT("ExportSelectedTargetMeta_Tooltip", "Export the selected Blueprint assets as .meta files with property values."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"),
        FUIAction(FExecuteAction::CreateRaw(this, &FEpicUnrealMCPModule::ExecuteExportSelectedTargetMeta)));
}

bool FEpicUnrealMCPModule::HasSelectedBlueprintAssets() const
{
    TArray<FAssetData> BlueprintAssets;
    GetSelectedBlueprintAssets(BlueprintAssets);
    return BlueprintAssets.Num() > 0;
}

void FEpicUnrealMCPModule::ExecuteExportSelectedTargetMeta()
{
    TArray<FAssetData> BlueprintAssets;
    GetSelectedBlueprintAssets(BlueprintAssets);
    if (BlueprintAssets.Num() == 0)
    {
        ShowModuleNotification(LOCTEXT("ExportSelectedTargetMeta_NoBlueprint", "No Blueprint asset is currently selected."), SNotificationItem::CS_Fail);
        return;
    }

    FEpicUnrealMCPBlueprintCommands BlueprintCommands;
    TArray<FString> ExportedPaths;
    TArray<FString> FailedBlueprints;

    TArray<TSharedPtr<FJsonValue>> MetaParts;
    MetaParts.Add(MakeShared<FJsonValueString>(TEXT("propertydeclarations")));
    MetaParts.Add(MakeShared<FJsonValueString>(TEXT("propertyvalues")));
    MetaParts.Add(MakeShared<FJsonValueString>(TEXT("events")));
    MetaParts.Add(MakeShared<FJsonValueString>(TEXT("functions")));
    MetaParts.Add(MakeShared<FJsonValueString>(TEXT("macros")));
    MetaParts.Add(MakeShared<FJsonValueString>(TEXT("interfaces")));
    MetaParts.Add(MakeShared<FJsonValueString>(TEXT("components")));
    MetaParts.Add(MakeShared<FJsonValueString>(TEXT("graphs")));

    for (const FAssetData& AssetData : BlueprintAssets)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
        if (!Blueprint)
        {
            FailedBlueprints.Add(AssetData.AssetName.ToString());
            continue;
        }

        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("target"), Blueprint->GetPathName());
        Params->SetArrayField(TEXT("parts"), MetaParts);
        Params->SetNumberField(TEXT("property_value_depth"), 1);
        Params->SetNumberField(TEXT("property_value_max_items"), 16);

        const TSharedPtr<FJsonObject> Result = BlueprintCommands.HandleCommand(TEXT("export_blueprint_meta"), Params);
        const bool bSuccess = Result.IsValid() && Result->HasTypedField<EJson::Boolean>(TEXT("success")) && Result->GetBoolField(TEXT("success"));
        if (!bSuccess)
        {
            FailedBlueprints.Add(Blueprint->GetName());
            UE_LOG(LogTemp, Error, TEXT("Failed to export meta for blueprint %s"), *Blueprint->GetPathName());
            continue;
        }

        FString OutputPath;
        Result->TryGetStringField(TEXT("output_path"), OutputPath);
        if (!OutputPath.IsEmpty())
        {
            ExportedPaths.Add(OutputPath);
            UE_LOG(LogTemp, Display, TEXT("Exported blueprint meta: %s"), *OutputPath);
        }
    }

    if (ExportedPaths.Num() > 0 && FailedBlueprints.Num() == 0)
    {
        const FString NotificationText = ExportedPaths.Num() == 1
            ? FString::Printf(TEXT("Meta exported: %s"), *ExportedPaths[0])
            : FString::Printf(TEXT("Exported %d Blueprint meta files. First file: %s"), ExportedPaths.Num(), *ExportedPaths[0]);
        ShowModuleNotification(FText::FromString(NotificationText), SNotificationItem::CS_Success);
        return;
    }

    const FString FailureText = FailedBlueprints.Num() > 0
        ? FString::Printf(TEXT("Meta export finished with failures. Failed: %s"), *FString::Join(FailedBlueprints, TEXT(", ")))
        : TEXT("Meta export did not produce any files.");
    ShowModuleNotification(FText::FromString(FailureText), SNotificationItem::CS_Fail);
}
#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FEpicUnrealMCPModule, UnrealMCP)
