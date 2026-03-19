// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "NodeCode/NodeCodeTypes.h"

class FNodeCodeTextFormat
{
public:
	static FString IRToText(const FNodeCodeGraphIR& IR);
	static FNodeCodeGraphIR ParseText(const FString& GraphText);
	static FString DiffResultToJson(const FNodeCodeDiffResult& Result);
};
