#pragma warning ( disable: 4819 )
#pragma warning ( disable: 4996 )
#pragma warning ( disable : 4786 )

#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <list>
#include <winsock.h>

#include <graph.hpp>
#include <expr.hpp>
#include <loader.hpp>

#include "Common.h"
#include "SharedSocket.h"
#include "SharedMemory.h"
#include "Configuration.h"
#include "SocketOperation.h"

#include "IDAAnalyzer.h"
#include "SQLiteDisassemblyStorage.h"
#include "DisassemblyStorage.h"
#include "FileVersion.h"
#include "Log.h"

using namespace std;

void SaveIDAAnalysis(bool ask_file_path);

#define DREF 0
#define CREF 1
#define FUNCTION 2
#define STACK 3
#define NAME 4
#define DISASM_LINE 5
#define DATA_TYPE 6

extern HANDLE gLogFile;

static error_t idaapi idc_set_log_file(
    idc_value_t *argv,
    idc_value_t *res)
{
    gLogFile = OpenLogFile(argv[0].c_str());
    res->num = 1;
    return eOk;
}

static const char idc_set_log_file_args[] = { VT_STR, 0 };

static const ext_idcfunc_t idc_set_log_file_desc =
{
  "SetLogFile",
  idc_set_log_file,
  idc_set_log_file_args,
  NULL,
  0,
  0
};


char *OutputFilename = NULL;

ea_t StartEA = 0;
ea_t EndEA = 0;

static const char idc_save_analysis_data_args[] = { VT_STR, VT_LONG, VT_LONG, 0 };
static error_t idaapi idc_save_analysis_data(idc_value_t *argv, idc_value_t *res)
{
    OutputFilename = strdup(argv[0].c_str());
    StartEA = argv[1].num;
    EndEA = argv[2].num;

    SaveIDAAnalysis(false);
    res->num = 1;
    return eOk;
}

static const ext_idcfunc_t idc_save_analysis_data_desc =
{
  "SaveAnalysisData",
  idc_save_analysis_data,
  idc_save_analysis_data_args,
  NULL,
  0,
  0
};

BOOL ConnectToDarunGrim(unsigned short port);

static error_t idaapi idc_connect_to_darungrim(
    idc_value_t *argv,
    idc_value_t *res)
{
    LogMessage(1, __FUNCTION__, "%s\n", __FUNCTION__);
    OutputFilename = NULL;
    unsigned short port = argv[0].num;

    ConnectToDarunGrim(port);
    res->num = 1;
    return eOk;
}

static const char idc_connect_to_darungrim_args[] = { VT_LONG, 0 };

static const ext_idcfunc_t idc_connect_to_darungrim_desc =
{
  "ConnectToDarunGrim",
  idc_connect_to_darungrim,
  idc_connect_to_darungrim_args,
  NULL,
  0,
  0
};

int idaapi init(void)
{
    add_idc_func(idc_save_analysis_data_desc);
    add_idc_func(idc_connect_to_darungrim_desc);
    add_idc_func(idc_set_log_file_desc);
    return PLUGIN_KEEP;
}

void idaapi term(void)
{
    del_idc_func(idc_save_analysis_data_desc.name);
    del_idc_func(idc_connect_to_darungrim_desc.name);
    del_idc_func(idc_set_log_file_desc.name);
}

bool IsNumber(char *data)
{
    bool is_number = TRUE;
    //hex
    if (strlen(data) > 1 && data[strlen(data) - 1] == 'h')
    {
        int i = 0;
        while (i < strlen(data) - 2)
        {
            if (
                ('0' <= data[i] && data[i] <= '9') ||
                ('a' <= data[i] && data[i] <= 'f') ||
                ('A' <= data[i] && data[i] <= 'F')
                )
            {
            }
            else {
                is_number = FALSE;
                break;
            }
            i++;
        }
    }
    else {
        int i = 0;
        while (data[i])
        {
            if ('0' <= data[i] && data[i] <= '9')
            {
            }
            else {
                is_number = FALSE;
                break;
            }
            i++;
        }
    }
    return is_number;
}

typedef list<FunctionMatchInfo*> RangeList;
typedef struct _ChooseListObj_ {
    SOCKET socket;
    RangeList range_list;
} ChooseListObj,  *PChooseListObj;

const int column_widths[] = { 16, 32, 5, 5, 16, 32, 5, 5 };
const char *column_header[] =
{
    "Address",
    "Name",
    "Matched",
    "Unmatched",
    "Address",
    "Name",
    "Matched",
    "Unmatched"
};

static DWORD idaapi size_callback(void *obj)
{
    RangeList range_list = ((PChooseListObj)obj)->range_list;
    return range_list.size();
}

static void idaapi line_callback(void *obj, DWORD n, char *const *arrptr)
{
    RangeList range_list = ((PChooseListObj)obj)->range_list;
    RangeList::iterator range_list_itr;
    DWORD i;

    qsnprintf(arrptr[0], MAXSTR, "Unknown");
    qsnprintf(arrptr[1], MAXSTR, "Unknown");
    qsnprintf(arrptr[2], MAXSTR, "Unknown");
    qsnprintf(arrptr[3], MAXSTR, "Unknown");

    if (n == 0)
    {
        for (int i = 0; i < qnumber(column_header); i++)
            qsnprintf(arrptr[i], MAXSTR, column_header[i]);

        return;
    }
    for (range_list_itr = range_list.begin(), i = 0;
        range_list_itr != range_list.end();
        range_list_itr++, i++)
    {
        if (i == n - 1)
        {
            qsnprintf(arrptr[0], MAXSTR, "%X", (*range_list_itr)->SourceAddress);
            qsnprintf(arrptr[1], MAXSTR, "%s", (*range_list_itr)->SourceFunctionName);

            qsnprintf(arrptr[2], MAXSTR, "%5d", (*range_list_itr)->MatchCountForTheSource);
            qsnprintf(arrptr[3], MAXSTR, "%5d", (*range_list_itr)->NoneMatchCountForTheSource);


            qsnprintf(arrptr[4], MAXSTR, "%X", (*range_list_itr)->TargetAddress);
            qsnprintf(arrptr[5], MAXSTR, "%s", (*range_list_itr)->TargetFunctionName);

            qsnprintf(arrptr[6], MAXSTR, "%5d", (*range_list_itr)->MatchCountForTheTarget);
            qsnprintf(arrptr[7], MAXSTR, "%5d", (*range_list_itr)->NoneMatchCountForTheTarget);

            break;
        }
    }
}

static void idaapi enter_callback(void *obj, DWORD n)
{
    RangeList range_list = ((PChooseListObj)obj)->range_list;
    RangeList::iterator range_list_itr;
    DWORD i;

    for (range_list_itr = range_list.begin(), i = 0;
        range_list_itr != range_list.end();
        range_list_itr++, i++)
    {
        if (i == n - 1)
        {
            LogMessage(1, __FUNCTION__, "Jump to %X\n", (*range_list_itr)->SourceAddress);
            jumpto((*range_list_itr)->SourceAddress);
            SendTLVData(
                ((PChooseListObj)obj)->socket,
                SHOW_MATCH_ADDR,
                (PBYTE) & (*range_list_itr)->SourceAddress,
                sizeof(DWORD));
            break;
        }
    }
}

static int idaapi graph_callback(void *obj, int code, va_list va)
{
    int result = 0;
    if (!obj)
        return result;
    switch (code)
    {
    case grcode_dblclicked:	 // a graph node has been double clicked
        // in:	graph_viewer_t *gv
        //			selection_item_t *current_item
        // out: 0-ok, 1-ignore click
    {
        graph_viewer_t *v = va_arg(va, graph_viewer_t*);
        selection_item_t *s = va_arg(va, selection_item_t*);
        //LogMessage( "%X: %sclicked on ", v, code == grcode_clicked ? "" : "dbl" );
        if (s && s->is_node)
        {
            DWORD addr = get_screen_ea();
            //LogMessage( "node %d( %X )\n", s->node, addr );
            LogMessage(1, __FUNCTION__, "Showing Block %X\n", addr);
            SendTLVData(
                ((PChooseListObj)obj)->socket,
                SHOW_MATCH_ADDR,
                (PBYTE)&addr,
                sizeof(DWORD));
        }
    }
    break;
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct _EARange_ {
    ea_t start;
    ea_t end;
} EARange;

typedef list<EARange> EARangeList;
const int column_widths_for_unidentified_block_choose_list[] = { 16, 16 };
const char *column_header_for_unidentified_block_choose_list[] =
{
    "Start",
    "End"
};

static DWORD idaapi size_callback_for_unidentified_block_choose_list(void *obj)
{
    return ((EARangeList*)obj)->size();
}

static void idaapi enter_callback_for_unidentified_block_choose_list(void *obj, DWORD n)
{
    EARangeList::iterator range_list_itr;
    DWORD i;

    for (range_list_itr = ((EARangeList*)obj)->begin(), i = 0;
        range_list_itr != ((EARangeList*)obj)->end();
        range_list_itr++, i++)
    {
        if (i == n - 1)
        {
            LogMessage(1, __FUNCTION__, "Jump to %X\n", (*range_list_itr).start);
            jumpto((*range_list_itr).start);
            break;
        }
    }
}

static void idaapi line_callback_for_unidentified_block_choose_list(void *obj, DWORD n, char *const *arrptr)
{
    EARangeList::iterator range_list_itr;
    DWORD i;

    qsnprintf(arrptr[0], MAXSTR, "Unknown");
    qsnprintf(arrptr[1], MAXSTR, "Unknown");


    if (n == 0)
    {
        for (int i = 0; i < qnumber(column_header_for_unidentified_block_choose_list); i++)
            qsnprintf(arrptr[i], MAXSTR, column_header_for_unidentified_block_choose_list[i]);
        return;
    }
    for (range_list_itr = ((EARangeList*)obj)->begin(), i = 0;
        range_list_itr != ((EARangeList*)obj)->end();
        range_list_itr++, i++)
    {
        if (i == n - 1)
        {
            qsnprintf(arrptr[0], MAXSTR, "%X", (*range_list_itr).start);
            qsnprintf(arrptr[1], MAXSTR, "%X", (*range_list_itr).end);
            break;
        }
    }
}

int idaapi graph_viewer_callback(void *user_data, int notification_code, va_list va)
{
    LogMessage(1, __FUNCTION__, "graph_viewer_callback called with notification_code=%d\n", notification_code);
    if (notification_code == grcode_dblclicked)
    {
        ea_t addr = get_screen_ea();

        SOCKET socket = (SOCKET)user_data;
        LogMessage(1, __FUNCTION__, "Showing Block %X(socket=%d)\n", addr, socket);
        SendTLVData(
            socket,
            SHOW_MATCH_ADDR,
            (PBYTE)&addr,
            sizeof(ea_t)
        );
    }
    return 0;
}

ChooseListObj unidentified_block_choose_list_obj;
EARangeList unidentified_block_choose_list;
ChooseListObj matched_block_choose_list_obj;

bool graph_viewer_callback_installed = FALSE;

int ProcessCommandFromDarunGrim(SOCKET data_socket, char type, DWORD length, PBYTE data)
{
    if (type == SEND_ANALYSIS_DATA)
    {
        DataSharer data_sharer;
        DWORD size = 0;
        memcpy(&size, data, sizeof(DWORD));
        if (!InitDataSharer(&data_sharer,
            (char*)data + sizeof(DWORD),
            size,
            FALSE))
        {
            return 0;
        }

        //TODO: Analyze( ( bool (  *)( PVOID context, BYTE type, PBYTE data, DWORD length ) )PutData, ( PVOID )&data_sharer, 0, 0 );
    }
    else if (type == UNINDENTIFIED_ADDR || type == MODIFIED_ADDR)
    {
        EARange ea_range;

        int color = 0x000000;
        if (type == UNINDENTIFIED_ADDR)
        {
            color = 0x0000ff;
        }
        else if (type == MODIFIED_ADDR)
        {
            color = 0x00ffff;
        }

        for (DWORD i = 0; i < length / (sizeof(DWORD)  *2); i++)
        {
            ea_range.start = (get_imagebase() & 0xFFFFFFFF00000000) + ((DWORD*)data)[i  *2];
            ea_range.end = (get_imagebase() & 0xFFFFFFFF00000000) + ((DWORD*)data)[i  *2 + 1];
            unidentified_block_choose_list.push_back(ea_range);

            for (
                ea_t ea = ea_range.start;
                ea < ea_range.end;
                ea = next_that(ea, ea_range.end, f_is_code, NULL)
                )
            {
                set_item_color(ea, color);
            }
        }
    }
    else if (type == MATCHED_ADDR && sizeof(FunctionMatchInfo) <= length)
    {
        FunctionMatchInfo *p_match_info = (FunctionMatchInfo*)data;
        if (p_match_info->BlockType == FUNCTION_BLOCK)
        {
            matched_block_choose_list_obj.range_list.push_back(p_match_info);
        }

        int color = 0x00ff00;
        if (p_match_info->MatchRate != 100)
        {
            color = 0x00ffff;
        }

        for (
            ea_t ea = (get_imagebase() & 0xFFFFFFFF00000000) + p_match_info->SourceAddress;
            ea < (get_imagebase() & 0xFFFFFFFF00000000) + p_match_info->EndAddress;
            ea = next_that(ea, (get_imagebase() & 0xFFFFFFFF00000000) + p_match_info->EndAddress, f_is_code, NULL)
            )
        {
            set_item_color(ea, color);
        }
    }
    else if (type == JUMP_TO_ADDR && length >= 4)
    {
        if (sizeof(ea_t) > sizeof(DWORD))
        {
            ea_t orig_addr = (get_imagebase() & 0xFFFFFFFF00000000) + *(DWORD*)data;
            jumpto(orig_addr);
        }
        else
        {
            jumpto(*(DWORD*)data);
        }
    }
    else if (type == COLOR_ADDRESS && length >= sizeof(unsigned long)  *3)
    {
        ea_t start_address = (get_imagebase() & 0xFFFFFFFF00000000) + ((DWORD*)data)[0];
        ea_t end_address = (get_imagebase() & 0xFFFFFFFF00000000) + ((DWORD*)data)[1];
        unsigned long color = ((DWORD*)data)[2];

        for (
            ea_t ea = start_address;
            ea <= end_address;
            ea = next_that(ea, end_address, f_is_code, NULL)
            )
        {
            set_item_color(ea, color);
        }

        if (!graph_viewer_callback_installed)
        {
            /*TODO:
            TForm *tform = find_tform( "IDA View-A" );
            if( tform )
            {
                graph_viewer_t *graph_viewer = get_graph_viewer( tform );

                if( graph_viewer )
                {
                    mutable_graph_t *mutable_graph = get_viewer_graph( graph_viewer );
                    if( mutable_graph )
                    {
                        mutable_graph->set_callback( graph_viewer_callback, ( void  *)data_socket );
                        graph_viewer_callback_installed = TRUE;
                    }
                }
            }*/
        }
    }
    else if (type == GET_DISASM_LINES && length >= sizeof(CodeBlock))
    {
        //dump disasmline
        char *disasm_buffer = NULL;
        CodeBlock *p_code_block = (CodeBlock*)data;
        qstring op_buffer;
        int current_buffer_offset = 0;
        int new_buffer_offset = 0;
        for (ea_t current_address = p_code_block->StartAddress; current_address < p_code_block->EndAddress; current_address += get_item_size(current_address))
        {
            generate_disasm_line(&op_buffer, current_address);
            tag_remove(&op_buffer);
            op_buffer += "\n";

            new_buffer_offset = current_buffer_offset + op_buffer.length();
            disasm_buffer = (char*)realloc(disasm_buffer, new_buffer_offset + 1);
            memcpy(disasm_buffer + current_buffer_offset, op_buffer.c_str(), op_buffer.length() + 1);
            current_buffer_offset = new_buffer_offset;
        }

        if (disasm_buffer)
        {
            SendTLVData(data_socket, DISASM_LINES, (PBYTE)disasm_buffer, strlen(disasm_buffer) + 1);
            free(disasm_buffer);
        }
        else
        {
            SendTLVData(data_socket, DISASM_LINES, (PBYTE)"", 1);
        }
    }
    else if (type == SHOW_DATA)
    {
        matched_block_choose_list_obj.socket = data_socket;

        /*TODO: choose2(
            0,
            -1, -1, -1, -1,
            &unidentified_block_choose_list,
            qnumber( column_header_for_unidentified_block_choose_list ),
            column_widths_for_unidentified_block_choose_list,
            size_callback_for_unidentified_block_choose_list,
            line_callback_for_unidentified_block_choose_list,
            "Unidentified Blocks",
            -1,
            0,
            NULL,
            NULL,
            NULL,
            NULL,
            enter_callback_for_unidentified_block_choose_list,
            NULL,
            NULL,
            NULL );
        choose2(
            0,
            -1, -1, -1, -1,
            &matched_block_choose_list_obj,
            qnumber( column_header ),
            column_widths,
            size_callback,
            line_callback,
            "Matched Blocks",
            -1,
            0,
            NULL,
            NULL,
            NULL,
            NULL,
            enter_callback,
            NULL,
            NULL,
            NULL );*/
#ifdef HT_GRAPH
        hook_to_notification_point(HT_GRAPH, graph_callback, (void*)&matched_block_choose_list_obj);
#endif
    }
    else if (type == GET_INPUT_NAME)
    {
        char buf[512] = { 0, };
        get_input_file_path(buf, sizeof(buf));
        SendTLVData(data_socket, INPUT_NAME, (PBYTE)buf, strlen(buf) + 1);
    }

    return 0;
}

BOOL ConnectToDarunGrim(unsigned short port)
{
    LogMessage(1, __FUNCTION__, "Connecting to DarunGrim GUI on port %d...\n", port);
    SOCKET data_socket = ConnectToServer("127.0.0.1", port);
    if (data_socket != INVALID_SOCKET)
    {
        LogMessage(1, __FUNCTION__, "Connected to DarunGrim GUI on port %d\n", port);
        SetSharedSocketDataReceiver(ProcessCommandFromDarunGrim);
        PutSocketToWSAAsyncSelect(data_socket, SharedSocketDataReceiverWndProc, WM_SHARED_SOCKET_EVENT);
        return TRUE;
    }
    else
    {
        LogMessage(1, __FUNCTION__, "Failed to connect to DarunGrim GUI on port %d\n", port);
    }
    return FALSE;
}

bool FileWriterWrapper(PVOID Context, BYTE Type, PBYTE Data, DWORD Length)
{
    BOOL Status = FALSE;
    HANDLE hFile = (HANDLE)Context;
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD NumberOfBytesWritten;
        Status = WriteFile(
            hFile,
            (LPCVOID)&Type,
            sizeof(Type),
            &NumberOfBytesWritten,
            NULL
        );
        if (Status && sizeof(Type) == NumberOfBytesWritten)
        {
            Status = WriteFile(
                hFile,
                (LPCVOID)&Length,
                sizeof(Length),
                &NumberOfBytesWritten,
                NULL
            );
        }
        else
        {
            Status = FALSE;
        }
        if (Status && sizeof(Length) == NumberOfBytesWritten)
        {
            Status = WriteFile(
                hFile,
                (LPCVOID)Data,
                Length,
                &NumberOfBytesWritten,
                NULL
            );
        }
        else
        {
            Status = FALSE;
        }
        if (Status && Length == NumberOfBytesWritten)
        {
        }
        else
        {
            Status = FALSE;
        }
    }
    return Status;
}

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
void SaveIDAAnalysis(bool ask_file_path)
{
    long start_tick = GetTickCount();

    char orignal_file_path[1024] = { 0, };
    char root_file_path[1024] = { 0, };
    char *input_file_path = NULL;
    get_input_file_path(orignal_file_path, sizeof(orignal_file_path) - 1);
    get_root_filename(root_file_path, sizeof(root_file_path) - 1);

    if (ask_file_path)
    {
        input_file_path = ask_file(true, "*.db", "Select DB File to Output");
        if (input_file_path == NULL)
        {
            LogMessage(1, __FUNCTION__, "input_file_path == NULL\n");
            return;
        }
    }

    LogMessage(1, __FUNCTION__, "input_file_path = [%s]\n", input_file_path);

    if (input_file_path)
    {
        SQLiteDisassemblyStorage disassemblyStorage(input_file_path);
        IDAAnalyzer idaAnalysis = IDAAnalyzer((DisassemblyStorage) disassemblyStorage);
        idaAnalysis.Analyze(StartEA, EndEA, false);
    }

    long end_tick = GetTickCount();
    LogMessage(1, __FUNCTION__, "DarunGrim Analysis Finished %.3f sec\n", (float)(end_tick - start_tick) / 1000);
}

bool idaapi run(size_t arg)
{
    SetLogLevel(1);
    LogMessage(1, __FUNCTION__, "DarunGrim plugin started...\n");

    if (arg == 1)
    {
        return false;
    }

    // Display a dialog box
    char *ask_message =
        "STARTITEM 0\n"
        "DarunGrim4\n\n"
        "<##Select operation##Save to DGF:r>\n"
        "<Connect to DarunGrim GUI:R>\n";

    ushort radio = 0;

    if (ask_form(ask_message, &radio) == 1)
    {
        if (radio == 0)
        {
            SaveIDAAnalysis(true);
        }
        else if (radio == 1)
        {
            char *dialog =
                "DarunGrim4\n\n"
                "Check Options->Server menu\n"
                "from DarunGrim4 GUI to get the port information\n"
                "<Port:D:10:10::>\n";
            sval_t port = DARUNGRIM_PORT;
            if (ask_form(dialog, &port) == 1)
            {
                ConnectToDarunGrim(port);
            }
        }
    }

    return true;
}

char comment[] = "This is a DarunGrim Plugin";
char help[] =
"A DarunGrim Plugin module\n"
"This module let you analyze differences in two binaries.\n";

char wanted_name[] = "DarunGrim";
char wanted_hotkey[] = "Alt-7";

plugin_t PLUGIN =
{
    IDP_INTERFACE_VERSION,
    0,
    init,
    term,
    run,
    comment,
    help,
    wanted_name,
    wanted_hotkey
};
