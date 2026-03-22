---
name: unreal-asset-operation
description: Query and manage UE assets via UCP. Use when the user asks about asset search, dependencies, referencers, asset CRUD, asset management, getting selected/current assets, opening/closing asset editors, or any editor asset operation in Unreal Engine.
---

# Unreal Asset Operations

Operate on Unreal Engine assets through UCP's `call` command. The core approach is to obtain the `IAssetRegistry` instance and call its rich API directly, combined with engine asset libraries for CRUD operations.

**Prerequisite**: The `unreal-client-protocol` skill must be available and the UE editor must be running with the UCP plugin enabled.

This document merges **(A)** the project-local skill (UCP.py shell notes, extended `UAssetEditorOperationLibrary` gather helpers, full `call` JSON examples) with **(B)** the upstream **UnrealClientProtocol** repository skill (additional APIs, minimal JSON examples, extra engine library rows). Where both describe the same topic, material from **both** is retained—either merged into one table or presented in adjacent subsections.

### UCP.py invocation (avoid “Invalid JSON” and empty stdin)

The CLI `scripts/UCP.py` **only reads one JSON value from standard input**. It does **not** treat the first command-line argument as a path to a JSON file. If you run `python UCP.py /path/to/query.json` with no stdin, stdin is empty and you get **`Invalid JSON: Expecting value: line 1 column 1 (char 0)`**.

- **Correct (PowerShell only)**: `@'...'@ | python UCP.py` — see `unreal-client-protocol` skill. **`@'...'@` is not valid in Bash.**
- **Correct (Bash / Git Bash / Unix)**: `python UCP.py <<'EOF'` … `EOF`, or `cat file.json | python UCP.py`, or `python UCP.py < file.json`. See the **Bash / Git Bash** section in `unreal-client-protocol`.
- **Wrong**: Passing JSON only as `argv` to `UCP.py` (no stdin).
- **Wrong**: Using **`cd ... && @'...'@ | python UCP.py`** when the shell is **Bash** — Bash does not understand `@'...'@`; you get **`No such file or directory`** on the `@{...}@` token and empty stdin → invalid JSON. Use Bash heredoc or pipe from a file instead.

### Why `GatherAssetReferences*` or search can return empty arrays

1. **Wrong `AssetPath` / object path**: `GatherAssetReferencesByAssetPath` resolves the asset with **`GetAssetByObjectPath`**. The string must be the **exact** soft object path the registry knows, including the **correct mount**:
   - Project content: `/Game/.../AssetName.AssetName`
   - **Plugin content**: `/PluginName/.../AssetName.AssetName` (e.g. `/LocomotionDriver/.../BP_InteractableDoor.BP_InteractableDoor`, **not** `/Game/...` unless the asset lives under `/Game`).
   Guessing `/Game/Blueprints/...` for an asset that actually lives under a plugin will yield **no registry entry** and empty dependency lists.
2. **`GatherAssetSoftPathsBySearchQuery` with `Scope: Project`**: **Project** only searches under **`/Game`**. Assets in **plugin** content dirs are **not** under `/Game`. Use **`AllAssets`**, or **`CustomPackagePath`** set to the plugin root (e.g. `/LocomotionDriver`), or call **`GatherAssetSoftPathsBySearchQueryInAllContent`** (wraps `AllAssets` with safe defaults).
3. **Parameter name**: The Blueprint parameter is **`AssetPath`** (not a truncated `Ass…`). Typos in JSON keys are ignored or wrong → empty or wrong behavior.
4. **Dependency graph**: Results reflect **saved** on-disk references; save assets in the editor if results look stale.

### Asset discovery workflow (path first, then global)

When looking for assets by **folder path** and/or **name**:

1. **If you know the content folder path** (long package path such as `/Game/...` or `/PluginName/...`): **first** use the simplified path APIs—**`GatherAssetSoftPathsUnderContentPathSimple`** (enumerate everything under that path, recursive) or **`GatherAssetSoftPathsBySearchQueryUnderContentPath`** (same SearchEverywhere-style `Query` as global search, but scoped to that path only). Prefer these over the full **`GatherAssetSoftPathsBySearchQuery`** when you do not need `Scope` / `ClassFilter` / `MinCharacters` / `bWholeWord` knobs.
2. **If you only have a name or keyword** and no reliable path, or path-scoped search returned nothing useful, call **`GatherAssetSoftPathsBySearchQueryInAllContent`** (all content roots, including plugin mounts).
3. **Never** tell the user that **no asset exists** based solely on **path-only** or **path-scoped** search returning an empty list. You must **also** run a **global** search (`GatherAssetSoftPathsBySearchQueryInAllContent` with the same name/query) before concluding absence—unless the user explicitly asked to search **only** that folder and nowhere else.

## Custom Function Library

### UAssetEditorOperationLibrary (Editor)

**CDO Path**: `/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary`

| Function | Params | Returns | Description |
|----------|--------|---------|-------------|
| `GetAssetRegistry` | (none) | `IAssetRegistry` object | Returns the AssetRegistry instance. Call its functions directly via UCP `call`. |
| `ForceDeleteAssets` | `AssetPaths` (array of string) | `int32` — number deleted | Force-deletes assets ignoring references. Wraps `ObjectTools::ForceDeleteObjects`. |
| `FixupReferencers` | `AssetPaths` (array of string) | `bool` | Cleans up redirectors left after rename/consolidate. Wraps `IAssetTools::FixupReferencers`. |
| `GatherDerivedClassPaths` | `BaseClasses`, `ExcludedClasses`, `OutDerivedClassPaths` | void | Fills `OutDerivedClassPaths` with derived class path names from the registry (skips `SKEL_` / `REINST_`). |
| `GatherDerivedClasses` | `BaseClasses`, `ExcludedClasses`, `OutDerivedClass` | void | Same as `GatherDerivedClassPaths` but resolves to loaded `UClass*` (loads blueprint-generated classes when needed). |
| `GatherDataAssetsByBaseClass` | `BaseDataAssetClass`, `OutAssetDatas` | void | All `DataAsset` instances whose class is `BaseDataAssetClass` or a subclass (registry only, no asset load unless needed). |
| `GatherDataAssetsByAssetPath` | `DataAssetPath`, `OutAssetDatas` | void | Passes one `DataAsset` or blueprint-DA asset path (string or `Class'/path/asset.asset'` form); resolves base class then collects same as `GatherDataAssetsByBaseClass`. |
| `GatherDataAssetSoftPathsByBaseClass` | `BaseDataAssetClass`, `OutSoftPaths` | void | Same as `GatherDataAssetsByBaseClass` but outputs `FSoftObjectPath` array. |
| `GatherDataAssetSoftPathsByAssetPath` | `DataAssetPath`, `OutSoftPaths` | void | Registry-only resolution of base class from path; then `OutSoftPaths` for all matching DAs. |
| `GatherDerivedClassesByBlueprintPath` | `BlueprintClassPath`, `OutDerivedClasses` | void | Given a blueprint asset path (or `BlueprintGeneratedClass` path), loads blueprint if needed and returns all derived `UClass*` (same filters as `GatherDerivedClasses`). |
| `GatherDerivedClassSoftPathsByBlueprintPath` | `BlueprintClassPath`, `OutDerivedClassSoftPaths` | void | Same as above but **no loading**; uses `GetDerivedClassNames` + soft paths only. |
| `GatherAssetReferencesByAssetPath` | `AssetPath` (exact soft path / export string), `OutDependencies`, `OutReferencers` | void | Disk dependency graph: `OutDependencies` = assets this package depends on; `OutReferencers` = packages that reference it. **`AssetPath` must match registry (plugin vs `/Game`).** |
| `GatherAssetReferencesBySoftPath` | `SoftObjectPath`, `OutDependencies`, `OutReferencers` | void | Same as above with `FSoftObjectPath` input. |
| `GatherImmediateSubFolderPaths` | `FolderPath`, `OutSubFolderPaths` | void | Direct child **content** folders under `FolderPath` (Asset Registry path tree; not recursive). |
| `GatherImmediateSubFolderNames` | `FolderPath`, `OutSubFolderNames` | void | Same as above with `FName` for folder and results. |
| `GatherAssetSoftPathsBySearchQuery` | `Query`, `Scope`, `ClassFilter`, `bCaseSensitive`, `bWholeWord`, `MaxResults`, `MinCharacters`, `CustomPackagePath`, `OutSoftPaths`, `OutIncludeTokensForHighlight` | void | Keyword search (same rules as SearchEverywhere). **`Scope`=`CustomPackagePath`** + **`CustomPackagePath`** = search only under that content root (recursive subfolders). `Scope` = `AllAssets` / `Project` / `CustomPackagePath` as above. |
| `GatherAssetSoftPathsBySearchQueryInAllContent` | `Query`, `MaxResults`, `OutSoftPaths`, `OutIncludeTokensForHighlight` | void | Same as `GatherAssetSoftPathsBySearchQuery` with **Scope = AllAssets** (includes plugin mounts), `MinCharacters=1`, no class filter, case-insensitive, substring match. Use when you want to find a named asset by keyword without restricting to `/Game` only. |
| `GatherAssetSoftPathsUnderContentFolderPath` | `FolderPath`, `bIncludeSubfolders`, `OutSoftPaths` | void | Lists **all assets** under a **long content path** (`/Game/...`, `/PluginName/...`). **`bIncludeSubfolders` = true** → includes assets in **nested** folders (`FARFilter::bRecursivePaths`). **`false`** → only non-recursive scope (no subfolders). **Does not load** assets; registry only. |
| `GatherAssetSoftPathsUnderContentPathSimple` | `ContentFolderPath`, `OutSoftPaths` | void | **Simplified path listing**: only the folder path; always **recursive** (same as `GatherAssetSoftPathsUnderContentFolderPath` with `bIncludeSubfolders: true`). Use when the path is known and you want the smallest parameter surface. |
| `GatherAssetSoftPathsBySearchQueryUnderContentPath` | `ContentFolderPath`, `Query`, `MaxResults`, `OutSoftPaths`, `OutIncludeTokensForHighlight` | void | **Simplified path + keyword**: SearchEverywhere-style `Query` restricted to **`CustomPackagePath` = `ContentFolderPath`** (recursive). Same defaults spirit as `GatherAssetSoftPathsBySearchQueryInAllContent` (`MinCharacters=1`, etc.) without exposing `Scope` / `ClassFilter` / `bWholeWord`. |

#### Getting the AssetRegistry

**Project-local (full UCP `call` payload):**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GetAssetRegistry"}
```

The returned object path can then be used as the `object` parameter for subsequent calls to IAssetRegistry functions.

Alternatively, you can use the engine helper directly:

```json
{"type":"call","object":"/Script/AssetRegistry.Default__AssetRegistryHelpers","function":"GetAssetRegistry"}
```

**Upstream UnrealClientProtocol repository (minimal object/function only — add `"type":"call"` for UCP.py):**

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GetAssetRegistry"}
```

```json
{"object":"/Script/AssetRegistry.Default__AssetRegistryHelpers","function":"GetAssetRegistry"}
```

#### Force-deleting assets

**Upstream repository example (wrap with `type`/`call` for UCP):**

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"ForceDeleteAssets","params":{"AssetPaths":["/Game/OldAssets/M_Unused","/Game/OldAssets/T_Unused"]}}
```

**Same call as full UCP payload:**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"ForceDeleteAssets","params":{"AssetPaths":["/Game/OldAssets/M_Unused","/Game/OldAssets/T_Unused"]}}
```

#### Fixing up redirectors

After renaming or consolidating assets, redirectors may be left behind. Use `FixupReferencers` to resolve them.

**Upstream repository example:**

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"FixupReferencers","params":{"AssetPaths":["/Game/Materials/M_OldName"]}}
```

**Full UCP payload:**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"FixupReferencers","params":{"AssetPaths":["/Game/Materials/M_OldName"]}}
```

#### Quick asset gather helpers (UAssetEditorOperationLibrary)

These functions live on the same CDO as `GetAssetRegistry`: `/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary`. Invoke them with UCP `call` and use the **Blueprint parameter names** exactly (case-sensitive). For any `TArray` outputs (`Out…`), pass an empty JSON array `[]` so the bridge can fill results.

**General notes**

- **`GatherDerivedClassPaths` / `GatherDerivedClasses`**: `BaseClasses` and `ExcludedClasses` are sets of `UClass` references. Pass them in whatever object-reference form your UCP layer uses for class pointers (often a soft path to the generated class, e.g. a blueprint-generated class `…_C`).
- **`GatherDataAssetsByBaseClass` / soft-path variants**: `BaseDataAssetClass` is a `TSubclassOf<UDataAsset>`; use your project’s DataAsset C++ class path or an equivalent class reference.
- **`GatherDataAssetsByAssetPath` / `GatherDataAssetSoftPathsByAssetPath`**: `DataAssetPath` may be a bare object path (`/Game/…/DA.DA`) or a full export string with quotes (e.g. `/Script/Engine.Blueprint'/Game/…/BPDA.BPDA'`); the implementation strips the inner quoted path when present.
- **`GatherDerivedClassesByBlueprintPath` / `GatherDerivedClassSoftPathsByBlueprintPath`**: `BlueprintClassPath` is the blueprint asset path or a `BlueprintGeneratedClass` path; the soft-path variant avoids loading assets and uses `GetDerivedClassNames` only.
- **`GatherAssetReferencesByAssetPath` / `GatherAssetReferencesBySoftPath`**: Results come from the **saved** Asset Registry dependency graph (package-level nodes). `OutDependencies` lists assets/packages this asset depends on; `OutReferencers` lists assets/packages that reference it. Save assets in the editor if results look stale.
- **`GatherImmediateSubFolderPaths` / `GatherImmediateSubFolderNames`**: Lists **only direct child** content folders under `FolderPath` (not recursive); depends on the registry path tree having scanned those paths.
- **`GatherAssetSoftPathsBySearchQuery`**: Query language matches the Search Everywhere plugin: split on spaces; tokens starting with `!` are **excluded** from the asset **name**; a token `&Type=Foo` filters by **asset class name** substring (case-insensitive); remaining tokens must all match the asset name (substring or whole-word per flags). At least one include token **or** a `&Type=` filter is required; if include tokens exist, each must be at least `MinCharacters` long. `ClassFilter` may restrict to a native class path starting with `/Script/` (same as the original implementation). `Scope` is `EUCPAssetSearchScope`: `AllAssets` (all content roots), `Project` (`/Game` only), or **`CustomPackagePath`** — **to search only under one content folder**, set **`Scope` to `CustomPackagePath`** and set **`CustomPackagePath`** to that folder’s **long package path** (e.g. `/Game/Blueprints/Interactable` or `/PluginName/SubFolder`). The filter uses **`bRecursivePaths`**, so **all subfolders** under that path are included. If `CustomPackagePath` is left empty while `Scope` is `CustomPackagePath`, behavior falls back to **`/Game`**.
- **`GatherAssetSoftPathsBySearchQueryInAllContent`**: Shortcut to search by asset **name** across **all** content roots (including plugin mounts), with fixed defaults (`AllAssets`, `MinCharacters=1`, case-insensitive, substring). Prefer this over `Scope: Project` when the asset might not live under `/Game`.
- **`GatherAssetSoftPathsUnderContentFolderPath`**: Enumerates every asset under the given **content folder path** (long package path style) and returns **`FSoftObjectPath`** strings. **`bIncludeSubfolders`** maps to **`FARFilter::bRecursivePaths`**: `true` includes packages under nested folders; `false` only the non-recursive filter (no assets in child content paths). **Not** a keyword filter—use `GatherAssetSoftPathsBySearchQuery` if you need name matching. Path is normalized (trim, optional leading `/`, trailing `/` stripped except root).
- **`GatherAssetSoftPathsUnderContentPathSimple`**: Same as **`GatherAssetSoftPathsUnderContentFolderPath`** with **`bIncludeSubfolders` fixed to `true`**; only **`ContentFolderPath`** + **`OutSoftPaths`**. Prefer when the workflow calls for **path-first** discovery (see **Asset discovery workflow** above).
- **`GatherAssetSoftPathsBySearchQueryUnderContentPath`**: Keyword search with **`Scope` = `CustomPackagePath`** and **`CustomPackagePath` = `ContentFolderPath`**; same query parsing as **`GatherAssetSoftPathsBySearchQuery`**, with **`MinCharacters=1`** and defaults aligned to the global shortcut. Use when you know the folder but need a **name filter** without building a full `GatherAssetSoftPathsBySearchQuery` parameter list.

**Example: derived blueprint classes as soft paths (no loading)**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherDerivedClassSoftPathsByBlueprintPath","params":{"BlueprintClassPath":"/Script/Engine.Blueprint'/Game/Blueprints/BP_Base.BP_Base'","OutDerivedClassSoftPaths":[]}}
```

**Example: dependencies and referencers by asset path**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherAssetReferencesByAssetPath","params":{"AssetPath":"/Game/MyFolder/MyAsset.MyAsset","OutDependencies":[],"OutReferencers":[]}}
```

**Example: immediate subfolders**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherImmediateSubFolderPaths","params":{"FolderPath":"/Game/MyFolder","OutSubFolderPaths":[]}}
```

**Example: keyword search (include token + exclude + type filter; project scope)**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherAssetSoftPathsBySearchQuery","params":{"Query":"rock !test &Type=Material","Scope":"Project","ClassFilter":"","bCaseSensitive":false,"bWholeWord":false,"MaxResults":100,"MinCharacters":1,"CustomPackagePath":"","OutSoftPaths":[],"OutIncludeTokensForHighlight":[]}}
```

**Example: keyword search restricted to a single content path** (`Scope` must be `CustomPackagePath`; `CustomPackagePath` is the root; subfolders are included recursively)

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherAssetSoftPathsBySearchQuery","params":{"Query":"Door","Scope":"CustomPackagePath","ClassFilter":"","bCaseSensitive":false,"bWholeWord":false,"MaxResults":100,"MinCharacters":1,"CustomPackagePath":"/Game/Blueprints/Interactable","OutSoftPaths":[],"OutIncludeTokensForHighlight":[]}}
```

**Example: find assets by name across all content roots (recommended when the asset may live in a plugin, not under `/Game`)**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherAssetSoftPathsBySearchQueryInAllContent","params":{"Query":"BP_InteractableDoor","MaxResults":50,"OutSoftPaths":[],"OutIncludeTokensForHighlight":[]}}
```

**Example: list all asset soft paths under a folder (recursive — includes subfolders)**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherAssetSoftPathsUnderContentFolderPath","params":{"FolderPath":"/Game/Blueprints/Interactable","bIncludeSubfolders":true,"OutSoftPaths":[]}}
```

**Example: same folder only (non-recursive — no assets in child content paths)**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherAssetSoftPathsUnderContentFolderPath","params":{"FolderPath":"/Game/Blueprints/Interactable","bIncludeSubfolders":false,"OutSoftPaths":[]}}
```

**Example: simplified path listing (recursive — same as `bIncludeSubfolders: true`, fewer parameters)**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherAssetSoftPathsUnderContentPathSimple","params":{"ContentFolderPath":"/Game/Blueprints/Interactable","OutSoftPaths":[]}}
```

**Example: simplified path + keyword (search under one content root only)**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherAssetSoftPathsBySearchQueryUnderContentPath","params":{"ContentFolderPath":"/LocomotionDriver/TheLastOfUs/Ellie","Query":"Door","MaxResults":50,"OutSoftPaths":[],"OutIncludeTokensForHighlight":[]}}
```

**Example: dependencies after you know the exact soft path** (from Content Browser → Copy Reference, or from the search result above)

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"GatherAssetReferencesByAssetPath","params":{"AssetPath":"/LocomotionDriver/TheLastOfUs/Objects/Door/BP_InteractableDoor.BP_InteractableDoor","OutDependencies":[],"OutReferencers":[]}}
```

(Replace `/LocomotionDriver/...` with your project’s real mount path; the example illustrates **plugin** layout vs `/Game/...`.)

## IAssetRegistry — Full API

Once you have the AssetRegistry instance, you can call these BlueprintCallable functions on it:

### Asset Queries

| Function | Key Params | Description |
|----------|------------|-------------|
| `GetAssetsByPackageName` | `PackageName`, `OutAssetData`, `bIncludeOnlyOnDiskAssets` | Get assets in a specific package |
| `GetAssetsByPath` | `PackagePath`, `OutAssetData`, `bRecursive` | Get assets under a content path |
| `GetAssetsByPaths` | `PackagePaths`, `OutAssetData`, `bRecursive` | Get assets under multiple paths |
| `GetAssetsByClass` | `ClassPathName`, `OutAssetData`, `bSearchSubClasses` | Get all assets of a specific class |
| `GetAssets` | `Filter`, `OutAssetData` | Query with an `FARFilter` (powerful filtered search) |
| `GetAllAssets` | `OutAssetData`, `bIncludeOnlyOnDiskAssets` | Get ALL registered assets |
| `GetAssetByObjectPath` | `ObjectPath` | Get single asset data by path |
| `HasAssets` | `PackagePath`, `bRecursive` | Check if any assets exist under a path |
| `GetInMemoryAssets` | `Filter`, `OutAssetData` | Query only currently loaded assets |

### Dependency & Reference Queries

| Function | Key Params | Description |
|----------|------------|-------------|
| `GetDependencies` | `PackageName`, `DependencyOptions`, `OutDependencies` | Get packages this asset depends on |
| `GetReferencers` | `PackageName`, `ReferenceOptions`, `OutReferencers` | Get packages that reference this asset |

### Class Hierarchy

| Function | Key Params | Description |
|----------|------------|-------------|
| `GetAncestorClassNames` | `ClassPathName`, `OutAncestorClassNames` | Get parent classes |
| `GetDerivedClassNames` | `ClassNames`, `DerivedClassNames` | Get child classes |

### Path & Scanning

| Function | Key Params | Description |
|----------|------------|-------------|
| `GetAllCachedPaths` | `OutPathList` | Get all known content paths |
| `GetSubPaths` | `InBasePath`, `OutPathList`, `bInRecurse` | Get sub-paths under a base path |
| `ScanPathsSynchronous` | `InPaths`, `bForceRescan` | Force scan specific paths |
| `ScanFilesSynchronous` | `InFilePaths`, `bForceRescan` | Force scan specific files |
| `SearchAllAssets` | `bSynchronousSearch` | Trigger full asset scan |
| `ScanModifiedAssetFiles` | `InFilePaths` | Scan modified files |
| `IsLoadingAssets` | (none) | Check if scan is in progress |
| `WaitForCompletion` | (none) | Block until scan finishes |

### Filter & Sort

| Function | Key Params | Description |
|----------|------------|-------------|
| `RunAssetsThroughFilter` | `AssetDataList`, `Filter` | Filter asset list in-place (keep matching) |
| `UseFilterToExcludeAssets` | `AssetDataList`, `Filter` | Filter asset list in-place (remove matching) |

### UAssetRegistryHelpers (Static Utilities)

**CDO Path**: `/Script/AssetRegistry.Default__AssetRegistryHelpers`

| Function | Description |
|----------|-------------|
| `GetAssetRegistry` | Get the IAssetRegistry instance |
| `GetDerivedClassAssetData` | Get asset data for derived classes |
| `SortByAssetName` | Sort FAssetData array by name |
| `SortByPredicate` | Sort FAssetData array by custom predicate |

**Upstream repository note:** the same IAssetRegistry API surface is also documented there under **Engine Built-in Asset Libraries → IAssetRegistry (via GetAssetRegistry)** with identical function lists grouped under `####` subheadings (Asset Queries, Dependency & Reference Queries, etc.).

## Engine Built-in Asset Libraries

### IAssetTools — Create, Import, Export Assets

Get the instance via static helper:

```json
{"type":"call","object":"/Script/AssetTools.Default__AssetToolsHelpers","function":"GetAssetTools"}
```

**Upstream repository (minimal — add `type`/`call` for UCP.py):**

```json
{"object":"/Script/AssetTools.Default__AssetToolsHelpers","function":"GetAssetTools"}
```

Then call functions on the returned instance:

#### Create
| Function | Key Params | Description |
|----------|------------|-------------|
| `CreateAsset` | `AssetName`, `PackagePath`, `AssetClass`, `Factory` | Create a new asset (Factory can be null for simple types) |
| `CreateUniqueAssetName` | `InBasePackageName`, `InSuffix` | Generate a unique name to avoid conflicts |
| `DuplicateAsset` | `AssetName`, `PackagePath`, `OriginalObject` | Duplicate an existing asset |

#### Import & Export
| Function | Key Params | Description |
|----------|------------|-------------|
| `ImportAssetTasks` | `ImportTasks` (array of UAssetImportTask) | Import external files as assets |
| `ImportAssetsAutomated` | `ImportData` (UAutomatedAssetImportData) | Automated batch import |
| `ExportAssets` | `AssetsToExport`, `ExportPath` | Export assets to files |

#### Rename, migrate, and move (merged: batch rename, soft references, migration)

| Function | Key Params | Description |
|----------|------------|-------------|
| `RenameAssets` | `AssetsAndNames` (array of FAssetRenameData) | Batch rename/move assets |
| `FindSoftReferencesToObject` | `TargetObject` | Find soft references (including “to an object” — upstream wording) |
| `RenameReferencingSoftObjectPaths` | `PackagesToCheck`, `AssetRedirectorMap` | Update soft references after rename |
| `MigratePackages` | `PackageNamesToMigrate`, `DestinationPath` | Migrate packages to another project |

#### Example: Create a new material

**Project-local:**

```json
{"type":"call","object":"<asset_tools_instance>","function":"CreateAsset","params":{"AssetName":"M_NewMaterial","PackagePath":"/Game/Materials","AssetClass":"/Script/Engine.Material","Factory":null}}
```

**Upstream repository:**

```json
{"object":"<asset_tools_instance>","function":"CreateAsset","params":{"AssetName":"M_NewMaterial","PackagePath":"/Game/Materials","AssetClass":"/Script/Engine.Material","Factory":null}}
```

#### Example: Batch rename assets (upstream repository)

`RenameAssets` takes an array of `FAssetRenameData` structs. Each struct has `Asset` (the object to rename), `NewPackagePath` (destination directory), and `NewName` (new asset name):

```json
{"object":"<asset_tools_instance>","function":"RenameAssets","params":{"AssetsAndNames":[{"Asset":"/Game/Materials/M_OldName.M_OldName","NewPackagePath":"/Game/Materials","NewName":"M_NewName"}]}}
```

**Full UCP:**

```json
{"type":"call","object":"<asset_tools_instance>","function":"RenameAssets","params":{"AssetsAndNames":[{"Asset":"/Game/Materials/M_OldName.M_OldName","NewPackagePath":"/Game/Materials","NewName":"M_NewName"}]}}
```

#### Example: Find soft references (upstream repository)

```json
{"object":"<asset_tools_instance>","function":"FindSoftReferencesToObject","params":{"TargetObject":"/Game/Materials/M_Example.M_Example"}}
```

**Full UCP:**

```json
{"type":"call","object":"<asset_tools_instance>","function":"FindSoftReferencesToObject","params":{"TargetObject":"/Game/Materials/M_Example.M_Example"}}
```

### UEditorAssetLibrary — Asset CRUD

**CDO Path**: `/Script/EditorScriptingUtilities.Default__EditorAssetLibrary`

#### Query & Check
| Function | Description |
|----------|-------------|
| `DoesAssetExist(AssetPath)` | Check if an asset exists |
| `DoAssetsExist(AssetPaths)` | Batch existence check |
| `FindAssetData(AssetPath)` | Get asset metadata |
| `ListAssets(DirectoryPath, bRecursive, bIncludeFolder)` | List assets in directory |
| `ListAssetByTagValue(AssetTagName, TagValue)` | List assets matching a specific tag value |
| `LoadAsset(AssetPath)` | Load an asset into memory |
| `LoadBlueprintClass(AssetPath)` | Load a Blueprint and return its generated class |
| `GetTagValues(AssetPath, AssetTagName)` | Get tag values for a specific asset |
| `FindPackageReferencersForAsset(AssetPath, bLoadAssetsToConfirm)` | Find all packages that reference this asset |

##### Example: Find references to an asset (upstream repository)

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"FindPackageReferencersForAsset","params":{"AssetPath":"/Game/Materials/M_Example","bLoadAssetsToConfirm":false}}
```

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"FindPackageReferencersForAsset","params":{"AssetPath":"/Game/Materials/M_Example","bLoadAssetsToConfirm":false}}
```

##### Example: List assets by tag (upstream repository)

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"ListAssetByTagValue","params":{"AssetTagName":"MyCustomTag","TagValue":"SomeValue"}}
```

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"ListAssetByTagValue","params":{"AssetTagName":"MyCustomTag","TagValue":"SomeValue"}}
```

##### Example: Load a Blueprint class (upstream repository)

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"LoadBlueprintClass","params":{"AssetPath":"/Game/Blueprints/BP_MyActor"}}
```

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"LoadBlueprintClass","params":{"AssetPath":"/Game/Blueprints/BP_MyActor"}}
```

#### Create & Modify
| Function | Description |
|----------|-------------|
| `DuplicateAsset(SourceAssetPath, DestAssetPath)` | Duplicate an asset |
| `DuplicateDirectory(SourceDirectoryPath, DestinationDirectoryPath)` | Duplicate an entire directory of assets |
| `RenameAsset(SourceAssetPath, DestAssetPath)` | Rename/move an asset |
| `RenameDirectory(SourceDirectoryPath, DestinationDirectoryPath)` | Rename/move an entire directory |
| `SaveAsset(AssetPath, bOnlyIfIsDirty)` | Save an asset to disk |
| `SaveLoadedAsset(LoadedAsset, bOnlyIfIsDirty)` | Save a loaded asset |
| `SaveDirectory(DirectoryPath, bOnlyIfIsDirty, bRecursive)` | Save all assets in directory |
| `SetMetadataTag(Object, Tag, Value)` | Set asset metadata tag |
| `GetMetadataTag(Object, Tag)` | Get asset metadata tag |
| `RemoveMetadataTag(Object, Tag)` | Remove asset metadata tag |

#### Delete & reference replace
| Function | Description |
|----------|-------------|
| `DeleteAsset(AssetPath)` | Delete an asset |
| `DeleteLoadedAsset(AssetToDelete)` | Delete a loaded asset |
| `DeleteLoadedAssets(AssetsToDelete)` | Delete multiple loaded assets |
| `ConsolidateAssets(AssetToConsolidateTo, AssetsToConsolidate)` | Replace all references to `AssetsToConsolidate` with `AssetToConsolidateTo`, then delete the consolidated assets |

##### Example: Consolidate assets (upstream repository)

Replace all references to `M_OldMaterial` with `M_NewMaterial`, effectively merging them:

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"ConsolidateAssets","params":{"AssetToConsolidateTo":"/Game/Materials/M_NewMaterial.M_NewMaterial","AssetsToConsolidate":["/Game/Materials/M_OldMaterial.M_OldMaterial"]}}
```

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"ConsolidateAssets","params":{"AssetToConsolidateTo":"/Game/Materials/M_NewMaterial.M_NewMaterial","AssetsToConsolidate":["/Game/Materials/M_OldMaterial.M_OldMaterial"]}}
```

After consolidation, run `FixupReferencers` to clean up any leftover redirectors:

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"FixupReferencers","params":{"AssetPaths":["/Game/Materials/M_OldMaterial"]}}
```

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"FixupReferencers","params":{"AssetPaths":["/Game/Materials/M_OldMaterial"]}}
```

#### Directory Management
| Function | Description |
|----------|-------------|
| `MakeDirectory(DirectoryPath)` | Create a content directory |
| `DeleteDirectory(DirectoryPath)` | Delete a content directory |
| `DoesDirectoryExist(DirectoryPath)` | Check if directory exists |
| `DoesDirectoryHaveAssets(DirectoryPath, bRecursive)` | Check if directory contains assets |

#### Metadata tags
| Function | Description |
|----------|-------------|
| `SetMetadataTag(Object, Tag, Value)` | Set asset metadata tag |
| `GetMetadataTag(Object, Tag)` | Get asset metadata tag |
| `RemoveMetadataTag(Object, Tag)` | Remove asset metadata tag |
| `GetMetadataTagValues(Object)` | Get all metadata tag key-value pairs for an asset |

##### Example: Get all metadata tags (upstream repository)

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"GetMetadataTagValues","params":{"Object":"/Game/Materials/M_Example.M_Example"}}
```

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"GetMetadataTagValues","params":{"Object":"/Game/Materials/M_Example.M_Example"}}
```

#### Browser sync (upstream repository)
| Function | Description |
|----------|-------------|
| `SyncBrowserToObjects(AssetPaths)` | Sync the Content Browser to show the specified assets |

##### Example: Sync browser to an asset

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"SyncBrowserToObjects","params":{"AssetPaths":["/Game/Materials/M_Example"]}}
```

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"SyncBrowserToObjects","params":{"AssetPaths":["/Game/Materials/M_Example"]}}
```

#### Checkout (Source Control)
| Function | Description |
|----------|-------------|
| `CheckoutAsset(AssetToCheckout)` | Check out asset for editing |
| `CheckoutLoadedAsset(AssetToCheckout)` | Check out a loaded asset |
| `CheckoutDirectory(DirectoryPath, bRecursive)` | Check out all assets in directory |

##### Example: Duplicate a directory (upstream repository)

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"DuplicateDirectory","params":{"SourceDirectoryPath":"/Game/Materials","DestinationDirectoryPath":"/Game/Materials_Backup"}}
```

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"DuplicateDirectory","params":{"SourceDirectoryPath":"/Game/Materials","DestinationDirectoryPath":"/Game/Materials_Backup"}}
```

##### Example: Rename a directory (upstream repository)

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"RenameDirectory","params":{"SourceDirectoryPath":"/Game/OldFolder","DestinationDirectoryPath":"/Game/NewFolder"}}
```

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"RenameDirectory","params":{"SourceDirectoryPath":"/Game/OldFolder","DestinationDirectoryPath":"/Game/NewFolder"}}
```

### UEditorUtilityLibrary — Content Browser & Selection

**CDO Path**: `/Script/Blutility.Default__EditorUtilityLibrary`

| Function | Description |
|----------|-------------|
| `GetSelectedAssets()` | Get currently selected assets in content browser |
| `GetSelectedAssetData()` | Get selected asset data |
| `GetSelectedAssetsOfClass(AssetClass)` | Get selected assets filtered by class |
| `GetCurrentContentBrowserPath()` | Get the current path shown in Content Browser |
| `GetSelectedFolderPaths()` | Get the folder paths selected in Content Browser |
| `SyncBrowserToFolders(FolderList)` | Navigate Content Browser to specified folders |

##### Example: Get selected assets of a specific class (upstream repository)

```json
{"object":"/Script/Blutility.Default__EditorUtilityLibrary","function":"GetSelectedAssetsOfClass","params":{"AssetClass":"/Script/Engine.Material"}}
```

```json
{"type":"call","object":"/Script/Blutility.Default__EditorUtilityLibrary","function":"GetSelectedAssetsOfClass","params":{"AssetClass":"/Script/Engine.Material"}}
```

##### Example: Navigate Content Browser to a folder (upstream repository)

```json
{"object":"/Script/Blutility.Default__EditorUtilityLibrary","function":"SyncBrowserToFolders","params":{"FolderList":["/Game/Materials"]}}
```

```json
{"type":"call","object":"/Script/Blutility.Default__EditorUtilityLibrary","function":"SyncBrowserToFolders","params":{"FolderList":["/Game/Materials"]}}
```

##### Example: Get current Content Browser path (upstream repository)

```json
{"object":"/Script/Blutility.Default__EditorUtilityLibrary","function":"GetCurrentContentBrowserPath"}
```

```json
{"type":"call","object":"/Script/Blutility.Default__EditorUtilityLibrary","function":"GetCurrentContentBrowserPath"}
```

### UAssetEditorSubsystem

**CDO Path**: Use via `call` on the subsystem (get instance via `FindObjectInstances`)

| Function | Description |
|----------|-------------|
| `OpenEditorForAsset(Asset)` | Open asset editor |
| `CloseAllEditorsForAsset(Asset)` | Close all editors for asset |

## Common Patterns

### Browse content directory

**Project-local (batch / full `call`):**

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"ListAssets","params":{"DirectoryPath":"/Game/Materials","bRecursive":true,"bIncludeFolder":false}}
```

**Upstream repository:**

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"ListAssets","params":{"DirectoryPath":"/Game/Materials","bRecursive":true,"bIncludeFolder":false}}
```

### Duplicate and rename an asset

**Project-local (single batch request):**

```json
[
  {"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"DuplicateAsset","params":{"SourceAssetPath":"/Game/Materials/M_Base","DestinationAssetPath":"/Game/Materials/M_BaseCopy"}},
  {"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"RenameAsset","params":{"SourceAssetPath":"/Game/Materials/M_BaseCopy","DestinationAssetPath":"/Game/Materials/M_NewName"}}
]
```

**Upstream repository (separate calls):**

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"DuplicateAsset","params":{"SourceAssetPath":"/Game/Materials/M_Base","DestinationAssetPath":"/Game/Materials/M_BaseCopy"}}
```

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"RenameAsset","params":{"SourceAssetPath":"/Game/Materials/M_BaseCopy","DestinationAssetPath":"/Game/Materials/M_NewName"}}
```

### Delete an asset

**Project-local:**

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"DeleteAsset","params":{"AssetPathToDelete":"/Game/Materials/M_Unused"}}
```

**Upstream repository:**

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"DeleteAsset","params":{"AssetPathToDelete":"/Game/Materials/M_Unused"}}
```

### Force-delete assets ignoring references

**Upstream repository:**

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"ForceDeleteAssets","params":{"AssetPaths":["/Game/OldAssets/M_Deprecated"]}}
```

**Full UCP:**

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"ForceDeleteAssets","params":{"AssetPaths":["/Game/OldAssets/M_Deprecated"]}}
```

### Query dependencies via AssetRegistry

**Project-local (batch):**

```json
[
  {"type":"call","object":"/Script/AssetRegistry.Default__AssetRegistryHelpers","function":"GetAssetRegistry"},
  {"type":"call","object":"<returned_registry_path>","function":"GetDependencies","params":{"PackageName":"/Game/Materials/M_Example","DependencyOptions":{},"OutDependencies":[]}}
]
```

**Upstream repository (separate steps):**

```json
{"object":"/Script/AssetRegistry.Default__AssetRegistryHelpers","function":"GetAssetRegistry"}
```

Then on the returned registry path:

```json
{"object":"<returned_registry_path>","function":"GetDependencies","params":{"PackageName":"/Game/Materials/M_Example","DependencyOptions":{},"OutDependencies":[]}}
```

### Find all materials in project

**Project-local:**

```json
{"type":"call","object":"/Script/AssetRegistry.Default__AssetRegistryHelpers","function":"GetAssetRegistry"}
```

Then on the returned registry:

```json
{"type":"call","object":"<registry_path>","function":"GetAssetsByClass","params":{"ClassPathName":"/Script/Engine.Material","OutAssetData":[],"bSearchSubClasses":false}}
```

**Upstream repository:**

```json
{"object":"/Script/AssetRegistry.Default__AssetRegistryHelpers","function":"GetAssetRegistry"}
```

```json
{"object":"<registry_path>","function":"GetAssetsByClass","params":{"ClassPathName":"/Script/Engine.Material","OutAssetData":[],"bSearchSubClasses":false}}
```

### Save all dirty assets in a directory

**Project-local:**

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"SaveDirectory","params":{"DirectoryPath":"/Game/Materials","bOnlyIfIsDirty":true,"bRecursive":true}}
```

**Upstream repository:**

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"SaveDirectory","params":{"DirectoryPath":"/Game/Materials","bOnlyIfIsDirty":true,"bRecursive":true}}
```

### Consolidate and clean up (upstream repository)

Replace references to old assets with a canonical asset, then fix redirectors:

```json
{"object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"ConsolidateAssets","params":{"AssetToConsolidateTo":"/Game/Materials/M_Master.M_Master","AssetsToConsolidate":["/Game/Materials/M_Duplicate1.M_Duplicate1","/Game/Materials/M_Duplicate2.M_Duplicate2"]}}
```

```json
{"object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"FixupReferencers","params":{"AssetPaths":["/Game/Materials/M_Duplicate1","/Game/Materials/M_Duplicate2"]}}
```

**Full UCP:**

```json
{"type":"call","object":"/Script/EditorScriptingUtilities.Default__EditorAssetLibrary","function":"ConsolidateAssets","params":{"AssetToConsolidateTo":"/Game/Materials/M_Master.M_Master","AssetsToConsolidate":["/Game/Materials/M_Duplicate1.M_Duplicate1","/Game/Materials/M_Duplicate2.M_Duplicate2"]}}
```

```json
{"type":"call","object":"/Script/UnrealClientProtocolEditor.Default__AssetEditorOperationLibrary","function":"FixupReferencers","params":{"AssetPaths":["/Game/Materials/M_Duplicate1","/Game/Materials/M_Duplicate2"]}}
```
