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

N0 MaterialExpressionTextureSample {Texture:"/Game/Textures/T_Wood_BC"} #ee001122
  > RGB -> [BaseColor]
  > A -> N3.A

N1 MaterialExpressionScalarParameter {ParameterName:"Roughness", DefaultValue:0.5} #ee003344
  > -> N3.B

N2 MaterialExpressionVectorParameter {ParameterName:"EmissiveColor", DefaultValue:{"R":1,"G":0.5,"B":0}} #ee005566
  > -> N4.A

N3 MaterialExpressionMultiply #ee007788
  > -> [Roughness]

N4 MaterialExpressionMultiply #ee009900
  > -> [EmissiveColor]

N5 MaterialExpressionConstant {R:5.0} #ee00aa00
  > -> N4.B

N6 MaterialExpressionTextureSample {Texture:"/Game/Textures/T_Wood_N"} #ee00bb00
  > -> [Normal]

N7 MaterialExpressionTime
  > -> N8.A

N8 MaterialExpressionSine {Period:0}
  > -> N9.B

N9 MaterialExpressionMultiply
  > -> [WorldPositionOffset]

N10 MaterialExpressionWorldPosition
  > -> N9.A
```

Note how `Time` (N7) and `WorldPosition` (N10) — source-only nodes with no inputs — both have `> ->` lines. Without these lines they would be deleted as orphans.

### Nodes

- `N<idx>`: local reference ID — **`<idx>` must be a plain integer** (e.g. `N0`, `N1`, `N42`). Letter suffixes like `N3b` or `N40x` are **not valid** and will cause parse errors or misrouted connections.
- `<ClassName>`: UMaterialExpression class name (e.g. `MaterialExpressionMultiply`, `MaterialExpressionConstant3Vector`)
- `{...}`: non-default properties, single line
- `#<guid>`: preserve for existing nodes, omit for new

### Connections

**Connections are declared on the source (output) node only.** Each `>` line under a node declares where that node's output goes. There is no "reverse" declaration — if a node has no `>` lines, it has no outgoing connections.

```
  > OutputPin -> N<target>.InputPin     # named output to named input
  > OutputPin -> [GraphOutput]          # named output to material output
  > -> N<target>.InputPin               # single-output node to named input
  > -> N<target>                        # single-output to single-input (both omitted)
```

**CRITICAL — Orphan cleanup:** After WriteGraph, any node not reachable from the material output pins is **automatically deleted** as an orphan. This means:

- **Every node must be part of a connected chain that reaches a `[GraphOutput]`.** If you create a node but forget to write its `> ->` output connections, it will be silently deleted.
- **Source-only nodes** (nodes with no inputs, only outputs — e.g. `Time`, `ScreenPosition`, `ViewSize`, `TexCoord`, `WorldPosition`, `CameraPositionWS`, `VertexColor`, constants) are especially prone to this. They **must** have `> ->` lines connecting their output to downstream nodes.

### Multi-Output Nodes

Some nodes have multiple named outputs. Use the output name before `->`:

| Node | Outputs |
|------|---------|
| `MaterialExpressionScreenPosition` | `ViewportUV` (float2, index 0), `PixelPosition` (float2, index 1) |
| `MaterialExpressionTextureSample` | `RGB`, `R`, `G`, `B`, `A`, `RGBA` |
| `MaterialExpressionWorldPosition` | (single output, omit name) |
| `MaterialExpressionViewSize` | (single output float2, omit name) |

Example — ScreenPosition with named output:
```
N0 MaterialExpressionScreenPosition
  > ViewportUV -> N1.A
```

### Common Input Pin Names

| Node Type | Input Pins |
|-----------|-----------|
| Single-input nodes (`Sine`, `Cosine`, `Tangent`, `Abs`, `OneMinus`, `Frac`, `Floor`, `Ceil`, `Saturate`, `SquareRoot`, `Length`, `Normalize`) | `Input` (can be omitted) |
| `ComponentMask` | `Input` (can be omitted) |
| Binary math (`Multiply`, `Add`, `Subtract`, `Divide`) | `A`, `B` |
| `Power` | `Base`, `Exponent` |
| `DotProduct`, `CrossProduct`, `Distance` | `A`, `B` |
| `AppendVector` | `A`, `B` |
| `LinearInterpolate` | `A`, `B`, `Alpha` |
| `Clamp` | `Input`, `Min`, `Max` |
| `If` | `A`, `B`, `AGreaterThanB`, `AEqualsB`, `ALessThanB` |
| `Arctangent2Fast` | `Y`, `X` |

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

**Source-only nodes** (no inputs — must always have `> ->` output lines or they will be deleted as orphans):

| ClassName | Output | Description |
|-----------|--------|-------------|
| `MaterialExpressionConstant` | float1 | Float constant (property: `R`) |
| `MaterialExpressionConstant2Vector` | float2 | Vector2 constant |
| `MaterialExpressionConstant3Vector` | float3 | Vector3 constant (property: `Constant:(R=,G=,B=,A=)`) |
| `MaterialExpressionConstant4Vector` | float4 | Vector4 constant |
| `MaterialExpressionScalarParameter` | float1 | Float parameter |
| `MaterialExpressionVectorParameter` | float3 | Vector parameter |
| `MaterialExpressionTexCoord` | float2 | Texture coordinates |
| `MaterialExpressionTime` | float1 | Time value |
| `MaterialExpressionScreenPosition` | float2 × 2 | Outputs: `ViewportUV`, `PixelPosition` |
| `MaterialExpressionViewSize` | float2 | Viewport resolution in pixels |
| `MaterialExpressionWorldPosition` | float3 | World position |
| `MaterialExpressionVertexColor` | float4 | Vertex color (outputs: R, G, B, A, RGB) |
| `MaterialExpressionCameraPositionWS` | float3 | Camera position |
| `MaterialExpressionTextureObject` | Texture | Texture reference |

**Texture sampling:**

| ClassName | Description |
|-----------|-------------|
| `MaterialExpressionTextureSample` | Sample a texture (outputs: RGB, R, G, B, A, RGBA) |
| `MaterialExpressionTextureSampleParameter2D` | Texture parameter |

**Math — binary:**

| ClassName | Inputs | Unconnected-default properties | Description |
|-----------|--------|-------------------------------|-------------|
| `MaterialExpressionMultiply` | `A`, `B` | `ConstA`, `ConstB` (float) | A * B |
| `MaterialExpressionAdd` | `A`, `B` | `ConstA`, `ConstB` (float) | A + B |
| `MaterialExpressionSubtract` | `A`, `B` | `ConstA`, `ConstB` (float) | A - B |
| `MaterialExpressionDivide` | `A`, `B` | `ConstA`, `ConstB` (float) | A / B |
| `MaterialExpressionPower` | `Base`, `Exponent` | `ConstExponent` (float) | Base ^ Exponent |
| `MaterialExpressionDotProduct` | `A`, `B` | *(none)* | Dot(A, B) → float1 |
| `MaterialExpressionCrossProduct` | `A`, `B` | *(none)* | Cross(A, B) → float3 |
| `MaterialExpressionDistance` | `A`, `B` | *(none)* | Distance(A, B) → float1 |
| `MaterialExpressionAppendVector` | `A`, `B` | *(none — both inputs required)* | Append(A, B) — combine components (e.g. float1+float1→float2) |

`ConstA`/`ConstB` are **only available on the four basic arithmetic nodes** (Multiply, Add, Subtract, Divide). They provide a scalar fallback when the corresponding input pin is unconnected. All other binary nodes require explicit input connections — use a `MaterialExpressionConstant` node to supply fixed values.

**Math — unary** (input: `Input`, can be omitted in connection):

| ClassName | Description |
|-----------|-------------|
| `MaterialExpressionOneMinus` | 1 - X |
| `MaterialExpressionAbs` | Absolute value |
| `MaterialExpressionNormalize` | Normalize → same dimension |
| `MaterialExpressionLength` | Length → float1 |
| `MaterialExpressionSquareRoot` | sqrt |
| `MaterialExpressionFrac` | Fractional part |
| `MaterialExpressionFloor` | Floor |
| `MaterialExpressionCeil` | Ceiling |
| `MaterialExpressionSaturate` | Clamp to 0-1 |

**Trig** (input: `Input`; `Period` property: default 1 maps 0-1 to full cycle; **set `Period:0` for raw radians**):

| ClassName | Description |
|-----------|-------------|
| `MaterialExpressionSine` | sin |
| `MaterialExpressionCosine` | cos |
| `MaterialExpressionTangent` | tan |
| `MaterialExpressionArctangent2Fast` | atan2(Y, X) — inputs: `Y`, `X` |

**Interpolation & logic:**

| ClassName | Description |
|-----------|-------------|
| `MaterialExpressionLinearInterpolate` | Lerp(A, B, Alpha) |
| `MaterialExpressionClamp` | Clamp(Input, Min, Max) |
| `MaterialExpressionIf` | If(A, B, AGreaterThanB, AEqualsB, ALessThanB) |
| `MaterialExpressionStaticSwitchParameter` | Static bool parameter |

**Channel operations:**

| ClassName | Description |
|-----------|-------------|
| `MaterialExpressionComponentMask` | Mask channels (properties: R, G, B, A booleans) |

**Other:**

| ClassName | Description |
|-----------|-------------|
| `MaterialExpressionPanner` | UV panning |
| `MaterialExpressionTransform` | Transform vector between spaces |
| `MaterialExpressionCustom` | Custom HLSL code |
| `MaterialExpressionMaterialFunctionCall` | Call a material function |
| `MaterialExpressionSetMaterialAttributes` | Set material attributes |

## Custom HLSL

For `MaterialExpressionCustom`, the `Code` property contains HLSL and `InputNames` defines custom inputs:

```
N0 MaterialExpressionCustom {Code:"float3 result = Input1 * Input2;\nreturn result;", OutputType:CMOT_Float3, InputNames:["A","B"]} #aabb...
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
6. **Every node needs output connections** — nodes without `> ->` lines that aren't reachable from material outputs will be deleted as orphans. This is especially important for source-only nodes (constants, Time, ScreenPosition, ViewSize, etc.).
7. **Connections are output-side only** — you declare where a node's output goes by writing `> ->` lines under it. There is no way to declare an incoming connection on the target side.

## Error Handling

- Check `diff` object in response for changes applied.
- Unknown expression class: `"Unknown expression class: ..."`.
- Pin not found: `"Input 'X' not found on N0 (ClassName). Available: [...]"`.
