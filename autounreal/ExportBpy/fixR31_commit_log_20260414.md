# fixR31 提交日志（2026-04-14）

## 目的
修复 ExportBpy 导入后 `K2Node_PromotableOperator` 函数漂移，导致 AC_TraversalLogic 跨栏计算失真（`Add_DoubleDouble` 漂移成 `Add_VectorVector`）。

## 代码改动
- 文件：`autounreal/ExportBpy/Source/ExportBpy/Private/BPDirectImporter.cpp`
- 新增：`RestorePromotableOperatorBindingsAfterConnections_ImportBpy(...)`
- 在 `PopulateGraph(...)` 连接与 pin id 回放后，针对以下节点做二次函数回绑：
  - `K2Node_PromotableOperator`
  - `K2Node_CommutativeAssociativeBinaryOperator`
- 回绑后重放该节点的 `defaults` 与 `pin_ids`，避免 `SetFromFunction` 重建 pin 后再次漂移。

## 验证
1. 编译通过：`Build.bat GameAnimationSampleEditor Win64 Development`
2. 自动导入导出回归：
   - 日志：`Saved/Logs/import_export_diag_r31_promofix_20260414_144727.stdout.log`
   - `R31_IMPORT` 两个任务均 `success=true`
   - 无 `Could not find a root node for the graph AnimGraph`
3. GUID 对比通过（关键 3 个 GUID 保持 Double 算子）：
   - `21A313E74ECF345881D695A0E01EF5C1 => Add_DoubleDouble`
   - `2D1BE5F04CC49B225D3A6DBEDE484870 => Add_DoubleDouble_2`
   - `2C7F4536451C956EFBFE379EC7FE82FF => Add_DoubleDouble_4`
4. 命令行冒烟通过：
   - 日志：`Saved/Logs/smoke_crossbar_runtimeapply_20260414_145656.stdout.log`
   - `GM_Sandbox` 正常加载，未出现断言/致命错误。

## 说明
- `Widgets/*` 的编译报错是项目既有问题，不属于本次修复范围。
