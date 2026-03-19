# NodeCode Architecture

NodeCode is a text-based intermediate representation (IR) for Unreal Engine node graphs. It converts visual node graphs into a concise, human-readable text format that AI agents can understand, reason about, and modify — then writes the changes back to the live graph.

## Data Flow

```
Read:   UE Graph Object ──→ BuildIR ──→ FNodeCodeGraphIR ──→ IRToText ──→ Text (to AI)
Write:  Text (from AI) ──→ ParseText ──→ FNodeCodeGraphIR ──→ DiffAndApply ──→ UE Graph Object
```

### Read Path

```
┌─────────────────┐     ┌──────────────────────────────────────────────┐     ┌──────────────┐
│  UE Graph Object│     │              BuildIR                         │     │  IRToText    │
│  (Material /    │────→│  1. Resolve scope (find target graph/nodes)  │────→│  (shared)    │────→ Text
│   Blueprint)    │     │  2. Collect nodes (skip comments, reroutes)  │     │              │
│                 │     │  3. Serialize node properties                │     │  Topo-sort   │
│                 │     │  4. Trace connections (resolve reroutes)     │     │  nodes by    │
│                 │     │  5. Build FNodeCodeGraphIR                   │     │  depth       │
└─────────────────┘     └──────────────────────────────────────────────┘     └──────────────┘
```

### Write Path (DiffAndApply)

```
Text ──→ ParseText ──→ NewIR
                         │
                         ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│                           DiffAndApply                                           │
│                                                                                  │
│  ┌─────────────────────────────────────────────────────────────────────────────┐ │
│  │ Phase 0: Build OldIR from live graph (same BuildIR as read path)           │ │
│  └─────────────────────────────────────────────────────────────────────────────┘ │
│                              │                                                   │
│                              ▼                                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐ │
│  │ Phase 1: MatchNodes — pair OldIR nodes with NewIR nodes                    │ │
│  │   Pass 1: by GUID (stable identity)                                        │ │
│  │   Pass 2: by ClassName + key property (ParameterName, Function, etc.)      │ │
│  │   Pass 3: by ClassName alone (reuse orphaned nodes)                        │ │
│  └─────────────────────────────────────────────────────────────────────────────┘ │
│                              │                                                   │
│                              ▼                                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐ │
│  │ Phase 2: Delete — remove unmatched old nodes from the live graph           │ │
│  │   (protected nodes like FunctionEntry/FunctionResult are preserved)         │ │
│  └─────────────────────────────────────────────────────────────────────────────┘ │
│                              │                                                   │
│                              ▼                                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐ │
│  │ Phase 3: Create — instantiate new nodes for unmatched NewIR entries        │ │
│  │   Build NewIndex → LiveNode map (matched + newly created)                  │ │
│  └─────────────────────────────────────────────────────────────────────────────┘ │
│                              │                                                   │
│                              ▼                                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐ │
│  │ Phase 4: Update Properties — apply property/pin-default changes on         │ │
│  │   matched nodes (skip if properties identical)                             │ │
│  └─────────────────────────────────────────────────────────────────────────────┘ │
│                              │                                                   │
│                              ▼                                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐ │
│  │ Phase 5+6: Incremental Connection Diff                                     │ │
│  │   1. Resolve NewIR links to live pin/input pointers → DesiredLinks set     │ │
│  │   2. For each live link between in-scope nodes:                            │ │
│  │      - If NOT in DesiredLinks → break it (stale)                           │ │
│  │      - If in DesiredLinks → keep it (unchanged)                            │ │
│  │   3. For each DesiredLink not already connected → create it                │ │
│  │   External links (to out-of-scope nodes) are never touched.               │ │
│  │   Material: ConnectExpression / direct FExpressionInput assignment         │ │
│  │   Blueprint: Schema->TryCreateConnection / MakeLinkTo                     │ │
│  └─────────────────────────────────────────────────────────────────────────────┘ │
│                              │                                                   │
│                              ▼                                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐ │
│  │ Phase 7: Post-Apply (graph-type specific)                                  │ │
│  │   Material: RebuildGraph, remove orphans, relayout, recompile, refresh UI  │ │
│  │   Blueprint: MarkBlueprintAsStructurallyModified (triggers recompile)      │ │
│  └─────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                  │
└──────────────────────────────────────────────────────────────────────────────────┘
```

## IR Structures

Defined in `Private/NodeCode/NodeCodeTypes.h`:

| Struct | Fields | Description |
|--------|--------|-------------|
| `FNodeCodeNodeIR` | Index, ClassName, Guid, Properties, SourceObject | A single node |
| `FNodeCodeLinkIR` | FromNodeIndex, FromOutputName, ToNodeIndex, ToInputName, bToGraphOutput | A connection |
| `FNodeCodeGraphIR` | Nodes, Links, ScopeName | A complete graph scope |
| `FNodeCodeDiffResult` | NodesAdded, NodesRemoved, NodesModified, LinksAdded, LinksRemoved | Write operation result |

### Node IR

- `Index`: local reference ID (N0, N1, ...), 0-based
- `ClassName`: identifies the node type (graph-specific encoding)
- `Guid`: 32-hex GUID for stable identity across read/write cycles
- `Properties`: key-value map of non-default properties
- `SourceObject`: live UObject pointer (only valid during BuildIR, nullptr after ParseText)

### Link IR

- `FromNodeIndex` / `ToNodeIndex`: reference by node Index
- `FromOutputName` / `ToInputName`: pin names
- `bToGraphOutput`: true when the link targets a graph-level output (e.g. material BaseColor pin)

## Text Format

```
=== scope: <ScopeName> ===

=== nodes ===
N<idx> <ClassName> {Key:Value, Key:Value} #<guid>
N<idx> <ClassName> #<guid>

=== links ===
N<from>[.OutputPin] -> N<to>.InputPin
N<from>[.OutputPin] -> [GraphOutput]
```

### Sections

- `=== scope: ... ===` — optional, identifies which sub-scope of the graph
- `=== nodes ===` — node definitions, sorted by topological depth
- `=== links ===` — connections between nodes

### Node Line

```
N<index> <ClassName> {Key:Value, ...} #<guid>
```

- Braces `{}` omitted if no non-default properties
- GUID `#...` preserved for existing nodes, omitted for new nodes
- Property values: `"string"`, `0.5`, `true`, `(R=1.0,G=0.5,B=0.0,A=1.0)`, `"/Game/Path"`

### Link Line

```
N<from>.OutputName -> N<to>.InputName
N<from> -> N<to>.InputName
N<from>.OutputName -> [GraphOutputName]
```

- Output name omitted when the node has a single output
- `[...]` syntax for graph-level outputs (material properties, etc.)

## Shared Utilities

| File | Description |
|------|-------------|
| `NodeCodeTypes.h` | IR struct definitions |
| `NodeCodeTextFormat.h/.cpp` | `IRToText`, `ParseText`, `DiffResultToJson` |
| `NodeCodePropertyUtils.h/.cpp` | `FormatPropertyValue`, `ShouldSkipProperty` |
| `NodeCodeClassCache.h/.cpp` | Generic class name cache (parameterized by base class) |

## Diff/Apply Flow

The write path follows a consistent 8-phase pattern across all graph types:

| Phase | Action | Details |
|-------|--------|---------|
| 0 | **Build OldIR** | Serialize the live graph into IR (same `BuildIR` as read path) |
| 1 | **Match nodes** | Pair old/new by GUID → key property → class name |
| 2 | **Delete** | Remove unmatched old nodes (protected nodes preserved) |
| 3 | **Create** | Instantiate unmatched new nodes, build index→node map |
| 4 | **Update properties** | Apply property/pin-default changes on matched nodes |
| 5 | **Remove stale connections** | Incremental diff: break only links that exist in live graph but not in NewIR (between in-scope nodes only; external links preserved) |
| 6 | **Create new connections** | Incremental diff: create only links that exist in NewIR but not in live graph |
| 7 | **Post-apply** | Graph-specific cleanup: recompile, relayout, refresh UI |

## Supported Graph Types

### Material Graph

**Files:** `Private/Material/MaterialGraphSerializer.h/.cpp`, `MaterialGraphDiffer.h/.cpp`, `MaterialExpressionClassCache.h/.cpp`

**API:** `UMaterialGraphEditingLibrary` (CDO: `/Script/UnrealClientProtocolEditor.Default__MaterialGraphEditingLibrary`)

| Function | Params | Description |
|----------|--------|-------------|
| `ListScopes` | AssetPath | Material property outputs + Composite subgraphs |
| `ReadGraph` | AssetPath, ScopeName | Serialize material graph to text |
| `WriteGraph` | AssetPath, ScopeName, GraphText | Apply text changes, returns diff JSON |
| `Relayout` | AssetPath | Force auto-layout |

**Scope model:** Scopes are material output pins (BaseColor, Roughness, ...) or `Composite:<name>` subgraphs. Empty scope = all connected nodes.

**Node ClassName:** UMaterialExpression class names (e.g. `MaterialExpressionScalarParameter`)

**Special features:**
- Reroute node tracing (skips `UMaterialExpressionRerouteBase`)
- Material output links (`bToGraphOutput = true`, `[BaseColor]`)
- Custom/Switch/SetMaterialAttributes dynamic pin handling
- Composite subgraph scoping

### Blueprint Graph

**Files:** `Private/Blueprint/BlueprintGraphSerializer.h/.cpp`, `BlueprintGraphDiffer.h/.cpp`

**API:** `UBlueprintGraphEditingLibrary` (CDO: `/Script/UnrealClientProtocolEditor.Default__BlueprintGraphEditingLibrary`)

| Function | Params | Description |
|----------|--------|-------------|
| `ListScopes` | AssetPath | EventGraph pages + Function:Name + Macro:Name |
| `ReadGraph` | AssetPath, ScopeName | Serialize blueprint graph to text |
| `WriteGraph` | AssetPath, ScopeName, GraphText | Apply text changes, returns diff JSON |

**Scope model:** Each UEdGraph is a scope. Naming convention:
- `EventGraph` (or graph name for UbergraphPages)
- `Function:<FunctionName>`
- `Macro:<MacroName>`

**Name encoding:** Blueprint names (functions, variables, events, pins) may contain spaces. In the text format, **spaces are encoded as underscores** (`_`). When writing back, the system uses fuzzy matching (space/underscore equivalence) to resolve names against the live Blueprint. For example, a variable named `My Variable` becomes `My_Variable` in the text.

**Node ClassName encoding:** Blueprint nodes use semantic class names:
- `CallFunction:<ClassName>.<FuncName>` or `CallFunction:<FuncName>` (self context)
- `VariableGet:<VarName>`, `VariableSet:<VarName>`
- `CustomEvent:<EventName>`, `Event:<EventName>`
- `FunctionEntry`, `FunctionResult`
- Raw UK2Node class name for other node types

**Pin defaults:** Serialized as `pin.<PinName>` in the properties block. Only non-default, unconnected input pins are included. Pin names follow the same space-to-underscore encoding.

**Connection model:** All connections are node-to-node (`bToGraphOutput = false`). Pin names use the encoded form (underscores for spaces). Exec flow pins use their actual names (typically `execute` / `then`).

**Special features:**
- Knot (reroute) node tracing
- Comment node skipping
- FunctionEntry/FunctionResult protection (cannot be deleted)
- Schema-based connection validation
- Auto-recompile after write

## Adding Support for a New Graph Type

To add support for a new graph type (e.g. Niagara, Animation Blueprint):

### 1. Create Serializer

```
Private/<Domain>/<Domain>GraphSerializer.h/.cpp
```

Implement:
- `BuildIR()` — collect nodes from the graph, populate `FNodeCodeGraphIR`
- `ListScopes()` — return available sub-scopes
- `Serialize()` — call `BuildIR()` then `FNodeCodeTextFormat::IRToText()`

Key decisions:
- How to collect nodes (which nodes to include/skip)
- How to encode node class names (semantic vs raw)
- How to serialize node properties (special handling for domain-specific nodes)
- How to enumerate and name pins
- Whether the graph has "graph outputs" (`bToGraphOutput`)

### 2. Create Differ

```
Private/<Domain>/<Domain>GraphDiffer.h/.cpp
```

Implement:
- `Apply()` — parse text, call `DiffAndApply()`
- `DiffAndApply()` — follow the 6-phase pattern
- `CreateNodeFromIR()` — create nodes from class name + properties
- `ApplyPropertyChanges()` — set properties on existing nodes

Key decisions:
- How to create nodes (graph-specific APIs)
- How to establish connections (pin-based vs expression-based)
- Post-apply cleanup (recompile, relayout, refresh)

### 3. Create API Library

```
Public/<Domain>/<Domain>GraphEditingLibrary.h
Private/<Domain>/<Domain>GraphEditingLibrary.cpp
```

Standard `UBlueprintFunctionLibrary` with `ReadGraph`, `WriteGraph`, `ListScopes`.

### 4. Update Build.cs

Add required module dependencies.

### 5. Create Skill

Add `Skills/unreal-<domain>-editing/SKILL.md` documenting the API, text format specifics, and common node classes.
