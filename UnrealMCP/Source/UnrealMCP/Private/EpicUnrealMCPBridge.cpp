#include "EpicUnrealMCPBridge.h"
#include "MCPServerRunnable.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
// Add Blueprint related includes
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
// UE5.5 correct includes
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
// Blueprint Graph specific includes
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "GameFramework/InputSettings.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
// Include our new command handler classes
#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPBlueprintGraphCommands.h"
#include "Commands/EpicUnrealMCPInspectorCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

// Default settings
#define MCP_SERVER_HOST "127.0.0.1"
#define MCP_SERVER_PORT 55557

namespace
{
enum class EMCPCommandGroup : uint8
{
    Unknown,
    Editor,
    Blueprint,
    BlueprintGraph,
    Inspector,
    Deferred,
};

FString CanonicalizeCommand(const FString& CommandType)
{
    static const TMap<FString, FString> AliasMap = {
        {TEXT("search_fab"), TEXT("searchassetsbynamefromfab")},
        {TEXT("add_blueprint_node_to_strand"), TEXT("add_blueprint_node_to_strand")},
        {TEXT("connect_blueprint_nodes"), TEXT("connect_blueprint_nodes")},
        {TEXT("disconnect_blueprint_nodes"), TEXT("disconnect_blueprint_nodes")},
        {TEXT("remove_blueprint_nodes"), TEXT("remove_blueprint_nodes")},
        {TEXT("set_node_pins_defaults"), TEXT("set_node_pins_defaults")},
        {TEXT("spawn_blueprint_actors"), TEXT("spawn_blueprint_actors")}
    };

    if (const FString* Alias = AliasMap.Find(CommandType))
    {
        return *Alias;
    }

    return CommandType;
}

EMCPCommandGroup GetCommandGroup(const FString& CanonicalCommand)
{
    static const TSet<FString> EditorCommands = {
        TEXT("get_actors_in_level"),
        TEXT("find_actors_by_name"),
        TEXT("spawn_actor"),
        TEXT("delete_actor"),
        TEXT("set_actor_transform"),
        TEXT("create_input_action_key"),
        TEXT("add_input_action_key"),
        TEXT("create_input_actions"),
        TEXT("add_input_action_to_mapping_context"),
        TEXT("create_gameplay_tag"),
        TEXT("add_or_replace_rows_in_data_table"),
        TEXT("remove_rows_from_data_table"),
        TEXT("get_data_asset_types"),
        TEXT("get_data_asset_type_info"),
        TEXT("edit_enumeration"),
        TEXT("edit_structure"),
        TEXT("create_text_file"),
        TEXT("edit_text_file"),
        TEXT("create_new_todo_list"),
        TEXT("get_todo_list"),
        TEXT("edit_todo_list"),
        TEXT("searchassetsbynamefromfab"),
        TEXT("getassetsbynamefromfab"),
        TEXT("add_fab_asset_to_project"),
        TEXT("save_project"),
        TEXT("save_all"),
        TEXT("spawn_blueprint_actor")
    };

    static const TSet<FString> BlueprintCommands = {
        TEXT("execute_unreal_python"),
        TEXT("bp_agent"),
        TEXT("fetch_blueprint_best_practices"),
        TEXT("create_blueprint"),
        TEXT("create_blueprint_interface"),
        TEXT("add_component_to_blueprint"),
        TEXT("set_physics_properties"),
        TEXT("compile_blueprint"),
        TEXT("set_static_mesh_properties"),
        TEXT("set_skeletal_mesh_properties"),
        TEXT("copy_value"),
        TEXT("find_and_replace"),
        TEXT("run_python"),
        TEXT("exportblueprint"),
        TEXT("export_blueprint"),
        TEXT("get_blueprint_meta"),
        TEXT("export_asset_meta"),
        TEXT("export_blueprint_meta"),
        TEXT("get_asset_py"),
        TEXT("get_blueprint_py"),
        TEXT("export_asset_py"),
        TEXT("export_blueprint_py"),
        TEXT("get_asset_bpy"),
        TEXT("get_blueprint_bpy"),
        TEXT("export_asset_bpy"),
        TEXT("export_blueprint_bpy"),
        TEXT("import_asset_py"),
        TEXT("import_blueprint_py"),
        TEXT("import_blueprint_from_bpy"),
        TEXT("edit_blueprint_by_bpy"),
        TEXT("export_blueprint_functions"),
        TEXT("split_blueprint_functions"),
        TEXT("import_blueprint_functions"),
        TEXT("reimport_blueprint_functions"),
        TEXT("compile_live_coding"),
        TEXT("compile_cpp"),
        TEXT("compile_unreal_code"),
        TEXT("get_live_coding_log"),
        TEXT("trigger_live_coding"),
        TEXT("set_gamemode_default_spawn_class"),
        TEXT("set_gamemode_default_pawn_class"),
        TEXT("set_mesh_material_color"),
        TEXT("get_available_materials"),
        TEXT("apply_material_to_actor"),
        TEXT("apply_material_to_blueprint"),
        TEXT("get_actor_material_info"),
        TEXT("get_blueprint_material_info"),
        TEXT("read_blueprint_content"),
        TEXT("read_blueprint_content_fast"),
        TEXT("analyze_blueprint_graph"),
        TEXT("analyze_blueprint_graph_fast"),
        TEXT("get_blueprint_properties"),
        TEXT("get_blueprint_component_properties"),
        TEXT("get_blueprint_properties_specifiers"),
        TEXT("get_blueprint_variable_details"),
        TEXT("get_blueprint_function_details"),
        TEXT("get_blueprint_class_info"),
        TEXT("edit_blueprint"),
        TEXT("spawn_blueprint_actors")
    };

    static const TSet<FString> BlueprintGraphCommands = {
        TEXT("add_blueprint_node"),
        TEXT("connect_nodes"),
        TEXT("create_variable"),
        TEXT("set_blueprint_variable_properties"),
        TEXT("add_event_node"),
        TEXT("delete_node"),
        TEXT("set_node_property"),
        TEXT("add_nodes_batch"),
        TEXT("connect_nodes_batch"),
        TEXT("disconnect_nodes_batch"),
        TEXT("remove_nodes_batch"),
        TEXT("set_node_pin_defaults_batch"),
        TEXT("create_function"),
        TEXT("add_function_input"),
        TEXT("add_function_output"),
        TEXT("delete_function"),
        TEXT("rename_function"),
        TEXT("format_graph"),
        TEXT("add_blueprint_node_to_strand"),
        TEXT("connect_blueprint_nodes"),
        TEXT("disconnect_blueprint_nodes"),
        TEXT("remove_blueprint_nodes"),
        TEXT("set_node_pins_defaults")
    };

    static const TSet<FString> InspectorCommands = {
        TEXT("get_headless_status"),
        TEXT("launch_unreal_project"),
        TEXT("get_unreal_context"),
        TEXT("query_unreal_project_assets"),
        TEXT("quicksearch"),
        TEXT("grep"),
        TEXT("get_code_examples"),
        TEXT("get_asset_meta"),
        TEXT("get_asset_graph"),
        TEXT("get_asset_structs"),
        TEXT("get_blueprint_material_properties"),
        TEXT("review_blueprint"),
        TEXT("create_and_validate_blueprint_plan"),
        TEXT("get_text_file_contents"),
        TEXT("get_available_actors_in_level"),
        TEXT("get_enums"),
        TEXT("get_gameplay_tags"),
        TEXT("read_datatable_keys"),
        TEXT("read_datatable_values"),
        TEXT("shutdown_headless"),
        TEXT("get_recent_generated_images"),
        TEXT("fetch_animation_skill"),
        TEXT("create_or_edit_plan"),
        TEXT("fetch_gas_best_practices"),
        TEXT("fetch_ui_best_practices"),
        TEXT("import_AssetCreation_understanding"),
        TEXT("import_AssetRegistry_understanding"),
        TEXT("import_AssetType_Level_understanding"),
        TEXT("import_AssetValidation_understanding"),
        TEXT("import_Color_understanding"),
        TEXT("import_FileSystem_understanding"),
        TEXT("import_Logs_understanding"),
        TEXT("import_Subsystems_understanding"),
        TEXT("inspect_current_level")
    };

    static const TSet<FString> DeferredCommands = {
        TEXT("behavior_tree_agent"),
        TEXT("material_agent"),
        TEXT("audio_agent"),
        TEXT("python_agent"),
        TEXT("generate_audio"),
        TEXT("generate_images"),
        TEXT("edit_images"),
        TEXT("generate_model_from_text"),
        TEXT("generate_model_from_image"),
        TEXT("rig_model_from_mesh"),
        TEXT("generate_cpp_file"),
        TEXT("edit_cpp_file"),
        TEXT("take_editor_screenshot"),
        TEXT("fetch_material_best_practices"),
        TEXT("fetch_audio_skill"),
        TEXT("search_unreal_python_api"),
        TEXT("inspect_current_level_legacy_unimplemented")
    };

    if (EditorCommands.Contains(CanonicalCommand))
    {
        return EMCPCommandGroup::Editor;
    }
    if (BlueprintCommands.Contains(CanonicalCommand))
    {
        return EMCPCommandGroup::Blueprint;
    }
    if (BlueprintGraphCommands.Contains(CanonicalCommand))
    {
        return EMCPCommandGroup::BlueprintGraph;
    }
    if (InspectorCommands.Contains(CanonicalCommand))
    {
        return EMCPCommandGroup::Inspector;
    }
    if (DeferredCommands.Contains(CanonicalCommand))
    {
        return EMCPCommandGroup::Deferred;
    }

    return EMCPCommandGroup::Unknown;
}

bool ShouldExecuteOffGameThread(const FString& CanonicalCommand)
{
    return CanonicalCommand == TEXT("searchassetsbynamefromfab") ||
           CanonicalCommand == TEXT("getassetsbynamefromfab");
}
}

UEpicUnrealMCPBridge::UEpicUnrealMCPBridge()
{
    EditorCommands = MakeShared<FEpicUnrealMCPEditorCommands>();
    BlueprintCommands = MakeShared<FEpicUnrealMCPBlueprintCommands>();
    BlueprintGraphCommands = MakeShared<FEpicUnrealMCPBlueprintGraphCommands>();
    InspectorCommands = MakeShared<FEpicUnrealMCPInspectorCommands>(EditorCommands, BlueprintCommands);
}

UEpicUnrealMCPBridge::~UEpicUnrealMCPBridge()
{
    EditorCommands.Reset();
    BlueprintCommands.Reset();
    BlueprintGraphCommands.Reset();
    InspectorCommands.Reset();
}

// Initialize subsystem
void UEpicUnrealMCPBridge::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Initializing"));
    
    bIsRunning = false;
    ListenerSocket = nullptr;
    ConnectionSocket = nullptr;
    ServerThread = nullptr;
    Port = MCP_SERVER_PORT;
    FIPv4Address::Parse(MCP_SERVER_HOST, ServerAddress);

    // Start the server automatically
    StartServer();
}

// Clean up resources when subsystem is destroyed
void UEpicUnrealMCPBridge::Deinitialize()
{
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Shutting down"));
    StopServer();
}

// Start the MCP server
void UEpicUnrealMCPBridge::StartServer()
{
    if (bIsRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("EpicUnrealMCPBridge: Server is already running"));
        return;
    }

    // Create socket subsystem
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to get socket subsystem"));
        return;
    }

    // Create listener socket
    TSharedPtr<FSocket> NewListenerSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealMCPListener"), false));
    if (!NewListenerSocket.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to create listener socket"));
        return;
    }

    // Allow address reuse for quick restarts
    NewListenerSocket->SetReuseAddr(true);
    NewListenerSocket->SetNonBlocking(true);

    // Bind to address
    FIPv4Endpoint Endpoint(ServerAddress, Port);
    if (!NewListenerSocket->Bind(*Endpoint.ToInternetAddr()))
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to bind listener socket to %s:%d"), *ServerAddress.ToString(), Port);
        return;
    }

    // Start listening
    if (!NewListenerSocket->Listen(5))
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to start listening"));
        return;
    }

    ListenerSocket = NewListenerSocket;
    bIsRunning = true;
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Server started on %s:%d"), *ServerAddress.ToString(), Port);

    // Start server thread
    ServerThread = FRunnableThread::Create(
        new FMCPServerRunnable(this, ListenerSocket),
        TEXT("UnrealMCPServerThread"),
        0, TPri_Normal
    );

    if (!ServerThread)
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to create server thread"));
        StopServer();
        return;
    }
}

// Stop the MCP server
void UEpicUnrealMCPBridge::StopServer()
{
    if (!bIsRunning)
    {
        return;
    }

    bIsRunning = false;

    // Clean up thread
    if (ServerThread)
    {
        ServerThread->Kill(true);
        delete ServerThread;
        ServerThread = nullptr;
    }

    // Close sockets
    if (ConnectionSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ConnectionSocket.Get());
        ConnectionSocket.Reset();
    }

    if (ListenerSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket.Get());
        ListenerSocket.Reset();
    }

    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Server stopped"));
}

// Execute a command received from a client
FString UEpicUnrealMCPBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Executing command: %s"), *CommandType);
    const FString CanonicalCommand = CanonicalizeCommand(CommandType);
    EMCPCommandGroup CommandGroup = GetCommandGroup(CanonicalCommand);
    if (CanonicalCommand == TEXT("get_asset_meta") && (!Params.IsValid() || !Params->HasField(TEXT("parts"))))
    {
        CommandGroup = EMCPCommandGroup::Blueprint;
    }

    auto ExecuteRoutedCommand = [this, CommandType, CanonicalCommand, Params, CommandGroup]() -> FString
    {
        TSharedPtr<FJsonObject> ResponseJson = MakeShareable(new FJsonObject);

        try
        {
            TSharedPtr<FJsonObject> ResultJson;

            if (CanonicalCommand == TEXT("ping"))
            {
                ResultJson = MakeShareable(new FJsonObject);
                ResultJson->SetStringField(TEXT("message"), TEXT("pong"));
            }
            else if (CommandGroup == EMCPCommandGroup::Editor)
            {
                ResultJson = EditorCommands->HandleCommand(CanonicalCommand, Params);
            }
            else if (CommandGroup == EMCPCommandGroup::Blueprint)
            {
                ResultJson = BlueprintCommands->HandleCommand(
                    CanonicalCommand == TEXT("get_asset_meta") ? CommandType : CanonicalCommand,
                    Params);
            }
            else if (CommandGroup == EMCPCommandGroup::BlueprintGraph)
            {
                ResultJson = BlueprintGraphCommands->HandleCommand(CanonicalCommand, Params);
            }
            else if (CommandGroup == EMCPCommandGroup::Inspector)
            {
                ResultJson = InspectorCommands->HandleCommand(CanonicalCommand, Params);
            }
            else if (CommandGroup == EMCPCommandGroup::Deferred)
            {
                ResultJson = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Command not supported yet in UnrealMCP: %s"), *CanonicalCommand));
                ResultJson->SetStringField(TEXT("error_code"), TEXT("not_supported_yet"));
                ResultJson->SetStringField(TEXT("command"), CanonicalCommand);
            }
            else
            {
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown command: %s"), *CommandType));

                FString ResultString;
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
                FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
                return ResultString;
            }

            // Check if the result contains an error
            bool bSuccess = true;
            FString ErrorMessage;

            if (ResultJson->HasField(TEXT("success")))
            {
                bSuccess = ResultJson->GetBoolField(TEXT("success"));
                if (!bSuccess && ResultJson->HasField(TEXT("error")))
                {
                    ErrorMessage = ResultJson->GetStringField(TEXT("error"));
                }
            }
            
            if (bSuccess)
            {
                // Set success status and include the result
                ResponseJson->SetStringField(TEXT("status"), TEXT("success"));
                ResponseJson->SetObjectField(TEXT("result"), ResultJson);
            }
            else
            {
                // Set error status and include the error message
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetStringField(TEXT("error"), ErrorMessage);
                // Preserve result details for better diagnostics
                if (ResultJson.IsValid())
                {
                    ResponseJson->SetObjectField(TEXT("result"), ResultJson);
                }
            }
        }
        catch (const std::exception& e)
        {
            ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
            ResponseJson->SetStringField(TEXT("error"), UTF8_TO_TCHAR(e.what()));
        }
        
        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
        return ResultString;
    };

    bool bExecuteOffGameThread = ShouldExecuteOffGameThread(CanonicalCommand);
    if (CanonicalCommand == TEXT("execute_unreal_python"))
    {
        bExecuteOffGameThread = true;
    }
    else if (CanonicalCommand == TEXT("run_python"))
    {
        bool bUnsafeInProcess = false;
        if (Params.IsValid())
        {
            Params->TryGetBoolField(TEXT("unsafe_inprocess"), bUnsafeInProcess);
        }
        bExecuteOffGameThread = !bUnsafeInProcess;
    }

    if (bExecuteOffGameThread)
    {
        return ExecuteRoutedCommand();
    }

    // Create a promise to wait for the result
    TPromise<FString> Promise;
    TFuture<FString> Future = Promise.GetFuture();

    // Queue execution on Game Thread
    AsyncTask(ENamedThreads::GameThread, [ExecuteRoutedCommand, Promise = MoveTemp(Promise)]() mutable
    {
        Promise.SetValue(ExecuteRoutedCommand());
    });

    return Future.Get();
}

FString UEpicUnrealMCPBridge::ExecuteCommandJson(const FString& CommandType, const FString& ParamsJson)
{
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();

    const FString TrimmedParams = ParamsJson.TrimStartAndEnd();
    if (!TrimmedParams.IsEmpty())
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TrimmedParams);
        if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
        {
            TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
            ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
            ResponseJson->SetStringField(TEXT("error"), TEXT("Failed to parse ParamsJson as a JSON object"));

            FString ResultString;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
            FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
            return ResultString;
        }
    }

    return ExecuteCommand(CommandType, Params);
}
