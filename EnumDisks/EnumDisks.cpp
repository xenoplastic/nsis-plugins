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
typedef struct : SLIST_ENTRY {
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
// DiskInfoParam
//
typedef struct _DiskInfoParam {
	int nIndex;
} DiskInfoParam;

//
// ListInfoProc
//
typedef void (CALLBACK *ListInfoProc)(LPTSTR, int, DiskInfoParam *);

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
#define DiskInfoStrCvtW2T(pszInfo, cchInfo, pvData) (void)WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<LPCWCH>(pvData), -1, pszInfo, cchInfo, nullptr, nullptr);
#define DiskInfoStrCvtT2W(pszInfo, cchInfo, pvData) (void)MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<LPCSTR>(pvData), -1, pszInfo, cchInfo);
#define DiskInfoStrCvtA2T(pszInfo, cchInfo, pvData) (void)lstrcpynA(pszInfo, reinterpret_cast<LPCSTR>(pvData), cchInfo);
#define DiskInfoStrCvtT2A(pszInfo, cchInfo, pvData) (void)lstrcpynA(pszInfo, reinterpret_cast<LPCSTR>(pvData), cchInfo);
#else
#define DiskInfoStrCvtT2A(pszInfo, cchInfo, pvData) (void)WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<LPCWCH>(pvData), -1, pszInfo, cchInfo, nullptr, nullptr);
#define DiskInfoStrCvtA2T(pszInfo, cchInfo, pvData) (void)MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<LPCSTR>(pvData), -1, pszInfo, cchInfo);
#define DiskInfoStrCvtT2W(pszInfo, cchInfo, pvData) (void)lstrcpynW(pszInfo, reinterpret_cast<LPCWCH>(pvData), cchInfo);
#define DiskInfoStrCvtW2T(pszInfo, cchInfo, pvData) (void)lstrcpynW(pszInfo, reinterpret_cast<LPCWCH>(pvData), cchInfo);
#endif

//
// IS_PTR
//
static bool IS_PTR(const void *ptr)
{
	return (ptr != nullptr);
}

#if defined(_DEBUG)

//
// OutputDebugFormat
//
static void OutputDebugFormat(LPCTSTR pszFmt, ...)
{
	va_list vl;
	SYSTEMTIME st = { 0 };
	TCHAR rgcFmt[256] = { 0 };
	DWORD cchFmt = ARRAYSIZE(rgcFmt);
	TCHAR rgcInfo[1024] = { 0 };
	DWORD cchInfo = ARRAYSIZE(rgcInfo);
	GetLocalTime(&st);
	wnsprintf(rgcFmt, cchFmt, _T("[EnumDisk] %02hu:%02hu:%02hu %s"), st.wHour, st.wMinute, st.wSecond, pszFmt);
	*(&pszFmt) = rgcFmt;
	va_start(vl, pszFmt);
	wvnsprintf(rgcInfo, cchInfo, pszFmt, vl);
	OutputDebugString(rgcInfo);
	va_end(vl);
}

#endif

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
	BYTE *pbData = reinterpret_cast<BYTE *>(HeapAlloc(hHeap, HEAP_ZERO_MEMORY, cbData));
	if (pbData != nullptr)
	{
		DISK_INFO_BUFFER *pdib = reinterpret_cast<DISK_INFO_BUFFER *>(pbData);
		STORAGE_PROPERTY_QUERY spq;
		spq.PropertyId = StorageDeviceProperty;
		spq.QueryType = PropertyStandardQuery;

		do {
			DWORD dwOffset;
			DWORD cbRead;

			bResult = DeviceIoControl(hFile, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), pbData, cbData, &cbRead, nullptr);
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

			bResult = DeviceIoControl(hFile, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, pbData, cbData, &cbRead, nullptr);
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
static void CALLBACK DiskInfoInitialize(const GUID *lpGuid, LPCTSTR pszEnum = nullptr)
{
	HDEVINFO hDevInfo = SetupDiGetClassDevs(lpGuid, pszEnum, nullptr, DIGCF_PRESENT);
	if (hDevInfo != INVALID_HANDLE_VALUE)
	{
		TCHAR szRoot[] = { _T('\\'), _T('\\'), _T('.'), _T('\\'), _T('G'), _T('l'), _T('o'), _T('b'), _T('a'), _T('l'), _T('R'), _T('o'), _T('o'), _T('t') };
		TCHAR szPath[ARRAYSIZE(szRoot) + _MAX_FNAME];

		DiskInfoData did = { 0 };
		DiskInfoProp aip[] = {
			{ SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, reinterpret_cast<BYTE *>(&szPath[ARRAYSIZE(szRoot)]), sizeof(TCHAR) * _MAX_FNAME },
			{ SPDRP_ENUMERATOR_NAME, reinterpret_cast<BYTE *>(did.szEnumeratorName), sizeof(did.szEnumeratorName) },
			{ SPDRP_FRIENDLYNAME, reinterpret_cast<BYTE *>(did.szFriendlyName), sizeof(did.szFriendlyName) },
		};

		memcpy(szPath, szRoot, sizeof(szRoot));
		RtlInitializeSListHead(&hdr);

		SP_DEVINFO_DATA spdd = { 0 };
		spdd.cbSize = sizeof(spdd);

		WORD wIndex = 0;

		while (SetupDiEnumDeviceInfo(hDevInfo, wIndex++, &spdd))
		{
			for (const DiskInfoProp &ip : aip)
			{
				DWORD dwType;
				DWORD cbRead;
				BOOL bResult = SetupDiGetDeviceRegistryProperty(hDevInfo, &spdd, ip.dwProp, &dwType, ip.pbData, ip.cbData, &cbRead);
				if (!bResult)
				{
					goto CleanUp;
				}
			}

			HANDLE hDevice = CreateFile(szPath, FILE_ANY_ACCESS, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
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

				DiskInfoEntry *pdie = static_cast<DiskInfoEntry *>(pvHeap);
				memcpy(&pdie->di, &did, sizeof(did));
				RtlInterlockedPushEntrySList(&hdr, pdie);
			}
		}

	CleanUp:
		SetupDiDestroyDeviceInfoList(hDevInfo);
	}
}

//
// DiskInfoEntryHandler
//
static void CALLBACK DiskInfoEntryHandler(LPTSTR pszData, int nLength, DiskInfoProc proc, DiskInfoParam *pdip)
{
	SLIST_ENTRY *pent = RtlFirstEntrySList(&hdr);
	for (int nIndex = 0; (pent != nullptr) && (nIndex < pdip->nIndex); nIndex++)
	{
		pent = pent->Next;
	}

	if (IS_PTR(pent))
	{
		proc(pszData, nLength, &static_cast<DiskInfoEntry *>(pent)->di);
	}
}

//
// DiskInfoParamHandler
//
static void CALLBACK DiskInfoParamHandler(stack_t **stacktop, int nLength, DiskInfoProc proc, DiskInfoParam *pdip)
{
	if (*stacktop != nullptr)
	{
		stack_t *th = *stacktop;
		pdip->nIndex = StrToInt(th->text);
		*stacktop = th->next;
		GlobalFree(th);
	}

	stack_t *th = static_cast<stack_t *>(GlobalAlloc(GPTR, sizeof(stack_t) + nLength * sizeof(TCHAR)));
	if (th != nullptr)
	{
		DiskInfoEntryHandler(th->text, nLength, proc, pdip);
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
	if (stacktop != nullptr)
	{
		if (*stacktop != nullptr)
		{
			stack_t *th = *stacktop;
			DiskInfoInitialize(&GUID_DEVCLASS_DISKDRIVE, StrCmpC(th->text, _T("*")) != 0 ? th->text : nullptr);
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
	if (stacktop != nullptr)
	{
		stack_t *th = static_cast<stack_t *>(GlobalAlloc(GPTR, sizeof(stack_t) + nLength * sizeof(TCHAR)));
		if (th != nullptr)
		{
			_ultot_s(RtlQueryDepthSList(&hdr), th->text, nLength, 10);
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
	if (stacktop != nullptr)
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
	if (stacktop != nullptr)
	{
		DiskInfoParam dip = { 0 };
		DiskInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const DiskInfoData *pdi) {
			lstrcpyn(pszData, pdi->szEnumeratorName, nLength);
		}, &dip);
	}
}

//
// GetFriendlyName
//
PLUGINAPI(GetFriendlyName)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		DiskInfoParam dip = { 0 };
		DiskInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const DiskInfoData *pdi) {
			lstrcpyn(pszData, pdi->szFriendlyName, nLength);
		}, &dip);
	}
}

//
// GetSerialNumber
//
PLUGINAPI(GetSerialNumber)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		DiskInfoParam dip = { 0 };
		DiskInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const DiskInfoData *pdi) {
			lstrcpyn(pszData, pdi->szSerialNumber, nLength);
		}, &dip);
	}
}

//
// GetProductId
//
PLUGINAPI(GetProductId)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		DiskInfoParam dip = { 0 };
		DiskInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const DiskInfoData *pdi) {
			lstrcpyn(pszData, pdi->szProductId, nLength);
		}, &dip);
	}
}

//
// GetVendorId
//
PLUGINAPI(GetVendorId)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		DiskInfoParam dip = { 0 };
		DiskInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const DiskInfoData *pdi) {
			lstrcpyn(pszData, pdi->szVendorId, nLength);
		}, &dip);
	}
}

//
// GetDiskSize
//
PLUGINAPI(GetDiskSize)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		DiskInfoParam dip = { 0 };
		DiskInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const DiskInfoData *pdi) {
			_i64tot_s(pdi->llLength, pszData, nLength, 10);
		}, &dip);
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
		DiskInfoInitialize(&GUID_DEVCLASS_DISKDRIVE, StrCmpC(argv[1], _T("*")) != 0 ? argv[1] : nullptr);
	}
	else
	{
		DiskInfoInitialize(&GUID_DEVCLASS_DISKDRIVE);
	}

	for (curr = RtlFirstEntrySList(&hdr); curr != nullptr; curr = next)
	{
		DiskInfoEntry *pdie = static_cast<DiskInfoEntry *>(curr);

		_tprintf(_T("============================================================\n"));
		_tprintf(_T("枚举类型: [%Ts]\n"), pdie->di.szEnumeratorName);
		_tprintf(_T("友好名称: [%Ts]\n"), pdie->di.szFriendlyName);
		_tprintf(_T("产品序列: [%Ts]\n"), pdie->di.szSerialNumber);
		_tprintf(_T("产品标识: [%Ts]\n"), pdie->di.szProductId);
		_tprintf(_T("厂家标识: [%Ts]\n"), pdie->di.szVendorId);
		_i64tot_s(pdie->di.llLength, rgcInfo, cchInfo, 10);
		_tprintf(_T("磁盘大小: [%Ts]\n"), rgcInfo);

		next = curr->Next;

		HeapFree(hHeap, 0, curr);
	}

	return !getchar();
}

#else

//
// DllMain
//
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
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