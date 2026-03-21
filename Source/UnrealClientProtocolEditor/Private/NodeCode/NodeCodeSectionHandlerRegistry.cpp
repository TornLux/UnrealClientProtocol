// MIT License - Copyright (c) 2025 Italink

#include "NodeCode/INodeCodeSectionHandler.h"

FNodeCodeSectionHandlerRegistry& FNodeCodeSectionHandlerRegistry::Get()
{
	static FNodeCodeSectionHandlerRegistry Instance;
	return Instance;
}

void FNodeCodeSectionHandlerRegistry::Register(TSharedPtr<INodeCodeSectionHandler> Handler)
{
	if (Handler.IsValid())
	{
		Handlers.Add(Handler);
	}
}

INodeCodeSectionHandler* FNodeCodeSectionHandlerRegistry::FindHandler(UObject* Asset, const FString& Type) const
{
	for (const auto& Handler : Handlers)
	{
		if (Handler->CanHandle(Asset, Type))
		{
			return Handler.Get();
		}
	}
	return nullptr;
}

TArray<FNodeCodeSectionIR> FNodeCodeSectionHandlerRegistry::ListAllSections(UObject* Asset) const
{
	TArray<FNodeCodeSectionIR> AllSections;
	TSet<INodeCodeSectionHandler*> VisitedHandlers;

	for (const auto& Handler : Handlers)
	{
		if (!Handler->CanHandle(Asset, TEXT("")))
		{
			continue;
		}

		if (VisitedHandlers.Contains(Handler.Get()))
		{
			continue;
		}
		VisitedHandlers.Add(Handler.Get());

		TArray<FNodeCodeSectionIR> Sections = Handler->ListSections(Asset);
		AllSections.Append(Sections);
	}

	return AllSections;
}
