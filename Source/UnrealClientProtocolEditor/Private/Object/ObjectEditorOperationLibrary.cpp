// MIT License - Copyright (c) 2025 Italink

#include "Object/ObjectEditorOperationLibrary.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/ITransaction.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogObjectEditorOp, Log, All);

static FString JsonObjectToString(const TSharedPtr<FJsonObject>& Obj)
{
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return OutputString;
}

FString UObjectEditorOperationLibrary::UndoTransaction()
{
	if (!GEditor)
	{
		UE_LOG(LogObjectEditorOp, Error, TEXT("Undo: GEditor not available"));
		return FString();
	}

	if (!GEditor->UndoTransaction())
	{
		UE_LOG(LogObjectEditorOp, Warning, TEXT("Undo: Nothing to undo"));
	}

	return FString();
}

FString UObjectEditorOperationLibrary::RedoTransaction()
{
	if (!GEditor)
	{
		UE_LOG(LogObjectEditorOp, Error, TEXT("Redo: GEditor not available"));
		return FString();
	}

	if (!GEditor->RedoTransaction())
	{
		UE_LOG(LogObjectEditorOp, Warning, TEXT("Redo: Nothing to redo"));
	}

	return FString();
}

FString UObjectEditorOperationLibrary::GetTransactionState()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->Trans)
	{
		UE_LOG(LogObjectEditorOp, Error, TEXT("GetTransactionState: GEditor or transaction system not available"));
		return FString();
	}

	bool bCanUndo = GEditor->Trans->CanUndo();
	bool bCanRedo = GEditor->Trans->CanRedo();
	Result->SetBoolField(TEXT("canUndo"), bCanUndo);
	Result->SetBoolField(TEXT("canRedo"), bCanRedo);

	if (bCanUndo)
	{
		FTransactionContext UndoCtx = GEditor->Trans->GetUndoContext();
		Result->SetStringField(TEXT("undoTitle"), UndoCtx.Title.ToString());
	}
	else
	{
		Result->SetStringField(TEXT("undoTitle"), TEXT(""));
	}

	if (bCanRedo)
	{
		FTransactionContext RedoCtx = GEditor->Trans->GetRedoContext();
		Result->SetStringField(TEXT("redoTitle"), RedoCtx.Title.ToString());
	}
	else
	{
		Result->SetStringField(TEXT("redoTitle"), TEXT(""));
	}

	Result->SetNumberField(TEXT("undoCount"), GEditor->Trans->GetUndoCount());
	Result->SetNumberField(TEXT("queueLength"), GEditor->Trans->GetQueueLength());

	return JsonObjectToString(Result);
}
