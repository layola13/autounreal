// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "ExportBlueprintToTxt.h"

#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Blueprint/BlueprintSupport.h"
#include "ContentBrowserModule.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "EdGraphNode_Comment.h"
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/MemberReference.h"
#include "Engine/PoseWatch.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/TimelineTemplate.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "ToolMenus.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FExportBlueprintToTxtModule"

namespace
{
    struct FExportDirectorySet
    {
        FString Root;
        FString TxtRoot;
        FString TxtRawRoot;
        FString TxtStructuredRoot;
        FString TxtOtherRoot;
        FString DotRoot;
        FString MmidRoot;
        FString VariablesRoot;
        FString MappingsRoot;
        FString ReadmeRoot;
    };

    struct FDotEdge
    {
        FString FromNodeId;
        FString ToNodeId;
        FString Label;
    };

    enum class ECppHintMatchType : uint8
    {
        FunctionName,
        EventName,
        NodeClass,
        SemanticType,
        NodeTitleContains
    };

    struct FCppHintMappingEntry
    {
        const TCHAR* Category;
        const TCHAR* BlueprintNode;
        const TCHAR* Cpp;
        const TCHAR* Include;
        const TCHAR* CppHint;
        ECppHintMatchType MatchType;
        const TCHAR* MatchValue;
    };

    struct FVariableYamlComponent
    {
        FString Name;
        FString CppType;
        FString UPropertySpec;
        FString CppDecl;
        FString CppInit;
        FString Attachment;
    };

    struct FVariableYamlAsset
    {
        FString BpName;
        FString CppName;
        FString BpType;
        FString CppType;
        FString UPropertySpec;
        FString AssetPath;
        FString CppDecl;
        FString CppLoad;
        FString CppHint;
    };

    struct FBlueprintOverrideInfo
    {
        FString FunctionName;
        FString CppEquivalent;
    };

    enum class EReflectedExportContext : uint8
    {
        GenericObject,
        BlueprintAsset,
        Graph,
        GraphNode,
        ComponentTemplate,
        TimelineTemplate
    };

    static FString BuildReflectedObjectText(const UObject* AssetObject);

    static void AppendIndentedLine(FString& Output, int32 IndentLevel, const FString& Line)
    {
        Output += FString::ChrN(IndentLevel * 2, TEXT(' '));
        Output += Line;
        Output += LINE_TERMINATOR;
    }

    static FString EscapeInlineValue(const FString& Value)
    {
        FString Escaped = Value;
        Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
        Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
        Escaped.ReplaceInline(TEXT("\t"), TEXT("\\t"));
        Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
        return Escaped;
    }

    static FString QuoteValue(const FString& Value)
    {
        return FString::Printf(TEXT("\"%s\""), *EscapeInlineValue(Value));
    }

    static FString NameOrNone(const FName Name)
    {
        return Name.IsNone() ? TEXT("None") : Name.ToString();
    }

    static FString TextOrEmpty(const FText& Text)
    {
        return Text.IsEmpty() ? FString() : Text.ToString();
    }

    static FString BoolToString(const bool bValue)
    {
        return bValue ? TEXT("true") : TEXT("false");
    }

    static FString GuidToString(const FGuid& Guid)
    {
        return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("None");
    }

    static FString ObjectPathOrNone(const UObject* Object)
    {
        return Object ? Object->GetPathName() : TEXT("None");
    }

    static FString SanitizeFileName(const FString& Input)
    {
        FString Result = Input;
        static const TCHAR* InvalidChars = TEXT("/\\:*?\"<>|");
        for (const TCHAR InvalidChar : FString(InvalidChars))
        {
            Result.ReplaceInline(*FString(1, &InvalidChar), TEXT("_"));
        }
        return Result;
    }

    static FString SanitizeDotIdentifier(const FString& Input)
    {
        FString Result;
        Result.Reserve(Input.Len() + 8);

        for (const TCHAR Char : Input)
        {
            if (FChar::IsAlnum(Char) || Char == TEXT('_'))
            {
                Result.AppendChar(Char);
            }
            else
            {
                Result.AppendChar(TEXT('_'));
            }
        }

        if (Result.IsEmpty() || FChar::IsDigit(Result[0]))
        {
            Result = TEXT("node_") + Result;
        }

        return Result;
    }

    static FString EscapeDotString(const FString& Input)
    {
        FString Result = Input;
        Result.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
        Result.ReplaceInline(TEXT("\""), TEXT("\\\""));
        Result.ReplaceInline(TEXT("\r"), TEXT(""));
        Result.ReplaceInline(TEXT("\n"), TEXT("\\n"));
        return Result;
    }

    static FString EscapeMermaidString(const FString& Input)
    {
        FString Result = Input;
        Result.ReplaceInline(TEXT("&"), TEXT("&amp;"));
        Result.ReplaceInline(TEXT("\""), TEXT("&quot;"));
        Result.ReplaceInline(TEXT("<"), TEXT("&lt;"));
        Result.ReplaceInline(TEXT(">"), TEXT("&gt;"));
        Result.ReplaceInline(TEXT("|"), TEXT("&#124;"));
        Result.ReplaceInline(TEXT("\r"), TEXT(""));
        Result.ReplaceInline(TEXT("\n"), TEXT("<br/>"));
        return Result;
    }

    static bool DoesClassMatchByName(const UClass* Class, const FName ExpectedClassName)
    {
        for (const UClass* CurrentClass = Class; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
        {
            if (CurrentClass->GetFName() == ExpectedClassName)
            {
                return true;
            }
        }

        return false;
    }

    static bool IsSupportedSpecialAssetType(const UObject* AssetObject)
    {
        if (!AssetObject)
        {
            return false;
        }

        const UClass* AssetClass = AssetObject->GetClass();
        return DoesClassMatchByName(AssetClass, TEXT("ChooserTable"))
            || DoesClassMatchByName(AssetClass, TEXT("ProxyTable"))
            || DoesClassMatchByName(AssetClass, TEXT("PoseSearchDatabase"))
            || DoesClassMatchByName(AssetClass, TEXT("PoseSearchSchema"));
    }

    static FString DescribeSupportedSpecialAssetType(const UObject* AssetObject)
    {
        if (!AssetObject)
        {
            return TEXT("unknown");
        }

        const UClass* AssetClass = AssetObject->GetClass();
        if (DoesClassMatchByName(AssetClass, TEXT("ChooserTable")))
        {
            return TEXT("chooser_table");
        }

        if (DoesClassMatchByName(AssetClass, TEXT("ProxyTable")))
        {
            return TEXT("proxy_table");
        }

        if (DoesClassMatchByName(AssetClass, TEXT("PoseSearchDatabase")))
        {
            return TEXT("pose_search_database");
        }

        if (DoesClassMatchByName(AssetClass, TEXT("PoseSearchSchema")))
        {
            return TEXT("pose_search_schema");
        }

        return TEXT("generic_object");
    }

    static FString GetSpecialAssetExportFolderName(const UObject* AssetObject)
    {
        const FString ExportType = DescribeSupportedSpecialAssetType(AssetObject);
        return ExportType.IsEmpty() || ExportType.Equals(TEXT("unknown"), ESearchCase::CaseSensitive)
            ? TEXT("special_assets")
            : ExportType;
    }

    static FString GetSpecialAssetExportDirectory(const FExportDirectorySet& Directories, const UObject* AssetObject)
    {
        const FString FolderName = GetSpecialAssetExportFolderName(AssetObject);
        return Directories.TxtRoot / FolderName;
    }

    static FString GetGraphKind(const UBlueprint* Blueprint, const UEdGraph* Graph)
    {
        auto ContainsGraph = [Graph](const TArray<TObjectPtr<UEdGraph>>& Graphs) -> bool
        {
            for (const UEdGraph* Candidate : Graphs)
            {
                if (Candidate == Graph)
                {
                    return true;
                }
            }
            return false;
        };

        if (!Blueprint || !Graph)
        {
            return TEXT("Unknown");
        }

        if (ContainsGraph(Blueprint->UbergraphPages))
        {
            return TEXT("Ubergraph");
        }
        if (ContainsGraph(Blueprint->FunctionGraphs))
        {
            return TEXT("Function");
        }
        if (ContainsGraph(Blueprint->DelegateSignatureGraphs))
        {
            return TEXT("DelegateSignature");
        }
        if (ContainsGraph(Blueprint->MacroGraphs))
        {
            return TEXT("Macro");
        }

        const FString GraphClassName = Graph->GetClass()->GetName();
        if (GraphClassName == TEXT("AnimationStateMachineGraph"))
        {
            return TEXT("AnimStateMachine");
        }
        if (GraphClassName == TEXT("AnimationStateGraph"))
        {
            return TEXT("AnimState");
        }
        if (GraphClassName == TEXT("AnimationTransitionGraph"))
        {
            return TEXT("AnimTransition");
        }
        if (GraphClassName == TEXT("AnimationConduitGraph"))
        {
            return TEXT("AnimConduit");
        }
        if (GraphClassName.Contains(TEXT("AnimationGraph")))
        {
            return TEXT("AnimGraph");
        }

        return TEXT("Other");
    }

    static FString DescribeEnumValue(const UEnum* Enum, const int64 Value)
    {
        if (!Enum)
        {
            return FString::Printf(TEXT("%lld"), static_cast<long long>(Value));
        }

        return Enum->GetNameStringByValue(Value);
    }

    static FString DescribeBlueprintUsage(const EBlueprintUsage Usage)
    {
        switch (Usage)
        {
        case EBlueprintUsage::NoProperties:
            return TEXT("NoProperties");
        case EBlueprintUsage::DoesNotUseBlueprint:
            return TEXT("DoesNotUseBlueprint");
        case EBlueprintUsage::UsesBlueprint:
            return TEXT("UsesBlueprint");
        default:
            return TEXT("Unknown");
        }
    }

    static FString DescribePreviewAnimationBlueprintApplicationMethod(const EPreviewAnimationBlueprintApplicationMethod Method)
    {
        switch (Method)
        {
        case EPreviewAnimationBlueprintApplicationMethod::LinkedLayers:
            return TEXT("LinkedLayers");
        case EPreviewAnimationBlueprintApplicationMethod::LinkedAnimGraph:
            return TEXT("LinkedAnimGraph");
        default:
            return TEXT("Unknown");
        }
    }

    static FString DescribeAnimStateType(const EAnimStateType StateType)
    {
        switch (StateType)
        {
        case AST_SingleAnimation:
            return TEXT("SingleAnimation");
        case AST_BlendGraph:
            return TEXT("BlendGraph");
        default:
            return TEXT("Unknown");
        }
    }

    static FString DescribePinDirection(const EEdGraphPinDirection Direction)
    {
        switch (Direction)
        {
        case EGPD_Input:
            return TEXT("Input");
        case EGPD_Output:
            return TEXT("Output");
        default:
            return TEXT("Unknown");
        }
    }

    static FString DescribeContainerType(const EPinContainerType ContainerType)
    {
        switch (ContainerType)
        {
        case EPinContainerType::None:
            return TEXT("None");
        case EPinContainerType::Array:
            return TEXT("Array");
        case EPinContainerType::Set:
            return TEXT("Set");
        case EPinContainerType::Map:
            return TEXT("Map");
        default:
            return TEXT("Unknown");
        }
    }

    static FString DescribeTerminalType(const FEdGraphTerminalType& TerminalType)
    {
        TArray<FString> Parts;
        Parts.Add(FString::Printf(TEXT("category=%s"), *NameOrNone(TerminalType.TerminalCategory)));
        if (!TerminalType.TerminalSubCategory.IsNone())
        {
            Parts.Add(FString::Printf(TEXT("subcategory=%s"), *TerminalType.TerminalSubCategory.ToString()));
        }
        if (TerminalType.TerminalSubCategoryObject.IsValid())
        {
            Parts.Add(FString::Printf(TEXT("subcategory_object=%s"), *ObjectPathOrNone(TerminalType.TerminalSubCategoryObject.Get())));
        }
        if (TerminalType.bTerminalIsConst)
        {
            Parts.Add(TEXT("const"));
        }
        if (TerminalType.bTerminalIsWeakPointer)
        {
            Parts.Add(TEXT("weak"));
        }
        if (TerminalType.bTerminalIsUObjectWrapper)
        {
            Parts.Add(TEXT("uobject_wrapper"));
        }

        return FString::Join(Parts, TEXT(", "));
    }

    static FString DescribeSimpleMemberReference(const FSimpleMemberReference& Reference)
    {
        TArray<FString> Parts;
        Parts.Add(FString::Printf(TEXT("name=%s"), *NameOrNone(Reference.MemberName)));
        Parts.Add(FString::Printf(TEXT("guid=%s"), *GuidToString(Reference.MemberGuid)));
        Parts.Add(FString::Printf(TEXT("parent=%s"), *ObjectPathOrNone(Reference.MemberParent)));
        return FString::Join(Parts, TEXT(", "));
    }

    static FString DescribePinType(const FEdGraphPinType& PinType)
    {
        TArray<FString> Parts;
        Parts.Add(FString::Printf(TEXT("category=%s"), *NameOrNone(PinType.PinCategory)));

        if (!PinType.PinSubCategory.IsNone())
        {
            Parts.Add(FString::Printf(TEXT("subcategory=%s"), *PinType.PinSubCategory.ToString()));
        }
        if (PinType.PinSubCategoryObject.IsValid())
        {
            Parts.Add(FString::Printf(TEXT("subcategory_object=%s"), *ObjectPathOrNone(PinType.PinSubCategoryObject.Get())));
        }
        if (PinType.PinSubCategoryMemberReference.MemberName != NAME_None || PinType.PinSubCategoryMemberReference.MemberGuid.IsValid() || PinType.PinSubCategoryMemberReference.MemberParent)
        {
            Parts.Add(FString::Printf(TEXT("subcategory_member={%s}"), *DescribeSimpleMemberReference(PinType.PinSubCategoryMemberReference)));
        }

        Parts.Add(FString::Printf(TEXT("container=%s"), *DescribeContainerType(PinType.ContainerType)));

        if (PinType.IsMap())
        {
            Parts.Add(FString::Printf(TEXT("value_type={%s}"), *DescribeTerminalType(PinType.PinValueType)));
        }
        if (PinType.bIsReference)
        {
            Parts.Add(TEXT("ref"));
        }
        if (PinType.bIsConst)
        {
            Parts.Add(TEXT("const"));
        }
        if (PinType.bIsWeakPointer)
        {
            Parts.Add(TEXT("weak"));
        }
        if (PinType.bIsUObjectWrapper)
        {
            Parts.Add(TEXT("uobject_wrapper"));
        }
        if (PinType.bSerializeAsSinglePrecisionFloat)
        {
            Parts.Add(TEXT("serialized_as_single_precision_float"));
        }

        return FString::Join(Parts, TEXT(", "));
    }

    static FString DescribePropertyFlags(const uint64 PropertyFlags)
    {
        TArray<FString> Flags;
        auto AddFlag = [&Flags, PropertyFlags](const uint64 Flag, const TCHAR* Label)
        {
            if ((PropertyFlags & Flag) != 0)
            {
                Flags.Add(Label);
            }
        };

        AddFlag(CPF_Edit, TEXT("Edit"));
        AddFlag(CPF_BlueprintVisible, TEXT("BlueprintVisible"));
        AddFlag(CPF_BlueprintReadOnly, TEXT("BlueprintReadOnly"));
        AddFlag(CPF_DisableEditOnInstance, TEXT("DisableEditOnInstance"));
        AddFlag(CPF_DisableEditOnTemplate, TEXT("DisableEditOnTemplate"));
        AddFlag(CPF_ExposeOnSpawn, TEXT("ExposeOnSpawn"));
        AddFlag(CPF_Transient, TEXT("Transient"));
        AddFlag(CPF_Config, TEXT("Config"));
        AddFlag(CPF_SaveGame, TEXT("SaveGame"));
        AddFlag(CPF_Net, TEXT("Replicated"));
        AddFlag(CPF_RepNotify, TEXT("RepNotify"));
        AddFlag(CPF_Interp, TEXT("Interp"));
        AddFlag(CPF_InstancedReference, TEXT("InstancedReference"));
        AddFlag(CPF_ContainsInstancedReference, TEXT("ContainsInstancedReference"));
        AddFlag(CPF_AssetRegistrySearchable, TEXT("AssetRegistrySearchable"));
        AddFlag(CPF_AdvancedDisplay, TEXT("AdvancedDisplay"));

        if (Flags.Num() == 0)
        {
            Flags.Add(TEXT("None"));
        }

        Flags.Add(FString::Printf(TEXT("raw=0x%llX"), static_cast<unsigned long long>(PropertyFlags)));
        return FString::Join(Flags, TEXT(", "));
    }

    static FString DescribeFunctionFlags(const int32 FunctionFlags)
    {
        TArray<FString> Flags;
        auto AddFlag = [&Flags, FunctionFlags](const int32 Flag, const TCHAR* Label)
        {
            if ((FunctionFlags & Flag) != 0)
            {
                Flags.Add(Label);
            }
        };

        AddFlag(FUNC_Public, TEXT("Public"));
        AddFlag(FUNC_Protected, TEXT("Protected"));
        AddFlag(FUNC_Private, TEXT("Private"));
        AddFlag(FUNC_BlueprintCallable, TEXT("BlueprintCallable"));
        AddFlag(FUNC_BlueprintPure, TEXT("BlueprintPure"));
        AddFlag(FUNC_BlueprintEvent, TEXT("BlueprintEvent"));
        AddFlag(FUNC_BlueprintAuthorityOnly, TEXT("BlueprintAuthorityOnly"));
        AddFlag(FUNC_BlueprintCosmetic, TEXT("BlueprintCosmetic"));
        AddFlag(FUNC_Const, TEXT("Const"));
        AddFlag(FUNC_Static, TEXT("Static"));
        AddFlag(FUNC_Net, TEXT("Net"));
        AddFlag(FUNC_NetMulticast, TEXT("NetMulticast"));
        AddFlag(FUNC_NetServer, TEXT("NetServer"));
        AddFlag(FUNC_NetClient, TEXT("NetClient"));
        AddFlag(FUNC_Event, TEXT("Event"));
        AddFlag(FUNC_Delegate, TEXT("Delegate"));
        if (Flags.Num() == 0)
        {
            Flags.Add(TEXT("None"));
        }

        Flags.Add(FString::Printf(TEXT("raw=0x%X"), FunctionFlags));
        return FString::Join(Flags, TEXT(", "));
    }

    static FString DescribeReplicationCondition(const ELifetimeCondition Condition)
    {
        const UEnum* Enum = StaticEnum<ELifetimeCondition>();
        return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Condition)) : FString::FromInt(static_cast<int32>(Condition));
    }

    static FString DescribeBlueprintType(const EBlueprintType BlueprintType)
    {
        const UEnum* Enum = StaticEnum<EBlueprintType>();
        return Enum ? Enum->GetNameStringByValue(static_cast<int64>(BlueprintType)) : FString::FromInt(static_cast<int32>(BlueprintType));
    }

    static FString GetNodeTitleString(const UEdGraphNode* Node)
    {
        if (!Node)
        {
            return TEXT("None");
        }

        FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        if (Title.IsEmpty())
        {
            Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
        }
        if (Title.IsEmpty())
        {
            Title = Node->GetName();
        }

        Title.ReplaceInline(TEXT("\r"), TEXT(" "));
        Title.ReplaceInline(TEXT("\n"), TEXT(" "));
        Title.TrimStartAndEndInline();
        return Title.IsEmpty() ? Node->GetName() : Title;
    }

    static FString GetNodeSemanticType(const UEdGraphNode* Node)
    {
        if (!Node)
        {
            return TEXT("Unknown");
        }
        if (Cast<UK2Node_FunctionEntry>(Node))
        {
            return TEXT("FunctionEntry");
        }
        if (Cast<UK2Node_FunctionResult>(Node))
        {
            return TEXT("FunctionResult");
        }
        if (Cast<UK2Node_CustomEvent>(Node))
        {
            return TEXT("CustomEvent");
        }
        if (Cast<UK2Node_Event>(Node))
        {
            return TEXT("Event");
        }
        if (Cast<UK2Node_CallFunction>(Node))
        {
            return TEXT("CallFunction");
        }
        if (Cast<UK2Node_VariableGet>(Node))
        {
            return TEXT("VariableGet");
        }
        if (Cast<UK2Node_VariableSet>(Node))
        {
            return TEXT("VariableSet");
        }
        if (Cast<UK2Node_DynamicCast>(Node))
        {
            return TEXT("DynamicCast");
        }
        if (Cast<UK2Node_MacroInstance>(Node))
        {
            return TEXT("MacroInstance");
        }
        if (Cast<UK2Node_Knot>(Node))
        {
            return TEXT("Knot");
        }
        if (Cast<UEdGraphNode_Comment>(Node))
        {
            return TEXT("Comment");
        }

        return Node->GetClass()->GetName();
    }

    static FString SanitizeCppIdentifier(const FString& Input)
    {
        FString Result;
        Result.Reserve(Input.Len() + 4);

        for (const TCHAR Char : Input)
        {
            if (FChar::IsAlnum(Char) || Char == TEXT('_'))
            {
                Result.AppendChar(Char);
            }
            else
            {
                Result.AppendChar(TEXT('_'));
            }
        }

        if (Result.IsEmpty())
        {
            return TEXT("GeneratedName");
        }

        if (FChar::IsDigit(Result[0]))
        {
            Result = TEXT("_") + Result;
        }

        return Result;
    }

    static FString GetCppClassTypeToken(const UClass* Class)
    {
        if (!Class)
        {
            return TEXT("UObject");
        }

        return Class->GetPrefixCPP() + Class->GetName();
    }

    static FString GetMatchTypeLabel(const ECppHintMatchType MatchType)
    {
        switch (MatchType)
        {
        case ECppHintMatchType::FunctionName:
            return TEXT("function_name");
        case ECppHintMatchType::EventName:
            return TEXT("event_name");
        case ECppHintMatchType::NodeClass:
            return TEXT("node_class");
        case ECppHintMatchType::SemanticType:
            return TEXT("semantic_type");
        case ECppHintMatchType::NodeTitleContains:
            return TEXT("node_title_contains");
        default:
            return TEXT("unknown");
        }
    }

    static const TArray<FCppHintMappingEntry>& GetCppHintMappingEntries()
    {
        static const TArray<FCppHintMappingEntry> Entries =
        {
            { TEXT("character_movement"), TEXT("Get CharacterMovement"), TEXT("GetCharacterMovement()"), TEXT("GameFramework/CharacterMovementComponent.h"), TEXT("Read movement component first, then query gait and speed fields from it"), ECppHintMatchType::FunctionName, TEXT("GetCharacterMovement") },
            { TEXT("character_movement"), TEXT("Is Crouching"), TEXT("MoveComp->IsCrouching()"), TEXT("GameFramework/CharacterMovementComponent.h"), TEXT("Use UCharacterMovementComponent crouch state to mirror Blueprint behavior"), ECppHintMatchType::FunctionName, TEXT("IsCrouching") },
            { TEXT("character_movement"), TEXT("Set Movement Mode"), TEXT("MoveComp->SetMovementMode(MOVE_Walking)"), TEXT("GameFramework/CharacterMovementComponent.h"), TEXT("Preserve Blueprint-selected MOVE_* enum per branch"), ECppHintMatchType::FunctionName, TEXT("SetMovementMode") },
            { TEXT("character_movement"), TEXT("Calculate Direction"), TEXT("UKismetAnimationLibrary::CalculateDirection(Velocity, Rotation)"), TEXT("KismetAnimationLibrary.h"), TEXT("Keep direction calculation in the same coordinate space as the Blueprint"), ECppHintMatchType::FunctionName, TEXT("CalculateDirection") },
            { TEXT("character_movement"), TEXT("Map Range Clamped"), TEXT("FMath::GetMappedRangeValueClamped(InRange, OutRange, Value)"), TEXT("Math/UnrealMathUtility.h"), TEXT("Use the clamped variant to preserve range boundaries"), ECppHintMatchType::FunctionName, TEXT("MapRangeClamped") },
            { TEXT("animation"), TEXT("Play Montage"), TEXT("AnimInstance->Montage_Play(Montage, PlayRate)"), TEXT("Animation/AnimInstance.h"), TEXT("After Montage_Play, bind completion with Montage_SetEndDelegate for deterministic callbacks"), ECppHintMatchType::NodeTitleContains, TEXT("Play Montage") },
            { TEXT("animation"), TEXT("Play Montage"), TEXT("AnimInstance->Montage_Play(Montage, PlayRate)"), TEXT("Animation/AnimInstance.h"), TEXT("After Montage_Play, bind completion with Montage_SetEndDelegate for deterministic callbacks"), ECppHintMatchType::FunctionName, TEXT("Montage_Play") },
            { TEXT("animation"), TEXT("On Completed / On Interrupted"), TEXT("AnimInstance->Montage_SetEndDelegate(Delegate, Montage)"), TEXT("Animation/AnimInstance.h"), TEXT("Prefer Montage_SetEndDelegate over global OnMontageEnded for target montage callbacks"), ECppHintMatchType::NodeTitleContains, TEXT("On Completed") },
            { TEXT("motion_warping"), TEXT("Add/Update Warp Target"), TEXT("MotionWarping->AddOrUpdateWarpTargetFromLocationAndRotation(Name, Location, Rotation)"), TEXT("MotionWarpingComponent.h"), TEXT("Warp target name must match montage notify target identifiers"), ECppHintMatchType::FunctionName, TEXT("AddOrUpdateWarpTargetFromLocationAndRotation") },
            { TEXT("motion_warping"), TEXT("Remove Warp Target"), TEXT("MotionWarping->RemoveWarpTarget(Name)"), TEXT("MotionWarpingComponent.h"), TEXT("Remove warp target when traversal ends to avoid stale targets"), ECppHintMatchType::FunctionName, TEXT("RemoveWarpTarget") },
            { TEXT("input"), TEXT("Setup Enhanced Input"), TEXT("Subsystem->AddMappingContext(IMC, Priority, Options)"), TEXT("EnhancedInputSubsystems.h"), TEXT("Set FModifyContextOptions::bIgnoreAllPressedKeysUntilRelease=true when matching Blueprint setup"), ECppHintMatchType::FunctionName, TEXT("AddMappingContext") },
            { TEXT("input"), TEXT("Bind Action"), TEXT("EIC->BindAction(IA, ETriggerEvent::Triggered, this, &Class::Handler)"), TEXT("EnhancedInputComponent.h"), TEXT("Match Blueprint trigger event type exactly (Triggered/Started/Completed)"), ECppHintMatchType::FunctionName, TEXT("BindAction") },
            { TEXT("collision"), TEXT("Ignore Component When Moving"), TEXT("Capsule->IgnoreComponentWhenMoving(Component, bIgnore)"), TEXT("Components/CapsuleComponent.h"), TEXT("Set true at traversal start and restore false at traversal end"), ECppHintMatchType::FunctionName, TEXT("IgnoreComponentWhenMoving") },
            { TEXT("collision"), TEXT("Line Trace By Channel"), TEXT("GetWorld()->LineTraceSingleByChannel(Hit, Start, End, Channel, Params)"), TEXT("Engine/World.h"), TEXT("Keep trace channel and query params aligned with Blueprint"), ECppHintMatchType::FunctionName, TEXT("LineTraceSingleByChannel") },
            { TEXT("audio"), TEXT("Play Sound At Location"), TEXT("UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location)"), TEXT("Kismet/GameplayStatics.h"), TEXT("Resolve event tag to SoundBase first, then call PlaySoundAtLocation"), ECppHintMatchType::FunctionName, TEXT("PlaySoundAtLocation") },
            { TEXT("flow_control"), TEXT("Branch"), TEXT("if (Condition) { ... } else { ... }"), TEXT(""), TEXT("Translate Branch directly to if/else"), ECppHintMatchType::NodeClass, TEXT("K2Node_IfThenElse") },
            { TEXT("flow_control"), TEXT("Sequence"), TEXT("Execute Then0/Then1 in order"), TEXT(""), TEXT("Keep Sequence output order without extra control constructs"), ECppHintMatchType::NodeClass, TEXT("K2Node_ExecutionSequence") },
            { TEXT("flow_control"), TEXT("Select (enum)"), TEXT("switch (EnumValue) { ... }"), TEXT(""), TEXT("Translate Select(enum) to switch and preserve default path"), ECppHintMatchType::NodeClass, TEXT("K2Node_Select") },
            { TEXT("flow_control"), TEXT("For Each Loop"), TEXT("for (auto& Item : Array) { ... }"), TEXT(""), TEXT("Use range-for and preserve Blueprint break behavior"), ECppHintMatchType::NodeClass, TEXT("K2Node_ForEachElementInArray") },
            { TEXT("flow_control"), TEXT("Delay"), TEXT("GetWorld()->GetTimerManager().SetTimer(Handle, Delegate, Duration, false)"), TEXT("TimerManager.h"), TEXT("Do not convert Delay to Sleep; use timer callbacks"), ECppHintMatchType::FunctionName, TEXT("Delay") },
            { TEXT("flow_control"), TEXT("FlipFlop"), TEXT("bToggle = !bToggle; if (bToggle) { ... } else { ... }"), TEXT(""), TEXT("Persist a boolean toggle state to emulate FlipFlop"), ECppHintMatchType::NodeClass, TEXT("K2Node_FlipFlop") },
            { TEXT("type_casting"), TEXT("Cast To X"), TEXT("Cast<X>(Object)"), TEXT("UObject/Cast.h"), TEXT("Always implement cast failure handling path"), ECppHintMatchType::NodeClass, TEXT("K2Node_DynamicCast") },
            { TEXT("type_casting"), TEXT("Is Valid"), TEXT("IsValid(Obj)"), TEXT("UObject/Object.h"), TEXT("Use IsValid for UObject references and != nullptr for raw pointers"), ECppHintMatchType::FunctionName, TEXT("IsValid") },
        };

        return Entries;
    }

    static bool MatchesCppHintEntry(const UEdGraphNode* Node, const FCppHintMappingEntry& Entry)
    {
        if (!Node)
        {
            return false;
        }

        const FString MatchValue = Entry.MatchValue ? Entry.MatchValue : TEXT("");

        switch (Entry.MatchType)
        {
        case ECppHintMatchType::FunctionName:
        {
            const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
            if (!CallFunctionNode)
            {
                return false;
            }

            const FString FunctionName = NameOrNone(CallFunctionNode->FunctionReference.GetMemberName());
            return FunctionName.Equals(MatchValue, ESearchCase::IgnoreCase);
        }
        case ECppHintMatchType::EventName:
        {
            const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
            if (!EventNode)
            {
                return false;
            }

            const FString EventName = NameOrNone(EventNode->EventReference.GetMemberName());
            const FString CustomName = NameOrNone(EventNode->CustomFunctionName);
            return EventName.Equals(MatchValue, ESearchCase::IgnoreCase)
                || CustomName.Equals(MatchValue, ESearchCase::IgnoreCase);
        }
        case ECppHintMatchType::NodeClass:
            return Node->GetClass()->GetName().Equals(MatchValue, ESearchCase::IgnoreCase);
        case ECppHintMatchType::SemanticType:
            return GetNodeSemanticType(Node).Equals(MatchValue, ESearchCase::IgnoreCase);
        case ECppHintMatchType::NodeTitleContains:
            return GetNodeTitleString(Node).Contains(MatchValue, ESearchCase::IgnoreCase);
        default:
            return false;
        }
    }

    static FString GetNodeCppHint(const UEdGraphNode* Node)
    {
        for (const FCppHintMappingEntry& Entry : GetCppHintMappingEntries())
        {
            if (MatchesCppHintEntry(Node, Entry))
            {
                const FString Hint = Entry.CppHint ? Entry.CppHint : TEXT("");
                if (!Hint.IsEmpty())
                {
                    return Hint;
                }

                return Entry.Cpp ? Entry.Cpp : TEXT("");
            }
        }

        return FString();
    }

    static FString BuildBpToCppMapYaml()
    {
        FString Output;
        AppendIndentedLine(Output, 0, TEXT("# Blueprint node -> C++ mapping (auto-generated)"));
        AppendIndentedLine(Output, 0, TEXT("# This file is consumed by AI-assisted BP -> C++ conversion."));

        TArray<FString> Categories;
        TSet<FString> SeenCategories;
        for (const FCppHintMappingEntry& Entry : GetCppHintMappingEntries())
        {
            const FString Category = Entry.Category ? Entry.Category : TEXT("other");
            if (!SeenCategories.Contains(Category))
            {
                SeenCategories.Add(Category);
                Categories.Add(Category);
            }
        }

        for (const FString& Category : Categories)
        {
            AppendIndentedLine(Output, 0, FString::Printf(TEXT("%s:"), *Category));
            for (const FCppHintMappingEntry& Entry : GetCppHintMappingEntries())
            {
                const FString EntryCategory = Entry.Category ? Entry.Category : TEXT("other");
                if (!EntryCategory.Equals(Category, ESearchCase::CaseSensitive))
                {
                    continue;
                }

                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("bp: %s"), *QuoteValue(Entry.BlueprintNode ? Entry.BlueprintNode : TEXT(""))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp: %s"), *QuoteValue(Entry.Cpp ? Entry.Cpp : TEXT(""))));

                const FString Include = Entry.Include ? Entry.Include : TEXT("");
                if (!Include.IsEmpty())
                {
                    AppendIndentedLine(Output, 2, FString::Printf(TEXT("include: %s"), *QuoteValue(Include)));
                }

                const FString Hint = Entry.CppHint ? Entry.CppHint : TEXT("");
                if (!Hint.IsEmpty())
                {
                    AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_hint: %s"), *QuoteValue(Hint)));
                }

                AppendIndentedLine(Output, 2, FString::Printf(TEXT("match_type: %s"), *QuoteValue(GetMatchTypeLabel(Entry.MatchType))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("match_value: %s"), *QuoteValue(Entry.MatchValue ? Entry.MatchValue : TEXT(""))));
            }
        }

        return Output;
    }

    static FString GetNodeStableId(const UEdGraphNode* Node)
    {
        if (!Node)
        {
            return TEXT("node_none");
        }
        if (Node->NodeGuid.IsValid())
        {
            return SanitizeDotIdentifier(TEXT("node_") + Node->NodeGuid.ToString(EGuidFormats::Digits));
        }
        return SanitizeDotIdentifier(TEXT("node_") + Node->GetName());
    }

    static bool IsExecPin(const UEdGraphPin* Pin)
    {
        return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
    }

    static bool IsCommentNode(const UEdGraphNode* Node)
    {
        return Node && Cast<UEdGraphNode_Comment>(Node) != nullptr;
    }

    static bool IsKnotNode(const UEdGraphNode* Node)
    {
        return Node && Cast<UK2Node_Knot>(Node) != nullptr;
    }

    static bool HasExecPins(const UEdGraphNode* Node)
    {
        if (!Node)
        {
            return false;
        }

        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (IsExecPin(Pin))
            {
                return true;
            }
        }

        return false;
    }

    static bool IsImportantStandaloneNode(const UEdGraphNode* Node)
    {
        return Node
            && (Cast<UK2Node_FunctionEntry>(Node)
                || Cast<UK2Node_FunctionResult>(Node)
                || Cast<UK2Node_Event>(Node)
                || Cast<UK2Node_CustomEvent>(Node)
                || Cast<UK2Node_CallFunction>(Node)
                || Cast<UK2Node_VariableGet>(Node)
                || Cast<UK2Node_VariableSet>(Node)
                || Cast<UK2Node_DynamicCast>(Node)
                || Cast<UK2Node_MacroInstance>(Node)
                || Cast<UAnimGraphNode_Base>(Node)
                || Cast<UAnimStateNodeBase>(Node));
    }

    static bool IsConditionPin(const UEdGraphPin* Pin)
    {
        if (!Pin)
        {
            return false;
        }

        if (Pin->PinName == UEdGraphSchema_K2::PN_Condition)
        {
            return true;
        }

        const FString PinName = Pin->PinName.ToString();
        return PinName.Contains(TEXT("Condition"));
    }

    static void GatherLogicalTargets(const UEdGraphPin* SourcePin, TArray<const UEdGraphPin*>& OutTargets, TSet<const UEdGraphPin*>& VisitedPins)
    {
        if (!SourcePin || VisitedPins.Contains(SourcePin))
        {
            return;
        }

        VisitedPins.Add(SourcePin);

        for (const UEdGraphPin* LinkedPin : SourcePin->LinkedTo)
        {
            if (!LinkedPin)
            {
                continue;
            }

            const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNodeUnchecked();
            if (const UK2Node_Knot* KnotNode = Cast<UK2Node_Knot>(LinkedNode))
            {
                if (const UEdGraphPin* PassThroughPin = KnotNode->GetPassThroughPin(LinkedPin))
                {
                    GatherLogicalTargets(PassThroughPin, OutTargets, VisitedPins);
                }
                continue;
            }

            OutTargets.Add(LinkedPin);
        }
    }

    static bool ShouldIncludeDataDependency(const UEdGraphPin* SourcePin, const UEdGraphPin* TargetPin)
    {
        if (!SourcePin || !TargetPin || IsExecPin(SourcePin) || IsExecPin(TargetPin))
        {
            return false;
        }

        const UEdGraphNode* SourceNode = SourcePin->GetOwningNodeUnchecked();
        const UEdGraphNode* TargetNode = TargetPin->GetOwningNodeUnchecked();
        if (!SourceNode || !TargetNode || IsCommentNode(SourceNode) || IsCommentNode(TargetNode))
        {
            return false;
        }

        if (IsConditionPin(TargetPin))
        {
            return true;
        }

        if (Cast<UAnimGraphNode_Base>(SourceNode)
            || Cast<UAnimGraphNode_Base>(TargetNode)
            || Cast<UAnimStateNodeBase>(SourceNode)
            || Cast<UAnimStateNodeBase>(TargetNode))
        {
            return true;
        }

        return Cast<UK2Node_VariableGet>(SourceNode)
            || Cast<UK2Node_FunctionEntry>(SourceNode)
            || Cast<UK2Node_Event>(SourceNode)
            || Cast<UK2Node_CustomEvent>(SourceNode)
            || Cast<UK2Node_CallFunction>(SourceNode)
            || Cast<UK2Node_VariableSet>(TargetNode)
            || Cast<UK2Node_CallFunction>(TargetNode)
            || Cast<UK2Node_DynamicCast>(TargetNode)
            || Cast<UK2Node_FunctionResult>(TargetNode);
    }

    static FString GetExecEdgeLabel(const UEdGraphPin* SourcePin)
    {
        if (!SourcePin)
        {
            return TEXT("exec");
        }

        const FString PinName = SourcePin->PinName.ToString();
        if (PinName.IsEmpty() || SourcePin->PinName == UEdGraphSchema_K2::PN_Then || SourcePin->PinName == UEdGraphSchema_K2::PN_Execute)
        {
            return TEXT("exec");
        }

        return FString::Printf(TEXT("exec:%s"), *PinName);
    }

    static FString GetDataEdgeLabel(const UEdGraphPin* TargetPin)
    {
        if (!TargetPin || TargetPin->PinName.IsNone())
        {
            return TEXT("data");
        }

        return FString::Printf(TEXT("data:%s"), *TargetPin->PinName.ToString());
    }

    static const FProperty* FindGeneratedClassProperty(const UBlueprint* Blueprint, const FName PropertyName)
    {
        const UClass* GeneratedClass = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
        if (!GeneratedClass)
        {
            return nullptr;
        }

        for (TFieldIterator<FProperty> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            if (It->GetFName() == PropertyName)
            {
                return *It;
            }
        }

        return FindFProperty<FProperty>(GeneratedClass, PropertyName);
    }

    static const FProperty* FindStructProperty(const UStruct* Struct, const FName PropertyName)
    {
        if (!Struct)
        {
            return nullptr;
        }

        for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            if (It->GetFName() == PropertyName)
            {
                return *It;
            }
        }

        return FindFProperty<FProperty>(Struct, PropertyName);
    }

    static FString ExportPropertyValueFromContainer(const FProperty* Property, const void* ContainerPtr, const UObject* ValueOwner)
    {
        if (!Property || !ContainerPtr)
        {
            return FString();
        }

        FString ExportedValue;
        const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
        Property->ExportTextItem_Direct(ExportedValue, ValuePtr, ValuePtr, const_cast<UObject*>(ValueOwner), PPF_None);
        return ExportedValue;
    }

    static FString ExportPropertyValueFromValuePtr(const FProperty* Property, const void* ValuePtr, const UObject* ValueOwner)
    {
        if (!Property || !ValuePtr)
        {
            return FString();
        }

        FString ExportedValue;
        Property->ExportTextItem_Direct(ExportedValue, ValuePtr, ValuePtr, const_cast<UObject*>(ValueOwner), PPF_None);
        return ExportedValue;
    }

    static FString ExportPropertyDefaultValue(const FProperty* Property, const UObject* DefaultObject)
    {
        return ExportPropertyValueFromContainer(Property, DefaultObject, DefaultObject);
    }

    static void AppendStructFieldValues(
        FString& Output,
        const int32 IndentLevel,
        const UStruct* StructType,
        const void* StructValuePtr,
        const UObject* ValueOwner)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("properties:"));
        if (!StructType || !StructValuePtr)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
            return;
        }

        bool bHasProperties = false;
        for (TFieldIterator<FProperty> It(StructType, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Property = *It;
            if (!Property)
            {
                continue;
            }

            bHasProperties = true;
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("name: %s"), *QuoteValue(Property->GetName())));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Property->GetCPPType())));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("value: %s"), *QuoteValue(ExportPropertyValueFromContainer(Property, StructValuePtr, ValueOwner))));
        }

        if (!bHasProperties)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
        }
    }

    static void AppendInputActionMappings(FString& Output, const int32 IndentLevel, const UInputSettings* InputSettings)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("action_mappings:"));
        if (!InputSettings)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
            return;
        }

        TArray<FInputActionKeyMapping> ActionMappings = InputSettings->GetActionMappings();
        ActionMappings.Sort([](const FInputActionKeyMapping& A, const FInputActionKeyMapping& B)
        {
            if (A.ActionName != B.ActionName)
            {
                return A.ActionName.ToString() < B.ActionName.ToString();
            }

            return A.Key.ToString() < B.Key.ToString();
        });

        if (ActionMappings.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
            return;
        }

        for (const FInputActionKeyMapping& Mapping : ActionMappings)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("action_name: %s"), *QuoteValue(Mapping.ActionName.ToString())));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("key: %s"), *QuoteValue(Mapping.Key.ToString())));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("shift: %s"), *BoolToString(Mapping.bShift)));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("ctrl: %s"), *BoolToString(Mapping.bCtrl)));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("alt: %s"), *BoolToString(Mapping.bAlt)));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("cmd: %s"), *BoolToString(Mapping.bCmd)));
        }
    }

    static void AppendInputMappingContextMappings(FString& Output, const int32 IndentLevel, const UObject* MappingContextObject)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("mappings:"));
        if (!MappingContextObject)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
            return;
        }

        const FArrayProperty* MappingsProperty = CastField<FArrayProperty>(FindStructProperty(MappingContextObject->GetClass(), TEXT("Mappings")));
        if (!MappingsProperty)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
            return;
        }

        const void* ArrayPtr = MappingsProperty->ContainerPtrToValuePtr<void>(MappingContextObject);
        FScriptArrayHelper ArrayHelper(MappingsProperty, ArrayPtr);
        if (ArrayHelper.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
            return;
        }

        const FStructProperty* MappingStructProperty = CastField<FStructProperty>(MappingsProperty->Inner);
        for (int32 MappingIndex = 0; MappingIndex < ArrayHelper.Num(); ++MappingIndex)
        {
            const void* MappingValuePtr = ArrayHelper.GetRawPtr(MappingIndex);
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("index: %d"), MappingIndex));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("serialized: %s"), *QuoteValue(ExportPropertyValueFromValuePtr(MappingsProperty->Inner, MappingValuePtr, MappingContextObject))));
            AppendStructFieldValues(Output, IndentLevel + 2, MappingStructProperty ? MappingStructProperty->Struct : nullptr, MappingValuePtr, MappingContextObject);
        }
    }

    static void ExportProjectInputMappings(const FExportDirectorySet& Directories)
    {
        FString Output;
        AppendIndentedLine(Output, 0, TEXT("project_input:"));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("source_config_file: %s"), *QuoteValue(TEXT("Config/DefaultInput.ini"))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("input_settings_class: %s"), *QuoteValue(TEXT("/Script/Engine.InputSettings"))));

        const UInputSettings* InputSettings = GetDefault<UInputSettings>();
        AppendInputActionMappings(Output, 1, InputSettings);

        AppendIndentedLine(Output, 1, TEXT("input_mapping_contexts:"));

        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
        if (AssetRegistry.IsLoadingAssets())
        {
            AssetRegistry.WaitForCompletion();
        }

        FARFilter MappingContextFilter;
        MappingContextFilter.PackagePaths.Add(TEXT("/Game"));
        MappingContextFilter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/EnhancedInput"), TEXT("InputMappingContext")));
        MappingContextFilter.bRecursivePaths = true;
        MappingContextFilter.bRecursiveClasses = true;

        TArray<FAssetData> MappingContextAssets;
        AssetRegistry.GetAssets(MappingContextFilter, MappingContextAssets);
        MappingContextAssets.Sort([](const FAssetData& A, const FAssetData& B)
        {
            return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
        });

        const FString MappingContextExportRoot = Directories.TxtOtherRoot / TEXT("input_mapping_contexts");
        IFileManager::Get().MakeDirectory(*MappingContextExportRoot, true);

        if (MappingContextAssets.Num() == 0)
        {
            AppendIndentedLine(Output, 2, TEXT("[]"));
        }
        else
        {
            for (const FAssetData& MappingContextAsset : MappingContextAssets)
            {
                const FString ObjectPath = MappingContextAsset.GetSoftObjectPath().ToString();
                const FString ExportFileName = SanitizeFileName(ObjectPath) + TEXT(".txt");
                const FString ExportFilePath = MappingContextExportRoot / ExportFileName;
                const FString ExportFileRelativePath = FString::Printf(TEXT("txt/other/input_mapping_contexts/%s"), *ExportFileName);
#if ENGINE_MAJOR_VERSION >= 5
                const FString MappingContextAssetClass = MappingContextAsset.AssetClassPath.ToString();
#else
                const FString MappingContextAssetClass = MappingContextAsset.AssetClass.ToString();
#endif

                UObject* MappingContextObject = MappingContextAsset.GetSoftObjectPath().TryLoad();
                if (MappingContextObject)
                {
                    FFileHelper::SaveStringToFile(BuildReflectedObjectText(MappingContextObject), *ExportFilePath, FFileHelper::EEncodingOptions::ForceUTF8);
                }

                AppendIndentedLine(Output, 2, TEXT("-"));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("name: %s"), *QuoteValue(MappingContextAsset.AssetName.ToString())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("asset_path: %s"), *QuoteValue(ObjectPath)));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("asset_class: %s"), *QuoteValue(MappingContextAssetClass)));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("export_file: %s"), *QuoteValue(ExportFileRelativePath)));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("load_succeeded: %s"), *BoolToString(MappingContextObject != nullptr)));
                AppendInputMappingContextMappings(Output, 3, MappingContextObject);
            }
        }

        const FString ProjectInputMappingsPath = Directories.MappingsRoot / TEXT("ProjectInputMappings.yaml");
        FFileHelper::SaveStringToFile(Output, *ProjectInputMappingsPath, FFileHelper::EEncodingOptions::ForceUTF8);
    }

    static bool ShouldSkipReflectedProperty(const FProperty* Property, const EReflectedExportContext Context)
    {
        if (!Property)
        {
            return true;
        }

        const FName PropertyName = Property->GetFName();
        switch (Context)
        {
        case EReflectedExportContext::BlueprintAsset:
            return PropertyName == TEXT("SimpleConstructionScript")
                || PropertyName == TEXT("ComponentTemplates")
                || PropertyName == TEXT("Timelines")
                || PropertyName == TEXT("InheritableComponentHandler")
                || PropertyName == TEXT("NewVariables")
                || PropertyName == TEXT("ImplementedInterfaces")
                || PropertyName == TEXT("GeneratedClass")
                || PropertyName == TEXT("SkeletonGeneratedClass")
                || PropertyName == TEXT("ParentClass")
                || PropertyName == TEXT("UbergraphPages")
                || PropertyName == TEXT("FunctionGraphs")
                || PropertyName == TEXT("DelegateSignatureGraphs")
                || PropertyName == TEXT("MacroGraphs")
                || PropertyName == TEXT("LastEditedDocuments");
        case EReflectedExportContext::Graph:
            return PropertyName == TEXT("Nodes")
                || PropertyName == TEXT("Schema");
        case EReflectedExportContext::GraphNode:
            return PropertyName == TEXT("Pins")
                || PropertyName == TEXT("SubGraphs")
                || PropertyName == TEXT("NodePosX")
                || PropertyName == TEXT("NodePosY")
                || PropertyName == TEXT("NodeComment")
                || PropertyName == TEXT("AdvancedPinDisplay")
                || PropertyName == TEXT("ErrorType")
                || PropertyName == TEXT("ErrorMsg")
                || PropertyName == TEXT("NodeUpgradeMessage")
                || PropertyName == TEXT("bCommentBubbleVisible")
                || PropertyName == TEXT("bCommentBubblePinned");
        case EReflectedExportContext::TimelineTemplate:
            return PropertyName == TEXT("EventTracks")
                || PropertyName == TEXT("FloatTracks")
                || PropertyName == TEXT("VectorTracks")
                || PropertyName == TEXT("LinearColorTracks")
                || PropertyName == TEXT("MetaDataArray");
        case EReflectedExportContext::ComponentTemplate:
        {
            // Skip all object references (prevent sync-loading heavy assets like SkeletalMesh, PhysicsAsset, AnimClass)
            if (CastField<FObjectPropertyBase>(Property))
            {
                return true;
            }
            // Skip arrays/maps/sets to avoid serializing large data blobs (e.g. pose arrays)
            if (CastField<FArrayProperty>(Property)
                || CastField<FMapProperty>(Property)
                || CastField<FSetProperty>(Property))
            {
                return true;
            }
            // Skip known-heavy or editor-only noise properties
            return PropertyName == TEXT("AssetImportData")
                || PropertyName == TEXT("ThumbnailInfo")
                || PropertyName == TEXT("CachedMeshResourceVersion")
                || PropertyName == TEXT("BodySetup")
                || PropertyName == TEXT("PhysicsAsset")
                || PropertyName == TEXT("ClothingSimulationFactory")
                || PropertyName == TEXT("MorphTargets")
                || PropertyName == TEXT("LODInfo")
                || PropertyName == TEXT("Materials")
                || PropertyName == TEXT("OverrideMaterials")
                || PropertyName == TEXT("Sockets");
        }
        case EReflectedExportContext::GenericObject:
        {
            // For generic objects (ChooserTable, PoseSearchDatabase etc.) skip object refs and containers
            // to avoid triggering asset loads and serializing unbounded data arrays.
            if (CastField<FObjectPropertyBase>(Property))
            {
                return true;
            }
            if (CastField<FArrayProperty>(Property)
                || CastField<FMapProperty>(Property)
                || CastField<FSetProperty>(Property))
            {
                return true;
            }
            return PropertyName == TEXT("AssetImportData")
                || PropertyName == TEXT("ThumbnailInfo");
        }
        default:
            return false;
        }
    }

    static bool IsProjectAssetPath(const FString& ObjectPath)
    {
        return ObjectPath.StartsWith(TEXT("/Game/"));
    }

    static bool IsProjectAssetObject(const UObject* Object)
    {
        return Object && IsProjectAssetPath(Object->GetPathName());
    }

    static FString BuildUPropertySpecifier(const uint64 PropertyFlags, const FString& InCategory, const bool bForceVisibleAnywhere = false)
    {
        TArray<FString> Specifiers;
        if (bForceVisibleAnywhere)
        {
            Specifiers.Add(TEXT("VisibleAnywhere"));
        }
        else if ((PropertyFlags & CPF_Edit) != 0)
        {
            Specifiers.Add(TEXT("EditAnywhere"));
        }
        else
        {
            Specifiers.Add(TEXT("VisibleAnywhere"));
        }

        if ((PropertyFlags & CPF_BlueprintVisible) != 0)
        {
            Specifiers.Add((PropertyFlags & CPF_BlueprintReadOnly) != 0 ? TEXT("BlueprintReadOnly") : TEXT("BlueprintReadWrite"));
        }
        else
        {
            Specifiers.Add(TEXT("BlueprintReadOnly"));
        }

        FString Category = InCategory;
        Category.ReplaceInline(TEXT("\""), TEXT(""));
        Category.TrimStartAndEndInline();
        if (Category.IsEmpty())
        {
            Category = TEXT("Default");
        }

        Specifiers.Add(FString::Printf(TEXT("Category=\"%s\""), *Category));
        return FString::Join(Specifiers, TEXT(", "));
    }

    static FString BuildPropertyCppDecl(const FProperty* Property, const FString& PropertyName, const FString& UPropertySpec)
    {
        const FString SafePropertyName = SanitizeCppIdentifier(PropertyName);

        if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            const FString ObjectType = GetCppClassTypeToken(ObjectProperty->PropertyClass);
            return FString::Printf(TEXT("UPROPERTY(%s) TObjectPtr<%s> %s = nullptr;"), *UPropertySpec, *ObjectType, *SafePropertyName);
        }

        if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
        {
            const FString ObjectType = GetCppClassTypeToken(SoftObjectProperty->PropertyClass);
            return FString::Printf(TEXT("UPROPERTY(%s) TSoftObjectPtr<%s> %s;"), *UPropertySpec, *ObjectType, *SafePropertyName);
        }

        FString CppType = Property ? Property->GetCPPType() : FString();
        if (CppType.IsEmpty())
        {
            CppType = TEXT("int32");
        }

        return FString::Printf(TEXT("UPROPERTY(%s) %s %s;"), *UPropertySpec, *CppType, *SafePropertyName);
    }

    static void AddComponentFromSCSNode(const USCS_Node* Node, const FString& ParentName, TMap<FString, FVariableYamlComponent>& InOutComponents)
    {
        if (!Node)
        {
            return;
        }

        FString ComponentName = NameOrNone(Node->GetVariableName());
        if (ComponentName.IsEmpty() || ComponentName == TEXT("None"))
        {
            ComponentName = Node->ComponentTemplate ? Node->ComponentTemplate->GetName() : FString();
        }

        const UClass* ComponentClass = Node->ComponentClass.Get();
        if (!ComponentClass && Node->ComponentTemplate)
        {
            ComponentClass = Node->ComponentTemplate->GetClass();
        }

        if (!ComponentName.IsEmpty() && !InOutComponents.Contains(ComponentName))
        {
            const FString ComponentTypeToken = GetCppClassTypeToken(ComponentClass);
            FVariableYamlComponent& Component = InOutComponents.Add(ComponentName);
            Component.Name = ComponentName;
            Component.CppType = FString::Printf(TEXT("%s*"), *ComponentTypeToken);
            Component.UPropertySpec = TEXT("VisibleAnywhere, BlueprintReadOnly, Category=\"Components\"");
            Component.CppDecl = FString::Printf(TEXT("UPROPERTY(%s) TObjectPtr<%s> %s = nullptr;"), *Component.UPropertySpec, *ComponentTypeToken, *SanitizeCppIdentifier(ComponentName));
            Component.CppInit = FString::Printf(TEXT("CreateDefaultSubobject<%s>(TEXT(\"%s\"));"), *ComponentTypeToken, *ComponentName);
            Component.Attachment = ParentName.IsEmpty() ? TEXT("RootComponent") : ParentName;
        }

        const FString NextParentName = ComponentName.IsEmpty() ? ParentName : ComponentName;
        for (const USCS_Node* ChildNode : Node->GetChildNodes())
        {
            AddComponentFromSCSNode(ChildNode, NextParentName, InOutComponents);
        }
    }

    static void AddComponentTemplateIfMissing(const UActorComponent* ComponentTemplate, TMap<FString, FVariableYamlComponent>& InOutComponents)
    {
        if (!ComponentTemplate)
        {
            return;
        }

        const FString ComponentName = ComponentTemplate->GetName();
        if (ComponentName.IsEmpty() || InOutComponents.Contains(ComponentName))
        {
            return;
        }

        const UClass* ComponentClass = ComponentTemplate->GetClass();
        const FString ComponentTypeToken = GetCppClassTypeToken(ComponentClass);
        FVariableYamlComponent& Component = InOutComponents.Add(ComponentName);
        Component.Name = ComponentName;
        Component.CppType = FString::Printf(TEXT("%s*"), *ComponentTypeToken);
        Component.UPropertySpec = TEXT("VisibleAnywhere, BlueprintReadOnly, Category=\"Components\"");
        Component.CppDecl = FString::Printf(TEXT("UPROPERTY(%s) TObjectPtr<%s> %s = nullptr;"), *Component.UPropertySpec, *ComponentTypeToken, *SanitizeCppIdentifier(ComponentName));
        Component.CppInit = FString::Printf(TEXT("CreateDefaultSubobject<%s>(TEXT(\"%s\"));"), *ComponentTypeToken, *ComponentName);
        Component.Attachment = TEXT("None");
    }

    static void CollectUserTypesFromProperty(const FProperty* Property, TSet<const UEnum*>& OutEnums, TSet<const UScriptStruct*>& OutStructs)
    {
        if (!Property)
        {
            return;
        }

        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            if (const UEnum* Enum = EnumProperty->GetEnum())
            {
                if (IsProjectAssetObject(Enum))
                {
                    OutEnums.Add(Enum);
                }
            }
            return;
        }

        if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            if (const UEnum* Enum = ByteProperty->Enum)
            {
                if (IsProjectAssetObject(Enum))
                {
                    OutEnums.Add(Enum);
                }
            }
            return;
        }

        if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            if (const UScriptStruct* Struct = StructProperty->Struct)
            {
                if (IsProjectAssetObject(Struct))
                {
                    OutStructs.Add(Struct);
                }
            }
            return;
        }

        if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            CollectUserTypesFromProperty(ArrayProperty->Inner, OutEnums, OutStructs);
            return;
        }

        if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
        {
            CollectUserTypesFromProperty(SetProperty->ElementProp, OutEnums, OutStructs);
            return;
        }

        if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
        {
            CollectUserTypesFromProperty(MapProperty->KeyProp, OutEnums, OutStructs);
            CollectUserTypesFromProperty(MapProperty->ValueProp, OutEnums, OutStructs);
        }
    }

    static void CollectUserTypesFromPinType(const FEdGraphPinType& PinType, TSet<const UEnum*>& OutEnums, TSet<const UScriptStruct*>& OutStructs)
    {
        if (const UObject* SubCategoryObject = PinType.PinSubCategoryObject.Get())
        {
            if (const UEnum* Enum = Cast<UEnum>(SubCategoryObject))
            {
                if (IsProjectAssetObject(Enum))
                {
                    OutEnums.Add(Enum);
                }
            }
            else if (const UScriptStruct* Struct = Cast<UScriptStruct>(SubCategoryObject))
            {
                if (IsProjectAssetObject(Struct))
                {
                    OutStructs.Add(Struct);
                }
            }
        }
    }

    static void AddAssetFromProperty(
        const FProperty* Property,
        const UObject* DefaultObject,
        const FString& Category,
        TMap<FString, FVariableYamlAsset>& InOutAssets)
    {
        if (!Property || !DefaultObject)
        {
            return;
        }

        const FString PropertyName = Property->GetName();
        const FString CppName = SanitizeCppIdentifier(PropertyName);
        const FString UPropertySpec = BuildUPropertySpecifier(Property->PropertyFlags, Category);

        if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            UObject* AssetObject = ObjectProperty->GetObjectPropertyValue_InContainer(DefaultObject);
            if (!IsProjectAssetObject(AssetObject))
            {
                return;
            }

            FVariableYamlAsset Asset;
            Asset.BpName = PropertyName;
            Asset.CppName = CppName;
            Asset.BpType = ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetName() : TEXT("Object");
            Asset.CppType = FString::Printf(TEXT("%s*"), *GetCppClassTypeToken(ObjectProperty->PropertyClass));
            Asset.UPropertySpec = UPropertySpec;
            Asset.AssetPath = AssetObject->GetPathName();
            Asset.CppDecl = BuildPropertyCppDecl(Property, PropertyName, UPropertySpec);
            Asset.CppLoad = FString::Printf(TEXT("ConstructorHelpers::FObjectFinder<%s> Finder(TEXT(\"%s\"));"), *GetCppClassTypeToken(ObjectProperty->PropertyClass), *Asset.AssetPath);
            Asset.CppHint = TEXT("Hard reference; load in constructor via ConstructorHelpers::FObjectFinder");
            InOutAssets.FindOrAdd(Asset.BpName) = Asset;
            return;
        }

        if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
        {
            const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue_InContainer(DefaultObject);
            const FSoftObjectPath SoftPath = SoftObject.ToSoftObjectPath();
            if (!SoftPath.IsValid() || !IsProjectAssetPath(SoftPath.ToString()))
            {
                return;
            }

            FVariableYamlAsset Asset;
            Asset.BpName = PropertyName;
            Asset.CppName = CppName;
            Asset.BpType = SoftObjectProperty->PropertyClass ? SoftObjectProperty->PropertyClass->GetName() : TEXT("Object");
            Asset.CppType = FString::Printf(TEXT("TSoftObjectPtr<%s>"), *GetCppClassTypeToken(SoftObjectProperty->PropertyClass));
            Asset.UPropertySpec = UPropertySpec;
            Asset.AssetPath = SoftPath.ToString();
            Asset.CppDecl = BuildPropertyCppDecl(Property, PropertyName, UPropertySpec);
            Asset.CppLoad = TEXT("AssetRef.LoadSynchronous() // or StreamableManager for async loading");
            Asset.CppHint = TEXT("Soft reference; load on demand using LoadSynchronous or StreamableManager");
            InOutAssets.FindOrAdd(Asset.BpName) = Asset;
        }
    }

    static FString GetOverrideCppSignature(const FString& FunctionName)
    {
        if (FunctionName.Equals(TEXT("ReceiveTick"), ESearchCase::IgnoreCase))
        {
            return TEXT("Tick(float DeltaSeconds)");
        }
        if (FunctionName.Equals(TEXT("ReceiveBeginPlay"), ESearchCase::IgnoreCase))
        {
            return TEXT("BeginPlay()");
        }
        if (FunctionName.Equals(TEXT("ReceivePossessed"), ESearchCase::IgnoreCase))
        {
            return TEXT("PossessedBy(AController* NewController)");
        }
        if (FunctionName.Equals(TEXT("OnLanded"), ESearchCase::IgnoreCase))
        {
            return TEXT("Landed(const FHitResult& Hit)");
        }
        if (FunctionName.Equals(TEXT("OnJumped"), ESearchCase::IgnoreCase))
        {
            return TEXT("OnJumped_Implementation()");
        }

        return FunctionName + TEXT("()");
    }

    static FString BuildVariablesYaml(UBlueprint* Blueprint)
    {
        FString Output;
        if (!Blueprint)
        {
            return Output;
        }

        const UClass* GeneratedClass = Blueprint->GeneratedClass.Get();
        const UObject* DefaultObject = GeneratedClass ? GeneratedClass->GetDefaultObject() : nullptr;

        TMap<FString, FVariableYamlComponent> ComponentsByName;
        if (Blueprint->SimpleConstructionScript)
        {
            for (const USCS_Node* RootNode : Blueprint->SimpleConstructionScript->GetRootNodes())
            {
                AddComponentFromSCSNode(RootNode, FString(), ComponentsByName);
            }
        }

        for (const UActorComponent* ComponentTemplate : Blueprint->ComponentTemplates)
        {
            AddComponentTemplateIfMissing(ComponentTemplate, ComponentsByName);
        }

        TSet<const UEnum*> UsedEnums;
        TSet<const UScriptStruct*> UsedStructs;
        TMap<FString, FVariableYamlAsset> AssetsByName;
        TSet<FName> DeclaredVariableNames;
        for (const FBPVariableDescription& VariableDescription : Blueprint->NewVariables)
        {
            DeclaredVariableNames.Add(VariableDescription.VarName);
        }

        Output += FString::Printf(TEXT("# %s Variables%s"), *Blueprint->GetName(), LINE_TERMINATOR);
        Output += FString::Printf(TEXT("# Auto-generated for AI BP -> C++ conversion%s"), LINE_TERMINATOR);

        AppendIndentedLine(Output, 0, TEXT("class_info:"));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("name: %s"), *QuoteValue(Blueprint->GetName())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("parent_class: %s"), *QuoteValue(Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("generated_class: %s"), *QuoteValue(ObjectPathOrNone(GeneratedClass))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("asset_path: %s"), *QuoteValue(Blueprint->GetPathName())));

        AppendIndentedLine(Output, 0, TEXT("components:"));
        if (ComponentsByName.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            TArray<FString> ComponentNames;
            ComponentsByName.GetKeys(ComponentNames);
            ComponentNames.Sort();
            for (const FString& ComponentName : ComponentNames)
            {
                const FVariableYamlComponent* Component = ComponentsByName.Find(ComponentName);
                if (!Component)
                {
                    continue;
                }

                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("name: %s"), *QuoteValue(Component->Name)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Component->CppType)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("uproperty: %s"), *QuoteValue(Component->UPropertySpec)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_decl: %s"), *QuoteValue(Component->CppDecl)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_init: %s"), *QuoteValue(Component->CppInit)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("attachment: %s"), *QuoteValue(Component->Attachment)));
            }
        }

        AppendIndentedLine(Output, 0, TEXT("variables:"));
        if (Blueprint->NewVariables.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            for (const FBPVariableDescription& VariableDescription : Blueprint->NewVariables)
            {
                const FProperty* GeneratedProperty = FindGeneratedClassProperty(Blueprint, VariableDescription.VarName);
                const FString Category = TextOrEmpty(VariableDescription.Category).IsEmpty() ? TEXT("Default") : TextOrEmpty(VariableDescription.Category);
                const FString UPropertySpec = BuildUPropertySpecifier(VariableDescription.PropertyFlags, Category);
                const FString BpType = DescribePinType(VariableDescription.VarType);
                const FString CppType = GeneratedProperty ? GeneratedProperty->GetCPPType() : BpType;
                const FString DeclaredDefault = VariableDescription.DefaultValue;
                const FString GeneratedDefault = GeneratedProperty ? ExportPropertyValueFromContainer(GeneratedProperty, DefaultObject, DefaultObject) : FString();
                const FString DefaultValue = !GeneratedDefault.IsEmpty() ? GeneratedDefault : DeclaredDefault;

                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("bp_name: %s"), *QuoteValue(VariableDescription.VarName.ToString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_name: %s"), *QuoteValue(SanitizeCppIdentifier(VariableDescription.VarName.ToString()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("bp_type: %s"), *QuoteValue(BpType)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(CppType)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("uproperty: %s"), *QuoteValue(UPropertySpec)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("default: %s"), *QuoteValue(DefaultValue)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_decl: %s"), *QuoteValue(BuildPropertyCppDecl(GeneratedProperty, VariableDescription.VarName.ToString(), UPropertySpec))));

                CollectUserTypesFromPinType(VariableDescription.VarType, UsedEnums, UsedStructs);
                CollectUserTypesFromProperty(GeneratedProperty, UsedEnums, UsedStructs);
                AddAssetFromProperty(GeneratedProperty, DefaultObject, Category, AssetsByName);
            }
        }

        if (GeneratedClass && DefaultObject)
        {
            for (TFieldIterator<FProperty> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                const FProperty* Property = *It;
                CollectUserTypesFromProperty(Property, UsedEnums, UsedStructs);
                AddAssetFromProperty(Property, DefaultObject, TEXT("Assets"), AssetsByName);
            }
        }

        AppendIndentedLine(Output, 0, TEXT("assets:"));
        if (AssetsByName.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            TArray<FString> AssetNames;
            AssetsByName.GetKeys(AssetNames);
            AssetNames.Sort();
            for (const FString& AssetName : AssetNames)
            {
                const FVariableYamlAsset* Asset = AssetsByName.Find(AssetName);
                if (!Asset)
                {
                    continue;
                }

                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("bp_name: %s"), *QuoteValue(Asset->BpName)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_name: %s"), *QuoteValue(Asset->CppName)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("bp_type: %s"), *QuoteValue(Asset->BpType)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Asset->CppType)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("uproperty: %s"), *QuoteValue(Asset->UPropertySpec)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("asset_path: %s"), *QuoteValue(Asset->AssetPath)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_decl: %s"), *QuoteValue(Asset->CppDecl)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_load: %s"), *QuoteValue(Asset->CppLoad)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_hint: %s"), *QuoteValue(Asset->CppHint)));
            }
        }

        AppendIndentedLine(Output, 0, TEXT("initialization_defaults:"));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("cdo_object: %s"), *QuoteValue(ObjectPathOrNone(DefaultObject))));

        AppendIndentedLine(Output, 1, TEXT("generated_class_properties:"));
        if (!GeneratedClass || !DefaultObject)
        {
            AppendIndentedLine(Output, 2, TEXT("[]"));
        }
        else
        {
            bool bHasClassDefaults = false;
            for (TFieldIterator<FProperty> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                const FProperty* Property = *It;
                if (!Property || Property->HasAnyPropertyFlags(CPF_Transient))
                {
                    continue;
                }

                const FString DefaultValue = ExportPropertyValueFromContainer(Property, DefaultObject, DefaultObject);
                if (DefaultValue.IsEmpty())
                {
                    continue;
                }

                bHasClassDefaults = true;
                AppendIndentedLine(Output, 2, TEXT("-"));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("name: %s"), *QuoteValue(Property->GetName())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Property->GetCPPType())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("default_value: %s"), *QuoteValue(DefaultValue)));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("declared_member_variable: %s"), *BoolToString(DeclaredVariableNames.Contains(Property->GetFName()))));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("owner: %s"), *QuoteValue(ObjectPathOrNone(Property->GetOwnerStruct()))));
            }

            if (!bHasClassDefaults)
            {
                AppendIndentedLine(Output, 2, TEXT("[]"));
            }
        }

        AppendIndentedLine(Output, 1, TEXT("component_template_defaults:"));
        if (Blueprint->ComponentTemplates.Num() == 0)
        {
            AppendIndentedLine(Output, 2, TEXT("[]"));
        }
        else
        {
            for (const UActorComponent* ComponentTemplate : Blueprint->ComponentTemplates)
            {
                if (!ComponentTemplate)
                {
                    continue;
                }

                const UObject* ComponentCDO = ComponentTemplate->GetClass()->GetDefaultObject();

                AppendIndentedLine(Output, 2, TEXT("-"));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("name: %s"), *QuoteValue(ComponentTemplate->GetName())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("class: %s"), *QuoteValue(ComponentTemplate->GetClass()->GetPathName())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("path: %s"), *QuoteValue(ObjectPathOrNone(ComponentTemplate))));
                AppendIndentedLine(Output, 3, TEXT("property_overrides:"));

                bool bHasComponentOverrides = false;
                for (TFieldIterator<FProperty> It(ComponentTemplate->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
                {
                    const FProperty* Property = *It;
                    if (!Property || Property->HasAnyPropertyFlags(CPF_Transient) || ShouldSkipReflectedProperty(Property, EReflectedExportContext::ComponentTemplate))
                    {
                        continue;
                    }

                    const FString CurrentValue = ExportPropertyValueFromContainer(Property, ComponentTemplate, ComponentTemplate);
                    if (CurrentValue.IsEmpty())
                    {
                        continue;
                    }

                    const FString DefaultValue = ComponentCDO ? ExportPropertyValueFromContainer(Property, ComponentCDO, ComponentCDO) : FString();
                    if (CurrentValue == DefaultValue)
                    {
                        continue;
                    }

                    bHasComponentOverrides = true;
                    AppendIndentedLine(Output, 4, TEXT("-"));
                    AppendIndentedLine(Output, 5, FString::Printf(TEXT("name: %s"), *QuoteValue(Property->GetName())));
                    AppendIndentedLine(Output, 5, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Property->GetCPPType())));
                    AppendIndentedLine(Output, 5, FString::Printf(TEXT("default_value: %s"), *QuoteValue(CurrentValue)));
                    AppendIndentedLine(Output, 5, FString::Printf(TEXT("class_default: %s"), *QuoteValue(DefaultValue)));
                }

                if (!bHasComponentOverrides)
                {
                    AppendIndentedLine(Output, 4, TEXT("[]"));
                }
            }
        }

        AppendIndentedLine(Output, 1, TEXT("inheritable_component_overrides_defaults:"));
        if (!Blueprint->InheritableComponentHandler)
        {
            AppendIndentedLine(Output, 2, TEXT("[]"));
        }
        else
        {
            bool bHasInheritableOverrides = false;
            for (auto It = Blueprint->InheritableComponentHandler->CreateRecordIterator(); It; ++It)
            {
                const FComponentOverrideRecord& Record = *It;
                const UActorComponent* OverrideTemplate = Record.ComponentTemplate.Get();
                if (!OverrideTemplate)
                {
                    continue;
                }

                bHasInheritableOverrides = true;
                const UObject* NativeClassDefault = OverrideTemplate->GetClass()->GetDefaultObject(false);

                AppendIndentedLine(Output, 2, TEXT("-"));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("component_class: %s"), *QuoteValue(ObjectPathOrNone(Record.ComponentClass.Get()))));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("component_template: %s"), *QuoteValue(ObjectPathOrNone(OverrideTemplate))));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("component_key_owner: %s"), *QuoteValue(ObjectPathOrNone(Record.ComponentKey.GetComponentOwner()))));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("component_key_scs_variable: %s"), *QuoteValue(NameOrNone(Record.ComponentKey.GetSCSVariableName()))));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("component_key_guid: %s"), *QuoteValue(GuidToString(Record.ComponentKey.GetAssociatedGuid()))));
                AppendIndentedLine(Output, 3, TEXT("property_overrides:"));

                bool bHasRecordOverrides = false;
                for (TFieldIterator<FProperty> PropIt(OverrideTemplate->GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
                {
                    const FProperty* Property = *PropIt;
                    if (!Property || Property->HasAnyPropertyFlags(CPF_Transient) || ShouldSkipReflectedProperty(Property, EReflectedExportContext::ComponentTemplate))
                    {
                        continue;
                    }

                    const FString OverrideValue = ExportPropertyValueFromContainer(Property, OverrideTemplate, OverrideTemplate);
                    if (OverrideValue.IsEmpty())
                    {
                        continue;
                    }

                    const FString NativeDefaultValue = NativeClassDefault ? ExportPropertyValueFromContainer(Property, NativeClassDefault, NativeClassDefault) : FString();
                    if (OverrideValue == NativeDefaultValue)
                    {
                        continue;
                    }

                    bHasRecordOverrides = true;
                    AppendIndentedLine(Output, 4, TEXT("-"));
                    AppendIndentedLine(Output, 5, FString::Printf(TEXT("name: %s"), *QuoteValue(Property->GetName())));
                    AppendIndentedLine(Output, 5, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Property->GetCPPType())));
                    AppendIndentedLine(Output, 5, FString::Printf(TEXT("override_value: %s"), *QuoteValue(OverrideValue)));
                    AppendIndentedLine(Output, 5, FString::Printf(TEXT("native_class_default: %s"), *QuoteValue(NativeDefaultValue)));
                }

                if (!bHasRecordOverrides)
                {
                    AppendIndentedLine(Output, 4, TEXT("[]"));
                }
            }

            if (!bHasInheritableOverrides)
            {
                AppendIndentedLine(Output, 2, TEXT("[]"));
            }
        }

        AppendIndentedLine(Output, 1, TEXT("timeline_defaults:"));
        if (Blueprint->Timelines.Num() == 0)
        {
            AppendIndentedLine(Output, 2, TEXT("[]"));
        }
        else
        {
            for (const UTimelineTemplate* Timeline : Blueprint->Timelines)
            {
                AppendIndentedLine(Output, 2, TEXT("-"));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("name: %s"), *QuoteValue(Timeline ? Timeline->GetName() : FString())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("path: %s"), *QuoteValue(ObjectPathOrNone(Timeline))));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("variable_name: %s"), *QuoteValue(Timeline ? NameOrNone(Timeline->GetVariableName()) : FString())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("timeline_length: %s"), *QuoteValue(Timeline ? LexToString(Timeline->TimelineLength) : FString())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("length_mode: %s"), *QuoteValue(Timeline ? DescribeEnumValue(StaticEnum<ETimelineLengthMode>(), static_cast<int64>(Timeline->LengthMode.GetValue())) : FString())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("auto_play: %s"), *BoolToString(Timeline ? Timeline->bAutoPlay != 0 : false)));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("loop: %s"), *BoolToString(Timeline ? Timeline->bLoop != 0 : false)));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("replicated: %s"), *BoolToString(Timeline ? Timeline->bReplicated != 0 : false)));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("ignore_time_dilation: %s"), *BoolToString(Timeline ? Timeline->bIgnoreTimeDilation != 0 : false)));

                AppendIndentedLine(Output, 3, TEXT("event_tracks:"));
                if (!Timeline || Timeline->EventTracks.Num() == 0)
                {
                    AppendIndentedLine(Output, 4, TEXT("[]"));
                }
                else
                {
                    for (const FTTEventTrack& Track : Timeline->EventTracks)
                    {
                        AppendIndentedLine(Output, 4, TEXT("-"));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("track_name: %s"), *QuoteValue(NameOrNone(Track.GetTrackName()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("function_name: %s"), *QuoteValue(NameOrNone(Track.GetFunctionName()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("curve: %s"), *QuoteValue(ObjectPathOrNone(Track.CurveKeys.Get()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("external_curve: %s"), *BoolToString(Track.bIsExternalCurve)));
                    }
                }

                AppendIndentedLine(Output, 3, TEXT("float_tracks:"));
                if (!Timeline || Timeline->FloatTracks.Num() == 0)
                {
                    AppendIndentedLine(Output, 4, TEXT("[]"));
                }
                else
                {
                    for (const FTTFloatTrack& Track : Timeline->FloatTracks)
                    {
                        AppendIndentedLine(Output, 4, TEXT("-"));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("track_name: %s"), *QuoteValue(NameOrNone(Track.GetTrackName()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("property_name: %s"), *QuoteValue(NameOrNone(Track.GetPropertyName()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("curve: %s"), *QuoteValue(ObjectPathOrNone(Track.CurveFloat.Get()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("external_curve: %s"), *BoolToString(Track.bIsExternalCurve)));
                    }
                }

                AppendIndentedLine(Output, 3, TEXT("vector_tracks:"));
                if (!Timeline || Timeline->VectorTracks.Num() == 0)
                {
                    AppendIndentedLine(Output, 4, TEXT("[]"));
                }
                else
                {
                    for (const FTTVectorTrack& Track : Timeline->VectorTracks)
                    {
                        AppendIndentedLine(Output, 4, TEXT("-"));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("track_name: %s"), *QuoteValue(NameOrNone(Track.GetTrackName()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("property_name: %s"), *QuoteValue(NameOrNone(Track.GetPropertyName()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("curve: %s"), *QuoteValue(ObjectPathOrNone(Track.CurveVector.Get()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("external_curve: %s"), *BoolToString(Track.bIsExternalCurve)));
                    }
                }

                AppendIndentedLine(Output, 3, TEXT("linear_color_tracks:"));
                if (!Timeline || Timeline->LinearColorTracks.Num() == 0)
                {
                    AppendIndentedLine(Output, 4, TEXT("[]"));
                }
                else
                {
                    for (const FTTLinearColorTrack& Track : Timeline->LinearColorTracks)
                    {
                        AppendIndentedLine(Output, 4, TEXT("-"));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("track_name: %s"), *QuoteValue(NameOrNone(Track.GetTrackName()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("property_name: %s"), *QuoteValue(NameOrNone(Track.GetPropertyName()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("curve: %s"), *QuoteValue(ObjectPathOrNone(Track.CurveLinearColor.Get()))));
                        AppendIndentedLine(Output, 5, FString::Printf(TEXT("external_curve: %s"), *BoolToString(Track.bIsExternalCurve)));
                    }
                }
            }
        }

        AppendIndentedLine(Output, 1, TEXT("function_local_variable_defaults:"));
        TArray<UEdGraph*> LocalVariableGraphs;
        Blueprint->GetAllGraphs(LocalVariableGraphs);
        bool bHasLocalVariableDefaults = false;
        for (UEdGraph* Graph : LocalVariableGraphs)
        {
            if (!Graph)
            {
                continue;
            }

            UK2Node_FunctionEntry* FunctionEntryNode = nullptr;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                FunctionEntryNode = Cast<UK2Node_FunctionEntry>(Node);
                if (FunctionEntryNode)
                {
                    break;
                }
            }

            if (!FunctionEntryNode || FunctionEntryNode->LocalVariables.Num() == 0)
            {
                continue;
            }

            bHasLocalVariableDefaults = true;
            FunctionEntryNode->RefreshFunctionVariableCache();
            const TSharedPtr<FStructOnScope> FunctionVariableCache = FunctionEntryNode->GetFunctionVariableCache(false);
            const UStruct* FunctionVariableStruct = FunctionVariableCache.IsValid() ? FunctionVariableCache->GetStruct() : nullptr;
            const void* FunctionVariableMemory = (FunctionVariableCache.IsValid() && FunctionVariableCache->IsValid()) ? FunctionVariableCache->GetStructMemory() : nullptr;

            const FName FunctionName = FunctionEntryNode->CustomGeneratedFunctionName.IsNone() ? Graph->GetFName() : FunctionEntryNode->CustomGeneratedFunctionName;
            AppendIndentedLine(Output, 2, TEXT("-"));
            AppendIndentedLine(Output, 3, FString::Printf(TEXT("function: %s"), *QuoteValue(FunctionName.ToString())));
            AppendIndentedLine(Output, 3, FString::Printf(TEXT("graph: %s"), *QuoteValue(Graph->GetName())));
            AppendIndentedLine(Output, 3, TEXT("local_variables:"));

            for (const FBPVariableDescription& LocalVariable : FunctionEntryNode->LocalVariables)
            {
                const FProperty* LocalProperty = FindStructProperty(FunctionVariableStruct, LocalVariable.VarName);
                const FString GeneratedDefault = LocalProperty ? ExportPropertyValueFromContainer(LocalProperty, FunctionVariableMemory, FunctionEntryNode) : FString();

                AppendIndentedLine(Output, 4, TEXT("-"));
                AppendIndentedLine(Output, 5, FString::Printf(TEXT("name: %s"), *QuoteValue(LocalVariable.VarName.ToString())));
                AppendIndentedLine(Output, 5, FString::Printf(TEXT("pin_type: %s"), *QuoteValue(DescribePinType(LocalVariable.VarType))));
                AppendIndentedLine(Output, 5, FString::Printf(TEXT("declared_default: %s"), *QuoteValue(LocalVariable.DefaultValue)));
                AppendIndentedLine(Output, 5, FString::Printf(TEXT("generated_default: %s"), *QuoteValue(GeneratedDefault)));
                AppendIndentedLine(Output, 5, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(LocalProperty ? LocalProperty->GetCPPType() : FString())));
            }
        }

        if (!bHasLocalVariableDefaults)
        {
            AppendIndentedLine(Output, 2, TEXT("[]"));
        }

        AppendIndentedLine(Output, 0, TEXT("enums:"));
        if (UsedEnums.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            TArray<const UEnum*> SortedEnums = UsedEnums.Array();
            SortedEnums.RemoveAll([](const UEnum* Enum) { return Enum == nullptr; });
            SortedEnums.Sort([](const UEnum& A, const UEnum& B)
            {
                return A.GetName() < B.GetName();
            });

            for (const UEnum* Enum : SortedEnums)
            {
                if (!Enum)
                {
                    continue;
                }

                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("name: %s"), *QuoteValue(Enum->GetName())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_name: %s"), *QuoteValue(SanitizeCppIdentifier(Enum->GetName()))));
                AppendIndentedLine(Output, 2, TEXT("values:"));

                for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
                {
                    const FString NameString = Enum->GetNameStringByIndex(Index);
                    if (NameString.EndsWith(TEXT("_MAX")))
                    {
                        continue;
                    }

                    AppendIndentedLine(Output, 3, TEXT("-"));
                    AppendIndentedLine(Output, 4, FString::Printf(TEXT("bp: %s"), *QuoteValue(Enum->GetDisplayNameTextByIndex(Index).ToString())));
                    AppendIndentedLine(Output, 4, FString::Printf(TEXT("cpp: %s"), *QuoteValue(SanitizeCppIdentifier(NameString))));
                    AppendIndentedLine(Output, 4, FString::Printf(TEXT("ordinal: %lld"), static_cast<long long>(Enum->GetValueByIndex(Index))));
                }
            }
        }

        AppendIndentedLine(Output, 0, TEXT("structs:"));
        if (UsedStructs.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            TArray<const UScriptStruct*> SortedStructs = UsedStructs.Array();
            SortedStructs.RemoveAll([](const UScriptStruct* Struct) { return Struct == nullptr; });
            SortedStructs.Sort([](const UScriptStruct& A, const UScriptStruct& B)
            {
                return A.GetName() < B.GetName();
            });

            for (const UScriptStruct* Struct : SortedStructs)
            {
                if (!Struct)
                {
                    continue;
                }

                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("name: %s"), *QuoteValue(Struct->GetName())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_name: %s"), *QuoteValue(SanitizeCppIdentifier(Struct->GetName()))));
                AppendIndentedLine(Output, 2, TEXT("fields:"));

                bool bHasFields = false;
                for (TFieldIterator<FProperty> It(Struct); It; ++It)
                {
                    const FProperty* Property = *It;
                    if (!Property)
                    {
                        continue;
                    }

                    bHasFields = true;
                    AppendIndentedLine(Output, 3, TEXT("-"));
                    AppendIndentedLine(Output, 4, FString::Printf(TEXT("bp_name: %s"), *QuoteValue(Property->GetName())));
                    AppendIndentedLine(Output, 4, FString::Printf(TEXT("cpp_name: %s"), *QuoteValue(SanitizeCppIdentifier(Property->GetName()))));
                    AppendIndentedLine(Output, 4, FString::Printf(TEXT("type: %s"), *QuoteValue(Property->GetCPPType())));
                }

                if (!bHasFields)
                {
                    AppendIndentedLine(Output, 3, TEXT("[]"));
                }
            }
        }

        AppendIndentedLine(Output, 0, TEXT("blueprint_native_events:"));
        if (!GeneratedClass)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            TSet<FString> NativeEventNames;
            for (TFieldIterator<UFunction> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                const UFunction* Function = *It;
                if (!Function)
                {
                    continue;
                }

                if (!Function->HasAnyFunctionFlags(FUNC_BlueprintEvent) || !Function->HasAnyFunctionFlags(FUNC_Native))
                {
                    continue;
                }

                FString Name = Function->GetName();
                Name.RemoveFromEnd(TEXT("_Implementation"));
                if (Name.StartsWith(TEXT("ExecuteUbergraph_")))
                {
                    continue;
                }

                NativeEventNames.Add(Name);
            }

            if (NativeEventNames.Num() == 0)
            {
                AppendIndentedLine(Output, 1, TEXT("[]"));
            }
            else
            {
                TArray<FString> SortedNativeEvents = NativeEventNames.Array();
                SortedNativeEvents.Sort();
                for (const FString& NativeEventName : SortedNativeEvents)
                {
                    AppendIndentedLine(Output, 1, TEXT("-"));
                    AppendIndentedLine(Output, 2, FString::Printf(TEXT("function: %s"), *QuoteValue(NativeEventName)));
                    AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_hint: %s"), *QuoteValue(FString::Printf(TEXT("Blueprint override logic maps to %s_Implementation"), *NativeEventName))));
                }
            }
        }

        AppendIndentedLine(Output, 0, TEXT("overrides:"));
        TMap<FString, FBlueprintOverrideInfo> OverrideMap;
        TArray<UEdGraph*> AllGraphs;
        Blueprint->GetAllGraphs(AllGraphs);

        for (const UEdGraph* Graph : AllGraphs)
        {
            if (!Graph)
            {
                continue;
            }

            for (const UEdGraphNode* Node : Graph->Nodes)
            {
                const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
                if (!EventNode || !EventNode->bOverrideFunction)
                {
                    continue;
                }

                FString OverrideName = NameOrNone(EventNode->EventReference.GetMemberName());
                if (OverrideName.IsEmpty() || OverrideName == TEXT("None"))
                {
                    OverrideName = NameOrNone(EventNode->CustomFunctionName);
                }
                if (OverrideName.IsEmpty() || OverrideName == TEXT("None"))
                {
                    continue;
                }

                FBlueprintOverrideInfo& OverrideInfo = OverrideMap.FindOrAdd(OverrideName);
                OverrideInfo.FunctionName = OverrideName;
                OverrideInfo.CppEquivalent = GetOverrideCppSignature(OverrideName);
            }
        }

        if (OverrideMap.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            TArray<FString> OverrideNames;
            OverrideMap.GetKeys(OverrideNames);
            OverrideNames.Sort();
            for (const FString& OverrideName : OverrideNames)
            {
                const FBlueprintOverrideInfo* OverrideInfo = OverrideMap.Find(OverrideName);
                if (!OverrideInfo)
                {
                    continue;
                }

                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("function: %s"), *QuoteValue(OverrideInfo->FunctionName)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_equivalent: %s"), *QuoteValue(OverrideInfo->CppEquivalent)));
            }
        }

        return Output;
    }

    static FString BuildBlueprintReadme(
        const UBlueprint* Blueprint,
        const FString& VariablesRelativePath,
        const FString& StructuredBlueprintRelativePath,
        const TMap<FName, FString>& RawGraphFiles,
        const TMap<FName, FString>& StructuredGraphFiles,
        const TMap<FName, FString>& DotGraphFiles,
        const TMap<FName, FString>& MmidGraphFiles)
    {
        FString Output;
        if (!Blueprint)
        {
            return Output;
        }

        Output += FString::Printf(TEXT("# %s Export Guide%s%s"), *Blueprint->GetName(), LINE_TERMINATOR, LINE_TERMINATOR);
        Output += TEXT("This README is generated for AI-assisted Blueprint to C++ conversion.");
        Output += LINE_TERMINATOR;
        Output += LINE_TERMINATOR;

        Output += TEXT("## Recommended Reading Order");
        Output += LINE_TERMINATOR;
        Output += TEXT("1. `variables/*.yaml` to understand class-level members, initialization defaults, assets, enums, and structs.");
        Output += LINE_TERMINATOR;
        Output += TEXT("2. `txt/structured/{Blueprint}.txt` for blueprint summary and function signatures.");
        Output += LINE_TERMINATOR;
        Output += TEXT("3. Graph-level `mmid` files for flow and node-level `cpp_hint`.");
        Output += LINE_TERMINATOR;
        Output += TEXT("4. Use `mappings/BpToCppMap.yaml` as the canonical BP->C++ mapping table.");
        Output += LINE_TERMINATOR;
        Output += TEXT("5. Check `mappings/ProjectInputMappings.yaml` when Blueprint logic depends on project input setup or Enhanced Input contexts.");
        Output += LINE_TERMINATOR;
        Output += LINE_TERMINATOR;

        Output += TEXT("## Key Files");
        Output += LINE_TERMINATOR;
        Output += FString::Printf(TEXT("- Variables: `%s`%s"), *VariablesRelativePath, LINE_TERMINATOR);
        Output += FString::Printf(TEXT("- Structured Blueprint: `%s`%s"), *StructuredBlueprintRelativePath, LINE_TERMINATOR);
        Output += FString::Printf(TEXT("- Global Mapping: `mappings/BpToCppMap.yaml`%s"), LINE_TERMINATOR);
        Output += FString::Printf(TEXT("- Project Input: `mappings/ProjectInputMappings.yaml`%s"), LINE_TERMINATOR);
        Output += LINE_TERMINATOR;

        Output += TEXT("## Graph Files");
        Output += LINE_TERMINATOR;
        TArray<FName> GraphNames;
        StructuredGraphFiles.GetKeys(GraphNames);
        GraphNames.Sort([](const FName& A, const FName& B)
        {
            return A.ToString() < B.ToString();
        });

        if (GraphNames.Num() == 0)
        {
            Output += TEXT("- No graphs exported.");
            Output += LINE_TERMINATOR;
        }
        else
        {
            for (const FName GraphName : GraphNames)
            {
                const FString RawFile = RawGraphFiles.FindRef(GraphName);
                const FString StructuredFile = StructuredGraphFiles.FindRef(GraphName);
                const FString DotFile = DotGraphFiles.FindRef(GraphName);
                const FString MmidFile = MmidGraphFiles.FindRef(GraphName);

                Output += FString::Printf(TEXT("- `%s`: raw=`%s`, structured=`%s`, dot=`%s`, mmid=`%s`%s"),
                    *GraphName.ToString(),
                    *RawFile,
                    *StructuredFile,
                    *DotFile,
                    *MmidFile,
                    LINE_TERMINATOR);
            }
        }

        Output += LINE_TERMINATOR;
        Output += TEXT("## AI Conversion Notes");
        Output += LINE_TERMINATOR;
        Output += TEXT("- Prefer `cpp_hint` on nodes when present.");
        Output += LINE_TERMINATOR;
        Output += TEXT("- Keep Blueprint control-flow ordering identical in C++.");
        Output += LINE_TERMINATOR;
        Output += TEXT("- Read `initialization_defaults` first to preserve constructor/CDO default behavior.");
        Output += LINE_TERMINATOR;
        Output += TEXT("- `initialization_defaults` now includes class, component, inheritable override, timeline, and function-local defaults.");
        Output += LINE_TERMINATOR;
        Output += TEXT("- For montage/input/collision/audio nodes, apply patterns from `BpToCppMap.yaml`.");
        Output += LINE_TERMINATOR;
        Output += TEXT("- For input setup and bindings, cross-check `ProjectInputMappings.yaml` before inferring missing contexts or keys.");
        Output += LINE_TERMINATOR;

        return Output;
    }

    static void AppendReflectedPropertyDescription(
        FString& Output,
        const int32 IndentLevel,
        const FProperty* Property,
        const void* ContainerPtr,
        const UObject* ValueOwner)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("-"));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(Property ? Property->GetName() : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Property ? Property->GetCPPType() : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("owner: %s"), *QuoteValue(Property ? ObjectPathOrNone(Property->GetOwnerStruct()) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("property_flags: %s"), *QuoteValue(Property ? DescribePropertyFlags(Property->PropertyFlags) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("array_dim: %d"), Property ? Property->ArrayDim : 0));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("value: %s"), *QuoteValue(Property ? ExportPropertyValueFromContainer(Property, ContainerPtr, ValueOwner) : FString())));
    }

    static void AppendReflectedProperties(
        FString& Output,
        const int32 IndentLevel,
        const UObject* SourceObject,
        const EReflectedExportContext Context,
        const bool bIncludeSuper = true)
    {
        if (!SourceObject)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
            return;
        }

        // ComponentTemplate: only look at properties declared directly on the component's own class,
        // not the entire UActorComponent/USceneComponent inheritance chain (300+ properties).
        const bool bEffectiveIncludeSuper = bIncludeSuper
            && (Context != EReflectedExportContext::ComponentTemplate);

        bool bHasProperties = false;
        for (TFieldIterator<FProperty> It(SourceObject->GetClass(), bEffectiveIncludeSuper ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            const FProperty* Property = *It;
            if (!Property || ShouldSkipReflectedProperty(Property, Context))
            {
                continue;
            }

            bHasProperties = true;
            AppendReflectedPropertyDescription(Output, IndentLevel, Property, SourceObject, SourceObject);
        }

        if (!bHasProperties)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
        }
    }

    static void AppendObjectReferenceList(FString& Output, const int32 IndentLevel, const TArray<UObject*>& Objects)
    {
        if (Objects.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
            return;
        }

        for (const UObject* Object : Objects)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("path: %s"), *QuoteValue(ObjectPathOrNone(Object))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("class: %s"), *QuoteValue(Object ? Object->GetClass()->GetPathName() : FString())));
        }
    }

    static void AppendGraphReferenceList(FString& Output, const int32 IndentLevel, const TArray<UEdGraph*>& Graphs)
    {
        if (Graphs.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
            return;
        }

        for (const UEdGraph* Graph : Graphs)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(Graph ? Graph->GetName() : FString())));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("path: %s"), *QuoteValue(ObjectPathOrNone(Graph))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("class: %s"), *QuoteValue(Graph ? Graph->GetClass()->GetPathName() : FString())));
        }
    }

    static FString BuildPinDotValue(const UEdGraphPin* Pin)
    {
        if (!Pin)
        {
            return FString();
        }

        if (Pin->DefaultObject)
        {
            return ObjectPathOrNone(Pin->DefaultObject);
        }

        const FString DefaultText = TextOrEmpty(Pin->DefaultTextValue);
        if (!DefaultText.IsEmpty())
        {
            return DefaultText;
        }

        return Pin->DefaultValue;
    }

    static bool ShouldIncludePinInDotLabel(const UEdGraphPin* Pin)
    {
        if (!Pin || Pin->Direction != EGPD_Input || IsExecPin(Pin))
        {
            return false;
        }

        const FString PinValue = BuildPinDotValue(Pin);
        if (PinValue.IsEmpty())
        {
            return false;
        }

        if (Pin->DefaultObject || !TextOrEmpty(Pin->DefaultTextValue).IsEmpty())
        {
            return true;
        }

        if (!Pin->AutogeneratedDefaultValue.IsEmpty())
        {
            return PinValue != Pin->AutogeneratedDefaultValue;
        }

        return true;
    }

    static void GatherDotNodeValueLines(const UEdGraphNode* Node, TArray<FString>& OutLines)
    {
        if (!Node)
        {
            return;
        }

        const UObject* DefaultNodeObject = Node->GetClass()->GetDefaultObject();
        const FName NodeSemanticType = FName(*GetNodeSemanticType(Node));

        OutLines.Add(GetNodeTitleString(Node));
        OutLines.Add(FString::Printf(TEXT("type=%s"), *NodeSemanticType.ToString()));

        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!ShouldIncludePinInDotLabel(Pin))
            {
                continue;
            }

            OutLines.Add(FString::Printf(TEXT("pin.%s=%s"), *NameOrNone(Pin->PinName), *BuildPinDotValue(Pin)));
        }

        for (TFieldIterator<FProperty> It(Node->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Property = *It;
            if (!Property || ShouldSkipReflectedProperty(Property, EReflectedExportContext::GraphNode))
            {
                continue;
            }

            const FString CurrentValue = ExportPropertyValueFromContainer(Property, Node, Node);
            if (CurrentValue.IsEmpty())
            {
                continue;
            }

            const FString DefaultValue = DefaultNodeObject ? ExportPropertyValueFromContainer(Property, DefaultNodeObject, DefaultNodeObject) : FString();
            if (CurrentValue == DefaultValue)
            {
                continue;
            }

            OutLines.Add(FString::Printf(TEXT("prop.%s=%s"), *Property->GetName(), *CurrentValue));
        }
    }

    static FString BuildDotNodeLabel(const UEdGraphNode* Node)
    {
        TArray<FString> LabelLines;
        GatherDotNodeValueLines(Node, LabelLines);
        return FString::Join(LabelLines, TEXT("\n"));
    }

    static bool ShouldIncludePinInMmidLabel(const UEdGraphPin* Pin)
    {
        if (!Pin)
        {
            return false;
        }

        if (IsExecPin(Pin))
        {
            return true;
        }

        const FString PinValue = BuildPinDotValue(Pin);
        if (!PinValue.IsEmpty())
        {
            return true;
        }

        return Pin->LinkedTo.Num() > 0;
    }

    static void GatherMmidNodeValueLines(const UEdGraphNode* Node, TArray<FString>& OutLines)
    {
        if (!Node)
        {
            return;
        }

        const UObject* DefaultNodeObject = Node->GetClass()->GetDefaultObject();

        OutLines.Add(GetNodeTitleString(Node));
        OutLines.Add(FString::Printf(TEXT("class=%s"), *Node->GetClass()->GetName()));
        OutLines.Add(FString::Printf(TEXT("semantic=%s"), *GetNodeSemanticType(Node)));
        OutLines.Add(FString::Printf(TEXT("id=%s"), *GetNodeStableId(Node)));

        const FString CppHint = GetNodeCppHint(Node);
        if (!CppHint.IsEmpty())
        {
            OutLines.Add(FString::Printf(TEXT("cpp_hint: %s"), *CppHint));
        }

        if (!Node->NodeComment.IsEmpty())
        {
            OutLines.Add(FString::Printf(TEXT("comment=%s"), *Node->NodeComment));
        }

        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!ShouldIncludePinInMmidLabel(Pin))
            {
                continue;
            }

            const FString PinValue = BuildPinDotValue(Pin);
            const FString Direction = DescribePinDirection(Pin->Direction);
            const FString PinType = DescribePinType(Pin->PinType);
            const FString LinkedCount = FString::Printf(TEXT("%d"), Pin->LinkedTo.Num());

            if (!PinValue.IsEmpty())
            {
                OutLines.Add(FString::Printf(TEXT("pin.%s.%s: %s = %s [links=%s]"),
                    *Direction,
                    *NameOrNone(Pin->PinName),
                    *PinType,
                    *PinValue,
                    *LinkedCount));
            }
            else
            {
                OutLines.Add(FString::Printf(TEXT("pin.%s.%s: %s [links=%s]"),
                    *Direction,
                    *NameOrNone(Pin->PinName),
                    *PinType,
                    *LinkedCount));
            }
        }

        for (TFieldIterator<FProperty> It(Node->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Property = *It;
            if (!Property || ShouldSkipReflectedProperty(Property, EReflectedExportContext::GraphNode))
            {
                continue;
            }

            const FString CurrentValue = ExportPropertyValueFromContainer(Property, Node, Node);
            if (CurrentValue.IsEmpty())
            {
                continue;
            }

            const FString DefaultValue = DefaultNodeObject ? ExportPropertyValueFromContainer(Property, DefaultNodeObject, DefaultNodeObject) : FString();
            if (CurrentValue == DefaultValue)
            {
                continue;
            }

            OutLines.Add(FString::Printf(TEXT("prop.%s=%s"), *Property->GetName(), *CurrentValue));
        }
    }

    static FString BuildMmidNodeLabel(const UEdGraphNode* Node)
    {
        TArray<FString> LabelLines;
        GatherMmidNodeValueLines(Node, LabelLines);
        return FString::Join(LabelLines, TEXT("\n"));
    }

    static void AppendSimpleMemberReference(FString& Output, const int32 IndentLevel, const FString& Label, const FSimpleMemberReference& Reference)
    {
        AppendIndentedLine(Output, IndentLevel, FString::Printf(TEXT("%s:"), *Label));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(NameOrNone(Reference.MemberName))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("guid: %s"), *QuoteValue(GuidToString(Reference.MemberGuid))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("parent: %s"), *QuoteValue(ObjectPathOrNone(Reference.MemberParent))));
    }

    static void AppendMemberReference(FString& Output, const int32 IndentLevel, const FString& Label, const FMemberReference& Reference)
    {
        AppendIndentedLine(Output, IndentLevel, FString::Printf(TEXT("%s:"), *Label));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(NameOrNone(Reference.GetMemberName()))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("guid: %s"), *QuoteValue(GuidToString(Reference.GetMemberGuid()))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("parent_class: %s"), *QuoteValue(ObjectPathOrNone(Reference.GetMemberParentClass()))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("parent_package: %s"), *QuoteValue(ObjectPathOrNone(Reference.GetMemberParentPackage()))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("scope_name: %s"), *QuoteValue(Reference.GetMemberScopeName())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("self_context: %s"), *BoolToString(Reference.IsSelfContext())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("local_scope: %s"), *BoolToString(Reference.IsLocalScope())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("deprecated: %s"), *BoolToString(Reference.IsDeprecated())));
    }

    static void AppendVariableMetaData(FString& Output, const int32 IndentLevel, const TArray<FBPVariableMetaDataEntry>& MetaDataArray)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("metadata:"));
        if (MetaDataArray.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
            return;
        }

        for (const FBPVariableMetaDataEntry& Entry : MetaDataArray)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("key: %s"), *QuoteValue(NameOrNone(Entry.DataKey))));
            AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("value: %s"), *QuoteValue(Entry.DataValue)));
        }
    }

    static void AppendVariableDescription(
        FString& Output,
        const int32 IndentLevel,
        const FBPVariableDescription& VariableDescription,
        const FProperty* GeneratedProperty,
        const void* DefaultValueContainer,
        const UObject* DefaultValueOwner)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("-"));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(VariableDescription.VarName.ToString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("guid: %s"), *QuoteValue(GuidToString(VariableDescription.VarGuid))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("friendly_name: %s"), *QuoteValue(VariableDescription.FriendlyName)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("category: %s"), *QuoteValue(TextOrEmpty(VariableDescription.Category))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("pin_type: %s"), *QuoteValue(DescribePinType(VariableDescription.VarType))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("declared_default: %s"), *QuoteValue(VariableDescription.DefaultValue)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("generated_default: %s"), *QuoteValue(ExportPropertyValueFromContainer(GeneratedProperty, DefaultValueContainer, DefaultValueOwner))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("property_flags: %s"), *QuoteValue(DescribePropertyFlags(VariableDescription.PropertyFlags))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("rep_notify_func: %s"), *QuoteValue(NameOrNone(VariableDescription.RepNotifyFunc))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("replication_condition: %s"), *QuoteValue(DescribeReplicationCondition(VariableDescription.ReplicationCondition))));

        if (GeneratedProperty)
        {
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("generated_property_cpp_type: %s"), *QuoteValue(GeneratedProperty->GetCPPType())));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("generated_property_name: %s"), *QuoteValue(GeneratedProperty->GetName())));
        }
        else
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("generated_property_cpp_type: \"\""));
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("generated_property_name: \"\""));
        }

        AppendVariableMetaData(Output, IndentLevel + 1, VariableDescription.MetaDataArray);
    }

    static void AppendGeneratedPropertyDescription(
        FString& Output,
        const int32 IndentLevel,
        const FProperty* Property,
        const void* DefaultValueContainer,
        const UObject* DefaultValueOwner,
        const bool bDeclaredInNewVariables)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("-"));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(Property ? Property->GetName() : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Property ? Property->GetCPPType() : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("owner: %s"), *QuoteValue(Property ? ObjectPathOrNone(Property->GetOwnerStruct()) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("declared_member_variable: %s"), *BoolToString(bDeclaredInNewVariables)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("property_flags: %s"), *QuoteValue(Property ? DescribePropertyFlags(Property->PropertyFlags) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_value: %s"), *QuoteValue(Property ? ExportPropertyValueFromContainer(Property, DefaultValueContainer, DefaultValueOwner) : FString())));

        const UClass* OwnerClass = Property ? Cast<UClass>(Property->GetOwnerStruct()) : nullptr;
        const UBlueprint* OwnerBlueprint = OwnerClass ? Cast<UBlueprint>(OwnerClass->ClassGeneratedBy) : nullptr;
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("owner_blueprint: %s"), *QuoteValue(ObjectPathOrNone(OwnerBlueprint))));
    }

    static void AppendPinDescription(FString& Output, const int32 IndentLevel, const UEdGraphNode* Node, const UEdGraphPin* Pin)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("-"));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(NameOrNone(Pin->PinName))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("pin_id: %s"), *QuoteValue(GuidToString(Pin->PinId))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("direction: %s"), *QuoteValue(DescribePinDirection(Pin->Direction))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("type: %s"), *QuoteValue(DescribePinType(Pin->PinType))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("tool_tip: %s"), *QuoteValue(Pin->PinToolTip)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_value: %s"), *QuoteValue(Pin->DefaultValue)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("autogenerated_default_value: %s"), *QuoteValue(Pin->AutogeneratedDefaultValue)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_object: %s"), *QuoteValue(ObjectPathOrNone(Pin->DefaultObject))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_text_value: %s"), *QuoteValue(TextOrEmpty(Pin->DefaultTextValue))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("source_index: %d"), Pin->SourceIndex));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("is_exec: %s"), *BoolToString(IsExecPin(Pin))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("has_parent_pin: %s"), *BoolToString(Pin->ParentPin != nullptr)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("reference_passthrough: %s"), *BoolToString(Pin->ReferencePassThroughConnection != nullptr)));

#if WITH_EDITORONLY_DATA
        const FString FriendlyPinName = Node->ShouldOverridePinNames() ? Node->GetPinNameOverride(*Pin).ToString() : TextOrEmpty(Pin->PinFriendlyName);
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("friendly_name: %s"), *QuoteValue(FriendlyPinName)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("persistent_guid: %s"), *QuoteValue(GuidToString(Pin->PersistentGuid))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("hidden: %s"), *BoolToString(Pin->bHidden)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("not_connectable: %s"), *BoolToString(Pin->bNotConnectable)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("advanced_view: %s"), *BoolToString(Pin->bAdvancedView)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_value_read_only: %s"), *BoolToString(Pin->bDefaultValueIsReadOnly)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_value_ignored: %s"), *BoolToString(Pin->bDefaultValueIsIgnored)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("orphaned: %s"), *BoolToString(Pin->bOrphanedPin)));
#endif
    }

    static void AppendNameSet(FString& Output, const int32 IndentLevel, const TSet<FName>& Names)
    {
        if (Names.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
            return;
        }

        for (const FName Name : Names)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(NameOrNone(Name))));
        }
    }

    static void AppendOptionalPinSection(FString& Output, const int32 IndentLevel, const TArray<FOptionalPinFromProperty>& OptionalPins)
    {
        if (OptionalPins.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
            return;
        }

        for (const FOptionalPinFromProperty& OptionalPin : OptionalPins)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("property_name: %s"), *QuoteValue(NameOrNone(OptionalPin.PropertyName))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("friendly_name: %s"), *QuoteValue(OptionalPin.PropertyFriendlyName)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("tooltip: %s"), *QuoteValue(TextOrEmpty(OptionalPin.PropertyTooltip))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("category_name: %s"), *QuoteValue(NameOrNone(OptionalPin.CategoryName))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("show_pin: %s"), *BoolToString(OptionalPin.bShowPin)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("can_toggle_visibility: %s"), *BoolToString(OptionalPin.bCanToggleVisibility)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("property_is_customized: %s"), *BoolToString(OptionalPin.bPropertyIsCustomized)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("has_override_pin: %s"), *BoolToString(OptionalPin.bHasOverridePin)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("marked_for_advanced_display: %s"), *BoolToString(OptionalPin.bIsMarkedForAdvancedDisplay)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("override_enabled: %s"), *BoolToString(OptionalPin.bIsOverrideEnabled)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("set_value_pin_visible: %s"), *BoolToString(OptionalPin.bIsSetValuePinVisible)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("override_pin_visible: %s"), *BoolToString(OptionalPin.bIsOverridePinVisible)));
        }
    }

    static void AppendNodeSpecificDetails(FString& Output, const int32 IndentLevel, const UEdGraphNode* Node)
    {
        if (const UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(Node))
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("anim_graph_node:"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("blueprint_usage: %s"), *QuoteValue(DescribeBlueprintUsage(AnimGraphNode->BlueprintUsage))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("tag: %s"), *QuoteValue(NameOrNone(AnimGraphNode->GetTag()))));
            AppendMemberReference(Output, IndentLevel + 1, TEXT("initial_update_function"), AnimGraphNode->InitialUpdateFunction);
            AppendMemberReference(Output, IndentLevel + 1, TEXT("become_relevant_function"), AnimGraphNode->BecomeRelevantFunction);
            AppendMemberReference(Output, IndentLevel + 1, TEXT("update_function"), AnimGraphNode->UpdateFunction);

            AppendIndentedLine(Output, IndentLevel + 1, TEXT("always_dynamic_properties:"));
            AppendNameSet(Output, IndentLevel + 2, AnimGraphNode->AlwaysDynamicProperties);

            AppendIndentedLine(Output, IndentLevel + 1, TEXT("optional_pins:"));
            AppendOptionalPinSection(Output, IndentLevel + 2, AnimGraphNode->ShowPinForProperties);

            if (const UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node))
            {
                AppendIndentedLine(Output, IndentLevel + 1, TEXT("state_machine:"));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("name: %s"), *QuoteValue(const_cast<UAnimGraphNode_StateMachineBase*>(StateMachineNode)->GetStateMachineName())));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("editor_graph: %s"), *QuoteValue(ObjectPathOrNone(StateMachineNode->EditorStateMachineGraph.Get()))));
            }
        }

        if (const UAnimStateNodeBase* AnimStateNodeBase = Cast<UAnimStateNodeBase>(Node))
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("anim_state_node_base:"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("state_name: %s"), *QuoteValue(AnimStateNodeBase->GetStateName())));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("bound_graph: %s"), *QuoteValue(ObjectPathOrNone(AnimStateNodeBase->GetBoundGraph()))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("input_pin: %s"), *QuoteValue(NameOrNone(AnimStateNodeBase->GetInputPin() ? AnimStateNodeBase->GetInputPin()->PinName : NAME_None))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("output_pin: %s"), *QuoteValue(NameOrNone(AnimStateNodeBase->GetOutputPin() ? AnimStateNodeBase->GetOutputPin()->PinName : NAME_None))));

            TArray<UAnimStateTransitionNode*> TransitionNodes;
            AnimStateNodeBase->GetTransitionList(TransitionNodes, true);
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("transitions:"));
            if (TransitionNodes.Num() == 0)
            {
                AppendIndentedLine(Output, IndentLevel + 2, TEXT("[]"));
            }
            else
            {
                for (const UAnimStateTransitionNode* TransitionNode : TransitionNodes)
                {
                    AppendIndentedLine(Output, IndentLevel + 2, TEXT("-"));
                    AppendIndentedLine(Output, IndentLevel + 3, FString::Printf(TEXT("name: %s"), *QuoteValue(TransitionNode ? TransitionNode->GetStateName() : FString())));
                    AppendIndentedLine(Output, IndentLevel + 3, FString::Printf(TEXT("path: %s"), *QuoteValue(ObjectPathOrNone(TransitionNode))));
                }
            }
        }

        if (const UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("anim_state:"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("bound_graph: %s"), *QuoteValue(ObjectPathOrNone(StateNode->BoundGraph.Get()))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("state_type: %s"), *QuoteValue(DescribeAnimStateType(StateNode->StateType.GetValue()))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("always_reset_on_entry: %s"), *BoolToString(StateNode->bAlwaysResetOnEntry)));
        }

        if (const UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("anim_transition:"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("bound_graph: %s"), *QuoteValue(ObjectPathOrNone(TransitionNode->BoundGraph.Get()))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("custom_transition_graph: %s"), *QuoteValue(ObjectPathOrNone(TransitionNode->GetCustomTransitionGraph()))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("previous_state: %s"), *QuoteValue(TransitionNode->GetPreviousState() ? TransitionNode->GetPreviousState()->GetStateName() : FString(TEXT("None")))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("next_state: %s"), *QuoteValue(TransitionNode->GetNextState() ? TransitionNode->GetNextState()->GetStateName() : FString(TEXT("None")))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("priority_order: %d"), TransitionNode->PriorityOrder));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("crossfade_duration: %s"), *QuoteValue(LexToString(TransitionNode->CrossfadeDuration))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("blend_mode: %s"), *QuoteValue(DescribeEnumValue(StaticEnum<EAlphaBlendOption>(), static_cast<int64>(TransitionNode->BlendMode)))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("custom_blend_curve: %s"), *QuoteValue(ObjectPathOrNone(TransitionNode->CustomBlendCurve.Get()))));
#if UE_VERSION_OLDER_THAN(5, 6, 0)
            const UObject* BlendProfileObject = TransitionNode->BlendProfile.Get();
#else
            const UObject* BlendProfileObject = TransitionNode->BlendProfileWrapper.GetBlendProfile();
#endif
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("blend_profile: %s"), *QuoteValue(ObjectPathOrNone(BlendProfileObject))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("automatic_rule_based_on_sequence_player_in_state: %s"), *BoolToString(TransitionNode->bAutomaticRuleBasedOnSequencePlayerInState)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("automatic_rule_trigger_time: %s"), *QuoteValue(LexToString(TransitionNode->AutomaticRuleTriggerTime))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("sync_group_name_to_require_valid_markers_rule: %s"), *QuoteValue(NameOrNone(TransitionNode->SyncGroupNameToRequireValidMarkersRule))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("logic_type: %s"), *QuoteValue(FString::Printf(TEXT("%d"), static_cast<int32>(TransitionNode->LogicType.GetValue())))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("bidirectional: %s"), *BoolToString(TransitionNode->Bidirectional)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("shared_rules: %s"), *BoolToString(TransitionNode->bSharedRules)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("shared_crossfade: %s"), *BoolToString(TransitionNode->bSharedCrossfade)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("shared_rules_name: %s"), *QuoteValue(TransitionNode->SharedRulesName)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("shared_rules_guid: %s"), *QuoteValue(GuidToString(TransitionNode->SharedRulesGuid))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("shared_crossfade_name: %s"), *QuoteValue(TransitionNode->SharedCrossfadeName)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("shared_crossfade_guid: %s"), *QuoteValue(GuidToString(TransitionNode->SharedCrossfadeGuid))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("shared_crossfade_idx: %d"), TransitionNode->SharedCrossfadeIdx));
        }

        if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("call_function:"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("pure: %s"), *BoolToString(CallFunctionNode->bIsPureFunc)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("const: %s"), *BoolToString(CallFunctionNode->bIsConstFunc)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("interface_call: %s"), *BoolToString(CallFunctionNode->bIsInterfaceCall)));
            AppendMemberReference(Output, IndentLevel + 1, TEXT("function_reference"), CallFunctionNode->FunctionReference);
        }
        else if (const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(Node))
        {
            AppendMemberReference(Output, IndentLevel, TEXT("variable_reference"), VariableGetNode->VariableReference);
        }
        else if (const UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(Node))
        {
            AppendMemberReference(Output, IndentLevel, TEXT("variable_reference"), VariableSetNode->VariableReference);
        }
        else if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("event:"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("override_function: %s"), *BoolToString(EventNode->bOverrideFunction)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("internal_event: %s"), *BoolToString(EventNode->bInternalEvent)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("custom_function_name: %s"), *QuoteValue(NameOrNone(EventNode->CustomFunctionName))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("function_flags: %s"), *QuoteValue(DescribeFunctionFlags(EventNode->FunctionFlags))));
            AppendMemberReference(Output, IndentLevel + 1, TEXT("event_reference"), EventNode->EventReference);
        }
        else if (const UK2Node_DynamicCast* DynamicCastNode = Cast<UK2Node_DynamicCast>(Node))
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("dynamic_cast:"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("target_type: %s"), *QuoteValue(ObjectPathOrNone(DynamicCastNode->TargetType))));
        }
        else if (const UK2Node_MacroInstance* MacroInstanceNode = Cast<UK2Node_MacroInstance>(Node))
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("macro_instance:"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("macro_graph: %s"), *QuoteValue(ObjectPathOrNone(MacroInstanceNode->GetMacroGraph()))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("source_blueprint: %s"), *QuoteValue(ObjectPathOrNone(MacroInstanceNode->GetSourceBlueprint()))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("resolved_wildcard_type: %s"), *QuoteValue(DescribePinType(MacroInstanceNode->ResolvedWildcardType))));
        }
        else if (const UK2Node_FunctionEntry* FunctionEntryNode = Cast<UK2Node_FunctionEntry>(Node))
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("function_entry:"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("custom_generated_function_name: %s"), *QuoteValue(NameOrNone(FunctionEntryNode->CustomGeneratedFunctionName))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("function_flags: %s"), *QuoteValue(DescribeFunctionFlags(FunctionEntryNode->GetFunctionFlags()))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("extra_flags: %s"), *QuoteValue(DescribeFunctionFlags(FunctionEntryNode->GetExtraFlags()))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("enforce_const_correctness: %s"), *BoolToString(FunctionEntryNode->bEnforceConstCorrectness)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("local_variable_count: %d"), FunctionEntryNode->LocalVariables.Num()));
        }
    }

    static UK2Node_FunctionEntry* FindFunctionEntryNode(const UEdGraph* Graph)
    {
        if (!Graph)
        {
            return nullptr;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
            {
                return EntryNode;
            }
        }

        return nullptr;
    }

    static void FindFunctionResultNodes(const UEdGraph* Graph, TArray<const UK2Node_FunctionResult*>& OutResultNodes)
    {
        OutResultNodes.Reset();
        if (!Graph)
        {
            return;
        }

        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (const UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
            {
                OutResultNodes.Add(ResultNode);
            }
        }
    }

    static TArray<const UEdGraphPin*> CollectFunctionPins(const UEdGraphNode* Node, const EEdGraphPinDirection DesiredDirection, const UEdGraphPin* PinToSkip = nullptr)
    {
        TArray<const UEdGraphPin*> Result;
        if (!Node)
        {
            return Result;
        }

        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin == PinToSkip || Pin->Direction != DesiredDirection || IsExecPin(Pin))
            {
                continue;
            }

            if (Pin->PinName == UEdGraphSchema_K2::PN_Execute || Pin->PinName == UEdGraphSchema_K2::PN_Then)
            {
                continue;
            }

            Result.Add(Pin);
        }

        return Result;
    }

    static void AppendFunctionPinSection(FString& Output, const int32 IndentLevel, const TArray<const UEdGraphPin*>& Pins)
    {
        if (Pins.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
            return;
        }

        for (const UEdGraphPin* Pin : Pins)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(NameOrNone(Pin->PinName))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("direction: %s"), *QuoteValue(DescribePinDirection(Pin->Direction))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("type: %s"), *QuoteValue(DescribePinType(Pin->PinType))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_value: %s"), *QuoteValue(Pin->DefaultValue)));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_object: %s"), *QuoteValue(ObjectPathOrNone(Pin->DefaultObject))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_text_value: %s"), *QuoteValue(TextOrEmpty(Pin->DefaultTextValue))));
        }
    }

    static void AppendClassHierarchy(FString& Output, const int32 IndentLevel, const UClass* StartClass)
    {
        if (!StartClass)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
            return;
        }

        for (const UClass* CurrentClass = StartClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
        {
            const UBlueprint* OwnerBlueprint = Cast<UBlueprint>(CurrentClass->ClassGeneratedBy);

            AppendIndentedLine(Output, IndentLevel, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("class: %s"), *QuoteValue(CurrentClass->GetPathName())));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(CurrentClass->GetName())));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("owner_blueprint: %s"), *QuoteValue(ObjectPathOrNone(OwnerBlueprint))));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("native_class: %s"), *BoolToString(CurrentClass->ClassGeneratedBy == nullptr)));
        }
    }

    static void AppendFunctionPropertySection(FString& Output, const int32 IndentLevel, const UFunction* Function, const bool bReturnValues)
    {
        if (!Function)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
            return;
        }

        bool bHasProperties = false;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            const FProperty* Property = *It;
            if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
            {
                continue;
            }

            const bool bIsReturnValue = Property->HasAnyPropertyFlags(CPF_ReturnParm);
            if (bIsReturnValue != bReturnValues)
            {
                continue;
            }

            bHasProperties = true;
            AppendIndentedLine(Output, IndentLevel, TEXT("-"));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(Property->GetName())));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Property->GetCPPType())));
            AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("property_flags: %s"), *QuoteValue(DescribePropertyFlags(Property->PropertyFlags))));
        }

        if (!bHasProperties)
        {
            AppendIndentedLine(Output, IndentLevel, TEXT("[]"));
        }
    }

    static void AppendGeneratedFunctionDescription(FString& Output, const int32 IndentLevel, const UFunction* Function, const UClass* GeneratedClass)
    {
        const UClass* OwnerClass = Function ? Cast<UClass>(Function->GetOuter()) : nullptr;
        const UBlueprint* OwnerBlueprint = OwnerClass ? Cast<UBlueprint>(OwnerClass->ClassGeneratedBy) : nullptr;

        AppendIndentedLine(Output, IndentLevel, TEXT("-"));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("name: %s"), *QuoteValue(Function ? Function->GetName() : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("owner_class: %s"), *QuoteValue(ObjectPathOrNone(OwnerClass))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("owner_blueprint: %s"), *QuoteValue(ObjectPathOrNone(OwnerBlueprint))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("declared_in_this_blueprint: %s"), *BoolToString(OwnerClass == GeneratedClass)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("function_flags: %s"), *QuoteValue(Function ? DescribeFunctionFlags(Function->FunctionFlags) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("num_parms: %d"), Function ? Function->NumParms : 0));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("parm_size: %d"), Function ? Function->ParmsSize : 0));

        AppendIndentedLine(Output, IndentLevel + 1, TEXT("parameters:"));
        AppendFunctionPropertySection(Output, IndentLevel + 2, Function, false);

        AppendIndentedLine(Output, IndentLevel + 1, TEXT("return_values:"));
        AppendFunctionPropertySection(Output, IndentLevel + 2, Function, true);
    }

    static void AppendSCSNodeDescription(
        FString& Output,
        const int32 IndentLevel,
        const UBlueprint* Blueprint,
        const USCS_Node* Node,
        const USCS_Node* DefaultSceneRootNode)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("-"));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("variable_name: %s"), *QuoteValue(NameOrNone(Node ? Node->GetVariableName() : NAME_None))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("variable_guid: %s"), *QuoteValue(GuidToString(Node ? Node->VariableGuid : FGuid()))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("is_default_scene_root: %s"), *BoolToString(Node == DefaultSceneRootNode)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("is_root_node: %s"), *BoolToString(Node ? Node->IsRootNode() : false)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("component_class: %s"), *QuoteValue(Node ? ObjectPathOrNone(Node->ComponentClass.Get()) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("component_template: %s"), *QuoteValue(Node ? ObjectPathOrNone(Node->ComponentTemplate.Get()) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("attach_to_name: %s"), *QuoteValue(Node ? NameOrNone(Node->AttachToName) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("parent_component_or_variable_name: %s"), *QuoteValue(Node ? NameOrNone(Node->ParentComponentOrVariableName) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("parent_component_owner_class_name: %s"), *QuoteValue(Node ? NameOrNone(Node->ParentComponentOwnerClassName) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("parent_component_native: %s"), *BoolToString(Node ? Node->bIsParentComponentNative : false)));
#if WITH_EDITORONLY_DATA
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("category_name: %s"), *QuoteValue(Node ? TextOrEmpty(Node->CategoryName) : FString())));
#endif

#if WITH_EDITOR
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("resolved_parent_component_template: %s"), *QuoteValue(Node && Blueprint ? ObjectPathOrNone(Node->GetParentComponentTemplate(const_cast<UBlueprint*>(Blueprint))) : FString())));
#endif

        if (Node)
        {
            AppendVariableMetaData(Output, IndentLevel + 1, Node->MetaDataArray);
        }
        else
        {
            AppendVariableMetaData(Output, IndentLevel + 1, TArray<FBPVariableMetaDataEntry>());
        }

        AppendIndentedLine(Output, IndentLevel + 1, TEXT("component_template_properties:"));
        AppendReflectedProperties(Output, IndentLevel + 2, Node ? Node->ComponentTemplate.Get() : nullptr, EReflectedExportContext::ComponentTemplate);

        AppendIndentedLine(Output, IndentLevel + 1, TEXT("children:"));
        if (!Node || Node->GetChildNodes().Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 2, TEXT("[]"));
        }
        else
        {
            for (const USCS_Node* ChildNode : Node->GetChildNodes())
            {
                if (ChildNode)
                {
                    AppendSCSNodeDescription(Output, IndentLevel + 2, Blueprint, ChildNode, DefaultSceneRootNode);
                }
            }
        }
    }

    static void AppendComponentKeyDescription(FString& Output, const int32 IndentLevel, const FComponentKey& ComponentKey)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("component_key:"));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("owner_class: %s"), *QuoteValue(ObjectPathOrNone(ComponentKey.GetComponentOwner()))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("scs_variable_name: %s"), *QuoteValue(NameOrNone(ComponentKey.GetSCSVariableName()))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("associated_guid: %s"), *QuoteValue(GuidToString(ComponentKey.GetAssociatedGuid()))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("is_scs_key: %s"), *BoolToString(ComponentKey.IsSCSKey())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("is_ucs_key: %s"), *BoolToString(ComponentKey.IsUCSKey())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("is_valid: %s"), *BoolToString(ComponentKey.IsValid())));
    }

    static void AppendTimelineMetadata(FString& Output, const int32 IndentLevel, const UTimelineTemplate* Timeline)
    {
        if (Timeline)
        {
            AppendVariableMetaData(Output, IndentLevel, Timeline->MetaDataArray);
        }
        else
        {
            AppendVariableMetaData(Output, IndentLevel, TArray<FBPVariableMetaDataEntry>());
        }
    }

    static void AppendTimelineTracks(FString& Output, const int32 IndentLevel, const UTimelineTemplate* Timeline)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("event_tracks:"));
        if (!Timeline || Timeline->EventTracks.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
        }
        else
        {
            for (const FTTEventTrack& Track : Timeline->EventTracks)
            {
                AppendIndentedLine(Output, IndentLevel + 1, TEXT("-"));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("track_name: %s"), *QuoteValue(NameOrNone(Track.GetTrackName()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("function_name: %s"), *QuoteValue(NameOrNone(Track.GetFunctionName()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("curve: %s"), *QuoteValue(ObjectPathOrNone(Track.CurveKeys.Get()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("external_curve: %s"), *BoolToString(Track.bIsExternalCurve)));
            }
        }

        AppendIndentedLine(Output, IndentLevel, TEXT("float_tracks:"));
        if (!Timeline || Timeline->FloatTracks.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
        }
        else
        {
            for (const FTTFloatTrack& Track : Timeline->FloatTracks)
            {
                AppendIndentedLine(Output, IndentLevel + 1, TEXT("-"));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("track_name: %s"), *QuoteValue(NameOrNone(Track.GetTrackName()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("property_name: %s"), *QuoteValue(NameOrNone(Track.GetPropertyName()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("curve: %s"), *QuoteValue(ObjectPathOrNone(Track.CurveFloat.Get()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("external_curve: %s"), *BoolToString(Track.bIsExternalCurve)));
            }
        }

        AppendIndentedLine(Output, IndentLevel, TEXT("vector_tracks:"));
        if (!Timeline || Timeline->VectorTracks.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
        }
        else
        {
            for (const FTTVectorTrack& Track : Timeline->VectorTracks)
            {
                AppendIndentedLine(Output, IndentLevel + 1, TEXT("-"));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("track_name: %s"), *QuoteValue(NameOrNone(Track.GetTrackName()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("property_name: %s"), *QuoteValue(NameOrNone(Track.GetPropertyName()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("curve: %s"), *QuoteValue(ObjectPathOrNone(Track.CurveVector.Get()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("external_curve: %s"), *BoolToString(Track.bIsExternalCurve)));
            }
        }

        AppendIndentedLine(Output, IndentLevel, TEXT("linear_color_tracks:"));
        if (!Timeline || Timeline->LinearColorTracks.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 1, TEXT("[]"));
        }
        else
        {
            for (const FTTLinearColorTrack& Track : Timeline->LinearColorTracks)
            {
                AppendIndentedLine(Output, IndentLevel + 1, TEXT("-"));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("track_name: %s"), *QuoteValue(NameOrNone(Track.GetTrackName()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("property_name: %s"), *QuoteValue(NameOrNone(Track.GetPropertyName()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("curve: %s"), *QuoteValue(ObjectPathOrNone(Track.CurveLinearColor.Get()))));
                AppendIndentedLine(Output, IndentLevel + 2, FString::Printf(TEXT("external_curve: %s"), *BoolToString(Track.bIsExternalCurve)));
            }
        }
    }

    static void AppendAnimBlueprintDescription(FString& Output, const int32 IndentLevel, const UAnimBlueprint* AnimBlueprint)
    {
        AppendIndentedLine(Output, IndentLevel, TEXT("anim_blueprint:"));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("target_skeleton: %s"), *QuoteValue(ObjectPathOrNone(AnimBlueprint ? AnimBlueprint->TargetSkeleton.Get() : nullptr))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("is_template: %s"), *BoolToString(AnimBlueprint ? AnimBlueprint->bIsTemplate : false)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("use_multithreaded_animation_update: %s"), *BoolToString(AnimBlueprint ? AnimBlueprint->bUseMultiThreadedAnimationUpdate : false)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("warn_about_blueprint_usage: %s"), *BoolToString(AnimBlueprint ? AnimBlueprint->bWarnAboutBlueprintUsage : false)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("enable_linked_anim_layer_instance_sharing: %s"), *BoolToString(AnimBlueprint ? AnimBlueprint->bEnableLinkedAnimLayerInstanceSharing : false)));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("preview_skeletal_mesh: %s"), *QuoteValue(ObjectPathOrNone(AnimBlueprint ? AnimBlueprint->GetPreviewMesh() : nullptr))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("preview_animation_blueprint: %s"), *QuoteValue(ObjectPathOrNone(AnimBlueprint ? AnimBlueprint->GetPreviewAnimationBlueprint() : nullptr))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("preview_animation_blueprint_application_method: %s"), *QuoteValue(AnimBlueprint ? DescribePreviewAnimationBlueprintApplicationMethod(AnimBlueprint->GetPreviewAnimationBlueprintApplicationMethod()) : FString())));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("preview_animation_blueprint_tag: %s"), *QuoteValue(AnimBlueprint ? NameOrNone(AnimBlueprint->GetPreviewAnimationBlueprintTag()) : FString())));
#if WITH_EDITOR
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("root_anim_blueprint: %s"), *QuoteValue(ObjectPathOrNone(AnimBlueprint ? UAnimBlueprint::FindRootAnimBlueprint(AnimBlueprint) : nullptr))));
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("parent_anim_blueprint: %s"), *QuoteValue(ObjectPathOrNone(AnimBlueprint ? UAnimBlueprint::GetParentAnimBlueprint(AnimBlueprint) : nullptr))));
#endif
#if WITH_EDITORONLY_DATA
        AppendIndentedLine(Output, IndentLevel + 1, FString::Printf(TEXT("default_binding_class: %s"), *QuoteValue(ObjectPathOrNone(AnimBlueprint ? AnimBlueprint->GetDefaultBindingClass() : nullptr))));
#endif

        AppendIndentedLine(Output, IndentLevel + 1, TEXT("groups:"));
        if (!AnimBlueprint || AnimBlueprint->Groups.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 2, TEXT("[]"));
        }
        else
        {
            for (const FAnimGroupInfo& GroupInfo : AnimBlueprint->Groups)
            {
                AppendIndentedLine(Output, IndentLevel + 2, TEXT("-"));
                AppendIndentedLine(Output, IndentLevel + 3, FString::Printf(TEXT("name: %s"), *QuoteValue(NameOrNone(GroupInfo.Name))));
                AppendIndentedLine(Output, IndentLevel + 3, FString::Printf(TEXT("color: %s"), *QuoteValue(GroupInfo.Color.ToString())));
            }
        }

        AppendIndentedLine(Output, IndentLevel + 1, TEXT("parent_asset_overrides:"));
        if (!AnimBlueprint || AnimBlueprint->ParentAssetOverrides.Num() == 0)
        {
            AppendIndentedLine(Output, IndentLevel + 2, TEXT("[]"));
        }
        else
        {
            for (const FAnimParentNodeAssetOverride& Override : AnimBlueprint->ParentAssetOverrides)
            {
                AppendIndentedLine(Output, IndentLevel + 2, TEXT("-"));
                AppendIndentedLine(Output, IndentLevel + 3, FString::Printf(TEXT("parent_node_guid: %s"), *QuoteValue(GuidToString(Override.ParentNodeGuid))));
                AppendIndentedLine(Output, IndentLevel + 3, FString::Printf(TEXT("new_asset: %s"), *QuoteValue(ObjectPathOrNone(Override.NewAsset))));
            }
        }

        TArray<UObject*> PoseWatchFolders;
        TArray<UObject*> PoseWatches;
        if (AnimBlueprint)
        {
            for (const UObject* PoseWatchFolder : AnimBlueprint->PoseWatchFolders)
            {
                PoseWatchFolders.Add(const_cast<UObject*>(PoseWatchFolder));
            }

            for (const UObject* PoseWatch : AnimBlueprint->PoseWatches)
            {
                PoseWatches.Add(const_cast<UObject*>(PoseWatch));
            }
        }

        AppendIndentedLine(Output, IndentLevel + 1, TEXT("pose_watch_folders:"));
        AppendObjectReferenceList(Output, IndentLevel + 2, PoseWatchFolders);

        AppendIndentedLine(Output, IndentLevel + 1, TEXT("pose_watches:"));
        AppendObjectReferenceList(Output, IndentLevel + 2, PoseWatches);
    }

    static FString BuildStructuredGraphText(UBlueprint* Blueprint, UEdGraph* Graph)
    {
        FString Output;

        AppendIndentedLine(Output, 0, TEXT("graph:"));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("name: %s"), *QuoteValue(Graph->GetName())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("path: %s"), *QuoteValue(Graph->GetPathName())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("class: %s"), *QuoteValue(Graph->GetClass()->GetPathName())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("outer: %s"), *QuoteValue(ObjectPathOrNone(Graph->GetOuter()))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("kind: %s"), *QuoteValue(GetGraphKind(Blueprint, Graph))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("schema: %s"), *QuoteValue(ObjectPathOrNone(Graph->GetSchema()))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("node_count: %d"), Graph->Nodes.Num()));
        AppendIndentedLine(Output, 0, TEXT("graph_reflected_properties:"));
        AppendReflectedProperties(Output, 1, Graph, EReflectedExportContext::Graph);

        AppendIndentedLine(Output, 0, TEXT("nodes:"));
        if (Graph->Nodes.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            for (const UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }

                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("id: %s"), *QuoteValue(GetNodeStableId(Node))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("node_guid: %s"), *QuoteValue(GuidToString(Node->NodeGuid))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("name: %s"), *QuoteValue(Node->GetName())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("class: %s"), *QuoteValue(Node->GetClass()->GetPathName())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("semantic_type: %s"), *QuoteValue(GetNodeSemanticType(Node))));
                const FString CppHint = GetNodeCppHint(Node);
                if (!CppHint.IsEmpty())
                {
                    AppendIndentedLine(Output, 2, FString::Printf(TEXT("cpp_hint: %s"), *QuoteValue(CppHint)));
                }
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("title: %s"), *QuoteValue(GetNodeTitleString(Node))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("comment: %s"), *QuoteValue(Node->NodeComment)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("outer: %s"), *QuoteValue(ObjectPathOrNone(Node->GetOuter()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("enabled_state: %s"), *QuoteValue(LexToString(Node->GetDesiredEnabledState()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("advanced_pin_display: %d"), static_cast<int32>(Node->AdvancedPinDisplay)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("node_pos_x: %d"), Node->NodePosX));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("node_pos_y: %d"), Node->NodePosY));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("pin_count: %d"), Node->Pins.Num()));
                AppendNodeSpecificDetails(Output, 2, Node);

                AppendIndentedLine(Output, 2, TEXT("sub_graphs:"));
                AppendGraphReferenceList(Output, 3, Node->GetSubGraphs());

                AppendIndentedLine(Output, 2, TEXT("pins:"));
                if (Node->Pins.Num() == 0)
                {
                    AppendIndentedLine(Output, 3, TEXT("[]"));
                }
                else
                {
                    for (const UEdGraphPin* Pin : Node->Pins)
                    {
                        if (Pin)
                        {
                            AppendPinDescription(Output, 3, Node, Pin);
                        }
                    }
                }

                AppendIndentedLine(Output, 2, TEXT("reflected_properties:"));
                AppendReflectedProperties(Output, 3, Node, EReflectedExportContext::GraphNode);
            }
        }

        AppendIndentedLine(Output, 0, TEXT("links:"));
        bool bHasLinks = false;
        TSet<FString> UniqueLinks;

        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }

            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Output)
                {
                    continue;
                }

                for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (!LinkedPin)
                    {
                        continue;
                    }

                    const UEdGraphNode* TargetNode = LinkedPin->GetOwningNodeUnchecked();
                    if (!TargetNode)
                    {
                        continue;
                    }

                    const FString UniqueKey = FString::Printf(
                        TEXT("%s|%s|%s|%s"),
                        *GetNodeStableId(Node),
                        *GuidToString(Pin->PinId),
                        *GetNodeStableId(TargetNode),
                        *GuidToString(LinkedPin->PinId));

                    if (!UniqueLinks.Contains(UniqueKey))
                    {
                        UniqueLinks.Add(UniqueKey);
                        bHasLinks = true;

                        AppendIndentedLine(Output, 1, TEXT("-"));
                        AppendIndentedLine(Output, 2, FString::Printf(TEXT("source_node: %s"), *QuoteValue(GetNodeStableId(Node))));
                        AppendIndentedLine(Output, 2, FString::Printf(TEXT("source_pin: %s"), *QuoteValue(NameOrNone(Pin->PinName))));
                        AppendIndentedLine(Output, 2, FString::Printf(TEXT("source_pin_id: %s"), *QuoteValue(GuidToString(Pin->PinId))));
                        AppendIndentedLine(Output, 2, FString::Printf(TEXT("target_node: %s"), *QuoteValue(GetNodeStableId(TargetNode))));
                        AppendIndentedLine(Output, 2, FString::Printf(TEXT("target_pin: %s"), *QuoteValue(NameOrNone(LinkedPin->PinName))));
                        AppendIndentedLine(Output, 2, FString::Printf(TEXT("target_pin_id: %s"), *QuoteValue(GuidToString(LinkedPin->PinId))));
                        AppendIndentedLine(Output, 2, FString::Printf(TEXT("kind: %s"), *QuoteValue(IsExecPin(Pin) ? TEXT("exec") : TEXT("data"))));
                    }
                }
            }
        }

        if (!bHasLinks)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }

        return Output;
    }

    static FString BuildRawGraphText(UEdGraph* Graph)
    {
        TSet<UObject*> NodesToExport;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node)
            {
                NodesToExport.Add(Node);
                Node->PrepareForCopying();
            }
        }

        if (NodesToExport.IsEmpty())
        {
            return TEXT("");
        }

        for (UObject* Node : NodesToExport)
        {
            Node->Mark(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));
        }

        FString RawText;
        FEdGraphUtilities::ExportNodesToText(NodesToExport, RawText);
        return RawText;
    }

    static FString BuildGraphDot(UBlueprint* Blueprint, UEdGraph* Graph)
    {
        FString Output;
        Output += FString::Printf(TEXT("digraph \"%s\" {%s"), *EscapeDotString(Graph->GetName()), LINE_TERMINATOR);
        Output += FString::Printf(TEXT("  label=\"%s\";%s"), *EscapeDotString(FString::Printf(TEXT("%s::%s"), *Blueprint->GetName(), *Graph->GetName())), LINE_TERMINATOR);

        TArray<FDotEdge> Edges;
        TSet<FString> UniqueEdgeKeys;
        TSet<const UEdGraphNode*> NodesToEmit;

        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node || IsCommentNode(Node) || IsKnotNode(Node))
            {
                continue;
            }

            if (HasExecPins(Node) || IsImportantStandaloneNode(Node))
            {
                NodesToEmit.Add(Node);
            }

            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Output)
                {
                    continue;
                }

                TArray<const UEdGraphPin*> ResolvedTargets;
                TSet<const UEdGraphPin*> VisitedPins;
                GatherLogicalTargets(Pin, ResolvedTargets, VisitedPins);

                for (const UEdGraphPin* TargetPin : ResolvedTargets)
                {
                    if (!TargetPin)
                    {
                        continue;
                    }

                    const UEdGraphNode* TargetNode = TargetPin->GetOwningNodeUnchecked();
                    if (!TargetNode || IsCommentNode(TargetNode) || IsKnotNode(TargetNode))
                    {
                        continue;
                    }

                    FString Label;
                    if (IsExecPin(Pin) && IsExecPin(TargetPin))
                    {
                        Label = GetExecEdgeLabel(Pin);
                    }
                    else if (ShouldIncludeDataDependency(Pin, TargetPin))
                    {
                        Label = GetDataEdgeLabel(TargetPin);
                    }
                    else
                    {
                        continue;
                    }

                    NodesToEmit.Add(Node);
                    NodesToEmit.Add(TargetNode);

                    const FString EdgeKey = FString::Printf(TEXT("%s|%s|%s"),
                        *GetNodeStableId(Node),
                        *GetNodeStableId(TargetNode),
                        *Label);

                    if (!UniqueEdgeKeys.Contains(EdgeKey))
                    {
                        UniqueEdgeKeys.Add(EdgeKey);
                        FDotEdge& Edge = Edges.AddDefaulted_GetRef();
                        Edge.FromNodeId = GetNodeStableId(Node);
                        Edge.ToNodeId = GetNodeStableId(TargetNode);
                        Edge.Label = Label;
                    }
                }
            }
        }

        bool bEmittedAnyNode = false;
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node || !NodesToEmit.Contains(Node) || IsCommentNode(Node) || IsKnotNode(Node))
            {
                continue;
            }

            bEmittedAnyNode = true;
            Output += FString::Printf(TEXT("  %s [label=\"%s\"];%s"),
                *GetNodeStableId(Node),
                *EscapeDotString(GetNodeTitleString(Node)),
                LINE_TERMINATOR);
        }

        if (!bEmittedAnyNode)
        {
            Output += FString::Printf(TEXT("  %s [label=\"%s\"];%s"),
                *SanitizeDotIdentifier(TEXT("empty_graph")),
                *EscapeDotString(Graph->GetName()),
                LINE_TERMINATOR);
        }

        for (const FDotEdge& Edge : Edges)
        {
            Output += FString::Printf(TEXT("  %s -> %s [label=\"%s\"];%s"),
                *Edge.FromNodeId,
                *Edge.ToNodeId,
                *EscapeDotString(Edge.Label),
                LINE_TERMINATOR);
        }

        Output += TEXT("}");
        Output += LINE_TERMINATOR;
        return Output;
    }

    static FString BuildGraphMmid(UBlueprint* Blueprint, UEdGraph* Graph)
    {
        FString Output;
        Output += TEXT("flowchart TD");
        Output += LINE_TERMINATOR;

        TSet<FString> DefinedNodes;
        TSet<FString> UniqueEdges;

        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }

            const FString NodeId = GetNodeStableId(Node);
            if (!DefinedNodes.Contains(NodeId))
            {
                DefinedNodes.Add(NodeId);
                Output += FString::Printf(TEXT("  %s[\"%s\"]%s"),
                    *NodeId,
                    *EscapeMermaidString(BuildMmidNodeLabel(Node)),
                    LINE_TERMINATOR);
            }
        }

        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }

            const FString SourceNodeId = GetNodeStableId(Node);
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Output)
                {
                    continue;
                }

                for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (!LinkedPin)
                    {
                        continue;
                    }

                    const UEdGraphNode* TargetNode = LinkedPin->GetOwningNodeUnchecked();
                    if (!TargetNode)
                    {
                        continue;
                    }

                    const FString TargetNodeId = GetNodeStableId(TargetNode);
                    const FString EdgeLabel = FString::Printf(TEXT("%s:%s -> %s:%s (%s)"),
                        *SourceNodeId,
                        *NameOrNone(Pin->PinName),
                        *TargetNodeId,
                        *NameOrNone(LinkedPin->PinName),
                        IsExecPin(Pin) ? TEXT("exec") : TEXT("data"));
                    const FString EdgeKey = FString::Printf(TEXT("%s|%s|%s|%s"),
                        *SourceNodeId,
                        *GuidToString(Pin->PinId),
                        *TargetNodeId,
                        *GuidToString(LinkedPin->PinId));

                    if (UniqueEdges.Contains(EdgeKey))
                    {
                        continue;
                    }

                    UniqueEdges.Add(EdgeKey);
                    Output += FString::Printf(TEXT("  %s -->|\"%s\"| %s%s"),
                        *SourceNodeId,
                        *EscapeMermaidString(EdgeLabel),
                        *TargetNodeId,
                        LINE_TERMINATOR);
                }
            }
        }

        if (DefinedNodes.Num() == 0)
        {
            const FString EmptyNodeId = SanitizeDotIdentifier(TEXT("empty_graph"));
            Output += FString::Printf(TEXT("  %s[\"%s\"]%s"),
                *EmptyNodeId,
                *EscapeMermaidString(FString::Printf(TEXT("%s::%s"), *Blueprint->GetName(), *Graph->GetName())),
                LINE_TERMINATOR);
        }

        return Output;
    }

    static FString BuildStructuredBlueprintText(
        UBlueprint* Blueprint,
        const TMap<FName, FString>& StructuredGraphFiles,
        const TMap<FName, FString>& RawGraphFiles,
        const TMap<FName, FString>& DotGraphFiles,
        const TMap<FName, FString>& MmidGraphFiles)
    {
        FString Output;

        const UClass* GeneratedClass = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
        const UClass* SkeletonGeneratedClass = Blueprint ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
        const UObject* DefaultObject = GeneratedClass ? GeneratedClass->GetDefaultObject() : nullptr;

        AppendIndentedLine(Output, 0, TEXT("blueprint:"));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("name: %s"), *QuoteValue(Blueprint->GetName())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("asset_path: %s"), *QuoteValue(Blueprint->GetPathName())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("class: %s"), *QuoteValue(Blueprint->GetClass()->GetPathName())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("blueprint_type: %s"), *QuoteValue(DescribeBlueprintType(Blueprint->BlueprintType))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("parent_class: %s"), *QuoteValue(ObjectPathOrNone(Blueprint->ParentClass.Get()))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("generated_class: %s"), *QuoteValue(ObjectPathOrNone(GeneratedClass))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("skeleton_generated_class: %s"), *QuoteValue(ObjectPathOrNone(SkeletonGeneratedClass))));

        AppendIndentedLine(Output, 0, TEXT("class_hierarchy:"));
        AppendClassHierarchy(Output, 1, GeneratedClass ? GeneratedClass : Blueprint->ParentClass.Get());

        AppendIndentedLine(Output, 0, TEXT("blueprint_reflected_properties:"));
        AppendReflectedProperties(Output, 1, Blueprint, EReflectedExportContext::BlueprintAsset);

        if (const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
        {
            AppendAnimBlueprintDescription(Output, 0, AnimBlueprint);
        }

        AppendIndentedLine(Output, 0, TEXT("implemented_interfaces:"));
        if (Blueprint->ImplementedInterfaces.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            for (const FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
            {
                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("interface: %s"), *QuoteValue(ObjectPathOrNone(InterfaceDescription.Interface.Get()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("graph_count: %d"), InterfaceDescription.Graphs.Num()));
            }
        }

        AppendIndentedLine(Output, 0, TEXT("member_variables:"));
        if (Blueprint->NewVariables.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            for (const FBPVariableDescription& VariableDescription : Blueprint->NewVariables)
            {
                const FProperty* GeneratedProperty = FindGeneratedClassProperty(Blueprint, VariableDescription.VarName);
                AppendVariableDescription(Output, 1, VariableDescription, GeneratedProperty, DefaultObject, DefaultObject);
            }
        }

        AppendIndentedLine(Output, 0, TEXT("generated_class_properties:"));
        if (!GeneratedClass)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            bool bHasGeneratedProperties = false;
            for (TFieldIterator<FProperty> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                const FProperty* Property = *It;
                if (!Property)
                {
                    continue;
                }

                bool bDeclaredInNewVariables = false;
                for (const FBPVariableDescription& VariableDescription : Blueprint->NewVariables)
                {
                    if (VariableDescription.VarName == Property->GetFName())
                    {
                        bDeclaredInNewVariables = true;
                        break;
                    }
                }

                bHasGeneratedProperties = true;
                AppendGeneratedPropertyDescription(Output, 1, Property, DefaultObject, DefaultObject, bDeclaredInNewVariables);
            }

            if (!bHasGeneratedProperties)
            {
                AppendIndentedLine(Output, 1, TEXT("[]"));
            }
        }

        AppendIndentedLine(Output, 0, TEXT("inherited_blueprint_generated_properties:"));
        if (!GeneratedClass)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            bool bHasInheritedBlueprintProperties = false;
            for (TFieldIterator<FProperty> It(GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
            {
                const FProperty* Property = *It;
                const UClass* OwnerClass = Property ? Cast<UClass>(Property->GetOwnerStruct()) : nullptr;
                const bool bIsBlueprintGeneratedOwner = OwnerClass && OwnerClass->ClassGeneratedBy != nullptr;
                if (!Property || OwnerClass == GeneratedClass || !bIsBlueprintGeneratedOwner)
                {
                    continue;
                }

                bHasInheritedBlueprintProperties = true;
                AppendGeneratedPropertyDescription(Output, 1, Property, DefaultObject, DefaultObject, false);
            }

            if (!bHasInheritedBlueprintProperties)
            {
                AppendIndentedLine(Output, 1, TEXT("[]"));
            }
        }

        AppendIndentedLine(Output, 0, TEXT("simple_construction_script:"));
        if (!Blueprint->SimpleConstructionScript)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            const USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
            AppendIndentedLine(Output, 1, FString::Printf(TEXT("path: %s"), *QuoteValue(ObjectPathOrNone(SCS))));
            AppendIndentedLine(Output, 1, FString::Printf(TEXT("owner_class: %s"), *QuoteValue(ObjectPathOrNone(SCS->GetOwnerClass()))));
            AppendIndentedLine(Output, 1, FString::Printf(TEXT("parent_class: %s"), *QuoteValue(ObjectPathOrNone(SCS->GetParentClass()))));
            AppendIndentedLine(Output, 1, FString::Printf(TEXT("all_node_count: %d"), SCS->GetAllNodes().Num()));
            AppendIndentedLine(Output, 1, TEXT("root_nodes:"));
            if (SCS->GetRootNodes().Num() == 0)
            {
                AppendIndentedLine(Output, 2, TEXT("[]"));
            }
            else
            {
                for (const USCS_Node* RootNode : SCS->GetRootNodes())
                {
                    if (RootNode)
                    {
                        AppendSCSNodeDescription(Output, 2, Blueprint, RootNode, SCS->GetDefaultSceneRootNode());
                    }
                }
            }
        }

        AppendIndentedLine(Output, 0, TEXT("component_templates:"));
        if (Blueprint->ComponentTemplates.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            for (const UActorComponent* ComponentTemplate : Blueprint->ComponentTemplates)
            {
                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("path: %s"), *QuoteValue(ObjectPathOrNone(ComponentTemplate))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("name: %s"), *QuoteValue(ComponentTemplate ? ComponentTemplate->GetName() : FString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("class: %s"), *QuoteValue(ComponentTemplate ? ComponentTemplate->GetClass()->GetPathName() : FString())));
                AppendIndentedLine(Output, 2, TEXT("reflected_properties:"));
                AppendReflectedProperties(Output, 3, ComponentTemplate, EReflectedExportContext::ComponentTemplate);
            }
        }

        AppendIndentedLine(Output, 0, TEXT("inheritable_component_overrides:"));
        if (!Blueprint->InheritableComponentHandler)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            bool bHasComponentOverrides = false;
            for (auto It = Blueprint->InheritableComponentHandler->CreateRecordIterator(); It; ++It)
            {
                const FComponentOverrideRecord& Record = *It;
                bHasComponentOverrides = true;
                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("component_class: %s"), *QuoteValue(ObjectPathOrNone(Record.ComponentClass.Get()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("component_template: %s"), *QuoteValue(ObjectPathOrNone(Record.ComponentTemplate.Get()))));
                AppendComponentKeyDescription(Output, 2, Record.ComponentKey);
                AppendIndentedLine(Output, 2, TEXT("reflected_properties:"));
                AppendReflectedProperties(Output, 3, Record.ComponentTemplate.Get(), EReflectedExportContext::ComponentTemplate);
            }

            if (!bHasComponentOverrides)
            {
                AppendIndentedLine(Output, 1, TEXT("[]"));
            }
        }

        AppendIndentedLine(Output, 0, TEXT("timelines:"));
        if (Blueprint->Timelines.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            for (const UTimelineTemplate* Timeline : Blueprint->Timelines)
            {
                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("name: %s"), *QuoteValue(Timeline ? Timeline->GetName() : FString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("path: %s"), *QuoteValue(ObjectPathOrNone(Timeline))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("class: %s"), *QuoteValue(Timeline ? Timeline->GetClass()->GetPathName() : FString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("variable_name: %s"), *QuoteValue(Timeline ? NameOrNone(Timeline->GetVariableName()) : FString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("direction_property_name: %s"), *QuoteValue(Timeline ? NameOrNone(Timeline->GetDirectionPropertyName()) : FString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("update_function_name: %s"), *QuoteValue(Timeline ? NameOrNone(Timeline->GetUpdateFunctionName()) : FString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("finished_function_name: %s"), *QuoteValue(Timeline ? NameOrNone(Timeline->GetFinishedFunctionName()) : FString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("timeline_guid: %s"), *QuoteValue(GuidToString(Timeline ? Timeline->TimelineGuid : FGuid()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("timeline_length: %s"), *QuoteValue(Timeline ? LexToString(Timeline->TimelineLength) : FString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("length_mode: %s"), *QuoteValue(Timeline ? DescribeEnumValue(StaticEnum<ETimelineLengthMode>(), static_cast<int64>(Timeline->LengthMode.GetValue())) : FString())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("auto_play: %s"), *BoolToString(Timeline ? Timeline->bAutoPlay != 0 : false)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("loop: %s"), *BoolToString(Timeline ? Timeline->bLoop != 0 : false)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("replicated: %s"), *BoolToString(Timeline ? Timeline->bReplicated != 0 : false)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("ignore_time_dilation: %s"), *BoolToString(Timeline ? Timeline->bIgnoreTimeDilation != 0 : false)));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("timeline_tick_group: %s"), *QuoteValue(Timeline ? DescribeEnumValue(StaticEnum<ETickingGroup>(), static_cast<int64>(Timeline->TimelineTickGroup.GetValue())) : FString())));
                AppendTimelineMetadata(Output, 2, Timeline);
                AppendTimelineTracks(Output, 2, Timeline);
                AppendIndentedLine(Output, 2, TEXT("reflected_properties:"));
                AppendReflectedProperties(Output, 3, Timeline, EReflectedExportContext::TimelineTemplate);
            }
        }

        AppendIndentedLine(Output, 0, TEXT("functions:"));
        bool bHasFunctions = false;
        TArray<UEdGraph*> AllGraphs;
        Blueprint->GetAllGraphs(AllGraphs);

        for (UEdGraph* Graph : AllGraphs)
        {
            if (!Graph)
            {
                continue;
            }

            UK2Node_FunctionEntry* FunctionEntryNode = FindFunctionEntryNode(Graph);
            if (!FunctionEntryNode)
            {
                continue;
            }

            bHasFunctions = true;
            const FName FunctionName = FunctionEntryNode->CustomGeneratedFunctionName.IsNone() ? Graph->GetFName() : FunctionEntryNode->CustomGeneratedFunctionName;

            TArray<const UK2Node_FunctionResult*> ResultNodes;
            FindFunctionResultNodes(Graph, ResultNodes);

            const UClass* FunctionClass = GeneratedClass ? GeneratedClass : SkeletonGeneratedClass;
            const UFunction* GeneratedFunction = FunctionClass ? FunctionClass->FindFunctionByName(FunctionName) : nullptr;
            const UEdGraphPin* WorldContextPin = FunctionEntryNode->GetAutoWorldContextPin();
            const TArray<const UEdGraphPin*> InputPins = CollectFunctionPins(FunctionEntryNode, EGPD_Output, WorldContextPin);
            FunctionEntryNode->RefreshFunctionVariableCache();
            const TSharedPtr<FStructOnScope> FunctionVariableCache = FunctionEntryNode->GetFunctionVariableCache(false);
            const UStruct* FunctionVariableStruct = FunctionVariableCache.IsValid() ? FunctionVariableCache->GetStruct() : nullptr;
            const void* FunctionVariableMemory = (FunctionVariableCache.IsValid() && FunctionVariableCache->IsValid()) ? FunctionVariableCache->GetStructMemory() : nullptr;

            AppendIndentedLine(Output, 1, TEXT("-"));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("name: %s"), *QuoteValue(FunctionName.ToString())));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("graph: %s"), *QuoteValue(Graph->GetName())));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("kind: %s"), *QuoteValue(GetGraphKind(Blueprint, Graph))));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("generated_function: %s"), *QuoteValue(ObjectPathOrNone(GeneratedFunction))));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("function_flags: %s"), *QuoteValue(DescribeFunctionFlags(GeneratedFunction ? GeneratedFunction->FunctionFlags : FunctionEntryNode->GetFunctionFlags()))));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("extra_flags: %s"), *QuoteValue(DescribeFunctionFlags(FunctionEntryNode->GetExtraFlags()))));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("tool_tip: %s"), *QuoteValue(TextOrEmpty(FunctionEntryNode->MetaData.ToolTip))));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("category: %s"), *QuoteValue(TextOrEmpty(FunctionEntryNode->MetaData.Category))));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("keywords: %s"), *QuoteValue(TextOrEmpty(FunctionEntryNode->MetaData.Keywords))));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("compact_node_title: %s"), *QuoteValue(TextOrEmpty(FunctionEntryNode->MetaData.CompactNodeTitle))));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("deprecation_message: %s"), *QuoteValue(FunctionEntryNode->MetaData.DeprecationMessage)));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("deprecated: %s"), *BoolToString(FunctionEntryNode->MetaData.bIsDeprecated)));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("call_in_editor: %s"), *BoolToString(FunctionEntryNode->MetaData.bCallInEditor)));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("thread_safe: %s"), *BoolToString(FunctionEntryNode->MetaData.bThreadSafe)));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("unsafe_during_actor_construction: %s"), *BoolToString(FunctionEntryNode->MetaData.bIsUnsafeDuringActorConstruction)));
            AppendIndentedLine(Output, 2, FString::Printf(TEXT("enforce_const_correctness: %s"), *BoolToString(FunctionEntryNode->bEnforceConstCorrectness)));

            AppendIndentedLine(Output, 2, TEXT("parameters:"));
            AppendFunctionPinSection(Output, 3, InputPins);

            AppendIndentedLine(Output, 2, TEXT("return_values:"));
            if (ResultNodes.Num() == 0)
            {
                AppendIndentedLine(Output, 3, TEXT("[]"));
            }
            else
            {
                bool bHasReturnPins = false;
                for (const UK2Node_FunctionResult* ResultNode : ResultNodes)
                {
                    const TArray<const UEdGraphPin*> ReturnPins = CollectFunctionPins(ResultNode, EGPD_Input);
                    if (ReturnPins.Num() > 0)
                    {
                        bHasReturnPins = true;
                        AppendFunctionPinSection(Output, 3, ReturnPins);
                    }
                }

                if (!bHasReturnPins)
                {
                    AppendIndentedLine(Output, 3, TEXT("[]"));
                }
            }

            AppendIndentedLine(Output, 2, TEXT("local_variables:"));
            if (FunctionEntryNode->LocalVariables.Num() == 0)
            {
                AppendIndentedLine(Output, 3, TEXT("[]"));
            }
            else
            {
                for (const FBPVariableDescription& LocalVariable : FunctionEntryNode->LocalVariables)
                {
                    const FProperty* LocalProperty = FindStructProperty(FunctionVariableStruct, LocalVariable.VarName);
                    AppendVariableDescription(Output, 3, LocalVariable, LocalProperty, FunctionVariableMemory, FunctionEntryNode);
                }
            }

            AppendIndentedLine(Output, 2, TEXT("metadata_map:"));
            const TMap<FName, FString>& MetadataMap = FunctionEntryNode->MetaData.GetMetaDataMap();
            if (MetadataMap.Num() == 0)
            {
                AppendIndentedLine(Output, 3, TEXT("[]"));
            }
            else
            {
                for (const TPair<FName, FString>& Pair : MetadataMap)
                {
                    AppendIndentedLine(Output, 3, TEXT("-"));
                    AppendIndentedLine(Output, 4, FString::Printf(TEXT("key: %s"), *QuoteValue(NameOrNone(Pair.Key))));
                    AppendIndentedLine(Output, 4, FString::Printf(TEXT("value: %s"), *QuoteValue(Pair.Value)));
                }
            }
        }

        if (!bHasFunctions)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }

        AppendIndentedLine(Output, 0, TEXT("generated_functions_by_class_hierarchy:"));
        if (!GeneratedClass)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            bool bHasGeneratedFunctions = false;
            for (TFieldIterator<UFunction> It(GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
            {
                const UFunction* Function = *It;
                const UClass* OwnerClass = Function ? Cast<UClass>(Function->GetOuter()) : nullptr;
                if (!Function || !OwnerClass || OwnerClass->ClassGeneratedBy == nullptr)
                {
                    continue;
                }

                bHasGeneratedFunctions = true;
                AppendGeneratedFunctionDescription(Output, 1, Function, GeneratedClass);
            }

            if (!bHasGeneratedFunctions)
            {
                AppendIndentedLine(Output, 1, TEXT("[]"));
            }
        }

        AppendIndentedLine(Output, 0, TEXT("graphs:"));
        if (AllGraphs.Num() == 0)
        {
            AppendIndentedLine(Output, 1, TEXT("[]"));
        }
        else
        {
            for (const UEdGraph* Graph : AllGraphs)
            {
                if (!Graph)
                {
                    continue;
                }

                AppendIndentedLine(Output, 1, TEXT("-"));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("name: %s"), *QuoteValue(Graph->GetName())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("path: %s"), *QuoteValue(Graph->GetPathName())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("class: %s"), *QuoteValue(Graph->GetClass()->GetPathName())));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("kind: %s"), *QuoteValue(GetGraphKind(Blueprint, Graph))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("schema: %s"), *QuoteValue(ObjectPathOrNone(Graph->GetSchema()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("node_count: %d"), Graph->Nodes.Num()));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("raw_file: %s"), *QuoteValue(RawGraphFiles.FindRef(Graph->GetFName()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("structured_file: %s"), *QuoteValue(StructuredGraphFiles.FindRef(Graph->GetFName()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("dot_file: %s"), *QuoteValue(DotGraphFiles.FindRef(Graph->GetFName()))));
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("mmid_file: %s"), *QuoteValue(MmidGraphFiles.FindRef(Graph->GetFName()))));
            }
        }

        return Output;
    }

    static FString BuildEnumText(const UUserDefinedEnum* EnumAsset)
    {
        FString Output;
        AppendIndentedLine(Output, 0, TEXT("enum:"));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("name: %s"), *QuoteValue(EnumAsset->GetName())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("path: %s"), *QuoteValue(EnumAsset->GetPathName())));
        AppendIndentedLine(Output, 1, TEXT("entries:"));
        for (int32 Index = 0; Index < EnumAsset->NumEnums(); ++Index)
        {
            AppendIndentedLine(Output, 2, TEXT("-"));
            AppendIndentedLine(Output, 3, FString::Printf(TEXT("name: %s"), *QuoteValue(EnumAsset->GetNameStringByIndex(Index))));
            AppendIndentedLine(Output, 3, FString::Printf(TEXT("display_name: %s"), *QuoteValue(EnumAsset->GetDisplayNameTextByIndex(Index).ToString())));
            AppendIndentedLine(Output, 3, FString::Printf(TEXT("value: %lld"), static_cast<long long>(EnumAsset->GetValueByIndex(Index))));
        }
        return Output;
    }

    static FString BuildStructText(const UUserDefinedStruct* StructAsset)
    {
        FString Output;
        AppendIndentedLine(Output, 0, TEXT("struct:"));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("name: %s"), *QuoteValue(StructAsset->GetName())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("path: %s"), *QuoteValue(StructAsset->GetPathName())));
        AppendIndentedLine(Output, 1, TEXT("properties:"));

        bool bHasProperties = false;
        for (TFieldIterator<FProperty> PropertyIt(StructAsset); PropertyIt; ++PropertyIt)
        {
            const FProperty* Property = *PropertyIt;
            if (!Property)
            {
                continue;
            }

            bHasProperties = true;
            AppendIndentedLine(Output, 2, TEXT("-"));
            AppendIndentedLine(Output, 3, FString::Printf(TEXT("name: %s"), *QuoteValue(Property->GetName())));
            AppendIndentedLine(Output, 3, FString::Printf(TEXT("cpp_type: %s"), *QuoteValue(Property->GetCPPType())));
            AppendIndentedLine(Output, 3, FString::Printf(TEXT("property_flags: %s"), *QuoteValue(DescribePropertyFlags(Property->PropertyFlags))));
        }

        if (!bHasProperties)
        {
            AppendIndentedLine(Output, 2, TEXT("[]"));
        }

        return Output;
    }

    static FString BuildReflectedObjectText(const UObject* AssetObject)
    {
        FString Output;
        AppendIndentedLine(Output, 0, TEXT("asset:"));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("name: %s"), *QuoteValue(AssetObject ? AssetObject->GetName() : FString())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("path: %s"), *QuoteValue(ObjectPathOrNone(AssetObject))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("class: %s"), *QuoteValue(AssetObject ? AssetObject->GetClass()->GetPathName() : FString())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("export_type: %s"), *QuoteValue(DescribeSupportedSpecialAssetType(AssetObject))));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("outer: %s"), *QuoteValue(AssetObject ? ObjectPathOrNone(AssetObject->GetOuter()) : FString())));
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("package: %s"), *QuoteValue(AssetObject ? ObjectPathOrNone(AssetObject->GetPackage()) : FString())));
        AppendIndentedLine(Output, 1, TEXT("properties:"));
        AppendReflectedProperties(Output, 2, AssetObject, EReflectedExportContext::GenericObject);

        TArray<UObject*> Subobjects;
        if (AssetObject)
        {
            GetObjectsWithOuter(const_cast<UObject*>(AssetObject), Subobjects, false);
            Subobjects.RemoveAll([](const UObject* Object)
                {
                    return Object == nullptr || Object->HasAnyFlags(RF_Transient | RF_ClassDefaultObject);
                });
            Subobjects.Sort([](const UObject& A, const UObject& B)
                {
                    return A.GetPathName() < B.GetPathName();
                });
        }

        // Subobjects: output path/class only - do NOT recurse into properties.
        // PoseSearchDatabase and similar assets can have thousands of subobjects;
        // expanding each one's properties causes unbounded serialization and freezes the editor.
        AppendIndentedLine(Output, 1, FString::Printf(TEXT("subobject_count: %d"), Subobjects.Num()));
        AppendIndentedLine(Output, 1, TEXT("subobjects:"));
        if (Subobjects.Num() == 0)
        {
            AppendIndentedLine(Output, 2, TEXT("[]"));
        }
        else
        {
            const int32 MaxSubobjects = 64;
            const int32 OutputCount = FMath::Min(Subobjects.Num(), MaxSubobjects);
            for (int32 i = 0; i < OutputCount; ++i)
            {
                const UObject* Subobject = Subobjects[i];
                AppendIndentedLine(Output, 2, TEXT("-"));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("name: %s"), *QuoteValue(Subobject ? Subobject->GetName() : FString())));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("gate: %s"), *QuoteValue(ObjectPathOrNone(Subobject))));
                AppendIndentedLine(Output, 3, FString::Printf(TEXT("class: %s"), *QuoteValue(Subobject ? Subobject->GetClass()->GetPathName() : FString())));
            }
            if (Subobjects.Num() > MaxSubobjects)
            {
                AppendIndentedLine(Output, 2, FString::Printf(TEXT("# ... %d more subobjects truncated"), Subobjects.Num() - MaxSubobjects));
            }
        }
        return Output;
    }

    static void AddSupportedAssetClassPaths(FARFilter& Filter)
    {
        Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
        Filter.ClassPaths.Add(UUserDefinedEnum::StaticClass()->GetClassPathName());
        Filter.ClassPaths.Add(UUserDefinedStruct::StaticClass()->GetClassPathName());
        Filter.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());
        Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Chooser"), TEXT("ChooserTable")));
        Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Chooser"), TEXT("ProxyTable")));
        Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/PoseSearch"), TEXT("PoseSearchDatabase")));
        Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/PoseSearch"), TEXT("PoseSearchSchema")));
    }

    static FExportDirectorySet EnsureExportDirectories()
    {
        FExportDirectorySet Directories;
        Directories.Root = FPaths::ProjectDir() / TEXT("ExportedBlueprints");
        Directories.TxtRoot = Directories.Root / TEXT("txt");
        Directories.TxtRawRoot = Directories.TxtRoot / TEXT("raw");
        Directories.TxtStructuredRoot = Directories.TxtRoot / TEXT("structured");
        Directories.TxtOtherRoot = Directories.TxtRoot / TEXT("other");
        Directories.DotRoot = Directories.Root / TEXT("dot");
        Directories.MmidRoot = Directories.Root / TEXT("mmid");
        Directories.VariablesRoot = Directories.Root / TEXT("variables");
        Directories.MappingsRoot = Directories.Root / TEXT("mappings");
        Directories.ReadmeRoot = Directories.Root / TEXT("readme");

        IFileManager& FileManager = IFileManager::Get();
        FileManager.MakeDirectory(*Directories.Root, true);
        FileManager.MakeDirectory(*Directories.TxtRoot, true);
        FileManager.MakeDirectory(*Directories.TxtRawRoot, true);
        FileManager.MakeDirectory(*Directories.TxtStructuredRoot, true);
        FileManager.MakeDirectory(*Directories.TxtOtherRoot, true);
        FileManager.MakeDirectory(*Directories.DotRoot, true);
        FileManager.MakeDirectory(*Directories.MmidRoot, true);
        FileManager.MakeDirectory(*Directories.VariablesRoot, true);
        FileManager.MakeDirectory(*Directories.MappingsRoot, true);
        FileManager.MakeDirectory(*Directories.ReadmeRoot, true);

        return Directories;
    }
}

void FExportBlueprintToTxtModule::StartupModule()
{
    UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: StartupModule begin"));

#if ENGINE_MAJOR_VERSION == 5
    UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: Running on UE5"));
#elif ENGINE_MAJOR_VERSION == 4
    UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: Running on UE4"));
#else
#error This plugin requires Unreal Engine 4 or 5.
#endif

    try
    {
        UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: Registering commands"));
        FExportBlueprintToTxtCommands::Register();

        UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: Creating command list"));
        PluginCommands = MakeShareable(new FUICommandList);

        UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: Mapping actions"));
        PluginCommands->MapAction(
            FExportBlueprintToTxtCommands::Get().ExportBlueprintsCommand,
            FExecuteAction::CreateRaw(this, &FExportBlueprintToTxtModule::ExportBlueprintsToText),
            FCanExecuteAction());

        PluginCommands->MapAction(
            FExportBlueprintToTxtCommands::Get().ExportSelectedBlueprintsCommand,
            FExecuteAction::CreateRaw(this, &FExportBlueprintToTxtModule::ExportSelectedBlueprintsToText),
            FCanExecuteAction());

        PluginCommands->MapAction(
            FExportBlueprintToTxtCommands::Get().ExportBlueprintsInFolderCommand,
            FExecuteAction::CreateRaw(this, &FExportBlueprintToTxtModule::ExportBlueprintsInFolderToText),
            FCanExecuteAction());

        UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: Loading LevelEditor module"));
        FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

        UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: Adding menu extension"));
        {
            TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
            MenuExtender->AddMenuExtension("FileProject", EExtensionHook::After, PluginCommands, FMenuExtensionDelegate::CreateRaw(this, &FExportBlueprintToTxtModule::AddMenuExtension));
            LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
        }

        UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: Adding toolbar extension"));
        {
            TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
            ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FExportBlueprintToTxtModule::AddToolbarExtension));
            LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
        }

        UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: Registering asset context menu"));
        RegisterAssetContextMenu();

        UE_LOG(LogTemp, Log, TEXT("ExportBlueprintToTxt: StartupModule completed successfully"));
    }
    catch (const std::exception& Error)
    {
        UE_LOG(LogTemp, Error, TEXT("ExportBlueprintToTxt: Initialization failed: %s"), UTF8_TO_TCHAR(Error.what()));
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("ExportBlueprintToTxt: Initialization failed with unknown exception"));
    }
}

void FExportBlueprintToTxtModule::ShutdownModule()
{
    FExportBlueprintToTxtCommands::Unregister();

    FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    if (FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
    {
        FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");
        TArray<FContentBrowserMenuExtender_SelectedAssets>& AssetMenuExtenders = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
        AssetMenuExtenders.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
            {
                return Delegate.GetHandle() == AssetExtenderDelegateHandle;
            });

        TArray<FContentBrowserMenuExtender_SelectedPaths>& PathMenuExtenders = ContentBrowserModule.GetAllPathViewContextMenuExtenders();
        PathMenuExtenders.RemoveAll([this](const FContentBrowserMenuExtender_SelectedPaths& Delegate)
            {
                return Delegate.GetHandle() == PathExtenderDelegateHandle;
            });
    }
}

void FExportBlueprintToTxtModule::ExportSelectedBlueprintsToText()
{
    TArray<FAssetData> SelectedAssets;
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

    ExportBlueprintsToTextInternal(SelectedAssets);
}

void FExportBlueprintToTxtModule::ExportAssetsToText(const TArray<FAssetData>& Assets)
{
    ExportBlueprintsToTextInternal(Assets);
}

void FExportBlueprintToTxtModule::ExportBlueprintsInPathsToText(const TArray<FString>& InSelectedPaths)
{
    TArray<FString> SelectedPaths = InSelectedPaths;

    if (SelectedPaths.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoContentBrowserFolderSelected", "Please select at least one folder in the Content Browser."));
        return;
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    if (AssetRegistry.IsLoadingAssets())
    {
        AssetRegistry.WaitForCompletion();
    }

    TArray<FAssetData> FolderAssets;
    for (const FString& Path : SelectedPaths)
    {
        const FString ProcessedPath = Path.Replace(TEXT("/All/Game/"), TEXT("/Game/"));
        UE_LOG(LogTemp, Log, TEXT("Processing path: %s"), *ProcessedPath);

        FARFilter Filter;
        Filter.PackagePaths.Add(*ProcessedPath);
        Filter.bRecursivePaths = true;
        AddSupportedAssetClassPaths(Filter);
        Filter.bRecursiveClasses = true;

        TArray<FAssetData> AssetsInPath;
        AssetRegistry.GetAssets(Filter, AssetsInPath);
        UE_LOG(LogTemp, Log, TEXT("Found %d supported assets in path"), AssetsInPath.Num());

        if (AssetsInPath.Num() == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("All assets in path:"));
            TArray<FAssetData> AllAssetsInPath;
            AssetRegistry.GetAssetsByPath(*ProcessedPath, AllAssetsInPath, true);
            for (const FAssetData& Asset : AllAssetsInPath)
            {
#if ENGINE_MAJOR_VERSION >= 5
                UE_LOG(LogTemp, Log, TEXT("  Asset: %s, Class: %s"), *Asset.AssetName.ToString(), *Asset.AssetClassPath.ToString());
#else
                UE_LOG(LogTemp, Log, TEXT("  Asset: %s, Class: %s"), *Asset.AssetName.ToString(), *Asset.AssetClass.ToString());
#endif
            }
        }

        FolderAssets.Append(AssetsInPath);
    }

    UE_LOG(LogTemp, Log, TEXT("Total supported assets found: %d"), FolderAssets.Num());
    ExportBlueprintsToTextInternal(FolderAssets);
}

void FExportBlueprintToTxtModule::ExportBlueprintsInFolderToText()
{
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    TArray<FString> SelectedPaths;
    ContentBrowserModule.Get().GetSelectedFolders(SelectedPaths);
    ExportBlueprintsInPathsToText(SelectedPaths);
}

void FExportBlueprintToTxtModule::ExportBlueprintsToText()
{
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    if (AssetRegistry.IsLoadingAssets())
    {
        AssetRegistry.WaitForCompletion();
    }

    FARFilter Filter;
    AddSupportedAssetClassPaths(Filter);
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;
    Filter.PackagePaths.Add("/Game");

    TArray<FAssetData> AllAssets;
    AssetRegistry.GetAssets(Filter, AllAssets);

    ExportBlueprintsToTextInternal(AllAssets);
}

TSharedRef<FExtender> FExportBlueprintToTxtModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
    TSharedRef<FExtender> Extender(new FExtender());

    Extender->AddMenuExtension(
        "AssetContextReferences",
        EExtensionHook::After,
        PluginCommands,
        FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
            {
                MenuBuilder.AddMenuEntry(
                    LOCTEXT("ExportSelectedBlueprintsToTxt", "Export Selected Assets to TXT"),
                    LOCTEXT("ExportSelectedBlueprintsToTxtTooltip", "Export the selected supported assets to text files"),
                    FSlateIcon(),
                    FUIAction(FExecuteAction::CreateRaw(this, &FExportBlueprintToTxtModule::ExportSelectedBlueprintsToText)));
            }));

    return Extender;
}

TSharedRef<FExtender> FExportBlueprintToTxtModule::OnExtendContentBrowserPathSelectionMenu(const TArray<FString>& SelectedPaths)
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
                    LOCTEXT("ExportBlueprintsInFolderToTxt", "Export Assets in Folder to TXT"),
                    LOCTEXT("ExportBlueprintsInFolderToTxtTooltip", "Export supported assets in the selected folder to text files"),
                    FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, CapturedPaths]()
                        {
                            ExportBlueprintsInPathsToText(CapturedPaths);
                        })));
            }));

    return Extender;
}

void FExportBlueprintToTxtModule::ExportBlueprintsToTextInternal(const TArray<FAssetData>& Assets)
{
    const FExportDirectorySet Directories = EnsureExportDirectories();
    const FString BpToCppMapPath = Directories.MappingsRoot / TEXT("BpToCppMap.yaml");
    FFileHelper::SaveStringToFile(BuildBpToCppMapYaml(), *BpToCppMapPath, FFileHelper::EEncodingOptions::ForceUTF8);
    ExportProjectInputMappings(Directories);

    if (Assets.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No supported assets found to export."));
        return;
    }

    FScopedSlowTask SlowTask(Assets.Num(), LOCTEXT("ExportingBlueprints", "Exporting Assets..."));
    SlowTask.MakeDialog();

    for (const FAssetData& Asset : Assets)
    {
        SlowTask.EnterProgressFrame(1, FText::Format(LOCTEXT("ExportingBlueprint", "Exporting asset: {0}"), FText::FromString(Asset.AssetName.ToString())));

        const FString AssetName = Asset.AssetName.ToString();
        const FSoftObjectPath AssetPath = Asset.GetSoftObjectPath();
        UObject* AssetObject = AssetPath.TryLoad();
        if (!AssetObject)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to load asset for export: %s"), *AssetName);
            continue;
        }

        if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(AssetObject))
        {
            const FString BlueprintFolderName = SanitizeFileName(BlueprintAsset->GetName());
            const FString BlueprintRawDir = Directories.TxtRawRoot / BlueprintFolderName;
            const FString BlueprintStructuredDir = Directories.TxtStructuredRoot / BlueprintFolderName;
            const FString BlueprintDotDir = Directories.DotRoot / BlueprintFolderName;
            const FString BlueprintMmidDir = Directories.MmidRoot / BlueprintFolderName;
            const FString BlueprintReadmeDir = Directories.ReadmeRoot / BlueprintFolderName;

            IFileManager& FileManager = IFileManager::Get();
            FileManager.MakeDirectory(*BlueprintRawDir, true);
            FileManager.MakeDirectory(*BlueprintStructuredDir, true);
            FileManager.MakeDirectory(*BlueprintDotDir, true);
            FileManager.MakeDirectory(*BlueprintMmidDir, true);
            FileManager.MakeDirectory(*BlueprintReadmeDir, true);

            FString CombinedRawText;
            TMap<FName, FString> RawGraphFiles;
            TMap<FName, FString> StructuredGraphFiles;
            TMap<FName, FString> DotGraphFiles;
            TMap<FName, FString> MmidGraphFiles;

            TArray<UEdGraph*> AllGraphs;
            BlueprintAsset->GetAllGraphs(AllGraphs);

            for (UEdGraph* Graph : AllGraphs)
            {
                if (!Graph)
                {
                    continue;
                }

                const FString GraphBaseName = SanitizeFileName(FString::Printf(TEXT("%s_%s"), *GetGraphKind(BlueprintAsset, Graph), *Graph->GetName()));
                const FString RawGraphRelativePath = BlueprintFolderName / (GraphBaseName + TEXT(".txt"));
                const FString StructuredGraphRelativePath = BlueprintFolderName / (GraphBaseName + TEXT(".txt"));
                const FString DotGraphRelativePath = BlueprintFolderName / (GraphBaseName + TEXT(".dot"));
                const FString MmidGraphRelativePath = BlueprintFolderName / (GraphBaseName + TEXT(".mmid"));

                const FString RawGraphText = BuildRawGraphText(Graph);
                const FString StructuredGraphText = BuildStructuredGraphText(BlueprintAsset, Graph);
                const FString DotGraphText = BuildGraphDot(BlueprintAsset, Graph);
                const FString MmidGraphText = BuildGraphMmid(BlueprintAsset, Graph);

                const FString RawGraphPath = BlueprintRawDir / (GraphBaseName + TEXT(".txt"));
                const FString StructuredGraphPath = BlueprintStructuredDir / (GraphBaseName + TEXT(".txt"));
                const FString DotGraphPath = BlueprintDotDir / (GraphBaseName + TEXT(".dot"));
                const FString MmidGraphPath = BlueprintMmidDir / (GraphBaseName + TEXT(".mmid"));

                FFileHelper::SaveStringToFile(RawGraphText, *RawGraphPath, FFileHelper::EEncodingOptions::ForceUTF8);
                FFileHelper::SaveStringToFile(StructuredGraphText, *StructuredGraphPath, FFileHelper::EEncodingOptions::ForceUTF8);
                FFileHelper::SaveStringToFile(DotGraphText, *DotGraphPath, FFileHelper::EEncodingOptions::ForceUTF8);
                FFileHelper::SaveStringToFile(MmidGraphText, *MmidGraphPath, FFileHelper::EEncodingOptions::ForceUTF8);

                RawGraphFiles.Add(Graph->GetFName(), RawGraphRelativePath);
                StructuredGraphFiles.Add(Graph->GetFName(), StructuredGraphRelativePath);
                DotGraphFiles.Add(Graph->GetFName(), DotGraphRelativePath);
                MmidGraphFiles.Add(Graph->GetFName(), MmidGraphRelativePath);

                CombinedRawText += FString::Printf(TEXT("### Graph: %s (%s) ###%s"), *Graph->GetName(), *GetGraphKind(BlueprintAsset, Graph), LINE_TERMINATOR);
                CombinedRawText += RawGraphText.IsEmpty() ? FString(TEXT("# No nodes exported")) : RawGraphText;
                CombinedRawText += LINE_TERMINATOR;
                CombinedRawText += LINE_TERMINATOR;
            }

            const FString CombinedRawPath = Directories.TxtRawRoot / (BlueprintFolderName + TEXT(".txt"));
            const FString StructuredBlueprintPath = Directories.TxtStructuredRoot / (BlueprintFolderName + TEXT(".txt"));
            const FString StructuredBlueprintText = BuildStructuredBlueprintText(BlueprintAsset, StructuredGraphFiles, RawGraphFiles, DotGraphFiles, MmidGraphFiles);
            const FString VariablesPath = Directories.VariablesRoot / (BlueprintFolderName + TEXT(".yaml"));
            const FString VariablesRelativePath = FString::Printf(TEXT("variables/%s.yaml"), *BlueprintFolderName);
            const FString StructuredBlueprintRelativePath = FString::Printf(TEXT("txt/structured/%s.txt"), *BlueprintFolderName);
            const FString ReadmePath = BlueprintReadmeDir / TEXT("README.md");

            FFileHelper::SaveStringToFile(CombinedRawText, *CombinedRawPath, FFileHelper::EEncodingOptions::ForceUTF8);
            FFileHelper::SaveStringToFile(StructuredBlueprintText, *StructuredBlueprintPath, FFileHelper::EEncodingOptions::ForceUTF8);
            FFileHelper::SaveStringToFile(BuildVariablesYaml(BlueprintAsset), *VariablesPath, FFileHelper::EEncodingOptions::ForceUTF8);
            FFileHelper::SaveStringToFile(
                BuildBlueprintReadme(BlueprintAsset, VariablesRelativePath, StructuredBlueprintRelativePath, RawGraphFiles, StructuredGraphFiles, DotGraphFiles, MmidGraphFiles),
                *ReadmePath,
                FFileHelper::EEncodingOptions::ForceUTF8);
        }
        else if (const UUserDefinedEnum* EnumAsset = Cast<UUserDefinedEnum>(AssetObject))
        {
            const FString ExportFilePath = Directories.TxtOtherRoot / (SanitizeFileName(AssetName) + TEXT(".txt"));
            FFileHelper::SaveStringToFile(BuildEnumText(EnumAsset), *ExportFilePath, FFileHelper::EEncodingOptions::ForceUTF8);
        }
        else if (const UUserDefinedStruct* StructAsset = Cast<UUserDefinedStruct>(AssetObject))
        {
            const FString ExportFilePath = Directories.TxtOtherRoot / (SanitizeFileName(AssetName) + TEXT(".txt"));
            FFileHelper::SaveStringToFile(BuildStructText(StructAsset), *ExportFilePath, FFileHelper::EEncodingOptions::ForceUTF8);
        }
        else if (IsSupportedSpecialAssetType(AssetObject))
        {
            const FString ExportRoot = GetSpecialAssetExportDirectory(Directories, AssetObject);
            IFileManager::Get().MakeDirectory(*ExportRoot, true);
            const FString ExportFilePath = ExportRoot / (SanitizeFileName(AssetName) + TEXT(".txt"));
            FFileHelper::SaveStringToFile(BuildReflectedObjectText(AssetObject), *ExportFilePath, FFileHelper::EEncodingOptions::ForceUTF8);
        }
    }

    FNotificationInfo Info(LOCTEXT("ExportComplete", "Asset export completed!"));
    Info.ExpireDuration = 5.0f;
    Info.bUseSuccessFailIcons = true;
    FSlateNotificationManager::Get().AddNotification(Info);
}

void FExportBlueprintToTxtModule::AddMenuExtension(FMenuBuilder& Builder)
{
    Builder.AddMenuEntry(FExportBlueprintToTxtCommands::Get().ExportBlueprintsCommand);
    Builder.AddMenuEntry(FExportBlueprintToTxtCommands::Get().ExportSelectedBlueprintsCommand);
    Builder.AddMenuEntry(FExportBlueprintToTxtCommands::Get().ExportBlueprintsInFolderCommand);
}

void FExportBlueprintToTxtModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
    Builder.AddToolBarButton(FExportBlueprintToTxtCommands::Get().ExportBlueprintsCommand);
    Builder.AddToolBarButton(FExportBlueprintToTxtCommands::Get().ExportSelectedBlueprintsCommand);
    Builder.AddToolBarButton(FExportBlueprintToTxtCommands::Get().ExportBlueprintsInFolderCommand);
}

void FExportBlueprintToTxtModule::RegisterAssetContextMenu()
{
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

    TArray<FContentBrowserMenuExtender_SelectedAssets>& AssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
    AssetMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateRaw(this, &FExportBlueprintToTxtModule::OnExtendContentBrowserAssetSelectionMenu));

    TArray<FContentBrowserMenuExtender_SelectedPaths>& PathMenuExtenderDelegates = ContentBrowserModule.GetAllPathViewContextMenuExtenders();
    PathMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedPaths::CreateRaw(this, &FExportBlueprintToTxtModule::OnExtendContentBrowserPathSelectionMenu));
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FExportBlueprintToTxtModule, ExportBlueprintToTxt)
