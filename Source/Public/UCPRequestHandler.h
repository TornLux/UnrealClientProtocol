// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FUCPRequestHandler
{
public:
	TSharedPtr<FJsonObject> HandleRequest(const TSharedPtr<FJsonObject>& Request);

private:
	TSharedPtr<FJsonObject> DispatchSingle(const TSharedPtr<FJsonObject>& Request);
	TSharedPtr<FJsonObject> ExecBatch(const TSharedPtr<FJsonObject>& Request);
	TSharedPtr<FJsonObject> CallUFunction(const TSharedPtr<FJsonObject>& Request);
	TSharedPtr<FJsonObject> GetUProperty(const TSharedPtr<FJsonObject>& Request);
	TSharedPtr<FJsonObject> SetUProperty(const TSharedPtr<FJsonObject>& Request);
	TSharedPtr<FJsonObject> Describe(const TSharedPtr<FJsonObject>& Request);
	TSharedPtr<FJsonObject> FindUObjects(const TSharedPtr<FJsonObject>& Request);

	TSharedPtr<FJsonObject> MakeError(const FString& Id, const FString& Error);
	void CopyIdField(const TSharedPtr<FJsonObject>& From, const TSharedPtr<FJsonObject>& To);
};
