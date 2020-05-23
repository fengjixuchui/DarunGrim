#pragma warning(disable:4996)
#pragma warning(disable:4200)
#include "IDASession.h"
#include <string>
#include "LogOperation.h"

//DB Related
#include "sqlite3.h"
#include "Storage.h"

#include <unordered_set>
using namespace std;
using namespace stdext;

#include "LogOperation.h"

extern LogOperation Logger;

#define DEBUG_LEVEL 0

char *MapInfoTypesStr[] = { "Call", "Cref From", "Cref To", "Dref From", "Dref To" };
int types[] = { CREF_FROM, CREF_TO, CALL, DREF_FROM, DREF_TO, CALLED };

IDASession::IDASession(Storage *disassemblyStorage) :
    ClientAnalysisInfo(NULL),
    TargetFunctionAddress(0),
    m_OriginalFilePath(NULL),
    DisasmLine(NULL),
    Socket(INVALID_SOCKET),
    m_FileID(0)
{
    ClientAnalysisInfo = new AnalysisInfo;
    m_pStorage = disassemblyStorage;
}

IDASession::~IDASession()
{
    if (m_OriginalFilePath)
        free(m_OriginalFilePath);

    if (ClientAnalysisInfo)
    {
        ClientAnalysisInfo->name_map.clear();

        for (auto& val : ClientAnalysisInfo->map_info_map)
        {
            if (val.second)
                delete val.second;
        }

        ClientAnalysisInfo->map_info_map.clear();

        for (auto& val : ClientAnalysisInfo->address_fingerprint_map)
        {
            if (val.second)
            {
                free(val.second);
            }
        }
        ClientAnalysisInfo->address_fingerprint_map.clear();
        ClientAnalysisInfo->fingerprint_map.clear();

        delete ClientAnalysisInfo;
    }
}

void IDASession::SetSocket(SOCKET socket)
{
    Socket = socket;
}

BOOL IDASession::LoadIDARawDataFromSocket(SOCKET socket)
{
    Socket = socket;
    ClientAnalysisInfo = NULL;
    char shared_memory_name[1024];
    _snprintf(shared_memory_name, 1024, TEXT("DG Shared Memory - %u - %u"),
        GetCurrentProcessId(),
        GetCurrentThreadId());

    Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d InitDataSharer\n", __FUNCTION__);

#define SHARED_MEMORY_SIZE 100000
    if (!InitDataSharer(&IDADataSharer,
        shared_memory_name,
        SHARED_MEMORY_SIZE,
        TRUE))
    {
        Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d InitDataSharer failed\n", __FUNCTION__);
        return FALSE;
    }
    char data[1024 + sizeof(DWORD)];
    *(DWORD*)data = SHARED_MEMORY_SIZE;
    memcpy(data + sizeof(DWORD), shared_memory_name, strlen(shared_memory_name) + 1);

    Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d SendTLVData SEND_ANALYSIS_DATA\n", __FUNCTION__);

    if (SendTLVData(SEND_ANALYSIS_DATA, (PBYTE)data, sizeof(DWORD) + strlen(shared_memory_name) + 1))
    {
        Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d LoadIDARawData\n", __FUNCTION__);
        LoadIDARawData((PBYTE(*)(PVOID Context, BYTE  *Type, DWORD  *Length))GetData, (PVOID)&IDADataSharer);
        return TRUE;
    }
    return FALSE;
}

va_t *IDASession::GetMappedAddresses(va_t address, int type, int *p_length)
{
    va_t *addresses = NULL;
    int current_size = 50;

    addresses = (va_t*)malloc(sizeof(va_t)  *current_size);
    int addresses_i = 0;

    multimap <va_t, PMapInfo> *p_map_info_map;

    if (ClientAnalysisInfo && ClientAnalysisInfo->map_info_map.size() > 0)
    {
        p_map_info_map = &ClientAnalysisInfo->map_info_map;
    }
    else
    {
        p_map_info_map = new multimap <va_t, PMapInfo>();
        LoadMapInfo(p_map_info_map, address);
    }

    multimap <va_t, PMapInfo>::iterator map_info_map_pIter;

    for (map_info_map_pIter = p_map_info_map->find(address); map_info_map_pIter != p_map_info_map->end(); map_info_map_pIter++)
    {
        if (map_info_map_pIter->first != address)
            break;
        if (map_info_map_pIter->second->Type == type)
        {
            //map_info_map_pIter->second->Dst
            //TODO: add
            if (current_size < addresses_i + 2)
            {
                current_size += 50;
                addresses = (va_t*)realloc(addresses, sizeof(va_t)  *(current_size));
            }
            addresses[addresses_i] = map_info_map_pIter->second->Dst;
            addresses_i++;
            addresses[addresses_i] = NULL;
        }
    }

    if (!ClientAnalysisInfo)
    {
        p_map_info_map->clear();
        free(p_map_info_map);
    }

    if (p_length)
        *p_length = addresses_i;
    if (addresses_i == 0)
    {
        free(addresses);
        addresses = NULL;
    }
    return addresses;
}


list <va_t> *IDASession::GetFunctionAddresses()
{
    if (TargetFunctionAddress != 0)
    {
        list <va_t> *function_addresses = new list<va_t>;
        if (function_addresses)
        {
            function_addresses->push_back(TargetFunctionAddress);
        }

        return function_addresses;
    }

    int DoCrefFromCheck = FALSE;
    int DoCallCheck = TRUE;
    unordered_set <va_t> function_address_hash;
    unordered_map <va_t, short> addresses;

    if (DoCrefFromCheck)
    {
        Logger.Log(10, LOG_IDA_CONTROLLER, "addresses.size() = %u\n", addresses.size());

        for (auto& val: ClientAnalysisInfo->map_info_map)
        {
            Logger.Log(10, LOG_IDA_CONTROLLER, "%X-%X(%s) ", val.first, val.second->Dst, MapInfoTypesStr[val.second->Type]);
            if (val.second->Type == CREF_FROM)
            {
                unordered_map <va_t, short>::iterator iter = addresses.find(val.second->Dst);
                if (iter != addresses.end())
                {
                    iter->second = FALSE;
                }
            }
        }
        Logger.Log(10, LOG_IDA_CONTROLLER, "%s\n", __FUNCTION__);

        for (auto& val : ClientAnalysisInfo->address_fingerprint_map)
        {
            addresses.insert(pair<va_t, short>(val.first, DoCrefFromCheck ? TRUE : FALSE));
        }

        Logger.Log(10, LOG_IDA_CONTROLLER, "addresses.size() = %u\n", addresses.size());
        for (auto& val : addresses)
        {
            if (val.second)
            {
                Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d Function %X\n", __FUNCTION__, m_FileID, val.first);
                function_address_hash.insert(val.first);
            }
        }
    }
    else
    {
        m_pStorage->ReadFunctionAddressMap(m_FileID, function_address_hash);
    }

    if (DoCallCheck && ClientAnalysisInfo)
    {
        for (auto& val : ClientAnalysisInfo->map_info_map)
        {
            if (val.second->Type == CALL)
            {
                if (function_address_hash.find(val.second->Dst) == function_address_hash.end())
                {
                    Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d Function %X (by Call Recognition)\n", __FUNCTION__, m_FileID, val.second->Dst);
                    function_address_hash.insert(val.second->Dst);
                }
            }
        }
    }

    list <va_t> *function_addresses = new list<va_t>;
    if (function_addresses)
    {
        for (auto& val : function_address_hash)
        {
            function_addresses->push_back(val);
            Logger.Log(11, LOG_IDA_CONTROLLER, "%s: ID = %d Function %X\n", __FUNCTION__, m_FileID, val);
        }

        Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d Returns(%u entries)\n", __FUNCTION__, m_FileID, function_addresses->size());
    }
    return function_addresses;
}

#undef USE_LEGACY_MAP_FOR_ADDRESS_unordered_map
void IDASession::RemoveFromFingerprintHash(va_t address)
{
    unsigned char *Fingerprint = NULL;

    char *FingerprintStr = m_pStorage->ReadFingerPrint(m_FileID, address);

    if (FingerprintStr)
    {
        Fingerprint = HexToBytesWithLengthAmble(FingerprintStr);
    }

    if (Fingerprint)
    {
        multimap <unsigned char*, va_t, hash_compare_fingerprint>::iterator fingerprint_map_PIter;
        for (fingerprint_map_PIter = ClientAnalysisInfo->fingerprint_map.find(Fingerprint);
            fingerprint_map_PIter != ClientAnalysisInfo->fingerprint_map.end();
            fingerprint_map_PIter++
            )
        {
            if (!IsEqualByteWithLengthAmble(fingerprint_map_PIter->first, Fingerprint))
                break;
            if (fingerprint_map_PIter->second == address)
            {
                ClientAnalysisInfo->fingerprint_map.erase(fingerprint_map_PIter);
                break;
            }
        }
        free(Fingerprint);
    }
}

char *IDASession::GetFingerPrintStr(va_t address)
{
    if (ClientAnalysisInfo && ClientAnalysisInfo->address_fingerprint_map.size() > 0)
    {
        multimap <va_t, unsigned char*>::iterator address_fingerprint_map_PIter = ClientAnalysisInfo->address_fingerprint_map.find(address);
        if (address_fingerprint_map_PIter != ClientAnalysisInfo->address_fingerprint_map.end())
        {
            return BytesWithLengthAmbleToHex(address_fingerprint_map_PIter->second);
        }
    }
    else
    {
        char *FingerprintPtr = m_pStorage->ReadFingerPrint(m_FileID, address);
        return FingerprintPtr;
    }
    return NULL;
}

char *IDASession::GetName(va_t address)
{
#ifdef USE_LEGACY_MAP
    multimap <va_t, string>::iterator address_name_map_iter;

    address_name_map_iter = ClientAnalysisInfo->address_name_map.find(address);
    if (address_name_map_iter != ClientAnalysisInfo->address_name_map.end())
    {
        return _strdup((*address_name_map_iter).second.c_str());
    }
    return NULL;
#else
    char *Name = m_pStorage->ReadName(m_FileID, address);
    return Name;
#endif
}

va_t IDASession::GetBlockAddress(va_t address)
{
#ifdef USE_LEGACY_MAP
    while (1)
    {
        if (ClientAnalysisInfo->address_map.find(address) != ClientAnalysisInfo->address_map.end())
            break;
        address--;
    }
    return address;
#else
    return m_pStorage->ReadBlockStartAddress(m_FileID, address);
#endif
}

void IDASession::DumpBlockInfo(va_t block_address)
{
    int addresses_number;
    char *type_descriptions[] = { "Cref From", "Cref To", "Call", "Dref From", "Dref To" };
    for (int i = 0; i < sizeof(types) / sizeof(int); i++)
    {
        va_t *addresses = GetMappedAddresses(block_address, types[i], &addresses_number);
        if (addresses)
        {
            Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d %s: ", __FUNCTION__, m_FileID, type_descriptions[i]);
            for (int j = 0; j < addresses_number; j++)
            {
                Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d %X ", __FUNCTION__, m_FileID, addresses[j]);
            }
            Logger.Log(10, LOG_IDA_CONTROLLER, "\n");
        }
    }
    char *hex_str = GetFingerPrintStr(block_address);
    if (hex_str)
    {
        Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d fingerprint: %s\n", __FUNCTION__, m_FileID, hex_str);
        free(hex_str);
    }
}

const char *GetAnalysisDataTypeStr(int type)
{
    static const char *Types[] = { "BASIC_BLOCK", "MAP_INFO", "FILE_INFO", "END_OF_DATA" };
    if (type < sizeof(Types) / sizeof(Types[0]))
        return Types[type];
    return "Unknown";
}

enum { TYPE_FILE_INFO, TYPE_ADDRESS_unordered_map, TYPE_ADDRESS_DISASSEMBLY_MAP, TYPE_FINGERPRINT_unordered_map, TYPE_TWO_LEVEL_FINGERPRINT_unordered_map, TYPE_ADDRESS_FINGERPRINT_unordered_map, TYPE_NAME_unordered_map, TYPE_ADDRESS_NAME_unordered_map, TYPE_MAP_INFO_unordered_map };

const char *GetFileDataTypeStr(int type)
{
    static const char *Types[] = { "FILE_INFO", "ADDRESS_unordered_map", "ADDRESS_DISASSEMBLY_MAP", "FINGERPRINT_unordered_map", "TWO_LEVEL_FINGERPRINT_unordered_map", "ADDRESS_FINGERPRINT_unordered_map", "NAME_unordered_map", "ADDRESS_NAME_unordered_map", "MAP_INFO_unordered_map" };
    if (type < sizeof(Types) / sizeof(Types[0]))
        return Types[type];
    return "Unknown";
}

BOOL IDASession::Save(char *DataFile, DWORD Offset, DWORD dwMoveMethod, unordered_set <va_t> *pSelectedAddresses)
{
    return TRUE;
}

BOOL IDASession::Retrieve(char *DataFile, DWORD Offset, DWORD Length)
{
    return TRUE;
}

char *IDASession::GetOriginalFilePath()
{
    return m_OriginalFilePath;
}

BOOL IDASession::LoadBasicBlock()
{
    if (ClientAnalysisInfo->fingerprint_map.size() == 0)
    {
        char conditionStr[50] = { 0, };
        if (TargetFunctionAddress)
        {
            _snprintf(conditionStr, sizeof(conditionStr) - 1, "AND FunctionAddress = '%d'", TargetFunctionAddress);
        }

        m_pStorage->ReadBasicBlockInfo(m_FileID, conditionStr, ClientAnalysisInfo);
        GenerateFingerprintHashMap();
    }
    return TRUE;
}

/*
FunctionAddress = 0 : Retrieve All Functions
    else			: Retrieve That Specific Function
*/

void IDASession::SetFileID(int FileID)
{
    m_FileID = FileID;
}

void IDASession::LoadMapInfo(multimap <va_t, PMapInfo> *p_map_info_map, va_t Address, bool IsFunction)
{
    if (Address == 0)
    {
        p_map_info_map = m_pStorage->ReadMapInfo(m_FileID);
    }
    else
    {
        p_map_info_map = m_pStorage->ReadMapInfo(m_FileID, Address, IsFunction);
    }

    BuildCrefToMap(p_map_info_map);
}


void IDASession::BuildCrefToMap(multimap <va_t, PMapInfo> *p_map_info_map)
{
    for (auto& val : *p_map_info_map)
    {
        if (val.second->Type == CREF_FROM)
        {
            CrefToMap.insert(pair<va_t, va_t>(val.second->Dst, val.first));
        }
    }
}

BOOL IDASession::Load()
{
    m_OriginalFilePath = m_pStorage->GetOriginalFilePath(m_FileID);

    LoadBasicBlock();
    LoadMapInfo(&(ClientAnalysisInfo->map_info_map), TargetFunctionAddress, true);

    return TRUE;
}

void IDASession::DeleteMatchInfo(Storage *InputDB, int FileID, va_t FunctionAddress)
{
    m_pStorage->DeleteMatchInfo(FileID, FunctionAddress);
}

void IDASession::AddAnalysisTargetFunction(va_t FunctionAddress)
{
    Logger.Log(10, LOG_IDA_CONTROLLER, "Add Analysis Target Function: %X\n", FunctionAddress);
    TargetFunctionAddress = FunctionAddress;
}

typedef struct {
    va_t address;
    va_t child_address;
} AddressPair;

void IDASession::LoadIDARawData(PBYTE(*RetrieveCallback)(PVOID Context, BYTE *Type, DWORD *Length), PVOID Context)
{
    BYTE type;
    DWORD length;

    multimap <va_t, PBasicBlock>::iterator address_map_pIter;
    multimap <string, va_t>::iterator fingerprint_map_pIter;
    multimap <string, va_t>::iterator name_map_pIter;
    multimap <va_t, PMapInfo>::iterator map_info_map_pIter;

    va_t current_addr = 0L;

    if (m_pStorage)
        m_pStorage->BeginTransaction();

    while (1)
    {
        PBYTE data = RetrieveCallback(Context, &type, &length);
#if DEBUG_LEVEL > 0
        Logger.Log(10, "%s: ID = %d type = %u Data(0x%X) is Read %u Bytes Long\n", __FUNCTION__, m_FileID, type, data, length);
#endif

        if (type == END_OF_DATA)
        {
#if DEBUG_LEVEL > -1
            Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d End of Analysis\n", __FUNCTION__);
            Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d address_map:%u/address_fingerprint_map:%u/fingerprint_map:%u/name_map:%u/map_info_map:%u\n",
                __FUNCTION__, m_FileID,
                ClientAnalysisInfo->address_map.size(),
                ClientAnalysisInfo->address_fingerprint_map.size(),
                ClientAnalysisInfo->fingerprint_map.size(),
                ClientAnalysisInfo->name_map.size(),
                ClientAnalysisInfo->map_info_map.size()
            );
#endif
            if (data)
                free(data);
            break;
        }
        if (!data)
            continue;

        if (m_pStorage)
            m_FileID = m_pStorage->ProcessTLV(type, data, length);

        if (type == BASIC_BLOCK && sizeof(BasicBlock) <= length)
        {
            PBasicBlock pBasicBlock = (PBasicBlock)data;
            current_addr = pBasicBlock->StartAddress;
            Logger.Log(11, LOG_IDA_CONTROLLER, "%s: ID = %d BASIC_BLOCK[StartAddress = %X Flag = %u function addr = %X BlockType = %u]\n", __FUNCTION__, m_FileID,
                pBasicBlock->StartAddress, //ea_t
                pBasicBlock->Flag,  //Flag_t
                pBasicBlock->FunctionAddress,
                pBasicBlock->BlockType);
#ifdef USE_LEGACY_MAP
            ClientAnalysisInfo->address_map.insert(AddrPBasicBlock_Pair(pBasicBlock->StartAddress, pBasicBlock));
#endif
            ClientAnalysisInfo->name_map.insert(NameAddress_Pair(pBasicBlock->Data, pBasicBlock->StartAddress));
            if (pBasicBlock->FingerprintLen > 0)
            {
                unsigned char *FingerprintBuffer = (unsigned char*)malloc(pBasicBlock->FingerprintLen + sizeof(short));
                *(unsigned short*)FingerprintBuffer = pBasicBlock->FingerprintLen;
                memcpy(FingerprintBuffer + sizeof(short), pBasicBlock->Data + pBasicBlock->NameLen + pBasicBlock->DisasmLinesLen, *(unsigned short*)FingerprintBuffer);
                ClientAnalysisInfo->address_fingerprint_map.insert(AddressFingerPrintAddress_Pair(pBasicBlock->StartAddress, FingerprintBuffer));
            }
            free(data);
        }
        else if (type == MAP_INFO && length == sizeof(MapInfo))
        {
            PMapInfo p_map_info = (PMapInfo)data;
#if DEBUG_LEVEL > 2
            Logger.Log(10, "%s: ID = %d %s %X(%X)->%X\n", __FUNCTION__, m_FileID,
                MapInfoTypesStr[p_map_info->Type],
                p_map_info->SrcBlock,
                p_map_info->SrcBlockEnd,
                p_map_info->Dst);
#endif
            ClientAnalysisInfo->map_info_map.insert(AddressPMapInfoPair(p_map_info->SrcBlock, p_map_info));
            /*
            We don't use backward CFG anymore.
            if(p_map_info->Type  ==  CREF_FROM || p_map_info->Type  ==  CALL)
            {
                PMapInfo p_new_map_info = (PMapInfo)malloc(sizeof(MapInfo));
                p_new_map_info->SrcBlock = p_map_info->Dst;
                p_new_map_info->Src = p_map_info->Dst;
                p_new_map_info->Dst = p_map_info->SrcBlock;
                if(p_map_info->Type  ==  CREF_FROM)
                    p_new_map_info->Type = CREF_TO;
                else
                    p_new_map_info->Type = CALLED;
                ClientAnalysisInfo->map_info_map.insert(AddressPMapInfoPair(p_new_map_info->SrcBlock, p_new_map_info));
            }*/
        }
        else
        {
            free(data);
        }
    }

    if (m_pStorage)
        m_pStorage->EndTransaction();
    FixFunctionAddresses();
    GenerateFingerprintHashMap();
}

void IDASession::GenerateFingerprintHashMap()
{
    multimap <va_t, PBasicBlock>::iterator address_map_pIter;
    list <AddressPair> AddressPairs;

    for (auto& val : ClientAnalysisInfo->address_map)
    {
        va_t address = val.first;
        multimap <va_t, PMapInfo>::iterator map_info_map_iter;
        int matched_children_count = 0;
        va_t matched_child_addr = 0L;
        for (map_info_map_iter = ClientAnalysisInfo->map_info_map.find(address);
            map_info_map_iter != ClientAnalysisInfo->map_info_map.end();
            map_info_map_iter++
            )
        {
            if (map_info_map_iter->first != address)
                break;
            PMapInfo p_map_info = map_info_map_iter->second;
            if (p_map_info->Type == CREF_FROM)
            {
                matched_child_addr = map_info_map_iter->second->Dst;
                matched_children_count++;
            }
        }
        Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d 0x%X children count: %u\n", __FUNCTION__, m_FileID, address, matched_children_count);
        if (matched_children_count == 1 && matched_child_addr != 0L)
        {
            int matched_parents_count = 0;
            for (map_info_map_iter = ClientAnalysisInfo->map_info_map.find(matched_child_addr);
                map_info_map_iter != ClientAnalysisInfo->map_info_map.end();
                map_info_map_iter++
                )
            {
                if (map_info_map_iter->first != matched_child_addr)
                    break;
                PMapInfo p_map_info = map_info_map_iter->second;
                if (p_map_info->Type == CREF_TO || p_map_info->Type == CALLED)
                    matched_parents_count++;
            }
            Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d 0x%X -> 0x%X parent count: %u\n", __FUNCTION__, m_FileID, address, matched_child_addr, matched_parents_count);
            if (matched_parents_count == 1)
            {
                address_map_pIter = ClientAnalysisInfo->address_map.find(matched_child_addr);
                if (address_map_pIter != ClientAnalysisInfo->address_map.end())
                {
                    PBasicBlock pBasicBlock = (PBasicBlock)address_map_pIter->second;
                    if (pBasicBlock->FunctionAddress != matched_child_addr)
                    {
                        AddressPair address_pair;
                        address_pair.address = address;
                        address_pair.child_address = matched_child_addr;
                        AddressPairs.push_back(address_pair);
                    }
                }
            }
        }
    }

    for (AddressPair addressPair : AddressPairs)
    {
        va_t address = addressPair.address;
        va_t child_address = addressPair.child_address;
        Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d Joining 0x%X-0x%X\n", __FUNCTION__, m_FileID, address, child_address);

        va_t matched_child_addr = 0L;

        multimap <va_t, PMapInfo>::iterator map_info_map_iter;
        for (map_info_map_iter = ClientAnalysisInfo->map_info_map.find(child_address);
            map_info_map_iter != ClientAnalysisInfo->map_info_map.end();
            map_info_map_iter++
            )
        {
            if (map_info_map_iter->first != child_address)
                break;
            PMapInfo p_map_info = map_info_map_iter->second;
            PMapInfo p_new_map_info = (PMapInfo)malloc(sizeof(MapInfo));
            p_new_map_info->SrcBlockEnd = address;
            p_new_map_info->SrcBlock = address;
            p_new_map_info->Dst = p_map_info->Dst;
            p_new_map_info->Type = p_map_info->Type;
            ClientAnalysisInfo->map_info_map.insert(AddressPMapInfoPair(address, p_new_map_info));
        }
        for (map_info_map_iter = ClientAnalysisInfo->map_info_map.find(address);
            map_info_map_iter != ClientAnalysisInfo->map_info_map.end();
            map_info_map_iter++
            )
        {
            if (map_info_map_iter->first != address)
                break;
            PMapInfo p_map_info = map_info_map_iter->second;
            if (p_map_info->Dst == child_address)
            {
                ClientAnalysisInfo->map_info_map.erase(map_info_map_iter);
                break;
            }
        }
        multimap <va_t, string>::iterator child_address_disassembly_map_iter;
        child_address_disassembly_map_iter = ClientAnalysisInfo->address_disassembly_map.find(child_address);
        if (child_address_disassembly_map_iter != ClientAnalysisInfo->address_disassembly_map.end())
        {
            multimap <va_t, string>::iterator address_disassembly_map_iter;
            address_disassembly_map_iter = ClientAnalysisInfo->address_disassembly_map.find(address);
            if (address_disassembly_map_iter != ClientAnalysisInfo->address_disassembly_map.end())
            {
                address_disassembly_map_iter->second += child_address_disassembly_map_iter->second;
            }
        }

        multimap <va_t, unsigned char*>::iterator child_address_fingerprint_map_iter;
        child_address_fingerprint_map_iter = ClientAnalysisInfo->address_fingerprint_map.find(child_address);
        if (child_address_fingerprint_map_iter != ClientAnalysisInfo->address_fingerprint_map.end())
        {
            multimap <va_t, unsigned char*>::iterator address_fingerprint_map_iter;
            address_fingerprint_map_iter = ClientAnalysisInfo->address_fingerprint_map.find(address);
            if (address_fingerprint_map_iter != ClientAnalysisInfo->address_fingerprint_map.end())
            {
                //TODO: address_fingerprint_map_iter->second += child_address_fingerprint_map_iter->second;
            }
        }
        ClientAnalysisInfo->address_map.erase(addressPair.child_address);
        ClientAnalysisInfo->address_name_map.erase(addressPair.child_address);
        ClientAnalysisInfo->map_info_map.erase(addressPair.child_address);
        ClientAnalysisInfo->address_disassembly_map.erase(addressPair.child_address);
        ClientAnalysisInfo->address_fingerprint_map.erase(addressPair.child_address);
    }
    AddressPairs.clear();

    for (auto& val : ClientAnalysisInfo->address_fingerprint_map)
    {
        ClientAnalysisInfo->fingerprint_map.insert(FingerPrintAddress_Pair(val.second, val.first));
    }
    GenerateTwoLevelFingerPrint();
}

void IDASession::GenerateTwoLevelFingerPrint()
{
    /*
    multimap <unsigned char *, va_t, hash_compare_fingerprint>::iterator fingerprint_map_pIter;
    for (fingerprint_map_pIter = ClientAnalysisInfo->fingerprint_map.begin();
        fingerprint_map_pIter != ClientAnalysisInfo->fingerprint_map.end();
        fingerprint_map_pIter++)

    {
        if(ClientAnalysisInfo->fingerprint_map.count(fingerprint_map_pIter->first)>1)
        {
            int addresses_number = 0;
            va_t *addresses = GetMappedAddresses(fingerprint_map_pIter->second, CREF_FROM, &addresses_number);
            if(!addresses)
                addresses = GetMappedAddresses(fingerprint_map_pIter->second, CREF_TO, NULL);
            if(addresses)
            {
                int TwoLevelFingerprintLength = 0;
                TwoLevelFingerprintLength += *(unsigned short *)fingerprint_map_pIter->first; //+
                multimap <va_t,  unsigned char *>::iterator address_fingerprint_map_Iter;
                for (int i = 0;i<addresses_number;i++)
                {
                    address_fingerprint_map_Iter = ClientAnalysisInfo->address_fingerprint_map.find(addresses[i]);
                    if(address_fingerprint_map_Iter != ClientAnalysisInfo->address_fingerprint_map.end())
                    {
                        TwoLevelFingerprintLength += *(unsigned short *)address_fingerprint_map_Iter->second; //+
                    }
                }

                if(TwoLevelFingerprintLength>0)
                {
                    unsigned char *TwoLevelFingerprint = (unsigned char *)malloc(TwoLevelFingerprintLength+sizeof(short));
                    if(TwoLevelFingerprint)
                    {
                        *(unsigned short *)TwoLevelFingerprint = TwoLevelFingerprintLength;

                        int Offset = sizeof(short);
                        memcpy(TwoLevelFingerprint+Offset, fingerprint_map_pIter->first+sizeof(short), *(unsigned short *)fingerprint_map_pIter->first);
                        Offset += *(unsigned short *)fingerprint_map_pIter->first;
                        for (int i = 0;i<addresses_number;i++)
                        {
                            address_fingerprint_map_Iter = ClientAnalysisInfo->address_fingerprint_map.find(addresses[i]);
                            if(address_fingerprint_map_Iter != ClientAnalysisInfo->address_fingerprint_map.end())
                            {
                                memcpy(TwoLevelFingerprint+Offset, address_fingerprint_map_Iter->second+sizeof(short), *(unsigned short *)address_fingerprint_map_Iter->second);
                                Offset += *(unsigned short *)address_fingerprint_map_Iter->second;
                            }
                        }
                        ClientAnalysisInfo->fingerprint_map.insert(FingerPrintAddress_Pair(TwoLevelFingerprint, fingerprint_map_pIter->second));
                    }
                }
            }
        }
    }*/
}

void IDASession::DumpAnalysisInfo()
{
    if (ClientAnalysisInfo)
    {
        Logger.Log(10, LOG_IDA_CONTROLLER, "OriginalFilePath = %s\n", ClientAnalysisInfo->file_info.OriginalFilePath);
        Logger.Log(10, LOG_IDA_CONTROLLER, "ComputerName = %s\n", ClientAnalysisInfo->file_info.ComputerName);
        Logger.Log(10, LOG_IDA_CONTROLLER, "UserName = %s\n", ClientAnalysisInfo->file_info.UserName);
        Logger.Log(10, LOG_IDA_CONTROLLER, "CompanyName = %s\n", ClientAnalysisInfo->file_info.CompanyName);
        Logger.Log(10, LOG_IDA_CONTROLLER, "FileVersion = %s\n", ClientAnalysisInfo->file_info.FileVersion);
        Logger.Log(10, LOG_IDA_CONTROLLER, "FileDescription = %s\n", ClientAnalysisInfo->file_info.FileDescription);
        Logger.Log(10, LOG_IDA_CONTROLLER, "InternalName = %s\n", ClientAnalysisInfo->file_info.InternalName);
        Logger.Log(10, LOG_IDA_CONTROLLER, "ProductName = %s\n", ClientAnalysisInfo->file_info.ProductName);
        Logger.Log(10, LOG_IDA_CONTROLLER, "ModifiedTime = %s\n", ClientAnalysisInfo->file_info.ModifiedTime);
        Logger.Log(10, LOG_IDA_CONTROLLER, "MD5Sum = %s\n", ClientAnalysisInfo->file_info.MD5Sum);

        Logger.Log(10, LOG_IDA_CONTROLLER, "fingerprint_map = %u\n", ClientAnalysisInfo->fingerprint_map.size());
    }
}

BOOL IDASession::SendTLVData(char type, PBYTE data, DWORD data_length)
{
    if (Socket != INVALID_SOCKET)
    {
        BOOL ret = ::SendTLVData(Socket,
            type,
            data,
            data_length);
        if (!ret)
            Socket = INVALID_SOCKET;
        return ret;
    }
    return FALSE;
}

char *IDASession::GetDisasmLines(unsigned long StartAddress, unsigned long EndAddress)
{
#ifdef USE_LEGACY_MAP
    //Look for p_analysis_info->address_disassembly_map first
    multimap <va_t, string>::iterator address_disassembly_map_pIter;
    address_disassembly_map_pIter = ClientAnalysisInfo->address_disassembly_map.find(StartAddress);
    if (address_disassembly_map_pIter != ClientAnalysisInfo->address_disassembly_map.end())
    {
        return _strdup(address_disassembly_map_pIter->second.c_str());
    }
    CodeBlock code_block;
    code_block.StartAddress = StartAddress;
    if (Socket == INVALID_SOCKET)
        return strdup("");

    multimap <va_t, PBasicBlock>::iterator address_map_pIter;
    if (EndAddress == 0)
    {
        address_map_pIter = ClientAnalysisInfo->address_map.find(StartAddress);
        if (address_map_pIter != ClientAnalysisInfo->address_map.end())
        {
            PBasicBlock pBasicBlock = (PBasicBlock)address_map_pIter->second;
            EndAddress = pBasicBlock->EndAddress;
        }
    }
    code_block.EndAddress = EndAddress;
    DisasmLine = NULL;
    if (SendTLVData(GET_DISASM_LINES, (PBYTE)&code_block, sizeof(code_block)))
    {
        char type;
        DWORD length;
        PBYTE data = RecvTLVData(Socket, &type, &length);
        if (data)
            DisasmLine = (char*)data;
        return (char*)data;
    }
    return strdup("");
#else
    char *DisasmLines = m_pStorage->ReadDisasmLine(m_FileID, StartAddress);

    if (DisasmLines)
    {
        Logger.Log(10, LOG_IDA_CONTROLLER, "DisasmLines = %s\n", DisasmLines);
        return DisasmLines;
    }
    return _strdup("");
#endif
}

string IDASession::GetInputName()
{
    string input_name;

    if (SendTLVData(GET_INPUT_NAME, (PBYTE)"", 1))
    {
        char type;
        DWORD length;

        PBYTE data = RecvTLVData(Socket, &type, &length);
        input_name = (char*)data;
    }
    return input_name;
}

void IDASession::RetrieveIdentity()
{
    Identity = GetInputName();
}

string IDASession::GetIdentity()
{
    return Identity;
}

PBasicBlock IDASession::GetBasicBlock(va_t address)
{
    return m_pStorage->ReadBasicBlock(m_FileID, address);
}

void IDASession::FreeDisasmLines()
{
    if (DisasmLine)
        free(DisasmLine);
}

void IDASession::JumpToAddress(unsigned long address)
{
    SendTLVData(JUMP_TO_ADDR, (PBYTE)&address, sizeof(va_t));
}

void IDASession::ColorAddress(unsigned long start_address, unsigned long end_address, unsigned long color)
{
    unsigned long data[3];
    data[0] = start_address;
    data[1] = end_address;
    data[2] = color;
    SendTLVData(COLOR_ADDRESS, (PBYTE)data, sizeof(data));
}

list <BLOCK> IDASession::GetFunctionMemberBlocks(unsigned long function_address)
{
    list <BLOCK> block_list;

    if (ClientAnalysisInfo)
    {
        list <va_t> address_list;
        unordered_set <va_t> checked_addresses;
        address_list.push_back(function_address);

        BLOCK block;
        block.Start = function_address;
        PBasicBlock pBasicBlock = GetBasicBlock(function_address);
        block.End = pBasicBlock->EndAddress;
        block_list.push_back(block);

        checked_addresses.insert(function_address);

        for (va_t currentAddress: address_list)
        {
            int addresses_number;
            va_t *p_addresses = GetMappedAddresses(currentAddress, CREF_FROM, &addresses_number);
            if (p_addresses && addresses_number > 0)
            {
                for (int i = 0; i < addresses_number; i++)
                {
                    va_t address = p_addresses[i];
                    if (address)
                    {
                        if (FunctionHeads.find(address) != FunctionHeads.end())
                            continue;

                        if (checked_addresses.find(address) == checked_addresses.end())
                        {
                            address_list.push_back(address);
                            block.Start = address;
                            PBasicBlock pBasicBlock = GetBasicBlock(address);
                            block.End = pBasicBlock->EndAddress;
                            block_list.push_back(block);

                            checked_addresses.insert(address);
                        }
                    }
                }
                free(p_addresses);
            }
        }
    }
    else
    {
        block_list = m_pStorage->ReadFunctionMemberAddresses(m_FileID, function_address);
    }

    return block_list;
}

void IDASession::MergeBlocks()
{
    multimap <va_t, PMapInfo>::iterator last_iter = ClientAnalysisInfo->map_info_map.end();
    multimap <va_t, PMapInfo>::iterator iter;
    multimap <va_t, PMapInfo>::iterator child_iter;

    int NumberOfChildren = 1;
    for (iter = ClientAnalysisInfo->map_info_map.begin();
        iter != ClientAnalysisInfo->map_info_map.end();
        iter++
        )
    {
        if (iter->second->Type == CREF_FROM)
        {
            BOOL bHasOnlyOneChild = FALSE;
            if (last_iter != ClientAnalysisInfo->map_info_map.end())
            {
                if (last_iter->first == iter->first)
                {
                    NumberOfChildren++;
                }
                else
                {
                    Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d Number Of Children for %X  = %u\n",
                        __FUNCTION__, m_FileID,
                        last_iter->first,
                        NumberOfChildren);
                    if (NumberOfChildren == 1)
                        bHasOnlyOneChild = TRUE;
                    multimap <va_t, PMapInfo>::iterator next_iter = iter;
                    next_iter++;
                    if (next_iter == ClientAnalysisInfo->map_info_map.end())
                    {
                        last_iter = iter;
                        bHasOnlyOneChild = TRUE;
                    }
                    NumberOfChildren = 1;
                }
            }
            if (bHasOnlyOneChild)
            {
                int NumberOfParents = 0;
                for (child_iter = ClientAnalysisInfo->map_info_map.find(last_iter->second->Dst);
                    child_iter != ClientAnalysisInfo->map_info_map.end() && child_iter->first == last_iter->second->Dst;
                    child_iter++)
                {
                    if (child_iter->second->Type == CREF_TO && child_iter->second->Dst != last_iter->first)
                    {
                        Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d Found %X -> %X\n",
                            __FUNCTION__, m_FileID,
                            child_iter->second->Dst, child_iter->first);
                        NumberOfParents++;
                    }
                }
                if (NumberOfParents == 0)
                {
                    Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d Found Mergable Nodes %X -> %X\n",
                        __FUNCTION__, m_FileID,
                        last_iter->first, last_iter->second->Dst);
                }
            }
            last_iter = iter;
        }
    }
}

int IDASession::GetFileID()
{
    return m_FileID;
}

multimap <va_t, va_t> *IDASession::GetFunctionToBlock()
{
    Logger.Log(10, LOG_IDA_CONTROLLER, "LoadFunctionMembersMap\n");
    return &FunctionToBlock;
}

static int ReadAddressToFunctionMapResultsCallback(void *arg, int argc, char **argv, char **names)
{
    unordered_map <va_t, va_t> *AddressToFunctionMap = (unordered_map <va_t, va_t>*)arg;
    if (AddressToFunctionMap)
    {
#if DEBUG_LEVEL > 1
        Logger.Log(10, "%s: ID = %d strtoul10(%s) = 0x%X, strtoul10(%s) = 0x%X\n", __FUNCTION__, m_FileID, argv[0], strtoul10(argv[0]), argv[1], strtoul10(argv[1]));
#endif
        AddressToFunctionMap->insert(pair <va_t, va_t>(strtoul10(argv[0]), strtoul10(argv[1])));
    }
    return 0;
}

void IDASession::LoadBlockToFunction()
{
    int Count = 0;

    Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d GetFunctionAddresses\n", __FUNCTION__);
    list <va_t> *function_addresses = GetFunctionAddresses();
    if (function_addresses)
    {
        Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d Function %u entries\n", __FUNCTION__, m_FileID, function_addresses->size());

        unordered_map<va_t, va_t> addresses;
        unordered_map<va_t, va_t> membership_hash;

        for (va_t address : *function_addresses)
        {
            list <BLOCK> function_member_blocks = GetFunctionMemberBlocks(address);

            for (auto& val : function_member_blocks)
            {
                va_t addr = val.Start;
                BlockToFunction.insert(pair <va_t, va_t>(addr, address));

                if (addresses.find(addr) == addresses.end())
                {
                    addresses.insert(pair<va_t, va_t>(addr, 1));
                }
                else
                {
                    addresses[addr] += 1;
                }

                if (membership_hash.find(addr) == membership_hash.end())
                {
                    membership_hash.insert(pair<va_t, va_t>(addr, address));
                }
                else
                {
                    membership_hash[addr] += address;
                }
            }
        }

        for (auto& val : addresses)
        {
            if (val.second > 1)
            {
                bool function_start = true;
                for (multimap<va_t, va_t>::iterator it2 = CrefToMap.find(val.first);
                    it2 != CrefToMap.end() && it2->first == val.first;
                    it2++
                    )
                {
                    unordered_map<va_t, va_t>::iterator current_membership_it = membership_hash.find(val.first);
                    va_t parent = it2->second;
                    Logger.Log(10, LOG_IDA_CONTROLLER, "Found parent for %X -> %X\n", val.first, parent);
                    unordered_map<va_t, va_t>::iterator parent_membership_it = membership_hash.find(parent);
                    if (current_membership_it != membership_hash.end() && parent_membership_it != membership_hash.end())
                    {
                        if (current_membership_it->second == parent_membership_it->second)
                        {
                            function_start = false;
                            break;
                        }
                    }
                }

                Logger.Log(10, LOG_IDA_CONTROLLER, "Multiple function membership: %X (%d) %s\n", val.first, val.second, function_start ? "Possible Head" : "Member");

                if (function_start)
                {
                    va_t function_start_addr = val.first;
                    FunctionHeads.insert(function_start_addr);
                    list <BLOCK> function_member_blocks = GetFunctionMemberBlocks(function_start_addr);
                    unordered_map<va_t, va_t>::iterator function_start_membership_it = membership_hash.find(function_start_addr);

                    for (list <BLOCK>::iterator it2 = function_member_blocks.begin();
                        it2 != function_member_blocks.end();
                        it2++
                        )
                    {
                        va_t addr = (*it2).Start;

                        unordered_map<va_t, va_t>::iterator current_membership_it = membership_hash.find(addr);

                        if (function_start_membership_it->second != current_membership_it->second)
                            continue;

                        for (multimap <va_t, va_t>::iterator a2f_it = BlockToFunction.find(addr);
                            a2f_it != BlockToFunction.end() && a2f_it->first == addr;
                            a2f_it++
                            )
                        {
                            Logger.Log(10, LOG_IDA_CONTROLLER, "\tRemoving Block: %X Function: %X\n", a2f_it->first, a2f_it->second);
                            a2f_it = BlockToFunction.erase(a2f_it);
                        }
                        BlockToFunction.insert(pair <va_t, va_t>(addr, function_start_addr));
                        Logger.Log(10, LOG_IDA_CONTROLLER, "\tAdding Block: %X Function: %X\n", addr, function_start_addr);
                    }
                }
            }
        }
        function_addresses->clear();
        delete function_addresses;

        for (auto& val : BlockToFunction)
        {
            FunctionToBlock.insert(pair<va_t, va_t>(val.second, val.first));
        }

        Logger.Log(10, LOG_IDA_CONTROLLER, "%s: ID = %d BlockToFunction %u entries\n", __FUNCTION__, m_FileID, BlockToFunction.size());
    }
}

BOOL IDASession::FixFunctionAddresses()
{
    BOOL is_fixed = FALSE;
    Logger.Log(10, LOG_IDA_CONTROLLER, "%s", __FUNCTION__);
    LoadBlockToFunction();

    if (m_pStorage)
        m_pStorage->BeginTransaction();

    for (auto& val : BlockToFunction)
    {
        //StartAddress: val.first
        //FunctionAddress: val.second
        Logger.Log(10, LOG_IDA_CONTROLLER, "Updating BasicBlockTable Address = %X Function = %X\n", val.second, val.first);

        m_pStorage->UpdateBasicBlock(m_FileID, val.first, val.second);
        is_fixed = TRUE;
    }

    if (m_pStorage)
        m_pStorage->EndTransaction();

    ClearBlockToFunction();

    return is_fixed;
}

bool IDASession::SendMatchedAddrTLVData(FunctionMatchInfo& Data)
{
    return SendTLVData(
        MATCHED_ADDR,
        (PBYTE) & (Data),
        sizeof(Data));
}

bool IDASession::SendAddrTypeTLVData(int Type, va_t Start, va_t End)
{
    va_t StartToEnd[2];

    StartToEnd[0] = Start;
    StartToEnd[1] = End;

    return SendTLVData(
        Type,
        (PBYTE)StartToEnd,
        sizeof(StartToEnd));
}

