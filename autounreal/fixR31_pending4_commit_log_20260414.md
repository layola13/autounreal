# 四文件改动提交日志（2026-04-14）

## 提交范围
本次提交包含以下 4 个已修改文件，并补充本日志文件：

1. `autounreal/ExportBpy/Content/Python/bp_importer.py`
2. `autounreal/ExportBpy/Source/ExportBpy/Private/BPDirectExporter.cpp`
3. `autounreal/UnrealMCP/Source/UnrealMCP/Private/Commands/EpicUnrealMCPBlueprintCommands.cpp`
4. `autounreal/UnrealMCP/Python/unreal_mcp_advanced.log`

## 1) bp_importer.py

主要改进：
- 导入后 pin 修复从单阶段扩展为 `pre-compile` + `post-compile` 两阶段，减少编译后 pin 重建导致的默认值回退。
- 新增图对象识别与遍历辅助：
  - `_looks_like_graph_object`
  - `_unwrap_graph_candidate`
  - `_iter_blueprint_graphs`
- 增强 live pin 定位与修复：
  - `_resolve_live_pin`（支持 pin aliases / pin_ids）
  - `_set_live_pin_default`（精确定位 pin 后再写默认值）
- 增加 ForceBlend 调用位点判定：
  - `_is_force_blend_callsite_node`
- `_repair_imported_blueprint_pin_defaults` 增强统计与日志输出，便于追踪“跳过/失败样本”。

## 2) BPDirectExporter.cpp

主要改进：
- 增强图序列化元数据：导出 `graph_guid`、`graph_outer`。
- 改进 knot/reroute 导出：
  - `K2Node_Knot` 不再被连接导出流程跳过。
  - `TranslateOutputPinRef_ExportBpy` / `TranslateInputPinRef_ExportBpy` 保留 knot 原始 pin 语义，避免误映射为 exec 别名。
- `SerializeConnections` 调整为包含 knot 节点 uid，保证 reroute 链可重建。
- 扩展 Node 属性导出：
  - `NodePurityOverride`
  - `BoundGraphJson`（包含 composite/state/transition 等子图序列化信息）

## 3) EpicUnrealMCPBlueprintCommands.cpp

主要改进：
- 新增 Blueprint 图全量可达遍历：
  - `GatherReachableBlueprintGraphsForLookup`
  - `GatherAllBlueprintGraphsForLookup`
- 图查找策略改进（`FindBlueprintGraphByNameOrPath`）：
  - 优先 path 精确匹配
  - 支持 path 后缀匹配
  - 同名图冲突时按节点数选择更“完整”的图（适配 nested animation graphs）
- `GetAllBlueprintGraphNames` 同步使用可达图枚举，提升错误提示和工具稳定性。

## 4) unreal_mcp_advanced.log

主要改动：
- 追加了最近一轮 UnrealMCP 调试与命令执行日志（大体量 append）。
- 用于追踪导入/导出与 Live Coding 执行链路。

## 说明
- 本次为“把当前 4 个本地改动统一入库并推送”的整理提交。
- 其中 `.log` 文件为运行记录，属于追踪类资产，体积较大。
