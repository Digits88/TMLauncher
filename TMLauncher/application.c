#include "common.h"

INT WINAPI ApplicationMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	PWSTR pCmdLine,
	INT nCmdShow
)
{
    SC_HANDLE hSC = OpenSCManager(
        NULL,
        NULL,
        SC_MANAGER_CONNECT
    );
    if (hSC)
    {
        SC_HANDLE hS = OpenService(
            hSC,
            SERVICE_NAME,
            SERVICE_USER_DEFINED_CONTROL
        );
        if (hS)
        {
            SERVICE_STATUS ss;
            ControlService(
                hS,
                SERVICE_CONTROL_LAUNCH,
                &ss
            );

            HWND hWnd;
            while ((hWnd = FindWindow(L"TaskManagerWindow", NULL)) == NULL)
            {
                Sleep(100);
            }
            SetForegroundWindow(hWnd);
        }
    }
    
    return 0;
}