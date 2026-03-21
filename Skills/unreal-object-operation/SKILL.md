---
name: unreal-object-operation
description: Read/write any UObject property (material settings, actor transforms, component configs, etc.), inspect object metadata, find instances, undo/redo. This is the foundational skill for modifying ANY object in Unreal Engine — use it whenever you need to get or set a property on any UObject.
---

# Unreal Object Operations

Operate on UObjects through UCP's `call` command. This skill covers two function libraries and relevant engine APIs (one at **runtime** and one **editor-only**).

**Prerequisite**: The `unreal-client-protocol` skill must be available and the UE editor must be running with the UCP plugin enabled.

This document merges **(A)** the project-local skill (warnings against invented APIs, full `{"type":"call",...}` payloads, `UKismet*` notes, **GameplayTag** workflow) with **(B)** the upstream **UnrealClientProtocol** repository skill (extra `UObjectOperationLibrary` functions, **Keyword**-based undo/redo, **ForceReplaceReferences**, **Safe Undo/Redo Pattern**, and minimal `object`/`function` JSON). Where both show the same example, **both** forms are kept when they differ.

---

## Custom Function Libraries

### UObjectOperationLibrary (Runtime)

**CDO Path**: `/Script/UnrealClientProtocol.Default__ObjectOperationLibrary`

| Function | Params | Description |
|----------|--------|-------------|
| `GetObjectProperty` | `ObjectPath`, `PropertyName` | Read a UPROPERTY, returns JSON with property value |
| `SetObjectProperty` | `ObjectPath`, `PropertyName`, `JsonValue` | Write a UPROPERTY (supports Undo in editor). JsonValue is a JSON string. |
| `DescribeObject` | `ObjectPath` | Returns class info, all properties (with current values), and all functions |
| `DescribeObjectProperty` | `ObjectPath`, `PropertyName` | Returns detailed property metadata and current value |
| `DescribeObjectFunction` | `ObjectPath`, `FunctionName` | Returns full function signature with parameter types |
| `FindObjectInstances` | `ClassName`, `Limit` (default 100) | Find UObject instances by class path |
| `FindDerivedClasses` | `ClassName`, `bRecursive` (default true), `Limit` (default 500) | Find all subclasses of a class |
| `ListComponents` | `ObjectPath` | List all components attached to an actor |
| `FindObjectsByOuter` | `OuterPath`, `ClassName` (default ""), `Limit` (default 100) | Find objects owned by a given outer object |

**Important:** Only these functions exist on `UObjectOperationLibrary`. Do **not** assume names like `CallFunctionByName`, `SetBoolPropertyByName`, `Modify`, `CallFunction`, `Invoke`, `Execute`, `GetObjectPath`, or `GetClass` — they will return **Function not found**.

#### Examples

**Read a property (project-local — full UCP `call`):**

```json
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"GetObjectProperty","params":{"ObjectPath":"/Game/Maps/Main.Main:PersistentLevel.StaticMeshActor_0.StaticMeshComponent0","PropertyName":"RelativeLocation"}}
```

**Read a property (upstream repository — minimal payload; add `"type":"call"` for UCP.py):**

```json
{"object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"GetObjectProperty","params":{"ObjectPath":"/Game/Maps/Main.Main:PersistentLevel.StaticMeshActor_0.StaticMeshComponent0","PropertyName":"RelativeLocation"}}
```

**Write a property (project-local):**

```json
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"SetObjectProperty","params":{"ObjectPath":"/Game/Maps/Main.Main:PersistentLevel.StaticMeshActor_0.StaticMeshComponent0","PropertyName":"RelativeLocation","JsonValue":"{\"X\":100,\"Y\":200,\"Z\":0}"}}
```

**Write a property (upstream repository):**

```json
{"object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"SetObjectProperty","params":{"ObjectPath":"/Game/Maps/Main.Main:PersistentLevel.StaticMeshActor_0.StaticMeshComponent0","PropertyName":"RelativeLocation","JsonValue":"{\"X\":100,\"Y\":200,\"Z\":0}"}}
```

**Find instances (project-local):**

```json
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"FindObjectInstances","params":{"ClassName":"/Script/Engine.StaticMeshActor","Limit":50}}
```

**Find instances (upstream repository):**

```json
{"object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"FindObjectInstances","params":{"ClassName":"/Script/Engine.StaticMeshActor","Limit":50}}
```

**Describe an unfamiliar object (project-local):**

```json
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"DescribeObject","params":{"ObjectPath":"/Game/Maps/Main.Main:PersistentLevel.BP_CustomActor_C_0"}}
```

**Describe an unfamiliar object (upstream repository):**

```json
{"object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"DescribeObject","params":{"ObjectPath":"/Game/Maps/Main.Main:PersistentLevel.BP_CustomActor_C_0"}}
```

**List components on an actor (upstream repository):**

```json
{"object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"ListComponents","params":{"ObjectPath":"/Game/Maps/Main.Main:PersistentLevel.StaticMeshActor_0"}}
```

**Full UCP:**

```json
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"ListComponents","params":{"ObjectPath":"/Game/Maps/Main.Main:PersistentLevel.StaticMeshActor_0"}}
```

**Find objects by outer (upstream repository):**

```json
{"object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"FindObjectsByOuter","params":{"OuterPath":"/Game/Maps/Main.Main:PersistentLevel","ClassName":"/Script/Engine.StaticMeshActor","Limit":50}}
```

**Full UCP:**

```json
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"FindObjectsByOuter","params":{"OuterPath":"/Game/Maps/Main.Main:PersistentLevel","ClassName":"/Script/Engine.StaticMeshActor","Limit":50}}
```

### UObjectEditorOperationLibrary (Editor)

**CDO Path**: `/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary`

| Function | Params | Description |
|----------|--------|-------------|
| `UndoTransaction` | `Keyword` (optional, default `""`) | Undo the last editor transaction. If `Keyword` is set, only undoes if the top transaction contains the keyword (see **Safe Undo/Redo Pattern**). |
| `RedoTransaction` | `Keyword` (optional, default `""`) | Redo the last undone transaction. If `Keyword` is set, only redoes if the top transaction contains the keyword. |
| `GetTransactionState` | (none) | Returns undo/redo stack state: canUndo, canRedo, undoTitle, redoTitle, undoCount, queueLength |
| `ForceReplaceReferences` | `ReplacementObjectPath`, `ObjectsToReplacePaths` | Redirect all references from the listed objects to the replacement object. `ObjectsToReplacePaths` is an array of strings. |

#### Examples

**Batch: transaction state then undo (project-local — no `Keyword`):**

```json
[
  {"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"GetTransactionState"},
  {"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"UndoTransaction"}
]
```

**Get transaction state (upstream repository):**

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"GetTransactionState"}
```

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"GetTransactionState"}
```

**Undo last transaction — unconditional (upstream repository):**

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"UndoTransaction"}
```

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"UndoTransaction"}
```

**Safe undo — only if top transaction matches keyword (upstream repository):**

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"UndoTransaction","params":{"Keyword":"UCP-A1B2C3D4"}}
```

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"UndoTransaction","params":{"Keyword":"UCP-A1B2C3D4"}}
```

**Force-replace references (upstream repository):**

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"ForceReplaceReferences","params":{"ReplacementObjectPath":"/Game/Meshes/NewMesh.NewMesh","ObjectsToReplacePaths":["/Game/Meshes/OldMesh.OldMesh"]}}
```

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__ObjectEditorOperationLibrary","function":"ForceReplaceReferences","params":{"ReplacementObjectPath":"/Game/Meshes/NewMesh.NewMesh","ObjectsToReplacePaths":["/Game/Meshes/OldMesh.OldMesh"]}}
```

## Engine Built-in Function Libraries

You can also call these engine-provided libraries via UCP `call`:

### UKismetSystemLibrary

**CDO Path**: `/Script/Engine.Default__KismetSystemLibrary`

Commonly used functions:
- `PrintString(InString, bPrintToScreen, bPrintToLog, TextColor, Duration)` — Print debug text
- `GetDisplayName(Object)` — Get display name
- `GetObjectName(Object)` — Get object name
- `GetPathName(Object)` — Get full path name
- `IsValid(Object)` — Check if object is valid
- `GetClassDisplayName(Class)` — Get class display name

### UKismetMathLibrary

**CDO Path**: `/Script/Engine.Default__KismetMathLibrary`

Math utilities: vector operations, rotator operations, transforms, random, etc.

## Property Value Formats

When using `SetObjectProperty`, the `JsonValue` parameter is a JSON string:
- `FVector` → `"{\"X\":1,\"Y\":2,\"Z\":3}"`
- `FRotator` → `"{\"Pitch\":0,\"Yaw\":90,\"Roll\":0}"`
- `FLinearColor` → `"{\"R\":1,\"G\":0.5,\"B\":0,\"A\":1}"`
- `FString` → `"\"hello\""`
- `bool` → `"true"` / `"false"`
- `float/int` → `"1.5"` / `"42"`
- `UObject*` → `"\"/Game/Path/To/Asset.Asset\""`
- `FGameplayTag` → `"{\"TagName\":\"Your.Tag.Here\"}"` (see **GameplayTag properties** below)

## GameplayTag properties (`FGameplayTag`)

Use **`GetObjectProperty`** / **`SetObjectProperty`** only. The UPROPERTY name is whatever the class declares (often `Tag` or similar — confirm with `DescribeObject` / `DescribeObjectProperty` if unsure).

- **Read**

```json
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"GetObjectProperty","params":{"ObjectPath":"/LocomotionDriver/Setting/Weapon/DA_Weapon_MPX5.DA_Weapon_MPX5","PropertyName":"Tag"}}
```

- **Write** — `JsonValue` must be a JSON **string** whose content is one object with a **`TagName`** field (matches UE’s `FGameplayTag` JSON import):

```json
{"type":"call","object":"/Script/UnrealClientProtocol.Default__ObjectOperationLibrary","function":"SetObjectProperty","params":{"ObjectPath":"/LocomotionDriver/Setting/Weapon/DA_Weapon_MPX5.DA_Weapon_MPX5","PropertyName":"Tag","JsonValue":"{\"TagName\":\"Unit.Weapon.Melee.LeadPipe\"}"}}
```

Do **not** try to call Blueprint/C++ methods such as `SetTag` via generic “call function” helpers — they are **not** exposed on this library. For **`FGameplayTagContainer`** or other tag-related types, use **`DescribeObjectProperty`** on that property name first to see the JSON shape `SetObjectProperty` expects.

## Safe Undo/Redo Pattern

Every UCP call returns an `id` field (e.g. `"UCP-A1B2C3D4"`) that identifies the Undo transaction. Use this ID as the `Keyword` parameter when undoing to ensure you only undo your own operations:

1. Make a call → response contains `"id":"UCP-A1B2C3D4"`
2. If the call produced an undesired result, undo it with `UndoTransaction(Keyword="UCP-A1B2C3D4")`
3. If the top of the Undo stack is NOT your operation (e.g. user did something manually), the undo will be rejected with an error showing the actual top transaction title.

This prevents accidentally undoing user's manual edits.

**When to use Keyword:**
- After a failed or undesired operation — pass the ID you just received
- For "try and rollback" patterns — record ID, check result, undo if wrong

**When to omit Keyword:**
- When the user explicitly asks to undo (they expect any undo)
- When calling `GetTransactionState` to inspect the stack

## Decision Flow

```mermaid
flowchart TD
    A[User request about objects] --> B{Do I know the class and property/function?}
    B -->|YES| C[Build call directly from UE knowledge]
    B -->|NO| D[Call DescribeObject to discover API]
    D --> C
    C --> E[Execute via UCP]
    E --> F{Success?}
    F -->|YES| G[Done]
    F -->|NO| H[Read error + expected, fix and retry]
    H --> E
```
