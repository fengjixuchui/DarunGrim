#pragma once
#pragma warning(disable:4200) 
#pragma pack(push)
#pragma pack(4)

typedef struct _TLV_ {
	char Type;
	DWORD Length;
	char Data[];
} TLV,*PTLV;

#pragma pack(pop)