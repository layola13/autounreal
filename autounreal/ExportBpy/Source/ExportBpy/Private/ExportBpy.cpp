// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "ExportBpy.h"
#include "BPDirectExporter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditor.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "Interfaces/IPluginManager.h"
#include "IPythonScriptPlugin.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/Paths.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#define LOCTEXT_NAMESPACE "FExportBpyModule"

namespace
{
FString EscapePythonCommandString_ExportBpy(const FString& InValue)
{
	FString Escaped = InValue;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("'"), TEXT("\\'"));
	Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	return Escaped;
}

FString JoinPythonLog_ExportBpy(const TArray<FPythonLogOutputEntry>& Entries)
{
	FString Joined;
	for (const FPythonLogOutputEntry& Entry : Entries)
	{
		if (!Joined.IsEmpty())
		{
			Joined += TEXT("\n");
		}
		Joined += FString::Printf(TEXT("[%s] %s"), LexToString(Entry.Type), *Entry.Output);
	}
	return Joined;
}
}

// ─── RegisterCommands ─────────────────────────────────────────────────────────

void FExportBpyCommands::RegisterCommands()
{
	UI_COMMAND(ExportAllToPy,
		"Export All Blueprints to .bp.py",
		"Export all Blueprints in /Game as Python DSL scripts",
		EUserInterfaceActionType::Button,
		FInputChord());

	UI_COMMAND(ExportSelectedToPy,
		"Export Selected to .bp.py",
		"Export the selected Blueprints as Python DSL scripts",
		EUserInterfaceActionType::Button,
		FInputChord());

	UI_COMMAND(ExportSelectedFolderToPy,
		"Export Selected Folder to .bp.py",
		"Export all Blueprints in the selected Content Browser folder as Python DSL scripts",
		EUserInterfaceActionType::Button,
		FInputChord());

	UI_COMMAND(CompileUpperPackage,
		"Compile Upper Package...",
		"Compile an UpperBlueprints package and import it into a Blueprint asset",
		EUserInterfaceActionType::Button,
		FInputChord());
}

// ─── StartupModule ────────────────────────────────────────────────────────────

void FExportBpyModule::StartupModule()
{
	FExportBpyCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FExportBpyCommands::Get().ExportAllToPy,
		FExecuteAction::CreateRaw(this, &FExportBpyModule::ExportAllBlueprintsToPy),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FExportBpyCommands::Get().ExportSelectedToPy,
		FExecuteAction::CreateRaw(this, &FExportBpyModule::ExportSelectedBlueprintsToPy),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FExportBpyCommands::Get().ExportSelectedFolderToPy,
		FExecuteAction::CreateRaw(this, &FExportBpyModule::ExportSelectedFolderToPy),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FExportBpyCommands::Get().CompileUpperPackage,
		FExecuteAction::CreateRaw(this, &FExportBpyModule::CompileUpperPackageFromDialog),
		FCanExecuteAction());

	// Add to Level Editor File menu
	FLevelEditorModule& LevelEditorModule =
		FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension(
			"FileProject",
			EExtensionHook::After,
			PluginCommands,
			FMenuExtensionDelegate::CreateRaw(this, &FExportBpyModule::AddMenuExtension));
		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}

	RegisterAssetContextMenu();
}

// ─── ShutdownModule ───────────────────────────────────────────────────────────

void FExportBpyModule::ShutdownModule()
{
	FExportBpyCommands::Unregister();

	if (FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
	{
		FContentBrowserModule& CBModule =
			FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");

		TArray<FContentBrowserMenuExtender_SelectedAssets>& AssetExtenders =
			CBModule.GetAllAssetViewContextMenuExtenders();
		AssetExtenders.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& D)
		{
			return D.GetHandle() == AssetExtenderDelegateHandle;
		});

		TArray<FContentBrowserMenuExtender_SelectedPaths>& PathExtenders =
			CBModule.GetAllPathViewContextMenuExtenders();
		PathExtenders.RemoveAll([this](const FContentBrowserMenuExtender_SelectedPaths& D)
		{
			return D.GetHandle() == PathExtenderDelegateHandle;
		});
	}
}

// ─── AddMenuExtension ─────────────────────────────────────────────────────────

void FExportBpyModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.AddMenuEntry(FExportBpyCommands::Get().ExportAllToPy);
	Builder.AddMenuEntry(FExportBpyCommands::Get().ExportSelectedToPy);
	Builder.AddMenuEntry(FExportBpyCommands::Get().ExportSelectedFolderToPy);
	Builder.AddMenuEntry(FExportBpyCommands::Get().CompileUpperPackage);
}

// ─── RegisterAssetContextMenu ─────────────────────────────────────────────────

void FExportBpyModule::RegisterAssetContextMenu()
{
	FContentBrowserModule& CBModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TArray<FContentBrowserMenuExtender_SelectedAssets>& AssetExtenders =
		CBModule.GetAllAssetViewContextMenuExtenders();
	auto AssetDelegate = FContentBrowserMenuExtender_SelectedAssets::CreateRaw(
		this, &FExportBpyModule::OnExtendContentBrowserAssetSelectionMenu);
	AssetExtenderDelegateHandle = AssetDelegate.GetHandle();
	AssetExtenders.Add(AssetDelegate);

	TArray<FContentBrowserMenuExtender_SelectedPaths>& PathExtenders =
		CBModule.GetAllPathViewContextMenuExtenders();
	auto PathDelegate = FContentBrowserMenuExtender_SelectedPaths::CreateRaw(
		this, &FExportBpyModule::OnExtendContentBrowserPathSelectionMenu);
	PathExtenderDelegateHandle = PathDelegate.GetHandle();
	PathExtenders.Add(PathDelegate);
}

// ─── OnExtendContentBrowserAssetSelectionMenu ─────────────────────────────────

TSharedRef<FExtender> FExportBpyModule::OnExtendContentBrowserAssetSelectionMenu(
	const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender(new FExtender());

	Extender->AddMenuExtension(
		"AssetContextReferences",
		EExtensionHook::After,
		PluginCommands,
		FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ExportSelectedToPy", "Export Selected to .bp.py"),
				LOCTEXT("ExportSelectedToPyTooltip", "Export selected Blueprints as Python DSL scripts"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(
					this, &FExportBpyModule::ExportSelectedBlueprintsToPy)));
		}));

	return Extender;
}

// ─── OnExtendContentBrowserPathSelectionMenu ──────────────────────────────────

TSharedRef<FExtender> FExportBpyModule::OnExtendContentBrowserPathSelectionMenu(
	const TArray<FString>& SelectedPaths)
{
	TSharedRef<FExtender> Extender(new FExtender());
	const TArray<FString> CapturedPaths = SelectedPaths;

	Extender->AddMenuExtension(
		"PathContextBulkOperations",
		EExtensionHook::After,
		PluginCommands,
		FMenuExtensionDelegate::CreateLambda([this, CapturedPaths](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ExportFolderToPy", "Export Folder to .bp.py"),
				LOCTEXT("ExportFolderToPyTooltip", "Export all Blueprints in this folder as Python DSL scripts"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, CapturedPaths]()
				{
					ExportBlueprintsInPathsToPy(CapturedPaths);
				})));
		}));

	return Extender;
}

// ─── ExportAllBlueprintsToPy ──────────────────────────────────────────────────

void FExportBpyModule::ExportAllBlueprintsToPy()
{
	FAssetRegistryModule& ARModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARModule.Get();
	if (AR.IsLoadingAssets())
		AR.WaitForCompletion();

	FARFilter Filter;
	Filter.PackagePaths.Add("/Game");
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());

	TArray<FAssetData> AllAssets;
	AR.GetAssets(Filter, AllAssets);

	ExportAssetsToDirectory(AllAssets);
}

// ─── ExportSelectedBlueprintsToPy ─────────────────────────────────────────────

void FExportBpyModule::ExportSelectedBlueprintsToPy()
{
	FContentBrowserModule& CBModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	CBModule.Get().GetSelectedAssets(SelectedAssets);
	ExportAssetsToDirectory(SelectedAssets);
}

// ─── ExportSelectedFolderToPy ─────────────────────────────────────────────────

void FExportBpyModule::ExportSelectedFolderToPy()
{
	FContentBrowserModule& CBModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FString> SelectedPaths;
	CBModule.Get().GetSelectedFolders(SelectedPaths);
	ExportBlueprintsInPathsToPy(SelectedPaths);
}

// ─── ExportBlueprintsInPathsToPy ──────────────────────────────────────────────

void FExportBpyModule::ExportBlueprintsInPathsToPy(const TArray<FString>& InPaths)
{
	if (InPaths.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoFolderSelected", "Please select at least one folder in the Content Browser."));
		return;
	}

	FAssetRegistryModule& ARModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARModule.Get();
	if (AR.IsLoadingAssets())
		AR.WaitForCompletion();

	TArray<FAssetData> FolderAssets;
	for (const FString& InPath : InPaths)
	{
		FString ProcessedPath = InPath.Replace(TEXT("/All/Game/"), TEXT("/Game/"));

		FARFilter Filter;
		Filter.PackagePaths.Add(*ProcessedPath);
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());

		TArray<FAssetData> AssetsInPath;
		AR.GetAssets(Filter, AssetsInPath);
		FolderAssets.Append(AssetsInPath);
	}

	ExportAssetsToDirectory(FolderAssets);
}

// ─── ExportAssetsToDirectory ──────────────────────────────────────────────────

void FExportBpyModule::ExportAssetsToDirectory(const TArray<FAssetData>& Assets)
{
	if (Assets.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoAssets", "No Blueprint assets found to export."));
		return;
	}

	FString OutputDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("ExportedBlueprints"), TEXT("bpy"));
	IFileManager::Get().MakeDirectory(*OutputDir, true);

	FScopedSlowTask SlowTask(Assets.Num(), LOCTEXT("ExportingBlueprints", "Exporting Blueprints..."));
	SlowTask.MakeDialog();

	int32 Exported = 0;
	int32 Failed   = 0;
	for (const FAssetData& Asset : Assets)
	{
		SlowTask.EnterProgressFrame(1, FText::Format(
			LOCTEXT("ExportingBlueprint", "Exporting: {0}"),
			FText::FromName(Asset.AssetName)));

		FName AssetClass = Asset.AssetClassPath.GetAssetName();
		if (AssetClass != FName("Blueprint") && AssetClass != FName("AnimBlueprint"))
			continue;

		FString AssetPath = Asset.GetSoftObjectPath().ToString();
		if (AssetPath.EndsWith(TEXT("_C")))
			AssetPath = AssetPath.LeftChop(2);

		FString OutError;
		if (UBPDirectExporter::ExportBlueprintToPy(AssetPath, OutputDir, OutError))
			Exported++;
		else
		{
			Failed++;
			UE_LOG(LogTemp, Warning, TEXT("ExportBpy: failed %s: %s"), *AssetPath, *OutError);
		}
	}

	FNotificationInfo Info(FText::Format(
		LOCTEXT("ExportComplete", "Export complete. Succeeded: {0}  Failed: {1}"),
		FText::AsNumber(Exported), FText::AsNumber(Failed)));
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;
	FSlateNotificationManager::Get().AddNotification(Info);
}

// ─── CompileUpperPackageFromDialog ───────────────────────────────────────────

void FExportBpyModule::CompileUpperPackageFromDialog()
{
	const FString DefaultRoot = FPaths::Combine(FPaths::ProjectDir(), TEXT("UpperBlueprints"));
	IFileManager::Get().MakeDirectory(*DefaultRoot, true);

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoDesktopPlatform", "DesktopPlatform is unavailable."));
		return;
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	FString SelectedFolder;
	const bool bSelected = DesktopPlatform->OpenDirectoryDialog(
		ParentWindowHandle,
		LOCTEXT("ChooseUpperPackage", "Choose an Upper Package").ToString(),
		DefaultRoot,
		SelectedFolder);

	if (!bSelected || SelectedFolder.IsEmpty())
	{
		return;
	}

	const FString UpperFile = FPaths::Combine(SelectedFolder, TEXT("__upper__.py"));
	if (!FPaths::FileExists(UpperFile))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				LOCTEXT("MissingUpperFile", "The selected folder does not contain __upper__.py:\n{0}"),
				FText::FromString(SelectedFolder)));
		return;
	}

	FString OutError;
	if (!CompileUpperPackage(SelectedFolder, OutError))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				LOCTEXT("CompileUpperFailed", "Compile Upper Package failed:\n{0}"),
				FText::FromString(OutError)));
		return;
	}

	FNotificationInfo Info(FText::Format(
		LOCTEXT("CompileUpperSucceeded", "Upper package compiled: {0}"),
		FText::FromString(FPaths::GetCleanFilename(SelectedFolder))));
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;
	FSlateNotificationManager::Get().AddNotification(Info);
}

bool FExportBpyModule::CompileUpperPackage(const FString& SourceDir, FString& OutError) const
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ExportBpy"));
	if (!Plugin.IsValid())
	{
		OutError = TEXT("Cannot locate ExportBpy plugin directory.");
		return false;
	}

	IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
	if (!PythonScriptPlugin)
	{
		OutError = TEXT("PythonScriptPlugin is not loaded. Enable the Python Script Plugin and restart the editor.");
		return false;
	}

	PythonScriptPlugin->ForceEnablePythonAtRuntime();
	if (!PythonScriptPlugin->IsPythonInitialized())
	{
		OutError = TEXT("Python is not initialized yet.");
		return false;
	}

	const FString PluginPythonDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Content"), TEXT("Python"));
	const FString EscapedPluginPythonDir = EscapePythonCommandString_ExportBpy(PluginPythonDir);
	const FString EscapedSourceDir = EscapePythonCommandString_ExportBpy(SourceDir);

	const FString PythonScript = FString::Printf(
		TEXT("import sys\n")
		TEXT("plugin_python_dir = '%s'\n")
		TEXT("source_dir = '%s'\n")
		TEXT("if plugin_python_dir not in sys.path:\n")
		TEXT("    sys.path.insert(0, plugin_python_dir)\n")
		TEXT("from bpy_compile.api import compile_and_import\n")
		TEXT("ok, err = compile_and_import(source_dir)\n")
		TEXT("if not ok:\n")
		TEXT("    raise RuntimeError(err or 'compile_and_import failed')\n"),
		*EscapedPluginPythonDir,
		*EscapedSourceDir);

	FPythonCommandEx PythonCommand;
	PythonCommand.Command = PythonScript;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Private;

	const bool bExecuted = PythonScriptPlugin->ExecPythonCommandEx(PythonCommand);
	if (bExecuted)
	{
		return true;
	}

	OutError = PythonCommand.CommandResult;
	if (OutError.IsEmpty())
	{
		OutError = JoinPythonLog_ExportBpy(PythonCommand.LogOutput);
	}
	if (OutError.IsEmpty())
	{
		OutError = TEXT("Unknown Python execution failure.");
	}
	return false;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FExportBpyModule, ExportBpy)
