#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "ImageUtils.h"
#include "HighResScreenshot.h"
#include "Engine/GameViewportClient.h"
#include "Misc/FileHelper.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "FileHelpers.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/InputSettings.h"
#include "InputCoreTypes.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedEnum.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "JsonObjectConverter.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Internationalization/Regex.h"
#include "Misc/ScopeExit.h"
#include "Misc/PackageName.h"
#include "EdGraphSchema_K2.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"

namespace
{
struct FFabSearchResult
{
    FString Title;
    FString FabUrl;
    FString ListingId;
    FString Snippet;
    FString IndexedDate;
    bool bIsFreeMatch = false;
    int32 MatchScore = 0;
    int32 SearchRank = 0;
};

bool TryGetNonEmptyStringField(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, FString& OutValue)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    if (!JsonObject->TryGetStringField(FieldName, OutValue))
    {
        return false;
    }

    OutValue.TrimStartAndEndInline();
    return !OutValue.IsEmpty();
}

bool GetOptionalBoolField(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, bool bDefaultValue = false)
{
    bool bValue = bDefaultValue;
    if (JsonObject.IsValid())
    {
        JsonObject->TryGetBoolField(FieldName, bValue);
    }

    return bValue;
}

FKey ResolveInputKey(const FString& RequestedKeyName)
{
    const FString TrimmedKeyName = RequestedKeyName.TrimStartAndEnd();
    if (TrimmedKeyName.IsEmpty())
    {
        return EKeys::Invalid;
    }

    const FKey DirectKey(*TrimmedKeyName);
    if (DirectKey.IsValid())
    {
        return DirectKey;
    }

    TArray<FKey> AllKeys;
    EKeys::GetAllKeys(AllKeys);

    for (const FKey& CandidateKey : AllKeys)
    {
        if (!CandidateKey.IsValid())
        {
            continue;
        }

        if (CandidateKey.GetFName().ToString().Equals(TrimmedKeyName, ESearchCase::IgnoreCase) ||
            CandidateKey.ToString().Equals(TrimmedKeyName, ESearchCase::IgnoreCase) ||
            CandidateKey.GetDisplayName(false).ToString().Equals(TrimmedKeyName, ESearchCase::IgnoreCase) ||
            CandidateKey.GetDisplayName(true).ToString().Equals(TrimmedKeyName, ESearchCase::IgnoreCase))
        {
            return CandidateKey;
        }
    }

    return EKeys::Invalid;
}

bool HasExactActionMapping(const UInputSettings* InputSettings, const FInputActionKeyMapping& Mapping)
{
    if (!InputSettings)
    {
        return false;
    }

    for (const FInputActionKeyMapping& ExistingMapping : InputSettings->GetActionMappings())
    {
        if (ExistingMapping.ActionName == Mapping.ActionName &&
            ExistingMapping.Key == Mapping.Key &&
            ExistingMapping.bShift == Mapping.bShift &&
            ExistingMapping.bCtrl == Mapping.bCtrl &&
            ExistingMapping.bAlt == Mapping.bAlt &&
            ExistingMapping.bCmd == Mapping.bCmd)
        {
            return true;
        }
    }

    return false;
}

TSharedPtr<FJsonObject> ActionMappingToJson(const FInputActionKeyMapping& Mapping)
{
    TSharedPtr<FJsonObject> MappingJson = MakeShared<FJsonObject>();
    MappingJson->SetStringField(TEXT("action_name"), Mapping.ActionName.ToString());
    MappingJson->SetStringField(TEXT("key"), Mapping.Key.ToString());
    MappingJson->SetBoolField(TEXT("shift"), Mapping.bShift);
    MappingJson->SetBoolField(TEXT("ctrl"), Mapping.bCtrl);
    MappingJson->SetBoolField(TEXT("alt"), Mapping.bAlt);
    MappingJson->SetBoolField(TEXT("cmd"), Mapping.bCmd);
    return MappingJson;
}

bool TryGetJsonArrayFieldFlexible(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, TArray<TSharedPtr<FJsonValue>>& OutArray)
{
    OutArray.Reset();
    if (!JsonObject.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray)
    {
        OutArray = *JsonArray;
        return true;
    }

    FString JsonText;
    if (!TryGetNonEmptyStringField(JsonObject, FieldName, JsonText))
    {
        return false;
    }

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    return FJsonSerializer::Deserialize(Reader, OutArray);
}

bool TryGetJsonObjectFieldFlexible(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, TSharedPtr<FJsonObject>& OutObject)
{
    OutObject.Reset();
    if (!JsonObject.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* NestedObject = nullptr;
    if (JsonObject->TryGetObjectField(FieldName, NestedObject) && NestedObject && NestedObject->IsValid())
    {
        OutObject = *NestedObject;
        return true;
    }

    FString JsonText;
    if (!TryGetNonEmptyStringField(JsonObject, FieldName, JsonText))
    {
        return false;
    }

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}

bool NormalizeGameFolderPath(const FString& InPath, FString& OutFolderPath, FString& OutError)
{
    OutFolderPath = InPath;
    OutFolderPath.TrimStartAndEndInline();

    if (OutFolderPath.IsEmpty())
    {
        OutError = TEXT("Path is empty");
        return false;
    }

    if (OutFolderPath.Contains(TEXT(".")))
    {
        OutFolderPath = FPackageName::ObjectPathToPackageName(OutFolderPath);
    }

    OutFolderPath.ReplaceInline(TEXT("\\"), TEXT("/"));
    OutFolderPath.RemoveFromEnd(TEXT("/"));

    if (!OutFolderPath.StartsWith(TEXT("/Game/")) && OutFolderPath != TEXT("/Game"))
    {
        OutError = TEXT("Path must be a /Game/... folder path");
        return false;
    }

    return true;
}

FString BuildAssetPath(const FString& FolderPath, const FString& AssetName)
{
    return FolderPath.EndsWith(TEXT("/"))
        ? FolderPath + AssetName
        : FolderPath + TEXT("/") + AssetName;
}

template<typename TObjectType>
TObjectType* LoadEditorAsset(const FString& AssetPath)
{
    FString NormalizedPath = AssetPath;
    NormalizedPath.TrimStartAndEndInline();
    if (NormalizedPath.IsEmpty())
    {
        return nullptr;
    }

    return Cast<TObjectType>(UEditorAssetLibrary::LoadAsset(NormalizedPath));
}

EInputActionValueType ParseInputActionValueType(const FString& RequestedValueType, bool& bOutSuccess)
{
    FString Normalized = RequestedValueType;
    Normalized.TrimStartAndEndInline();
    Normalized.ToLowerInline();
    bOutSuccess = true;

    if (Normalized.IsEmpty() || Normalized == TEXT("digital") || Normalized == TEXT("bool") || Normalized == TEXT("boolean"))
    {
        return EInputActionValueType::Boolean;
    }
    if (Normalized == TEXT("axis1d") || Normalized == TEXT("1d"))
    {
        return EInputActionValueType::Axis1D;
    }
    if (Normalized == TEXT("axis2d") || Normalized == TEXT("2d"))
    {
        return EInputActionValueType::Axis2D;
    }
    if (Normalized == TEXT("axis3d") || Normalized == TEXT("3d"))
    {
        return EInputActionValueType::Axis3D;
    }

    bOutSuccess = false;
    return EInputActionValueType::Boolean;
}

bool TryParseSwizzleOrder(const FString& RequestedOrder, EInputAxisSwizzle& OutOrder)
{
    FString Normalized = RequestedOrder;
    Normalized.TrimStartAndEndInline();
    Normalized.ToUpperInline();
    if (Normalized == TEXT("YXZ"))
    {
        OutOrder = EInputAxisSwizzle::YXZ;
        return true;
    }
    if (Normalized == TEXT("ZYX"))
    {
        OutOrder = EInputAxisSwizzle::ZYX;
        return true;
    }
    if (Normalized == TEXT("XZY"))
    {
        OutOrder = EInputAxisSwizzle::XZY;
        return true;
    }
    if (Normalized == TEXT("YZX"))
    {
        OutOrder = EInputAxisSwizzle::YZX;
        return true;
    }
    if (Normalized == TEXT("ZXY"))
    {
        OutOrder = EInputAxisSwizzle::ZXY;
        return true;
    }

    return false;
}

bool TryGetScaleVector(const TSharedPtr<FJsonObject>& Params, const TCHAR* FieldName, FVector& OutScale)
{
    OutScale = FVector::OneVector;
    if (!Params.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(FieldName);
    if (!JsonValue.IsValid())
    {
        return false;
    }

    if (JsonValue->Type == EJson::Array)
    {
        const TArray<TSharedPtr<FJsonValue>>& Values = JsonValue->AsArray();
        if (Values.Num() >= 3)
        {
            OutScale.X = Values[0]->AsNumber();
            OutScale.Y = Values[1]->AsNumber();
            OutScale.Z = Values[2]->AsNumber();
            return true;
        }
    }
    else if (JsonValue->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> ScaleObject = JsonValue->AsObject();
        if (ScaleObject.IsValid())
        {
            double X = 1.0;
            double Y = 1.0;
            double Z = 1.0;
            ScaleObject->TryGetNumberField(TEXT("x"), X);
            ScaleObject->TryGetNumberField(TEXT("y"), Y);
            ScaleObject->TryGetNumberField(TEXT("z"), Z);
            OutScale = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
            return true;
        }
    }

    return false;
}

TSharedPtr<FJsonObject> BuildEnhancedMappingJson(const FString& MappingContextPath, const FString& InputActionPath, const FEnhancedActionKeyMapping& Mapping)
{
    TSharedPtr<FJsonObject> MappingJson = MakeShared<FJsonObject>();
    MappingJson->SetStringField(TEXT("mapping_context_path"), MappingContextPath);
    MappingJson->SetStringField(TEXT("input_action_path"), InputActionPath);
    MappingJson->SetStringField(TEXT("key"), Mapping.Key.ToString());
    MappingJson->SetStringField(TEXT("action_name"), Mapping.Action ? Mapping.Action->GetName() : FString());
    MappingJson->SetNumberField(TEXT("modifier_count"), Mapping.Modifiers.Num());

    TArray<TSharedPtr<FJsonValue>> ModifierArray;
    for (const UInputModifier* Modifier : Mapping.Modifiers)
    {
        if (!Modifier)
        {
            continue;
        }

        TSharedPtr<FJsonObject> ModifierJson = MakeShared<FJsonObject>();
        ModifierJson->SetStringField(TEXT("class"), Modifier->GetClass()->GetName());

        if (const UInputModifierNegate* Negate = Cast<UInputModifierNegate>(Modifier))
        {
            ModifierJson->SetStringField(TEXT("type"), TEXT("Negate"));
            ModifierJson->SetBoolField(TEXT("x"), Negate->bX);
            ModifierJson->SetBoolField(TEXT("y"), Negate->bY);
            ModifierJson->SetBoolField(TEXT("z"), Negate->bZ);
        }
        else if (const UInputModifierSwizzleAxis* Swizzle = Cast<UInputModifierSwizzleAxis>(Modifier))
        {
            ModifierJson->SetStringField(TEXT("type"), TEXT("SwizzleAxis"));
            ModifierJson->SetStringField(TEXT("order"), StaticEnum<EInputAxisSwizzle>()->GetNameStringByValue(static_cast<int64>(Swizzle->Order)));
        }
        else if (const UInputModifierScalar* Scalar = Cast<UInputModifierScalar>(Modifier))
        {
            ModifierJson->SetStringField(TEXT("type"), TEXT("Scalar"));
            TArray<TSharedPtr<FJsonValue>> ScalarValues;
            ScalarValues.Add(MakeShared<FJsonValueNumber>(Scalar->Scalar.X));
            ScalarValues.Add(MakeShared<FJsonValueNumber>(Scalar->Scalar.Y));
            ScalarValues.Add(MakeShared<FJsonValueNumber>(Scalar->Scalar.Z));
            ModifierJson->SetArrayField(TEXT("scalar"), ScalarValues);
        }

        ModifierArray.Add(MakeShared<FJsonValueObject>(ModifierJson));
    }

    MappingJson->SetArrayField(TEXT("modifiers"), ModifierArray);
    return MappingJson;
}

FString StripLeadingUFromTypeName(const FString& TypeName)
{
    if (TypeName.Len() > 1 && TypeName.StartsWith(TEXT("U")))
    {
        return TypeName.RightChop(1);
    }
    return TypeName;
}

bool IsGeneratedClassTransient(const UClass* Class)
{
    if (!Class)
    {
        return true;
    }

    const FString Name = Class->GetName();
    return Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_"));
}

bool ShouldIncludeDataAssetClass(const UClass* Class)
{
    if (!Class || !Class->IsChildOf(UDataAsset::StaticClass()) || IsGeneratedClassTransient(Class))
    {
        return false;
    }

    if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown))
    {
        return false;
    }

    return true;
}

UClass* ResolveDataAssetClassReference(const FString& DataAssetType)
{
    FString Reference = DataAssetType;
    Reference.TrimStartAndEndInline();
    if (Reference.IsEmpty())
    {
        return nullptr;
    }

    if (Reference.StartsWith(TEXT("/Script/")))
    {
        if (UClass* LoadedClass = LoadClass<UObject>(nullptr, *Reference))
        {
            return LoadedClass;
        }
    }

    if (Reference.StartsWith(TEXT("/Game/")))
    {
        if (UBlueprint* BlueprintAsset = LoadEditorAsset<UBlueprint>(Reference))
        {
            return BlueprintAsset->GeneratedClass;
        }

        if (UClass* AssetClass = LoadEditorAsset<UClass>(Reference))
        {
            return AssetClass;
        }
    }

    const FString NormalizedName = StripLeadingUFromTypeName(Reference).ToLower();
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        if (!Class)
        {
            continue;
        }

        const FString ClassName = StripLeadingUFromTypeName(Class->GetName()).ToLower();
        if (ClassName == NormalizedName || Class->GetPathName().Equals(Reference, ESearchCase::IgnoreCase))
        {
            return Class;
        }
    }

    FARFilter Filter;
    Filter.PackagePaths.Add(TEXT("/Game"));
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;

    TArray<FAssetData> BlueprintAssets;
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    AssetRegistryModule.Get().GetAssets(Filter, BlueprintAssets);

    for (const FAssetData& AssetData : BlueprintAssets)
    {
        if (!AssetData.AssetName.ToString().Equals(Reference, ESearchCase::IgnoreCase))
        {
            continue;
        }

        if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(AssetData.GetAsset()))
        {
            return BlueprintAsset->GeneratedClass;
        }
    }

    return nullptr;
}

TSharedPtr<FJsonObject> DescribePropertyTypeJson(const FProperty* Property)
{
    TSharedPtr<FJsonObject> PropertyJson = MakeShared<FJsonObject>();
    if (!Property)
    {
        return PropertyJson;
    }

    PropertyJson->SetStringField(TEXT("name"), Property->GetName());
    PropertyJson->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
    PropertyJson->SetStringField(TEXT("property_class"), Property->GetClass()->GetName());
    PropertyJson->SetBoolField(TEXT("instanced"), Property->HasAnyPropertyFlags(CPF_InstancedReference));
    PropertyJson->SetBoolField(TEXT("edit_inline"), Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance));
    PropertyJson->SetBoolField(TEXT("edit_anywhere"), Property->HasAnyPropertyFlags(CPF_Edit));
    PropertyJson->SetBoolField(TEXT("blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
    PropertyJson->SetBoolField(TEXT("blueprint_read_only"), Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
    PropertyJson->SetBoolField(TEXT("save_game"), Property->HasAnyPropertyFlags(CPF_SaveGame));
    PropertyJson->SetBoolField(TEXT("transient"), Property->HasAnyPropertyFlags(CPF_Transient));
    PropertyJson->SetBoolField(TEXT("config"), Property->HasAnyPropertyFlags(CPF_Config));

    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        PropertyJson->SetStringField(TEXT("type_category"), TEXT("struct"));
        if (StructProperty->Struct)
        {
            PropertyJson->SetStringField(TEXT("struct_name"), StructProperty->Struct->GetName());
            PropertyJson->SetStringField(TEXT("struct_path"), StructProperty->Struct->GetPathName());
        }
    }
    else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        PropertyJson->SetStringField(TEXT("type_category"), TEXT("enum"));
        if (const UEnum* Enum = EnumProperty->GetEnum())
        {
            PropertyJson->SetStringField(TEXT("enum_name"), Enum->GetName());
            PropertyJson->SetStringField(TEXT("enum_path"), Enum->GetPathName());
        }
    }
    else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty && ByteProperty->Enum)
    {
        PropertyJson->SetStringField(TEXT("type_category"), TEXT("enum"));
        PropertyJson->SetStringField(TEXT("enum_name"), ByteProperty->Enum->GetName());
        PropertyJson->SetStringField(TEXT("enum_path"), ByteProperty->Enum->GetPathName());
    }
    else if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        PropertyJson->SetStringField(TEXT("type_category"), TEXT("class"));
        if (ClassProperty->MetaClass)
        {
            PropertyJson->SetStringField(TEXT("meta_class_name"), ClassProperty->MetaClass->GetName());
            PropertyJson->SetStringField(TEXT("meta_class_path"), ClassProperty->MetaClass->GetPathName());
        }
    }
    else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        PropertyJson->SetStringField(TEXT("type_category"), TEXT("object"));
        if (ObjectProperty->PropertyClass)
        {
            PropertyJson->SetStringField(TEXT("object_class_name"), ObjectProperty->PropertyClass->GetName());
            PropertyJson->SetStringField(TEXT("object_class_path"), ObjectProperty->PropertyClass->GetPathName());
        }
    }
    else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        PropertyJson->SetStringField(TEXT("type_category"), TEXT("array"));
        PropertyJson->SetObjectField(TEXT("inner"), DescribePropertyTypeJson(ArrayProperty->Inner));
    }
    else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        PropertyJson->SetStringField(TEXT("type_category"), TEXT("set"));
        PropertyJson->SetObjectField(TEXT("element"), DescribePropertyTypeJson(SetProperty->ElementProp));
    }
    else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        PropertyJson->SetStringField(TEXT("type_category"), TEXT("map"));
        PropertyJson->SetObjectField(TEXT("key"), DescribePropertyTypeJson(MapProperty->KeyProp));
        PropertyJson->SetObjectField(TEXT("value"), DescribePropertyTypeJson(MapProperty->ValueProp));
    }
    else
    {
        PropertyJson->SetStringField(TEXT("type_category"), TEXT("scalar"));
    }

    return PropertyJson;
}

FString StripLeadingFFromTypeName(const FString& TypeName)
{
    if (TypeName.Len() > 1 && TypeName.StartsWith(TEXT("F")))
    {
        return TypeName.RightChop(1);
    }
    return TypeName;
}

UScriptStruct* ResolveScriptStructReference(const FString& TypeReference)
{
    FString Reference = TypeReference;
    Reference.TrimStartAndEndInline();
    if (Reference.IsEmpty())
    {
        return nullptr;
    }

    const TMap<FString, UScriptStruct*> CommonStructs = {
        {TEXT("vector"), TBaseStructure<FVector>::Get()},
        {TEXT("fvector"), TBaseStructure<FVector>::Get()},
        {TEXT("vector2d"), TBaseStructure<FVector2D>::Get()},
        {TEXT("fvector2d"), TBaseStructure<FVector2D>::Get()},
        {TEXT("vector4"), TBaseStructure<FVector4>::Get()},
        {TEXT("fvector4"), TBaseStructure<FVector4>::Get()},
        {TEXT("rotator"), TBaseStructure<FRotator>::Get()},
        {TEXT("frotator"), TBaseStructure<FRotator>::Get()},
        {TEXT("transform"), TBaseStructure<FTransform>::Get()},
        {TEXT("ftransform"), TBaseStructure<FTransform>::Get()},
        {TEXT("quat"), TBaseStructure<FQuat>::Get()},
        {TEXT("fquat"), TBaseStructure<FQuat>::Get()},
        {TEXT("color"), TBaseStructure<FColor>::Get()},
        {TEXT("fcolor"), TBaseStructure<FColor>::Get()},
        {TEXT("linearcolor"), TBaseStructure<FLinearColor>::Get()},
        {TEXT("flinearcolor"), TBaseStructure<FLinearColor>::Get()},
        {TEXT("guid"), TBaseStructure<FGuid>::Get()},
        {TEXT("fguid"), TBaseStructure<FGuid>::Get()},
        {TEXT("datetime"), TBaseStructure<FDateTime>::Get()},
        {TEXT("fdatetime"), TBaseStructure<FDateTime>::Get()},
        {TEXT("intvector"), TBaseStructure<FIntVector>::Get()},
        {TEXT("fintvector"), TBaseStructure<FIntVector>::Get()},
        {TEXT("intpoint"), TBaseStructure<FIntPoint>::Get()},
        {TEXT("fintpoint"), TBaseStructure<FIntPoint>::Get()}
    };

    if (const UScriptStruct* const* KnownStruct = CommonStructs.Find(Reference.ToLower()))
    {
        return const_cast<UScriptStruct*>(*KnownStruct);
    }

    if (Reference.Equals(TEXT("Timespan"), ESearchCase::IgnoreCase) || Reference.Equals(TEXT("FTimespan"), ESearchCase::IgnoreCase))
    {
        if (UScriptStruct* TimeSpanStruct = LoadObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.Timespan")))
        {
            return TimeSpanStruct;
        }
    }

    if (Reference.StartsWith(TEXT("/Script/")) || Reference.StartsWith(TEXT("/Game/")))
    {
        if (UScriptStruct* LoadedStruct = LoadObject<UScriptStruct>(nullptr, *Reference))
        {
            return LoadedStruct;
        }

        if (UScriptStruct* AssetStruct = LoadEditorAsset<UScriptStruct>(Reference))
        {
            return AssetStruct;
        }
    }

    const FString NormalizedName = StripLeadingFFromTypeName(Reference).ToLower();
    for (TObjectIterator<UScriptStruct> It; It; ++It)
    {
        UScriptStruct* Struct = *It;
        if (!Struct)
        {
            continue;
        }

        const FString StructName = StripLeadingFFromTypeName(Struct->GetName()).ToLower();
        if (StructName == NormalizedName || Struct->GetPathName().Equals(Reference, ESearchCase::IgnoreCase))
        {
            return Struct;
        }
    }

    return nullptr;
}

UEnum* ResolveEnumReference(const FString& TypeReference)
{
    FString Reference = TypeReference;
    Reference.TrimStartAndEndInline();
    if (Reference.IsEmpty())
    {
        return nullptr;
    }

    if (Reference.StartsWith(TEXT("/Script/")) || Reference.StartsWith(TEXT("/Game/")))
    {
        if (UEnum* LoadedEnum = LoadObject<UEnum>(nullptr, *Reference))
        {
            return LoadedEnum;
        }

        if (UEnum* AssetEnum = LoadEditorAsset<UEnum>(Reference))
        {
            return AssetEnum;
        }
    }

    const FString NormalizedName = Reference.ToLower();
    for (TObjectIterator<UEnum> It; It; ++It)
    {
        UEnum* Enum = *It;
        if (!Enum)
        {
            continue;
        }

        if (Enum->GetName().ToLower() == NormalizedName || Enum->GetPathName().Equals(Reference, ESearchCase::IgnoreCase))
        {
            return Enum;
        }
    }

    return nullptr;
}

FString GetStructFieldFriendlyName(const FStructVariableDescription& VarDesc)
{
    return !VarDesc.FriendlyName.IsEmpty() ? VarDesc.FriendlyName : VarDesc.VarName.ToString();
}

bool TryInferTypeHintFromJsonValue(const TSharedPtr<FJsonValue>& JsonValue, FString& OutTypeHint)
{
    OutTypeHint.Empty();
    if (!JsonValue.IsValid())
    {
        return false;
    }

    switch (JsonValue->Type)
    {
    case EJson::Boolean:
        OutTypeHint = TEXT("bool");
        return true;
    case EJson::Number:
    {
        const double NumericValue = JsonValue->AsNumber();
        OutTypeHint = FMath::IsNearlyEqual(NumericValue, FMath::RoundToDouble(NumericValue)) ? TEXT("int") : TEXT("float");
        return true;
    }
    case EJson::String:
        OutTypeHint = TEXT("string");
        return true;
    default:
        return false;
    }
}

bool ArePinTypesEquivalent(const FEdGraphPinType& A, const FEdGraphPinType& B)
{
    return A.PinCategory == B.PinCategory &&
        A.PinSubCategory == B.PinSubCategory &&
        A.PinSubCategoryObject == B.PinSubCategoryObject &&
        A.ContainerType == B.ContainerType &&
        A.PinValueType.TerminalCategory == B.PinValueType.TerminalCategory &&
        A.PinValueType.TerminalSubCategory == B.PinValueType.TerminalSubCategory &&
        A.PinValueType.TerminalSubCategoryObject == B.PinValueType.TerminalSubCategoryObject &&
        A.bIsReference == B.bIsReference &&
        A.bIsConst == B.bIsConst;
}

bool TryBuildPinTypeFromTypeHint(const FString& RawTypeHint, FEdGraphPinType& OutPinType, FString& OutError)
{
    FString TypeHint = RawTypeHint;
    TypeHint.TrimStartAndEndInline();
    if (TypeHint.IsEmpty())
    {
        OutError = TEXT("Type hint is empty");
        return false;
    }

    auto MakeRealType = [](const FName RealSubCategory)
    {
        FEdGraphPinType PinType = UBlueprintEditorLibrary::GetBasicTypeByName(UEdGraphSchema_K2::PC_Real);
        PinType.PinSubCategory = RealSubCategory;
        return PinType;
    };

    auto MakeEnumType = [](UEnum* Enum)
    {
        FEdGraphPinType PinType;
        PinType.PinCategory = UEdGraphSchema_K2::PC_Enum;
        PinType.PinSubCategoryObject = Enum;
        return PinType;
    };

    auto MakeSoftObjectType = [](UClass* Class)
    {
        FEdGraphPinType PinType;
        PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
        PinType.PinSubCategoryObject = Class;
        return PinType;
    };

    auto MakeSoftClassType = [](UClass* Class)
    {
        FEdGraphPinType PinType;
        PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
        PinType.PinSubCategoryObject = Class;
        return PinType;
    };

    if (TypeHint.StartsWith(TEXT("array:"), ESearchCase::IgnoreCase))
    {
        FEdGraphPinType InnerType;
        if (!TryBuildPinTypeFromTypeHint(TypeHint.RightChop(6), InnerType, OutError))
        {
            return false;
        }

        OutPinType = UBlueprintEditorLibrary::GetArrayType(InnerType);
        return true;
    }

    if (TypeHint.StartsWith(TEXT("set:"), ESearchCase::IgnoreCase))
    {
        FEdGraphPinType ElementType;
        if (!TryBuildPinTypeFromTypeHint(TypeHint.RightChop(4), ElementType, OutError))
        {
            return false;
        }

        OutPinType = UBlueprintEditorLibrary::GetSetType(ElementType);
        return true;
    }

    if (TypeHint.StartsWith(TEXT("map:"), ESearchCase::IgnoreCase))
    {
        const FString MapTypes = TypeHint.RightChop(4);
        FString KeyHint;
        FString ValueHint;
        if (!MapTypes.Split(TEXT(","), &KeyHint, &ValueHint))
        {
            OutError = FString::Printf(TEXT("Invalid map type hint: %s"), *TypeHint);
            return false;
        }

        FEdGraphPinType KeyType;
        FEdGraphPinType ValueType;
        if (!TryBuildPinTypeFromTypeHint(KeyHint, KeyType, OutError) ||
            !TryBuildPinTypeFromTypeHint(ValueHint, ValueType, OutError))
        {
            return false;
        }

        OutPinType = UBlueprintEditorLibrary::GetMapType(KeyType, ValueType);
        return true;
    }

    const FString NormalizedHint = TypeHint.ToLower();
    if (NormalizedHint == TEXT("bool") || NormalizedHint == TEXT("boolean"))
    {
        OutPinType = UBlueprintEditorLibrary::GetBasicTypeByName(UEdGraphSchema_K2::PC_Boolean);
        return true;
    }
    if (NormalizedHint == TEXT("int") || NormalizedHint == TEXT("int32") || NormalizedHint == TEXT("integer"))
    {
        OutPinType = UBlueprintEditorLibrary::GetBasicTypeByName(UEdGraphSchema_K2::PC_Int);
        return true;
    }
    if (NormalizedHint == TEXT("int64"))
    {
        OutPinType = UBlueprintEditorLibrary::GetBasicTypeByName(UEdGraphSchema_K2::PC_Int64);
        return true;
    }
    if (NormalizedHint == TEXT("float"))
    {
        OutPinType = MakeRealType(UEdGraphSchema_K2::PC_Float);
        return true;
    }
    if (NormalizedHint == TEXT("double"))
    {
        OutPinType = MakeRealType(UEdGraphSchema_K2::PC_Double);
        return true;
    }
    if (NormalizedHint == TEXT("string") || NormalizedHint == TEXT("fstring"))
    {
        OutPinType = UBlueprintEditorLibrary::GetBasicTypeByName(UEdGraphSchema_K2::PC_String);
        return true;
    }
    if (NormalizedHint == TEXT("name") || NormalizedHint == TEXT("fname"))
    {
        OutPinType = UBlueprintEditorLibrary::GetBasicTypeByName(UEdGraphSchema_K2::PC_Name);
        return true;
    }
    if (NormalizedHint == TEXT("text") || NormalizedHint == TEXT("ftext"))
    {
        OutPinType = UBlueprintEditorLibrary::GetBasicTypeByName(UEdGraphSchema_K2::PC_Text);
        return true;
    }
    if (NormalizedHint == TEXT("byte") || NormalizedHint == TEXT("uint8"))
    {
        OutPinType = UBlueprintEditorLibrary::GetBasicTypeByName(UEdGraphSchema_K2::PC_Byte);
        return true;
    }

    auto TryResolveStructType = [&](const FString& StructReference) -> bool
    {
        if (UScriptStruct* StructType = ResolveScriptStructReference(StructReference))
        {
            OutPinType = UBlueprintEditorLibrary::GetStructType(StructType);
            return true;
        }
        return false;
    };

    auto TryResolveEnumType = [&](const FString& EnumReference) -> bool
    {
        if (UEnum* EnumType = ResolveEnumReference(EnumReference))
        {
            OutPinType = MakeEnumType(EnumType);
            return true;
        }
        return false;
    };

    auto TryResolveClassType = [&](const FString& ClassReference, bool bClassReference, bool bSoftReference) -> bool
    {
        if (UClass* ClassType = ResolveDataAssetClassReference(ClassReference))
        {
            OutPinType = bSoftReference
                ? (bClassReference ? MakeSoftClassType(ClassType) : MakeSoftObjectType(ClassType))
                : (bClassReference ? UBlueprintEditorLibrary::GetClassReferenceType(ClassType) : UBlueprintEditorLibrary::GetObjectReferenceType(ClassType));
            return true;
        }
        return false;
    };

    if (TypeHint.StartsWith(TEXT("struct:"), ESearchCase::IgnoreCase))
    {
        if (TryResolveStructType(TypeHint.RightChop(7)))
        {
            return true;
        }
        OutError = FString::Printf(TEXT("Struct type not found: %s"), *TypeHint);
        return false;
    }

    if (TypeHint.StartsWith(TEXT("enum:"), ESearchCase::IgnoreCase))
    {
        if (TryResolveEnumType(TypeHint.RightChop(5)))
        {
            return true;
        }
        OutError = FString::Printf(TEXT("Enum type not found: %s"), *TypeHint);
        return false;
    }

    if (TypeHint.StartsWith(TEXT("class:"), ESearchCase::IgnoreCase))
    {
        if (TryResolveClassType(TypeHint.RightChop(6), true, false))
        {
            return true;
        }
        OutError = FString::Printf(TEXT("Class type not found: %s"), *TypeHint);
        return false;
    }

    if (TypeHint.StartsWith(TEXT("softclass:"), ESearchCase::IgnoreCase))
    {
        if (TryResolveClassType(TypeHint.RightChop(10), true, true))
        {
            return true;
        }
        OutError = FString::Printf(TEXT("Soft class type not found: %s"), *TypeHint);
        return false;
    }

    if (TypeHint.StartsWith(TEXT("object:"), ESearchCase::IgnoreCase))
    {
        if (TryResolveClassType(TypeHint.RightChop(7), false, false))
        {
            return true;
        }
        OutError = FString::Printf(TEXT("Object type not found: %s"), *TypeHint);
        return false;
    }

    if (TypeHint.StartsWith(TEXT("softobject:"), ESearchCase::IgnoreCase))
    {
        if (TryResolveClassType(TypeHint.RightChop(11), false, true))
        {
            return true;
        }
        OutError = FString::Printf(TEXT("Soft object type not found: %s"), *TypeHint);
        return false;
    }

    if (TryResolveStructType(TypeHint) || TryResolveEnumType(TypeHint) || TryResolveClassType(TypeHint, false, false))
    {
        return true;
    }

    OutError = FString::Printf(TEXT("Unsupported type hint: %s"), *TypeHint);
    return false;
}

FString BuildStructDefaultValueString(const TSharedPtr<FJsonValue>& JsonValue)
{
    if (!JsonValue.IsValid())
    {
        return FString();
    }

    switch (JsonValue->Type)
    {
    case EJson::Boolean:
        return JsonValue->AsBool() ? TEXT("true") : TEXT("false");
    case EJson::Number:
        return FString::SanitizeFloat(JsonValue->AsNumber());
    case EJson::String:
        return JsonValue->AsString();
    default:
        return FString();
    }
}

FString DecodeHtmlEntities(const FString& Input)
{
    FString Decoded = Input;
    Decoded.ReplaceInline(TEXT("&amp;"), TEXT("&"));
    Decoded.ReplaceInline(TEXT("&quot;"), TEXT("\""));
    Decoded.ReplaceInline(TEXT("&#x27;"), TEXT("'"));
    Decoded.ReplaceInline(TEXT("&#39;"), TEXT("'"));
    Decoded.ReplaceInline(TEXT("&apos;"), TEXT("'"));
    Decoded.ReplaceInline(TEXT("&lt;"), TEXT("<"));
    Decoded.ReplaceInline(TEXT("&gt;"), TEXT(">"));
    Decoded.ReplaceInline(TEXT("&nbsp;"), TEXT(" "));
    return Decoded;
}

FString StripHtmlTags(const FString& Input)
{
    FString Output = DecodeHtmlEntities(Input);
    static const FRegexPattern HtmlTagPattern(TEXT("<[^>]+>"));
    FRegexMatcher Matcher(HtmlTagPattern, Output);
    TArray<FIntPoint> MatchRanges;

    while (Matcher.FindNext())
    {
        MatchRanges.Emplace(Matcher.GetMatchBeginning(), Matcher.GetMatchEnding());
    }

    for (int32 Index = MatchRanges.Num() - 1; Index >= 0; --Index)
    {
        const FIntPoint& Range = MatchRanges[Index];
        Output.RemoveAt(Range.X, Range.Y - Range.X, EAllowShrinking::No);
    }

    Output.ReplaceInline(TEXT("\r"), TEXT(" "));
    Output.ReplaceInline(TEXT("\n"), TEXT(" "));
    Output.ReplaceInline(TEXT("\t"), TEXT(" "));
    while (Output.ReplaceInline(TEXT("  "), TEXT(" ")) > 0)
    {
    }

    Output.TrimStartAndEndInline();
    return Output;
}

FString CleanupFabTitle(const FString& Input)
{
    FString Title = StripHtmlTags(Input);
    Title.RemoveFromEnd(TEXT(" | Fab"), ESearchCase::IgnoreCase);
    Title.RemoveFromEnd(TEXT(" - fab.com"), ESearchCase::IgnoreCase);
    Title.TrimStartAndEndInline();
    return Title;
}

FString NormalizeSearchText(const FString& Input)
{
    FString Normalized;
    Normalized.Reserve(Input.Len());

    bool bLastWasSpace = true;
    for (const TCHAR Character : Input)
    {
        if (FChar::IsAlnum(Character))
        {
            Normalized.AppendChar(FChar::ToLower(Character));
            bLastWasSpace = false;
        }
        else if (!bLastWasSpace)
        {
            Normalized.AppendChar(TEXT(' '));
            bLastWasSpace = true;
        }
    }

    Normalized.TrimStartAndEndInline();
    return Normalized;
}

bool ContainsNormalizedToken(const FString& NormalizedText, const FString& Token)
{
    if (Token.IsEmpty())
    {
        return false;
    }

    const FString PaddedHaystack = FString::Printf(TEXT(" %s "), *NormalizedText);
    const FString PaddedNeedle = FString::Printf(TEXT(" %s "), *Token);
    return PaddedHaystack.Contains(PaddedNeedle, ESearchCase::CaseSensitive);
}

int32 CountTokenHits(const FString& NormalizedText, const TArray<FString>& Tokens)
{
    int32 Hits = 0;
    for (const FString& Token : Tokens)
    {
        Hits += ContainsNormalizedToken(NormalizedText, Token) ? 1 : 0;
    }

    return Hits;
}

FString ExtractDecodedQueryParam(const FString& Url, const FString& ParamName)
{
    const FString SearchToken = ParamName + TEXT("=");
    const int32 ParamIndex = Url.Find(SearchToken, ESearchCase::IgnoreCase);
    if (ParamIndex == INDEX_NONE)
    {
        return FString();
    }

    FString Value = Url.Mid(ParamIndex + SearchToken.Len());
    int32 AmpersandIndex = INDEX_NONE;
    if (Value.FindChar(TEXT('&'), AmpersandIndex))
    {
        Value = Value.Left(AmpersandIndex);
    }

    return FGenericPlatformHttp::UrlDecode(Value);
}

FString ResolveFabUrl(const FString& RawHref, const FString& RawDisplayUrl)
{
    FString Href = DecodeHtmlEntities(RawHref);
    if (Href.StartsWith(TEXT("//")))
    {
        Href = TEXT("https:") + Href;
    }

    FString FabUrl = ExtractDecodedQueryParam(Href, TEXT("uddg"));
    if (!FabUrl.IsEmpty())
    {
        return FabUrl;
    }

    FString DisplayUrl = StripHtmlTags(RawDisplayUrl);
    DisplayUrl.TrimStartAndEndInline();
    if (DisplayUrl.StartsWith(TEXT("www."), ESearchCase::IgnoreCase))
    {
        return TEXT("https://") + DisplayUrl;
    }

    if (DisplayUrl.StartsWith(TEXT("fab.com"), ESearchCase::IgnoreCase))
    {
        return TEXT("https://") + DisplayUrl;
    }

    if (DisplayUrl.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) ||
        DisplayUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
    {
        return DisplayUrl;
    }

    return FString();
}

FString ExtractFabListingId(const FString& FabUrl)
{
    static const FString ListingToken = TEXT("/listings/");
    const int32 ListingIndex = FabUrl.Find(ListingToken, ESearchCase::IgnoreCase);
    if (ListingIndex == INDEX_NONE)
    {
        return FString();
    }

    FString ListingId = FabUrl.Mid(ListingIndex + ListingToken.Len());

    int32 SlashIndex = INDEX_NONE;
    if (ListingId.FindChar(TEXT('/'), SlashIndex))
    {
        ListingId = ListingId.Left(SlashIndex);
    }

    int32 QueryIndex = INDEX_NONE;
    if (ListingId.FindChar(TEXT('?'), QueryIndex))
    {
        ListingId = ListingId.Left(QueryIndex);
    }

    int32 HashIndex = INDEX_NONE;
    if (ListingId.FindChar(TEXT('#'), HashIndex))
    {
        ListingId = ListingId.Left(HashIndex);
    }

    return ListingId;
}

bool IsLikelyFreeFabResult(const FString& Title, const FString& Snippet)
{
    const FString CombinedText = FString::Printf(TEXT(" %s "), *NormalizeSearchText(Title + TEXT(" ") + Snippet));
    return CombinedText.Contains(TEXT(" free "), ESearchCase::CaseSensitive) ||
           CombinedText.Contains(TEXT(" 100 free "), ESearchCase::CaseSensitive) ||
           CombinedText.Contains(TEXT(" for free "), ESearchCase::CaseSensitive);
}

int32 ScoreFabResult(const FFabSearchResult& Result, const FString& QueryName, const bool bPreferFree)
{
    const FString NormalizedQuery = NormalizeSearchText(QueryName);
    const FString NormalizedTitle = NormalizeSearchText(Result.Title);
    const FString NormalizedSnippet = NormalizeSearchText(Result.Snippet);

    TArray<FString> QueryTokens;
    NormalizedQuery.ParseIntoArrayWS(QueryTokens);

    const int32 TitleTokenHits = CountTokenHits(NormalizedTitle, QueryTokens);
    const int32 SnippetTokenHits = CountTokenHits(NormalizedSnippet, QueryTokens);

    int32 Score = 0;

    if (!NormalizedQuery.IsEmpty())
    {
        if (NormalizedTitle == NormalizedQuery)
        {
            Score += 350;
        }
        else if (NormalizedTitle.StartsWith(NormalizedQuery))
        {
            Score += 260;
        }
        else if (NormalizedTitle.Contains(NormalizedQuery))
        {
            Score += 200;
        }

        if (NormalizedSnippet.Contains(NormalizedQuery))
        {
            Score += 50;
        }
    }

    Score += TitleTokenHits * 35;
    Score += SnippetTokenHits * 12;

    if (QueryTokens.Num() > 0 && TitleTokenHits == QueryTokens.Num())
    {
        Score += 80;
    }

    if (Result.bIsFreeMatch)
    {
        Score += bPreferFree ? 45 : 10;
    }
    else if (bPreferFree)
    {
        Score -= 20;
    }

    Score -= Result.SearchRank * 2;
    return Score;
}

bool ExtractRegexCapture(const FString& Source, const FRegexPattern& Pattern, const int32 CaptureGroupIndex, FString& OutValue)
{
    FRegexMatcher Matcher(Pattern, Source);
    if (!Matcher.FindNext())
    {
        return false;
    }

    OutValue = Matcher.GetCaptureGroup(CaptureGroupIndex);
    return true;
}

bool FetchTextFromUrl(const FString& Url, FString& OutContent, FString& OutError)
{
    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
    if (!CompletionEvent)
    {
        OutError = TEXT("Failed to allocate HTTP completion event");
        return false;
    }

    ON_SCOPE_EXIT
    {
        FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
    };

    bool bCompleted = false;
    bool bSucceeded = false;
    int32 ResponseCode = 0;
    FString ResponseContent;
    FString FailureReason;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("User-Agent"), TEXT("Mozilla/5.0 (Windows NT 10.0; Win64; x64) UnrealMCP/1.0"));
    Request->SetHeader(TEXT("Accept"), TEXT("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"));
    Request->SetHeader(TEXT("Referer"), TEXT("https://duckduckgo.com/"));
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
    Request->OnProcessRequestComplete().BindLambda(
        [&bCompleted, &bSucceeded, &ResponseCode, &ResponseContent, &FailureReason, CompletionEvent](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            bCompleted = true;
            ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
            ResponseContent = Response.IsValid() ? Response->GetContentAsString() : FString();

            if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(ResponseCode))
            {
                bSucceeded = true;
            }
            else
            {
                FailureReason = ResponseCode > 0
                    ? FString::Printf(TEXT("HTTP %d"), ResponseCode)
                    : TEXT("Request failed without a valid response");
            }

            CompletionEvent->Trigger();
        });

    if (!Request->ProcessRequest())
    {
        OutError = TEXT("Failed to start HTTP request");
        return false;
    }

    if (!CompletionEvent->Wait(15000))
    {
        Request->CancelRequest();
        OutError = TEXT("Request timed out");
        return false;
    }

    if (!bCompleted || !bSucceeded)
    {
        OutError = FailureReason.IsEmpty() ? TEXT("Request failed") : FailureReason;
        return false;
    }

    OutContent = ResponseContent;
    return true;
}

TArray<FFabSearchResult> ParseDuckDuckGoFabResults(const FString& Html)
{
    static const FRegexPattern TitleLinkPattern(TEXT("<a[^>]*class=\"result__a\"[^>]*href=\"([^\"]+)\"[^>]*>([\\s\\S]*?)</a>"));
    static const FRegexPattern SnippetPattern(TEXT("<a[^>]*class=\"result__snippet\"[^>]*>([\\s\\S]*?)</a>"));
    static const FRegexPattern DisplayUrlPattern(TEXT("<a[^>]*class=\"result__url\"[^>]*>([\\s\\S]*?)</a>"));
    static const FRegexPattern DatePattern(TEXT("(20\\d{2}-\\d{2}-\\d{2}[^<\\s]*)"));

    TArray<FFabSearchResult> Results;
    TSet<FString> SeenFabUrls;

    int32 SearchOffset = 0;
    while (true)
    {
        const int32 BlockStart = Html.Find(TEXT("<div class=\"result results_links"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchOffset);
        if (BlockStart == INDEX_NONE)
        {
            break;
        }

        int32 BlockEnd = Html.Find(TEXT("<div class=\"result results_links"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BlockStart + 1);
        if (BlockEnd == INDEX_NONE)
        {
            BlockEnd = Html.Find(TEXT("<div class=\"nav-link\">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BlockStart);
        }
        if (BlockEnd == INDEX_NONE)
        {
            BlockEnd = Html.Len();
        }

        const FString Block = Html.Mid(BlockStart, BlockEnd - BlockStart);
        SearchOffset = BlockEnd;

        FRegexMatcher TitleMatcher(TitleLinkPattern, Block);
        if (!TitleMatcher.FindNext())
        {
            continue;
        }

        const FString RawHref = TitleMatcher.GetCaptureGroup(1);
        const FString RawTitle = TitleMatcher.GetCaptureGroup(2);

        FString RawSnippet;
        ExtractRegexCapture(Block, SnippetPattern, 1, RawSnippet);

        FString RawDisplayUrl;
        ExtractRegexCapture(Block, DisplayUrlPattern, 1, RawDisplayUrl);

        const FString FabUrl = ResolveFabUrl(RawHref, RawDisplayUrl);
        if (!FabUrl.Contains(TEXT("fab.com/listings/"), ESearchCase::IgnoreCase) || SeenFabUrls.Contains(FabUrl))
        {
            continue;
        }

        SeenFabUrls.Add(FabUrl);

        FFabSearchResult Result;
        Result.Title = CleanupFabTitle(RawTitle);
        Result.FabUrl = FabUrl;
        Result.ListingId = ExtractFabListingId(FabUrl);
        Result.Snippet = StripHtmlTags(RawSnippet);
        Result.SearchRank = Results.Num() + 1;
        Result.bIsFreeMatch = IsLikelyFreeFabResult(Result.Title, Result.Snippet);

        FString IndexedDate;
        if (ExtractRegexCapture(Block, DatePattern, 1, IndexedDate))
        {
            Result.IndexedDate = StripHtmlTags(IndexedDate);
        }

        Results.Add(MoveTemp(Result));
    }

    return Results;
}

TSharedPtr<FJsonObject> FabSearchResultToJson(const FFabSearchResult& Result)
{
    TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
    ResultJson->SetStringField(TEXT("title"), Result.Title);
    ResultJson->SetStringField(TEXT("fab_url"), Result.FabUrl);
    ResultJson->SetNumberField(TEXT("search_rank"), Result.SearchRank);
    ResultJson->SetNumberField(TEXT("match_score"), Result.MatchScore);
    ResultJson->SetBoolField(TEXT("is_free_match"), Result.bIsFreeMatch);

    if (!Result.ListingId.IsEmpty())
    {
        ResultJson->SetStringField(TEXT("listing_id"), Result.ListingId);
    }

    if (!Result.Snippet.IsEmpty())
    {
        ResultJson->SetStringField(TEXT("snippet"), Result.Snippet);
    }

    if (!Result.IndexedDate.IsEmpty())
    {
        ResultJson->SetStringField(TEXT("indexed_date"), Result.IndexedDate);
    }

    return ResultJson;
}

TSharedPtr<FJsonObject> BuildFabSearchResponse(const TSharedPtr<FJsonObject>& Params, const bool bReturnBestMatch)
{
    FString AssetName;
    if (!TryGetNonEmptyStringField(Params, TEXT("name"), AssetName) &&
        !TryGetNonEmptyStringField(Params, TEXT("query"), AssetName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    bool bIsFree = true;
    if (Params.IsValid() && !Params->TryGetBoolField(TEXT("isFree"), bIsFree))
    {
        Params->TryGetBoolField(TEXT("is_free"), bIsFree);
    }

    int32 Limit = 10;
    double LimitValue = 0.0;
    if (Params.IsValid() && Params->TryGetNumberField(TEXT("limit"), LimitValue))
    {
        Limit = FMath::Clamp(static_cast<int32>(LimitValue), 1, 10);
    }

    const FString SearchQuery = FString::Printf(TEXT("site:fab.com/listings %s%s"), *AssetName, bIsFree ? TEXT(" free") : TEXT(""));
    const FString SearchUrl = FString::Printf(TEXT("https://html.duckduckgo.com/html/?q=%s"), *FGenericPlatformHttp::UrlEncode(SearchQuery));

    FString HtmlContent;
    FString ErrorMessage;
    if (!FetchTextFromUrl(SearchUrl, HtmlContent, ErrorMessage))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Fab search request failed: %s"), *ErrorMessage));
    }

    TArray<FFabSearchResult> RankedResults = ParseDuckDuckGoFabResults(HtmlContent);
    for (FFabSearchResult& Result : RankedResults)
    {
        Result.MatchScore = ScoreFabResult(Result, AssetName, bIsFree);
    }

    RankedResults.Sort([](const FFabSearchResult& Left, const FFabSearchResult& Right)
    {
        if (Left.MatchScore != Right.MatchScore)
        {
            return Left.MatchScore > Right.MatchScore;
        }

        return Left.SearchRank < Right.SearchRank;
    });

    const int32 RawResultCount = RankedResults.Num();
    bool bUsedFreeFilterFallback = false;

    if (bIsFree)
    {
        TArray<FFabSearchResult> FreeResults;
        for (const FFabSearchResult& Result : RankedResults)
        {
            if (Result.bIsFreeMatch)
            {
                FreeResults.Add(Result);
            }
        }

        if (FreeResults.Num() > 0)
        {
            RankedResults = MoveTemp(FreeResults);
        }
        else
        {
            bUsedFreeFilterFallback = true;
        }
    }

    if (RankedResults.Num() > Limit)
    {
        RankedResults.SetNum(Limit);
    }

    TArray<TSharedPtr<FJsonValue>> ResultArray;
    ResultArray.Reserve(RankedResults.Num());
    for (const FFabSearchResult& Result : RankedResults)
    {
        ResultArray.Add(MakeShared<FJsonValueObject>(FabSearchResultToJson(Result)));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("command"), bReturnBestMatch ? TEXT("getassetsbynamefromfab") : TEXT("searchassetsbynamefromfab"));
    ResultObj->SetStringField(TEXT("name"), AssetName);
    ResultObj->SetStringField(TEXT("search_query"), SearchQuery);
    ResultObj->SetStringField(TEXT("source"), TEXT("duckduckgo_html"));
    ResultObj->SetBoolField(TEXT("is_free"), bIsFree);
    ResultObj->SetBoolField(TEXT("used_free_filter_fallback"), bUsedFreeFilterFallback);
    ResultObj->SetNumberField(TEXT("raw_result_count"), RawResultCount);
    ResultObj->SetNumberField(TEXT("count"), RankedResults.Num());
    ResultObj->SetArrayField(TEXT("results"), ResultArray);

    const bool bHasBestMatch = RankedResults.Num() > 0;
    ResultObj->SetBoolField(TEXT("has_best_match"), bHasBestMatch);
    if (bReturnBestMatch && bHasBestMatch)
    {
        ResultObj->SetObjectField(TEXT("best_match"), FabSearchResultToJson(RankedResults[0]));
    }

    if (!bHasBestMatch)
    {
        ResultObj->SetStringField(TEXT("message"), TEXT("No Fab assets matched the search criteria"));
    }

    return ResultObj;
}

struct FTodoItemRecord
{
    FString Priority;
    FString Status;
    FString Description;
};

FString GetTodoListStoragePath()
{
    return FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/.Aura/todo_list.txt"));
}

bool TryMakeProjectRelativePath(const FString& InPath, FString& OutRelativePath, FString& OutAbsolutePath, FString& OutError)
{
    FString CandidatePath = InPath;
    CandidatePath.TrimStartAndEndInline();
    if (CandidatePath.IsEmpty())
    {
        OutError = TEXT("Path cannot be empty");
        return false;
    }

    if (FPaths::IsRelative(CandidatePath))
    {
        OutAbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), CandidatePath);
    }
    else
    {
        OutAbsolutePath = FPaths::ConvertRelativePathToFull(CandidatePath);
    }

    FPaths::NormalizeFilename(OutAbsolutePath);
    FPaths::CollapseRelativeDirectories(OutAbsolutePath);

    FString NormalizedProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeFilename(NormalizedProjectDir);

    OutRelativePath = OutAbsolutePath;
    if (!FPaths::MakePathRelativeTo(OutRelativePath, *NormalizedProjectDir))
    {
        OutError = FString::Printf(TEXT("Path must stay inside the project directory: %s"), *InPath);
        return false;
    }

    FPaths::NormalizeFilename(OutRelativePath);
    return true;
}

bool IsAllowedSavedAuraExtension(const FString& RelativePath)
{
    const FString Extension = FPaths::GetExtension(RelativePath, true).ToLower();
    static const TSet<FString> AllowedExtensions = {
        TEXT(".txt"),
        TEXT(".md"),
        TEXT(".csv"),
        TEXT(".xml"),
        TEXT(".json"),
        TEXT(".ini"),
        TEXT(".yaml"),
        TEXT(".yml"),
        TEXT(".toml"),
        TEXT(".cfg"),
        TEXT(".conf"),
        TEXT(".log"),
        TEXT(".py")
    };

    return AllowedExtensions.Contains(Extension);
}

bool ValidateCreateTextFilePath(const FString& RelativePath, FString& OutError)
{
    if (RelativePath.StartsWith(TEXT("Saved/.Aura/plans/"), ESearchCase::IgnoreCase))
    {
        OutError = TEXT("create_text_file cannot write into Saved/.Aura/plans/");
        return false;
    }

    if (RelativePath.StartsWith(TEXT("Saved/.Aura/"), ESearchCase::IgnoreCase))
    {
        if (!IsAllowedSavedAuraExtension(RelativePath))
        {
            OutError = FString::Printf(TEXT("Unsupported file extension for Saved/.Aura path: %s"), *RelativePath);
            return false;
        }

        return true;
    }

    if (RelativePath.StartsWith(TEXT("Config/"), ESearchCase::IgnoreCase))
    {
        if (!RelativePath.EndsWith(TEXT(".ini"), ESearchCase::IgnoreCase))
        {
            OutError = TEXT("Config files must use the .ini extension");
            return false;
        }

        return true;
    }

    if (RelativePath.StartsWith(TEXT("Content/Python/"), ESearchCase::IgnoreCase))
    {
        if (!RelativePath.EndsWith(TEXT(".py"), ESearchCase::IgnoreCase))
        {
            OutError = TEXT("Content/Python files must use the .py extension");
            return false;
        }

        return true;
    }

    OutError = TEXT("Path is not allowed. Use Saved/.Aura/**, Config/*.ini, or Content/Python/**.py");
    return false;
}

bool ValidateEditTextFilePath(const FString& RelativePath, FString& OutError)
{
    if (RelativePath.StartsWith(TEXT("Saved/.Aura/"), ESearchCase::IgnoreCase))
    {
        return true;
    }

    if (RelativePath.StartsWith(TEXT("Config/"), ESearchCase::IgnoreCase) &&
        RelativePath.EndsWith(TEXT(".ini"), ESearchCase::IgnoreCase))
    {
        return true;
    }

    OutError = TEXT("Path is not allowed. edit_text_file currently supports Saved/.Aura/** and Config/*.ini");
    return false;
}

bool ParseTodoLine(const FString& RawLine, FTodoItemRecord& OutItem, FString& OutError)
{
    FString Line = RawLine;
    Line.TrimStartAndEndInline();

    if (Line.IsEmpty())
    {
        OutError = TEXT("Todo line cannot be empty");
        return false;
    }

    TArray<FString> Parts;
    Line.ParseIntoArray(Parts, TEXT("|"), false);
    if (Parts.Num() < 3)
    {
        OutError = FString::Printf(TEXT("Todo line must use 'priority|status|description': %s"), *RawLine);
        return false;
    }

    OutItem.Priority = Parts[0].TrimStartAndEnd();
    OutItem.Status = Parts[1].TrimStartAndEnd();
    Parts.RemoveAt(0, 2);
    OutItem.Description = FString::Join(Parts, TEXT("|")).TrimStartAndEnd();

    static const TSet<FString> AllowedPriorities = {TEXT("low"), TEXT("medium"), TEXT("high")};
    static const TSet<FString> AllowedStatuses = {TEXT("not_started"), TEXT("in_progress"), TEXT("completed")};

    if (!AllowedPriorities.Contains(OutItem.Priority))
    {
        OutError = FString::Printf(TEXT("Invalid todo priority: %s"), *OutItem.Priority);
        return false;
    }

    if (!AllowedStatuses.Contains(OutItem.Status))
    {
        OutError = FString::Printf(TEXT("Invalid todo status: %s"), *OutItem.Status);
        return false;
    }

    if (OutItem.Description.IsEmpty())
    {
        OutError = TEXT("Todo description cannot be empty");
        return false;
    }

    return true;
}

FString TodoItemToLine(const FTodoItemRecord& Item)
{
    return FString::Printf(TEXT("%s|%s|%s"), *Item.Priority, *Item.Status, *Item.Description);
}

TSharedPtr<FJsonObject> TodoItemToJson(const FTodoItemRecord& Item, int32 Index)
{
    TSharedPtr<FJsonObject> ItemObj = MakeShared<FJsonObject>();
    ItemObj->SetNumberField(TEXT("index"), Index);
    ItemObj->SetStringField(TEXT("priority"), Item.Priority);
    ItemObj->SetStringField(TEXT("status"), Item.Status);
    ItemObj->SetStringField(TEXT("description"), Item.Description);
    return ItemObj;
}

TSharedPtr<FJsonObject> BuildTodoListResponse(const TArray<FTodoItemRecord>& Items, bool bSuccess = true)
{
    TArray<TSharedPtr<FJsonValue>> RawLines;
    TArray<TSharedPtr<FJsonValue>> ParsedItems;
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        RawLines.Add(MakeShared<FJsonValueString>(TodoItemToLine(Items[Index])));
        ParsedItems.Add(MakeShared<FJsonValueObject>(TodoItemToJson(Items[Index], Index)));
    }

    TSharedPtr<FJsonObject> ContentEntry = MakeShared<FJsonObject>();
    ContentEntry->SetStringField(TEXT("type"), TEXT("todolist"));
    ContentEntry->SetArrayField(TEXT("data"), ParsedItems);

    TArray<TSharedPtr<FJsonValue>> ContentArray;
    ContentArray.Add(MakeShared<FJsonValueObject>(ContentEntry));

    int32 CompletedCount = 0;
    int32 InProgressCount = 0;
    int32 NotStartedCount = 0;
    for (const FTodoItemRecord& Item : Items)
    {
        if (Item.Status == TEXT("completed"))
        {
            ++CompletedCount;
        }
        else if (Item.Status == TEXT("in_progress"))
        {
            ++InProgressCount;
        }
        else
        {
            ++NotStartedCount;
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), bSuccess);
    ResultObj->SetArrayField(TEXT("todo_list_data"), RawLines);
    ResultObj->SetArrayField(TEXT("items"), ParsedItems);
    ResultObj->SetArrayField(TEXT("content"), ContentArray);
    ResultObj->SetNumberField(TEXT("count"), Items.Num());
    ResultObj->SetNumberField(TEXT("completed_count"), CompletedCount);
    ResultObj->SetNumberField(TEXT("in_progress_count"), InProgressCount);
    ResultObj->SetNumberField(TEXT("not_started_count"), NotStartedCount);
    ResultObj->SetStringField(TEXT("storage_path"), GetTodoListStoragePath());
    return ResultObj;
}

bool LoadTodoItems(TArray<FTodoItemRecord>& OutItems, FString& OutError)
{
    OutItems.Reset();

    FString FileContent;
    const FString StoragePath = GetTodoListStoragePath();
    if (!FPaths::FileExists(StoragePath))
    {
        return true;
    }

    if (!FFileHelper::LoadFileToString(FileContent, *StoragePath))
    {
        OutError = FString::Printf(TEXT("Failed to read todo list file: %s"), *StoragePath);
        return false;
    }

    TArray<FString> Lines;
    FileContent.ParseIntoArrayLines(Lines, false);
    for (const FString& Line : Lines)
    {
        FString TrimmedLine = Line;
        TrimmedLine.TrimStartAndEndInline();
        if (TrimmedLine.IsEmpty())
        {
            continue;
        }

        FTodoItemRecord Item;
        if (!ParseTodoLine(TrimmedLine, Item, OutError))
        {
            return false;
        }

        OutItems.Add(Item);
    }

    return true;
}

bool SaveTodoItems(const TArray<FTodoItemRecord>& Items, FString& OutError)
{
    TArray<FString> Lines;
    Lines.Reserve(Items.Num());
    for (const FTodoItemRecord& Item : Items)
    {
        Lines.Add(TodoItemToLine(Item));
    }

    const FString StoragePath = GetTodoListStoragePath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(StoragePath), true);

    const FString FileContent = FString::Join(Lines, TEXT("\n")) + (Lines.Num() > 0 ? TEXT("\n") : TEXT(""));
    if (!FFileHelper::SaveStringToFile(FileContent, *StoragePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        OutError = FString::Printf(TEXT("Failed to write todo list file: %s"), *StoragePath);
        return false;
    }

    return true;
}

bool TryDecodePriorityCode(const FString& Code, FString& OutPriority)
{
    const FString Normalized = Code.TrimStartAndEnd().ToLower();
    if (Normalized.IsEmpty())
    {
        return false;
    }

    if (Normalized == TEXT("l") || Normalized == TEXT("low"))
    {
        OutPriority = TEXT("low");
        return true;
    }
    if (Normalized == TEXT("m") || Normalized == TEXT("medium"))
    {
        OutPriority = TEXT("medium");
        return true;
    }
    if (Normalized == TEXT("h") || Normalized == TEXT("high"))
    {
        OutPriority = TEXT("high");
        return true;
    }

    return false;
}

bool TryDecodeStatusCode(const FString& Code, FString& OutStatus)
{
    const FString Normalized = Code.TrimStartAndEnd().ToLower();
    if (Normalized.IsEmpty())
    {
        return false;
    }

    if (Normalized == TEXT("ns") || Normalized == TEXT("not_started"))
    {
        OutStatus = TEXT("not_started");
        return true;
    }
    if (Normalized == TEXT("ip") || Normalized == TEXT("in_progress"))
    {
        OutStatus = TEXT("in_progress");
        return true;
    }
    if (Normalized == TEXT("c") || Normalized == TEXT("completed"))
    {
        OutStatus = TEXT("completed");
        return true;
    }

    return false;
}

bool ParseUnifiedDiffHunkHeader(const FString& Line, int32& OutStartLine, FString& OutError)
{
    const FRegexPattern Pattern(TEXT("^@@ -([0-9]+)(?:,[0-9]+)? \\+([0-9]+)(?:,[0-9]+)? @@"));
    FRegexMatcher Matcher(Pattern, Line);
    if (!Matcher.FindNext())
    {
        OutError = FString::Printf(TEXT("Invalid unified diff hunk header: %s"), *Line);
        return false;
    }

    OutStartLine = FCString::Atoi(*Matcher.GetCaptureGroup(1));
    return true;
}

bool ApplyUnifiedDiffPatch(const FString& OriginalContent, const FString& PatchText, FString& OutPatchedContent, FString& OutError)
{
    TArray<FString> OriginalLines;
    OriginalContent.ParseIntoArrayLines(OriginalLines, false);
    const bool bOriginalHadTrailingNewline = OriginalContent.EndsWith(TEXT("\n"));

    TArray<FString> PatchLines;
    PatchText.ParseIntoArrayLines(PatchLines, false);

    TArray<FString> ResultLines;
    ResultLines.Reserve(OriginalLines.Num() + PatchLines.Num());

    int32 OriginalIndex = 0;
    bool bSawHunk = false;

    for (int32 LineIndex = 0; LineIndex < PatchLines.Num();)
    {
        const FString& PatchLine = PatchLines[LineIndex];
        if (PatchLine.StartsWith(TEXT("***")) ||
            PatchLine.StartsWith(TEXT("--- ")) ||
            PatchLine.StartsWith(TEXT("+++ ")) ||
            PatchLine == TEXT("\\ No newline at end of file"))
        {
            ++LineIndex;
            continue;
        }

        if (!PatchLine.StartsWith(TEXT("@@")))
        {
            ++LineIndex;
            continue;
        }

        int32 HunkStartLine = 0;
        if (!ParseUnifiedDiffHunkHeader(PatchLine, HunkStartLine, OutError))
        {
            return false;
        }

        bSawHunk = true;
        const int32 HunkStartIndex = FMath::Max(0, HunkStartLine - 1);
        if (HunkStartIndex < OriginalIndex || HunkStartIndex > OriginalLines.Num())
        {
            OutError = FString::Printf(TEXT("Patch hunk start is out of range: %d"), HunkStartLine);
            return false;
        }

        while (OriginalIndex < HunkStartIndex)
        {
            ResultLines.Add(OriginalLines[OriginalIndex++]);
        }

        ++LineIndex;
        while (LineIndex < PatchLines.Num())
        {
            const FString& HunkLine = PatchLines[LineIndex];
            if (HunkLine.StartsWith(TEXT("@@")))
            {
                break;
            }
            if (HunkLine.StartsWith(TEXT("*** End Patch")))
            {
                ++LineIndex;
                break;
            }
            if (HunkLine == TEXT("\\ No newline at end of file"))
            {
                ++LineIndex;
                continue;
            }
            if (HunkLine.IsEmpty())
            {
                OutError = TEXT("Unified diff hunk contains an empty line without a prefix");
                return false;
            }

            const TCHAR Prefix = HunkLine[0];
            const FString Payload = HunkLine.Mid(1);

            if (Prefix == TEXT(' '))
            {
                if (!OriginalLines.IsValidIndex(OriginalIndex) || OriginalLines[OriginalIndex] != Payload)
                {
                    OutError = FString::Printf(TEXT("Patch context mismatch near line %d"), OriginalIndex + 1);
                    return false;
                }

                ResultLines.Add(OriginalLines[OriginalIndex++]);
            }
            else if (Prefix == TEXT('-'))
            {
                if (!OriginalLines.IsValidIndex(OriginalIndex) || OriginalLines[OriginalIndex] != Payload)
                {
                    OutError = FString::Printf(TEXT("Patch removal mismatch near line %d"), OriginalIndex + 1);
                    return false;
                }

                ++OriginalIndex;
            }
            else if (Prefix == TEXT('+'))
            {
                ResultLines.Add(Payload);
            }
            else
            {
                OutError = FString::Printf(TEXT("Unsupported unified diff line prefix '%c'"), Prefix);
                return false;
            }

            ++LineIndex;
        }
    }

    if (!bSawHunk)
    {
        OutError = TEXT("apply_patch edit requires at least one unified diff hunk");
        return false;
    }

    while (OriginalIndex < OriginalLines.Num())
    {
        ResultLines.Add(OriginalLines[OriginalIndex++]);
    }

    OutPatchedContent = FString::Join(ResultLines, TEXT("\n"));
    if (bOriginalHadTrailingNewline && !OutPatchedContent.EndsWith(TEXT("\n")))
    {
        OutPatchedContent += TEXT("\n");
    }

    return true;
}

bool ValidatePlanFileEdit(const FString& OriginalContent, const FString& UpdatedContent, FString& OutError)
{
    TArray<FString> OriginalLines;
    TArray<FString> UpdatedLines;
    OriginalContent.ParseIntoArrayLines(OriginalLines, false);
    UpdatedContent.ParseIntoArrayLines(UpdatedLines, false);

    if (OriginalLines.Num() != UpdatedLines.Num())
    {
        OutError = TEXT("Plan files may only mark existing checklist items complete");
        return false;
    }

    for (int32 Index = 0; Index < OriginalLines.Num(); ++Index)
    {
        if (OriginalLines[Index] == UpdatedLines[Index])
        {
            continue;
        }

        FString AllowedReplacement = OriginalLines[Index];
        AllowedReplacement.ReplaceInline(TEXT("- [ ]"), TEXT("- [x]"));
        if (UpdatedLines[Index] != AllowedReplacement)
        {
            OutError = TEXT("Plan files may only change '- [ ]' to '- [x]'");
            return false;
        }
    }

    return true;
}
}

FEpicUnrealMCPEditorCommands::FEpicUnrealMCPEditorCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    // Actor manipulation commands
    if (CommandType == TEXT("get_actors_in_level"))
    {
        return HandleGetActorsInLevel(Params);
    }
    else if (CommandType == TEXT("find_actors_by_name"))
    {
        return HandleFindActorsByName(Params);
    }
    else if (CommandType == TEXT("spawn_actor"))
    {
        return HandleSpawnActor(Params);
    }
    else if (CommandType == TEXT("delete_actor"))
    {
        return HandleDeleteActor(Params);
    }
    else if (CommandType == TEXT("set_actor_transform"))
    {
        return HandleSetActorTransform(Params);
    }
    else if (CommandType == TEXT("create_input_action_key") || CommandType == TEXT("add_input_action_key"))
    {
        return HandleCreateInputActionKey(Params);
    }
    else if (CommandType == TEXT("create_input_actions"))
    {
        return HandleCreateInputActions(Params);
    }
    else if (CommandType == TEXT("add_input_action_to_mapping_context"))
    {
        return HandleAddInputActionToMappingContext(Params);
    }
    else if (CommandType == TEXT("create_gameplay_tag"))
    {
        return HandleCreateGameplayTag(Params);
    }
    else if (CommandType == TEXT("add_or_replace_rows_in_data_table"))
    {
        return HandleAddOrReplaceRowsInDataTable(Params);
    }
    else if (CommandType == TEXT("remove_rows_from_data_table"))
    {
        return HandleRemoveRowsFromDataTable(Params);
    }
    else if (CommandType == TEXT("get_data_asset_types"))
    {
        return HandleGetDataAssetTypes(Params);
    }
    else if (CommandType == TEXT("get_data_asset_type_info"))
    {
        return HandleGetDataAssetTypeInfo(Params);
    }
    else if (CommandType == TEXT("edit_enumeration"))
    {
        return HandleEditEnumeration(Params);
    }
    else if (CommandType == TEXT("edit_structure"))
    {
        return HandleEditStructure(Params);
    }
    else if (CommandType == TEXT("create_text_file"))
    {
        return HandleCreateTextFile(Params);
    }
    else if (CommandType == TEXT("edit_text_file"))
    {
        return HandleEditTextFile(Params);
    }
    else if (CommandType == TEXT("create_new_todo_list"))
    {
        return HandleCreateNewTodoList(Params);
    }
    else if (CommandType == TEXT("get_todo_list"))
    {
        return HandleGetTodoList(Params);
    }
    else if (CommandType == TEXT("edit_todo_list"))
    {
        return HandleEditTodoList(Params);
    }
    else if (CommandType == TEXT("searchassetsbynamefromfab"))
    {
        return HandleSearchAssetsByNameFromFab(Params);
    }
    else if (CommandType == TEXT("getassetsbynamefromfab"))
    {
        return HandleGetAssetsByNameFromFab(Params);
    }
    else if (CommandType == TEXT("save_project") || CommandType == TEXT("save_all"))
    {
        return HandleSaveProject(Params);
    }
    // Blueprint actor spawning
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown editor command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> ActorArray;
    for (AActor* Actor : AllActors)
    {
        if (Actor)
        {
            ActorArray.Add(FEpicUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), ActorArray);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern;
    if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pattern' parameter"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> MatchingActors;
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName().Contains(Pattern))
        {
            MatchingActors.Add(FEpicUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorType;
    if (!Params->TryGetStringField(TEXT("type"), ActorType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    // Get actor name (required parameter)
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Get optional transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    // Create the actor based on type
    AActor* NewActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();

    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    // Check if an actor with this name already exists
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *ActorName));
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;

    if (ActorType == TEXT("StaticMeshActor"))
    {
        AStaticMeshActor* NewMeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
        if (NewMeshActor)
        {
            // Check for an optional static_mesh parameter to assign a mesh
            FString MeshPath;
            if (Params->TryGetStringField(TEXT("static_mesh"), MeshPath))
            {
                UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
                if (Mesh)
                {
                    NewMeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("Could not find static mesh at path: %s"), *MeshPath);
                }
            }
        }
        NewActor = NewMeshActor;
    }
    else if (ActorType == TEXT("PointLight"))
    {
        NewActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("SpotLight"))
    {
        NewActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("DirectionalLight"))
    {
        NewActor = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("CameraActor"))
    {
        NewActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Location, Rotation, SpawnParams);
    }
    else
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown actor type: %s"), *ActorType));
    }

    if (NewActor)
    {
        // Set scale (since SpawnActor only takes location and rotation)
        FTransform Transform = NewActor->GetTransform();
        Transform.SetScale3D(Scale);
        NewActor->SetActorTransform(Transform);

        // Return the created actor's details
        return FEpicUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create actor"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            // Store actor info before deletion for the response
            TSharedPtr<FJsonObject> ActorInfo = FEpicUnrealMCPCommonUtils::ActorToJsonObject(Actor);
            
            // Delete the actor
            Actor->Destroy();
            
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetObjectField(TEXT("deleted_actor"), ActorInfo);
            return ResultObj;
        }
    }
    
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get transform parameters
    FTransform NewTransform = TargetActor->GetTransform();

    if (Params->HasField(TEXT("location")))
    {
        NewTransform.SetLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        NewTransform.SetRotation(FQuat(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
    }
    if (Params->HasField(TEXT("scale")))
    {
        NewTransform.SetScale3D(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
    }

    // Set the new transform
    TargetActor->SetActorTransform(NewTransform);

    // Return updated actor info
    return FEpicUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCreateInputActionKey(const TSharedPtr<FJsonObject>& Params)
{
    FString ActionNameString;
    if (!TryGetNonEmptyStringField(Params, TEXT("action_name"), ActionNameString) &&
        !TryGetNonEmptyStringField(Params, TEXT("action"), ActionNameString) &&
        !TryGetNonEmptyStringField(Params, TEXT("name"), ActionNameString))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'action_name' parameter"));
    }

    FString KeyName;
    if (!TryGetNonEmptyStringField(Params, TEXT("key"), KeyName) &&
        !TryGetNonEmptyStringField(Params, TEXT("key_name"), KeyName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'key' parameter"));
    }

    const FName ActionName(*ActionNameString);
    const FKey ResolvedKey = ResolveInputKey(KeyName);
    if (!ResolvedKey.IsValid() || !ResolvedKey.IsBindableToActions())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid or non-bindable input key: %s"), *KeyName));
    }

    UInputSettings* InputSettings = UInputSettings::GetInputSettings();
    if (!InputSettings)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to access input settings"));
    }

    FInputActionKeyMapping NewMapping;
    NewMapping.ActionName = ActionName;
    NewMapping.Key = ResolvedKey;
    NewMapping.bShift = GetOptionalBoolField(Params, TEXT("shift"));
    NewMapping.bCtrl = GetOptionalBoolField(Params, TEXT("ctrl"));
    NewMapping.bAlt = GetOptionalBoolField(Params, TEXT("alt"));
    NewMapping.bCmd = GetOptionalBoolField(Params, TEXT("cmd"));

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("command"), TEXT("create_input_action_key"));
    ResultObj->SetObjectField(TEXT("mapping"), ActionMappingToJson(NewMapping));

    if (HasExactActionMapping(InputSettings, NewMapping))
    {
        TArray<FInputActionKeyMapping> ExistingMappings;
        InputSettings->GetActionMappingByName(ActionName, ExistingMappings);

        ResultObj->SetBoolField(TEXT("success"), true);
        ResultObj->SetBoolField(TEXT("created"), false);
        ResultObj->SetBoolField(TEXT("already_exists"), true);
        ResultObj->SetNumberField(TEXT("mapping_count_for_action"), ExistingMappings.Num());
        ResultObj->SetStringField(TEXT("message"), TEXT("Input action key mapping already exists"));
        return ResultObj;
    }

    InputSettings->Modify();
    InputSettings->AddActionMapping(NewMapping, false);
    InputSettings->SaveKeyMappings();
    InputSettings->ForceRebuildKeymaps();
    InputSettings->SaveConfig();

    TArray<FInputActionKeyMapping> UpdatedMappings;
    InputSettings->GetActionMappingByName(ActionName, UpdatedMappings);

    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("created"), true);
    ResultObj->SetBoolField(TEXT("already_exists"), false);
    ResultObj->SetNumberField(TEXT("mapping_count_for_action"), UpdatedMappings.Num());
    ResultObj->SetStringField(TEXT("message"), TEXT("Input action key mapping created"));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCreateInputActions(const TSharedPtr<FJsonObject>& Params)
{
    TArray<TSharedPtr<FJsonValue>> InputActionSpecs;
    if (!TryGetJsonArrayFieldFlexible(Params, TEXT("input_actions"), InputActionSpecs) || InputActionSpecs.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing or empty 'input_actions' parameter"));
    }

    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Errors;

    for (int32 Index = 0; Index < InputActionSpecs.Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& SpecValue = InputActionSpecs[Index];
        if (!SpecValue.IsValid() || SpecValue->Type != EJson::Object)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("input_actions[%d] must be an object"), Index)));
            continue;
        }

        const TSharedPtr<FJsonObject> Spec = SpecValue->AsObject();
        FString ActionName;
        if (!TryGetNonEmptyStringField(Spec, TEXT("action_name"), ActionName))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("input_actions[%d] is missing 'action_name'"), Index)));
            continue;
        }

        FString FolderPath = TEXT("/Game/Input");
        TryGetNonEmptyStringField(Spec, TEXT("package_path"), FolderPath);

        FString NormalizeError;
        if (!NormalizeGameFolderPath(FolderPath, FolderPath, NormalizeError))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("input_actions[%d]: %s"), Index, *NormalizeError)));
            continue;
        }

        bool bValueTypeParsed = false;
        FString RequestedValueType = TEXT("Digital");
        Spec->TryGetStringField(TEXT("value_type"), RequestedValueType);
        const EInputActionValueType ValueType = ParseInputActionValueType(RequestedValueType, bValueTypeParsed);
        if (!bValueTypeParsed)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("input_actions[%d]: unsupported value_type '%s'"), Index, *RequestedValueType)));
            continue;
        }

        const FString AssetPath = BuildAssetPath(FolderPath, ActionName);
        TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
        ItemResult->SetStringField(TEXT("action_name"), ActionName);
        ItemResult->SetStringField(TEXT("asset_path"), AssetPath);

        if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
        {
            if (UInputAction* ExistingAction = LoadEditorAsset<UInputAction>(AssetPath))
            {
                ItemResult->SetBoolField(TEXT("success"), true);
                ItemResult->SetBoolField(TEXT("created"), false);
                ItemResult->SetBoolField(TEXT("already_exists"), true);
                ItemResult->SetStringField(TEXT("full_path"), ExistingAction->GetPathName());
                Results.Add(MakeShared<FJsonValueObject>(ItemResult));
            }
            else
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("input_actions[%d]: failed to load existing asset '%s'"), Index, *AssetPath)));
            }
            continue;
        }

        UPackage* Package = CreatePackage(*AssetPath);
        if (!Package)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("input_actions[%d]: failed to create package '%s'"), Index, *AssetPath)));
            continue;
        }

        UInputAction* NewInputAction = NewObject<UInputAction>(Package, *ActionName, RF_Public | RF_Standalone);
        if (!NewInputAction)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("input_actions[%d]: failed to create asset '%s'"), Index, *AssetPath)));
            continue;
        }

        NewInputAction->ValueType = ValueType;
        Spec->TryGetBoolField(TEXT("consume_input"), NewInputAction->bConsumeInput);
        Spec->TryGetBoolField(TEXT("trigger_when_paused"), NewInputAction->bTriggerWhenPaused);

        FAssetRegistryModule::AssetCreated(NewInputAction);
        Package->MarkPackageDirty();
        UEditorAssetLibrary::SaveLoadedAsset(NewInputAction);

        ItemResult->SetBoolField(TEXT("success"), true);
        ItemResult->SetBoolField(TEXT("created"), true);
        ItemResult->SetBoolField(TEXT("already_exists"), false);
        ItemResult->SetStringField(TEXT("full_path"), NewInputAction->GetPathName());
        ItemResult->SetStringField(TEXT("value_type"), StaticEnum<EInputActionValueType>()->GetNameStringByValue(static_cast<int64>(NewInputAction->ValueType)));
        Results.Add(MakeShared<FJsonValueObject>(ItemResult));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0);
    ResultObj->SetArrayField(TEXT("results"), Results);
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("result_count"), Results.Num());
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleAddInputActionToMappingContext(const TSharedPtr<FJsonObject>& Params)
{
    FString MappingContextPath;
    FString InputActionPath;
    FString KeyName;

    if (!TryGetNonEmptyStringField(Params, TEXT("mapping_context_path"), MappingContextPath) ||
        !TryGetNonEmptyStringField(Params, TEXT("input_action_path"), InputActionPath) ||
        !TryGetNonEmptyStringField(Params, TEXT("key"), KeyName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing required parameters: 'mapping_context_path', 'input_action_path', or 'key'"));
    }

    UInputMappingContext* MappingContext = LoadEditorAsset<UInputMappingContext>(MappingContextPath);
    if (!MappingContext)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Input Mapping Context not found: %s"), *MappingContextPath));
    }

    UInputAction* InputAction = LoadEditorAsset<UInputAction>(InputActionPath);
    if (!InputAction)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Input Action not found: %s"), *InputActionPath));
    }

    const FKey ResolvedKey = ResolveInputKey(KeyName);
    if (!ResolvedKey.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid input key: %s"), *KeyName));
    }

    for (const FEnhancedActionKeyMapping& ExistingMapping : MappingContext->GetMappings())
    {
        if (ExistingMapping.Action == InputAction && ExistingMapping.Key == ResolvedKey)
        {
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetBoolField(TEXT("success"), true);
            ResultObj->SetBoolField(TEXT("created"), false);
            ResultObj->SetBoolField(TEXT("already_exists"), true);
            ResultObj->SetObjectField(TEXT("mapping"), BuildEnhancedMappingJson(MappingContextPath, InputActionPath, ExistingMapping));
            return ResultObj;
        }
    }

    MappingContext->Modify();
    FEnhancedActionKeyMapping& NewMapping = MappingContext->MapKey(InputAction, ResolvedKey);

    bool bNegate = false;
    Params->TryGetBoolField(TEXT("negate"), bNegate);
    if (bNegate)
    {
        UInputModifierNegate* NegateModifier = NewObject<UInputModifierNegate>(MappingContext);
        NewMapping.Modifiers.Add(NegateModifier);
    }

    FString SwizzleOrder;
    if (TryGetNonEmptyStringField(Params, TEXT("swizzle_order"), SwizzleOrder))
    {
        EInputAxisSwizzle ParsedOrder = EInputAxisSwizzle::YXZ;
        if (!TryParseSwizzleOrder(SwizzleOrder, ParsedOrder))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported swizzle_order: %s"), *SwizzleOrder));
        }

        UInputModifierSwizzleAxis* SwizzleModifier = NewObject<UInputModifierSwizzleAxis>(MappingContext);
        SwizzleModifier->Order = ParsedOrder;
        NewMapping.Modifiers.Add(SwizzleModifier);
    }

    FVector ScaleVector = FVector::OneVector;
    if (TryGetScaleVector(Params, TEXT("scale"), ScaleVector) && !ScaleVector.Equals(FVector::OneVector))
    {
        UInputModifierScalar* ScalarModifier = NewObject<UInputModifierScalar>(MappingContext);
        ScalarModifier->Scalar = ScaleVector;
        NewMapping.Modifiers.Add(ScalarModifier);
    }

    MappingContext->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(MappingContext);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("created"), true);
    ResultObj->SetBoolField(TEXT("already_exists"), false);
    ResultObj->SetObjectField(TEXT("mapping"), BuildEnhancedMappingJson(MappingContextPath, InputActionPath, NewMapping));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCreateGameplayTag(const TSharedPtr<FJsonObject>& Params)
{
    TArray<TSharedPtr<FJsonValue>> TagValues;
    if (!TryGetJsonArrayFieldFlexible(Params, TEXT("tag_names"), TagValues) || TagValues.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing or empty 'tag_names' parameter"));
    }

    FString IniFile = TEXT("DefaultGameplayTags.ini");
    TryGetNonEmptyStringField(Params, TEXT("ini_file"), IniFile);
    if (!IniFile.EndsWith(TEXT(".ini"), ESearchCase::IgnoreCase))
    {
        IniFile += TEXT(".ini");
    }

    UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
    const bool bIsDefaultSource = IniFile.Equals(TEXT("DefaultGameplayTags.ini"), ESearchCase::IgnoreCase);
    const FString ConfigFileName = bIsDefaultSource
        ? (FPaths::ProjectConfigDir() / IniFile)
        : (FPaths::SourceConfigDir() / IniFile);
    TArray<TSharedPtr<FJsonValue>> CreatedTags;
    TArray<TSharedPtr<FJsonValue>> SkippedTags;
    TArray<FString> ExistingLines;
    FFileHelper::LoadFileToStringArray(ExistingLines, *ConfigFileName);

    bool bHasGameplayTagsSection = ExistingLines.ContainsByPredicate(
        [](const FString& Line)
        {
            return Line.TrimStartAndEnd().Equals(TEXT("[/Script/GameplayTags.GameplayTagsSettings]"), ESearchCase::IgnoreCase);
        });

    for (const TSharedPtr<FJsonValue>& TagValue : TagValues)
    {
        FString TagName;
        if (!TagValue.IsValid() || !TagValue->TryGetString(TagName))
        {
            continue;
        }

        TagName.TrimStartAndEndInline();
        if (TagName.IsEmpty())
        {
            continue;
        }

        const FName TagFName(*TagName);
        const bool bAlreadyExists = Manager.RequestGameplayTag(TagFName, false).IsValid() ||
            ExistingLines.ContainsByPredicate(
            [&TagName](const FString& ExistingLine)
            {
                return ExistingLine.Contains(FString::Printf(TEXT("Tag=\"%s\""), *TagName), ESearchCase::CaseSensitive);
            });

        if (bAlreadyExists)
        {
            SkippedTags.Add(MakeShared<FJsonValueString>(TagName));
            continue;
        }

        if (!bHasGameplayTagsSection)
        {
            if (ExistingLines.Num() > 0 && !ExistingLines.Last().IsEmpty())
            {
                ExistingLines.Add(FString());
            }

            ExistingLines.Add(TEXT("[/Script/GameplayTags.GameplayTagsSettings]"));
            if (!bIsDefaultSource)
            {
                ExistingLines.Add(TEXT("ImportTagsFromConfig=True"));
            }
            bHasGameplayTagsSection = true;
        }

        ExistingLines.Add(FString::Printf(TEXT("+GameplayTagList=(Tag=\"%s\",DevComment=\"\")"), *TagName));
        CreatedTags.Add(MakeShared<FJsonValueString>(TagName));
    }

    if (CreatedTags.Num() > 0)
    {
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigFileName), true);
        if (!FFileHelper::SaveStringArrayToFile(ExistingLines, *ConfigFileName))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to write gameplay tags file: %s"), *ConfigFileName));
        }

        GConfig->LoadFile(ConfigFileName);
        Manager.EditorRefreshGameplayTagTree();
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("ini_file"), IniFile);
    ResultObj->SetStringField(TEXT("config_file"), ConfigFileName);
    ResultObj->SetArrayField(TEXT("created_tags"), CreatedTags);
    ResultObj->SetArrayField(TEXT("skipped_tags"), SkippedTags);
    ResultObj->SetNumberField(TEXT("created_count"), CreatedTags.Num());
    ResultObj->SetNumberField(TEXT("skipped_count"), SkippedTags.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleAddOrReplaceRowsInDataTable(const TSharedPtr<FJsonObject>& Params)
{
    FString SoftPath;
    if (!TryGetNonEmptyStringField(Params, TEXT("soft_path"), SoftPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path' parameter"));
    }

    UDataTable* DataTable = LoadEditorAsset<UDataTable>(SoftPath);
    if (!DataTable)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("DataTable not found: %s"), *SoftPath));
    }

    TSharedPtr<FJsonObject> RowDataObject;
    if (!TryGetJsonObjectFieldFlexible(Params, TEXT("row_datas"), RowDataObject) || !RowDataObject.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'row_datas' parameter"));
    }

    const UScriptStruct* RowStruct = DataTable->GetRowStruct();
    if (!RowStruct)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("DataTable row struct is not available"));
    }

    DataTable->Modify();

    TArray<TSharedPtr<FJsonValue>> UpdatedRows;
    TArray<TSharedPtr<FJsonValue>> Errors;

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RowDataObject->Values)
    {
        if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("row_datas.%s must be an object"), *Pair.Key)));
            continue;
        }

        const FName RowName(*Pair.Key);
        const bool bReplaced = DataTable->FindRowUnchecked(RowName) != nullptr;

        FStructOnScope RowScope(RowStruct);
        FText FailureReason;
        if (!FJsonObjectConverter::JsonObjectToUStruct(Pair.Value->AsObject().ToSharedRef(), RowStruct, RowScope.GetStructMemory(), 0, 0, false, &FailureReason, nullptr))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Failed to import row '%s': %s"), *Pair.Key, *FailureReason.ToString())));
            continue;
        }

        DataTable->RemoveRow(RowName);
        DataTable->AddRow(RowName, RowScope.GetStructMemory(), RowStruct);

        TSharedPtr<FJsonObject> RowResult = MakeShared<FJsonObject>();
        RowResult->SetStringField(TEXT("row_name"), Pair.Key);
        RowResult->SetBoolField(TEXT("replaced"), bReplaced);
        UpdatedRows.Add(MakeShared<FJsonValueObject>(RowResult));
    }

    DataTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveLoadedAsset(DataTable);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0);
    ResultObj->SetStringField(TEXT("soft_path"), SoftPath);
    ResultObj->SetArrayField(TEXT("updated_rows"), UpdatedRows);
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("updated_count"), UpdatedRows.Num());
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleRemoveRowsFromDataTable(const TSharedPtr<FJsonObject>& Params)
{
    FString SoftPath;
    if (!TryGetNonEmptyStringField(Params, TEXT("soft_path"), SoftPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path' parameter"));
    }

    UDataTable* DataTable = LoadEditorAsset<UDataTable>(SoftPath);
    if (!DataTable)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("DataTable not found: %s"), *SoftPath));
    }

    TArray<TSharedPtr<FJsonValue>> RowNameValues;
    if (!TryGetJsonArrayFieldFlexible(Params, TEXT("row_names"), RowNameValues) || RowNameValues.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_names' parameter"));
    }

    DataTable->Modify();

    TArray<TSharedPtr<FJsonValue>> RemovedRows;
    TArray<TSharedPtr<FJsonValue>> MissingRows;

    for (const TSharedPtr<FJsonValue>& RowNameValue : RowNameValues)
    {
        FString RowNameString;
        if (!RowNameValue.IsValid() || !RowNameValue->TryGetString(RowNameString))
        {
            continue;
        }

        RowNameString.TrimStartAndEndInline();
        if (RowNameString.IsEmpty())
        {
            continue;
        }

        const FName RowName(*RowNameString);
        if (DataTable->FindRowUnchecked(RowName) != nullptr)
        {
            DataTable->RemoveRow(RowName);
            RemovedRows.Add(MakeShared<FJsonValueString>(RowNameString));
        }
        else
        {
            MissingRows.Add(MakeShared<FJsonValueString>(RowNameString));
        }
    }

    if (RemovedRows.Num() > 0)
    {
        DataTable->MarkPackageDirty();
        UEditorAssetLibrary::SaveLoadedAsset(DataTable);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("soft_path"), SoftPath);
    ResultObj->SetArrayField(TEXT("removed_rows"), RemovedRows);
    ResultObj->SetArrayField(TEXT("missing_rows"), MissingRows);
    ResultObj->SetNumberField(TEXT("removed_count"), RemovedRows.Num());
    ResultObj->SetNumberField(TEXT("missing_count"), MissingRows.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetDataAssetTypes(const TSharedPtr<FJsonObject>& Params)
{
    TMap<FString, UClass*> UniqueClasses;

    auto AddClass = [&UniqueClasses](UClass* Class)
    {
        if (!ShouldIncludeDataAssetClass(Class))
        {
            return;
        }

        UniqueClasses.FindOrAdd(Class->GetPathName()) = Class;
    };

    AddClass(UDataAsset::StaticClass());
    AddClass(UPrimaryDataAsset::StaticClass());

    for (TObjectIterator<UClass> It; It; ++It)
    {
        AddClass(*It);
    }

    FARFilter Filter;
    Filter.PackagePaths.Add(TEXT("/Game"));
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;

    TArray<FAssetData> BlueprintAssets;
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    AssetRegistryModule.Get().GetAssets(Filter, BlueprintAssets);

    for (const FAssetData& AssetData : BlueprintAssets)
    {
        if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(AssetData.GetAsset()))
        {
            AddClass(BlueprintAsset->GeneratedClass);
        }
    }

    TArray<FString> SortedKeys;
    UniqueClasses.GetKeys(SortedKeys);
    SortedKeys.Sort();

    TArray<TSharedPtr<FJsonValue>> TypeArray;
    for (const FString& Key : SortedKeys)
    {
        UClass* Class = UniqueClasses[Key];
        if (!Class || !ShouldIncludeDataAssetClass(Class))
        {
            continue;
        }

        TSharedPtr<FJsonObject> TypeJson = MakeShared<FJsonObject>();
        TypeJson->SetStringField(TEXT("class_name"), Class->GetName());
        TypeJson->SetStringField(TEXT("class_path"), Class->GetPathName());
        TypeJson->SetStringField(TEXT("source"), Class->ClassGeneratedBy ? TEXT("blueprint") : TEXT("native"));
        if (UClass* SuperClass = Class->GetSuperClass())
        {
            TypeJson->SetStringField(TEXT("parent_class_name"), SuperClass->GetName());
            TypeJson->SetStringField(TEXT("parent_class_path"), SuperClass->GetPathName());
        }
        TypeArray.Add(MakeShared<FJsonValueObject>(TypeJson));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetArrayField(TEXT("data_asset_types"), TypeArray);
    ResultObj->SetNumberField(TEXT("count"), TypeArray.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetDataAssetTypeInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString DataAssetType;
    if (!TryGetNonEmptyStringField(Params, TEXT("data_asset_type"), DataAssetType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'data_asset_type' parameter"));
    }

    UClass* DataAssetClass = ResolveDataAssetClassReference(DataAssetType);
    if (!DataAssetClass || !DataAssetClass->IsChildOf(UDataAsset::StaticClass()))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("DataAsset class not found or invalid: %s"), *DataAssetType));
    }

    UObject* CDO = DataAssetClass->GetDefaultObject();
    TArray<TSharedPtr<FJsonValue>> Properties;

    for (TFieldIterator<FProperty> It(DataAssetClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property)
        {
            continue;
        }

        TSharedPtr<FJsonObject> PropertyJson = DescribePropertyTypeJson(Property);
        if (const UStruct* OwnerStruct = Property->GetOwnerStruct())
        {
            PropertyJson->SetStringField(TEXT("owner_struct_name"), OwnerStruct->GetName());
            PropertyJson->SetStringField(TEXT("owner_struct_path"), OwnerStruct->GetPathName());
        }

        if (CDO)
        {
            FString DefaultValueText;
            Property->ExportTextItem_Direct(DefaultValueText, Property->ContainerPtrToValuePtr<void>(CDO), nullptr, CDO, PPF_None);
            PropertyJson->SetStringField(TEXT("default_value_text"), DefaultValueText);
        }

        Properties.Add(MakeShared<FJsonValueObject>(PropertyJson));
    }

    TArray<TSharedPtr<FJsonValue>> InheritanceChain;
    for (UClass* CurrentClass = DataAssetClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
    {
        TSharedPtr<FJsonObject> InheritanceJson = MakeShared<FJsonObject>();
        InheritanceJson->SetStringField(TEXT("class_name"), CurrentClass->GetName());
        InheritanceJson->SetStringField(TEXT("class_path"), CurrentClass->GetPathName());
        InheritanceChain.Add(MakeShared<FJsonValueObject>(InheritanceJson));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("class_name"), DataAssetClass->GetName());
    ResultObj->SetStringField(TEXT("class_path"), DataAssetClass->GetPathName());
    ResultObj->SetBoolField(TEXT("is_blueprint_class"), DataAssetClass->ClassGeneratedBy != nullptr);
    if (UClass* SuperClass = DataAssetClass->GetSuperClass())
    {
        ResultObj->SetStringField(TEXT("parent_class_name"), SuperClass->GetName());
        ResultObj->SetStringField(TEXT("parent_class_path"), SuperClass->GetPathName());
    }
    ResultObj->SetArrayField(TEXT("inheritance_chain"), InheritanceChain);
    ResultObj->SetArrayField(TEXT("properties"), Properties);
    ResultObj->SetNumberField(TEXT("property_count"), Properties.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleEditEnumeration(const TSharedPtr<FJsonObject>& Params)
{
    FString SoftPath;
    if (!TryGetNonEmptyStringField(Params, TEXT("soft_path"), SoftPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path' parameter"));
    }

    UUserDefinedEnum* EnumAsset = LoadEditorAsset<UUserDefinedEnum>(SoftPath);
    if (!EnumAsset)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Enumeration not found: %s"), *SoftPath));
    }

    TArray<TSharedPtr<FJsonValue>> EnumFieldValues;
    if (!TryGetJsonArrayFieldFlexible(Params, TEXT("enum_fields"), EnumFieldValues) || EnumFieldValues.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing or empty 'enum_fields' parameter"));
    }

    TArray<FString> DesiredFields;
    for (const TSharedPtr<FJsonValue>& FieldValue : EnumFieldValues)
    {
        FString FieldName;
        if (!FieldValue.IsValid() || !FieldValue->TryGetString(FieldName))
        {
            continue;
        }

        FieldName.TrimStartAndEndInline();
        if (!FieldName.IsEmpty())
        {
            DesiredFields.Add(FieldName);
        }
    }

    if (DesiredFields.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No valid enum field names were provided"));
    }

    EnumAsset->Modify();

    int32 CurrentFieldCount = FMath::Max(0, EnumAsset->NumEnums() - 1);
    while (CurrentFieldCount < DesiredFields.Num())
    {
        FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(EnumAsset);
        CurrentFieldCount = FMath::Max(0, EnumAsset->NumEnums() - 1);
    }

    while (CurrentFieldCount > DesiredFields.Num() && CurrentFieldCount > 0)
    {
        FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(EnumAsset, CurrentFieldCount - 1);
        CurrentFieldCount = FMath::Max(0, EnumAsset->NumEnums() - 1);
    }

    for (int32 Index = 0; Index < DesiredFields.Num(); ++Index)
    {
        FEnumEditorUtils::SetEnumeratorDisplayName(EnumAsset, Index, FText::FromString(DesiredFields[Index]));
    }

    if (UPackage* Package = EnumAsset->GetOutermost())
    {
        Package->MarkPackageDirty();
    }
    UEditorAssetLibrary::SaveLoadedAsset(EnumAsset);

    TArray<TSharedPtr<FJsonValue>> FieldResults;
    for (const FString& FieldName : DesiredFields)
    {
        FieldResults.Add(MakeShared<FJsonValueString>(FieldName));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("soft_path"), SoftPath);
    ResultObj->SetArrayField(TEXT("enum_fields"), FieldResults);
    ResultObj->SetNumberField(TEXT("enum_field_count"), DesiredFields.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleEditStructure(const TSharedPtr<FJsonObject>& Params)
{
    FString SoftPath;
    if (!TryGetNonEmptyStringField(Params, TEXT("soft_path"), SoftPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path' parameter"));
    }

    UUserDefinedStruct* StructAsset = LoadEditorAsset<UUserDefinedStruct>(SoftPath);
    if (!StructAsset)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Structure not found: %s"), *SoftPath));
    }

    TSharedPtr<FJsonObject> DesiredStructureData;
    if (!TryGetJsonObjectFieldFlexible(Params, TEXT("default_structure_data"), DesiredStructureData) || !DesiredStructureData.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'default_structure_data' parameter"));
    }

    TSharedPtr<FJsonObject> TypeHintsObject;
    TryGetJsonObjectFieldFlexible(Params, TEXT("type_hints"), TypeHintsObject);

    struct FDesiredStructField
    {
        FString Name;
        FEdGraphPinType PinType;
        FString DefaultValue;
    };

    TArray<FDesiredStructField> DesiredFields;
    TSet<FString> DesiredNames;
    TArray<TSharedPtr<FJsonValue>> Errors;

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DesiredStructureData->Values)
    {
        FString FieldName = Pair.Key;
        FieldName.TrimStartAndEndInline();
        if (FieldName.IsEmpty())
        {
            continue;
        }

        FString TypeHint;
        if (TypeHintsObject.IsValid())
        {
            TypeHintsObject->TryGetStringField(FieldName, TypeHint);
        }

        if (TypeHint.IsEmpty() && !TryInferTypeHintFromJsonValue(Pair.Value, TypeHint))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Field '%s' requires an explicit type_hints entry"), *FieldName)));
            continue;
        }

        FEdGraphPinType PinType;
        FString TypeError;
        if (!TryBuildPinTypeFromTypeHint(TypeHint, PinType, TypeError))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Field '%s': %s"), *FieldName, *TypeError)));
            continue;
        }

        DesiredFields.Add({FieldName, PinType, BuildStructDefaultValueString(Pair.Value)});
        DesiredNames.Add(FieldName);
    }

    if (DesiredFields.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No valid structure fields were resolved from 'default_structure_data'"));
    }

    if (Errors.Num() > 0)
    {
        TSharedPtr<FJsonObject> ErrorObj = FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to resolve one or more structure fields"));
        ErrorObj->SetArrayField(TEXT("details"), Errors);
        return ErrorObj;
    }

    StructAsset->Modify();

    TArray<TSharedPtr<FJsonValue>> AddedFields;
    TArray<TSharedPtr<FJsonValue>> UpdatedFields;
    TArray<TSharedPtr<FJsonValue>> RemovedFields;

    auto FindFieldByName = [StructAsset](const FString& FieldName) -> FStructVariableDescription*
    {
        TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(StructAsset);
        for (FStructVariableDescription& VarDesc : VarDescs)
        {
            if (GetStructFieldFriendlyName(VarDesc).Equals(FieldName, ESearchCase::CaseSensitive))
            {
                return &VarDesc;
            }
        }

        return nullptr;
    };

    for (const FDesiredStructField& DesiredField : DesiredFields)
    {
        FStructVariableDescription* ExistingField = FindFieldByName(DesiredField.Name);
        if (!ExistingField)
        {
            TSet<FGuid> PreviousGuids;
            for (const FStructVariableDescription& VarDesc : FStructureEditorUtils::GetVarDesc(StructAsset))
            {
                PreviousGuids.Add(VarDesc.VarGuid);
            }

            if (!FStructureEditorUtils::AddVariable(StructAsset, DesiredField.PinType))
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Failed to add structure field '%s'"), *DesiredField.Name)));
                continue;
            }

            FStructVariableDescription* NewField = nullptr;
            for (FStructVariableDescription& VarDesc : FStructureEditorUtils::GetVarDesc(StructAsset))
            {
                if (!PreviousGuids.Contains(VarDesc.VarGuid))
                {
                    NewField = &VarDesc;
                    break;
                }
            }

            if (!NewField || !FStructureEditorUtils::RenameVariable(StructAsset, NewField->VarGuid, DesiredField.Name))
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Failed to rename new structure field to '%s'"), *DesiredField.Name)));
                continue;
            }

            if (!DesiredField.DefaultValue.IsEmpty())
            {
                FStructVariableDescription* RenamedField = FindFieldByName(DesiredField.Name);
                if (RenamedField)
                {
                    FStructureEditorUtils::ChangeVariableDefaultValue(StructAsset, RenamedField->VarGuid, DesiredField.DefaultValue);
                }
            }

            AddedFields.Add(MakeShared<FJsonValueString>(DesiredField.Name));
            continue;
        }

        bool bFieldChanged = false;
        if (!ArePinTypesEquivalent(ExistingField->ToPinType(), DesiredField.PinType))
        {
            if (FStructureEditorUtils::ChangeVariableType(StructAsset, ExistingField->VarGuid, DesiredField.PinType))
            {
                bFieldChanged = true;
            }
            else
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Failed to change type for structure field '%s'"), *DesiredField.Name)));
            }
        }

        if (!DesiredField.DefaultValue.IsEmpty())
        {
            bFieldChanged = FStructureEditorUtils::ChangeVariableDefaultValue(StructAsset, ExistingField->VarGuid, DesiredField.DefaultValue) || bFieldChanged;
        }

        if (bFieldChanged)
        {
            UpdatedFields.Add(MakeShared<FJsonValueString>(DesiredField.Name));
        }
    }

    TArray<FGuid> FieldsToRemove;
    TArray<FString> FieldNamesToRemove;
    for (const FStructVariableDescription& VarDesc : FStructureEditorUtils::GetVarDesc(StructAsset))
    {
        const FString ExistingFieldName = GetStructFieldFriendlyName(VarDesc);
        if (!DesiredNames.Contains(ExistingFieldName))
        {
            FieldsToRemove.Add(VarDesc.VarGuid);
            FieldNamesToRemove.Add(ExistingFieldName);
        }
    }

    for (int32 Index = 0; Index < FieldsToRemove.Num(); ++Index)
    {
        if (FStructureEditorUtils::RemoveVariable(StructAsset, FieldsToRemove[Index]))
        {
            RemovedFields.Add(MakeShared<FJsonValueString>(FieldNamesToRemove[Index]));
        }
    }

    FStructureEditorUtils::CompileStructure(StructAsset);
    if (UPackage* Package = StructAsset->GetOutermost())
    {
        Package->MarkPackageDirty();
    }
    UEditorAssetLibrary::SaveLoadedAsset(StructAsset);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0);
    ResultObj->SetStringField(TEXT("soft_path"), SoftPath);
    ResultObj->SetArrayField(TEXT("added_fields"), AddedFields);
    ResultObj->SetArrayField(TEXT("updated_fields"), UpdatedFields);
    ResultObj->SetArrayField(TEXT("removed_fields"), RemovedFields);
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("added_count"), AddedFields.Num());
    ResultObj->SetNumberField(TEXT("updated_count"), UpdatedFields.Num());
    ResultObj->SetNumberField(TEXT("removed_count"), RemovedFields.Num());
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCreateTextFile(const TSharedPtr<FJsonObject>& Params)
{
    TArray<TSharedPtr<FJsonValue>> FileEntries;
    if (!TryGetJsonArrayFieldFlexible(Params, TEXT("files"), FileEntries) || FileEntries.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing or empty 'files' parameter"));
    }

    TArray<TSharedPtr<FJsonValue>> CreatedFiles;
    for (int32 Index = 0; Index < FileEntries.Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& EntryValue = FileEntries[Index];
        if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("files[%d] must be an object"), Index));
        }

        const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
        FString RequestedPath;
        FString Content;
        if (!TryGetNonEmptyStringField(EntryObject, TEXT("path"), RequestedPath))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("files[%d] is missing 'path'"), Index));
        }
        if (!EntryObject->TryGetStringField(TEXT("content"), Content))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("files[%d] is missing 'content'"), Index));
        }

        FString RelativePath;
        FString AbsolutePath;
        FString Error;
        if (!TryMakeProjectRelativePath(RequestedPath, RelativePath, AbsolutePath, Error))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }
        if (!ValidateCreateTextFilePath(RelativePath, Error))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }

        IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePath), true);
        if (!FFileHelper::SaveStringToFile(Content, *AbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to write file: %s"), *RelativePath));
        }

        TSharedPtr<FJsonObject> CreatedFileObj = MakeShared<FJsonObject>();
        CreatedFileObj->SetStringField(TEXT("path"), RelativePath);
        CreatedFileObj->SetStringField(TEXT("full_path"), AbsolutePath);
        CreatedFileObj->SetNumberField(TEXT("content_length"), Content.Len());
        CreatedFiles.Add(MakeShared<FJsonValueObject>(CreatedFileObj));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetArrayField(TEXT("files"), CreatedFiles);
    ResultObj->SetNumberField(TEXT("count"), CreatedFiles.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleEditTextFile(const TSharedPtr<FJsonObject>& Params)
{
    FString RequestedPath;
    if (!TryGetNonEmptyStringField(Params, TEXT("path"), RequestedPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'path' parameter"));
    }

    FString RelativePath;
    FString AbsolutePath;
    FString Error;
    if (!TryMakeProjectRelativePath(RequestedPath, RelativePath, AbsolutePath, Error))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    if (!ValidateEditTextFilePath(RelativePath, Error))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FString OriginalContent;
    if (!FFileHelper::LoadFileToString(OriginalContent, *AbsolutePath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to read file: %s"), *RelativePath));
    }

    TArray<TSharedPtr<FJsonValue>> EditEntries;
    if (!TryGetJsonArrayFieldFlexible(Params, TEXT("edits"), EditEntries) || EditEntries.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing or empty 'edits' parameter"));
    }

    FString UpdatedContent = OriginalContent;
    for (int32 Index = 0; Index < EditEntries.Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& EditValue = EditEntries[Index];
        if (!EditValue.IsValid() || EditValue->Type != EJson::Object)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("edits[%d] must be an object"), Index));
        }

        const TSharedPtr<FJsonObject> EditObject = EditValue->AsObject();
        FString EditType;
        if (!TryGetNonEmptyStringField(EditObject, TEXT("type"), EditType))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("edits[%d] is missing 'type'"), Index));
        }

        if (EditType == TEXT("replace_content"))
        {
            if (!EditObject->TryGetStringField(TEXT("content"), UpdatedContent))
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("edits[%d] replace_content is missing 'content'"), Index));
            }
        }
        else if (EditType == TEXT("find_and_replace"))
        {
            FString FindText;
            FString ReplaceText;
            if (!EditObject->TryGetStringField(TEXT("find"), FindText))
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("edits[%d] find_and_replace is missing 'find'"), Index));
            }
            if (!EditObject->TryGetStringField(TEXT("replace"), ReplaceText))
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("edits[%d] find_and_replace is missing 'replace'"), Index));
            }

            int32 RequestedOccurrence = 0;
            double RequestedOccurrenceNumber = 0.0;
            if (EditObject->TryGetNumberField(TEXT("index"), RequestedOccurrenceNumber))
            {
                RequestedOccurrence = FMath::Max(0, static_cast<int32>(RequestedOccurrenceNumber));
            }

            int32 SearchFrom = 0;
            int32 FoundIndex = INDEX_NONE;
            for (int32 Occurrence = 0; Occurrence <= RequestedOccurrence; ++Occurrence)
            {
                FoundIndex = UpdatedContent.Find(FindText, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
                if (FoundIndex == INDEX_NONE)
                {
                    break;
                }
                SearchFrom = FoundIndex + FindText.Len();
            }

            if (FoundIndex == INDEX_NONE)
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("edits[%d] could not find requested text"), Index));
            }

            UpdatedContent = UpdatedContent.Left(FoundIndex) + ReplaceText + UpdatedContent.Mid(FoundIndex + FindText.Len());
        }
        else if (EditType == TEXT("apply_patch"))
        {
            FString PatchText;
            if (!EditObject->TryGetStringField(TEXT("patch"), PatchText))
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("edits[%d] apply_patch is missing 'patch'"), Index));
            }

            FString PatchedContent;
            if (!ApplyUnifiedDiffPatch(UpdatedContent, PatchText, PatchedContent, Error))
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
            }

            UpdatedContent = PatchedContent;
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unsupported edit type: %s"), *EditType));
        }
    }

    if (RelativePath.StartsWith(TEXT("Saved/.Aura/plans/"), ESearchCase::IgnoreCase) &&
        !ValidatePlanFileEdit(OriginalContent, UpdatedContent, Error))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    if (!FFileHelper::SaveStringToFile(UpdatedContent, *AbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to write file: %s"), *RelativePath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("path"), RelativePath);
    ResultObj->SetStringField(TEXT("full_path"), AbsolutePath);
    ResultObj->SetNumberField(TEXT("original_length"), OriginalContent.Len());
    ResultObj->SetNumberField(TEXT("updated_length"), UpdatedContent.Len());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCreateNewTodoList(const TSharedPtr<FJsonObject>& Params)
{
    TArray<TSharedPtr<FJsonValue>> TodoEntries;
    if (!TryGetJsonArrayFieldFlexible(Params, TEXT("todo_list_data"), TodoEntries))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'todo_list_data' parameter"));
    }

    TArray<FTodoItemRecord> Items;
    for (int32 Index = 0; Index < TodoEntries.Num(); ++Index)
    {
        FString RawLine;
        if (!TodoEntries[Index].IsValid() || !TodoEntries[Index]->TryGetString(RawLine))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("todo_list_data[%d] must be a string"), Index));
        }

        FTodoItemRecord Item;
        FString Error;
        if (!ParseTodoLine(RawLine, Item, Error))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }

        Items.Add(Item);
    }

    FString SaveError;
    if (!SaveTodoItems(Items, SaveError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(SaveError);
    }

    return BuildTodoListResponse(Items);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetTodoList(const TSharedPtr<FJsonObject>& Params)
{
    TArray<FTodoItemRecord> Items;
    FString Error;
    if (!LoadTodoItems(Items, Error))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    return BuildTodoListResponse(Items);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleEditTodoList(const TSharedPtr<FJsonObject>& Params)
{
    FString Codes;
    if (!TryGetNonEmptyStringField(Params, TEXT("codes"), Codes))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'codes' parameter"));
    }

    TArray<FTodoItemRecord> Items;
    FString Error;
    if (!LoadTodoItems(Items, Error))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    if (Items.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No todo list exists. Create one first."));
    }

    TArray<FString> Segments;
    Codes.ParseIntoArray(Segments, TEXT("|"), true);
    for (const FString& Segment : Segments)
    {
        TArray<FString> Parts;
        Segment.ParseIntoArray(Parts, TEXT(","), false);
        if (Parts.Num() == 0)
        {
            continue;
        }

        const int32 ItemIndex = FCString::Atoi(*Parts[0]);
        if (!Items.IsValidIndex(ItemIndex))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Todo index out of range: %d"), ItemIndex));
        }

        if (Parts.Num() >= 2)
        {
            FString NewPriority;
            if (TryDecodePriorityCode(Parts[1], NewPriority))
            {
                Items[ItemIndex].Priority = NewPriority;
            }
            else if (!Parts[1].TrimStartAndEnd().IsEmpty())
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Invalid todo priority code: %s"), *Parts[1]));
            }
        }

        if (Parts.Num() >= 3)
        {
            FString NewStatus;
            if (TryDecodeStatusCode(Parts[2], NewStatus))
            {
                Items[ItemIndex].Status = NewStatus;
            }
            else if (!Parts[2].TrimStartAndEnd().IsEmpty())
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Invalid todo status code: %s"), *Parts[2]));
            }
        }
    }

    FString SaveError;
    if (!SaveTodoItems(Items, SaveError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(SaveError);
    }

    return BuildTodoListResponse(Items);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSearchAssetsByNameFromFab(const TSharedPtr<FJsonObject>& Params)
{
    return BuildFabSearchResponse(Params, false);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetAssetsByNameFromFab(const TSharedPtr<FJsonObject>& Params)
{
    return BuildFabSearchResponse(Params, true);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSaveProject(const TSharedPtr<FJsonObject>& Params)
{
    TArray<UPackage*> DirtyMapPackagesBefore;
    TArray<UPackage*> DirtyContentPackagesBefore;
    UEditorLoadingAndSavingUtils::GetDirtyMapPackages(DirtyMapPackagesBefore);
    UEditorLoadingAndSavingUtils::GetDirtyContentPackages(DirtyContentPackagesBefore);

    const int32 DirtyMapCountBefore = DirtyMapPackagesBefore.Num();
    const int32 DirtyContentCountBefore = DirtyContentPackagesBefore.Num();
    const int32 TotalDirtyCountBefore = DirtyMapCountBefore + DirtyContentCountBefore;

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("command"), TEXT("save_project"));
    ResultObj->SetNumberField(TEXT("dirty_map_packages_before"), DirtyMapCountBefore);
    ResultObj->SetNumberField(TEXT("dirty_content_packages_before"), DirtyContentCountBefore);
    ResultObj->SetNumberField(TEXT("dirty_packages_before"), TotalDirtyCountBefore);

    if (TotalDirtyCountBefore == 0)
    {
        ResultObj->SetBoolField(TEXT("success"), true);
        ResultObj->SetBoolField(TEXT("nothing_to_save"), true);
        ResultObj->SetStringField(TEXT("message"), TEXT("No dirty packages to save"));
        ResultObj->SetNumberField(TEXT("dirty_map_packages_after"), 0);
        ResultObj->SetNumberField(TEXT("dirty_content_packages_after"), 0);
        ResultObj->SetNumberField(TEXT("dirty_packages_after"), 0);
        ResultObj->SetNumberField(TEXT("packages_saved"), 0);
        return ResultObj;
    }

    const bool bSaveSucceeded = UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true);

    TArray<UPackage*> DirtyMapPackagesAfter;
    TArray<UPackage*> DirtyContentPackagesAfter;
    UEditorLoadingAndSavingUtils::GetDirtyMapPackages(DirtyMapPackagesAfter);
    UEditorLoadingAndSavingUtils::GetDirtyContentPackages(DirtyContentPackagesAfter);

    const int32 DirtyMapCountAfter = DirtyMapPackagesAfter.Num();
    const int32 DirtyContentCountAfter = DirtyContentPackagesAfter.Num();
    const int32 TotalDirtyCountAfter = DirtyMapCountAfter + DirtyContentCountAfter;

    ResultObj->SetNumberField(TEXT("dirty_map_packages_after"), DirtyMapCountAfter);
    ResultObj->SetNumberField(TEXT("dirty_content_packages_after"), DirtyContentCountAfter);
    ResultObj->SetNumberField(TEXT("dirty_packages_after"), TotalDirtyCountAfter);
    ResultObj->SetNumberField(TEXT("packages_saved"), FMath::Max(0, TotalDirtyCountBefore - TotalDirtyCountAfter));

    if (!bSaveSucceeded)
    {
        ResultObj->SetBoolField(TEXT("success"), false);
        ResultObj->SetStringField(TEXT("error"), TEXT("Failed to save dirty packages"));
        return ResultObj;
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("nothing_to_save"), false);
    ResultObj->SetStringField(TEXT("message"), TEXT("Save All completed"));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    // This function will now correctly call the implementation in BlueprintCommands
    FEpicUnrealMCPBlueprintCommands BlueprintCommands;
    return BlueprintCommands.HandleCommand(TEXT("spawn_blueprint_actor"), Params);
}
