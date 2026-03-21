// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetEditorOperationLibrary.generated.h"
class IAssetRegistry;

/** 与 SearchEverywhere 风格一致的关键词搜索范围（Runtime 友好：用 CustomPackagePath 代替内容浏览器当前路径）。 */
UENUM(BlueprintType)
enum class EUCPAssetSearchScope : uint8
{
	AllAssets UMETA(DisplayName = "All Assets"),
	Project UMETA(DisplayName = "Project (/Game)"),
	CustomPackagePath UMETA(DisplayName = "Custom Package Root"),
};

UCLASS()
class UAssetEditorOperationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static TScriptInterface<IAssetRegistry> GetAssetRegistry();

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static int32 ForceDeleteAssets(const TArray<FString>& AssetPaths);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static bool FixupReferencers(const TArray<FString>& AssetPaths);
	
	
	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherDerivedClassPaths(const TArray<UClass*>& BaseClasses, const TSet<UClass*>& ExcludedClasses, TSet<FTopLevelAssetPath>& OutDerivedClassPaths);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherDerivedClasses(const TArray<UClass*>& BaseClasses, const TSet<UClass*>& ExcludedClasses, TSet<UClass*>& OutDerivedClass);

	static void GatherDerivedClassesWithFilter(const TArray<UClass*>& BaseClasses, const TSet<UClass*>& ExcludedClasses, TSet<UClass*>& OutDerivedClass, TFunction<bool(const UClass*)> ShouldSkipClassFilter);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherDataAssetsByBaseClass(TSubclassOf<UDataAsset> BaseDataAssetClass, TArray<FAssetData>& OutAssetDatas);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherDataAssetsByAssetPath(const FString& DataAssetPath, TArray<FAssetData>& OutAssetDatas);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherDataAssetSoftPathsByBaseClass(TSubclassOf<UDataAsset> BaseDataAssetClass, TArray<FSoftObjectPath>& OutSoftPaths);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherDataAssetSoftPathsByAssetPath(const FString& DataAssetPath, TArray<FSoftObjectPath>& OutSoftPaths);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherDerivedClassesByBlueprintPath(const FString& BlueprintClassPath, TArray<UClass*>& OutDerivedClasses);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherDerivedClassSoftPathsByBlueprintPath(const FString& BlueprintClassPath, TArray<FSoftObjectPath>& OutDerivedClassSoftPaths);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor", meta = (DisplayName = "Gather Asset References By Asset Path"))
	static void GatherAssetReferencesByAssetPath(
		UPARAM(DisplayName = "Object Path Or Export String") const FString& AssetPath,
		TArray<FSoftObjectPath>& OutDependencies,
		TArray<FSoftObjectPath>& OutReferencers);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherAssetReferencesBySoftPath(const FSoftObjectPath& SoftObjectPath, TArray<FSoftObjectPath>& OutDependencies, TArray<FSoftObjectPath>& OutReferencers);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherImmediateSubFolderPaths(const FString& FolderPath, TArray<FString>& OutSubFolderPaths);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherImmediateSubFolderNames(const FName& FolderPath, TArray<FName>& OutSubFolderNames);

	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherAssetSoftPathsBySearchQuery(
		const FString& Query,
		EUCPAssetSearchScope Scope,
		const FString& ClassFilter,
		bool bCaseSensitive,
		bool bWholeWord,
		int32 MaxResults,
		int32 MinCharacters,
		const FString& CustomPackagePath,
		TArray<FSoftObjectPath>& OutSoftPaths,
		TArray<FString>& OutIncludeTokensForHighlight);

	/**
	 * 在全部内容根（含插件挂载路径，如 /LocomotionDriver）下按关键词搜索资产软路径。
	 * 等价于 GatherAssetSoftPathsBySearchQuery：Scope=AllAssets、无 ClassFilter、非整词、大小写不敏感、MinCharacters=1、CustomPackagePath 空。
	 * 用于避免仅将 Scope 设为 Project（只搜 /Game）时漏掉插件内资产。
	 */
	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherAssetSoftPathsBySearchQueryInAllContent(
		const FString& Query,
		int32 MaxResults,
		TArray<FSoftObjectPath>& OutSoftPaths,
		TArray<FString>& OutIncludeTokensForHighlight);

	/**
	 * 给定内容浏览器长路径（如 /Game/MyFolder 或 /PluginName/...），通过 Asset Registry 枚举该路径下的资产，输出软引用路径。
	 * bIncludeSubfolders 为 true 时包含所有子文件夹内的资产（FARFilter::bRecursivePaths）；为 false 时仅匹配该路径层级（不递归子路径）。
	 */
	UFUNCTION(BlueprintCallable, Category = "UCP|Asset|Editor")
	static void GatherAssetSoftPathsUnderContentFolderPath(
		UPARAM(DisplayName = "Content Folder Path") const FString& FolderPath,
		UPARAM(DisplayName = "Include Subfolders") bool bIncludeSubfolders,
		TArray<FSoftObjectPath>& OutSoftPaths);
};
