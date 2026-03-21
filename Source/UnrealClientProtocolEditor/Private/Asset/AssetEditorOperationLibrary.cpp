// MIT License - Copyright (c) 2025 Italink

#include "Asset/AssetEditorOperationLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"

TScriptInterface<IAssetRegistry> UAssetEditorOperationLibrary::GetAssetRegistry()
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	return TScriptInterface<IAssetRegistry>(Cast<UObject>(&Registry));
}

int32 UAssetEditorOperationLibrary::ForceDeleteAssets(const TArray<FString>& AssetPaths)
{
	TArray<UObject*> ObjectsToDelete;
	for (const FString& Path : AssetPaths)
	{
		UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *Path);
		if (Obj)
		{
			ObjectsToDelete.Add(Obj);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[UCP] ForceDeleteAssets: Could not load asset: %s"), *Path);
		}
	}

	if (ObjectsToDelete.Num() == 0)
	{
		return 0;
	}

	return ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);
}

bool UAssetEditorOperationLibrary::FixupReferencers(const TArray<FString>& AssetPaths)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<UObjectRedirector*> Redirectors;
	for (const FString& Path : AssetPaths)
	{
		FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(Path));
		if (AssetData.IsValid() && AssetData.IsRedirector())
		{
			UObject* Obj = AssetData.GetAsset();
			if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Obj))
			{
				Redirectors.Add(Redirector);
			}
		}
	}

	if (Redirectors.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UCP] FixupReferencers: No redirectors found in the provided paths"));
		return false;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.FixupReferencers(Redirectors, false);
	return true;
}
void UAssetEditorOperationLibrary::GatherDerivedClassPaths(const TArray<UClass*>& BaseClasses, const TSet<UClass*>& ExcludedClasses,
	TSet<FTopLevelAssetPath>& OutDerivedClassPaths)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	TArray<FTopLevelAssetPath> BaseClassPaths;
	for (auto Iter : BaseClasses)
	{
		if (Iter)
		{
			BaseClassPaths.Emplace(Iter->GetClassPathName());
		}
	}
	TSet<FTopLevelAssetPath> ExcludedClassPaths;
	for (auto Iter : ExcludedClasses)
	{
		if (Iter)
		{
			ExcludedClassPaths.Emplace(Iter->GetClassPathName());
		}
	}
	TSet<FTopLevelAssetPath> DerivedClassPaths;
	AssetRegistry.GetDerivedClassNames(BaseClassPaths, ExcludedClassPaths, DerivedClassPaths);
	auto ShouldSkipClass = [](const FTopLevelAssetPath& InPath)
	{
		FString AssetName = InPath.GetAssetName().ToString();
		return AssetName.StartsWith(TEXT("SKEL_")) || AssetName.StartsWith(TEXT("REINST_"));
	};
	for (auto& Iter : DerivedClassPaths)
	{
		if (!ShouldSkipClass(Iter))
		{
			OutDerivedClassPaths.Add(Iter);
		}
	}
}

void UAssetEditorOperationLibrary::GatherDerivedClasses(const TArray<UClass*>& BaseClasses, const TSet<UClass*>& ExcludedClasses,
	TSet<UClass*>& OutDerivedClass)
{
	auto ShouldSkipClass = [](const UClass* InClass)
	{
		constexpr EClassFlags InvalidClassFlags = CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract | CLASS_NewerVersionExists;
		return InClass->HasAnyClassFlags(InvalidClassFlags) || InClass->GetName().StartsWith(TEXT("SKEL_")) || InClass->GetName().StartsWith(TEXT("REINST_"));
	};
	GatherDerivedClassesWithFilter(BaseClasses, ExcludedClasses, OutDerivedClass, ShouldSkipClass);
}

void UAssetEditorOperationLibrary::GatherDerivedClassesWithFilter(const TArray<UClass*>& BaseClasses, const TSet<UClass*>& ExcludedClasses,
	TSet<UClass*>& OutDerivedClass, TFunction<bool(const UClass*)> ShouldSkipClassFilter)
{
	TSet<FTopLevelAssetPath> DerivedClassPaths;
	GatherDerivedClassPaths(BaseClasses, ExcludedClasses, DerivedClassPaths);

	for (auto Iter : DerivedClassPaths)
	{
		UClass* Class = FindObject<UClass>(nullptr, *Iter.ToString());
		if (!Class)
		{
			FString AssetPath = Iter.ToString();
			if (AssetPath.EndsWith(TEXT("_C")))
			{
				AssetPath.LeftChopInline(2);
				UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
				if (Blueprint && Blueprint->GeneratedClass)
				{
					Class = Blueprint->GeneratedClass;
				}
			}
		}

		if (Class)
		{
			if (!ShouldSkipClassFilter(Class))
			{
				OutDerivedClass.Add(Class);
			}
		}
	}
}

void UAssetEditorOperationLibrary::GatherDataAssetsByBaseClass(TSubclassOf<UDataAsset> BaseDataAssetClass, TArray<FAssetData>& OutAssetDatas)
{
	OutAssetDatas.Reset();
	UClass* BaseClass = BaseDataAssetClass.Get();
	if (!BaseClass || !BaseClass->IsChildOf(UDataAsset::StaticClass()))
	{
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(BaseClass->GetClassPathName());
	Filter.bRecursiveClasses = true;

	AssetRegistry.GetAssets(Filter, OutAssetDatas);
}

void UAssetEditorOperationLibrary::GatherDataAssetsByAssetPath(const FString& DataAssetPath, TArray<FAssetData>& OutAssetDatas)
{
	OutAssetDatas.Reset();

	FString ObjectPath = DataAssetPath.TrimStartAndEnd();
	int32 QuoteStart = INDEX_NONE;
	if (ObjectPath.FindChar(TEXT('\''), QuoteStart))
	{
		int32 QuoteEnd = INDEX_NONE;
		if (ObjectPath.FindLastChar(TEXT('\''), QuoteEnd) && QuoteEnd > QuoteStart)
		{
			ObjectPath = ObjectPath.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
		}
	}

	UClass* BaseClass = nullptr;

	UBlueprint* LoadedBP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ObjectPath));
	if (LoadedBP && LoadedBP->GeneratedClass && LoadedBP->GeneratedClass->IsChildOf(UDataAsset::StaticClass()))
	{
		BaseClass = LoadedBP->GeneratedClass;
	}

	if (!BaseClass)
	{
		UDataAsset* LoadedDA = Cast<UDataAsset>(StaticLoadObject(UDataAsset::StaticClass(), nullptr, *ObjectPath));
		if (LoadedDA)
		{
			BaseClass = LoadedDA->GetClass();
		}
	}

	if (BaseClass)
	{
		GatherDataAssetsByBaseClass(BaseClass, OutAssetDatas);
	}
}

void UAssetEditorOperationLibrary::GatherDataAssetSoftPathsByBaseClass(TSubclassOf<UDataAsset> BaseDataAssetClass, TArray<FSoftObjectPath>& OutSoftPaths)
{
	OutSoftPaths.Reset();
	TArray<FAssetData> AssetDatas;
	GatherDataAssetsByBaseClass(BaseDataAssetClass, AssetDatas);
	for (const FAssetData& Data : AssetDatas)
	{
		OutSoftPaths.Add(Data.GetSoftObjectPath());
	}
}

void UAssetEditorOperationLibrary::GatherDataAssetSoftPathsByAssetPath(const FString& DataAssetPath, TArray<FSoftObjectPath>& OutSoftPaths)
{
	OutSoftPaths.Reset();

	FString ObjectPath = DataAssetPath.TrimStartAndEnd();
	int32 QuoteStart = INDEX_NONE;
	if (ObjectPath.FindChar(TEXT('\''), QuoteStart))
	{
		int32 QuoteEnd = INDEX_NONE;
		if (ObjectPath.FindLastChar(TEXT('\''), QuoteEnd) && QuoteEnd > QuoteStart)
		{
			ObjectPath = ObjectPath.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
		}
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
	if (!AssetData.IsValid())
	{
		return;
	}

	FTopLevelAssetPath BaseClassPath;

	FString GeneratedClassPathStr;
	if (AssetData.GetTagValue(FName("GeneratedClass"), GeneratedClassPathStr) && !GeneratedClassPathStr.IsEmpty())
	{
		BaseClassPath = FTopLevelAssetPath(GeneratedClassPathStr);
		if (!BaseClassPath.IsValid())
		{
			return;
		}
	}
	else
	{
		BaseClassPath = AssetData.AssetClassPath;
	}

	if (!BaseClassPath.IsValid())
	{
		return;
	}

	FARFilter Filter;
	Filter.ClassPaths.Add(BaseClassPath);
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> AssetDatas;
	AssetRegistry.GetAssets(Filter, AssetDatas);

	for (const FAssetData& Data : AssetDatas)
	{
		OutSoftPaths.Add(Data.GetSoftObjectPath());
	}
}

namespace UcpAssetEditorOps
{
	void ParsePathToObjectPath(const FString& InPath, FString& OutObjectPath)
	{
		OutObjectPath = InPath.TrimStartAndEnd();
		int32 QuoteStart = INDEX_NONE;
		if (OutObjectPath.FindChar(TEXT('\''), QuoteStart))
		{
			int32 QuoteEnd = INDEX_NONE;
			if (OutObjectPath.FindLastChar(TEXT('\''), QuoteEnd) && QuoteEnd > QuoteStart)
			{
				OutObjectPath = OutObjectPath.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
			}
		}
	}

	UClass* ResolveBlueprintPathToClass(const FString& BlueprintClassPath)
	{
		FString ObjectPath;
		ParsePathToObjectPath(BlueprintClassPath, ObjectPath);

		UClass* BaseClass = nullptr;
		UBlueprint* LoadedBP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ObjectPath));
		if (LoadedBP && LoadedBP->GeneratedClass)
		{
			BaseClass = LoadedBP->GeneratedClass;
		}
		if (!BaseClass)
		{
			BaseClass = FindObject<UClass>(nullptr, *ObjectPath);
		}
		if (!BaseClass && !ObjectPath.EndsWith(TEXT("_C")))
		{
			BaseClass = FindObject<UClass>(nullptr, *(ObjectPath + TEXT("_C")));
		}
		if (!BaseClass && !ObjectPath.EndsWith(TEXT("_C")))
		{
			BaseClass = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C")));
		}
		return BaseClass;
	}

	FSoftObjectPath ConvertAssetIdentifierToSoftObjectPath(const FAssetIdentifier& AssetIdentifier)
	{
		if (!AssetIdentifier.IsValid())
		{
			return FSoftObjectPath();
		}
		if (AssetIdentifier.PrimaryAssetType.IsValid())
		{
			return FSoftObjectPath(AssetIdentifier.ToString());
		}
		if (AssetIdentifier.IsPackage())
		{
			const FString PackageStr = AssetIdentifier.PackageName.ToString();
			const FString ShortName = FPackageName::GetShortName(AssetIdentifier.PackageName);
			return FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *PackageStr, *ShortName));
		}
		return FSoftObjectPath(AssetIdentifier.ToString());
	}

	void GatherAssetReferencesImpl(IAssetRegistry& AssetRegistry, const FSoftObjectPath& AssetSoftPath, TArray<FSoftObjectPath>& OutDependencies,
		TArray<FSoftObjectPath>& OutReferencers)
	{
		OutDependencies.Reset();
		OutReferencers.Reset();
		if (!AssetSoftPath.IsValid())
		{
			return;
		}

		const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(AssetSoftPath);
		if (!AssetData.IsValid())
		{
			return;
		}

		FAssetIdentifier GraphId(AssetData.PackageName);
		TArray<FAssetIdentifier> Dependencies;
		TArray<FAssetIdentifier> Referencers;
		if (!AssetRegistry.GetDependencies(GraphId, Dependencies, UE::AssetRegistry::EDependencyCategory::All, UE::AssetRegistry::FDependencyQuery()))
		{
			GraphId = FAssetIdentifier(AssetData.PackageName, AssetData.AssetName);
			AssetRegistry.GetDependencies(GraphId, Dependencies, UE::AssetRegistry::EDependencyCategory::All, UE::AssetRegistry::FDependencyQuery());
		}
		AssetRegistry.GetReferencers(GraphId, Referencers, UE::AssetRegistry::EDependencyCategory::All, UE::AssetRegistry::FDependencyQuery());

		TSet<FSoftObjectPath> SeenDeps;
		TSet<FSoftObjectPath> SeenRefs;

		for (const FAssetIdentifier& Id : Dependencies)
		{
			const FSoftObjectPath SoftPath = ConvertAssetIdentifierToSoftObjectPath(Id);
			if (SoftPath.IsValid() && !SeenDeps.Contains(SoftPath))
			{
				SeenDeps.Add(SoftPath);
				OutDependencies.Add(SoftPath);
			}
		}

		for (const FAssetIdentifier& Id : Referencers)
		{
			const FSoftObjectPath SoftPath = ConvertAssetIdentifierToSoftObjectPath(Id);
			if (SoftPath.IsValid() && !SeenRefs.Contains(SoftPath))
			{
				SeenRefs.Add(SoftPath);
				OutReferencers.Add(SoftPath);
			}
		}
	}

	bool ResolveBlueprintPathToClassPath(const FString& BlueprintClassPath, FTopLevelAssetPath& OutClassPath)
	{
		FString ObjectPath;
		ParsePathToObjectPath(BlueprintClassPath, ObjectPath);

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));

		if (AssetData.IsValid())
		{
			FString GeneratedClassPathStr;
			if (AssetData.GetTagValue(FName("GeneratedClass"), GeneratedClassPathStr) && !GeneratedClassPathStr.IsEmpty())
			{
				OutClassPath = FTopLevelAssetPath(GeneratedClassPathStr);
				return OutClassPath.IsValid();
			}
		}

		OutClassPath = FTopLevelAssetPath(BlueprintClassPath.TrimStartAndEnd());
		return OutClassPath.IsValid();
	}
}

void UAssetEditorOperationLibrary::GatherAssetReferencesByAssetPath(const FString& AssetPath, TArray<FSoftObjectPath>& OutDependencies,
	TArray<FSoftObjectPath>& OutReferencers)
{
	FString ObjectPath;
	UcpAssetEditorOps::ParsePathToObjectPath(AssetPath, ObjectPath);
	GatherAssetReferencesBySoftPath(FSoftObjectPath(ObjectPath), OutDependencies, OutReferencers);
}

void UAssetEditorOperationLibrary::GatherAssetReferencesBySoftPath(const FSoftObjectPath& SoftObjectPath, TArray<FSoftObjectPath>& OutDependencies,
	TArray<FSoftObjectPath>& OutReferencers)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	UcpAssetEditorOps::GatherAssetReferencesImpl(AssetRegistry, SoftObjectPath, OutDependencies, OutReferencers);
}

void UAssetEditorOperationLibrary::GatherDerivedClassesByBlueprintPath(const FString& BlueprintClassPath, TArray<UClass*>& OutDerivedClasses)
{
	OutDerivedClasses.Reset();
	UClass* BaseClass = UcpAssetEditorOps::ResolveBlueprintPathToClass(BlueprintClassPath);
	if (!BaseClass)
	{
		return;
	}

	TArray<UClass*> BaseArr;
	BaseArr.Add(BaseClass);
	TSet<UClass*> DerivedSet;
	auto ShouldSkip = [](const UClass* InClass)
	{
		constexpr EClassFlags InvalidClassFlags = CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract | CLASS_NewerVersionExists;
		return InClass->HasAnyClassFlags(InvalidClassFlags) || InClass->GetName().StartsWith(TEXT("SKEL_")) || InClass->GetName().StartsWith(TEXT("REINST_"));
	};
	GatherDerivedClassesWithFilter(BaseArr, TSet<UClass*>(), DerivedSet, ShouldSkip);

	for (UClass* C : DerivedSet)
	{
		OutDerivedClasses.Add(C);
	}
}

void UAssetEditorOperationLibrary::GatherDerivedClassSoftPathsByBlueprintPath(const FString& BlueprintClassPath, TArray<FSoftObjectPath>& OutDerivedClassSoftPaths)
{
	OutDerivedClassSoftPaths.Reset();

	FTopLevelAssetPath BaseClassPath;
	if (!UcpAssetEditorOps::ResolveBlueprintPathToClassPath(BlueprintClassPath, BaseClassPath))
	{
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FTopLevelAssetPath> BaseClassPaths;
	BaseClassPaths.Add(BaseClassPath);
	TSet<FTopLevelAssetPath> ExcludedClassPaths;
	TSet<FTopLevelAssetPath> DerivedClassPaths;
	AssetRegistry.GetDerivedClassNames(BaseClassPaths, ExcludedClassPaths, DerivedClassPaths);

	auto ShouldSkipClass = [](const FTopLevelAssetPath& InPath)
	{
		FString AssetName = InPath.GetAssetName().ToString();
		return AssetName.StartsWith(TEXT("SKEL_")) || AssetName.StartsWith(TEXT("REINST_"));
	};

	for (const FTopLevelAssetPath& Path : DerivedClassPaths)
	{
		if (!ShouldSkipClass(Path))
		{
			OutDerivedClassSoftPaths.Add(FSoftObjectPath(Path.ToString()));
		}
	}
}

void UAssetEditorOperationLibrary::GatherImmediateSubFolderPaths(const FString& FolderPath, TArray<FString>& OutSubFolderPaths)
{
	OutSubFolderPaths.Reset();
	FString BasePath = FolderPath.TrimStartAndEnd();
	if (BasePath.IsEmpty())
	{
		return;
	}
	while (BasePath.Len() > 1 && BasePath.EndsWith(TEXT("/"), ESearchCase::IgnoreCase))
	{
		BasePath.LeftChopInline(1);
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.GetSubPaths(BasePath, OutSubFolderPaths, false);
}

void UAssetEditorOperationLibrary::GatherImmediateSubFolderNames(const FName& FolderPath, TArray<FName>& OutSubFolderNames)
{
	OutSubFolderNames.Reset();
	if (FolderPath.IsNone())
	{
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.GetSubPaths(FolderPath, OutSubFolderNames, false);
}

namespace UcpAssetSearchQuery
{
	struct FParsedQuery
	{
		TArray<FString> IncludeTokens;
		TArray<FString> ExcludeTokens;
		FString TypeFilter;
	};

	static bool ParseQuery(const FString& Query, FParsedQuery& OutParsed)
	{
		OutParsed.IncludeTokens.Reset();
		OutParsed.ExcludeTokens.Reset();
		OutParsed.TypeFilter.Reset();

		FString Trimmed = Query.TrimStartAndEnd();
		TArray<FString> Tokens;
		Trimmed.ParseIntoArray(Tokens, TEXT(" "), true);

		for (FString& Token : Tokens)
		{
			Token.TrimStartAndEndInline();
			if (Token.IsEmpty()) continue;

			if (Token.StartsWith(TEXT("!")))
			{
				FString Exclude = Token.Mid(1).TrimStartAndEnd();
				if (!Exclude.IsEmpty())
				{
					OutParsed.ExcludeTokens.Add(MoveTemp(Exclude));
				}
			}
			else if (Token.StartsWith(TEXT("&Type="), ESearchCase::IgnoreCase))
			{
				OutParsed.TypeFilter = Token.Mid(6).TrimStartAndEnd();
			}
			else
			{
				OutParsed.IncludeTokens.Add(MoveTemp(Token));
			}
		}

		return OutParsed.IncludeTokens.Num() > 0 || !OutParsed.TypeFilter.IsEmpty();
	}

	static bool MatchesQuery(const FString& AssetName, const FString& Query, bool bCaseSensitive, bool bWholeWord)
	{
		FString Name = AssetName;
		FString Q = Query;
		if (!bCaseSensitive)
		{
			Name.ToLowerInline();
			Q.ToLowerInline();
		}
		if (bWholeWord)
		{
			int32 Idx = 0;
			while (Idx < Name.Len())
			{
				Idx = Name.Find(Q, ESearchCase::IgnoreCase, ESearchDir::FromStart, Idx);
				if (Idx == INDEX_NONE) return false;
				bool StartOK = (Idx == 0) || !FChar::IsAlnum(Name[Idx - 1]);
				bool EndOK = (Idx + Q.Len() >= Name.Len()) || !FChar::IsAlnum(Name[Idx + Q.Len()]);
				if (StartOK && EndOK) return true;
				Idx++;
			}
			return false;
		}
		return Name.Contains(Q);
	}

	static bool MatchesParsedQuery(const FString& AssetName, const FParsedQuery& Parsed, bool bCaseSensitive, bool bWholeWord)
	{
		for (const FString& Token : Parsed.IncludeTokens)
		{
			if (Token.IsEmpty()) continue;
			if (!MatchesQuery(AssetName, Token, bCaseSensitive, bWholeWord))
			{
				return false;
			}
		}
		for (const FString& Token : Parsed.ExcludeTokens)
		{
			if (Token.IsEmpty()) continue;
			if (MatchesQuery(AssetName, Token, bCaseSensitive, bWholeWord))
			{
				return false;
			}
		}
		return true;
	}

	static bool MatchesTypeFilter(const FAssetData& Data, const FString& TypeFilter)
	{
		if (TypeFilter.IsEmpty()) return true;
		FString ClassName = Data.AssetClassPath.GetAssetName().ToString();
		return ClassName.Contains(TypeFilter, ESearchCase::IgnoreCase);
	}

	static void NormalizeContentRoot(FString& Path)
	{
		Path.TrimStartAndEndInline();
		while (Path.Len() > 1 && Path.EndsWith(TEXT("/"), ESearchCase::IgnoreCase))
		{
			Path.LeftChopInline(1);
		}
		if (!Path.IsEmpty() && !Path.StartsWith(TEXT("/")))
		{
			Path = TEXT("/") + Path;
		}
	}

	static void SearchAssetsInternal(IAssetRegistry& Registry, const FParsedQuery& Parsed, EUCPAssetSearchScope Scope, const FString& InCustomPackagePath,
		const FString& ClassFilter, bool bCaseSensitive, bool bWholeWord, int32 MaxResults, TArray<FAssetData>& OutResults)
	{
		FARFilter Filter;
		Filter.bRecursivePaths = true;

		switch (Scope)
		{
		case EUCPAssetSearchScope::AllAssets:
		{
			TArray<FString> RootPaths;
			FPackageName::QueryRootContentPaths(RootPaths, false, false, true);
			for (const FString& Root : RootPaths)
			{
				FString Path = Root;
				if (!Path.StartsWith(TEXT("/"))) Path = TEXT("/") + Path;
				if (!Path.IsEmpty())
				{
					Filter.PackagePaths.Add(FName(*Path));
				}
			}
			if (Filter.PackagePaths.Num() == 0)
			{
				Filter.PackagePaths.Add(FName("/Game"));
				Filter.PackagePaths.Add(FName("/Engine"));
			}
			break;
		}
		case EUCPAssetSearchScope::Project:
			Filter.PackagePaths.Add(FName("/Game"));
			break;
		case EUCPAssetSearchScope::CustomPackagePath:
		{
			FString Path = InCustomPackagePath;
			NormalizeContentRoot(Path);
			if (Path.IsEmpty())
			{
				Filter.PackagePaths.Add(FName("/Game"));
			}
			else
			{
				Filter.PackagePaths.Add(FName(*Path));
			}
			break;
		}
		default:
			Filter.PackagePaths.Add(FName("/Game"));
			break;
		}

		if (!ClassFilter.IsEmpty() && ClassFilter != TEXT("*") && ClassFilter.StartsWith(TEXT("/Script/")))
		{
			Filter.ClassPaths.Add(FTopLevelAssetPath(ClassFilter));
			Filter.bRecursiveClasses = true;
		}

		TArray<FAssetData> AllCandidates;
		Registry.GetAssets(Filter, AllCandidates);

		for (const FAssetData& Data : AllCandidates)
		{
			if (OutResults.Num() >= MaxResults) break;
			FString AssetName = Data.AssetName.ToString();
			if (!MatchesParsedQuery(AssetName, Parsed, bCaseSensitive, bWholeWord))
			{
				continue;
			}
			if (!MatchesTypeFilter(Data, Parsed.TypeFilter))
			{
				continue;
			}
			OutResults.Add(Data);
		}
	}
}

void UAssetEditorOperationLibrary::GatherAssetSoftPathsBySearchQuery(const FString& Query, EUCPAssetSearchScope Scope, const FString& ClassFilter,
	bool bCaseSensitive, bool bWholeWord, int32 MaxResults, int32 MinCharacters, const FString& CustomPackagePath,
	TArray<FSoftObjectPath>& OutSoftPaths, TArray<FString>& OutIncludeTokensForHighlight)
{
	using namespace UcpAssetSearchQuery;

	OutSoftPaths.Reset();
	OutIncludeTokensForHighlight.Reset();

	FString Trimmed = Query.TrimStartAndEnd();
	FParsedQuery Parsed;
	if (!ParseQuery(Trimmed, Parsed))
	{
		return;
	}
	if (Parsed.IncludeTokens.Num() == 0 && Parsed.TypeFilter.IsEmpty())
	{
		return;
	}
	if (Parsed.IncludeTokens.Num() > 0)
	{
		int32 MinLen = Parsed.IncludeTokens[0].Len();
		for (const FString& T : Parsed.IncludeTokens)
		{
			if (T.Len() < MinLen) MinLen = T.Len();
		}
		if (MinLen < MinCharacters)
		{
			return;
		}
	}

	OutIncludeTokensForHighlight = Parsed.IncludeTokens;

	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = Module.Get();

	TArray<FAssetData> Results;
	SearchAssetsInternal(Registry, Parsed, Scope, CustomPackagePath, ClassFilter, bCaseSensitive, bWholeWord, MaxResults, Results);

	OutSoftPaths.Reserve(Results.Num());
	for (const FAssetData& Data : Results)
	{
		OutSoftPaths.Add(Data.GetSoftObjectPath());
	}
}

void UAssetEditorOperationLibrary::GatherAssetSoftPathsBySearchQueryInAllContent(const FString& Query, int32 MaxResults,
	TArray<FSoftObjectPath>& OutSoftPaths, TArray<FString>& OutIncludeTokensForHighlight)
{
	GatherAssetSoftPathsBySearchQuery(Query, EUCPAssetSearchScope::AllAssets, FString(), false, false, MaxResults, 1, FString(), OutSoftPaths,
		OutIncludeTokensForHighlight);
}

void UAssetEditorOperationLibrary::GatherAssetSoftPathsUnderContentFolderPath(const FString& FolderPath, bool bIncludeSubfolders,
	TArray<FSoftObjectPath>& OutSoftPaths)
{
	OutSoftPaths.Reset();
	FString BasePath = FolderPath.TrimStartAndEnd();
	if (BasePath.IsEmpty())
	{
		return;
	}
	while (BasePath.Len() > 1 && BasePath.EndsWith(TEXT("/"), ESearchCase::IgnoreCase))
	{
		BasePath.LeftChopInline(1);
	}
	if (!BasePath.StartsWith(TEXT("/")))
	{
		BasePath = TEXT("/") + BasePath;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*BasePath));
	Filter.bRecursivePaths = bIncludeSubfolders;

	TArray<FAssetData> AssetDatas;
	AssetRegistry.GetAssets(Filter, AssetDatas);

	OutSoftPaths.Reserve(AssetDatas.Num());
	for (const FAssetData& Data : AssetDatas)
	{
		OutSoftPaths.Add(Data.GetSoftObjectPath());
	}
}
