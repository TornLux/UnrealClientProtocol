// MIT License - Copyright (c) 2025 Italink

#include "Material/MaterialGraphEditingLibrary.h"
#include "Material/MaterialGraphSerializer.h"
#include "Material/MaterialGraphDiffer.h"

#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "MaterialEditingLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogMaterialGraphEditing, Log, All);

static UObject* LoadMaterialAsset(const FString& AssetPath)
{
	return StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
}

FString UMaterialGraphEditingLibrary::ReadGraph(const FString& AssetPath, const FString& ScopeName)
{
	UObject* Asset = LoadMaterialAsset(AssetPath);
	if (!Asset)
	{
		UE_LOG(LogMaterialGraphEditing, Error, TEXT("ReadGraph: Asset not found: %s"), *AssetPath);
		return FString();
	}

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		return FMaterialGraphSerializer::Serialize(Material, ScopeName);
	}

	if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset))
	{
		return FMaterialGraphSerializer::Serialize(MaterialFunction, ScopeName);
	}

	UE_LOG(LogMaterialGraphEditing, Error, TEXT("ReadGraph: Asset is not a Material or MaterialFunction: %s"), *AssetPath);
	return FString();
}

FString UMaterialGraphEditingLibrary::WriteGraph(const FString& AssetPath, const FString& ScopeName, const FString& GraphText)
{
	UObject* Asset = LoadMaterialAsset(AssetPath);
	if (!Asset)
	{
		UE_LOG(LogMaterialGraphEditing, Error, TEXT("WriteGraph: Asset not found: %s"), *AssetPath);
		return FString();
	}

	FMGDiffResult Result;

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		Result = FMaterialGraphDiffer::Apply(Material, ScopeName, GraphText);
	}
	else if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset))
	{
		Result = FMaterialGraphDiffer::Apply(MaterialFunction, ScopeName, GraphText);
	}
	else
	{
		UE_LOG(LogMaterialGraphEditing, Error, TEXT("WriteGraph: Asset is not a Material or MaterialFunction: %s"), *AssetPath);
		return FString();
	}

	return FMaterialGraphDiffer::DiffResultToJson(Result);
}

FString UMaterialGraphEditingLibrary::Relayout(const FString& AssetPath)
{
	UObject* Asset = LoadMaterialAsset(AssetPath);
	if (!Asset)
	{
		UE_LOG(LogMaterialGraphEditing, Error, TEXT("Relayout: Asset not found: %s"), *AssetPath);
		return FString();
	}

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
	}
	else if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset))
	{
		UMaterialEditingLibrary::LayoutMaterialFunctionExpressions(MaterialFunction);
	}
	else
	{
		UE_LOG(LogMaterialGraphEditing, Error, TEXT("Relayout: Asset is not a Material or MaterialFunction: %s"), *AssetPath);
		return FString();
	}

	return FString();
}

FString UMaterialGraphEditingLibrary::ListScopes(const FString& AssetPath)
{
	UObject* Asset = LoadMaterialAsset(AssetPath);
	if (!Asset)
	{
		UE_LOG(LogMaterialGraphEditing, Error, TEXT("ListScopes: Asset not found: %s"), *AssetPath);
		return FString();
	}

	TArray<FString> Scopes;

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		Scopes = FMaterialGraphSerializer::ListScopes(Material);
	}
	else if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset))
	{
		Scopes = FMaterialGraphSerializer::ListScopes(MaterialFunction);
	}
	else
	{
		UE_LOG(LogMaterialGraphEditing, Error, TEXT("ListScopes: Asset is not a Material or MaterialFunction: %s"), *AssetPath);
		return FString();
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ScopeValues;
	for (const FString& Scope : Scopes)
	{
		ScopeValues.Add(MakeShared<FJsonValueString>(Scope));
	}
	Root->SetArrayField(TEXT("scopes"), ScopeValues);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputString;
}
