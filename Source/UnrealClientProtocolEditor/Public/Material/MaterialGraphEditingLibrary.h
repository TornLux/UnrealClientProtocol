// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MaterialGraphEditingLibrary.generated.h"

UCLASS()
class UMaterialGraphEditingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UCP|Material")
	static FString ReadGraph(const FString& AssetPath, const FString& ScopeName);

	UFUNCTION(BlueprintCallable, Category = "UCP|Material")
	static FString WriteGraph(const FString& AssetPath, const FString& ScopeName, const FString& GraphText);

	UFUNCTION(BlueprintCallable, Category = "UCP|Material")
	static FString Relayout(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UCP|Material")
	static FString ListScopes(const FString& AssetPath);
};
