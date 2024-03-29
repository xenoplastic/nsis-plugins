#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <tchar.h>

#include <winsock2.h>
#include <ws2ipdef.h>
#pragma comment( lib, "ws2_32" )
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <shlwapi.h>
#pragma comment( lib, "shlwapi" )
#include <iphlpapi.h>
#pragma comment( lib, "iphlpapi" )
#include <cfgmgr32.h>
#include <setupapi.h>
#pragma comment( lib, "setupapi" )
#include <ntddndis.h>
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
// AdapterInfoPath
//
typedef struct _AdapterInfoPath {
	TCHAR GlobalRoot[4];
} AdapterInfoPath;

//
// AdapterInfoData
//
typedef struct _AdapterInfoData {
	TCHAR AdapterName[MAX_GUID_STRING_LEN];
	TCHAR FriendlyName[IF_MAX_STRING_SIZE + 1];
	TCHAR Description[IF_MAX_STRING_SIZE + 1];
	TCHAR Enumerator[IF_MAX_STRING_SIZE + 1];
	ULONG PhysicalAddressLength;
	UCHAR PhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
	NDIS_PHYSICAL_MEDIUM PhysicalMediumType;
	TCHAR Ipv4Address[16];
} AdapterInfoData;

//
// AdapterInfoFull
//
typedef struct _AdapterInfoFull : AdapterInfoPath {
	AdapterInfoData aid;
} AdapterInfoFull;

//
// AdapterInfoProc
//
typedef void (CALLBACK *AdapterInfoProc)(LPTSTR, int, const AdapterInfoData *);

//
// AdapterInfoEntry
//
typedef struct : SLIST_ENTRY {
	AdapterInfoData aid;
} AdapterInfoEntry;

//
// AdapterInfoProp
//
typedef struct _AdapterInfoProp {
	DWORD dwProp;
	BYTE *pbData;
	DWORD cbData;
} AdapterInfoProp;

//
// AdapterInfoParam
//
typedef struct _AdapterInfoParam {
	int nIndex;
} AdapterInfoParam;

//
// ListInfoProc
//
typedef void (CALLBACK *ListInfoProc)(LPTSTR, int, AdapterInfoParam *);

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
// MEMBER_ARRAYSIZE
//
#define MEMBER_ARRAYSIZE(Type, Name) ARRAYSIZE(static_cast<Type *>(nullptr)->Name)

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
	wnsprintf(rgcFmt, cchFmt, _T("[EnumAdapters] %02hu:%02hu:%02hu %s"), st.wHour, st.wMinute, st.wSecond, pszFmt);
	va_start(vl, pszFmt);
	wvnsprintf(rgcInfo, cchInfo, rgcFmt, vl);
	OutputDebugString(rgcInfo);
	va_end(vl);
}

#endif

//
// NdisQueryGlobalStats
//
static BOOL WINAPI NdisQueryGlobalStats(HANDLE hFile, DWORD dwCode, LPVOID pvData, DWORD cbData, DWORD *pcbRead)
{
	return DeviceIoControl(
		hFile,
		IOCTL_NDIS_QUERY_GLOBAL_STATS,
		&dwCode,
		sizeof(dwCode),
		pvData,
		cbData,
		pcbRead,
		nullptr);
}

//
// NdisQueryProperty
//
static BOOL WINAPI NdisQueryProperty(HANDLE hFile, AdapterInfoData *paid)
{
	BOOL bResult;
	DWORD cbRead;

	do {
		bResult = NdisQueryGlobalStats(
			hFile,
			OID_GEN_PHYSICAL_MEDIUM_EX,
			&paid->PhysicalMediumType,
			sizeof(paid->PhysicalMediumType),
			&cbRead);
		if (!bResult)
		{
			bResult = NdisQueryGlobalStats(
				hFile,
				OID_GEN_PHYSICAL_MEDIUM,
				&paid->PhysicalMediumType,
				sizeof(paid->PhysicalMediumType),
				&cbRead);
			if (!bResult)
			{
				paid->PhysicalMediumType = NdisPhysicalMediumUnspecified;
			}
		}

		bResult = NdisQueryGlobalStats(
			hFile,
			OID_802_3_PERMANENT_ADDRESS,
			paid->PhysicalAddress,
			sizeof(paid->PhysicalAddress),
			&paid->PhysicalAddressLength);
		if (!bResult)
		{
			bResult = NdisQueryGlobalStats(
				hFile,
				OID_802_3_CURRENT_ADDRESS,
				paid->PhysicalAddress,
				sizeof(paid->PhysicalAddress),
				&paid->PhysicalAddressLength);
			if (!bResult)
			{
				break;
			}
		}

	} while (0);

	return bResult;
}

//
// AdapterInfoInitialize
//
static void WINAPI AdapterInfoInitialize(const GUID *lpGuid, LPCTSTR pszEnum = nullptr)
{
	HDEVINFO hDevInfo = SetupDiGetClassDevs(lpGuid, pszEnum, nullptr, DIGCF_PRESENT);
	if (hDevInfo != INVALID_HANDLE_VALUE)
	{
		AdapterInfoFull aif{};
		AdapterInfoProp aip[] = {
			{ SPDRP_ENUMERATOR_NAME, reinterpret_cast<BYTE *>(aif.aid.Enumerator), sizeof(aif.aid.Enumerator) },
			{ SPDRP_DEVICEDESC, reinterpret_cast<BYTE *>(aif.aid.Description), sizeof(aif.aid.Description) },
		};

		SP_DEVINFO_DATA spdd = { 0 };
		WORD wIndex = 0;

		aif.GlobalRoot[0] = _T('\\');
		aif.GlobalRoot[1] = _T('\\');
		aif.GlobalRoot[2] = _T('.');
		aif.GlobalRoot[3] = _T('\\');
		RtlInitializeSListHead(&hdr);

		spdd.cbSize = sizeof(spdd);

		while (SetupDiEnumDeviceInfo(hDevInfo, wIndex++, &spdd))
		{
			HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &spdd, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
			if (hKey != INVALID_HANDLE_VALUE)
			{
				DWORD dwType;
				BYTE *pbData = reinterpret_cast<BYTE *>(aif.aid.AdapterName);
				DWORD cbData = sizeof(aif.aid.AdapterName);
				DWORD dwError = RegQueryValueEx(hKey, _T("NetCfgInstanceId"), nullptr, &dwType, pbData, &cbData);
				RegCloseKey(hKey);
				if (dwError != ERROR_SUCCESS)
				{
					goto CleanUp;
				}
			}

			for (const AdapterInfoProp &ip : aip)
			{
				DWORD dwType;
				DWORD cbRead;
				BOOL bResult = SetupDiGetDeviceRegistryProperty(hDevInfo, &spdd, ip.dwProp, &dwType, ip.pbData, ip.cbData, &cbRead);
				if (!bResult)
				{
					goto CleanUp;
				}
			}

			HANDLE hDevice = CreateFile(aif.GlobalRoot, STANDARD_RIGHTS_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hDevice != INVALID_HANDLE_VALUE)
			{
				BOOL bResult = NdisQueryProperty(hDevice, &aif.aid);
				CloseHandle(hDevice);
				if (!bResult)
				{
					continue;
				}

				PVOID pvHeap = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(AdapterInfoEntry));
				if (!IS_PTR(pvHeap))
				{
					continue;
				}

				AdapterInfoEntry *pdie = static_cast<AdapterInfoEntry *>(pvHeap);
				memcpy(&pdie->aid, &aif.aid, sizeof(aif.aid));

				RtlInterlockedPushEntrySList(&hdr, pdie);
			}
		}

		BOOL bResult = FALSE;

		DWORD cbData = 16UL << 10;
		PVOID pvData = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, cbData);

		PVOID pvHeap;
		IP_ADAPTER_ADDRESSES *pInfo;
		DWORD dwError;

	TryAgain:
		pInfo = static_cast<IP_ADAPTER_ADDRESSES *>(pvData);
		dwError = GetAdaptersAddresses(AF_INET, 0, nullptr, pInfo, &cbData);
		switch (dwError) {
		case ERROR_SUCCESS:
			bResult = TRUE;
			break;
		case ERROR_BUFFER_OVERFLOW:
			pvHeap = HeapReAlloc(hHeap, HEAP_ZERO_MEMORY, pvData, cbData);
			if (IS_PTR(pvHeap))
			{
				pvData = pvHeap;
				goto TryAgain;
			}
		default:
			break;
		}

		if (bResult)
		{
			for (; IS_PTR(pInfo); pInfo = pInfo->Next)
			{
				SOCKADDR *psa = pInfo->FirstUnicastAddress->Address.lpSockaddr;
				for (SLIST_ENTRY *pent = RtlFirstEntrySList(&hdr); pent != nullptr; pent = pent->Next)
				{
					TCHAR szAdapterName[MEMBER_ARRAYSIZE(AdapterInfoData, AdapterName)] = { 0 };
					AdapterInfoEntry *paie = static_cast<AdapterInfoEntry *>(pent);
					StrCvtA2T(szAdapterName, ARRAYSIZE(szAdapterName), pInfo->AdapterName);
					if ((psa->sa_family == AF_INET) && (lstrcmpi(szAdapterName, paie->aid.AdapterName) == 0))
					{
						StrCvtA2T(paie->aid.Ipv4Address, ARRAYSIZE(paie->aid.Ipv4Address), inet_ntoa(reinterpret_cast<SOCKADDR_IN *>(psa)->sin_addr));
						StrCvtW2T(paie->aid.FriendlyName, ARRAYSIZE(paie->aid.FriendlyName), pInfo->FriendlyName);
						break;
					}
				}
			}
		}

		HeapFree(hHeap, 0, pvData);

	CleanUp:
		SetupDiDestroyDeviceInfoList(hDevInfo);
	}
}

//
// AdapterInfoEntryHandler
//
static void WINAPI AdapterInfoEntryHandler(LPTSTR pszData, int nLength, AdapterInfoProc proc, AdapterInfoParam *pdip)
{
	SLIST_ENTRY *pent = RtlFirstEntrySList(&hdr);
	for (int nIndex = 0; (pent != nullptr) && (nIndex < pdip->nIndex); nIndex++)
	{
		pent = pent->Next;
	}

	if (IS_PTR(pent))
	{
		(*proc)(pszData, nLength, &static_cast<AdapterInfoEntry *>(pent)->aid);
	}
}

//
// AdapterInfoParamHandler
//
static void WINAPI AdapterInfoParamHandler(stack_t **stacktop, int nLength, AdapterInfoProc proc, AdapterInfoParam *pdip)
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
		AdapterInfoEntryHandler(th->text, nLength, proc, pdip);
		th->next = *stacktop;
		*stacktop = th;
	}
}

//
// FormatAdapterAddress
//
static const TCHAR szFmt[] = _T("%02X");
static const TCHAR chSep = _T('-');

static void WINAPI FormatAdapterAddress(LPTSTR pszData, int cchData, const BYTE *pbAddr, ULONG cbAddr)
{
	LPTSTR pszInfo = pszData;
	ULONG nIndex = 0;
	int nLength;

	switch (cbAddr) {
	case 0:
		while (nIndex < cbAddr && cchData > 0)
		{
			memcpy(pszData, &chSep, sizeof(chSep));
			pszData++;
			cchData--;
	default:
			nLength = wnsprintf(pszData, cchData, szFmt, pbAddr[nIndex++]);
			pszData += nLength;
			cchData -= nLength;
		}
	}

	if (cchData < 0)
	{
		memset(pszInfo, 0, cchData * sizeof(TCHAR));
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
			AdapterInfoInitialize(&GUID_DEVCLASS_NET, StrCmpC(th->text, _T("*")) != 0 ? th->text : nullptr);
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
// GetFriendlyName
//
PLUGINAPI(GetFriendlyName)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		AdapterInfoParam dip = { 0 };
		AdapterInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const AdapterInfoData *pdi) {
			lstrcpyn(pszData, pdi->FriendlyName, nLength);
		}, &dip);
	}
}

//
// GetDescription
//
PLUGINAPI(GetDescription)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		AdapterInfoParam dip = { 0 };
		AdapterInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const AdapterInfoData *pdi) {
			lstrcpyn(pszData, pdi->Description, nLength);
		}, &dip);
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
		AdapterInfoParam dip = { 0 };
		AdapterInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const AdapterInfoData *pdi) {
			lstrcpyn(pszData, pdi->Enumerator, nLength);
		}, &dip);
	}
}

//
// GetMacAddress
//
PLUGINAPI(GetMacAddress)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		AdapterInfoParam dip = { 0 };
		AdapterInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const AdapterInfoData *pdi) {
			FormatAdapterAddress(pszData, nLength, pdi->PhysicalAddress, pdi->PhysicalAddressLength);
		}, &dip);
	}
}

//
// GetMediumType
//
PLUGINAPI(GetMediumType)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		AdapterInfoParam dip = { 0 };
		AdapterInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const AdapterInfoData *pdi) {
			wnsprintf(pszData, nLength, _T("%d"), pdi->PhysicalMediumType);
		}, &dip);
	}
}

//
// GetIpAddress
//
PLUGINAPI(GetIpAddress)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		AdapterInfoParam dip = { 0 };
		AdapterInfoParamHandler(stacktop, nLength, [](LPTSTR pszData, int nLength, const AdapterInfoData *pdi) {
			lstrcpyn(pszData, pdi->Ipv4Address, nLength);
		}, &dip);
	}
}

#if defined(_DEBUG)

void printfString(LPCTSTR pszData)
{
	_tprintf(pszData);
}

int _tmain(int argc, _TCHAR *argv[])
{
	TCHAR rgcInfo[64] = { 0 };
	DWORD cchInfo = ARRAYSIZE(rgcInfo);
	HANDLE hHeap = GetProcessHeap();

	_tsetlocale(LC_CTYPE, _T(".ACP"));

	if (argc > 1)
	{
		AdapterInfoInitialize(&GUID_DEVCLASS_NET, StrCmpC(argv[1], _T("*")) != 0 ? argv[1] : nullptr);
	}
	else
	{
		AdapterInfoInitialize(&GUID_DEVCLASS_NET);
	}

	SLIST_ENTRY *pent = RtlFirstEntrySList(&hdr);
	while (pent != nullptr)
	{
		AdapterInfoEntry *pdie = static_cast<AdapterInfoEntry *>(pent);

		printf("============================================================\n");
		printf("设备名称: [");
		printfString(pdie->aid.AdapterName);
		printf("]\n");
		printf("友好名称: [");
		printfString(pdie->aid.FriendlyName);
		printf("]\n");
		printf("设备描述: [");
		printfString(pdie->aid.Description);
		printf("]\n");
		printf("枚举类型: [");
		printfString(pdie->aid.Enumerator);
		printf("]\n");
		printf("IPv4地址: [%hs]\n", pdie->aid.Ipv4Address);

		pent = pdie->Next;

		HeapFree(hHeap, 0, pdie);
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