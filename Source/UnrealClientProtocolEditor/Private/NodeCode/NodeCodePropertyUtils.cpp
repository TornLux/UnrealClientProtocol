// MIT License - Copyright (c) 2025 Italink

#include "NodeCode/NodeCodePropertyUtils.h"
#include "UObject/UnrealType.h"

FString FNodeCodePropertyUtils::FormatPropertyValue(FProperty* Prop, const void* ValuePtr, UObject* Owner)
{
	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
		if (!Obj)
		{
			return TEXT("None");
		}
		if (Obj->IsAsset())
		{
			return FString::Printf(TEXT("\"%s\""), *Obj->GetPathName().ReplaceCharWithEscapedChar());
		}
		FString ExportStr;
		Prop->ExportTextItem_Direct(ExportStr, ValuePtr, nullptr, Owner, PPF_None, nullptr);
		return ExportStr;
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		FString ExportStr;
		Prop->ExportTextItem_Direct(ExportStr, ValuePtr, nullptr, Owner, PPF_None, nullptr);
		return ExportStr;
	}

	if (CastField<FStrProperty>(Prop))
	{
		const FString& Str = *static_cast<const FString*>(ValuePtr);
		return FString::Printf(TEXT("\"%s\""), *Str.ReplaceCharWithEscapedChar());
	}

	if (CastField<FNameProperty>(Prop))
	{
		const FName& Name = *static_cast<const FName*>(ValuePtr);
		return FString::Printf(TEXT("\"%s\""), *Name.ToString().ReplaceCharWithEscapedChar());
	}

	if (CastField<FTextProperty>(Prop))
	{
		const FText& Text = *static_cast<const FText*>(ValuePtr);
		return FString::Printf(TEXT("\"%s\""), *Text.ToString().ReplaceCharWithEscapedChar());
	}

	FString ExportStr;
	Prop->ExportTextItem_Direct(ExportStr, ValuePtr, nullptr, Owner, PPF_None, nullptr);
	return ExportStr;
}

bool FNodeCodePropertyUtils::ShouldSkipProperty(const FProperty* Prop)
{
	if (Prop->HasAnyPropertyFlags(
		CPF_Transient | CPF_DuplicateTransient | CPF_SkipSerialization | CPF_Deprecated))
	{
		return true;
	}

	return false;
}
