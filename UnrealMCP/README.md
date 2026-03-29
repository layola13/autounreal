# UnrealMCP 插件详细说明手册

UnrealMCP 是一个用于 Unreal Engine 的插件，提供了丰富的 MCP（Model Context Protocol）接口，允许外部程序通过标准化的方式与 Unreal Engine 进行交互。本文档列出了所有公开可调用的方法。

## 蓝图相关命令 (Blueprint Commands)

以下命令通过 `FEpicUnrealMCPBlueprintCommands` 类处理：

| 命令名称 | 描述 |
|----------|------|
| CreateBlueprint | 创建一个新的 Blueprint 类。 |
| CreateBlueprintInterface | 创建一个新的 Blueprint 接口。 |
| AddComponentToBlueprint | 向 Blueprint 添加组件。 |
| SetPhysicsProperties | 设置组件的物理属性。 |
| CompileBlueprint | 编译 Blueprint。 |
| SpawnBlueprintActor | 从 Blueprint 生成一个单一的 Actor。 |
| SpawnBlueprintActors | 从 Blueprint 生成多个 Actor。 |
| SetStaticMeshProperties | 设置 StaticMeshComponent 的静态网格属性。 |
| SetSkeletalMeshProperties | 设置 SkeletalMeshComponent 的骨骼网格属性。 |
| CopyValue | 从一个 Blueprint 端点复制属性值到另一个端点。 |
| FindAndReplace | 在 Blueprint 中查找并替换图表引用。 |
| RunPython | 在 Unreal 编辑器中执行 Python 代码。 |
| ExecuteUnrealPython | 通过 Unreal 包装器执行 Python。 |
| ExportBlueprint | 导出 Blueprint 为文本。 |
| GetBlueprintMeta | 获取 Blueprint 的 .meta 文本内容。 |
| ExportBlueprintMeta | 导出 Blueprint 的 .meta 文本到磁盘。 |
| FetchBlueprintBestPractices | 获取蓝图编辑最佳实践。 |
| BpAgent | 高级蓝图助手，用于创建和编辑蓝图图表。 |
| GetBlueprintPython | 获取 Blueprint 的 Python 导出文本。 |
| ExportBlueprintPython | 导出 Blueprint 的 Python 文件。 |
| GetBlueprintBpy | 获取 Blueprint 的单文件 bpy 导出文本。 |
| ExportBlueprintBpy | 导出 Blueprint 的单文件 bpy。 |
| ImportBlueprintPython | 从 Python 导出内容导入 Blueprint。 |
| ExportBlueprintFunctions | 导出 Blueprint 的函数到目录。 |
| ImportBlueprintFunctions | 从目录导入 Blueprint 的函数。 |
| CompileLiveCoding | 触发 Live Coding 编译。 |
| GetLiveCodingLog | 获取最新的 Live Coding 编译日志。 |
| TriggerLiveCoding | 触发 Live Coding 编译或切换 Live Coding 开关。 |
| SetMeshMaterialColor | 设置网格组件的材质颜色。 |
| SetGameModeDefaultSpawnClass | 设置 GameMode Blueprint 的默认生成/pawn 类。 |
| GetAvailableMaterials | 获取项目中可用于对象的材质列表。 |
| ApplyMaterialToActor | 将特定材质应用到关卡中的 Actor。 |
| ApplyMaterialToBlueprint | 将特定材质应用到 Blueprint 中的组件。 |
| GetActorMaterialInfo | 获取 Actor 当前应用的材质信息。 |
| GetBlueprintMaterialInfo | 获取 Blueprint 内部当前应用的材质信息。 |
| ReadBlueprintContent | 读取和分析 Blueprint 的完整内容（包括事件图、函数、变量、组件和接口）。 |
| ReadBlueprintContentFast | 读取快速轻量级的 Blueprint 摘要。 |
| AnalyzeBlueprintGraph | 分析 Blueprint 中的特定图表（EventGraph、函数等），提供节点、连接和执行流的详细信息。 |
| AnalyzeBlueprintGraphFast | 使用桥的快速路径分析 Blueprint 图表。 |
| GetBlueprintProperties | 获取 Blueprint 文件的属性。 |
| GetBlueprintComponentProperties | 获取 Blueprint 文件中组件的属性。 |
| GetBlueprintPropertiesSpecifiers | 获取 Blueprint 中属性的指定说明符。 |
| GetBlueprintVariableDetails | 获取 Blueprint 变量的详细信息（包括类型、默认值、元数据和使用情况）。 |
| GetBlueprintFunctionDetails | 获取 Blueprint 函数的详细信息（包括参数、返回值、局部变量和函数图内容）。 |
| GetBlueprintClassInfo | 获取 Blueprint 的高级类信息。 |
| EditBlueprint | 编辑 Blueprint 文件（结构化属性、函数、组件和接口）。 |

## 编辑器相关命令 (Editor Commands)

以下命令通过 `FEpicUnrealMCPEditorCommands` 类处理：

| 命令名称 | 描述 |
|----------|------|
| GetActorsInLevel | 获取当前关卡中的所有演员列表。 |
| FindActorsByName | 通过名称模式查找演员。 |
| SpawnActor | 在当前关卡中生成一个演员。 |
| DeleteActor | 按名称删除演员。 |
| SetActorTransform | 设置演员的变换（位置、旋转、缩放）。 |
| CreateInputActionKey | 创建输入操作键（用于增强输入系统）。 |
| CreateInputActions | 创建新的 InputAction 资产用于增强输入系统。 |
| AddInputActionToMappingContext | 将输入操作添加到输入映射上下文。 |
| CreateGameplayTag | 通过在 INI 文件中添加条目来创建新的游戏玩法标签。 |
| AddOrReplaceRowsInDataTable | 向数据表添加或替换行。 |
| RemoveRowsFromDataTable | 从数据表删除行。 |
| GetDataAssetTypes | 获取项目中的所有数据资产类型。 |
| GetDataAssetTypeInfo | 获取特定数据资产类型的详细信息。 |
| EditEnumeration | 编辑 Blueprint 枚举资产。 |
| EditStructure | 编辑 Blueprint 结构资产。 |
| CreateTextFile | 在允许的项目路径中创建新的文本文件。 |
| EditTextFile | 编辑允许的项目路径中的现有文本文件。 |
| CreateNewTodoList | 创建或替换整个待办事项列表。 |
| GetTodoList | 获取当前待办事项列表。 |
| EditTodoList | 使用紧凑的批处理字符串编辑待办事项。 |
| SearchAssetsByNameFromFab | 在 FAB 市场（fab.com）中搜索虚幻引擎资产。 |
| GetAssetsByNameFromFab | 从 FAB 获取资产。 |
| AddFabAssetToProject | 打开 FAB 资产页面，让用户将其添加到虚幻引擎项目中。 |
| SaveProject | 保存编辑器中的脏项目包。 |
| SpawnBlueprintActor | 从 Blueprint 生成 Actor（与蓝图命令中的相同功能）。 |

## 检查器相关命令 (Inspector Commands)

以下命令通过 `FEpicUnrealMCPInspectorCommands` 类处理（提供只读检查功能）：

| 命令名称 | 描述 |
|----------|------|
| GetHeadlessStatus | 获取无头 Unreal Engine 实例的当前状态。 |
| LaunchUnrealProject | 启动 Unreal Engine 以无头模式运行当前项目。 |
| ShutdownHeadless | 关闭无头 Unreal Engine 实例。 |
| GetUnrealContext | 获取用户当前在虚幻编辑器中选择的上下文。 |
| QueryUnrealProjectAssets | 通过关键字查询虚幻项目资产，获取浅层信息。 |
| Quicksearch | 执行快速搜索桥接查询（基于文件路径的快速全局搜索）。 |
| Grep | 搜索代件文件（使用 Unreal 桥接 ripgrep 包装器）。 |
| GetCodeExamples | 搜索虚幻代码示例。 |
| GetAssetMeta | 获取资产的元数据。 |
| GetAssetGraph | 获取资产的详细图信息。 |
| GetAssetStructs | 获取资产引用的额外结构元数据。 |
| GetBlueprintMaterialProperties | 获取蓝图的材质属性。 |
| ReviewBlueprint | 对蓝图进行 AI 驱动的代码审查并提出改进建议。 |
| CreateAndValidateBlueprintPlan | 生成蓝图图的实施计划（仅用于规划）。 |
| GetTextFileContents | 读取文本文件内容。 |
| GetAvailableActorsInLevel | 列出指定关卡中可用的演员及其关联信息。 |
| GetEnums | 获取项目中的枚举及其条目。 |
| GetGameplayTags | 获取项目中的游戏玩法标签。 |
| ReadDatatableKeys | 读取数据表的行键。 |
| ReadDatatableValues | 读取数据表的行值。 |
| GetRecentGeneratedImages | 获取本会话中最近生成或编辑的图像文件路径。 |
| FetchAnimationSkill | 获取角色替换管道的动画技能参考。 |
| CreateOrEditPlan | 创建或编辑版本化的计划文件（用于计划模式）。 |
| FetchGasBestPractices | 获取本地 Gameplay Ability System 最佳实践。 |
| FetchUiBestPractices | 获取本地 UMG/UI 最佳实践。 |
| ImportUnderstanding | 导入特定的理解笔记（如 AssetCreation、AssetRegistry 等），需要通过 CommandType 参数指定要导入的理解类型。 |
| InspectCurrentLevel | 通过虚幻桥的安全检查路径检查当前关卡。 |

## 使用说明

所有命令都通过标准的 MCP 接口调用。具体的调用方式取决于您使用的 MCP 客户端。通常，您需要：

1. 确保 UnrealMCP 插件已在您的 Unreal Engine 项目中启用。
2. 通过 MCP 客户端连接到 UnrealMCP 服务器。
3. 发送命令请求，指定命令类型（如上表中的命令名称）和所需的参数（以 JSON 格式）。
4. 服务器将处理命令并返回结果（也以 JSON 格式）。

例如，要创建一个名为 "MyNewBlueprint" 的 Blueprint，父类为 "Actor"，您可能会发送类似以下的请求（具体格式取决于 MCP 实现）：

```json
{
  "commandType": "CreateBlueprint",
  "params": {
    "name": "MyNewBlueprint",
    "parent_class": "Actor"
  }
}
```

对于需要额外上下文的命令（如 `ImportUnderstanding`），您可能需要提供额外的参数：

```json
{
  "commandType": "ImportUnderstanding",
  "params": {
    "CommandType": "AssetCreation"
  }
}
```

请参考插件源码或 MCP 服务器文档以获取每个命令的确切参数结构。

## 注意事项

- 部分命令可能需要特定的上下文（例如，某些蓝图命令需要打开的蓝图资产）。
- 某些操作（如编译、保存）可能会触发 Unreal Engine 的自动保存或编译过程。
- 建议在进行重大修改前备份您的项目。
- 插件遵循虚幻引擎的最佳实践和编码标准。

## 支持与反馈

如有问题或建议，请通过项目的issue提交反馈。