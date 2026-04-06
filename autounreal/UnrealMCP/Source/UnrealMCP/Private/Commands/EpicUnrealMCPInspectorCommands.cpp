#include "Commands/EpicUnrealMCPInspectorCommands.h"

#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Algo/Unique.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DataTable.h"
#include "Engine/Selection.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManager.h"
#include "IContentBrowserSingleton.h"
#include "JsonObjectConverter.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
#include "Modules/ModuleManager.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
FString TrimmedOrEmpty(const FString& InValue)
{
    FString Value = InValue;
    Value.TrimStartAndEndInline();
    return Value;
}

FString NormalizeAssetReference(const FString& AssetPath)
{
    FString Normalized = TrimmedOrEmpty(AssetPath);
    if (Normalized.IsEmpty())
    {
        return Normalized;
    }

    if (Normalized.StartsWith(TEXT("/")) && !Normalized.Contains(TEXT(".")))
    {
        const FString AssetName = FPackageName::GetLongPackageAssetName(Normalized);
        if (!AssetName.IsEmpty())
        {
            return FString::Printf(TEXT("%s.%s"), *Normalized, *AssetName);
        }
    }

    return Normalized;
}

FString ToPackagePath(const FString& AssetPath)
{
    const FString Normalized = NormalizeAssetReference(AssetPath);
    const int32 DotIndex = Normalized.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (DotIndex != INDEX_NONE)
    {
        return Normalized.Left(DotIndex);
    }

    return Normalized;
}

UObject* LoadAssetByReference(const FString& AssetPath)
{
    const FString Normalized = NormalizeAssetReference(AssetPath);
    if (Normalized.IsEmpty())
    {
        return nullptr;
    }

    if (UObject* DirectObject = LoadObject<UObject>(nullptr, *Normalized))
    {
        return DirectObject;
    }

    const FString PackagePath = ToPackagePath(Normalized);
    if (!PackagePath.IsEmpty() && UEditorAssetLibrary::DoesAssetExist(PackagePath))
    {
        return UEditorAssetLibrary::LoadAsset(PackagePath);
    }

    return nullptr;
}

TArray<FString> GetStringArrayField(const TSharedPtr<FJsonObject>& Params, const TCHAR* FieldName)
{
    TArray<FString> Values;
    if (!Params.IsValid())
    {
        return Values;
    }

    const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
    if (!Params->TryGetArrayField(FieldName, JsonArray))
    {
        return Values;
    }

    for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
    {
        FString StringValue;
        if (Value.IsValid() && Value->TryGetString(StringValue))
        {
            StringValue = TrimmedOrEmpty(StringValue);
            if (!StringValue.IsEmpty())
            {
                Values.Add(StringValue);
            }
        }
    }

    return Values;
}

TArray<FString> TokenizeQuery(const FString& Query)
{
    TArray<FString> Terms;
    Query.ToLower().ParseIntoArrayWS(Terms);
    return Terms;
}

int32 ScoreQueryTerms(const TArray<FString>& Terms, const FString& Name, const FString& Path, const FString& TypeName)
{
    if (Terms.Num() == 0)
    {
        return 0;
    }

    const FString NameLower = Name.ToLower();
    const FString PathLower = Path.ToLower();
    const FString TypeLower = TypeName.ToLower();

    int32 Score = 0;
    for (const FString& Term : Terms)
    {
        if (Term.IsEmpty())
        {
            continue;
        }

        if (NameLower.Equals(Term, ESearchCase::CaseSensitive))
        {
            Score += 120;
        }
        else if (NameLower.Contains(Term))
        {
            Score += 70;
        }

        if (PathLower.Contains(Term))
        {
            Score += 35;
        }

        if (TypeLower.Contains(Term))
        {
            Score += 20;
        }
    }

    return Score;
}

TSet<FString> ParsePipeSeparatedFlags(const FString& RawFlags)
{
    TSet<FString> Flags;
    TArray<FString> Parts;
    RawFlags.ParseIntoArray(Parts, TEXT("|"), true);
    for (FString Part : Parts)
    {
        Part = TrimmedOrEmpty(Part);
        if (!Part.IsEmpty())
        {
            Flags.Add(Part);
        }
    }
    return Flags;
}

TSharedPtr<FJsonObject> RunAnalyzeBlueprintGraphFast_Inspector(
    const TSharedPtr<FEpicUnrealMCPBlueprintCommands>& BlueprintCommands,
    const FString& BlueprintPath,
    const FString& GraphName)
{
    if (!BlueprintCommands.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint command handler is not available"));
    }

    TSharedPtr<FJsonObject> ForwardParams = MakeShared<FJsonObject>();
    ForwardParams->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    ForwardParams->SetStringField(TEXT("graph_name"), GraphName);
    ForwardParams->SetBoolField(TEXT("include_pin_connections"), true);
    return BlueprintCommands->HandleCommand(TEXT("analyze_blueprint_graph_fast"), ForwardParams);
}

TArray<TSharedPtr<FJsonValue>> MakeStringValueArray_Inspector(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> JsonValues;
    for (const FString& Value : Values)
    {
        JsonValues.Add(MakeShared<FJsonValueString>(Value));
    }
    return JsonValues;
}

TArray<FString> GetDefaultProjectSearchRoots_Inspector()
{
    return {
        FPaths::GameSourceDir(),
        FPaths::ProjectPluginsDir(),
        FPaths::ProjectConfigDir(),
        FPaths::Combine(FPaths::ProjectDir(), TEXT("docs")),
        FPaths::Combine(FPaths::ProjectDir(), TEXT("Content/Python"))
    };
}

TArray<FString> GetDefaultCodeSearchRoots_Inspector(const bool bIncludeEngineSource)
{
    TArray<FString> Roots = {
        FPaths::GameSourceDir(),
        FPaths::ProjectPluginsDir()
    };

    if (bIncludeEngineSource)
    {
        Roots.Add(FPaths::Combine(FPaths::EngineDir(), TEXT("Source")));
    }

    return Roots;
}

void AddUniqueExistingRoot_Inspector(TArray<FString>& Roots, const FString& Root)
{
    if (!Root.IsEmpty() && IFileManager::Get().DirectoryExists(*Root))
    {
        Roots.AddUnique(FPaths::ConvertRelativePathToFull(Root));
    }
}

TArray<FString> ExpandGlobPatterns_Inspector(const FString& RawGlob)
{
    TArray<FString> Patterns;
    const FString TrimmedGlob = TrimmedOrEmpty(RawGlob);
    if (TrimmedGlob.IsEmpty())
    {
        return Patterns;
    }

    const int32 OpenBraceIndex = TrimmedGlob.Find(TEXT("{"));
    const int32 CloseBraceIndex = TrimmedGlob.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenBraceIndex + 1);
    if (OpenBraceIndex != INDEX_NONE && CloseBraceIndex != INDEX_NONE && CloseBraceIndex > OpenBraceIndex)
    {
        const FString Prefix = TrimmedGlob.Left(OpenBraceIndex);
        const FString Suffix = TrimmedGlob.Mid(CloseBraceIndex + 1);
        const FString Inner = TrimmedGlob.Mid(OpenBraceIndex + 1, CloseBraceIndex - OpenBraceIndex - 1);

        TArray<FString> Variants;
        Inner.ParseIntoArray(Variants, TEXT(","), true);
        for (FString Variant : Variants)
        {
            Variant = TrimmedOrEmpty(Variant);
            if (!Variant.IsEmpty())
            {
                Patterns.Add(Prefix + Variant + Suffix);
            }
        }
    }

    if (Patterns.Num() == 0)
    {
        Patterns.Add(TrimmedGlob);
    }

    return Patterns;
}

bool MatchesGlobFilter_Inspector(const FString& FilePath, const FString& RawGlob)
{
    const TArray<FString> Patterns = ExpandGlobPatterns_Inspector(RawGlob);
    if (Patterns.Num() == 0)
    {
        return true;
    }

    const FString FileName = FPaths::GetCleanFilename(FilePath);
    for (const FString& Pattern : Patterns)
    {
        if (FilePath.MatchesWildcard(Pattern) || FileName.MatchesWildcard(Pattern))
        {
            return true;
        }
    }

    return false;
}

bool MatchesTypeFilter_Inspector(const FString& FilePath, const FString& RawType)
{
    FString Type = TrimmedOrEmpty(RawType).ToLower();
    if (Type.IsEmpty())
    {
        return true;
    }

    const FString Extension = FPaths::GetExtension(FilePath, true).ToLower();
    TArray<FString> AllowedExtensions;

    if (Type == TEXT("cpp") || Type == TEXT("c++") || Type == TEXT("cc"))
    {
        AllowedExtensions = { TEXT(".h"), TEXT(".hpp"), TEXT(".hh"), TEXT(".inl"), TEXT(".cpp"), TEXT(".c"), TEXT(".cc"), TEXT(".cxx") };
    }
    else if (Type == TEXT("h") || Type == TEXT("header"))
    {
        AllowedExtensions = { TEXT(".h"), TEXT(".hpp"), TEXT(".hh"), TEXT(".inl") };
    }
    else if (Type == TEXT("py"))
    {
        AllowedExtensions = { TEXT(".py") };
    }
    else if (Type == TEXT("ini"))
    {
        AllowedExtensions = { TEXT(".ini") };
    }
    else if (Type == TEXT("json"))
    {
        AllowedExtensions = { TEXT(".json") };
    }
    else if (Type == TEXT("md"))
    {
        AllowedExtensions = { TEXT(".md") };
    }
    else if (Type == TEXT("cs"))
    {
        AllowedExtensions = { TEXT(".cs") };
    }
    else if (Type == TEXT("txt") || Type == TEXT("log"))
    {
        AllowedExtensions = { TEXT(".txt"), TEXT(".log") };
    }
    else
    {
        if (!Type.StartsWith(TEXT(".")))
        {
            Type = TEXT(".") + Type;
        }
        AllowedExtensions = { Type };
    }

    return AllowedExtensions.Contains(Extension);
}

bool IsSearchableTextFile_Inspector(const FString& FilePath)
{
    static const TSet<FString> AllowedExtensions = {
        TEXT(".h"), TEXT(".hpp"), TEXT(".hh"), TEXT(".inl"),
        TEXT(".cpp"), TEXT(".c"), TEXT(".cc"), TEXT(".cxx"),
        TEXT(".cs"), TEXT(".py"), TEXT(".ini"), TEXT(".json"),
        TEXT(".md"), TEXT(".txt"), TEXT(".log"), TEXT(".uplugin"),
        TEXT(".uproject"), TEXT(".usf"), TEXT(".ush"), TEXT(".xml"),
        TEXT(".yaml"), TEXT(".yml"), TEXT(".toml"), TEXT(".cfg")
    };

    return AllowedExtensions.Contains(FPaths::GetExtension(FilePath, true).ToLower());
}

bool ShouldTreatAsRegex_Inspector(const FString& Pattern)
{
    return Pattern.Contains(TEXT("\\")) ||
        Pattern.Contains(TEXT(".*")) ||
        Pattern.Contains(TEXT("[")) ||
        Pattern.Contains(TEXT("|")) ||
        Pattern.Contains(TEXT("^")) ||
        Pattern.Contains(TEXT("$"));
}

bool TextMatchesPattern_Inspector(const FString& Text, const FString& Pattern, const bool bIgnoreCase)
{
    if (Pattern.IsEmpty())
    {
        return false;
    }

    if (!ShouldTreatAsRegex_Inspector(Pattern))
    {
        return Text.Contains(Pattern, bIgnoreCase ? ESearchCase::IgnoreCase : ESearchCase::CaseSensitive);
    }

    const FString EffectivePattern = bIgnoreCase ? FString(TEXT("(?i)")) + Pattern : Pattern;
    const FRegexPattern RegexPattern(EffectivePattern);
    FRegexMatcher Matcher(RegexPattern, Text);
    return Matcher.FindNext();
}

bool FindFirstPatternMatch_Inspector(
    const FString& Text,
    const FString& Pattern,
    const bool bIgnoreCase,
    int32& OutMatchBegin,
    int32& OutMatchEnd)
{
    OutMatchBegin = INDEX_NONE;
    OutMatchEnd = INDEX_NONE;

    if (Pattern.IsEmpty())
    {
        return false;
    }

    if (!ShouldTreatAsRegex_Inspector(Pattern))
    {
        const ESearchCase::Type SearchCase = bIgnoreCase ? ESearchCase::IgnoreCase : ESearchCase::CaseSensitive;
        const int32 BeginIndex = Text.Find(Pattern, SearchCase);
        if (BeginIndex == INDEX_NONE)
        {
            return false;
        }

        OutMatchBegin = BeginIndex;
        OutMatchEnd = BeginIndex + Pattern.Len();
        return true;
    }

    const FString EffectivePattern = bIgnoreCase ? FString(TEXT("(?i)")) + Pattern : Pattern;
    const FRegexPattern RegexPattern(EffectivePattern);
    FRegexMatcher Matcher(RegexPattern, Text);
    if (!Matcher.FindNext())
    {
        return false;
    }

    OutMatchBegin = Matcher.GetMatchBeginning();
    OutMatchEnd = Matcher.GetMatchEnding();
    return OutMatchBegin != INDEX_NONE && OutMatchEnd != INDEX_NONE;
}

FString ResolveSearchPath_Inspector(const FString& RequestedPath, const bool bSearchEngine, const bool bDefaultToEngineSource)
{
    const FString TrimmedPath = TrimmedOrEmpty(RequestedPath);
    if (!TrimmedPath.IsEmpty())
    {
        if (FPaths::IsRelative(TrimmedPath))
        {
            const FString BaseRoot = bSearchEngine ? FPaths::EngineDir() : FPaths::ProjectDir();
            return FPaths::ConvertRelativePathToFull(BaseRoot, TrimmedPath);
        }

        return FPaths::ConvertRelativePathToFull(TrimmedPath);
    }

    if (bSearchEngine || bDefaultToEngineSource)
    {
        return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), TEXT("Source")));
    }

    return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
}

void CollectFilesForSearch_Inspector(const FString& SearchPath, TArray<FString>& OutFiles)
{
    OutFiles.Reset();

    const FString FullSearchPath = FPaths::ConvertRelativePathToFull(SearchPath);
    if (IFileManager::Get().FileExists(*FullSearchPath))
    {
        OutFiles.Add(FullSearchPath);
        return;
    }

    if (!IFileManager::Get().DirectoryExists(*FullSearchPath))
    {
        return;
    }

    IFileManager::Get().FindFilesRecursive(OutFiles, *FullSearchPath, TEXT("*.*"), true, false, false);
}

TArray<TSharedPtr<FJsonValue>> BuildLineSlice_Inspector(const TArray<FString>& Lines, const int32 StartIndexInclusive, const int32 EndIndexExclusive)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (int32 Index = FMath::Max(0, StartIndexInclusive); Index < FMath::Min(EndIndexExclusive, Lines.Num()); ++Index)
    {
        Values.Add(MakeShared<FJsonValueString>(Lines[Index]));
    }
    return Values;
}

int32 CountNewlinesBeforeIndex_Inspector(const FString& Text, const int32 Index)
{
    const int32 ClampedIndex = FMath::Clamp(Index, 0, Text.Len());
    int32 LineNumber = 1;
    for (int32 CharIndex = 0; CharIndex < ClampedIndex; ++CharIndex)
    {
        if (Text[CharIndex] == TEXT('\n'))
        {
            ++LineNumber;
        }
    }
    return LineNumber;
}

void CollectAssetDependencyNames_Inspector(
    const FAssetData& AssetData,
    TArray<TSharedPtr<FJsonValue>>& OutDependencies,
    TArray<TSharedPtr<FJsonValue>>& OutReferencers)
{
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

    TArray<FName> DependencyNames;
    AssetRegistryModule.Get().GetDependencies(AssetData.PackageName, DependencyNames);
    for (int32 Index = 0; Index < FMath::Min(DependencyNames.Num(), 10); ++Index)
    {
        OutDependencies.Add(MakeShared<FJsonValueString>(DependencyNames[Index].ToString()));
    }

    TArray<FName> ReferencerNames;
    AssetRegistryModule.Get().GetReferencers(AssetData.PackageName, ReferencerNames);
    for (int32 Index = 0; Index < FMath::Min(ReferencerNames.Num(), 10); ++Index)
    {
        OutReferencers.Add(MakeShared<FJsonValueString>(ReferencerNames[Index].ToString()));
    }
}
}

FEpicUnrealMCPInspectorCommands::FEpicUnrealMCPInspectorCommands(
    const TSharedPtr<FEpicUnrealMCPEditorCommands>& InEditorCommands,
    const TSharedPtr<FEpicUnrealMCPBlueprintCommands>& InBlueprintCommands)
    : EditorCommands(InEditorCommands)
    , BlueprintCommands(InBlueprintCommands)
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("get_headless_status"))
    {
        return HandleGetHeadlessStatus(Params);
    }
    if (CommandType == TEXT("launch_unreal_project"))
    {
        return HandleLaunchUnrealProject(Params);
    }
    if (CommandType == TEXT("shutdown_headless"))
    {
        return HandleShutdownHeadless(Params);
    }
    if (CommandType == TEXT("get_unreal_context"))
    {
        return HandleGetUnrealContext(Params);
    }
    if (CommandType == TEXT("query_unreal_project_assets"))
    {
        return HandleQueryUnrealProjectAssets(Params);
    }
    if (CommandType == TEXT("quicksearch"))
    {
        return HandleQuicksearch(Params);
    }
    if (CommandType == TEXT("grep"))
    {
        return HandleGrep(Params);
    }
    if (CommandType == TEXT("get_code_examples"))
    {
        return HandleGetCodeExamples(Params);
    }
    if (CommandType == TEXT("get_asset_meta"))
    {
        return HandleGetAssetMeta(Params);
    }
    if (CommandType == TEXT("get_asset_graph"))
    {
        return HandleGetAssetGraph(Params);
    }
    if (CommandType == TEXT("get_asset_structs"))
    {
        return HandleGetAssetStructs(Params);
    }
    if (CommandType == TEXT("get_blueprint_material_properties"))
    {
        return HandleGetBlueprintMaterialProperties(Params);
    }
    if (CommandType == TEXT("review_blueprint"))
    {
        return HandleReviewBlueprint(Params);
    }
    if (CommandType == TEXT("create_and_validate_blueprint_plan"))
    {
        return HandleCreateAndValidateBlueprintPlan(Params);
    }
    if (CommandType == TEXT("get_text_file_contents"))
    {
        return HandleGetTextFileContents(Params);
    }
    if (CommandType == TEXT("get_available_actors_in_level"))
    {
        return HandleGetAvailableActorsInLevel(Params);
    }
    if (CommandType == TEXT("get_enums"))
    {
        return HandleGetEnums(Params);
    }
    if (CommandType == TEXT("get_gameplay_tags"))
    {
        return HandleGetGameplayTags(Params);
    }
    if (CommandType == TEXT("read_datatable_keys"))
    {
        return HandleReadDatatableKeys(Params);
    }
    if (CommandType == TEXT("read_datatable_values"))
    {
        return HandleReadDatatableValues(Params);
    }
    if (CommandType == TEXT("get_recent_generated_images"))
    {
        return HandleGetRecentGeneratedImages(Params);
    }
    if (CommandType == TEXT("fetch_animation_skill"))
    {
        return HandleFetchAnimationSkill(Params);
    }
    if (CommandType == TEXT("create_or_edit_plan"))
    {
        return HandleCreateOrEditPlan(Params);
    }
    if (CommandType == TEXT("fetch_gas_best_practices"))
    {
        return HandleFetchGasBestPractices(Params);
    }
    if (CommandType == TEXT("fetch_ui_best_practices"))
    {
        return HandleFetchUiBestPractices(Params);
    }
    if (CommandType == TEXT("import_AssetCreation_understanding") ||
        CommandType == TEXT("import_AssetRegistry_understanding") ||
        CommandType == TEXT("import_AssetType_Level_understanding") ||
        CommandType == TEXT("import_AssetValidation_understanding") ||
        CommandType == TEXT("import_Color_understanding") ||
        CommandType == TEXT("import_FileSystem_understanding") ||
        CommandType == TEXT("import_Logs_understanding") ||
        CommandType == TEXT("import_Subsystems_understanding"))
    {
        return HandleImportUnderstanding(CommandType, Params);
    }
    if (CommandType == TEXT("inspect_current_level"))
    {
        return HandleInspectCurrentLevel(Params);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown inspector command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetHeadlessStatus(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("status"), TEXT("editor_connected"));
    Result->SetBoolField(TEXT("editor_connected"), true);
    Result->SetBoolField(TEXT("editor_running"), true);
    Result->SetStringField(TEXT("message"), TEXT("Unreal editor is running with UnrealMCP loaded"));
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleLaunchUnrealProject(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("status"), TEXT("editor_connected"));
    Result->SetStringField(TEXT("message"), TEXT("Unreal editor is already running"));
    return Result;
}

namespace
{
TSharedPtr<FJsonObject> CreateKnowledgeResponse_Inspector(
    const FString& Category,
    const FString& Summary,
    const TArray<FString>& Lines)
{
    TArray<TSharedPtr<FJsonValue>> Content;
    for (const FString& Line : Lines)
    {
        Content.Add(MakeShared<FJsonValueString>(Line));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("category"), Category);
    Result->SetStringField(TEXT("summary"), Summary);
    Result->SetArrayField(TEXT("content"), Content);
    Result->SetArrayField(TEXT("best_practices"), Content);
    Result->SetNumberField(TEXT("count"), Content.Num());
    return Result;
}

FString SanitizePlanName_Inspector(const FString& RawPlanName)
{
    FString Sanitized;
    bool bLastWasUnderscore = false;

    for (const TCHAR Character : RawPlanName)
    {
        if (FChar::IsAlnum(Character))
        {
            Sanitized.AppendChar(FChar::ToLower(Character));
            bLastWasUnderscore = false;
        }
        else if (!bLastWasUnderscore)
        {
            Sanitized.AppendChar(TEXT('_'));
            bLastWasUnderscore = true;
        }
    }

    while (Sanitized.StartsWith(TEXT("_")))
    {
        Sanitized.RightChopInline(1, EAllowShrinking::No);
    }
    while (Sanitized.EndsWith(TEXT("_")))
    {
        Sanitized.LeftChopInline(1, EAllowShrinking::No);
    }
    while (Sanitized.ReplaceInline(TEXT("__"), TEXT("_")) > 0)
    {
    }

    if (Sanitized.IsEmpty())
    {
        Sanitized = TEXT("plan");
    }

    return Sanitized;
}

FString MakePlanTitle_Inspector(const FString& SanitizedPlanName)
{
    TArray<FString> Parts;
    SanitizedPlanName.ParseIntoArray(Parts, TEXT("_"), true);

    for (FString& Part : Parts)
    {
        if (!Part.IsEmpty())
        {
            Part[0] = FChar::ToUpper(Part[0]);
        }
    }

    return Parts.Num() > 0 ? FString::Join(Parts, TEXT(" ")) : TEXT("Plan");
}

FString GetPlansDirectory_Inspector()
{
    return FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/.Aura/plans"));
}

int32 FindNextPlanVersion_Inspector(const FString& SanitizedPlanName)
{
    const FString PlansDirectory = GetPlansDirectory_Inspector();
    IFileManager::Get().MakeDirectory(*PlansDirectory, true);

    TArray<FString> ExistingFiles;
    IFileManager::Get().FindFiles(ExistingFiles, *(FPaths::Combine(PlansDirectory, SanitizedPlanName + TEXT("_v*.md"))), true, false);

    const FRegexPattern Pattern(FString::Printf(TEXT("^%s_v(\\d+)\\.md$"), *SanitizedPlanName));
    int32 MaxVersion = 0;

    for (const FString& FileName : ExistingFiles)
    {
        FRegexMatcher Matcher(Pattern, FileName);
        if (Matcher.FindNext())
        {
            MaxVersion = FMath::Max(MaxVersion, FCString::Atoi(*Matcher.GetCaptureGroup(1)));
        }
    }

    return MaxVersion + 1;
}

struct FRecentImageRecord_Inspector
{
    FString FullPath;
    FDateTime Timestamp;
};

void CollectRecentImagesFromDirectory_Inspector(const FString& Directory, TArray<FRecentImageRecord_Inspector>& OutImages)
{
    if (!IFileManager::Get().DirectoryExists(*Directory))
    {
        return;
    }

    static const TCHAR* const Patterns[] = {
        TEXT("*.png"),
        TEXT("*.jpg"),
        TEXT("*.jpeg"),
        TEXT("*.webp")
    };

    TSet<FString> SeenPaths;
    for (const FRecentImageRecord_Inspector& Existing : OutImages)
    {
        SeenPaths.Add(Existing.FullPath);
    }

    for (const TCHAR* Pattern : Patterns)
    {
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *Directory, Pattern, true, false);
        for (FString FilePath : Files)
        {
            FilePath = FPaths::ConvertRelativePathToFull(FilePath);
            FPaths::NormalizeFilename(FilePath);
            if (SeenPaths.Contains(FilePath))
            {
                continue;
            }

            SeenPaths.Add(FilePath);

            FRecentImageRecord_Inspector Record;
            Record.FullPath = FilePath;
            Record.Timestamp = IFileManager::Get().GetTimeStamp(*FilePath);
            OutImages.Add(MoveTemp(Record));
        }
    }
}

TArray<TSharedPtr<FJsonValue>> CollectSelectedActorSummaries_Inspector()
{
    TArray<TSharedPtr<FJsonValue>> SelectedActors;
    if (!GEditor)
    {
        return SelectedActors;
    }

    if (USelection* ActorSelection = GEditor->GetSelectedActors())
    {
        for (FSelectionIterator It(*ActorSelection); It; ++It)
        {
            if (AActor* Actor = Cast<AActor>(*It))
            {
                TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
                ActorObject->SetStringField(TEXT("label"), Actor->GetActorLabel());
                ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
                ActorObject->SetStringField(TEXT("actor_path"), Actor->GetPathName());
                SelectedActors.Add(MakeShared<FJsonValueObject>(ActorObject));
            }
        }
    }

    return SelectedActors;
}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleShutdownHeadless(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("shutdown_attempted"), false);
    Result->SetStringField(TEXT("status"), TEXT("editor_connected"));
    Result->SetStringField(
        TEXT("message"),
        TEXT("UnrealMCP is attached to the live editor session. No separate headless instance is managed by this bridge, so nothing was shut down."));
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetUnrealContext(const TSharedPtr<FJsonObject>& Params)
{
    const bool bDragDropOnly = Params.IsValid() && Params->HasField(TEXT("only_get_drag_dropped_objects")) && Params->GetBoolField(TEXT("only_get_drag_dropped_objects"));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("status"), TEXT("editor_connected"));
    Result->SetArrayField(TEXT("drag_dropped_objects"), TArray<TSharedPtr<FJsonValue>>());

    if (bDragDropOnly)
    {
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> SelectedActors;
    if (GEditor)
    {
        if (USelection* ActorSelection = GEditor->GetSelectedActors())
        {
            for (FSelectionIterator It(*ActorSelection); It; ++It)
            {
                if (AActor* Actor = Cast<AActor>(*It))
                {
                    SelectedActors.Add(MakeShared<FJsonValueObject>(FEpicUnrealMCPCommonUtils::ActorToJsonObject(Actor, true)));
                }
            }
        }
    }
    Result->SetArrayField(TEXT("selected_actors"), SelectedActors);

    TArray<TSharedPtr<FJsonValue>> SelectedAssets;
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    TArray<FAssetData> AssetDataArray;
    ContentBrowserModule.Get().GetSelectedAssets(AssetDataArray);
    for (const FAssetData& AssetData : AssetDataArray)
    {
        TSharedPtr<FJsonObject> AssetObject = MakeShared<FJsonObject>();
        AssetObject->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
        AssetObject->SetStringField(TEXT("asset_path"), AssetData.GetSoftObjectPath().ToString());
        AssetObject->SetStringField(TEXT("asset_class"), AssetData.AssetClassPath.GetAssetName().ToString());
        SelectedAssets.Add(MakeShared<FJsonValueObject>(AssetObject));
    }
    Result->SetArrayField(TEXT("selected_assets"), SelectedAssets);
    Result->SetNumberField(TEXT("selected_actor_count"), SelectedActors.Num());
    Result->SetNumberField(TEXT("selected_asset_count"), SelectedAssets.Num());
    return Result;
}

namespace
{
void DescribeStructRecursive(
    const UStruct* Struct,
    TSharedPtr<FJsonObject>& OutStructObject,
    TSet<FString>& InOutVisited,
    const int32 Depth = 0)
{
    if (!Struct)
    {
        return;
    }

    OutStructObject->SetStringField(TEXT("struct_name"), Struct->GetName());
    OutStructObject->SetStringField(TEXT("struct_path"), Struct->GetPathName());
    if (const UScriptStruct* ScriptStruct = Cast<const UScriptStruct>(Struct))
    {
        OutStructObject->SetStringField(TEXT("struct_cpp_type"), ScriptStruct->GetStructCPPName());
    }
    else
    {
        OutStructObject->SetStringField(
            TEXT("struct_cpp_type"),
            FString::Printf(TEXT("%s%s"), Struct->GetPrefixCPP(), *Struct->GetName()));
    }

    const FString StructKey = Struct->GetPathName();
    if (InOutVisited.Contains(StructKey) || Depth >= 4)
    {
        OutStructObject->SetBoolField(TEXT("truncated"), true);
        return;
    }

    InOutVisited.Add(StructKey);

    TArray<TSharedPtr<FJsonValue>> Fields;
    for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        if (!Property)
        {
            continue;
        }

        TSharedPtr<FJsonObject> FieldObject = MakeShared<FJsonObject>();
        FieldObject->SetStringField(TEXT("name"), Property->GetName());
        FieldObject->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
        FieldObject->SetStringField(TEXT("property_class"), Property->GetClass()->GetName());

        if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            TSharedPtr<FJsonObject> NestedStructObject = MakeShared<FJsonObject>();
            DescribeStructRecursive(StructProperty->Struct, NestedStructObject, InOutVisited, Depth + 1);
            FieldObject->SetObjectField(TEXT("struct"), NestedStructObject);
        }
        else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            FieldObject->SetStringField(TEXT("container_kind"), TEXT("array"));
            FieldObject->SetStringField(TEXT("inner_cpp_type"), ArrayProperty->Inner ? ArrayProperty->Inner->GetCPPType() : FString());
        }
        else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
        {
            FieldObject->SetStringField(TEXT("container_kind"), TEXT("set"));
            FieldObject->SetStringField(TEXT("element_cpp_type"), SetProperty->ElementProp ? SetProperty->ElementProp->GetCPPType() : FString());
        }
        else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
        {
            FieldObject->SetStringField(TEXT("container_kind"), TEXT("map"));
            FieldObject->SetStringField(TEXT("key_cpp_type"), MapProperty->KeyProp ? MapProperty->KeyProp->GetCPPType() : FString());
            FieldObject->SetStringField(TEXT("value_cpp_type"), MapProperty->ValueProp ? MapProperty->ValueProp->GetCPPType() : FString());
        }

        Fields.Add(MakeShared<FJsonValueObject>(FieldObject));
    }

    OutStructObject->SetArrayField(TEXT("fields"), Fields);
}

TSharedPtr<FJsonObject> MaterialToJson(UMaterialInterface* Material)
{
    TSharedPtr<FJsonObject> MaterialObject = MakeShared<FJsonObject>();
    if (!Material)
    {
        MaterialObject->SetStringField(TEXT("material_name"), TEXT("None"));
        MaterialObject->SetStringField(TEXT("material_path"), TEXT(""));
        return MaterialObject;
    }

    MaterialObject->SetStringField(TEXT("material_name"), Material->GetName());
    MaterialObject->SetStringField(TEXT("material_path"), Material->GetPathName());
    MaterialObject->SetStringField(TEXT("material_class"), Material->GetClass()->GetName());

    TArray<FMaterialParameterInfo> ScalarInfos;
    TArray<FGuid> ScalarIds;
    Material->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);

    TArray<FMaterialParameterInfo> VectorInfos;
    TArray<FGuid> VectorIds;
    Material->GetAllVectorParameterInfo(VectorInfos, VectorIds);

    TArray<FMaterialParameterInfo> TextureInfos;
    TArray<FGuid> TextureIds;
    Material->GetAllTextureParameterInfo(TextureInfos, TextureIds);

    auto ToNames = [](const TArray<FMaterialParameterInfo>& Infos) -> TArray<TSharedPtr<FJsonValue>>
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const FMaterialParameterInfo& Info : Infos)
        {
            Values.Add(MakeShared<FJsonValueString>(Info.Name.ToString()));
        }
        return Values;
    };

    MaterialObject->SetArrayField(TEXT("scalar_parameters"), ToNames(ScalarInfos));
    MaterialObject->SetArrayField(TEXT("vector_parameters"), ToNames(VectorInfos));
    MaterialObject->SetArrayField(TEXT("texture_parameters"), ToNames(TextureInfos));
    return MaterialObject;
}

TArray<TSharedPtr<FJsonValue>> CollectBlueprintMaterials(UBlueprint* Blueprint)
{
    TArray<TSharedPtr<FJsonValue>> Results;
    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        return Results;
    }

    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (!Node || !Node->ComponentTemplate)
        {
            continue;
        }

        UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Node->ComponentTemplate);
        if (!PrimitiveComponent)
        {
            continue;
        }

        TSharedPtr<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
        ComponentObject->SetStringField(TEXT("component_name"), Node->GetVariableName().ToString());
        ComponentObject->SetStringField(TEXT("component_class"), PrimitiveComponent->GetClass()->GetName());

        TArray<TSharedPtr<FJsonValue>> MaterialSlots;
        for (int32 SlotIndex = 0; SlotIndex < PrimitiveComponent->GetNumMaterials(); ++SlotIndex)
        {
            TSharedPtr<FJsonObject> SlotObject = MaterialToJson(PrimitiveComponent->GetMaterial(SlotIndex));
            SlotObject->SetNumberField(TEXT("slot_index"), SlotIndex);
            MaterialSlots.Add(MakeShared<FJsonValueObject>(SlotObject));
        }

        ComponentObject->SetArrayField(TEXT("materials"), MaterialSlots);
        Results.Add(MakeShared<FJsonValueObject>(ComponentObject));
    }

    return Results;
}

TArray<TSharedPtr<FJsonValue>> CollectBlueprintEvents(UBlueprint* Blueprint)
{
    TArray<TSharedPtr<FJsonValue>> Results;
    if (!Blueprint)
    {
        return Results;
    }

    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (!Graph)
        {
            continue;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node || (!Node->IsA<UK2Node_Event>() && !Node->IsA<UK2Node_CustomEvent>()))
            {
                continue;
            }

            TSharedPtr<FJsonObject> EventObject = MakeShared<FJsonObject>();
            EventObject->SetStringField(TEXT("graph_name"), Graph->GetName());
            EventObject->SetStringField(TEXT("node_name"), Node->GetName());
            EventObject->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
            EventObject->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            Results.Add(MakeShared<FJsonValueObject>(EventObject));
        }
    }

    return Results;
}

TArray<TSharedPtr<FJsonValue>> CollectBlueprintMacros(UBlueprint* Blueprint)
{
    TArray<TSharedPtr<FJsonValue>> Results;
    if (!Blueprint)
    {
        return Results;
    }

    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (!Graph)
        {
            continue;
        }

        TSharedPtr<FJsonObject> MacroObject = MakeShared<FJsonObject>();
        MacroObject->SetStringField(TEXT("name"), Graph->GetName());
        MacroObject->SetStringField(TEXT("path"), Graph->GetPathName());
        MacroObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        Results.Add(MakeShared<FJsonValueObject>(MacroObject));
    }

    return Results;
}

TArray<TSharedPtr<FJsonValue>> CollectAnimGraphs(UBlueprint* Blueprint)
{
    TArray<TSharedPtr<FJsonValue>> Results;
    if (!Blueprint)
    {
        return Results;
    }

    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);
    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph || !Graph->GetName().Contains(TEXT("AnimGraph")))
        {
            continue;
        }

        TSharedPtr<FJsonObject> GraphObject = MakeShared<FJsonObject>();
        GraphObject->SetStringField(TEXT("name"), Graph->GetName());
        GraphObject->SetStringField(TEXT("path"), Graph->GetPathName());
        GraphObject->SetStringField(TEXT("graph_class"), Graph->GetClass()->GetName());
        GraphObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        Results.Add(MakeShared<FJsonValueObject>(GraphObject));
    }

    return Results;
}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleQueryUnrealProjectAssets(const TSharedPtr<FJsonObject>& Params)
{
    FString Query;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("query"), Query) || TrimmedOrEmpty(Query).IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'query' parameter"));
    }

    int32 Limit = 5;
    double LimitNumber = 0.0;
    if (Params->TryGetNumberField(TEXT("num_files"), LimitNumber))
    {
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 20);
    }

    const TArray<FString> Terms = TokenizeQuery(Query);

    struct FSearchResult
    {
        int32 Score = 0;
        TSharedPtr<FJsonObject> Payload;
    };

    TArray<FSearchResult> AssetResults;
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    FARFilter Filter;
    Filter.PackagePaths.Add(TEXT("/Game"));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    AssetRegistryModule.Get().GetAssets(Filter, Assets);
    for (const FAssetData& AssetData : Assets)
    {
        const FString AssetName = AssetData.AssetName.ToString();
        const FString AssetPath = AssetData.GetSoftObjectPath().ToString();
        const FString AssetClass = AssetData.AssetClassPath.GetAssetName().ToString();
        const int32 Score = ScoreQueryTerms(Terms, AssetName, AssetPath, AssetClass);
        if (Score <= 0)
        {
            continue;
        }

        TSharedPtr<FJsonObject> AssetObject = MakeShared<FJsonObject>();
        AssetObject->SetStringField(TEXT("result_type"), TEXT("asset"));
        AssetObject->SetStringField(TEXT("asset_name"), AssetName);
        AssetObject->SetStringField(TEXT("asset_path"), AssetPath);
        AssetObject->SetStringField(TEXT("asset_class"), AssetClass);
        AssetObject->SetNumberField(TEXT("score"), Score);
        AssetResults.Add({Score, AssetObject});
    }

    TArray<FSearchResult> FileResults;
    const TArray<FString> SearchRoots = {
        FPaths::GameSourceDir(),
        FPaths::ProjectPluginsDir(),
        FPaths::ProjectConfigDir(),
        FPaths::ProjectDir() / TEXT("Content/Python")
    };

    const TArray<FString> AllowedExtensions = {
        TEXT(".h"), TEXT(".hpp"), TEXT(".cpp"), TEXT(".cs"),
        TEXT(".py"), TEXT(".ini"), TEXT(".json"), TEXT(".md")
    };

    for (const FString& SearchRoot : SearchRoots)
    {
        if (SearchRoot.IsEmpty() || !IFileManager::Get().DirectoryExists(*SearchRoot))
        {
            continue;
        }

        TArray<FString> FoundFiles;
        IFileManager::Get().FindFilesRecursive(FoundFiles, *SearchRoot, TEXT("*.*"), true, false, false);
        for (const FString& FilePath : FoundFiles)
        {
            const FString Extension = FPaths::GetExtension(FilePath, true).ToLower();
            if (!AllowedExtensions.Contains(Extension))
            {
                continue;
            }

            const FString FileName = FPaths::GetCleanFilename(FilePath);
            const int32 Score = ScoreQueryTerms(Terms, FileName, FilePath, Extension);
            if (Score <= 0)
            {
                continue;
            }

            TSharedPtr<FJsonObject> FileObject = MakeShared<FJsonObject>();
            FileObject->SetStringField(TEXT("result_type"), TEXT("file"));
            FileObject->SetStringField(TEXT("file_name"), FileName);
            FileObject->SetStringField(TEXT("file_path"), FilePath);
            FileObject->SetStringField(TEXT("extension"), Extension);
            FileObject->SetNumberField(TEXT("score"), Score);
            FileResults.Add({Score, FileObject});
        }
    }

    auto SortResults = [](TArray<FSearchResult>& Results)
    {
        Results.Sort([](const FSearchResult& Left, const FSearchResult& Right)
        {
            return Left.Score > Right.Score;
        });
    };

    SortResults(AssetResults);
    SortResults(FileResults);

    TArray<TSharedPtr<FJsonValue>> CombinedResults;
    for (const FSearchResult& AssetResult : AssetResults)
    {
        CombinedResults.Add(MakeShared<FJsonValueObject>(AssetResult.Payload));
        if (CombinedResults.Num() >= Limit)
        {
            break;
        }
    }

    for (const FSearchResult& FileResult : FileResults)
    {
        if (CombinedResults.Num() >= Limit)
        {
            break;
        }

        CombinedResults.Add(MakeShared<FJsonValueObject>(FileResult.Payload));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("query"), Query);
    Result->SetArrayField(TEXT("results"), CombinedResults);
    Result->SetNumberField(TEXT("count"), CombinedResults.Num());
    Result->SetNumberField(TEXT("asset_match_count"), AssetResults.Num());
    Result->SetNumberField(TEXT("file_match_count"), FileResults.Num());
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleQuicksearch(const TSharedPtr<FJsonObject>& Params)
{
    FString Query;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("query"), Query) || TrimmedOrEmpty(Query).IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'query' parameter"));
    }

    int32 MaxResults = 10;
    double MaxResultsNumber = 0.0;
    if (Params->TryGetNumberField(TEXT("max_results"), MaxResultsNumber))
    {
        MaxResults = FMath::Clamp(static_cast<int32>(MaxResultsNumber), 1, 50);
    }

    const TArray<FString> Terms = TokenizeQuery(Query);

    struct FQuicksearchResult
    {
        int32 Score = 0;
        TSharedPtr<FJsonObject> Payload;
    };

    TArray<FQuicksearchResult> Results;

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    FARFilter Filter;
    Filter.PackagePaths.Add(TEXT("/Game"));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    AssetRegistryModule.Get().GetAssets(Filter, Assets);
    for (const FAssetData& AssetData : Assets)
    {
        const FString AssetName = AssetData.AssetName.ToString();
        const FString AssetPath = AssetData.GetSoftObjectPath().ToString();
        const FString AssetClass = AssetData.AssetClassPath.GetAssetName().ToString();
        const int32 Score = ScoreQueryTerms(Terms, AssetName, AssetPath, AssetClass);
        if (Score <= 0)
        {
            continue;
        }

        TArray<TSharedPtr<FJsonValue>> Dependencies;
        TArray<TSharedPtr<FJsonValue>> Referencers;
        CollectAssetDependencyNames_Inspector(AssetData, Dependencies, Referencers);

        TSharedPtr<FJsonObject> AssetObject = MakeShared<FJsonObject>();
        AssetObject->SetStringField(TEXT("result_type"), TEXT("asset"));
        AssetObject->SetStringField(TEXT("asset_name"), AssetName);
        AssetObject->SetStringField(TEXT("path"), AssetPath);
        AssetObject->SetStringField(TEXT("asset_class"), AssetClass);
        AssetObject->SetNumberField(TEXT("score"), Score);
        AssetObject->SetArrayField(TEXT("dependencies"), Dependencies);
        AssetObject->SetArrayField(TEXT("referencers"), Referencers);
        Results.Add({ Score, AssetObject });
    }

    TArray<FString> SearchRoots = GetDefaultProjectSearchRoots_Inspector();
    for (FString& SearchRoot : SearchRoots)
    {
        SearchRoot = FPaths::ConvertRelativePathToFull(SearchRoot);
    }

    for (const FString& SearchRoot : SearchRoots)
    {
        if (!IFileManager::Get().DirectoryExists(*SearchRoot))
        {
            continue;
        }

        TArray<FString> FoundFiles;
        IFileManager::Get().FindFilesRecursive(FoundFiles, *SearchRoot, TEXT("*.*"), true, false, false);
        for (const FString& FilePath : FoundFiles)
        {
            if (!IsSearchableTextFile_Inspector(FilePath))
            {
                continue;
            }

            const FString FileName = FPaths::GetCleanFilename(FilePath);
            const int32 Score = ScoreQueryTerms(Terms, FileName, FilePath, FPaths::GetExtension(FilePath, true));
            if (Score <= 0)
            {
                continue;
            }

            TSharedPtr<FJsonObject> FileObject = MakeShared<FJsonObject>();
            FileObject->SetStringField(TEXT("result_type"), TEXT("file"));
            FileObject->SetStringField(TEXT("file_name"), FileName);
            FileObject->SetStringField(TEXT("path"), FilePath);
            FileObject->SetNumberField(TEXT("score"), Score);
            FileObject->SetArrayField(TEXT("dependencies"), TArray<TSharedPtr<FJsonValue>>());
            FileObject->SetArrayField(TEXT("referencers"), TArray<TSharedPtr<FJsonValue>>());
            Results.Add({ Score, FileObject });
        }
    }

    Results.Sort([](const FQuicksearchResult& Left, const FQuicksearchResult& Right)
    {
        return Left.Score > Right.Score;
    });

    TArray<TSharedPtr<FJsonValue>> ResultValues;
    const int32 ResultCount = FMath::Min(MaxResults, Results.Num());
    for (int32 Index = 0; Index < ResultCount; ++Index)
    {
        ResultValues.Add(MakeShared<FJsonValueObject>(Results[Index].Payload));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("query"), Query);
    Result->SetArrayField(TEXT("results"), ResultValues);
    Result->SetNumberField(TEXT("count"), ResultValues.Num());
    Result->SetBoolField(TEXT("truncated"), Results.Num() > ResultCount);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGrep(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("pattern"), Pattern) || TrimmedOrEmpty(Pattern).IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pattern' parameter"));
    }

    FString PathValue;
    Params->TryGetStringField(TEXT("path"), PathValue);

    FString OutputMode = TEXT("content");
    Params->TryGetStringField(TEXT("output_mode"), OutputMode);
    OutputMode = TrimmedOrEmpty(OutputMode).ToLower();
    if (OutputMode.IsEmpty())
    {
        OutputMode = TEXT("content");
    }

    FString TypeFilter;
    Params->TryGetStringField(TEXT("type"), TypeFilter);

    FString GlobFilter;
    Params->TryGetStringField(TEXT("glob"), GlobFilter);

    bool bIgnoreCase = false;
    Params->TryGetBoolField(TEXT("-i"), bIgnoreCase);
    if (!bIgnoreCase)
    {
        Params->TryGetBoolField(TEXT("i"), bIgnoreCase);
    }

    bool bMultiline = false;
    Params->TryGetBoolField(TEXT("multiline"), bMultiline);

    bool bSearchFilenames = false;
    Params->TryGetBoolField(TEXT("search_filenames"), bSearchFilenames);

    bool bSearchEngine = false;
    Params->TryGetBoolField(TEXT("search_engine"), bSearchEngine);

    int32 HeadLimit = 100;
    double NumberValue = 0.0;
    if (Params->TryGetNumberField(TEXT("head_limit"), NumberValue))
    {
        HeadLimit = FMath::Clamp(static_cast<int32>(NumberValue), 1, 500);
    }

    int32 ContextBefore = 0;
    int32 ContextAfter = 0;
    if (Params->TryGetNumberField(TEXT("-C"), NumberValue) || Params->TryGetNumberField(TEXT("C"), NumberValue))
    {
        ContextBefore = ContextAfter = FMath::Clamp(static_cast<int32>(NumberValue), 0, 20);
    }
    if (Params->TryGetNumberField(TEXT("-B"), NumberValue) || Params->TryGetNumberField(TEXT("B"), NumberValue))
    {
        ContextBefore = FMath::Clamp(static_cast<int32>(NumberValue), 0, 20);
    }
    if (Params->TryGetNumberField(TEXT("-A"), NumberValue) || Params->TryGetNumberField(TEXT("A"), NumberValue))
    {
        ContextAfter = FMath::Clamp(static_cast<int32>(NumberValue), 0, 20);
    }

    const FString SearchPath = ResolveSearchPath_Inspector(PathValue, bSearchEngine, false);

    TArray<FString> CandidateFiles;
    if (PathValue.IsEmpty())
    {
        const TArray<FString> DefaultRoots = bSearchEngine
            ? TArray<FString>({ FPaths::Combine(FPaths::EngineDir(), TEXT("Source")) })
            : GetDefaultProjectSearchRoots_Inspector();

        for (const FString& Root : DefaultRoots)
        {
            TArray<FString> RootFiles;
            CollectFilesForSearch_Inspector(Root, RootFiles);
            CandidateFiles.Append(RootFiles);
        }
    }
    else
    {
        CollectFilesForSearch_Inspector(SearchPath, CandidateFiles);
    }

    CandidateFiles.Sort();
    CandidateFiles.SetNum(Algo::Unique(CandidateFiles));

    TArray<TSharedPtr<FJsonValue>> MatchValues;
    TArray<TSharedPtr<FJsonValue>> FileValues;
    TArray<TSharedPtr<FJsonValue>> CountValues;
    int32 TotalMatches = 0;

    for (const FString& FilePath : CandidateFiles)
    {
        if (!MatchesTypeFilter_Inspector(FilePath, TypeFilter) || !MatchesGlobFilter_Inspector(FilePath, GlobFilter))
        {
            continue;
        }

        const FString FileName = FPaths::GetCleanFilename(FilePath);
        if (bSearchFilenames)
        {
            if (!TextMatchesPattern_Inspector(FilePath, Pattern, bIgnoreCase) && !TextMatchesPattern_Inspector(FileName, Pattern, bIgnoreCase))
            {
                continue;
            }

            ++TotalMatches;
            if (OutputMode == TEXT("count"))
            {
                TSharedPtr<FJsonObject> CountObject = MakeShared<FJsonObject>();
                CountObject->SetStringField(TEXT("file_path"), FilePath);
                CountObject->SetNumberField(TEXT("count"), 1);
                CountValues.Add(MakeShared<FJsonValueObject>(CountObject));
            }
            else
            {
                TSharedPtr<FJsonObject> FileObject = MakeShared<FJsonObject>();
                FileObject->SetStringField(TEXT("file_path"), FilePath);
                FileObject->SetStringField(TEXT("file_name"), FileName);
                if (OutputMode == TEXT("files_with_matches"))
                {
                    FileValues.Add(MakeShared<FJsonValueObject>(FileObject));
                }
                else
                {
                    MatchValues.Add(MakeShared<FJsonValueObject>(FileObject));
                }
            }

            if ((MatchValues.Num() + FileValues.Num() + CountValues.Num()) >= HeadLimit)
            {
                break;
            }

            continue;
        }

        if (!IsSearchableTextFile_Inspector(FilePath))
        {
            continue;
        }

        FString FileContents;
        if (!FFileHelper::LoadFileToString(FileContents, *FilePath))
        {
            continue;
        }

        if (bMultiline)
        {
            int32 MatchBegin = INDEX_NONE;
            int32 MatchEnd = INDEX_NONE;
            if (!FindFirstPatternMatch_Inspector(FileContents, Pattern, bIgnoreCase, MatchBegin, MatchEnd))
            {
                continue;
            }

            ++TotalMatches;

            if (OutputMode == TEXT("count"))
            {
                TSharedPtr<FJsonObject> CountObject = MakeShared<FJsonObject>();
                CountObject->SetStringField(TEXT("file_path"), FilePath);
                CountObject->SetNumberField(TEXT("count"), 1);
                CountValues.Add(MakeShared<FJsonValueObject>(CountObject));
            }
            else if (OutputMode == TEXT("files_with_matches"))
            {
                TSharedPtr<FJsonObject> FileObject = MakeShared<FJsonObject>();
                FileObject->SetStringField(TEXT("file_path"), FilePath);
                FileValues.Add(MakeShared<FJsonValueObject>(FileObject));
            }
            else
            {
                const int32 LineNumber = CountNewlinesBeforeIndex_Inspector(FileContents, MatchBegin);
                const int32 SnippetStart = FMath::Max(0, MatchBegin - 120);
                const int32 SnippetEnd = FMath::Min(FileContents.Len(), MatchEnd + 120);

                TSharedPtr<FJsonObject> MatchObject = MakeShared<FJsonObject>();
                MatchObject->SetStringField(TEXT("file_path"), FilePath);
                MatchObject->SetNumberField(TEXT("line_number"), LineNumber);
                MatchObject->SetStringField(TEXT("snippet"), FileContents.Mid(SnippetStart, SnippetEnd - SnippetStart));
                MatchValues.Add(MakeShared<FJsonValueObject>(MatchObject));
            }

            if ((MatchValues.Num() + FileValues.Num() + CountValues.Num()) >= HeadLimit)
            {
                break;
            }

            continue;
        }

        TArray<FString> Lines;
        FileContents.ParseIntoArrayLines(Lines, false);

        int32 FileMatchCount = 0;
        for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
        {
            if (!TextMatchesPattern_Inspector(Lines[LineIndex], Pattern, bIgnoreCase))
            {
                continue;
            }

            ++FileMatchCount;
            ++TotalMatches;

            if (OutputMode == TEXT("content"))
            {
                TSharedPtr<FJsonObject> MatchObject = MakeShared<FJsonObject>();
                MatchObject->SetStringField(TEXT("file_path"), FilePath);
                MatchObject->SetNumberField(TEXT("line_number"), LineIndex + 1);
                MatchObject->SetStringField(TEXT("line"), Lines[LineIndex]);
                MatchObject->SetArrayField(TEXT("before"), BuildLineSlice_Inspector(Lines, LineIndex - ContextBefore, LineIndex));
                MatchObject->SetArrayField(TEXT("after"), BuildLineSlice_Inspector(Lines, LineIndex + 1, LineIndex + 1 + ContextAfter));
                MatchValues.Add(MakeShared<FJsonValueObject>(MatchObject));
            }

            if ((MatchValues.Num() + FileValues.Num() + CountValues.Num()) >= HeadLimit)
            {
                break;
            }
        }

        if (FileMatchCount == 0)
        {
            continue;
        }

        if (OutputMode == TEXT("files_with_matches"))
        {
            TSharedPtr<FJsonObject> FileObject = MakeShared<FJsonObject>();
            FileObject->SetStringField(TEXT("file_path"), FilePath);
            FileObject->SetNumberField(TEXT("count"), FileMatchCount);
            FileValues.Add(MakeShared<FJsonValueObject>(FileObject));
        }
        else if (OutputMode == TEXT("count"))
        {
            TSharedPtr<FJsonObject> CountObject = MakeShared<FJsonObject>();
            CountObject->SetStringField(TEXT("file_path"), FilePath);
            CountObject->SetNumberField(TEXT("count"), FileMatchCount);
            CountValues.Add(MakeShared<FJsonValueObject>(CountObject));
        }

        if ((MatchValues.Num() + FileValues.Num() + CountValues.Num()) >= HeadLimit)
        {
            break;
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("pattern"), Pattern);
    Result->SetStringField(TEXT("searched_path"), SearchPath);
    Result->SetStringField(TEXT("output_mode"), OutputMode);
    Result->SetNumberField(TEXT("match_count"), TotalMatches);
    Result->SetBoolField(TEXT("truncated"), (MatchValues.Num() + FileValues.Num() + CountValues.Num()) >= HeadLimit);

    if (OutputMode == TEXT("files_with_matches"))
    {
        Result->SetArrayField(TEXT("files"), FileValues);
        Result->SetNumberField(TEXT("count"), FileValues.Num());
    }
    else if (OutputMode == TEXT("count"))
    {
        Result->SetArrayField(TEXT("counts"), CountValues);
        Result->SetNumberField(TEXT("count"), CountValues.Num());
    }
    else
    {
        Result->SetArrayField(TEXT("matches"), MatchValues);
        Result->SetNumberField(TEXT("count"), MatchValues.Num());
    }

    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetCodeExamples(const TSharedPtr<FJsonObject>& Params)
{
    FString Keywords;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("keywords"), Keywords) || TrimmedOrEmpty(Keywords).IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'keywords' parameter"));
    }

    bool bIncludeEngineSource = false;
    Params->TryGetBoolField(TEXT("include_engine_source"), bIncludeEngineSource);

    TArray<FString> SearchTerms;
    Keywords.ParseIntoArray(SearchTerms, TEXT("|"), true);
    for (FString& SearchTerm : SearchTerms)
    {
        SearchTerm = TrimmedOrEmpty(SearchTerm);
    }
    SearchTerms.RemoveAll([](const FString& Value) { return Value.IsEmpty(); });

    if (SearchTerms.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No valid keywords were provided"));
    }

    const TArray<FString> SearchRoots = GetDefaultCodeSearchRoots_Inspector(bIncludeEngineSource);
    TArray<FString> CandidateFiles;
    for (const FString& SearchRoot : SearchRoots)
    {
        TArray<FString> RootFiles;
        CollectFilesForSearch_Inspector(SearchRoot, RootFiles);
        CandidateFiles.Append(RootFiles);
    }

    CandidateFiles.Sort();
    CandidateFiles.SetNum(Algo::Unique(CandidateFiles));

    TArray<TSharedPtr<FJsonValue>> KeywordResults;
    int32 TotalMatches = 0;

    for (const FString& SearchTerm : SearchTerms)
    {
        TArray<TSharedPtr<FJsonValue>> Matches;

        for (const FString& FilePath : CandidateFiles)
        {
            if (!MatchesTypeFilter_Inspector(FilePath, TEXT("cpp")))
            {
                continue;
            }

            FString FileContents;
            if (!FFileHelper::LoadFileToString(FileContents, *FilePath))
            {
                continue;
            }

            TArray<FString> Lines;
            FileContents.ParseIntoArrayLines(Lines, false);

            for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
            {
                if (!TextMatchesPattern_Inspector(Lines[LineIndex], SearchTerm, true))
                {
                    continue;
                }

                TSharedPtr<FJsonObject> MatchObject = MakeShared<FJsonObject>();
                MatchObject->SetStringField(TEXT("file_path"), FilePath);
                MatchObject->SetNumberField(TEXT("line_number"), LineIndex + 1);
                MatchObject->SetStringField(TEXT("line"), Lines[LineIndex]);
                MatchObject->SetArrayField(TEXT("before"), BuildLineSlice_Inspector(Lines, LineIndex - 2, LineIndex));
                MatchObject->SetArrayField(TEXT("after"), BuildLineSlice_Inspector(Lines, LineIndex + 1, LineIndex + 3));
                Matches.Add(MakeShared<FJsonValueObject>(MatchObject));
                ++TotalMatches;

                if (Matches.Num() >= 5)
                {
                    break;
                }
            }

            if (Matches.Num() >= 5)
            {
                break;
            }
        }

        TSharedPtr<FJsonObject> KeywordObject = MakeShared<FJsonObject>();
        KeywordObject->SetStringField(TEXT("keyword"), SearchTerm);
        KeywordObject->SetArrayField(TEXT("matches"), Matches);
        KeywordObject->SetNumberField(TEXT("count"), Matches.Num());
        KeywordResults.Add(MakeShared<FJsonValueObject>(KeywordObject));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("keywords"), Keywords);
    Result->SetBoolField(TEXT("include_engine_source"), bIncludeEngineSource);
    Result->SetStringField(TEXT("note"), TEXT("This simplified implementation scans project source directly; the first run may be slower on large projects."));
    Result->SetArrayField(TEXT("results"), KeywordResults);
    Result->SetNumberField(TEXT("count"), KeywordResults.Num());
    Result->SetNumberField(TEXT("total_matches"), TotalMatches);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetAssetMeta(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UObject* Asset = LoadAssetByReference(AssetPath);
    if (!Asset)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
    }

    const TArray<FString> Parts = GetStringArrayField(Params, TEXT("parts"));
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), NormalizeAssetReference(AssetPath));
    Result->SetStringField(TEXT("asset_name"), Asset->GetName());
    Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());

    TSet<FString> UnsupportedParts;
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);

    for (const FString& Part : Parts)
    {
        if (Part.Equals(TEXT("PropertyDeclarations"), ESearchCase::IgnoreCase))
        {
            if (!BlueprintCommands.IsValid() || !Blueprint)
            {
                UnsupportedParts.Add(Part);
                continue;
            }

            TSharedPtr<FJsonObject> ForwardParams = MakeShared<FJsonObject>();
            ForwardParams->SetStringField(TEXT("target"), NormalizeAssetReference(AssetPath));
            ForwardParams->SetBoolField(TEXT("include_values"), false);
            ForwardParams->SetBoolField(TEXT("include_metadata"), true);
            ForwardParams->SetBoolField(TEXT("editable_only"), false);
            Result->SetObjectField(TEXT("PropertyDeclarations"), BlueprintCommands->HandleCommand(TEXT("get_blueprint_properties"), ForwardParams));
        }
        else if (Part.Equals(TEXT("PropertyValues"), ESearchCase::IgnoreCase))
        {
            if (!BlueprintCommands.IsValid() || !Blueprint)
            {
                UnsupportedParts.Add(Part);
                continue;
            }

            TSharedPtr<FJsonObject> ForwardParams = MakeShared<FJsonObject>();
            ForwardParams->SetStringField(TEXT("target"), NormalizeAssetReference(AssetPath));
            ForwardParams->SetBoolField(TEXT("include_values"), true);
            ForwardParams->SetBoolField(TEXT("include_metadata"), true);
            ForwardParams->SetBoolField(TEXT("editable_only"), false);
            Result->SetObjectField(TEXT("PropertyValues"), BlueprintCommands->HandleCommand(TEXT("get_blueprint_properties"), ForwardParams));
        }
        else if (Part.Equals(TEXT("Components"), ESearchCase::IgnoreCase))
        {
            if (!BlueprintCommands.IsValid() || !Blueprint)
            {
                UnsupportedParts.Add(Part);
                continue;
            }

            TSharedPtr<FJsonObject> ForwardParams = MakeShared<FJsonObject>();
            ForwardParams->SetStringField(TEXT("target"), NormalizeAssetReference(AssetPath));
            ForwardParams->SetBoolField(TEXT("include_values"), true);
            ForwardParams->SetBoolField(TEXT("include_metadata"), true);
            ForwardParams->SetBoolField(TEXT("editable_only"), false);
            Result->SetObjectField(TEXT("Components"), BlueprintCommands->HandleCommand(TEXT("get_blueprint_component_properties"), ForwardParams));
        }
        else if (Part.Equals(TEXT("Functions"), ESearchCase::IgnoreCase))
        {
            if (!BlueprintCommands.IsValid() || !Blueprint)
            {
                UnsupportedParts.Add(Part);
                continue;
            }

            TSharedPtr<FJsonObject> ForwardParams = MakeShared<FJsonObject>();
            ForwardParams->SetStringField(TEXT("blueprint_path"), NormalizeAssetReference(AssetPath));
            ForwardParams->SetBoolField(TEXT("include_event_graph"), false);
            ForwardParams->SetBoolField(TEXT("include_functions"), true);
            ForwardParams->SetBoolField(TEXT("include_variables"), false);
            ForwardParams->SetBoolField(TEXT("include_components"), false);
            ForwardParams->SetBoolField(TEXT("include_interfaces"), false);
            Result->SetObjectField(TEXT("Functions"), BlueprintCommands->HandleCommand(TEXT("read_blueprint_content_fast"), ForwardParams));
        }
        else if (Part.Equals(TEXT("Interfaces"), ESearchCase::IgnoreCase))
        {
            if (!BlueprintCommands.IsValid() || !Blueprint)
            {
                UnsupportedParts.Add(Part);
                continue;
            }

            TSharedPtr<FJsonObject> ForwardParams = MakeShared<FJsonObject>();
            ForwardParams->SetStringField(TEXT("blueprint_path"), NormalizeAssetReference(AssetPath));
            ForwardParams->SetBoolField(TEXT("include_event_graph"), false);
            ForwardParams->SetBoolField(TEXT("include_functions"), false);
            ForwardParams->SetBoolField(TEXT("include_variables"), false);
            ForwardParams->SetBoolField(TEXT("include_components"), false);
            ForwardParams->SetBoolField(TEXT("include_interfaces"), true);
            Result->SetObjectField(TEXT("Interfaces"), BlueprintCommands->HandleCommand(TEXT("read_blueprint_content_fast"), ForwardParams));
        }
        else if (Part.Equals(TEXT("Events"), ESearchCase::IgnoreCase))
        {
            if (!Blueprint)
            {
                UnsupportedParts.Add(Part);
                continue;
            }

            TSharedPtr<FJsonObject> EventsObject = MakeShared<FJsonObject>();
            EventsObject->SetArrayField(TEXT("events"), CollectBlueprintEvents(Blueprint));
            Result->SetObjectField(TEXT("Events"), EventsObject);
        }
        else if (Part.Equals(TEXT("Macros"), ESearchCase::IgnoreCase))
        {
            if (!Blueprint)
            {
                UnsupportedParts.Add(Part);
                continue;
            }

            TSharedPtr<FJsonObject> MacrosObject = MakeShared<FJsonObject>();
            MacrosObject->SetArrayField(TEXT("macros"), CollectBlueprintMacros(Blueprint));
            Result->SetObjectField(TEXT("Macros"), MacrosObject);
        }
        else if (Part.Equals(TEXT("MaterialParams"), ESearchCase::IgnoreCase))
        {
            if (!Blueprint)
            {
                UnsupportedParts.Add(Part);
                continue;
            }

            TSharedPtr<FJsonObject> MaterialsObject = MakeShared<FJsonObject>();
            MaterialsObject->SetArrayField(TEXT("components"), CollectBlueprintMaterials(Blueprint));
            Result->SetObjectField(TEXT("MaterialParams"), MaterialsObject);
        }
        else if (Part.Equals(TEXT("AnimGraphs"), ESearchCase::IgnoreCase))
        {
            if (!Blueprint)
            {
                UnsupportedParts.Add(Part);
                continue;
            }

            TSharedPtr<FJsonObject> AnimGraphObject = MakeShared<FJsonObject>();
            AnimGraphObject->SetArrayField(TEXT("graphs"), CollectAnimGraphs(Blueprint));
            Result->SetObjectField(TEXT("AnimGraphs"), AnimGraphObject);
        }
        else
        {
            UnsupportedParts.Add(Part);
        }
    }

    if (Parts.Num() == 0)
    {
        TSharedPtr<FJsonObject> SummaryObject = MakeShared<FJsonObject>();
        SummaryObject->SetStringField(TEXT("asset_name"), Asset->GetName());
        SummaryObject->SetStringField(TEXT("asset_path"), NormalizeAssetReference(AssetPath));
        SummaryObject->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
        Result->SetObjectField(TEXT("summary"), SummaryObject);
    }

    if (UnsupportedParts.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> UnsupportedPartArray;
        for (const FString& Part : UnsupportedParts)
        {
            UnsupportedPartArray.Add(MakeShared<FJsonValueString>(Part));
        }
        Result->SetArrayField(TEXT("unsupported_parts"), UnsupportedPartArray);
    }

    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetAssetGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    const TArray<FString> StrandNames = GetStringArrayField(Params, TEXT("strand_names"));
    if (StrandNames.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'strand_names' parameter"));
    }

    TArray<TSharedPtr<FJsonValue>> Strands;
    int32 SuccessCount = 0;

    for (const FString& StrandName : StrandNames)
    {
        TSharedPtr<FJsonObject> ForwardParams = MakeShared<FJsonObject>();
        ForwardParams->SetStringField(TEXT("blueprint_path"), NormalizeAssetReference(AssetPath));
        ForwardParams->SetStringField(TEXT("graph_name"), StrandName);
        ForwardParams->SetBoolField(TEXT("include_pin_connections"), true);

        TSharedPtr<FJsonObject> StrandResult = BlueprintCommands.IsValid()
            ? BlueprintCommands->HandleCommand(TEXT("analyze_blueprint_graph_fast"), ForwardParams)
            : FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint command handler unavailable"));

        if (StrandResult.IsValid() && StrandResult->HasField(TEXT("success")) && StrandResult->GetBoolField(TEXT("success")))
        {
            ++SuccessCount;
        }

        Strands.Add(MakeShared<FJsonValueObject>(StrandResult));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), SuccessCount > 0);
    Result->SetStringField(TEXT("asset_path"), NormalizeAssetReference(AssetPath));
    Result->SetArrayField(TEXT("strands"), Strands);
    Result->SetNumberField(TEXT("requested_count"), StrandNames.Num());
    Result->SetNumberField(TEXT("success_count"), SuccessCount);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetAssetStructs(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UObject* Asset = LoadAssetByReference(AssetPath);
    if (!Asset)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
    }

    TArray<TSharedPtr<FJsonValue>> StructEntries;
    TSet<FString> VisitedStructs;

    if (const UDataTable* DataTable = Cast<UDataTable>(Asset))
    {
        if (const UScriptStruct* RowStruct = DataTable->GetRowStruct())
        {
            TSharedPtr<FJsonObject> RowStructObject = MakeShared<FJsonObject>();
            RowStructObject->SetStringField(TEXT("source"), TEXT("DataTableRowStruct"));
            RowStructObject->SetStringField(TEXT("property_name"), TEXT("RowStruct"));

            TSharedPtr<FJsonObject> StructObject = MakeShared<FJsonObject>();
            DescribeStructRecursive(RowStruct, StructObject, VisitedStructs);
            RowStructObject->SetObjectField(TEXT("struct"), StructObject);
            StructEntries.Add(MakeShared<FJsonValueObject>(RowStructObject));
        }
    }
    else if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        UClass* BlueprintClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(Blueprint);
        UObject* CDO = BlueprintClass ? BlueprintClass->GetDefaultObject() : nullptr;
        if (CDO)
        {
            for (TFieldIterator<FProperty> It(CDO->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
            {
                const FProperty* Property = *It;
                const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
                if (!StructProperty || !StructProperty->Struct)
                {
                    continue;
                }

                TSharedPtr<FJsonObject> PropertyStructObject = MakeShared<FJsonObject>();
                PropertyStructObject->SetStringField(TEXT("source"), TEXT("BlueprintProperty"));
                PropertyStructObject->SetStringField(TEXT("property_name"), Property->GetName());

                TSharedPtr<FJsonObject> StructObject = MakeShared<FJsonObject>();
                DescribeStructRecursive(StructProperty->Struct, StructObject, VisitedStructs);
                PropertyStructObject->SetObjectField(TEXT("struct"), StructObject);
                StructEntries.Add(MakeShared<FJsonValueObject>(PropertyStructObject));
            }
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), NormalizeAssetReference(AssetPath));
    Result->SetArrayField(TEXT("structs"), StructEntries);
    Result->SetNumberField(TEXT("struct_count"), StructEntries.Num());
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetBlueprintMaterialProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("soft_path_from_project_root"), BlueprintPath))
    {
        Params->TryGetStringField(TEXT("asset_path"), BlueprintPath);
    }

    if (BlueprintPath.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path_from_project_root' parameter"));
    }

    UBlueprint* Blueprint = Cast<UBlueprint>(LoadAssetByReference(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("blueprint_path"), NormalizeAssetReference(BlueprintPath));
    Result->SetArrayField(TEXT("components"), CollectBlueprintMaterials(Blueprint));
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleReviewBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    AssetPath = NormalizeAssetReference(AssetPath);
    UBlueprint* Blueprint = Cast<UBlueprint>(LoadAssetByReference(AssetPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
    }

    TArray<FString> StrandNames = GetStringArrayField(Params, TEXT("strand_names"));
    if (StrandNames.Num() == 0)
    {
        StrandNames.Add(TEXT("EventGraph"));
    }

    TArray<TSharedPtr<FJsonValue>> Findings;
    TArray<TSharedPtr<FJsonValue>> ReviewedGraphs;

    auto AddFinding = [&Findings](const FString& Severity, const FString& GraphName, const FString& Title, const FString& Detail)
    {
        TSharedPtr<FJsonObject> Finding = MakeShared<FJsonObject>();
        Finding->SetStringField(TEXT("severity"), Severity);
        Finding->SetStringField(TEXT("graph_name"), GraphName);
        Finding->SetStringField(TEXT("title"), Title);
        Finding->SetStringField(TEXT("detail"), Detail);
        Findings.Add(MakeShared<FJsonValueObject>(Finding));
    };

    for (const FString& GraphName : StrandNames)
    {
        TSharedPtr<FJsonObject> AnalysisResult = RunAnalyzeBlueprintGraphFast_Inspector(BlueprintCommands, Blueprint->GetPathName(), GraphName);
        if (!AnalysisResult.IsValid() || !AnalysisResult->GetBoolField(TEXT("success")))
        {
            AddFinding(
                TEXT("medium"),
                GraphName,
                TEXT("Graph could not be analyzed"),
                AnalysisResult.IsValid() && AnalysisResult->HasField(TEXT("error"))
                    ? AnalysisResult->GetStringField(TEXT("error"))
                    : TEXT("Fast graph analysis failed"));
            continue;
        }

        const TSharedPtr<FJsonObject>* GraphData = nullptr;
        if (!AnalysisResult->TryGetObjectField(TEXT("graph_data"), GraphData) || !GraphData || !GraphData->IsValid())
        {
            AddFinding(TEXT("medium"), GraphName, TEXT("Missing graph data"), TEXT("Analysis result did not include graph_data"));
            continue;
        }

        const int32 NodeCount = (*GraphData)->HasField(TEXT("node_count")) ? static_cast<int32>((*GraphData)->GetNumberField(TEXT("node_count"))) : 0;
        const int32 ConnectionCount = (*GraphData)->HasField(TEXT("connection_count")) ? static_cast<int32>((*GraphData)->GetNumberField(TEXT("connection_count"))) : 0;

        TSharedPtr<FJsonObject> ReviewedGraph = MakeShared<FJsonObject>();
        ReviewedGraph->SetStringField(TEXT("graph_name"), GraphName);
        ReviewedGraph->SetNumberField(TEXT("node_count"), NodeCount);
        ReviewedGraph->SetNumberField(TEXT("connection_count"), ConnectionCount);
        ReviewedGraphs.Add(MakeShared<FJsonValueObject>(ReviewedGraph));

        if (NodeCount > 120)
        {
            AddFinding(TEXT("high"), GraphName, TEXT("Graph is very large"), TEXT("This graph exceeds 120 nodes and is a strong candidate for refactor into functions or macros."));
        }
        else if (NodeCount > 60)
        {
            AddFinding(TEXT("medium"), GraphName, TEXT("Graph is getting large"), TEXT("This graph exceeds 60 nodes; splitting responsibilities will reduce edit risk."));
        }

        if (ConnectionCount == 0 && NodeCount > 1)
        {
            AddFinding(TEXT("low"), GraphName, TEXT("Graph has no recorded connections"), TEXT("Multiple nodes exist but no pin connections were detected; verify that the graph is not partially disconnected."));
        }

        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (!(*GraphData)->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes)
        {
            continue;
        }

        int32 PrintNodeCount = 0;
        int32 DynamicCastCount = 0;
        int32 GetAllActorsCount = 0;
        int32 DelayCount = 0;

        for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
        {
            if (!NodeValue.IsValid() || NodeValue->Type != EJson::Object)
            {
                continue;
            }

            const TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
            FString NodeClass;
            FString NodeTitle;
            NodeObject->TryGetStringField(TEXT("class"), NodeClass);
            NodeObject->TryGetStringField(TEXT("title"), NodeTitle);

            if (NodeTitle.Contains(TEXT("Print"), ESearchCase::IgnoreCase))
            {
                ++PrintNodeCount;
            }
            if (NodeClass.Contains(TEXT("DynamicCast"), ESearchCase::IgnoreCase))
            {
                ++DynamicCastCount;
            }
            if (NodeTitle.Contains(TEXT("Get All Actors Of Class"), ESearchCase::IgnoreCase) ||
                NodeTitle.Contains(TEXT("GetAllActorsOfClass"), ESearchCase::IgnoreCase))
            {
                ++GetAllActorsCount;
            }
            if (NodeTitle.Contains(TEXT("Delay"), ESearchCase::IgnoreCase))
            {
                ++DelayCount;
            }
        }

        if (PrintNodeCount > 0)
        {
            AddFinding(TEXT("low"), GraphName, TEXT("Debug print nodes remain"), FString::Printf(TEXT("Detected %d node(s) with Print in the title. Remove temporary debug output before shipping changes."), PrintNodeCount));
        }
        if (DynamicCastCount > 3)
        {
            AddFinding(TEXT("medium"), GraphName, TEXT("Many Dynamic Cast nodes"), FString::Printf(TEXT("Detected %d Dynamic Cast node(s). Prefer stable references or interfaces where possible."), DynamicCastCount));
        }
        if (GetAllActorsCount > 0)
        {
            AddFinding(TEXT("high"), GraphName, TEXT("Get All Actors Of Class detected"), FString::Printf(TEXT("Detected %d expensive actor query node(s). Avoid these in high-frequency paths."), GetAllActorsCount));
        }
        if (!GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase) && DelayCount > 0)
        {
            AddFinding(TEXT("medium"), GraphName, TEXT("Delay node outside EventGraph"), TEXT("Delay nodes are only safe in event-style graphs; verify that this graph is not a function graph."));
        }
    }

    FString PreviousAssetPath;
    TSharedPtr<FJsonObject> ComparisonObject = MakeShared<FJsonObject>();
    if (Params->TryGetStringField(TEXT("previous_asset_path"), PreviousAssetPath))
    {
        PreviousAssetPath = NormalizeAssetReference(PreviousAssetPath);
        UBlueprint* PreviousBlueprint = Cast<UBlueprint>(LoadAssetByReference(PreviousAssetPath));
        if (PreviousBlueprint)
        {
            TSharedPtr<FJsonObject> CurrentReadParams = MakeShared<FJsonObject>();
            CurrentReadParams->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
            CurrentReadParams->SetBoolField(TEXT("include_event_graph"), true);
            CurrentReadParams->SetBoolField(TEXT("include_functions"), true);
            CurrentReadParams->SetBoolField(TEXT("include_variables"), true);
            CurrentReadParams->SetBoolField(TEXT("include_components"), true);
            CurrentReadParams->SetBoolField(TEXT("include_interfaces"), true);

            TSharedPtr<FJsonObject> PreviousReadParams = MakeShared<FJsonObject>();
            PreviousReadParams->SetStringField(TEXT("blueprint_path"), PreviousBlueprint->GetPathName());
            PreviousReadParams->SetBoolField(TEXT("include_event_graph"), true);
            PreviousReadParams->SetBoolField(TEXT("include_functions"), true);
            PreviousReadParams->SetBoolField(TEXT("include_variables"), true);
            PreviousReadParams->SetBoolField(TEXT("include_components"), true);
            PreviousReadParams->SetBoolField(TEXT("include_interfaces"), true);

            TSharedPtr<FJsonObject> CurrentReadResult = BlueprintCommands->HandleCommand(TEXT("read_blueprint_content_fast"), CurrentReadParams);
            TSharedPtr<FJsonObject> PreviousReadResult = BlueprintCommands->HandleCommand(TEXT("read_blueprint_content_fast"), PreviousReadParams);

            if (CurrentReadResult.IsValid() && PreviousReadResult.IsValid() &&
                CurrentReadResult->GetBoolField(TEXT("success")) && PreviousReadResult->GetBoolField(TEXT("success")))
            {
                ComparisonObject->SetBoolField(TEXT("available"), true);
                ComparisonObject->SetStringField(TEXT("previous_asset_path"), PreviousBlueprint->GetPathName());
                ComparisonObject->SetNumberField(
                    TEXT("variable_count_delta"),
                    CurrentReadResult->HasField(TEXT("variable_count")) && PreviousReadResult->HasField(TEXT("variable_count"))
                        ? CurrentReadResult->GetNumberField(TEXT("variable_count")) - PreviousReadResult->GetNumberField(TEXT("variable_count"))
                        : 0.0);
                ComparisonObject->SetNumberField(
                    TEXT("function_count_delta"),
                    CurrentReadResult->HasField(TEXT("function_count")) && PreviousReadResult->HasField(TEXT("function_count"))
                        ? CurrentReadResult->GetNumberField(TEXT("function_count")) - PreviousReadResult->GetNumberField(TEXT("function_count"))
                        : 0.0);
                ComparisonObject->SetNumberField(
                    TEXT("component_count_delta"),
                    CurrentReadResult->HasField(TEXT("component_count")) && PreviousReadResult->HasField(TEXT("component_count"))
                        ? CurrentReadResult->GetNumberField(TEXT("component_count")) - PreviousReadResult->GetNumberField(TEXT("component_count"))
                        : 0.0);
            }
            else
            {
                ComparisonObject->SetBoolField(TEXT("available"), false);
                ComparisonObject->SetStringField(TEXT("reason"), TEXT("Failed to read current or previous blueprint for comparison"));
            }
        }
        else
        {
            ComparisonObject->SetBoolField(TEXT("available"), false);
            ComparisonObject->SetStringField(TEXT("reason"), TEXT("previous_asset_path could not be loaded"));
        }
    }

    FString CustomInstructions;
    Params->TryGetStringField(TEXT("custom_instructions"), CustomInstructions);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("review_mode"), TEXT("heuristic"));
    Result->SetStringField(
        TEXT("summary"),
        Findings.Num() > 0
            ? FString::Printf(TEXT("Generated %d heuristic finding(s) for %s."), Findings.Num(), *Blueprint->GetName())
            : FString::Printf(TEXT("No obvious structural issues were found in the requested graph set for %s."), *Blueprint->GetName()));
    Result->SetArrayField(TEXT("strand_names"), MakeStringValueArray_Inspector(StrandNames));
    Result->SetArrayField(TEXT("reviewed_graphs"), ReviewedGraphs);
    Result->SetArrayField(TEXT("findings"), Findings);
    Result->SetNumberField(TEXT("finding_count"), Findings.Num());
    if (!CustomInstructions.IsEmpty())
    {
        Result->SetStringField(TEXT("custom_instructions"), CustomInstructions);
    }
    if (ComparisonObject->Values.Num() > 0)
    {
        Result->SetObjectField(TEXT("comparison"), ComparisonObject);
    }
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleCreateAndValidateBlueprintPlan(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    FString BlueprintPath;
    FString GraphPage = TEXT("EventGraph");
    FString GraphType = TEXT("EventGraph");
    FString UserPrompt;

    if (!Params.IsValid() ||
        !Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName) ||
        !Params->TryGetStringField(TEXT("soft_path_from_project_root"), BlueprintPath) ||
        !Params->TryGetStringField(TEXT("user_prompt"), UserPrompt))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameters: 'blueprint_name', 'soft_path_from_project_root', or 'user_prompt'"));
    }

    Params->TryGetStringField(TEXT("graph_page"), GraphPage);
    Params->TryGetStringField(TEXT("graph_type"), GraphType);

    const FString NormalizedBlueprintPath = NormalizeAssetReference(BlueprintPath);
    UBlueprint* Blueprint = Cast<UBlueprint>(LoadAssetByReference(NormalizedBlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    TSharedPtr<FJsonObject> GraphAnalysis = RunAnalyzeBlueprintGraphFast_Inspector(BlueprintCommands, Blueprint->GetPathName(), GraphPage);

    TArray<TSharedPtr<FJsonValue>> Warnings;
    TArray<TSharedPtr<FJsonValue>> Steps;
    TArray<TSharedPtr<FJsonValue>> AdditionalBlueprints;
    TArray<TSharedPtr<FJsonValue>> SpecificProperties = MakeStringValueArray_Inspector(GetStringArrayField(Params, TEXT("specific_properties_to_change")));

    auto AddWarning = [&Warnings](const FString& WarningText)
    {
        Warnings.Add(MakeShared<FJsonValueString>(WarningText));
    };

    if (!GraphAnalysis.IsValid() || !GraphAnalysis->GetBoolField(TEXT("success")))
    {
        AddWarning(GraphAnalysis.IsValid() && GraphAnalysis->HasField(TEXT("error"))
            ? GraphAnalysis->GetStringField(TEXT("error"))
            : TEXT("Target graph could not be validated with fast analysis"));
    }

    TArray<FString> PromptLines;
    UserPrompt.ParseIntoArrayLines(PromptLines, true);

    TArray<FString> CurrentStepLines;
    bool bInsideStep = false;
    int32 StepNumber = 0;

    auto FlushStep = [&Steps, &CurrentStepLines, &StepNumber]()
    {
        FString StepText = FString::Join(CurrentStepLines, TEXT("\n")).TrimStartAndEnd();
        if (StepText.IsEmpty())
        {
            CurrentStepLines.Reset();
            return;
        }

        TSharedPtr<FJsonObject> StepObject = MakeShared<FJsonObject>();
        StepObject->SetNumberField(TEXT("step_number"), StepNumber);
        StepObject->SetStringField(TEXT("instruction_text"), StepText);

        FString Summary = StepText;
        Summary = Summary.Replace(TEXT("\r"), TEXT(""));
        int32 NewLineIndex = INDEX_NONE;
        if (Summary.FindChar(TEXT('\n'), NewLineIndex))
        {
            Summary = Summary.Left(NewLineIndex);
        }
        if (Summary.Len() > 120)
        {
            Summary = Summary.Left(120) + TEXT("...");
        }
        StepObject->SetStringField(TEXT("summary"), Summary);
        Steps.Add(MakeShared<FJsonValueObject>(StepObject));
        CurrentStepLines.Reset();
    };

    for (const FString& RawLine : PromptLines)
    {
        const FString Line = RawLine.TrimStartAndEnd();
        if (Line.StartsWith(TEXT("//Step_Start"), ESearchCase::IgnoreCase))
        {
            if (bInsideStep && CurrentStepLines.Num() > 0)
            {
                FlushStep();
            }
            bInsideStep = true;
            ++StepNumber;
            continue;
        }

        if (Line.StartsWith(TEXT("//Step_End"), ESearchCase::IgnoreCase))
        {
            if (bInsideStep)
            {
                FlushStep();
            }
            bInsideStep = false;
            continue;
        }

        if (bInsideStep)
        {
            CurrentStepLines.Add(Line);
        }
    }

    if (bInsideStep && CurrentStepLines.Num() > 0)
    {
        FlushStep();
    }

    if (Steps.Num() == 0)
    {
        AddWarning(TEXT("user_prompt did not contain //Step_Start and //Step_End blocks; generated a single fallback step from the raw prompt."));
        StepNumber = 1;
        CurrentStepLines = { UserPrompt };
        FlushStep();
    }

    const TArray<FString> AdditionalBlueprintPaths = GetStringArrayField(Params, TEXT("additional_blueprint_paths"));
    for (const FString& AdditionalPath : AdditionalBlueprintPaths)
    {
        TSharedPtr<FJsonObject> AdditionalObject = MakeShared<FJsonObject>();
        AdditionalObject->SetStringField(TEXT("asset_path"), NormalizeAssetReference(AdditionalPath));
        AdditionalObject->SetBoolField(TEXT("exists"), LoadAssetByReference(AdditionalPath) != nullptr);
        AdditionalBlueprints.Add(MakeShared<FJsonValueObject>(AdditionalObject));
    }

    bool bOnlyChangePartOfGraph = false;
    Params->TryGetBoolField(TEXT("only_change_part_of_graph"), bOnlyChangePartOfGraph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("validated"), GraphAnalysis.IsValid() && GraphAnalysis->GetBoolField(TEXT("success")));
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("soft_path_from_project_root"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("graph_page"), GraphPage);
    Result->SetStringField(TEXT("graph_type"), GraphType);
    Result->SetBoolField(TEXT("only_change_part_of_graph"), bOnlyChangePartOfGraph);
    Result->SetStringField(TEXT("user_prompt"), UserPrompt);
    Result->SetArrayField(TEXT("steps"), Steps);
    Result->SetNumberField(TEXT("step_count"), Steps.Num());
    Result->SetArrayField(TEXT("specific_properties_to_change"), SpecificProperties);
    Result->SetArrayField(TEXT("additional_blueprint_paths"), AdditionalBlueprints);
    Result->SetArrayField(TEXT("variables_needed"), TArray<TSharedPtr<FJsonValue>>());
    Result->SetArrayField(TEXT("custom_events_needed"), TArray<TSharedPtr<FJsonValue>>());
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetNumberField(TEXT("warning_count"), Warnings.Num());
    if (GraphAnalysis.IsValid())
    {
        Result->SetObjectField(TEXT("graph_validation"), GraphAnalysis);
    }
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetTextFileContents(const TSharedPtr<FJsonObject>& Params)
{
    FString FilePath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("full_file_path"), FilePath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'full_file_path' parameter"));
    }

    if (FPaths::IsRelative(FilePath))
    {
        FilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), FilePath);
    }

    FString FileContents;
    if (!FFileHelper::LoadFileToString(FileContents, *FilePath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to read file: %s"), *FilePath));
    }

    int32 LineStart = 1;
    int32 NumLines = 50;
    double NumberValue = 0.0;
    if (Params->TryGetNumberField(TEXT("line_start"), NumberValue))
    {
        LineStart = FMath::Max(1, static_cast<int32>(NumberValue));
    }
    if (Params->TryGetNumberField(TEXT("num_lines"), NumberValue))
    {
        NumLines = FMath::Max(1, static_cast<int32>(NumberValue));
    }

    TArray<FString> Lines;
    FileContents.ParseIntoArrayLines(Lines, false);

    TArray<TSharedPtr<FJsonValue>> LineValues;
    const int32 StartIndex = FMath::Clamp(LineStart - 1, 0, Lines.Num());
    const int32 EndIndex = FMath::Min(StartIndex + NumLines, Lines.Num());
    for (int32 Index = StartIndex; Index < EndIndex; ++Index)
    {
        LineValues.Add(MakeShared<FJsonValueString>(Lines[Index]));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("full_file_path"), FilePath);
    Result->SetNumberField(TEXT("total_line_count"), Lines.Num());
    Result->SetNumberField(TEXT("fetched_line_count"), LineValues.Num());
    Result->SetArrayField(TEXT("lines"), LineValues);
    return Result;
}

namespace
{
bool ShouldIncludeActorByClass(AActor* Actor, const FString& FilterClass)
{
    if (!Actor || FilterClass.IsEmpty())
    {
        return Actor != nullptr;
    }

    return Actor->GetClass()->GetName().Contains(FilterClass, ESearchCase::IgnoreCase);
}

bool ShouldIncludeActorByLabel(AActor* Actor, const FString& FilterLabel)
{
    if (!Actor || FilterLabel.IsEmpty())
    {
        return Actor != nullptr;
    }

    const FString Label = Actor->GetActorLabel();
    return Label.Contains(FilterLabel, ESearchCase::IgnoreCase) || Actor->GetName().Contains(FilterLabel, ESearchCase::IgnoreCase);
}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetAvailableActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to access editor world"));
    }

    FString FilterActorClass;
    FString FilterActorLabel;
    FString GetFlags;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("filter_actor_class"), FilterActorClass);
        Params->TryGetStringField(TEXT("filter_actor_label"), FilterActorLabel);
        Params->TryGetStringField(TEXT("get_flags"), GetFlags);
    }

    const TSet<FString> Flags = ParsePipeSeparatedFlags(GetFlags);

    TArray<TSharedPtr<FJsonValue>> Actors;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!ShouldIncludeActorByClass(Actor, FilterActorClass) || !ShouldIncludeActorByLabel(Actor, FilterActorLabel))
        {
            continue;
        }

        TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
        ActorObject->SetStringField(TEXT("actor_path"), Actor->GetPathName());

        if (Flags.Num() == 0 || Flags.Contains(TEXT("Label")))
        {
            ActorObject->SetStringField(TEXT("label"), Actor->GetActorLabel());
        }
        if (Flags.Num() == 0 || Flags.Contains(TEXT("FName")))
        {
            ActorObject->SetStringField(TEXT("fname"), Actor->GetFName().ToString());
        }
        if (Flags.Num() == 0 || Flags.Contains(TEXT("Class")))
        {
            ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
        }
        if (Flags.Contains(TEXT("SpatialInfo")))
        {
            const FVector Location = Actor->GetActorLocation();
            const FRotator Rotation = Actor->GetActorRotation();
            const FVector Scale = Actor->GetActorScale3D();

            TSharedPtr<FJsonObject> SpatialInfo = MakeShared<FJsonObject>();
            SpatialInfo->SetArrayField(TEXT("location"), {
                MakeShared<FJsonValueNumber>(Location.X),
                MakeShared<FJsonValueNumber>(Location.Y),
                MakeShared<FJsonValueNumber>(Location.Z)
            });
            SpatialInfo->SetArrayField(TEXT("rotation"), {
                MakeShared<FJsonValueNumber>(Rotation.Pitch),
                MakeShared<FJsonValueNumber>(Rotation.Yaw),
                MakeShared<FJsonValueNumber>(Rotation.Roll)
            });
            SpatialInfo->SetArrayField(TEXT("scale"), {
                MakeShared<FJsonValueNumber>(Scale.X),
                MakeShared<FJsonValueNumber>(Scale.Y),
                MakeShared<FJsonValueNumber>(Scale.Z)
            });
            ActorObject->SetObjectField(TEXT("spatial_info"), SpatialInfo);
        }
        if (Flags.Contains(TEXT("Modifications")))
        {
            ActorObject->SetArrayField(TEXT("modified_properties"), TArray<TSharedPtr<FJsonValue>>());
        }

        Actors.Add(MakeShared<FJsonValueObject>(ActorObject));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetArrayField(TEXT("actors"), Actors);
    Result->SetNumberField(TEXT("count"), Actors.Num());
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetEnums(const TSharedPtr<FJsonObject>& Params)
{
    FString Query;
    int32 MaxResults = 10;
    double NumberValue = 0.0;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("query"), Query);
        if (Params->TryGetNumberField(TEXT("max_results"), NumberValue))
        {
            MaxResults = FMath::Clamp(static_cast<int32>(NumberValue), 1, 100);
        }
    }

    const TArray<FString> Terms = TokenizeQuery(Query);

    TArray<UEnum*> Enums;
    for (TObjectIterator<UEnum> It; It; ++It)
    {
        if (UEnum* Enum = *It)
        {
            Enums.Add(Enum);
        }
    }

    Enums.Sort([](const UEnum& Left, const UEnum& Right)
    {
        return Left.GetName() < Right.GetName();
    });

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);

    if (Terms.Num() == 0)
    {
        TArray<TSharedPtr<FJsonValue>> EnumNames;
        for (UEnum* Enum : Enums)
        {
            EnumNames.Add(MakeShared<FJsonValueString>(Enum->GetName()));
        }
        Result->SetArrayField(TEXT("enum_names"), EnumNames);
        Result->SetNumberField(TEXT("count"), EnumNames.Num());
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> EnumObjects;
    for (UEnum* Enum : Enums)
    {
        const int32 Score = ScoreQueryTerms(Terms, Enum->GetName(), Enum->GetPathName(), TEXT("enum"));
        if (Score <= 0)
        {
            continue;
        }

        TSharedPtr<FJsonObject> EnumObject = MakeShared<FJsonObject>();
        EnumObject->SetStringField(TEXT("name"), Enum->GetName());
        EnumObject->SetStringField(TEXT("path"), Enum->GetPathName());

        TArray<TSharedPtr<FJsonValue>> Entries;
        for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
        {
            const FString EntryName = Enum->GetNameStringByIndex(Index);
            if (EntryName.EndsWith(TEXT("_MAX")))
            {
                continue;
            }

            TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
            EntryObject->SetStringField(TEXT("name"), EntryName);
            EntryObject->SetNumberField(TEXT("value"), Enum->GetValueByIndex(Index));
            Entries.Add(MakeShared<FJsonValueObject>(EntryObject));
        }

        EnumObject->SetArrayField(TEXT("entries"), Entries);
        EnumObject->SetNumberField(TEXT("score"), Score);
        EnumObjects.Add(MakeShared<FJsonValueObject>(EnumObject));
        if (EnumObjects.Num() >= MaxResults)
        {
            break;
        }
    }

    Result->SetArrayField(TEXT("enums"), EnumObjects);
    Result->SetNumberField(TEXT("count"), EnumObjects.Num());
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetGameplayTags(const TSharedPtr<FJsonObject>& Params)
{
    FString Query;
    int32 MaxResults = 10;
    double NumberValue = 0.0;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("query"), Query);
        if (Params->TryGetNumberField(TEXT("max_results"), NumberValue))
        {
            MaxResults = FMath::Clamp(static_cast<int32>(NumberValue), 1, 200);
        }
    }

    FGameplayTagContainer AllTags;
    UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, true);

    TArray<FString> TagNames;
    for (const FGameplayTag& Tag : AllTags)
    {
        TagNames.Add(Tag.ToString());
    }
    TagNames.Sort();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);

    const TArray<FString> Terms = TokenizeQuery(Query);
    if (Terms.Num() == 0)
    {
        TMap<FString, int32> CategoryCounts;
        for (const FString& TagName : TagNames)
        {
            TArray<FString> Parts;
            TagName.ParseIntoArray(Parts, TEXT("."), true);
            if (Parts.Num() == 0)
            {
                continue;
            }

            const FString Category = Parts.Num() >= 2 ? Parts[0] + TEXT(".") + Parts[1] : Parts[0];
            CategoryCounts.FindOrAdd(Category)++;
        }

        TArray<TSharedPtr<FJsonValue>> Categories;
        for (const TPair<FString, int32>& Pair : CategoryCounts)
        {
            TSharedPtr<FJsonObject> CategoryObject = MakeShared<FJsonObject>();
            CategoryObject->SetStringField(TEXT("category"), Pair.Key);
            CategoryObject->SetNumberField(TEXT("count"), Pair.Value);
            Categories.Add(MakeShared<FJsonValueObject>(CategoryObject));
        }

        Result->SetArrayField(TEXT("categories"), Categories);
        Result->SetNumberField(TEXT("count"), Categories.Num());
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> MatchingTags;
    for (const FString& TagName : TagNames)
    {
        const FString LowerTag = TagName.ToLower();
        bool bMatches = false;
        for (const FString& Term : Terms)
        {
            if (LowerTag.Contains(Term))
            {
                bMatches = true;
                break;
            }
        }

        if (!bMatches)
        {
            continue;
        }

        MatchingTags.Add(MakeShared<FJsonValueString>(TagName));
        if (MatchingTags.Num() >= MaxResults)
        {
            break;
        }
    }

    Result->SetArrayField(TEXT("tags"), MatchingTags);
    Result->SetNumberField(TEXT("count"), MatchingTags.Num());
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleReadDatatableKeys(const TSharedPtr<FJsonObject>& Params)
{
    FString SoftPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("soft_path"), SoftPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path' parameter"));
    }

    UDataTable* DataTable = Cast<UDataTable>(LoadAssetByReference(SoftPath));
    if (!DataTable)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("DataTable not found: %s"), *SoftPath));
    }

    TArray<TSharedPtr<FJsonValue>> RowNames;
    for (const FName& RowName : DataTable->GetRowNames())
    {
        RowNames.Add(MakeShared<FJsonValueString>(RowName.ToString()));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("soft_path"), NormalizeAssetReference(SoftPath));
    Result->SetArrayField(TEXT("row_names"), RowNames);
    Result->SetNumberField(TEXT("count"), RowNames.Num());
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleReadDatatableValues(const TSharedPtr<FJsonObject>& Params)
{
    FString SoftPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("soft_path"), SoftPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path' parameter"));
    }

    const TArray<FString> RequestedRows = GetStringArrayField(Params, TEXT("row_names"));

    UDataTable* DataTable = Cast<UDataTable>(LoadAssetByReference(SoftPath));
    if (!DataTable)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("DataTable not found: %s"), *SoftPath));
    }

    const UScriptStruct* RowStruct = DataTable->GetRowStruct();
    if (!RowStruct)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("DataTable row struct is not available"));
    }

    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const FString& RowNameString : RequestedRows)
    {
        const FName RowName(*RowNameString);
        const uint8* RowPtr = DataTable->FindRowUnchecked(RowName);
        if (!RowPtr)
        {
            continue;
        }

        TSharedPtr<FJsonObject> RowObject = MakeShared<FJsonObject>();
        FJsonObjectConverter::UStructToJsonObject(RowStruct, RowPtr, RowObject.ToSharedRef(), 0, 0);

        TSharedPtr<FJsonObject> WrappedRowObject = MakeShared<FJsonObject>();
        WrappedRowObject->SetStringField(TEXT("row_name"), RowNameString);
        WrappedRowObject->SetObjectField(TEXT("data"), RowObject);
        Rows.Add(MakeShared<FJsonValueObject>(WrappedRowObject));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("soft_path"), NormalizeAssetReference(SoftPath));
    Result->SetArrayField(TEXT("rows"), Rows);
    Result->SetNumberField(TEXT("count"), Rows.Num());
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleGetRecentGeneratedImages(const TSharedPtr<FJsonObject>& Params)
{
    TArray<FRecentImageRecord_Inspector> Images;
    CollectRecentImagesFromDirectory_Inspector(FPaths::Combine(FPaths::ProjectDir(), TEXT("Content/Generated_Images")), Images);
    CollectRecentImagesFromDirectory_Inspector(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/generated_images")), Images);
    CollectRecentImagesFromDirectory_Inspector(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/Generated_Images")), Images);
    CollectRecentImagesFromDirectory_Inspector(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/GeneratedImages")), Images);

    Images.Sort([](const FRecentImageRecord_Inspector& Left, const FRecentImageRecord_Inspector& Right)
    {
        return Left.Timestamp > Right.Timestamp;
    });

    TArray<TSharedPtr<FJsonValue>> ImagePaths;
    TArray<TSharedPtr<FJsonValue>> ImageObjects;
    const int32 MaxResults = 100;
    const int32 ResultCount = FMath::Min(Images.Num(), MaxResults);

    for (int32 Index = 0; Index < ResultCount; ++Index)
    {
        const FRecentImageRecord_Inspector& Image = Images[Index];
        ImagePaths.Add(MakeShared<FJsonValueString>(Image.FullPath));

        TSharedPtr<FJsonObject> ImageObject = MakeShared<FJsonObject>();
        ImageObject->SetStringField(TEXT("full_path"), Image.FullPath);
        ImageObject->SetStringField(TEXT("modified_timestamp"), Image.Timestamp.ToIso8601());
        ImageObjects.Add(MakeShared<FJsonValueObject>(ImageObject));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetArrayField(TEXT("image_paths"), ImagePaths);
    Result->SetArrayField(TEXT("images"), ImageObjects);
    Result->SetNumberField(TEXT("count"), ImagePaths.Num());
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleFetchAnimationSkill(const TSharedPtr<FJsonObject>& Params)
{
    FString SkillName;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("skill_name"), SkillName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'skill_name' parameter"));
    }

    const TArray<FString> Tips = {
        TEXT("角色替换和动画重定向前，先统一骨架、参考姿势和缩放基线，避免后续批处理出现系统性偏差。"),
        TEXT("优先在中间副本资产上做重定向和批量修正，不要直接覆盖原始动画源文件。"),
        TEXT("处理角色替换链路时，先确认 SkeletalMesh、Skeleton、AnimBlueprint 和 PhysicsAsset 的依赖关系。"),
        TEXT("涉及大批量动画资源时，先生成可重复执行的流程，再做抽样验证，而不是手工逐个修改。"),
        TEXT("如果需要 Python 自动化，优先使用隔离执行路径，并把导入、重定向、保存拆成清晰阶段。")
    };

    TSharedPtr<FJsonObject> Result = CreateKnowledgeResponse_Inspector(
        TEXT("animation_skill_reference"),
        TEXT("动画替换、重定向和批处理的关键点是先统一骨架/姿势基线，再在中间资产上分阶段执行和验证。"),
        Tips);
    Result->SetStringField(TEXT("skill_name"), SkillName);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleCreateOrEditPlan(const TSharedPtr<FJsonObject>& Params)
{
    FString MarkdownText;
    FString PlanName;
    FString Title;
    if (!Params.IsValid() ||
        !Params->TryGetStringField(TEXT("markdown_text"), MarkdownText) ||
        !Params->TryGetStringField(TEXT("plan_name"), PlanName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameters: 'markdown_text' or 'plan_name'"));
    }

    MarkdownText.TrimStartAndEndInline();
    if (MarkdownText.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'markdown_text' cannot be empty"));
    }

    Params->TryGetStringField(TEXT("title"), Title);

    const FString SanitizedPlanName = SanitizePlanName_Inspector(PlanName);
    const int32 Version = FindNextPlanVersion_Inspector(SanitizedPlanName);
    if (Title.TrimStartAndEnd().IsEmpty())
    {
        Title = MakePlanTitle_Inspector(SanitizedPlanName);
    }

    const FString FileName = FString::Printf(TEXT("%s_v%d.md"), *SanitizedPlanName, Version);
    const FString PlansDirectory = GetPlansDirectory_Inspector();
    const FString AbsolutePath = FPaths::Combine(PlansDirectory, FileName);

    IFileManager::Get().MakeDirectory(*PlansDirectory, true);
    if (!FFileHelper::SaveStringToFile(MarkdownText, *AbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to write plan file: %s"), *AbsolutePath));
    }

    FString RelativePath = AbsolutePath;
    FString NormalizedProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeFilename(NormalizedProjectDir);
    FPaths::NormalizeFilename(RelativePath);
    FPaths::MakePathRelativeTo(RelativePath, *NormalizedProjectDir);

    TSharedPtr<FJsonObject> ContentObject = MakeShared<FJsonObject>();
    ContentObject->SetStringField(TEXT("type"), TEXT("plan_markdown"));
    ContentObject->SetStringField(TEXT("title"), Title);
    ContentObject->SetStringField(TEXT("path"), RelativePath);
    ContentObject->SetStringField(TEXT("markdown_text"), MarkdownText);

    TArray<TSharedPtr<FJsonValue>> ContentArray;
    ContentArray.Add(MakeShared<FJsonValueObject>(ContentObject));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("plan_name"), SanitizedPlanName);
    Result->SetStringField(TEXT("title"), Title);
    Result->SetNumberField(TEXT("version"), Version);
    Result->SetStringField(TEXT("file_path"), AbsolutePath);
    Result->SetStringField(TEXT("relative_path"), RelativePath);
    Result->SetStringField(TEXT("markdown_text"), MarkdownText);
    Result->SetArrayField(TEXT("content"), ContentArray);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleFetchGasBestPractices(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<FString> Tips = {
        TEXT("用 GameplayTag 明确定义 Ability、Effect、输入和状态语义，避免把分支规则散落在图里。"),
        TEXT("AttributeSet 只保存权威属性与最小派生值，复杂结算优先放到 Ability 或 Effect 执行逻辑。"),
        TEXT("GameplayEffect 尽量数据驱动，持续时间、叠层和修饰规则统一放进可复用资产。"),
        TEXT("网络能力优先明确服务器权威、客户端预测和回滚边界，不要把复制责任隐式分散。"),
        TEXT("Ability 激活前先梳理输入、冷却、消耗、阻断标签和 UI 反馈链路，再开始实现。")
    };

    return CreateKnowledgeResponse_Inspector(
        TEXT("gas_best_practices"),
        TEXT("GAS 设计的核心是标签驱动、数据驱动和明确的权威/预测边界。"),
        Tips);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleFetchUiBestPractices(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<FString> Tips = {
        TEXT("先搭高层容器和布局，再逐步填充叶子控件，不要一次堆出整棵深层 WidgetTree。"),
        TEXT("Widget 负责展示，业务状态尽量从 ViewModel、蓝图逻辑或外部数据注入，不在层级里重复保存状态。"),
        TEXT("优先使用语义清晰的命名、分区容器和有限层级，避免大量无意义的嵌套 Box。"),
        TEXT("需要改 WidgetTree 前，先读取当前树结构和关键属性，避免对父子关系做盲改。"),
        TEXT("完成 UI 结构修改后，始终做一次可视化验证，确认布局、尺寸和对齐在目标分辨率下成立。")
    };

    return CreateKnowledgeResponse_Inspector(
        TEXT("ui_best_practices"),
        TEXT("UMG 修改应先搭结构、再填细节，并保持浅层树、清晰命名和可视化验证闭环。"),
        Tips);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleImportUnderstanding(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    FString Category;
    FString Summary;
    TArray<FString> Lines;

    if (CommandType == TEXT("import_AssetCreation_understanding"))
    {
        Category = TEXT("asset_creation_understanding");
        Summary = TEXT("创建资产前先确定包路径、工厂类型和保存策略，避免生成后再回补元数据。");
        Lines = {
            TEXT("创建资产前先规范化 `/Game/...` 目标路径，并先检查资产是否已存在。"),
            TEXT("优先通过明确的工厂或资产工具创建，而不是先生成空对象再补反射字段。"),
            TEXT("涉及蓝图、结构体、枚举或 DataAsset 时，先确定父类/行结构，再写默认值。"),
            TEXT("批量创建资产后立即保存并刷新注册表，避免后续查询拿到过期视图。")
        };
    }
    else if (CommandType == TEXT("import_AssetRegistry_understanding"))
    {
        Category = TEXT("asset_registry_understanding");
        Summary = TEXT("Asset Registry 适合按类、路径和依赖关系快速检索资产，但前提是路径与过滤条件准确。");
        Lines = {
            TEXT("检索前先明确是按类、按包路径还是按依赖关系搜索，避免过宽过滤导致结果噪声。"),
            TEXT("用 FARFilter 时优先限制类和路径，再决定是否递归扫描子目录。"),
            TEXT("软路径和对象路径要统一格式，否则常见问题是找得到包却找不到对象。"),
            TEXT("做引用分析时要区分 dependencies 与 referencers，结论方向相反。")
        };
    }
    else if (CommandType == TEXT("import_AssetType_Level_understanding"))
    {
        Category = TEXT("asset_type_level_understanding");
        Summary = TEXT("处理 Level 资产时要区分当前编辑世界、子关卡和资产本体，不要把运行时对象和磁盘资产混为一谈。");
        Lines = {
            TEXT("读取关卡信息前先确认当前编辑器世界是否就是目标场景。"),
            TEXT("涉及子关卡或分区场景时，先确认哪些区域当前已加载，否则结果会天然不完整。"),
            TEXT("只读分析和写入改动要分离，避免在检查逻辑里隐式触发保存或脏标记。"),
            TEXT("定位场景对象时优先用类、标签和路径组合，而不是只靠显示名称。")
        };
    }
    else if (CommandType == TEXT("import_AssetValidation_understanding"))
    {
        Category = TEXT("asset_validation_understanding");
        Summary = TEXT("资产校验要先定义稳定规则，再输出可定位的错误上下文，而不是只返回通过/失败。");
        Lines = {
            TEXT("把校验拆成命名、路径、类型、引用完整性和编译状态几个层面。"),
            TEXT("发现问题时同时返回资产路径和失败原因，方便后续批量修复。"),
            TEXT("批量校验时先做轻量过滤，再跑昂贵检查，避免整体耗时失控。"),
            TEXT("涉及蓝图资产时，把编译结果作为校验的一部分，而不是事后补查。")
        };
    }
    else if (CommandType == TEXT("import_Color_understanding"))
    {
        Category = TEXT("color_understanding");
        Summary = TEXT("颜色处理要明确是线性空间还是 8 位颜色，并统一输入输出范围。");
        Lines = {
            TEXT("材质、UI 和调试颜色优先统一成 `FLinearColor` 语义，再决定最终显示或存储格式。"),
            TEXT("0-1 浮点颜色和 0-255 整数颜色不要混用；跨接口时先做显式转换。"),
            TEXT("需要序列化颜色时，约定 RGBA 顺序和数值范围，避免脚本侧解释不一致。"),
            TEXT("颜色只是参数，不要把业务状态硬编码成魔法颜色。")
        };
    }
    else if (CommandType == TEXT("import_FileSystem_understanding"))
    {
        Category = TEXT("filesystem_understanding");
        Summary = TEXT("文件系统相关脚本首先要保证路径留在项目允许范围内，并统一相对/绝对路径转换。");
        Lines = {
            TEXT("先做项目内路径归一化，再决定允许的读写根目录。"),
            TEXT("写文件前总是确保目录存在，并明确编码策略，避免中文或换行差异导致结果不稳定。"),
            TEXT("对导入导出流程，保留相对路径和绝对路径两种表示，便于 UI 和底层执行同时使用。"),
            TEXT("计划文件、配置文件和脚本文件应分别走各自约束，不要共用一个宽松写入口。")
        };
    }
    else if (CommandType == TEXT("import_Logs_understanding"))
    {
        Category = TEXT("logs_understanding");
        Summary = TEXT("日志应服务于定位和批处理诊断，重点是可筛选、可归因，而不是输出越多越好。");
        Lines = {
            TEXT("统一区分 info、warning、error 三层，错误日志必须带出资产或对象上下文。"),
            TEXT("长流程中保留关键阶段节点，减少无意义进度刷屏。"),
            TEXT("批处理任务失败时，日志里同时记录输入参数和失败对象，方便重放。"),
            TEXT("用户可见结果和调试日志应分层，不要把底层噪声直接暴露给上层工作流。")
        };
    }
    else if (CommandType == TEXT("import_Subsystems_understanding"))
    {
        Category = TEXT("subsystems_understanding");
        Summary = TEXT("Subsystem 是访问编辑器和项目能力的稳定入口，先确定需要哪个 subsystem，再组织脚本调用。");
        Lines = {
            TEXT("编辑器操作优先走 Editor Subsystem，而不是直接依赖临时窗口状态。"),
            TEXT("资产操作、关卡操作和工具窗口操作通常对应不同 subsystem，不要混在一个入口里猜测。"),
            TEXT("在脚本开始处显式获取并校验 subsystem，可减少中途失败时的歧义。"),
            TEXT("当某项能力已有高层结构化命令时，优先用命令而不是再下钻到 subsystem。")
        };
    }
    else
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported understanding import: %s"), *CommandType));
    }

    TSharedPtr<FJsonObject> Result = CreateKnowledgeResponse_Inspector(Category, Summary, Lines);
    Result->SetStringField(TEXT("command"), CommandType);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPInspectorCommands::HandleInspectCurrentLevel(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to access editor world"));
    }

    FString QueryText;
    FString PythonScript;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("query_text"), QueryText);
        Params->TryGetStringField(TEXT("python_script"), PythonScript);
    }

    const TArray<FString> Terms = TokenizeQuery(QueryText);

    struct FActorMatch_Inspector
    {
        AActor* Actor = nullptr;
        int32 Score = 0;
    };

    TArray<FActorMatch_Inspector> Matches;
    int32 TotalActorCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        ++TotalActorCount;

        const int32 Score = Terms.Num() == 0
            ? 0
            : ScoreQueryTerms(Terms, Actor->GetActorLabel(), Actor->GetPathName(), Actor->GetClass()->GetName());

        if (Terms.Num() == 0 || Score > 0)
        {
            FActorMatch_Inspector Match;
            Match.Actor = Actor;
            Match.Score = Score;
            Matches.Add(Match);
        }
    }

    Matches.Sort([](const FActorMatch_Inspector& Left, const FActorMatch_Inspector& Right)
    {
        if (Left.Score != Right.Score)
        {
            return Left.Score > Right.Score;
        }
        if (!Left.Actor || !Right.Actor)
        {
            return Left.Actor != nullptr;
        }
        return Left.Actor->GetActorLabel() < Right.Actor->GetActorLabel();
    });

    const int32 MaxMatches = 50;
    TArray<TSharedPtr<FJsonValue>> MatchingActors;
    for (int32 Index = 0; Index < Matches.Num() && Index < MaxMatches; ++Index)
    {
        AActor* Actor = Matches[Index].Actor;
        if (!Actor)
        {
            continue;
        }

        TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
        ActorObject->SetStringField(TEXT("label"), Actor->GetActorLabel());
        ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
        ActorObject->SetStringField(TEXT("actor_path"), Actor->GetPathName());
        ActorObject->SetNumberField(TEXT("score"), Matches[Index].Score);

        const FVector Location = Actor->GetActorLocation();
        ActorObject->SetArrayField(TEXT("location"), {
            MakeShared<FJsonValueNumber>(Location.X),
            MakeShared<FJsonValueNumber>(Location.Y),
            MakeShared<FJsonValueNumber>(Location.Z)
        });

        MatchingActors.Add(MakeShared<FJsonValueObject>(ActorObject));
    }

    TArray<FString> LogLines;
    LogLines.Add(TEXT("Native safe level inspection executed in read-only mode."));
    LogLines.Add(FString::Printf(TEXT("World: %s"), *World->GetName()));
    LogLines.Add(FString::Printf(TEXT("Total actors scanned: %d"), TotalActorCount));
    if (!QueryText.TrimStartAndEnd().IsEmpty())
    {
        LogLines.Add(FString::Printf(TEXT("Query text: %s"), *QueryText));
        LogLines.Add(FString::Printf(TEXT("Matched actors: %d"), Matches.Num()));
    }
    else
    {
        LogLines.Add(TEXT("No query_text provided; returning a capped actor summary."));
    }
    if (!PythonScript.TrimStartAndEnd().IsEmpty())
    {
        LogLines.Add(TEXT("Compatibility note: python_script was accepted but not executed; UnrealMCP keeps this inspector path native and read-only."));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("query_text"), QueryText);
    Result->SetStringField(TEXT("execution_mode"), TEXT("native_safe_inspector"));
    Result->SetBoolField(TEXT("python_executed"), false);
    Result->SetStringField(TEXT("world_name"), World->GetName());
    Result->SetNumberField(TEXT("total_actor_count"), TotalActorCount);
    Result->SetNumberField(TEXT("matching_actor_count"), Matches.Num());
    Result->SetArrayField(TEXT("matching_actors"), MatchingActors);
    Result->SetArrayField(TEXT("selected_actors"), CollectSelectedActorSummaries_Inspector());
    Result->SetArrayField(TEXT("log_lines"), MakeStringValueArray_Inspector(LogLines));
    Result->SetStringField(TEXT("log_output"), FString::Join(LogLines, TEXT("\n")));
    return Result;
}
