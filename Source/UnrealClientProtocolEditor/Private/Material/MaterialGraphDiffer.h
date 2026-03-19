// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "NodeCode/NodeCodeTypes.h"

class UMaterial;
class UMaterialFunction;
class UMaterialExpression;

class FMaterialGraphDiffer
{
public:
	static FNodeCodeDiffResult Apply(UMaterial* Material, const FString& ScopeName, const FString& GraphText);
	static FNodeCodeDiffResult Apply(UMaterialFunction* MaterialFunction, const FString& ScopeName, const FString& GraphText);

private:
	static FNodeCodeDiffResult DiffAndApply(
		UMaterial* Material,
		UMaterialFunction* MaterialFunction,
		const FString& ScopeName,
		const FNodeCodeGraphIR& NewIR);

	static void MatchNodes(
		const FNodeCodeGraphIR& OldIR,
		const FNodeCodeGraphIR& NewIR,
		TMap<int32, int32>& OutNewToOld);

	static void ApplyPropertyChanges(
		UMaterialExpression* Expression,
		const TMap<FString, FString>& NewProperties,
		TArray<FString>& OutChanges);

	static EMaterialProperty FindMaterialPropertyByName(UMaterial* Material, const FString& Name);
};
