// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"

struct FNodeCodeNodeIR
{
	int32 Index = -1;
	FString ClassName;
	FGuid Guid;
	TMap<FString, FString> Properties;
	UObject* SourceObject = nullptr;
};

struct FNodeCodeLinkIR
{
	int32 FromNodeIndex = -1;
	FString FromOutputName;
	int32 ToNodeIndex = -1;
	FString ToInputName;
	bool bToGraphOutput = false;
};

struct FNodeCodeGraphIR
{
	TArray<FNodeCodeNodeIR> Nodes;
	TArray<FNodeCodeLinkIR> Links;
	FString ScopeName;
};

namespace NodeCodeUtils
{
	inline FString EncodeSpaces(const FString& InStr)
	{
		return InStr.Replace(TEXT(" "), TEXT("_"));
	}

	inline bool MatchName(const FString& Encoded, const FString& Original)
	{
		if (Encoded == Original)
		{
			return true;
		}
		return Encoded.Replace(TEXT("_"), TEXT(" ")) == Original
			|| Encoded == Original.Replace(TEXT(" "), TEXT("_"));
	}
}

struct FNodeCodeDiffResult
{
	TArray<FString> NodesAdded;
	TArray<FString> NodesRemoved;
	TArray<FString> NodesModified;
	TArray<FString> LinksAdded;
	TArray<FString> LinksRemoved;
};
