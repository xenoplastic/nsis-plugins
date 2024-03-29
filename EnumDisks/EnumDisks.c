#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <tchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <shlwapi.h>
#pragma comment( lib, "shlwapi" )
#include <setupapi.h>
#pragma comment( lib, "setupapi" )
#include <devguid.h>
#pragma comment( lib, "ntdll" )

#include "plugin.h"
#include "strcvt.h"

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
	STORAGE_DISK_HEALTH_STATUS HealthStatus;
	BOOLEAN IncursSeekPenalty;
	BOOLEAN TrimEnabled;
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
	DiskInfoEnumeratorName,
	DiskInfoFriendlyName,
	DiskInfoSerialNumber,
	DiskInfoProductID,
	DiskInfoVendorID,
	DiskInfoHealthStatus,
	DiskInfoSeekPenalty,
	DiskInfoTrimEnabled,
	DiskInfoSize,
} DiskInfoType;

//
// DiskInfoParam
//
typedef struct _DiskInfoParam {
	int nIndex;
	int nType;
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
// IS_PTR
//
static BOOL IS_PTR(const void *ptr)
{
	return (ptr != NULL);
}

//
// DiskInfoQueryDeviceProperty
//
static BOOL CALLBACK DiskInfoQueryDeviceProperty(HANDLE hFile, DiskInfoData *pdi)
{
	BOOL bResult;

	ULONG code = IOCTL_STORAGE_QUERY_PROPERTY;
	STORAGE_PROPERTY_QUERY spq;
	STORAGE_DESCRIPTOR_HEADER hdr;
	DWORD cb;

	spq.QueryType = PropertyStandardQuery;
	spq.PropertyId = StorageDeviceProperty;

	bResult = DeviceIoControl(hFile, code, &spq, sizeof(spq), &hdr, sizeof(hdr), &cb, NULL);
	if (bResult)
	{
		BYTE *pb = (BYTE *)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, hdr.Size);
		if (pb != NULL)
		{
			bResult = DeviceIoControl(hFile, code, &spq, sizeof(spq), pb, hdr.Size, &cb, NULL);
			if (bResult)
			{
				STORAGE_DEVICE_DESCRIPTOR *psdd = (STORAGE_DEVICE_DESCRIPTOR *)pb;
				DWORD pos;
				if ((pos = psdd->SerialNumberOffset) > 0UL)
				{
					StrCvtA2T(pdi->szSerialNumber, ARRAYSIZE(pdi->szSerialNumber), &pb[pos]);
				}
				if ((pos = psdd->ProductIdOffset) > 0UL)
				{
					StrCvtA2T(pdi->szProductId, ARRAYSIZE(pdi->szProductId), &pb[pos]);
				}
				if ((pos = psdd->VendorIdOffset) > 0UL)
				{
					StrCvtA2T(pdi->szVendorId, ARRAYSIZE(pdi->szVendorId), &pb[pos]);
				}
			}
			HeapFree(hHeap, 0, pb);
		}
		else
		{
			bResult = FALSE;
		}
	}

	return bResult;
}

//
// DiskInfoQuerySeekPenalty
//
static void CALLBACK DiskInfoQuerySeekPenalty(HANDLE hFile, DiskInfoData *pdi)
{
	BOOL bResult;

	ULONG code = IOCTL_STORAGE_QUERY_PROPERTY;
	STORAGE_PROPERTY_QUERY spq;
	DEVICE_SEEK_PENALTY_DESCRIPTOR dspd;
	DWORD cb;

	spq.QueryType = PropertyStandardQuery;
	spq.PropertyId = StorageDeviceSeekPenaltyProperty;

	bResult = DeviceIoControl(hFile, code, &spq, sizeof(spq), &dspd, sizeof(dspd), &cb, NULL);
	if (bResult)
	{
		pdi->IncursSeekPenalty = dspd.IncursSeekPenalty;
	}
}

//
// DiskInfoQueryTrimEnabled
//
static void CALLBACK DiskInfoQueryTrimEnabled(HANDLE hFile, DiskInfoData *pdi)
{
	BOOL bResult;

	ULONG code = IOCTL_STORAGE_QUERY_PROPERTY;
	STORAGE_PROPERTY_QUERY spq;
	DEVICE_TRIM_DESCRIPTOR dtd;
	DWORD cb;

	spq.QueryType = PropertyStandardQuery;
	spq.PropertyId = StorageDeviceTrimProperty;

	bResult = DeviceIoControl(hFile, code, &spq, sizeof(spq), &dtd, sizeof(dtd), &cb, NULL);
	if (bResult)
	{
		pdi->TrimEnabled = dtd.TrimEnabled;
	}
}

//
// DiskInfoQueryHealthStatus
//
static void CALLBACK DiskInfoQueryHealthStatus(HANDLE hFile, DiskInfoData *pdi)
{
	BOOL bResult;

	ULONG code = IOCTL_STORAGE_QUERY_PROPERTY;
	STORAGE_PROPERTY_QUERY spq;
	STORAGE_DEVICE_MANAGEMENT_STATUS sdms;
	DWORD cb;

	spq.QueryType = PropertyStandardQuery;
	spq.PropertyId = StorageDeviceManagementStatus;

	bResult = DeviceIoControl(hFile, code, &spq, sizeof(spq), &sdms, sizeof(sdms), &cb, NULL);
	if (bResult)
	{
		pdi->HealthStatus = sdms.Health;
	}
}

//
// DiskInfoQueryGeometryEx
//
static void CALLBACK DiskInfoQueryGeometryEx(HANDLE hFile, DiskInfoData *pdi)
{
	BOOL bResult;

	DWORD code = IOCTL_DISK_GET_DRIVE_GEOMETRY_EX;
	BYTE dgx[FIELD_OFFSET(DISK_GEOMETRY_EX, Data) + sizeof(DISK_PARTITION_INFO) + sizeof(DISK_DETECTION_INFO)];
	DWORD cb;

	bResult = DeviceIoControl(hFile, code, NULL, 0, &dgx, sizeof(dgx), &cb, NULL);
	if (bResult)
	{
		DISK_GEOMETRY_EX *pdgx = (DISK_GEOMETRY_EX *)dgx;
		pdi->llLength = pdgx->DiskSize.QuadPart;
	}
}

//
// DiskInfoQueryProperty
//
static BOOL CALLBACK DiskInfoQueryProperty(HANDLE hFile, DiskInfoData *pdi)
{
	BOOL bResult;

	bResult = DiskInfoQueryDeviceProperty(hFile, pdi);
	if (bResult)
	{
		DiskInfoQuerySeekPenalty(hFile, pdi);
		DiskInfoQueryTrimEnabled(hFile, pdi);
		DiskInfoQueryHealthStatus(hFile, pdi);
		DiskInfoQueryGeometryEx(hFile, pdi);
	}

	return bResult;
}

//
// DiskInfoInitialize
//
static const TCHAR achGlobalRoot[] = { _T('\\'), _T('\\'), _T('.'), _T('\\'), _T('G'), _T('l'), _T('o'), _T('b'), _T('a'), _T('l'), _T('R'), _T('o'), _T('o'), _T('t') };

static void CALLBACK DiskInfoInitialize(const GUID *lpGuid, LPCTSTR pszEnum)
{
	HDEVINFO hDevInfo = SetupDiGetClassDevs(lpGuid, pszEnum, NULL, DIGCF_PRESENT);
	if (hDevInfo != INVALID_HANDLE_VALUE)
	{
		TCHAR szDevicePath[ARRAYSIZE(achGlobalRoot) + _MAX_FNAME];

		DiskInfoData did = { 0 };
		DiskInfoProp aip[] = {
			{ SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, (BYTE *)&szDevicePath[ARRAYSIZE(achGlobalRoot)], sizeof(TCHAR) * _MAX_FNAME },
			{ SPDRP_ENUMERATOR_NAME, (BYTE *)did.szEnumeratorName, sizeof(did.szEnumeratorName) },
			{ SPDRP_FRIENDLYNAME, (BYTE *)did.szFriendlyName, sizeof(did.szFriendlyName) },
		};

		SP_DEVINFO_DATA spdd = { 0 };
		WORD wIndex = 0;

		memcpy(szDevicePath, achGlobalRoot, sizeof(achGlobalRoot));
		RtlInitializeSListHead(&hdr);

		spdd.cbSize = sizeof(spdd);

		while (SetupDiEnumDeviceInfo(hDevInfo, wIndex++, &spdd))
		{
			for (size_t i = 0; i < ARRAYSIZE(aip); i++)
			{
				DiskInfoProp *pdip = (DiskInfoProp *)&aip[i];
				DWORD dwType;
				DWORD cbRead;
				BOOL bResult = SetupDiGetDeviceRegistryProperty(hDevInfo, &spdd, pdip->dwProp, &dwType, pdip->pbData, pdip->cbData, &cbRead);
				if (!bResult)
				{
					goto CleanUp;
				}
			}

			HANDLE hDevice = CreateFile(szDevicePath, FILE_ANY_ACCESS, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
			if (hDevice != INVALID_HANDLE_VALUE)
			{
				BOOL bResult = DiskInfoQueryProperty(hDevice, &did);
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
	case DiskInfoEnumeratorName:
		lstrcpyn(pszData, pdi->szEnumeratorName, nLength);
		break;
	case DiskInfoFriendlyName:
		lstrcpyn(pszData, pdi->szFriendlyName, nLength);
		break;
	case DiskInfoSerialNumber:
		lstrcpyn(pszData, pdi->szSerialNumber, nLength);
		break;
	case DiskInfoProductID:
		lstrcpyn(pszData, pdi->szProductId, nLength);
		break;
	case DiskInfoVendorID:
		lstrcpyn(pszData, pdi->szVendorId, nLength);
		break;
	case DiskInfoHealthStatus:
		wnsprintf(pszData, nLength, _T("%d"), pdi->HealthStatus);
		break;
	case DiskInfoSeekPenalty:
		wnsprintf(pszData, nLength, _T("%u"), pdi->IncursSeekPenalty);
		break;
	case DiskInfoTrimEnabled:
		wnsprintf(pszData, nLength, _T("%u"), pdi->TrimEnabled);
		break;
	case DiskInfoSize:
		wnsprintf(pszData, nLength, _T("%I64d"), pdi->llLength);
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
			wnsprintf(th->text, nLength, _T("%hu"), RtlQueryDepthSList(&hdr));
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
		dip.nType = DiskInfoEnumeratorName;
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
		dip.nType = DiskInfoFriendlyName;
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
		dip.nType = DiskInfoSerialNumber;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetProductID
//
PLUGINAPI(GetProductID)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DiskInfoProductID;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetVendorID
//
PLUGINAPI(GetVendorID)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DiskInfoVendorID;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetHealthStatus
//
PLUGINAPI(GetHealthStatus)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DiskInfoHealthStatus;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetSeekPenalty
//
PLUGINAPI(GetSeekPenalty)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DiskInfoSeekPenalty;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

//
// GetTrimEnabled
//
PLUGINAPI(GetTrimEnabled)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != NULL)
	{
		DiskInfoParam dip = { 0 };
		dip.nType = DiskInfoTrimEnabled;
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
		dip.nType = DiskInfoSize;
		DiskInfoParamHandler(stacktop, nLength, &dip);
	}
}

#if defined(_DEBUG)

static const TCHAR *aHealth[] = {
	_T("Unknown"),
	_T("Unhealthy"),
	_T("Warning"),
	_T("Healthy")
};

int _tmain( int argc, _TCHAR *argv[] )
{
	SLIST_ENTRY *curr;
	SLIST_ENTRY *next;

	_tsetlocale(LC_CTYPE, _T(".ACP"));

	hHeap = GetProcessHeap();

	if (argc > 1)
	{
		DiskInfoInitialize(&GUID_DEVCLASS_DISKDRIVE, StrCmpC(argv[1], _T("*")) != 0 ? argv[1] : NULL);
	}
	else
	{
		DiskInfoInitialize(&GUID_DEVCLASS_DISKDRIVE, NULL);
	}

	for (curr = RtlFirstEntrySList(&hdr); curr != NULL; curr = next)
	{
		DiskInfoEntry *pdie = (DiskInfoEntry *)curr;

		_tprintf(_T("============================================================\n"));
		_tprintf(_T("枚举类型: [%Ts]\n"), pdie->di.szEnumeratorName);
		_tprintf(_T("友好名称: [%Ts]\n"), pdie->di.szFriendlyName);
		_tprintf(_T("产品序列: [%Ts]\n"), pdie->di.szSerialNumber);
		_tprintf(_T("产品标识: [%Ts]\n"), pdie->di.szProductId);
		_tprintf(_T("厂家标识: [%Ts]\n"), pdie->di.szVendorId);
		_tprintf(_T("健康状态: [%Ts]\n"), aHealth[pdie->di.HealthStatus]);
		_tprintf(_T("查找开销: [%u]\n"), pdie->di.IncursSeekPenalty);
		_tprintf(_T("优化标记: [%u]\n"), pdie->di.TrimEnabled);
		_tprintf(_T("磁盘大小: [%I64d]\n"), pdie->di.llLength);

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