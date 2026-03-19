// MIT License - Copyright (c) 2025 Italink

#include "Material/MaterialGraphDiffer.h"
#include "Material/MaterialExpressionClassCache.h"
#include "Material/MaterialGraphSerializer.h"
#include "NodeCode/NodeCodeTextFormat.h"

#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionRerouteBase.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSwitch.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "MaterialEditingLibrary.h"
#include "UObject/UnrealType.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "IMaterialEditor.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"

DEFINE_LOG_CATEGORY_STATIC(LogUCP, Log, All);

struct FMatLink
{
	UMaterialExpression* FromExpr = nullptr;
	int32 OutputIndex = 0;
	FExpressionInput* ToInput = nullptr;

	bool operator==(const FMatLink& Other) const
	{
		return FromExpr == Other.FromExpr && OutputIndex == Other.OutputIndex && ToInput == Other.ToInput;
	}
	friend uint32 GetTypeHash(const FMatLink& L)
	{
		return HashCombine(HashCombine(::GetTypeHash(L.FromExpr), ::GetTypeHash(L.OutputIndex)), ::GetTypeHash(L.ToInput));
	}
};

// ---- Diff & Apply ----

FNodeCodeDiffResult FMaterialGraphDiffer::Apply(UMaterial* Material, const FString& ScopeName, const FString& GraphText)
{
	if (!Material)
	{
		FNodeCodeDiffResult R;
		UE_LOG(LogUCP, Error, TEXT("WriteGraph: Material is null"));
		return R;
	}

	FNodeCodeGraphIR NewIR = FNodeCodeTextFormat::ParseText(GraphText);
	return DiffAndApply(Material, nullptr, ScopeName, NewIR);
}

FNodeCodeDiffResult FMaterialGraphDiffer::Apply(UMaterialFunction* MaterialFunction, const FString& ScopeName, const FString& GraphText)
{
	if (!MaterialFunction)
	{
		FNodeCodeDiffResult R;
		UE_LOG(LogUCP, Error, TEXT("WriteGraph: MaterialFunction is null"));
		return R;
	}

	FNodeCodeGraphIR NewIR = FNodeCodeTextFormat::ParseText(GraphText);
	return DiffAndApply(nullptr, MaterialFunction, ScopeName, NewIR);
}

void FMaterialGraphDiffer::MatchNodes(
	const FNodeCodeGraphIR& OldIR,
	const FNodeCodeGraphIR& NewIR,
	TMap<int32, int32>& OutNewToOld)
{
	TMap<FGuid, int32> OldGuidMap;
	for (int32 i = 0; i < OldIR.Nodes.Num(); ++i)
	{
		if (OldIR.Nodes[i].Guid.IsValid())
		{
			OldGuidMap.Add(OldIR.Nodes[i].Guid, i);
		}
	}

	TSet<int32> MatchedOld;

	auto GetKeyProp = [](const FNodeCodeNodeIR& N) -> FString
	{
		if (const FString* Val = N.Properties.Find(TEXT("ParameterName")))
		{
			return *Val;
		}
		if (const FString* Val = N.Properties.Find(TEXT("Texture")))
		{
			return *Val;
		}
		if (const FString* Val = N.Properties.Find(TEXT("MaterialFunction")))
		{
			return *Val;
		}
		return FString();
	};

	// Pass 1: Match by Guid
	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		const FNodeCodeNodeIR& NewNode = NewIR.Nodes[NewIdx];
		if (NewNode.Guid.IsValid())
		{
			if (int32* OldIdx = OldGuidMap.Find(NewNode.Guid))
			{
				if (!MatchedOld.Contains(*OldIdx))
				{
					OutNewToOld.Add(NewIdx, *OldIdx);
					MatchedOld.Add(*OldIdx);
				}
			}
		}
	}

	// Pass 2: Match by ClassName + KeyProp
	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		if (OutNewToOld.Contains(NewIdx))
		{
			continue;
		}

		const FNodeCodeNodeIR& NewNode = NewIR.Nodes[NewIdx];
		FString NewKey = GetKeyProp(NewNode);
		if (NewKey.IsEmpty())
		{
			continue;
		}

		for (int32 OldIdx = 0; OldIdx < OldIR.Nodes.Num(); ++OldIdx)
		{
			if (MatchedOld.Contains(OldIdx))
			{
				continue;
			}

			const FNodeCodeNodeIR& OldNode = OldIR.Nodes[OldIdx];
			if (OldNode.ClassName == NewNode.ClassName && GetKeyProp(OldNode) == NewKey)
			{
				OutNewToOld.Add(NewIdx, OldIdx);
				MatchedOld.Add(OldIdx);
				break;
			}
		}
	}

	// Pass 3: Match remaining by ClassName alone
	TMultiMap<FString, int32> UnmatchedOldByClass;
	for (int32 OldIdx = 0; OldIdx < OldIR.Nodes.Num(); ++OldIdx)
	{
		if (!MatchedOld.Contains(OldIdx))
		{
			UnmatchedOldByClass.Add(OldIR.Nodes[OldIdx].ClassName, OldIdx);
		}
	}

	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		if (OutNewToOld.Contains(NewIdx))
		{
			continue;
		}

		const FString& ClassName = NewIR.Nodes[NewIdx].ClassName;
		for (auto It = UnmatchedOldByClass.CreateKeyIterator(ClassName); It; ++It)
		{
			int32 OldIdx = It.Value();
			OutNewToOld.Add(NewIdx, OldIdx);
			MatchedOld.Add(OldIdx);
			It.RemoveCurrent();
			break;
		}
	}
}

void FMaterialGraphDiffer::ApplyPropertyChanges(
	UMaterialExpression* Expression,
	const TMap<FString, FString>& NewProperties,
	TArray<FString>& OutChanges)
{
	UClass* ExprClass = Expression->GetClass();

	if (UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expression))
	{
		if (const FString* InputNamesStr = NewProperties.Find(TEXT("InputNames")))
		{
			FString Working = *InputNamesStr;
			Working.TrimStartAndEndInline();
			if (Working.StartsWith(TEXT("[")) && Working.EndsWith(TEXT("]")))
			{
				Working = Working.Mid(1, Working.Len() - 2);
			}

			TArray<FString> Names;
			Working.ParseIntoArray(Names, TEXT(","));

			CustomExpr->Inputs.Empty();
			for (FString& Name : Names)
			{
				Name.TrimStartAndEndInline();
				if (Name.StartsWith(TEXT("\"")) && Name.EndsWith(TEXT("\"")))
				{
					Name = Name.Mid(1, Name.Len() - 2);
				}
				FCustomInput NewInput;
				NewInput.InputName = FName(*Name);
				CustomExpr->Inputs.Add(MoveTemp(NewInput));
			}

			if (CustomExpr->Inputs.Num() == 0)
			{
				FCustomInput DefaultInput;
				DefaultInput.InputName = NAME_None;
				CustomExpr->Inputs.Add(MoveTemp(DefaultInput));
			}

			CustomExpr->RebuildOutputs();
			OutChanges.Add(FString::Printf(TEXT("InputNames: %d inputs"), CustomExpr->Inputs.Num()));
		}
	}

	if (UMaterialExpressionSwitch* SwitchExpr = Cast<UMaterialExpressionSwitch>(Expression))
	{
		if (const FString* SwitchNamesStr = NewProperties.Find(TEXT("SwitchInputNames")))
		{
			FString Working = *SwitchNamesStr;
			Working.TrimStartAndEndInline();
			if (Working.StartsWith(TEXT("[")) && Working.EndsWith(TEXT("]")))
			{
				Working = Working.Mid(1, Working.Len() - 2);
			}

			TArray<FString> Names;
			Working.ParseIntoArray(Names, TEXT(","));

			SwitchExpr->Inputs.Empty();
			for (FString& Name : Names)
			{
				Name.TrimStartAndEndInline();
				if (Name.StartsWith(TEXT("\"")) && Name.EndsWith(TEXT("\"")))
				{
					Name = Name.Mid(1, Name.Len() - 2);
				}
				FSwitchCustomInput NewInput;
				NewInput.InputName = FName(*Name);
				SwitchExpr->Inputs.Add(MoveTemp(NewInput));
			}

			OutChanges.Add(FString::Printf(TEXT("SwitchInputNames: %d cases"), SwitchExpr->Inputs.Num()));
		}
	}

	if (UMaterialExpressionSetMaterialAttributes* SetAttrExpr = Cast<UMaterialExpressionSetMaterialAttributes>(Expression))
	{
		if (const FString* AttrsStr = NewProperties.Find(TEXT("Attributes")))
		{
			FString Working = *AttrsStr;
			Working.TrimStartAndEndInline();
			if (Working.StartsWith(TEXT("[")) && Working.EndsWith(TEXT("]")))
			{
				Working = Working.Mid(1, Working.Len() - 2);
			}

			TArray<FString> AttrNames;
			Working.ParseIntoArray(AttrNames, TEXT(","));

			SetAttrExpr->AttributeSetTypes.Empty();
			SetAttrExpr->Inputs.Empty();
			SetAttrExpr->Inputs.Add(FExpressionInput());

			for (FString& AttrName : AttrNames)
			{
				AttrName.TrimStartAndEndInline();
				if (AttrName.StartsWith(TEXT("\"")) && AttrName.EndsWith(TEXT("\"")))
				{
					AttrName = AttrName.Mid(1, AttrName.Len() - 2);
				}

				FGuid AttrGuid;
				for (int32 i = 0; i < MP_MAX; ++i)
				{
					EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
					const FString& Name = FMaterialAttributeDefinitionMap::GetAttributeName(Prop);
					if (Name == AttrName)
					{
						AttrGuid = FMaterialAttributeDefinitionMap::GetID(Prop);
						break;
					}
				}

				if (!AttrGuid.IsValid())
				{
					AttrGuid = FMaterialAttributeDefinitionMap::GetCustomAttributeID(AttrName);
				}

				if (AttrGuid.IsValid())
				{
					SetAttrExpr->AttributeSetTypes.Add(AttrGuid);
					SetAttrExpr->Inputs.Add(FExpressionInput());
				}
				else
				{
					OutChanges.Add(FString::Printf(TEXT("Unknown attribute: %s"), *AttrName));
				}
			}

			OutChanges.Add(FString::Printf(TEXT("Attributes: %d attributes"), SetAttrExpr->AttributeSetTypes.Num()));
		}
	}

	for (const auto& Pair : NewProperties)
	{
		if (Pair.Key == TEXT("InputNames") || Pair.Key == TEXT("SwitchInputNames") || Pair.Key == TEXT("Attributes"))
		{
			continue;
		}

		FProperty* Prop = ExprClass->FindPropertyByName(FName(*Pair.Key));
		if (!Prop)
		{
			OutChanges.Add(FString::Printf(TEXT("Property not found: %s"), *Pair.Key));
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expression);

		FString OldValue;
		Prop->ExportTextItem_Direct(OldValue, ValuePtr, nullptr, Expression, PPF_None, nullptr);

		FString ImportValue = Pair.Value;
		if (ImportValue.StartsWith(TEXT("\"")) && ImportValue.EndsWith(TEXT("\"")))
		{
			ImportValue = ImportValue.Mid(1, ImportValue.Len() - 2);
			ImportValue = ImportValue.ReplaceEscapedCharWithChar();
		}

		const TCHAR* Buffer = *ImportValue;
		const TCHAR* Result = Prop->ImportText_Direct(Buffer, ValuePtr, Expression, PPF_None, GWarn);

		if (Result)
		{
			FString NewValue;
			Prop->ExportTextItem_Direct(NewValue, ValuePtr, nullptr, Expression, PPF_None, nullptr);
			if (OldValue != NewValue)
			{
				OutChanges.Add(FString::Printf(TEXT("%s: %s -> %s"), *Pair.Key, *OldValue, *NewValue));
			}
		}
		else
		{
			OutChanges.Add(FString::Printf(TEXT("Failed to import %s = %s"), *Pair.Key, *Pair.Value));
		}
	}
}

EMaterialProperty FMaterialGraphDiffer::FindMaterialPropertyByName(UMaterial* Material, const FString& Name)
{
	for (int32 i = 0; i < MP_MAX; ++i)
	{
		EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
		const FString& AttrName = FMaterialAttributeDefinitionMap::GetAttributeName(Prop);
		if (AttrName == Name)
		{
			return Prop;
		}
	}
	return MP_MAX;
}

FNodeCodeDiffResult FMaterialGraphDiffer::DiffAndApply(
	UMaterial* Material,
	UMaterialFunction* MaterialFunction,
	const FString& ScopeName,
	const FNodeCodeGraphIR& NewIR)
{
	FNodeCodeDiffResult Result;

	FMaterialExpressionClassCache::Get().Build();

	FNodeCodeGraphIR OldIR;
	if (Material)
	{
		OldIR = FMaterialGraphSerializer::BuildIR(Material, ScopeName);
	}
	else if (MaterialFunction)
	{
		OldIR = FMaterialGraphSerializer::BuildIR(MaterialFunction, ScopeName);
	}

	TMap<int32, int32> NewToOld;
	MatchNodes(OldIR, NewIR, NewToOld);

	TSet<int32> MatchedOldIndices;
	for (auto& Pair : NewToOld)
	{
		MatchedOldIndices.Add(Pair.Value);
	}

	FScopedTransaction Transaction(NSLOCTEXT("UCPMaterialGraph", "WriteGraph", "UCP: Write Material Graph"));

	// Phase 1: Delete removed nodes
	for (int32 OldIdx = 0; OldIdx < OldIR.Nodes.Num(); ++OldIdx)
	{
		if (MatchedOldIndices.Contains(OldIdx))
		{
			continue;
		}

		const FNodeCodeNodeIR& OldNode = OldIR.Nodes[OldIdx];
		UMaterialExpression* OldExpr = Cast<UMaterialExpression>(OldNode.SourceObject);
		if (OldExpr)
		{
			if (Material)
			{
				UMaterialEditingLibrary::DeleteMaterialExpression(Material, OldExpr);
			}
			else if (MaterialFunction)
			{
				UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(MaterialFunction, OldExpr);
			}
			Result.NodesRemoved.Add(FString::Printf(TEXT("N%d %s"), OldNode.Index, *OldNode.ClassName));
		}
	}

	// Phase 2: Create new nodes and map NewIndex -> Expression
	TMap<int32, UMaterialExpression*> NewIndexToExpr;

	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		const FNodeCodeNodeIR& NewNode = NewIR.Nodes[NewIdx];

		if (int32* OldIdx = NewToOld.Find(NewIdx))
		{
			NewIndexToExpr.Add(NewNode.Index, Cast<UMaterialExpression>(OldIR.Nodes[*OldIdx].SourceObject));
		}
		else
		{
			UClass* ExprClass = FMaterialExpressionClassCache::Get().FindClass(NewNode.ClassName);
			if (!ExprClass)
			{
				UE_LOG(LogUCP, Error, TEXT("WriteGraph: Unknown expression class: %s"), *NewNode.ClassName);
				continue;
			}

			UMaterialExpression* NewExpr = nullptr;
			if (Material)
			{
				NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExprClass, 0, 0);
			}
			else if (MaterialFunction)
			{
				NewExpr = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(MaterialFunction, ExprClass, 0, 0);
			}

			if (!NewExpr)
			{
				UE_LOG(LogUCP, Error, TEXT("WriteGraph: Failed to create expression: %s"), *NewNode.ClassName);
				continue;
			}

			TArray<FString> Changes;
			ApplyPropertyChanges(NewExpr, NewNode.Properties, Changes);

			NewIndexToExpr.Add(NewNode.Index, NewExpr);
			Result.NodesAdded.Add(FString::Printf(TEXT("N%d %s"), NewNode.Index, *NewNode.ClassName));
		}
	}

	// Phase 3: Modify properties on matched nodes
	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		int32* OldIdx = NewToOld.Find(NewIdx);
		if (!OldIdx)
		{
			continue;
		}

		const FNodeCodeNodeIR& NewNode = NewIR.Nodes[NewIdx];
		const FNodeCodeNodeIR& OldNode = OldIR.Nodes[*OldIdx];
		UMaterialExpression* Expr = Cast<UMaterialExpression>(OldNode.SourceObject);

		if (!Expr)
		{
			continue;
		}

		bool bPropsChanged = (NewNode.Properties.Num() != OldNode.Properties.Num());
		if (!bPropsChanged)
		{
			for (const auto& Pair : NewNode.Properties)
			{
				const FString* OldVal = OldNode.Properties.Find(Pair.Key);
				if (!OldVal || *OldVal != Pair.Value)
				{
					bPropsChanged = true;
					break;
				}
			}
		}

		if (bPropsChanged)
		{
			Expr->PreEditChange(nullptr);

			TArray<FString> Changes;
			ApplyPropertyChanges(Expr, NewNode.Properties, Changes);

			FPropertyChangedEvent ChangedEvent(nullptr);
			Expr->PostEditChangeProperty(ChangedEvent);

			if (Changes.Num() > 0)
			{
				Result.NodesModified.Add(FString::Printf(TEXT("N%d: %s"),
					NewNode.Index, *FString::Join(Changes, TEXT("; "))));
			}
		}
	}

	// Phase 4+5: Incremental connection diff
	// Helper lambdas for pin resolution
	auto FindInputIndexByName = [](UMaterialExpression* Expr, const FString& InputName) -> int32
	{
		if (InputName.IsEmpty())
		{
			return (Expr->GetInput(0) != nullptr) ? 0 : INDEX_NONE;
		}

		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input)
			{
				break;
			}
			if (Expr->GetInputName(i).ToString() == InputName)
			{
				return i;
			}
		}

		UClass* ExprClass = Expr->GetClass();
		for (TFieldIterator<FStructProperty> PropIt(ExprClass); PropIt; ++PropIt)
		{
			FStructProperty* StructProp = *PropIt;
			if (!StructProp || !StructProp->Struct)
			{
				continue;
			}

			const UStruct* Current = StructProp->Struct;
			bool bIsInput = false;
			while (Current)
			{
				if (Current->GetFName() == NAME_ExpressionInput)
				{
					bIsInput = true;
					break;
				}
				Current = Current->GetSuperStruct();
			}

			if (bIsInput && StructProp->GetName() == InputName)
			{
				const void* InputPtr = StructProp->ContainerPtrToValuePtr<void>(Expr);
				for (int32 i = 0; ; ++i)
				{
					FExpressionInput* Input = Expr->GetInput(i);
					if (!Input)
					{
						break;
					}
					if (Input == InputPtr)
					{
						return i;
					}
				}
			}
		}

		return INDEX_NONE;
	};

	auto FindOutputIndexByName = [](UMaterialExpression* Expr, const FString& OutputName) -> int32
	{
		if (OutputName.IsEmpty())
		{
			return 0;
		}

		TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
		for (int32 i = 0; i < Outputs.Num(); ++i)
		{
			const FExpressionOutput& Output = Outputs[i];
			if (!Output.OutputName.IsNone() && Output.OutputName.ToString() == OutputName)
			{
				return i;
			}

			if (Output.MaskR && !Output.MaskG && !Output.MaskB && !Output.MaskA && OutputName == TEXT("R")) return i;
			if (!Output.MaskR && Output.MaskG && !Output.MaskB && !Output.MaskA && OutputName == TEXT("G")) return i;
			if (!Output.MaskR && !Output.MaskG && Output.MaskB && !Output.MaskA && OutputName == TEXT("B")) return i;
			if (!Output.MaskR && !Output.MaskG && !Output.MaskB && Output.MaskA && OutputName == TEXT("A")) return i;
			if (Output.MaskR && Output.MaskG && Output.MaskB && !Output.MaskA && OutputName == TEXT("RGB")) return i;
			if (Output.MaskR && Output.MaskG && Output.MaskB && Output.MaskA && OutputName == TEXT("RGBA")) return i;
		}

		return INDEX_NONE;
	};

	// Build desired link set from NewIR
	TSet<FMatLink> DesiredLinks;
	TArray<FMatLink> LinksToCreate;

	for (const FNodeCodeLinkIR& Link : NewIR.Links)
	{
		UMaterialExpression** FromExprPtr = NewIndexToExpr.Find(Link.FromNodeIndex);
		if (!FromExprPtr || !*FromExprPtr)
		{
			UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Link source N%d not found"), Link.FromNodeIndex);
			continue;
		}

		int32 FromOutputIndex = FindOutputIndexByName(*FromExprPtr, Link.FromOutputName);
		if (FromOutputIndex == INDEX_NONE)
		{
			UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Output '%s' not found on N%d"), *Link.FromOutputName, Link.FromNodeIndex);
			continue;
		}

		FExpressionInput* TargetInput = nullptr;

		if (Link.bToGraphOutput && Material)
		{
			EMaterialProperty MatProp = FindMaterialPropertyByName(Material, Link.ToInputName);
			if (MatProp != MP_MAX)
			{
				TargetInput = Material->GetExpressionInputForProperty(MatProp);
			}
			else
			{
				UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Unknown material output: %s"), *Link.ToInputName);
			}
		}
		else if (!Link.bToGraphOutput)
		{
			UMaterialExpression** ToExprPtr = NewIndexToExpr.Find(Link.ToNodeIndex);
			if (!ToExprPtr || !*ToExprPtr)
			{
				UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Link target N%d not found"), Link.ToNodeIndex);
				continue;
			}

			int32 ToInputIndex = FindInputIndexByName(*ToExprPtr, Link.ToInputName);
			if (ToInputIndex == INDEX_NONE)
			{
				FString AvailableInputs;
				for (int32 DbgIdx = 0; ; ++DbgIdx)
				{
					FExpressionInput* DbgInput = (*ToExprPtr)->GetInput(DbgIdx);
					if (!DbgInput) break;
					if (!AvailableInputs.IsEmpty()) AvailableInputs += TEXT(", ");
					AvailableInputs += (*ToExprPtr)->GetInputName(DbgIdx).ToString();
				}
				UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Input '%s' not found on N%d (%s). Available: [%s]"),
					*Link.ToInputName, Link.ToNodeIndex, *(*ToExprPtr)->GetClass()->GetName(), *AvailableInputs);
				continue;
			}

			TargetInput = (*ToExprPtr)->GetInput(ToInputIndex);
		}

		if (!TargetInput)
		{
			continue;
		}

		FMatLink Desired;
		Desired.FromExpr = *FromExprPtr;
		Desired.OutputIndex = FromOutputIndex;
		Desired.ToInput = TargetInput;
		DesiredLinks.Add(Desired);

		bool bAlreadyConnected = (TargetInput->Expression == *FromExprPtr && TargetInput->OutputIndex == FromOutputIndex);
		if (!bAlreadyConnected)
		{
			LinksToCreate.Add(Desired);
		}
	}

	// Collect all live input slots on in-scope nodes + material outputs pointing to in-scope nodes
	// Remove connections that are not in the desired set
	TSet<UMaterialExpression*> ScopeExprSet;
	for (auto& Pair : NewIndexToExpr)
	{
		if (Pair.Value)
		{
			ScopeExprSet.Add(Pair.Value);
		}
	}

	for (auto& Pair : NewIndexToExpr)
	{
		UMaterialExpression* Expr = Pair.Value;
		if (!Expr)
		{
			continue;
		}

		for (FExpressionInputIterator It(Expr); It; ++It)
		{
			if (!It->Expression || !ScopeExprSet.Contains(It->Expression))
			{
				continue;
			}

			FMatLink Live;
			Live.FromExpr = It->Expression;
			Live.OutputIndex = It->OutputIndex;
			Live.ToInput = It.Input;

			if (!DesiredLinks.Contains(Live))
			{
				Result.LinksRemoved.Add(FString::Printf(TEXT("N?->N%d.%s"), Pair.Key, *Expr->GetInputName(It.Index).ToString()));
				It->Expression = nullptr;
			}
		}
	}

	if (Material)
	{
		for (int32 i = 0; i < MP_MAX; ++i)
		{
			EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
			FExpressionInput* Input = Material->GetExpressionInputForProperty(Prop);
			if (!Input || !Input->Expression || !ScopeExprSet.Contains(Input->Expression))
			{
				continue;
			}

			FMatLink Live;
			Live.FromExpr = Input->Expression;
			Live.OutputIndex = Input->OutputIndex;
			Live.ToInput = Input;

			if (!DesiredLinks.Contains(Live))
			{
				const FString& AttrName = FMaterialAttributeDefinitionMap::GetAttributeName(Prop);
				Result.LinksRemoved.Add(FString::Printf(TEXT("N?->[%s]"), *AttrName));
				Input->Expression = nullptr;
			}
		}
	}

	// Create new links
	for (const FMatLink& Link : LinksToCreate)
	{
		Link.FromExpr->ConnectExpression(Link.ToInput, Link.OutputIndex);
		Result.LinksAdded.Add(FString::Printf(TEXT("%s[%d] -> input"),
			*FMaterialExpressionClassCache::Get().GetSerializableName(Link.FromExpr->GetClass()),
			Link.OutputIndex));
	}

	// Phase 6: Clean orphaned nodes, relayout, recompile and refresh editor UI
	if (Material)
	{
		if (Material->MaterialGraph)
		{
			Material->MaterialGraph->RebuildGraph();
		}

		if (Material->MaterialGraph)
		{
			TArray<UEdGraphNode*> UnusedNodes;
			Material->MaterialGraph->GetUnusedExpressions(UnusedNodes);
			for (UEdGraphNode* Node : UnusedNodes)
			{
				UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(Node);
				if (!GraphNode || !GraphNode->MaterialExpression)
				{
					continue;
				}
				Result.NodesRemoved.Add(FString::Printf(TEXT("(orphaned) %s"),
					*FMaterialExpressionClassCache::Get().GetSerializableName(GraphNode->MaterialExpression->GetClass())));
				UMaterialExpression* Expr = GraphNode->MaterialExpression;
				Material->GetExpressionCollection().RemoveExpression(Expr);
				Material->RemoveExpressionParameter(Expr);
				Expr->MarkAsGarbage();
			}
			if (UnusedNodes.Num() > 0)
			{
				Material->MaterialGraph->RebuildGraph();
			}
		}

		bool bGraphChanged = Result.NodesAdded.Num() > 0 || Result.NodesRemoved.Num() > 0
			|| Result.LinksAdded.Num() > 0 || Result.LinksRemoved.Num() > 0;

		if (bGraphChanged)
		{
			UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
		}
		UMaterialEditingLibrary::RecompileMaterial(Material);

		if (GEditor)
		{
			if (IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Material, false))
			{
				IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(EditorInstance);
				UMaterialInterface* EditorMaterial = MaterialEditor->GetMaterialInterface();
				if (EditorMaterial && EditorMaterial != Material)
				{
					UMaterial* PreviewMaterial = Cast<UMaterial>(EditorMaterial);
					if (PreviewMaterial)
					{
						PreviewMaterial->GetExpressionCollection().Empty();
						for (UMaterialExpression* Expr : Material->GetExpressions())
						{
							UMaterialExpression* DupExpr = Cast<UMaterialExpression>(StaticDuplicateObject(Expr, PreviewMaterial));
							if (DupExpr)
							{
								DupExpr->Material = PreviewMaterial;
								PreviewMaterial->GetExpressionCollection().AddExpression(DupExpr);
							}
						}

						for (int32 i = 0; i < MP_MAX; ++i)
						{
							EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
							FExpressionInput* OrigInput = Material->GetExpressionInputForProperty(Prop);
							FExpressionInput* PreviewInput = PreviewMaterial->GetExpressionInputForProperty(Prop);
							if (OrigInput && PreviewInput)
							{
								if (OrigInput->Expression)
								{
									int32 OrigIdx = Material->GetExpressions().IndexOfByKey(OrigInput->Expression);
									if (OrigIdx != INDEX_NONE && OrigIdx < PreviewMaterial->GetExpressions().Num())
									{
										PreviewInput->Expression = PreviewMaterial->GetExpressions()[OrigIdx];
										PreviewInput->OutputIndex = OrigInput->OutputIndex;
										PreviewInput->Mask = OrigInput->Mask;
										PreviewInput->MaskR = OrigInput->MaskR;
										PreviewInput->MaskG = OrigInput->MaskG;
										PreviewInput->MaskB = OrigInput->MaskB;
										PreviewInput->MaskA = OrigInput->MaskA;
									}
								}
								else
								{
									PreviewInput->Expression = nullptr;
								}
							}
						}

						for (int32 i = 0; i < PreviewMaterial->GetExpressions().Num(); ++i)
						{
							UMaterialExpression* PreviewExpr = PreviewMaterial->GetExpressions()[i];
							for (FExpressionInputIterator It(PreviewExpr); It; ++It)
							{
								if (It->Expression)
								{
									int32 OrigIdx = Material->GetExpressions().IndexOfByKey(It->Expression);
									if (OrigIdx != INDEX_NONE && OrigIdx < PreviewMaterial->GetExpressions().Num())
									{
										It->Expression = PreviewMaterial->GetExpressions()[OrigIdx];
									}
									else
									{
										It->Expression = nullptr;
									}
								}
							}
						}

						if (PreviewMaterial->MaterialGraph)
						{
							PreviewMaterial->MaterialGraph->RebuildGraph();
						}

						MaterialEditor->UpdateMaterialAfterGraphChange();
					}
				}
			}
		}
	}
	else if (MaterialFunction)
	{
		UMaterialEditingLibrary::LayoutMaterialFunctionExpressions(MaterialFunction);
		UMaterialEditingLibrary::UpdateMaterialFunction(MaterialFunction, nullptr);
	}

	return Result;
}
