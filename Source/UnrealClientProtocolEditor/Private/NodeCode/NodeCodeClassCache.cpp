// MIT License - Copyright (c) 2025 Italink

#include "NodeCode/NodeCodeClassCache.h"
#include "UObject/UObjectHash.h"
#include "UObject/Class.h"

FNodeCodeClassCache::FNodeCodeClassCache(UClass* InBaseClass)
	: BaseClass(InBaseClass)
{
}

void FNodeCodeClassCache::Build()
{
	if (bBuilt)
	{
		return;
	}

	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(BaseClass, DerivedClasses, true);

	TMap<FName, TArray<UClass*>> NameCollisions;

	for (UClass* Class : DerivedClasses)
	{
		FName ShortName = Class->GetFName();
		NameCollisions.FindOrAdd(ShortName).Add(Class);
	}

	for (auto& Pair : NameCollisions)
	{
		if (Pair.Value.Num() == 1)
		{
			NameToClass.Add(Pair.Key, Pair.Value[0]);
		}
		else
		{
			AmbiguousNames.Add(Pair.Key);
			for (UClass* Class : Pair.Value)
			{
				FName FullPath(*Class->GetPathName());
				NameToClass.Add(FullPath, Class);
			}
		}
	}

	bBuilt = true;
}

UClass* FNodeCodeClassCache::FindClass(const FString& ClassName) const
{
	if (UClass* const* Found = NameToClass.Find(FName(*ClassName)))
	{
		return *Found;
	}

	UClass* FoundClass = UClass::TryFindTypeSlow<UClass>(ClassName, EFindFirstObjectOptions::ExactClass);
	if (FoundClass && FoundClass->IsChildOf(BaseClass))
	{
		return FoundClass;
	}

	return nullptr;
}

FString FNodeCodeClassCache::GetSerializableName(UClass* InClass) const
{
	if (!InClass)
	{
		return FString();
	}

	FName ShortName = InClass->GetFName();
	if (AmbiguousNames.Contains(ShortName))
	{
		return InClass->GetPathName();
	}

	return ShortName.ToString();
}
