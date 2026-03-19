// MIT License - Copyright (c) 2025 Italink

#include "Material/MaterialExpressionClassCache.h"
#include "Materials/MaterialExpression.h"

FMaterialExpressionClassCache::FMaterialExpressionClassCache()
	: Cache(UMaterialExpression::StaticClass())
{
}

FMaterialExpressionClassCache& FMaterialExpressionClassCache::Get()
{
	static FMaterialExpressionClassCache Instance;
	return Instance;
}

void FMaterialExpressionClassCache::Build()
{
	Cache.Build();
}

UClass* FMaterialExpressionClassCache::FindClass(const FString& ClassName) const
{
	return Cache.FindClass(ClassName);
}

FString FMaterialExpressionClassCache::GetSerializableName(UClass* InClass) const
{
	return Cache.GetSerializableName(InClass);
}
