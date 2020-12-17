#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <tchar.h>

#include <vector>

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
// SetupDiDiskInfo
//
typedef struct {
	TCHAR szEnumerator[126 + 1];
	TCHAR szFriendlyName[126 + 1];
	TCHAR szSerialNumber[126 + 1];
	TCHAR szProductId[126 + 1];
	TCHAR szVendorId[126 + 1];
	INT64 llLength;
} SetupDiDiskInfo;

//
// SetupDiInfoEntry
//
typedef struct : SLIST_ENTRY {
	SetupDiDiskInfo di;
} SetupDiInfoEntry;

//
// DiskInfoProc
//
typedef void (CALLBACK *DiskInfoProc)(LPTSTR, int, const SetupDiDiskInfo *);

//
// ListInfoProc
//
typedef void (CALLBACK *ListInfoProc)(LPTSTR, int, DiskInfoProc, int);

//
// Global variables
//
static SLIST_HEADER hdr;
static HMODULE hModule;

//
// PluginCallback
//
static UINT_PTR PluginCallback(NSPIM msg)
{
	return 0;
}

//
// OutputDebugFormat
//
static void OutputDebugFormat(LPCTSTR pszFmt, ...)
{
	SYSTEMTIME st = { 0 };
	TCHAR rgcFmt[256] = { 0 };
	DWORD cchFmt = ARRAYSIZE(rgcFmt);
	TCHAR rgcInfo[1024] = { 0 };
	DWORD cchInfo = ARRAYSIZE(rgcInfo);
	va_list vl;
	GetLocalTime(&st);
	wnsprintf(rgcFmt, cchFmt, _T("[EnumDisk] %02hu:%02hu:%02hu %s"), st.wHour, st.wMinute, st.wSecond, pszFmt);
	*(&pszFmt) = rgcFmt;
	va_start(vl, pszFmt);
	wvnsprintf(rgcInfo, cchInfo, pszFmt, vl);
	OutputDebugString(rgcInfo);
	va_end(vl);
}

//
// SetupDiQueryStorageProperty
//
static BOOL SetupDiQueryStorageProperty(HANDLE hFile, STORAGE_PROPERTY_ID PropertyId, STORAGE_QUERY_TYPE QueryType, std::vector<BYTE> &rgData)
{
	BOOL bResult;

	STORAGE_PROPERTY_QUERY spq{};
	spq.PropertyId = PropertyId;
	spq.QueryType = QueryType;

	PVOID pvData = static_cast<PVOID>(rgData.data());
	DWORD cbData = static_cast<DWORD>(rgData.size());
	DWORD cbRead;

TryAgain:
	bResult = DeviceIoControl(hFile, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), pvData, cbData, &cbRead, nullptr);
	if (bResult)
	{
		DWORD cbSize = static_cast<STORAGE_DESCRIPTOR_HEADER *>(pvData)->Size;
		if (cbRead < cbSize)
		{
			rgData.resize(cbSize);
			pvData = rgData.data();
			cbData = cbSize;
			goto TryAgain;
		}
	}

	return bResult;
}

//
// SetupDiDeviceIoControl
//
static BOOL SetupDiDeviceIoControl(HANDLE hFile, DWORD dwCode, LPVOID pvInput, DWORD cbInput, std::vector<BYTE> &rgData)
{
	BOOL bResult;

	PVOID pvData = static_cast<PVOID>(rgData.data());
	DWORD cbData = static_cast<DWORD>(rgData.size());
	DWORD cbRead;

TryAgain:
	bResult = DeviceIoControl(hFile, dwCode, pvInput, cbInput, pvData, cbData, &cbRead, nullptr);
	if (!bResult)
	{
		switch (GetLastError()) {
		case ERROR_INSUFFICIENT_BUFFER:
		case ERROR_MORE_DATA:
			rgData.resize(cbRead);
			pvData = rgData.data();
			cbData = cbRead;
			goto TryAgain;
		default:
			break;
		}
	}

	return bResult;
}

//
// SetupDiGetDeviceString
//
static void SetupDiGetDeviceString(LPTSTR pszInfo, int cchInfo, LPCVOID pvData)
{
	LPCSTR pszData = static_cast<LPCSTR>(pvData);
#if defined(UNICODE) || defined(_UNICODE)
	MultiByteToWideChar(CP_ACP, 0, pszData, -1, pszInfo, cchInfo);
#else
	lstrcpynA(pszInfo, pszData, cchInfo);
#endif
}

//
// SetupDiInitDeviceInfo
//
static void SetupDiInitDeviceInfo(const GUID *lpGuid, LPCTSTR pszEnum = nullptr)
{
	HDEVINFO hDevInfo = SetupDiGetClassDevs(lpGuid, pszEnum, nullptr, DIGCF_PRESENT);
	if (hDevInfo != INVALID_HANDLE_VALUE)
	{
		BOOL bResult;
		WORD wIndex = 0;

		RtlInitializeSListHead(&hdr);

		do {
			SP_DEVINFO_DATA spdd{};
			spdd.cbSize = sizeof(spdd);

			bResult = SetupDiEnumDeviceInfo(hDevInfo, wIndex++, &spdd);
			if (!bResult)
			{
				break;
			}

			TCHAR szName[14 + _MAX_FNAME] = { _T('\\'), _T('\\'), _T('.'), _T('\\'), _T('G'), _T('l'), _T('o'), _T('b'), _T('a'), _T('l'), _T('R'), _T('o'), _T('o'), _T('t') };
			DWORD dwType;
			BYTE *pbData = reinterpret_cast<BYTE *>(&szName[14]);
			DWORD cbData = sizeof(TCHAR) * _MAX_FNAME;
			DWORD cbRead;

			bResult = SetupDiGetDeviceRegistryProperty(hDevInfo, &spdd, SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, &dwType, pbData, cbData, &cbRead);
			if (!bResult)
			{
				break;
			}

			HANDLE hDevice = CreateFile(szName, FILE_ANY_ACCESS, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (hDevice != INVALID_HANDLE_VALUE)
			{
				SetupDiDiskInfo di;
				std::vector<BYTE> rgData(1024);
				STORAGE_DEVICE_DESCRIPTOR *sdd;
				SetupDiInfoEntry *pdie;
				DISK_GEOMETRY_EX *pdgx;

				do {
					bResult = SetupDiGetDeviceRegistryProperty(hDevInfo, &spdd, SPDRP_ENUMERATOR_NAME, nullptr, reinterpret_cast<BYTE *>(di.szEnumerator), sizeof(di.szEnumerator), nullptr);
					if (!bResult)
					{
						break;
					}

					bResult = SetupDiGetDeviceRegistryProperty(hDevInfo, &spdd, SPDRP_FRIENDLYNAME, nullptr, reinterpret_cast<BYTE *>(di.szFriendlyName), sizeof(di.szFriendlyName), nullptr);
					if (!bResult)
					{
						break;
					}	

					bResult = SetupDiQueryStorageProperty(hDevice, StorageDeviceProperty, PropertyStandardQuery, rgData);
					if (!bResult)
					{
						break;
					}

					sdd = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR *>(pbData = rgData.data());

					if (sdd->SerialNumberOffset > 0)
					{
						SetupDiGetDeviceString(di.szSerialNumber, ARRAYSIZE(di.szSerialNumber), &pbData[sdd->SerialNumberOffset]);
					}
					else
					{
						memset(di.szSerialNumber, 0, sizeof(di.szSerialNumber));
					}

					if (sdd->ProductIdOffset > 0)
					{
						SetupDiGetDeviceString(di.szProductId, ARRAYSIZE(di.szProductId), &pbData[sdd->ProductIdOffset]);
					}
					else
					{
						memset(di.szProductId, 0, sizeof(di.szProductId));
					}

					if (sdd->VendorIdOffset > 0)
					{
						SetupDiGetDeviceString(di.szVendorId, ARRAYSIZE(di.szVendorId), &pbData[sdd->VendorIdOffset]);
					}
					else
					{
						memset(di.szVendorId, 0, sizeof(di.szVendorId));
					}

					bResult = SetupDiDeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, rgData);
					if (!bResult)
					{
						break;
					}

					pdie = static_cast<SetupDiInfoEntry *>(LocalAlloc(LPTR, sizeof(SetupDiInfoEntry)));
					if (pdie != nullptr)
					{
						pdgx = reinterpret_cast<DISK_GEOMETRY_EX *>(rgData.data());
						memcpy(&pdie->di, &di, FIELD_OFFSET(SetupDiDiskInfo, llLength));
						pdie->di.llLength = pdgx->DiskSize.QuadPart;
						RtlInterlockedPushEntrySList(&hdr, pdie);
					}

				} while (0);

				CloseHandle(hDevice);
			}

		} while (1);

		SetupDiDestroyDeviceInfoList(hDevInfo);
	}
}

//
// SetupDiGetDeviceInfo
//
static void SetupDiGetDeviceInfo(stack_t **stacktop, int nLength, ListInfoProc pfn, DiskInfoProc proc)
{
	LONG32 nIndex = -1;

	if (*stacktop != nullptr)
	{
		stack_t *th = *stacktop;
		nIndex = StrToInt(th->text);
		*stacktop = th->next;
		GlobalFree(th);
	}

	stack_t *th = static_cast<stack_t *>(GlobalAlloc(GPTR, sizeof(stack_t) + nLength * sizeof(TCHAR)));
	if (th != nullptr)
	{
		pfn(th->text, nLength, proc, nIndex);
		th->next = *stacktop;
		*stacktop = th;
	}
}

//
// SetupDiGetDeviceInfo
//
static void SetupDiGetDeviceInfo(stack_t **stacktop, int nLength, DiskInfoProc proc)
{
	SetupDiGetDeviceInfo(stacktop, nLength, [](LPTSTR pszData, int nLength, DiskInfoProc proc, int nIndex) {
		SLIST_ENTRY *pent = RtlFirstEntrySList(&hdr);
		while ((nIndex > 0) && (pent != nullptr))
		{
			pent = pent->Next;
			nIndex--;
		}
		if (pent != nullptr)
		{
			SetupDiInfoEntry *pdie = static_cast<SetupDiInfoEntry *>(pent);
			proc(pszData, nLength, &pdie->di);
		}
	}, proc);
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
			SetupDiInitDeviceInfo(&GUID_DEVCLASS_DISKDRIVE, *th->text ? th->text : nullptr);
			*stacktop = th->next;
			GlobalFree(th);
		}
		else
		{
			SetupDiInitDeviceInfo(&GUID_DEVCLASS_DISKDRIVE);
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
// GetFriendlyName
//
PLUGINAPI(GetFriendlyName)
{
	extra->RegisterPluginCallback(hModule, PluginCallback);
	if (stacktop != nullptr)
	{
		SetupDiGetDeviceInfo(stacktop, nLength, [](LPTSTR pszData, int nLength, const SetupDiDiskInfo *pdi) {
			lstrcpyn(pszData, pdi->szFriendlyName, nLength);
		});
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
		SetupDiGetDeviceInfo(stacktop, nLength, [](LPTSTR pszData, int nLength, const SetupDiDiskInfo *pdi) {
			lstrcpyn(pszData, pdi->szEnumerator, nLength);
		});
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
		SetupDiGetDeviceInfo(stacktop, nLength, [](LPTSTR pszData, int nLength, const SetupDiDiskInfo *pdi) {
			lstrcpyn(pszData, pdi->szSerialNumber, nLength);
		});
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
		SetupDiGetDeviceInfo(stacktop, nLength, [](LPTSTR pszData, int nLength, const SetupDiDiskInfo *pdi) {
			lstrcpyn(pszData, pdi->szProductId, nLength);
		});
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
		SetupDiGetDeviceInfo(stacktop, nLength, [](LPTSTR pszData, int nLength, const SetupDiDiskInfo *pdi) {
			lstrcpyn(pszData, pdi->szVendorId, nLength);
		});
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
		SetupDiGetDeviceInfo(stacktop, nLength, [](LPTSTR pszData, int nLength, const SetupDiDiskInfo *pdi) {
			_i64tot_s(pdi->llLength, pszData, nLength, 10);
		});
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
		while ((pent = RtlInterlockedPopEntrySList(&hdr)) != nullptr)
		{
			RtlInterlockedFlushSList(&hdr);
			LocalFree(pent);
		}
	}
}

#if _DEBUG

int _tmain( int argc, _TCHAR *argv[] )
{
	TCHAR rgcInfo[64] = { 0 };
	DWORD cchInfo = ARRAYSIZE(rgcInfo);

	SLIST_ENTRY *curr;
	SLIST_ENTRY *next;

	_tsetlocale(LC_CTYPE, _T(".ACP"));

	if (argc > 1)
	{
		SetupDiInitDeviceInfo(&GUID_DEVCLASS_DISKDRIVE, *argv[1] ? argv[1] : nullptr);
	}
	else
	{
		SetupDiInitDeviceInfo(&GUID_DEVCLASS_DISKDRIVE);
	}

	for (curr = RtlFirstEntrySList(&hdr); curr != nullptr; curr = next)
	{
		SetupDiInfoEntry *pdie = static_cast<SetupDiInfoEntry *>(curr);

		_tprintf(_T("============================================================\n"));
		_tprintf(_T("友好名称: [%Ts]\n"), pdie->di.szFriendlyName);
		_tprintf(_T("产品序列: [%Ts]\n"), pdie->di.szSerialNumber);
		_tprintf(_T("产品标识: [%Ts]\n"), pdie->di.szProductId);
		_tprintf(_T("厂家标识: [%Ts]\n"), pdie->di.szVendorId);
		_i64tot_s(pdie->di.llLength, rgcInfo, cchInfo, 10);
		_tprintf(_T("磁盘大小: [%Ts]\n"), rgcInfo);

		next = curr->Next;
		LocalFree(curr);
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
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		return TRUE;
	default:
		return TRUE;
	}
}

#endif