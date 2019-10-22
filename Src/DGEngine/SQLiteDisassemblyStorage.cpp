#pragma once
#include <stdio.h>
#include <string>

#include "sqlite3.h"

#include "SQLiteDisassemblyStorage.h"
#include "Log.h"

using namespace std;

SQLiteDisassemblyStorage::SQLiteDisassemblyStorage(const char *DatabaseName)
{
	db=NULL;
    if (DatabaseName)
    {
        CreateDatabase(DatabaseName);
        CreateTables();
    }
}

SQLiteDisassemblyStorage::~SQLiteDisassemblyStorage()
{
	CloseDatabase();
}

void SQLiteDisassemblyStorage::CreateTables()
{
	ExecuteStatement(NULL, NULL, CREATE_BASIC_BLOCK_TABLE_STATEMENT);
	ExecuteStatement(NULL, NULL, CREATE_BASIC_BLOCK_TABLE_FUNCTION_ADDRESS_INDEX_STATEMENT);
	ExecuteStatement(NULL, NULL, CREATE_BASIC_BLOCK_TABLE_START_ADDRESS_INDEX_STATEMENT);
	ExecuteStatement(NULL, NULL, CREATE_BASIC_BLOCK_TABLE_END_ADDRESS_INDEX_STATEMENT);
	ExecuteStatement(NULL, NULL, CREATE_MAP_INFO_TABLE_STATEMENT);
	ExecuteStatement(NULL, NULL, CREATE_MAP_INFO_TABLE_SRCBLOCK_INDEX_STATEMENT);
	ExecuteStatement(NULL, NULL, CREATE_FILE_INFO_TABLE_STATEMENT);
}
bool SQLiteDisassemblyStorage::Open(char* DatabaseName)
{
	m_DatabaseName = DatabaseName;
	return CreateDatabase(DatabaseName);
}

bool SQLiteDisassemblyStorage::CreateDatabase(const char* DatabaseName)
{
	//Database Setup
	m_DatabaseName = DatabaseName;
	int rc = sqlite3_open(DatabaseName, &db);
	if (rc)
	{
		printf("Opening Database [%s] Failed\n", DatabaseName);
		sqlite3_close(db);
		db = NULL;
		return FALSE;
	}
	return TRUE;
}

const char* SQLiteDisassemblyStorage::GetDatabaseName()
{
	return m_DatabaseName.c_str();
}

void SQLiteDisassemblyStorage::CloseDatabase()
{
	//Close Database
	if (db)
	{
		sqlite3_close(db);
		db = NULL;
	}
}

int SQLiteDisassemblyStorage::BeginTransaction()
{
	return ExecuteStatement(NULL, NULL, "BEGIN TRANSACTION");
}

int SQLiteDisassemblyStorage::EndTransaction()
{
	return ExecuteStatement(NULL, NULL, "COMMIT");
}

int SQLiteDisassemblyStorage::GetLastInsertRowID()
{
	return (int)sqlite3_last_insert_rowid(db);
}

int SQLiteDisassemblyStorage::ExecuteStatement(sqlite3_callback callback, void* context, const char* format, ...)
{
	int debug = 0;

	if (db)
	{
		int rc = 0;
		char* statement_buffer = NULL;
		char* zErrMsg = 0;

		va_list args;
		va_start(args, format);
#ifdef USE_VSNPRINTF
		int statement_buffer_len = 0;

		while (1)
		{
			statement_buffer_len += 1024;
			statement_buffer = (char*)malloc(statement_buffer_len);
			memset(statement_buffer, 0, statement_buffer_len);
			if (statement_buffer && _vsnprintf(statement_buffer, statement_buffer_len, format, args) != -1)
			{
				free(statement_buffer);
				break;
			}

			if (!statement_buffer)
				break;
			free(statement_buffer);
		}
#else
		statement_buffer = sqlite3_vmprintf(format, args);
#endif
		va_end(args);

		if (debug > 1)
		{
			LogMessage(1, __FUNCTION__, "Executing [%s]\n", statement_buffer);
		}

		if (statement_buffer)
		{
			rc = sqlite3_exec(db, statement_buffer, callback, context, &zErrMsg);

			if (rc != SQLITE_OK)
			{
				if (debug > 0)
				{
#ifdef IDA_PLUGIN				
					LogMessage(1, __FUNCTION__, "SQL error: [%s] [%s]\n", statement_buffer, zErrMsg);
#else
					LogMessage(1, __FUNCTION__, "SQL error: [%s] [%s]\n", statement_buffer, zErrMsg);
#endif
				}
			}
#ifdef USE_VSNPRINTF
			free(statement_buffer);
#else
			sqlite3_free(statement_buffer);
#endif
		}

		return rc;
	}
	return SQLITE_ERROR;
}

void SQLiteDisassemblyStorage::SetFileInfo(FileInfo * pFileInfo)
{
	ExecuteStatement(NULL, NULL, INSERT_FILE_INFO_TABLE_STATEMENT,
		pFileInfo->OriginalFilePath,
		pFileInfo->ComputerName,
		pFileInfo->UserName,
		pFileInfo->CompanyName,
		pFileInfo->FileVersion,
		pFileInfo->FileDescription,
		pFileInfo->InternalName,
		pFileInfo->ProductName,
		pFileInfo->ModifiedTime,
		pFileInfo->MD5Sum
	);
}

void SQLiteDisassemblyStorage::AddBasicBlock(PBasicBlock pBasicBlock, int fileID)
{
	char* fingerprintStr = NULL;
	if (pBasicBlock->FingerprintLen > 0)
	{
		fingerprintStr = (char*)malloc(pBasicBlock->FingerprintLen * 2 + 10);
		if (fingerprintStr)
		{
			memset(fingerprintStr, 0, pBasicBlock->FingerprintLen * 2 + 10);
			char tmp_buffer[10];
			for (int i = 0; i < pBasicBlock->FingerprintLen; i++)
			{
				_snprintf(tmp_buffer, sizeof(tmp_buffer) - 1, "%.2x", pBasicBlock->Data[pBasicBlock->NameLen + pBasicBlock->DisasmLinesLen + i] & 0xff);
				tmp_buffer[sizeof(tmp_buffer) - 1] = NULL;
				strncat(fingerprintStr, tmp_buffer, sizeof(tmp_buffer));
			}
		}
	}

	ExecuteStatement(NULL, NULL, INSERT_BASIC_BLOCK_TABLE_STATEMENT,
		fileID,
		pBasicBlock->StartAddress,
		pBasicBlock->EndAddress,
		pBasicBlock->Flag,
		pBasicBlock->FunctionAddress,
		pBasicBlock->BlockType,
		pBasicBlock->Data,
		pBasicBlock->Data + pBasicBlock->NameLen,
		fingerprintStr ? fingerprintStr : ""
	);

	if (fingerprintStr)
		free(fingerprintStr);
}

void SQLiteDisassemblyStorage::AddMapInfo(PMapInfo pMapInfo, int fileID)
{
	ExecuteStatement(NULL, NULL, INSERT_MAP_INFO_TABLE_STATEMENT,
		fileID,
		pMapInfo->Type,
		pMapInfo->SrcBlock,
		pMapInfo->SrcBlockEnd,
		pMapInfo->Dst
	);
}

void SQLiteDisassemblyStorage::EndAnalysis()
{
}

int SQLiteDisassemblyStorage::ProcessTLV(BYTE Type, PBYTE Data, DWORD Length)
{
    static int fileID = 0;
    bool Status = FALSE;
    static DWORD CurrentAddress = 0L;

    switch (Type)
    {
    case BASIC_BLOCK:
        if (sizeof(BasicBlock) <= Length)
        {
			AddBasicBlock((PBasicBlock)Data, fileID);
        }
        break;

    case MAP_INFO:
        if (sizeof(MapInfo) <= Length)
        {
			AddMapInfo((PMapInfo)Data, fileID);
        }
        break;

    case FILE_INFO:
        if (sizeof(FileInfo) <= Length)
        {
			SetFileInfo((PFileInfo) Data);
			fileID = GetLastInsertRowID();
        }
        break;

    }
    Status = TRUE;
    return fileID;
}


int SQLiteDisassemblyStorage::display_callback(void *NotUsed, int argc, char **argv, char **azColName)
{
	int i;
	for(i=0; i<argc; i++){
		LogMessage(1, __FUNCTION__, "%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	return 0;
}

int SQLiteDisassemblyStorage::ReadRecordIntegerCallback(void *arg,int argc,char **argv,char **names)
{
#if DEBUG_LEVEL > 2
	printf("%s: arg=%x %d\n",__FUNCTION__,arg,argc);
	for(int i=0;i<argc;i++)
	{
		printf("	[%d] %s=%s\n",i,names[i],argv[i]);
	}
#endif
	*(int *)arg=atoi(argv[0]);
	return 0;
}

int SQLiteDisassemblyStorage::ReadRecordStringCallback(void *arg,int argc,char **argv,char **names)
{
#if DEBUG_LEVEL > 2
	printf("%s: arg=%x %d\n",__FUNCTION__,arg,argc);
	for(int i=0;i<argc;i++)
	{
		printf("	[%d] %s=%s\n",i,names[i],argv[i]);
	}
#endif
	*(char **)arg=_strdup(argv[0]);
	return 0;
}