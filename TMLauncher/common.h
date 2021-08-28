#ifndef _H_COMMON_H_
#define _H_COMMON_H_
#include <Windows.h>
#define SERVICE_NAME TEXT("TMLauncher")
#define EXIT_SAS 1
#define SERVICE_CONTROL_LAUNCH 255

VOID WINAPI ServiceMain(
	DWORD argc,
	LPTSTR* argv
);

INT WINAPI ApplicationMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	PWSTR pCmdLine,
	INT nCmdShow
);

#endif