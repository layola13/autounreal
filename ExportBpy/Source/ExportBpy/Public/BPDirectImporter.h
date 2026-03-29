// Copyright sonygodx@gmail.com. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BPDirectImporter.generated.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
struct FEdGraphPinType;

/**
 * Imports a Python DSL directory back into an Unreal Blueprint asset.
 *
 * The importer:
 *   1. Reads __bp__.bp.py via UE Python executor → records calls → builds JSON
 *   2. Reads graph scripts (evt_*, fn_*, macro_*, tl_*) → JSON node/edge lists
 *   3. Creates/updates the target Blueprint using BlueprintEditorUtils APIs
 *   4. Compiles the Blueprint
 */
UCLASS()
class EXPORTBPY_API UBPDirectImporter : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Import a Python DSL directory into a Blueprint asset.
	 *
	 * @param JsonData        Pre-built JSON string describing the blueprint
	 * @param TargetAssetPath Unreal asset path where the BP will be created/updated
	 * @param OutError        Receives an error message on failure
	 * @return                true on success
	 */
	UFUNCTION(BlueprintCallable, Category = "ExportBpy")
	static bool ImportBlueprintFromJson(
		const FString& JsonData,
		const FString& TargetAssetPath,
		bool bCompileBlueprint,
		FString& OutError);

	/**
	 * 将独立资产（InputAction / InputMappingContext / Chooser / PoseSearchDatabase 等）
	 * 的属性从一个扁平 JSON 属性字典字符串写回到已有资产。
	 *
	 * @param AssetPath       Unreal 软对象路径，资产必须已存在
	 * @param PropertiesJson  JSON 对象字符串，key = 属性名，value = 属性值
	 * @param OutError        失败时的错误信息
	 * @return                true 表示成功
	 */
	UFUNCTION(BlueprintCallable, Category = "ExportBpy")
	static bool ImportStandaloneAssetFromJson(
		const FString& AssetPath,
		const FString& PropertiesJson,
		FString& OutError);

private:
	// ── Asset management ─────────────────────────────────────────────────────

	static UBlueprint* CreateBlueprintAsset(
		const FString& AssetPath,
		UClass* ParentClass,
		FString& OutError);

	// ── Variable / graph creation ─────────────────────────────────────────────

	static void CreateVariable(
		UBlueprint* BP,
		const TSharedPtr<FJsonObject>& VarJson);

	static bool CreateGraph(
		UBlueprint* BP,
		const TSharedPtr<FJsonObject>& GraphJson,
		FString& OutError);

	// ── Node creation ─────────────────────────────────────────────────────────

	static UEdGraphNode* CreateNode(
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		FString& OutError);

	static UEdGraphNode* CreateEventNode(UEdGraph* Graph, const FString& EventName);

	static UEdGraphNode* CreateCallFunctionNode(
		UEdGraph* Graph,
		const FString& FunctionRef,
		const FString& NodeClassName,
		const TSharedPtr<class FJsonObject>& NodeJson,
		FString& OutError);

	static UEdGraphNode* CreateMessageNode(
		UEdGraph* Graph,
		const FString& FunctionRef,
		const TSharedPtr<class FJsonObject>& NodeJson,
		FString& OutError);

	static UEdGraphNode* CreateMacroInstanceNode(
		UEdGraph* Graph,
		const FString& MacroGraphPath,
		const FString& MacroName,
		FString& OutError);

	static UEdGraphNode* CreateVariableNode(
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		bool bIsGet);

	static UEdGraphNode* CreateBranchNode(UEdGraph* Graph);

	// ── Connection / type helpers ──────────────────────────────────────────────

	static void ConnectPins(
		UEdGraphNode* SrcNode,
		const FString& SrcPinName,
		const FString& SrcPinFullName,
		const FString& SrcPinId,
		UEdGraphNode* DstNode,
		const FString& DstPinName,
		const FString& DstPinFullName,
		const FString& DstPinId);

	static void ParsePinType(const FString& TypeStr, FEdGraphPinType& OutType);

	// ── Compile ───────────────────────────────────────────────────────────────

	static  void CompileBlueprint(UBlueprint* BP);
	static bool SaveBlueprint(UBlueprint* BP, FString& OutError);
};
