#pragma once
#include <vector>
#include <unordered_set>
#include <list>

#include "Common.h"
#include "IDASession.h"
#include "LogOperation.h"
#include "MatchResults.h"
#include "DiffAlgorithms.h"

class IDASessions
{
private:
    int DebugFlag;

    bool ShowFullMatched;
    bool ShowNonMatched;

    bool LoadMatchResults;
    bool LoadIDAController;

    int SourceID;
    string SourceDBName;
    va_t SourceFunctionAddress;

    int TargetID;
    string TargetDBName;
    va_t TargetFunctionAddress;

    Storage* m_diffStorage;
    Storage* m_sourceStorage;
    Storage* m_targetStorage;

    IDASession* SourceIDASession;
    IDASession* TargetIDASession;

    MatchResults* m_pMatchResults;
    FunctionMatchInfoList* m_pFunctionMatchInfoList;

    DiffAlgorithms* m_pdiffAlgorithms;

    unordered_set <va_t> m_sourceUnidentifedBlockHash;
    unordered_set <va_t> m_targetUnidentifedBlockHash;

    DumpAddressChecker* m_pdumpAddressChecker;

	BOOL _Load();

public:
    IDASessions(IDASession *the_source = NULL, IDASession *the_target = NULL);
    ~IDASessions();

    void SetDumpAddressChecker(DumpAddressChecker *p_dump_address_checker)
    {
        m_pdumpAddressChecker = p_dump_address_checker;
    }

    void SetSource(IDASession *NewSource)
    {
        SourceIDASession = NewSource;
    }

    void SetTarget(IDASession *NewTarget)
    {
        TargetIDASession = NewTarget;
    }

    void SetLoadMatchResults(bool NewLoadMatchResults)
    {
        LoadMatchResults = NewLoadMatchResults;
    }
    void SetLoadIDAController(bool NewLoadIDAController)
    {
        LoadIDAController = NewLoadIDAController;
    }

    void SetSource(const char* db_filename, DWORD id = 1, va_t function_address = 0)
    {
        SourceDBName = db_filename;
        SourceID = id;
        SourceFunctionAddress = function_address;
    }

    void SetTarget(const char* db_filename, DWORD id = 1, va_t function_address = 0)
    {
        TargetDBName = db_filename;
        TargetID = id;
        TargetFunctionAddress = function_address;
    }

    void SetSource(Storage* disassemblyStorage, DWORD id = 1, va_t function_address = 0)
    {
        m_sourceStorage = disassemblyStorage;
        SourceID = id;
        SourceFunctionAddress = function_address;
    }

    void SetTarget(Storage* disassemblyStorage, DWORD id = 1, va_t function_address = 0)
    {
        m_targetStorage = disassemblyStorage;
        TargetID = id;
        TargetFunctionAddress = function_address;
    }

    void SetTargetFunctions(va_t ParamSourceFunctionAddress, va_t ParamTargetFunctionAddress)
    {
        SourceFunctionAddress = ParamSourceFunctionAddress;
        TargetFunctionAddress = ParamTargetFunctionAddress;
    }

    BOOL Create(const char* DiffDBFilename);
    BOOL Load(const char* DiffDBFilename);
    BOOL Load(Storage* disassemblyStorage);

    IDASession *GetSourceIDASession();
    IDASession *GetTargetIDASession();

    void AppendToMatchMap(MATCHMAP* pBaseMap, MATCHMAP* pTemporaryMap);
    MatchMapList* GetMatchData(int index, va_t address, BOOL erase = FALSE);
    va_t GetMatchAddr(int index, va_t address);
    int GetMatchRate(va_t unpatched_address, va_t patched_address);
    void RemoveMatchData(va_t source_address, va_t target_address);
    void PrintMatchMapInfo();

    va_t DumpFunctionMatchInfo(int index, va_t address);

    void GetMatchStatistics(va_t address, int index, int& found_match_number, int& found_match_with_difference_number, int& not_found_match_number, float& matchrate);

    void ShowDiffMap(va_t unpatched_address, va_t patched_address);
    void TestFunctionMatchRate(int index, va_t Address);
    void RetrieveNonMatchingMembers(int index, va_t FunctionAddress, list <va_t>& Members);
    bool TestAnalysis();
    MATCHMAP* DoFunctionLevelMatchOptimizing(FunctionMatchInfoList* pFunctionMatchInfoList);
    bool Analyze();
    void AnalyzeFunctionSanity();

    int GetUnidentifiedBlockCount(int index);
    CodeBlock GetUnidentifiedBlock(int index, int i);
    BOOL IsInUnidentifiedBlockHash(int index, va_t address);
    BOOL Save(Storage& disassemblyStorage, unordered_set <va_t> *pTheSourceSelectedAddresses = NULL, unordered_set <va_t> *pTheTargetSelectedAddresses = NULL);
    BREAKPOINTS ShowUnidentifiedAndModifiedBlocks();
};
