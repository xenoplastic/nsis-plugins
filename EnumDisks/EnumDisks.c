#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <tchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <shlwapi.h>
#pragma comment( lib, "shlwapi.lib" )
#include <setupapi.h>
#pragma comment( lib, "setupapi.lib" )
#include <devguid.h>
#pragma comment( lib, "ntdll.lib" )

#include "plugin.h"

//
// PLUGINAPI
//
#if defined(__cplusplus)
#define PLUGINAPI(Name_) extern "C" void __cdecl Name_(HWND hwndParent, int nLength, LPTSTR variables, stack_t **stacktop, extra_parameters *extra, ...)
#else
#define PLUGINAPI(Name_) void __cdecl Name_(HWND hwndParent, int nLength, LPTSTR variables, stack_t **stacktop, extra_parameters *extra, ...)
#endif

//
// DiskInfoData
//
typedef struct _DiskInfoData {
	TCHAR szEnumeratorName[256 + 1];
	TCHAR szFriendlyName[256 + 1];
	TCHAR szSerialNumber[126 + 1];
	TCHAR szProductId[126 + 1];
	TCHAR szVendorId[126 + 1];
	INT64 llLength;
} DiskInfoData;

//
// DiskInfoProc
//
typedef void (CALLBACK *DiskInfoProc)(LPTSTR, int, const DiskInfoData *);

//
// DiskInfoEntry
//
typedef struct _DiskInfoEntry {
	SLIST_ENTRY ent;
	DiskInfoData di;
} DiskInfoEntry;

//
// DiskInfoProp
//
typedef struct _DiskInfoProp {
	DWORD dwProp;
	BYTE *pbData;
	DWORD cbData;
} DiskInfoProp;

//
// DiskInfoType
//
typedef enum _DiskInfoType {
	DEV_INFO_ENUM,
	DEV_INFO_NAME,
	DEV_INFO_SERN,
	DEV_INFO_PROD,
	DEV_INFO_VEND,
	DEV_INFO_SIZE,
} DiskInfoType;

//
// DiskInfoParam
//
typedef struct _DiskInfoParam {
	int nIndex;
	int nType;
} DiskInfoParam;

//
// Global variables
//
static SLIST_HEADER hdr;
static HMODULE hModule;
static HANDLE hHeap;

//
// PluginCallback
//
static UINT_PTR PluginCallback(NSPIM msg)
{
	return 0;
}

//
// DiskInfoStrCvt
//
#if !defined(UNICODE) && !defined(_UNICODE)
#define DiskInfoStrCvtW2T(pszInfo, cchInfo, pvData) (void)WideCharToMultiByte(CP_ACP, 0, (LPCWCH)pvData, -1, pszInfo, cchInfo, NULL, NULL);
#define DiskInfoStrCvtT2W(pszInfo, cchInfo, pvData) (void)MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pvData, -1, pszInfo, cchInfo);
#define DiskInfoStrCvtA2T(pszInfo, cchInfo, pvData) (void)lstrcpynA(pszInfo, (LPCSTR)pvData, cchInfo);
#define DiskInfoStrCvtT2A(pszInfo, cchInfo, pvData) (void)lstrcpynA(pszInfo, (LPCSTR)pvData, cchInfo);
#else
#define DiskInfoStrCvtT2A(pszInfo, cchInfo, pvData) (void)WideCharToMultiByte(CP_ACP, 0, (LPCWCH)pvData, -1, pszInfo, cchInfo, NULL, NULL);
#define DiskInfoStrCvtA2T(pszInfo, cchInfo, pvData) (void)MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pvData, -1, pszInfo, cchInfo);
#define DiskInfoStrCvtT2W(pszInfo, cchInfo, pvData) (void)lstrcpynW(pszInfo, (LPCWCH)pvData, cchInfo);
#define DiskInfoStrCvtW2T(pszInfo, cchInfo, pvData) (void)lstrcpynW(pszInfo, (LPCWCH)pvData, cchInfo);
#endif

//
// IS_PTR
//
static BOOL IS_PTR(const void *ptr)
{
	return (ptr != nullptr);
}

//
// DISK_INFO_BUFFER
//
typedef BYTE DiskGeometryExBuffer[FIELD_OFFSET(DISK_GEOMETRY_EX, Data) + sizeof(DISK_PARTITION_INFO) + sizeof(DISK_DETECTION_INFO)];

typedef BYTE DiskDescriptorBuffer[FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) + 1024UL];

typedef union _DISK_INFO_BUFFER {
	DISK_GEOMETRY_EX dg;
	DiskGeometryExBuffer buf0;
	STORAGE_DEVICE_DESCRIPTOR dd;
	DiskDescriptorBuffer buf1;
} DISK_INFO_BUFFER;

//
// DiskInfoQueryProperty
//
static BOOL CALLBACK DiskInfoQueryProperty(HANDLE hFile, SP_DEVINFO_DATA *pspdd, DiskInfoData *pdi)
{
	BOOL bResult;

	DWORD cbData = sizeof(DISK_INFO_BUFFER);
	BYTE *pbData = (BYTE *)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, cbData);
	if (pbData != NULL)
	{
		DISK_INFO_BUFFER *pdib = (DISK_INFO_BUFFER *)pbData;
		STORAGE_PROPERTY_QUERY spq;
		spq.PropertyId = StorageDeviceProperty;
		spq.QueryType = PropertyStandardQuery;

		do {
			DWORD dwOffset;
			DWORD cbRead;

			bResult = DeviceIoControl(hFile, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), pbData, cbData, &cbRead, NULL);
			if (!bResult)
			{
				break;
			}

			if ((dwOffset = pdib->dd.SerialNumberOffset) != -1UL)
			{
				DiskInfoStrCvtA2T(pdi->szSerialNumber, ARRAYSIZE(pdi->szSerialNumber), &pbData[dwOffset]);
			}
			else
			{
				pdi->szSerialNumber[0] = _T('\0');
			}

			if ((dwOffset = pdib->dd.ProductIdOffset) > 0)
			{
				DiskInfoStrCvtA2T(pdi->szProductId, ARRAYSIZE(pdi->szProductId), &pbData[dwOffset]);
			}
			else
			{
				pdi->szProductId[0] = _T('\0');
			}

			if ((dwOffset = pdib->dd.VendorIdOffset) > 0)
			{
				DiskInfoStrCvtA2T(pdi->szVendorId, ARRAYSIZE(pdi->szVendorId), &pbData[dwOffset]);
			}
			else
			{
				pdi->szVendorId[0] = _T('\0');
			}

			bResult = DeviceIoControl(hFile, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, pbData, cbData, &cbRead, NULL);
			if (bResult)
			{
				pdi->llLength = pdib->dg.DiskSize.QuadPart;
			}
		} while (0);

		HeapFree(hHeap, 0, pbData);
	}
	else
	{
		bResult = FALSE;
	}

	return bResult;
}

//
// DiskInfoInitialize
//
static void CALLBACK DiskInfoInitialize(const GUID *lpGuid, LPCTSTR pszEnum)
{
	HDEVINFO hDevInfo = SetupDiGetClassDevs(lpGuid, pszEnum, NULL, DIGCF_PRESENT);
	if (hDevInfo != INVALID_HANDLE_VALUE)
	{
		TCHAR szRoot[] = { _T('\\'), _T('\\'), _T('.'), _T('\\'), _T('G'), _T('l'), _T('o'), _T('b'), _T('a'), _T('l'), _T('R'), _T('o'), _T('o'), _T('t') };
		TCHAR szPath[ARRAYSIZE(szRoot) + _MAX_FNAME];

		DiskInfoData did = { 0 };
		DiskInfoProp aip[] = {
			{ SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, (BYTE *)&szPath[ARRAYSIZE(szRoot)], sizeof(TCHAR) * _MAX_FNAME },
			{ SPDRP_ENUMERATOR_NAME, (BYTE *)did.szEnumeratorName, sizeof(did.szEnumeratorName) },
			{ SPDRP_FRIENDLYNAME, (BYTE *)did.szFriendlyName, sizeof(did.szFriendlyName) },
		};

		memcpy(szPath, szRoot, sizeof(szRoot));
		RtlInitializeSListHead(&hdr);

		SP_DEVINFO_DATA spdd = { 0 };
		spdd.cbSize = sizeof(spdd);

		WORD wIndex = 0;

		while (SetupDiEnumDeviceInfo(hDevInfo, wIndex++, &spdd))
		{
			for (size_t i = 0; i < ARRAYSIZE(aip); i++)
			{
				DiskInfoProp *pip = (DiskInfoProp *)&aip[i];
				DWORD dwType;
				DWORD cbRead;
				BOOL bResult = SetupDiGetDeviceRegistryProperty(hDevInfo, &spdd, pip->dwProp, &dwType, pip->pbData, pip->cbData, &cbRead);
				if (!bResult)
				{
					goto CleanUp;
				}
			}

			HANDLE hDevice = CreateFile(szPath, FILE_ANY_ACCESS, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
			if (hDevice != INVALID_HANDLE_VALUE)
			{
				BOOL bResult = DiskInfoQueryProperty(hDevice, &spdd, &did);
				CloseHandle(hDevice);
				if (!bResult)
				{
					continue;
				}

				PVOID pvHeap = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(DiskInfoEntry));
				if (!IS_PTR(pvHeap))
				{
					continue;
				}

				DiskInfoEntry *pdie = (DiskInfoEntry *)pvHeap;
				memcpy(&pdie->di, &did, sizeof(did));
				RtlInterlockedPushEntrySList(&hdr, &pdie->ent);
			}
		}

	CleanUp:
		SetupDiDestroyDeviceInfoList(hDevInfo);
	}
}

//
// DiskDataInfoHandler
//
static void CALLBACK DiskInfoTypeHandler(LPTSTR pszData, int nLength, DiskInfoData *pdi, int nType)
{
	switch (nType) {
	case DEV_INFO_ENUM:
		_tcsncpy(pszData, pdi->szEnumeratorName, nLength);
		break;
	case DEV_INFO_NAME:
		_tcsncpy(pszData, pdi->szFriendlyName, nLength);
		break;
	case DEV_INFO_SERN:
		_tcsncpy(pszData, pdi->szSerialNumber, nLength);
		break;
	case DEV_INFO_PROD:
		_tcsncpy(pszData, pdi->szProductId, nLength);
		break;
	case DEV_INFO_VEND:
		_tcsncpy(pszData, pdi->szVendorId, nLength);
		break;
	case DEV_INFO_SIZE:
		_sntprintf(pszData, nLength, _T("%I64d"), pdi->llLength);
		break;
	default:
		break;
	}
}

//
// DiskInfoEntryHandler
//
static void CALLBACK DiskInfoEntryHandler(LPTSTR pszData, int nLength, DiskInfoParam *pdip)
{
	SLIST_ENTRY *pent = RtlFirstEntrySList(&hdr);
	for (int nIndex = 0; (pent != NULL) && (nIndex < pdip->nIndex); nIndex++)
	{
		pent = pent->Next;
	}

	if (IS_PTR(pent))
	{
		DiskInfoTypeHandler(pszData, nLength, &((DiskInfoEntry *)pent)->di, pdip->nType);
	}
}

//
// DiskInfoParamHandler
//
static void CALLBACK DiskInfoParamHandler(stack_t **stacktop, int nLength, DiskInfoParam *pdip)
{
	if (*stacktop != NULL)
	{
		stack_t *th = *stacktop;
		pdip->nIndex = StrToInt(th->text);
		*stacktop = th->next;
		GlobalFree(th);
	}

	stack_t *th = (stack_t *)GlobalAlloc(GPTR, sizeof(stack_t) + nLength * sizeof(TCHAR));
	if (th != NULL)
	{
		DiskInfoEntryHandler(th->text, nLength, pdip);
		th->next = *stacktop;
		*stacktop = th;
	}
}

//
// Init
//
PLUGINAPI(Init)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		if (*stacktop != NULL)
		{
			stack_t *th = *stacktop;
			DiskInfoInitialize(&GUID_DEVCLASS_DISKDRIVE, StrCmpC(th->text, _T("*")) != 0 ? th->text : NULL);
			*stacktop = th->next;
			GlobalFree(th);
		}
	}
}

//
// Count
//
PLUGINAPI(Count)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		stack_t *th = (stack_t *)GlobalAlloc(GPTR, sizeof(stack_t) + nLength * sizeof(TCHAR));
		if (th != NULL)
		{
			_sntprintf(th->text, nLength, _T("%hu"), RtlQueryDepthSList(&hdr));
			th->next = *stacktop;
			*stacktop = th;
		}
	}
}

//
// Clear
//
PLUGINAPI(Clear)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		SLIST_ENTRY *pent;
		while (IS_PTR(pent = RtlInterlockedPopEntrySList(&hdr)))
		{
			RtlInterlockedFlushSList(&hdr);
			HeapFree(hHeap, 0, pent);
		}
	}
}

//
// GetEnumerator
//
PLUGINAPI(GetEnumerator)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DEV_INFO_ENUM;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetFriendlyName
//
PLUGINAPI(GetFriendlyName)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DEV_INFO_NAME;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetSerialNumber
//
PLUGINAPI(GetSerialNumber)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DEV_INFO_SERN;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetProductId
//
PLUGINAPI(GetProductId)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DEV_INFO_PROD;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetVendorId
//
PLUGINAPI(GetVendorId)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DEV_INFO_VEND;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetDiskSize
//
PLUGINAPI(GetDiskSize)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DEV_INFO_SIZE;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

#if defined(_DEBUG)

int _tmain( int argc, _TCHAR *argv[] )
{
	TCHAR rgcInfo[64] = { 0 };
	DWORD cchInfo = ARRAYSIZE(rgcInfo);

	SLIST_ENTRY *curr;
	SLIST_ENTRY *next;

	_tsetlocale(LC_CTYPE, _T(".ACP"));

	hHeap = GetProcessHeap();

	if (argc > 1)
	{
		SetupDiInitDeviceInfo(&GUID_DEVCLASS_DISKDRIVE, StrCmpC(argv[1], _T("*")) != 0 ? argv[1] : NULL);
	}
	else
	{
		SetupDiInitDeviceInfo(&GUID_DEVCLASS_DISKDRIVE, NULL);
	}

	for (curr = RtlFirstEntrySList(&hdr); curr != NULL; curr = next)
	{
		DiskInfoEntry *pdie = (DiskInfoEntry *)curr;

		_tprintf(_T("============================================================\n"));
		_tprintf(_T("枚举类型: [%s]\n"), pdie->di.szEnumeratorName);
		_tprintf(_T("友好名称: [%s]\n"), pdie->di.szFriendlyName);
		_tprintf(_T("产品序列: [%s]\n"), pdie->di.szSerialNumber);
		_tprintf(_T("产品标识: [%s]\n"), pdie->di.szProductId);
		_tprintf(_T("厂家标识: [%s]\n"), pdie->di.szVendorId);
		_i64tot_s(pdie->di.llLength, rgcInfo, cchInfo, 10);
		_tprintf(_T("磁盘大小: [%s]\n"), rgcInfo);

		next = curr->Next;

		HeapFree(hHeap, 0, curr);
	}

	return !getchar();
}

#else

//
// DllMain
//
BOOL WINAPI _DllMainCRTStartup(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		hModule = (HMODULE)hinstDLL;
		hHeap = GetProcessHeap();
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		return TRUE;
	default:
		return TRUE;
	}
}

#endif