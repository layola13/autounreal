// Copyright sonygodx@gmail.com. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Math/Vector2D.h"
#include "BPDirectExporter.generated.h"

class UBlueprint;
class UEdGraph;
class UEdGraphPin;
class UK2Node;

// ─── Internal node info struct ────────────────────────────────────────────────

struct FNodeInfo
{
	FString NodeType;       // K2Node class name, e.g. "K2Node_CallFunction"
	FString FunctionName;   // member / function / event name
	FString ClassName;      // parent class name (for CallFunction nodes)
	bool bIsCallFunctionLike = false; // any UK2Node_CallFunction-derived node
	FString TargetType;     // target class / enum / struct for typed nodes
	FString VarName;        // assigned Python variable name, e.g. "PrintString"
	FString NodeGuid;       // original UE node guid
	FVector2D Position;     // canvas position
	TArray<TPair<FString, FString>> CustomParams; // custom event param name -> type
	TMap<FString, FString> DefaultValues; // pin name → default value string
	TMap<FString, FString> PinAliases;    // logical pin name → raw UE pin name
	TMap<FString, FString> PinIds;        // logical pin name → original pin guid
	TMap<FString, FString> NodeProps;     // extra node properties not encoded in DSL
};

// ─── Exporter class ───────────────────────────────────────────────────────────

/**
 * 导出 Unreal Blueprint 到 Python DSL .bp.py 格式。
 *
 * 两个主入口：
 *   ExportBlueprintToPy   — 写文件到磁盘（右键菜单调用）
 *   ReadBlueprintToJson   — 返回 JSON 字符串（Python bp_exporter.py 调用）
 */
UCLASS()
class EXPORTBPY_API UBPDirectExporter : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 导出 Blueprint 到 .bp.py 文件（磁盘写出）。
	 *
	 * @param BlueprintPath  Unreal 软对象路径
	 * @param OutputDir      输出目录的绝对路径
	 * @param OutError       失败时的错误信息
	 * @return               true 表示成功
	 */
	UFUNCTION(BlueprintCallable, Category = "ExportBpy")
	static bool ExportBlueprintToPy(
		const FString& BlueprintPath,
		const FString& OutputDir,
		FString& OutError);

	/**
	 * 读取 Blueprint 并返回单文件、LLM 可读的 Python bpy 文本。
	 *
	 * @param BlueprintPath  Unreal 软对象路径
	 * @param OutBpyText     返回生成的 bpy 文本
	 * @param OutError       失败时的错误信息
	 * @return               true 表示成功
	 */
	UFUNCTION(BlueprintCallable, Category = "ExportBpy")
	static bool ReadBlueprintToBpyText(
		const FString& BlueprintPath,
		FString& OutBpyText,
		FString& OutError);

	/**
	 * 导出 Blueprint 到单文件 bpy。
	 *
	 * @param BlueprintPath  Unreal 软对象路径
	 * @param OutputPath     输出文件的绝对路径或目录
	 * @param OutError       失败时的错误信息
	 * @return               true 表示成功
	 */
	UFUNCTION(BlueprintCallable, Category = "ExportBpy")
	static bool ExportBlueprintToBpyFile(
		const FString& BlueprintPath,
		const FString& OutputPath,
		FString& OutError);

	/**
	 * 读取 Blueprint 并返回 JSON 字符串（供 Python bp_exporter.py 使用）。
	 *
	 * @param BlueprintPath  Unreal 软对象路径
	 * @return               JSON 字符串，失败时返回空字符串
	 */
	UFUNCTION(BlueprintCallable, Category = "ExportBpy")
	static FString ReadBlueprintToJson(const FString& BlueprintPath);

	/**
	 * 导出独立资产（InputAction / InputMappingContext / Chooser / PoseSearchDatabase 等）
	 * 的非默认属性到 __asset__.meta.py 文件。
	 *
	 * @param AssetPath   Unreal 软对象路径，例如 /Game/Input/Actions/IA_Jump.IA_Jump
	 * @param OutputDir   输出目录（文件名固定为 __asset__.meta.py）
	 * @param OutError    失败时的错误信息
	 * @return            true 表示成功
	 */
	UFUNCTION(BlueprintCallable, Category = "ExportBpy")
	static bool ExportStandaloneAssetToPy(
		const FString& AssetPath,
		const FString& OutputDir,
		FString& OutError);

private:
	// ── Main file generation ──────────────────────────────────────────────────

	static bool GenerateMainFile(
		UBlueprint* BP,
		const FString& OutDir,
		const TArray<FString>& GraphModules,
		FString& OutError);

	static FString GenerateVariablesSection(UBlueprint* BP);
	static FString GenerateComponentsSection(UBlueprint* BP);
	static FString GenerateInterfacesSection(UBlueprint* BP);
	static FString GenerateDispatchersSection(UBlueprint* BP);

	// ── JSON serialisation (for ReadBlueprintToJson) ──────────────────────────

	static TSharedPtr<FJsonObject> SerializeBlueprintToJson(UBlueprint* BP);
	static TSharedPtr<FJsonObject> SerializeGraph(UEdGraph* Graph);
	static TSharedPtr<FJsonObject> SerializeNode(UK2Node* Node);
	static TArray<TSharedPtr<FJsonValue>> SerializeConnections(UEdGraph* Graph);

	// ── Graph file generation ─────────────────────────────────────────────────

	static bool GenerateGraphFile(
		UBlueprint* BP,
		UEdGraph* Graph,
		const FString& Prefix,
		const FString& OutDir,
		FString& OutModuleName,
		FString& OutError);

	static TArray<TArray<UK2Node*>> SplitEventGraphByEntryPoints(UEdGraph* Graph);

	// ── Node serialisation helpers ────────────────────────────────────────────

	static FString NodeToCtorLine(const FNodeInfo& Info);
	static TArray<FString> NodeToDefaultValueLines(const FNodeInfo& Info);
	static FString PinConnectionToLine(
		const FString& SrcVar, const FString& SrcPin,
		const FString& DstVar, const FString& DstPin);

	static void AssignReadableNames(TArray<FNodeInfo>& Nodes);
	static TArray<UK2Node*> TopologicalSort(const TArray<UK2Node*>& Nodes);
	static UEdGraphPin* ResolveRerouteChain(UEdGraphPin* Pin);
};
