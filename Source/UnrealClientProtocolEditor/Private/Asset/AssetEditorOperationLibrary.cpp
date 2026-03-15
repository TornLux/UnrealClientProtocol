// MIT License - Copyright (c) 2025 Italink

#include "Asset/AssetEditorOperationLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"

TScriptInterface<IAssetRegistry> UAssetEditorOperationLibrary::GetAssetRegistry()
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	return TScriptInterface<IAssetRegistry>(Cast<UObject>(&Registry));
}
