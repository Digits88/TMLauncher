#include <Windows.h>
#include <stdio.h>
#include <assert.h>
#include <tlhelp32.h>
#include <conio.h>
#include "common.h"
#include <wtsapi32.h>
#include <userenv.h>
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Userenv.lib")

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
HANDLE                g_ServiceLaunchEvent = INVALID_HANDLE_VALUE;

HANDLE                g_ServiceSessionStartEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);

DWORD WINAPI ServiceWorkerThread(
	LPVOID lpParam
)
{
	HANDLE handles[2];
	handles[0] = g_ServiceStopEvent;
	handles[1] = g_ServiceLaunchEvent;
	while (TRUE)
	{
		DWORD dwRet = WaitForMultipleObjects(
			2,
			handles,
			FALSE,
			INFINITE
		);
		if (dwRet == WAIT_OBJECT_0 + 0)
		{
			return 0;
		}
		else if (dwRet == WAIT_OBJECT_0 + 1)
		{
			HKEY hKey;
			DWORD dw;
			if (RegCreateKeyExA(
				HKEY_LOCAL_MACHINE,
				"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\taskmgr.exe",
				0, 
				REG_NONE, 
				REG_OPTION_NON_VOLATILE,
				KEY_READ | KEY_WRITE, 
				NULL, 
				&hKey, 
				&dw
			) != ERROR_SUCCESS)
			{
				return GetLastError();
			}
			if (RegSetValueExA(
				hKey,
				"Debugger",
				0,
				REG_SZ,
				"",
				0
			))
			{
				return GetLastError();
			}

			TCHAR szPath[_MAX_PATH + 1], szPath2[_MAX_PATH + 1];
			if (!GetSystemDirectory(
				szPath,
				_MAX_PATH
			))
			{
				return 2;
			}
			memcpy(
				szPath2,
				szPath,
				sizeof(TCHAR) * (_MAX_PATH + 1)
			);

			wcscat_s(
				szPath,
				_MAX_PATH,
				L"\\taskmgr.exe"
			);
			wcscat_s(
				szPath2,
				_MAX_PATH,
				L"\\TMLauncher.exe"
			);

			HANDLE userToken;
			if (!WTSQueryUserToken(
				WTSGetActiveConsoleSessionId(),
				&userToken
			))
			{
				return GetLastError();
			}

			HANDLE elevatedUserToken;
			DWORD dwRet;
			if (!GetTokenInformation(
				userToken,
				TokenLinkedToken,
				&elevatedUserToken,
				sizeof(HANDLE),
				&dwRet
			))
			{
				return GetLastError();
			}

			LPVOID pEnvBlock;
			if (!CreateEnvironmentBlock(
				&pEnvBlock,
				elevatedUserToken,
				FALSE
			))
			{
				return GetLastError();
			}

			SECURITY_ATTRIBUTES tokenAttributes = { 0 };
			tokenAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
			SECURITY_ATTRIBUTES threadAttributes = { 0 };
			threadAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
			PROCESS_INFORMATION pi = { 0 };
			STARTUPINFO si = { 0 };
			si.cb = sizeof(STARTUPINFO);
			si.lpDesktop = L"WinSta0\\Default";
			if (!CreateProcessAsUser(
				elevatedUserToken,
				szPath,
				NULL,
				&tokenAttributes,
				&threadAttributes,
				TRUE,
				CREATE_NEW_CONSOLE | INHERIT_CALLER_PRIORITY | CREATE_UNICODE_ENVIRONMENT,
				pEnvBlock,
				NULL,
				&si,
				&pi
			))
			{
				return 7;
			}

			if (RegSetValueExW(
				hKey,
				L"Debugger",
				0,
				REG_SZ,
				szPath2,
				sizeof(TCHAR) * wcslen(szPath2)
			))
			{
				return GetLastError();
			}

			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			DestroyEnvironmentBlock(pEnvBlock);
			CloseHandle(elevatedUserToken);
			CloseHandle(userToken);
			RegCloseKey(hKey);
		}
	}

	return 0;
}

VOID WINAPI ServiceCtrlHandlerEx(
	DWORD dwControl,
	DWORD dwEventType,
	LPVOID lpEventData,
	LPVOID lpContext
)
{
	switch (dwControl)
	{
	case SERVICE_CONTROL_SESSIONCHANGE:

		if (dwEventType == WTS_CONSOLE_CONNECT)
		{
			SetEvent(g_ServiceSessionStartEvent);
		}

		break;

	case SERVICE_CONTROL_STOP:

		if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
			break;

		/*
		 * Perform tasks necessary to stop the service here
		 */

		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		g_ServiceStatus.dwWin32ExitCode = 0;
		g_ServiceStatus.dwCheckPoint = 4;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(TEXT(
				"TMLauncher Service: ServiceCtrlHandler: SetServiceStatus returned error"));
		}

		// This will signal the worker thread to start shutting down
		SetEvent(g_ServiceStopEvent);

		break;

	case SERVICE_CONTROL_LAUNCH:
	{
		SetEvent(g_ServiceLaunchEvent);
		break;
	}

	default:
		break;
	}
}

VOID WINAPI ServiceMain(
	DWORD argc,
	LPTSTR* argv
)
{
	DWORD Status = E_FAIL;

	// Register our service control handler with the SCM
	g_StatusHandle = RegisterServiceCtrlHandlerEx(
		SERVICE_NAME,
		ServiceCtrlHandlerEx,
		NULL
	);

	if (g_StatusHandle == NULL)
	{
		return;
	}

	// Tell the service controller we are starting
	ZeroMemory(
		&g_ServiceStatus,
		sizeof(g_ServiceStatus)
	);
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(
		g_StatusHandle,
		&g_ServiceStatus
	) == FALSE)
	{
		OutputDebugString(TEXT(
			"RemoteStreamer Service: ServiceMain: SetServiceStatus returned error"));
	}

	/*
	 * Perform tasks necessary to start the service here
	 */

	 // Create a service stop event to wait on later
	g_ServiceStopEvent = CreateEvent(
		NULL,
		TRUE,
		FALSE,
		NULL
	);
	g_ServiceLaunchEvent = CreateEvent(
		NULL,
		FALSE,
		FALSE,
		NULL
	);
	g_ServiceSessionStartEvent = CreateEvent(
		NULL,
		TRUE,
		FALSE,
		NULL
	);
	if (g_ServiceStopEvent == NULL || g_ServiceSessionStartEvent == NULL)
	{
		// Error creating event
		// Tell service controller we are stopped and exit
		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;

		if (SetServiceStatus(
			g_StatusHandle,
			&g_ServiceStatus
		) == FALSE)
		{
			OutputDebugString(TEXT(
				"RemoteStreamer Service: ServiceMain: SetServiceStatus returned error"));
		}
		return;
	}

	// Tell the service controller we are started
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(
		g_StatusHandle,
		&g_ServiceStatus
	) == FALSE)
	{
		OutputDebugString(TEXT(
			"RemoteStreamer Service: ServiceMain: SetServiceStatus returned error"));
	}

	// Start a thread that will perform the main task of the service
	HANDLE hThread = CreateThread(
		NULL,
		0,
		ServiceWorkerThread,
		NULL,
		0,
		NULL
	);
	if (hThread == NULL)
	{
		// Error creating event
		// Tell service controller we are stopped and exit
		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;

		if (SetServiceStatus(
			g_StatusHandle,
			&g_ServiceStatus
		) == FALSE)
		{
			OutputDebugString(TEXT(
				"RemoteStreamer Service: ServiceMain: SetServiceStatus returned error"));
		}
		return;

	}

	// Wait until our worker thread exits signaling that the service needs to stop
	WaitForSingleObject(hThread, INFINITE);

	DWORD dwExitCode = 0;
	GetExitCodeThread(hThread, &dwExitCode);

	/*
	 * Perform any cleanup tasks
	 */

	CloseHandle(g_ServiceStopEvent);
	CloseHandle(g_ServiceSessionStartEvent);

	// Tell the service controller we are stopped
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = dwExitCode;
	g_ServiceStatus.dwCheckPoint = 3;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(TEXT(
			"RemoteStreamer Service: ServiceMain: SetServiceStatus returned error"));
	}

EXIT:
	return;
}
