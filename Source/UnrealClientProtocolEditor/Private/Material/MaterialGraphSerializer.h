// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "NodeCode/NodeCodeTypes.h"

class UMaterial;
class UMaterialFunction;
class UMaterialExpression;
class UMaterialExpressionComposite;
struct FExpressionOutput;

class FMaterialGraphSerializer
{
public:
	static FString Serialize(UMaterial* Material, const FString& ScopeName);
	static FString Serialize(UMaterialFunction* MaterialFunction, const FString& ScopeName);

	static FNodeCodeGraphIR BuildIR(UMaterial* Material, const FString& ScopeName);
	static FNodeCodeGraphIR BuildIR(UMaterialFunction* MaterialFunction, const FString& ScopeName);

	static TArray<FString> ListScopes(UMaterial* Material);
	static TArray<FString> ListScopes(UMaterialFunction* MaterialFunction);

private:
	static FNodeCodeGraphIR BuildIRFromExpressions(
		TConstArrayView<TObjectPtr<UMaterialExpression>> AllExpressions,
		UMaterial* Material,
		UMaterialFunction* MaterialFunction,
		const FString& ScopeName);

	static void CollectReachableNodes(
		UMaterialExpression* StartExpr,
		TSet<UMaterialExpression*>& OutReachable,
		TSet<UMaterialExpression*>& Visited);

	static void SerializeNodeProperties(
		UMaterialExpression* Expression,
		TMap<FString, FString>& OutProperties);

	static FString GetOutputPinName(
		UMaterialExpression* Expression,
		int32 OutputIndex);

	static FString ShortenInputPinName(const FString& InputName);

	static bool IsConnectionProperty(const FProperty* Prop);
	static bool IsEmbeddedConnectionArrayProperty(const FProperty* Prop);

	static const TSet<FName>& GetBaseClassSkipProperties();
};
