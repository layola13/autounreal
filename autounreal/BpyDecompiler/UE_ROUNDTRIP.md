# BpyDecompiler human UE roundtrip

Goal: validate the full source loop without writing back to the original ExportedBlueprints package.

Loop covered by this runner:

1. ExportBpy BPY package, or an existing `*_human` directory
2. `BPY -> human py + .bpy_meta.json` when the source is BPY
3. `human py + .bpy_meta.json -> BPY` under `tmp/`
4. Unreal import into a scratch target asset
5. ExportBpy re-export from that scratch asset
6. Diff prepared BPY against the re-exported BPY

## Command

```bat
Plugins\autounreal\autounreal\BpyDecompiler\run_human_ue_roundtrip.bat ^
  "ExportedBlueprints\bpy\SandboxCharacter_Mover" ^
  "tmp\bpydecompiler_ue_roundtrip_cbp" ^
  CBPUE ^
  /Game/tmp/BpyDecompiler/CBPUE
```

ABP example:

```bat
Plugins\autounreal\autounreal\BpyDecompiler\run_human_ue_roundtrip.bat ^
  "ExportedBlueprints\bpy\SandboxCharacter_Mover_ABP" ^
  "tmp\bpydecompiler_ue_roundtrip_abp" ^
  ABPUE ^
  /Game/tmp/BpyDecompiler/ABPUE
```

Existing human source example:

```bat
Plugins\autounreal\autounreal\BpyDecompiler\run_human_ue_roundtrip.bat ^
  "tmp\some_label\CBP_human" ^
  "tmp\bpydecompiler_ue_roundtrip_from_human" ^
  CBPHUMAN ^
  /Game/tmp/BpyDecompiler/CBPHUMAN ^
  --source-is-human
```

## Outputs

All generated files stay under the supplied `tmp` work directory:

- `<label>_human/` human source package
- `<label>_human_compiled_bpy/` compiled BPY package
- `<label>_ue_roundtrip/report.json` full report
- `<label>_ue_roundtrip/roundtrip.diff.txt` diff when Unreal re-export differs

The original source package is never overwritten.

## Interpreting result

- `success=true` with empty diff means full Unreal import/re-export loop passed.
- `Unreal Python is unavailable` means the script was run outside Unreal; only sidecar-aware BPY source validation ran.
- Use scratch targets under `/Game/tmp/BpyDecompiler/...` to avoid changing production assets.
