// MIT License - Copyright (c) 2025 Italink

#include "NodeCode/NodeCodeTextFormat.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ---- Text Parser Helpers ----

static FString TrimLine(const FString& Line)
{
	FString Result = Line;
	Result.TrimStartAndEndInline();
	return Result;
}

static bool ParseNodeLine(const FString& Line, int32& OutIndex, FString& OutClassName, TMap<FString, FString>& OutProps, FGuid& OutGuid)
{
	FString Working = Line;

	OutGuid.Invalidate();
	int32 HashPos;
	if (Working.FindLastChar('#', HashPos))
	{
		FString GuidStr = Working.Mid(HashPos + 1).TrimStartAndEnd();
		if (GuidStr.Len() >= 32)
		{
			FGuid::Parse(GuidStr, OutGuid);
		}
		Working = Working.Left(HashPos).TrimEnd();
	}

	int32 BraceOpen, BraceClose;
	if (Working.FindChar('{', BraceOpen) && Working.FindLastChar('}', BraceClose) && BraceClose > BraceOpen)
	{
		FString PropsStr = Working.Mid(BraceOpen + 1, BraceClose - BraceOpen - 1);
		Working = Working.Left(BraceOpen).TrimEnd();

		int32 Depth = 0;
		bool bInQuote = false;
		int32 Start = 0;

		auto ParsePair = [&](const FString& PairStr)
		{
			int32 ColonPos;
			if (PairStr.FindChar(':', ColonPos))
			{
				FString Key = PairStr.Left(ColonPos).TrimStartAndEnd();
				FString Value = PairStr.Mid(ColonPos + 1).TrimStartAndEnd();
				if (!Key.IsEmpty())
				{
					OutProps.Add(Key, Value);
				}
			}
		};

		for (int32 i = 0; i < PropsStr.Len(); ++i)
		{
			TCHAR Ch = PropsStr[i];
			if (Ch == '"')
			{
				bInQuote = !bInQuote;
			}
			else if (!bInQuote)
			{
				if (Ch == '(' || Ch == '[')
				{
					Depth++;
				}
				else if (Ch == ')' || Ch == ']')
				{
					Depth--;
				}
				else if (Ch == ',' && Depth == 0)
				{
					ParsePair(PropsStr.Mid(Start, i - Start));
					Start = i + 1;
				}
			}
		}
		if (Start < PropsStr.Len())
		{
			ParsePair(PropsStr.Mid(Start));
		}
	}

	int32 SpacePos;
	if (!Working.FindChar(' ', SpacePos))
	{
		return false;
	}

	FString IndexStr = Working.Left(SpacePos);
	if (!IndexStr.StartsWith(TEXT("N")))
	{
		return false;
	}

	OutIndex = FCString::Atoi(*IndexStr.Mid(1));
	OutClassName = Working.Mid(SpacePos + 1).TrimStartAndEnd();

	return !OutClassName.IsEmpty();
}

static bool ParseLinkLine(const FString& Line, int32& OutFromNode, FString& OutFromOutput,
	int32& OutToNode, FString& OutToInput, bool& bOutToGraphOutput)
{
	int32 ArrowPos = Line.Find(TEXT("->"));
	if (ArrowPos == INDEX_NONE)
	{
		return false;
	}

	FString FromStr = Line.Left(ArrowPos).TrimEnd();
	FString ToStr = Line.Mid(ArrowPos + 2).TrimStart();

	int32 DotPos;
	if (FromStr.FindChar('.', DotPos))
	{
		FString NodeStr = FromStr.Left(DotPos);
		if (!NodeStr.StartsWith(TEXT("N")))
		{
			return false;
		}
		OutFromNode = FCString::Atoi(*NodeStr.Mid(1));
		OutFromOutput = FromStr.Mid(DotPos + 1);
	}
	else
	{
		if (!FromStr.StartsWith(TEXT("N")))
		{
			return false;
		}
		OutFromNode = FCString::Atoi(*FromStr.Mid(1));
		OutFromOutput = FString();
	}

	if (ToStr.StartsWith(TEXT("[")) && ToStr.EndsWith(TEXT("]")))
	{
		bOutToGraphOutput = true;
		OutToInput = ToStr.Mid(1, ToStr.Len() - 2);
		OutToNode = -1;
	}
	else
	{
		bOutToGraphOutput = false;
		if (ToStr.FindChar('.', DotPos))
		{
			FString NodeStr = ToStr.Left(DotPos);
			if (!NodeStr.StartsWith(TEXT("N")))
			{
				return false;
			}
			OutToNode = FCString::Atoi(*NodeStr.Mid(1));
			OutToInput = ToStr.Mid(DotPos + 1);
		}
		else
		{
			return false;
		}
	}

	return true;
}

// ---- Public API ----

FString FNodeCodeTextFormat::IRToText(const FNodeCodeGraphIR& IR)
{
	FString Result;

	if (!IR.ScopeName.IsEmpty())
	{
		Result += FString::Printf(TEXT("=== scope: %s ===\n\n"), *IR.ScopeName);
	}

	Result += TEXT("=== nodes ===\n");
	for (int32 i = 0; i < IR.Nodes.Num(); ++i)
	{
		const FNodeCodeNodeIR& Node = IR.Nodes[i];

		Result += FString::Printf(TEXT("N%d %s"), Node.Index, *Node.ClassName);

		if (Node.Properties.Num() > 0)
		{
			Result += TEXT(" {");
			bool bFirst = true;
			for (const auto& Pair : Node.Properties)
			{
				if (!bFirst)
				{
					Result += TEXT(", ");
				}
				Result += FString::Printf(TEXT("%s:%s"), *Pair.Key, *Pair.Value);
				bFirst = false;
			}
			Result += TEXT("}");
		}

		if (Node.Guid.IsValid())
		{
			Result += FString::Printf(TEXT(" #%s"), *Node.Guid.ToString(EGuidFormats::DigitsLower));
		}

		Result += TEXT("\n");
	}

	Result += TEXT("\n=== links ===\n");
	for (const FNodeCodeLinkIR& Link : IR.Links)
	{
		FString FromStr = FString::Printf(TEXT("N%d"), Link.FromNodeIndex);
		if (!Link.FromOutputName.IsEmpty())
		{
			FromStr += FString::Printf(TEXT(".%s"), *Link.FromOutputName);
		}

		FString ToStr;
		if (Link.bToGraphOutput)
		{
			ToStr = FString::Printf(TEXT("[%s]"), *Link.ToInputName);
		}
		else
		{
			ToStr = FString::Printf(TEXT("N%d.%s"), Link.ToNodeIndex, *Link.ToInputName);
		}

		Result += FString::Printf(TEXT("%s -> %s\n"), *FromStr, *ToStr);
	}

	return Result;
}

FNodeCodeGraphIR FNodeCodeTextFormat::ParseText(const FString& GraphText)
{
	FNodeCodeGraphIR IR;

	TArray<FString> Lines;
	GraphText.ParseIntoArrayLines(Lines);

	enum class ESection { None, Nodes, Links };
	ESection CurrentSection = ESection::None;

	for (const FString& RawLine : Lines)
	{
		FString Line = TrimLine(RawLine);
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}

		if (Line.StartsWith(TEXT("=== scope:")))
		{
			int32 ColonPos = Line.Find(TEXT(":"));
			int32 EndPos = Line.Find(TEXT("==="), ESearchCase::IgnoreCase, ESearchDir::FromStart, ColonPos + 1);
			if (ColonPos != INDEX_NONE && EndPos != INDEX_NONE)
			{
				IR.ScopeName = Line.Mid(ColonPos + 1, EndPos - ColonPos - 1).TrimStartAndEnd();
			}
			continue;
		}

		if (Line == TEXT("=== nodes ==="))
		{
			CurrentSection = ESection::Nodes;
			continue;
		}
		if (Line == TEXT("=== links ==="))
		{
			CurrentSection = ESection::Links;
			continue;
		}

		if (CurrentSection == ESection::Nodes)
		{
			FNodeCodeNodeIR Node;
			if (ParseNodeLine(Line, Node.Index, Node.ClassName, Node.Properties, Node.Guid))
			{
				IR.Nodes.Add(MoveTemp(Node));
			}
		}
		else if (CurrentSection == ESection::Links)
		{
			FNodeCodeLinkIR Link;
			if (ParseLinkLine(Line, Link.FromNodeIndex, Link.FromOutputName,
				Link.ToNodeIndex, Link.ToInputName, Link.bToGraphOutput))
			{
				IR.Links.Add(MoveTemp(Link));
			}
		}
	}

	return IR;
}

FString FNodeCodeTextFormat::DiffResultToJson(const FNodeCodeDiffResult& Result)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();

	auto ToJsonArray = [](const TArray<FString>& Arr) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& S : Arr)
		{
			Values.Add(MakeShared<FJsonValueString>(S));
		}
		return Values;
	};

	Diff->SetArrayField(TEXT("nodes_added"), ToJsonArray(Result.NodesAdded));
	Diff->SetArrayField(TEXT("nodes_removed"), ToJsonArray(Result.NodesRemoved));
	Diff->SetArrayField(TEXT("nodes_modified"), ToJsonArray(Result.NodesModified));
	Diff->SetArrayField(TEXT("links_added"), ToJsonArray(Result.LinksAdded));
	Diff->SetArrayField(TEXT("links_removed"), ToJsonArray(Result.LinksRemoved));
	Root->SetObjectField(TEXT("diff"), Diff);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputString;
}
