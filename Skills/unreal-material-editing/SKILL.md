---
name: unreal-material-editing
description: Edit UE material node graphs and properties via text (ReadGraph/WriteGraph). Use when the user asks to add, remove, or rewire material expression nodes, or change material properties like ShadingModel or BlendMode.
---

# Material Editing

Material editing covers both **material properties** (ShadingModel, BlendMode, etc.) via the `[Properties]` section, and **node graph** (expression nodes and connections) via the `[Material]` section. Both use the same unified API.

**Prerequisite**: UE editor running with UCP plugin enabled.

## API

CDO: `/Script/UnrealClientProtocolEditor.Default__NodeCodeEditingLibrary`

| Function | Params | Description |
|----------|--------|-------------|
| `Outline` | `AssetPath` | Returns available sections (Properties, Material, Composite:Name) |
| `ReadGraph` | `AssetPath`, `Section` | Returns text. Empty Section = all. `"Material"` = main graph only. |
| `WriteGraph` | `AssetPath`, `Section`, `GraphText` | Overwrite section. Auto-recompiles, relayouts, refreshes editor. |

## Material Properties

Use the `[Properties]` section to read/write material-level settings:

```
[Properties]
ShadingModel: MSM_DefaultLit
BlendMode: BLEND_Opaque
TwoSided: true
OpacityMaskClipValue: 0.333
```

Common properties:

| Property | Example Values |
|----------|---------------|
| `ShadingModel` | `MSM_DefaultLit`, `MSM_Unlit`, `MSM_Subsurface`, `MSM_ClearCoat` |
| `BlendMode` | `BLEND_Opaque`, `BLEND_Masked`, `BLEND_Translucent`, `BLEND_Additive` |
| `MaterialDomain` | `MD_Surface`, `MD_DeferredDecal`, `MD_LightFunction`, `MD_PostProcess`, `MD_UI` |
| `TwoSided` | `true`, `false` |

Read with `ReadGraph(AssetPath, "Properties")`, write with `WriteGraph(AssetPath, "Properties", text)`.

## Section Model

| Section | Description |
|---------|-------------|
| `[Properties]` | Material-level properties (ShadingModel, BlendMode, etc.) |
| `[Material]` | Complete main graph — all non-composite nodes, read/write as one unit |
| `[Composite:Name]` | Composite subgraph (physically isolated) |

The `[Material]` section contains the **entire main graph**. Output pins are expressed as graph output connections: `> RGB -> [BaseColor]`.

## Text Format

```
[Material]

N0 TextureSample {Texture:"/Game/Textures/T_Wood_BC"} #ee001122
  > RGB -> [BaseColor]
  > A -> N3.A

N1 ScalarParameter {ParameterName:"Roughness", DefaultValue:0.5} #ee003344
  > -> N3.B

N2 VectorParameter {ParameterName:"EmissiveColor", DefaultValue:{"R":1,"G":0.5,"B":0}} #ee005566
  > -> N4.A

N3 Multiply #ee007788
  > -> [Roughness]

N4 Multiply #ee009900
  > -> [EmissiveColor]

N5 Constant {R:5.0} #ee00aa00
  > -> N4.B

N6 TextureSample {Texture:"/Game/Textures/T_Wood_N"} #ee00bb00
  > -> [Normal]
```

### Nodes

- `N<idx>`: local reference ID
- `<ClassName>`: UMaterialExpression class name without prefix (e.g. `TextureSample`, `ScalarParameter`, `Multiply`)
- `{...}`: non-default properties, single line
- `#<guid>`: preserve for existing nodes, omit for new

### Connections

Output connections from the owning node:

```
  > OutputPin -> N<target>.InputPin     # to another node
  > OutputPin -> [GraphOutput]          # to material output (BaseColor, Roughness, etc.)
  > -> N<target>.InputPin               # single-output node
```

### Graph Outputs

Material output pins are expressed as `[PinName]`:

```
  > RGB -> [BaseColor]
  > -> [Roughness]
  > -> [Normal]
  > -> [EmissiveColor]
  > -> [Opacity]
  > -> [OpacityMask]
  > -> [Metallic]
  > -> [WorldPositionOffset]
```

### Common Expression Classes

| ClassName | Description |
|-----------|-------------|
| `TextureSample` | Sample a texture |
| `TextureObject` | Texture reference |
| `ScalarParameter` | Float parameter |
| `VectorParameter` | Vector parameter |
| `StaticSwitchParameter` | Static bool parameter |
| `Constant` | Float constant |
| `Constant2Vector` | Vector2 constant |
| `Constant3Vector` | Vector3 constant |
| `Constant4Vector` | Vector4 constant |
| `Multiply` | A * B |
| `Add` | A + B |
| `Subtract` | A - B |
| `Divide` | A / B |
| `Lerp` | Linear interpolate |
| `Power` | Base ^ Exponent |
| `Clamp` | Clamp value |
| `OneMinus` | 1 - X |
| `Abs` | Absolute value |
| `TexCoord` | Texture coordinates |
| `Time` | Time value |
| `Panner` | UV panning |
| `Custom` | Custom HLSL code |
| `MaterialFunctionCall` | Call a material function |
| `SetMaterialAttributes` | Set material attributes |

## Custom HLSL

For `MaterialExpressionCustom`, the `Code` property contains HLSL and `InputNames` defines custom inputs:

```
N0 Custom {Code:"float3 result = Input1 * Input2;\nreturn result;", OutputType:CMOT_Float3, InputNames:["A","B"]} #aabb...
```

## Material Instances

Material instance parameter editing uses `SetObjectProperty` from the `unreal-object-operation` skill, not NodeCode. NodeCode is for editing the **parent material's** node graph.

## Workflow

1. **Outline** — see what sections exist
2. **ReadGraph("Properties")** — check current material settings
3. **ReadGraph("Material")** — get the full node graph
4. **Modify** — edit properties and/or nodes
5. **WriteGraph("Properties", text)** — update settings
6. **WriteGraph("Material", text)** — update graph (auto-recompiles)

## Key Rules

1. **Preserve GUIDs** on existing nodes. Losing GUIDs causes unreliable node matching.
2. **`[Material]` is the complete main graph** — no per-output-pin splitting.
3. **ReadGraph before WriteGraph** — always read first.
4. All operations support **Undo** (Ctrl+Z).
5. **Incremental diff** — only changed nodes/connections are modified.

## Error Handling

- Check `diff` object in response for changes applied.
- Unknown expression class: `"Unknown expression class: ..."`.
- Pin not found: `"Input 'X' not found on N0 (ClassName). Available: [...]"`.
