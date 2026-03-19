// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"

class FNodeCodeClassCache
{
public:
	FNodeCodeClassCache(UClass* InBaseClass);

	void Build();

	UClass* FindClass(const FString& ClassName) const;

	FString GetSerializableName(UClass* InClass) const;

private:
	UClass* BaseClass;
	TMap<FName, UClass*> NameToClass;
	TSet<FName> AmbiguousNames;
	bool bBuilt = false;
};
