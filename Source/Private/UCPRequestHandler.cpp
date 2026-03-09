// MIT License - Copyright (c) 2025 Italink

#include "UCPRequestHandler.h"
#include "UCPFunctionInvoker.h"
#include "UCPParamConverter.h"
#include "UCPSettings.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonValue.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

static bool IsCallAllowed(const FString& ObjectPath, const FString& FunctionName)
{
	const UUCPSettings* Settings = GetDefault<UUCPSettings>();

	for (const FString& Blocked : Settings->BlockedFunctions)
	{
		if (Blocked.Equals(FunctionName, ESearchCase::IgnoreCase))
		{
			return false;
		}
		if (Blocked.Contains(TEXT("::")) && ObjectPath.Contains(Blocked.Left(Blocked.Find(TEXT("::")))))
		{
			FString BlockedFunc = Blocked.Mid(Blocked.Find(TEXT("::")) + 2);
			if (BlockedFunc.Equals(FunctionName, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
	}

	if (Settings->AllowedClassPrefixes.Num() > 0)
	{
		bool bAllowed = false;
		for (const FString& Prefix : Settings->AllowedClassPrefixes)
		{
			if (ObjectPath.StartsWith(Prefix))
			{
				bAllowed = true;
				break;
			}
		}
		if (!bAllowed)
		{
			return false;
		}
	}

	return true;
}

TSharedPtr<FJsonObject> FUCPRequestHandler::HandleRequest(const TSharedPtr<FJsonObject>& Request)
{
	if (!Request.IsValid())
	{
		return MakeError(FString(), TEXT("Null request"));
	}

	FString Type = Request->GetStringField(TEXT("type"));

	if (Type == TEXT("batch"))
	{
		TSharedPtr<FJsonObject> Response = ExecBatch(Request);
		CopyIdField(Request, Response);
		return Response;
	}

	TSharedPtr<FJsonObject> Response = DispatchSingle(Request);
	CopyIdField(Request, Response);
	return Response;
}

TSharedPtr<FJsonObject> FUCPRequestHandler::DispatchSingle(const TSharedPtr<FJsonObject>& Request)
{
	FString Type = Request->GetStringField(TEXT("type"));

	if (Type == TEXT("call"))           return CallUFunction(Request);
	if (Type == TEXT("get_property"))   return GetUProperty(Request);
	if (Type == TEXT("set_property"))   return SetUProperty(Request);
	if (Type == TEXT("describe"))       return Describe(Request);
	if (Type == TEXT("find"))           return FindUObjects(Request);

	return MakeError(FString(), FString::Printf(TEXT("Unknown request type: %s"), *Type));
}

TSharedPtr<FJsonObject> FUCPRequestHandler::ExecBatch(const TSharedPtr<FJsonObject>& Request)
{
	const TArray<TSharedPtr<FJsonValue>>* Commands;
	if (!Request->TryGetArrayField(TEXT("commands"), Commands))
	{
		return MakeError(FString(), TEXT("batch request requires a 'commands' array"));
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	Results.Reserve(Commands->Num());

	for (const TSharedPtr<FJsonValue>& CmdVal : *Commands)
	{
		TSharedPtr<FJsonObject> CmdObj = CmdVal->AsObject();
		if (!CmdObj.IsValid())
		{
			TSharedPtr<FJsonObject> Err = MakeError(FString(), TEXT("Each command in batch must be a JSON object"));
			Results.Add(MakeShared<FJsonValueObject>(Err));
			continue;
		}

		TSharedPtr<FJsonObject> SingleResp = DispatchSingle(CmdObj);
		CopyIdField(CmdObj, SingleResp);
		Results.Add(MakeShared<FJsonValueObject>(SingleResp));
	}

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), true);
	Response->SetArrayField(TEXT("results"), Results);
	return Response;
}

TSharedPtr<FJsonObject> FUCPRequestHandler::CallUFunction(const TSharedPtr<FJsonObject>& Request)
{
	FString ObjectPath = Request->GetStringField(TEXT("object"));
	FString FunctionName = Request->GetStringField(TEXT("function"));
	TSharedPtr<FJsonObject> Params;
	if (Request->HasField(TEXT("params")))
	{
		Params = Request->GetObjectField(TEXT("params"));
	}

	if (ObjectPath.IsEmpty())
	{
		return MakeError(FString(), TEXT("Missing 'object' field"));
	}
	if (FunctionName.IsEmpty())
	{
		return MakeError(FString(), TEXT("Missing 'function' field"));
	}

	if (!IsCallAllowed(ObjectPath, FunctionName))
	{
		return MakeError(FString(), FString::Printf(TEXT("Call denied by security policy: %s::%s"), *ObjectPath, *FunctionName));
	}

	TSharedPtr<FJsonObject> Result = FUCPFunctionInvoker::Invoke(ObjectPath, FunctionName, Params);

	if (!Result->GetBoolField(TEXT("success")))
	{
		TSharedPtr<FJsonObject> Sig = FUCPFunctionInvoker::DescribeFunction(ObjectPath, FunctionName);
		if (Sig->GetBoolField(TEXT("success")))
		{
			Result->SetObjectField(TEXT("expected"), Sig->GetObjectField(TEXT("result")));
		}
	}

	return Result;
}

TSharedPtr<FJsonObject> FUCPRequestHandler::GetUProperty(const TSharedPtr<FJsonObject>& Request)
{
	FString ObjectPath = Request->GetStringField(TEXT("object"));
	FString PropertyName = Request->GetStringField(TEXT("property"));

	if (ObjectPath.IsEmpty() || PropertyName.IsEmpty())
	{
		return MakeError(FString(), TEXT("Missing 'object' or 'property' field"));
	}

	UObject* Obj = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
	if (!Obj)
	{
		Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
	}
	if (!Obj)
	{
		return MakeError(FString(), FString::Printf(TEXT("Object not found: %s"), *ObjectPath));
	}

	FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		return MakeError(FString(), FString::Printf(TEXT("Property not found: %s on %s"), *PropertyName, *Obj->GetClass()->GetName()));
	}

	const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);
	TSharedPtr<FJsonValue> JsonVal = FUCPParamConverter::PropertyToJsonValue(Prop, ValuePtr);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetField(PropertyName, JsonVal);
	Result->SetObjectField(TEXT("result"), ResultData);

	return Result;
}

TSharedPtr<FJsonObject> FUCPRequestHandler::SetUProperty(const TSharedPtr<FJsonObject>& Request)
{
	FString ObjectPath = Request->GetStringField(TEXT("object"));
	FString PropertyName = Request->GetStringField(TEXT("property"));

	if (ObjectPath.IsEmpty() || PropertyName.IsEmpty())
	{
		return MakeError(FString(), TEXT("Missing 'object' or 'property' field"));
	}

	UObject* Obj = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
	if (!Obj)
	{
		Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
	}
	if (!Obj)
	{
		return MakeError(FString(), FString::Printf(TEXT("Object not found: %s"), *ObjectPath));
	}

	FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		return MakeError(FString(), FString::Printf(TEXT("Property not found: %s on %s"), *PropertyName, *Obj->GetClass()->GetName()));
	}

	TSharedPtr<FJsonValue> JsonVal = Request->TryGetField(TEXT("value"));
	if (!JsonVal.IsValid())
	{
		return MakeError(FString(), TEXT("Missing 'value' field"));
	}

	FString TransactionDesc = FString::Printf(TEXT("UCP: Set %s.%s"), *Obj->GetName(), *PropertyName);

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TransactionDesc));
	Obj->PreEditChange(Prop);
#endif

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);
	FString Error = FUCPParamConverter::JsonValueToProperty(JsonVal, Prop, ValuePtr);
	if (!Error.IsEmpty())
	{
		return MakeError(FString(), Error);
	}

#if WITH_EDITOR
	FPropertyChangedEvent ChangedEvent(Prop);
	Obj->PostEditChangeProperty(ChangedEvent);
#endif

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

TSharedPtr<FJsonObject> FUCPRequestHandler::Describe(const TSharedPtr<FJsonObject>& Request)
{
	FString ObjectPath = Request->GetStringField(TEXT("object"));
	if (ObjectPath.IsEmpty())
	{
		return MakeError(FString(), TEXT("Missing 'object' field"));
	}

	if (Request->HasField(TEXT("function")))
	{
		FString FunctionName = Request->GetStringField(TEXT("function"));
		if (FunctionName.IsEmpty())
		{
			return MakeError(FString(), TEXT("'function' field is empty"));
		}
		return FUCPFunctionInvoker::DescribeFunction(ObjectPath, FunctionName);
	}

	if (Request->HasField(TEXT("property")))
	{
		FString PropertyName = Request->GetStringField(TEXT("property"));
		if (PropertyName.IsEmpty())
		{
			return MakeError(FString(), TEXT("'property' field is empty"));
		}
		return FUCPFunctionInvoker::DescribeProperty(ObjectPath, PropertyName);
	}

	return FUCPFunctionInvoker::DescribeObject(ObjectPath);
}

TSharedPtr<FJsonObject> FUCPRequestHandler::FindUObjects(const TSharedPtr<FJsonObject>& Request)
{
	FString ClassName = Request->GetStringField(TEXT("class"));

	if (ClassName.IsEmpty())
	{
		return MakeError(FString(), TEXT("Missing 'class' field"));
	}

	UClass* TargetClass = FindObject<UClass>(nullptr, *ClassName);
	if (!TargetClass)
	{
		TargetClass = LoadObject<UClass>(nullptr, *ClassName);
	}
	if (!TargetClass)
	{
		return MakeError(FString(), FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	int32 Limit = 100;
	if (Request->HasField(TEXT("limit")))
	{
		Limit = FMath::Clamp((int32)Request->GetNumberField(TEXT("limit")), 1, 10000);
	}

	TArray<TSharedPtr<FJsonValue>> ObjectPaths;
	int32 Count = 0;
	for (TObjectIterator<UObject> It; It; ++It)
	{
		if (It->IsA(TargetClass))
		{
			ObjectPaths.Add(MakeShared<FJsonValueString>(It->GetPathName()));
			if (++Count >= Limit)
			{
				break;
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetArrayField(TEXT("objects"), ObjectPaths);
	ResultData->SetNumberField(TEXT("count"), Count);
	Result->SetObjectField(TEXT("result"), ResultData);

	return Result;
}

TSharedPtr<FJsonObject> FUCPRequestHandler::MakeError(const FString& Id, const FString& Error)
{
	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	if (!Id.IsEmpty())
	{
		Resp->SetStringField(TEXT("id"), Id);
	}
	Resp->SetBoolField(TEXT("success"), false);
	Resp->SetStringField(TEXT("error"), Error);
	return Resp;
}

void FUCPRequestHandler::CopyIdField(const TSharedPtr<FJsonObject>& From, const TSharedPtr<FJsonObject>& To)
{
	if (From.IsValid() && To.IsValid() && From->HasField(TEXT("id")))
	{
		To->SetStringField(TEXT("id"), From->GetStringField(TEXT("id")));
	}
}
