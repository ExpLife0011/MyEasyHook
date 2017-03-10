#include <windows.h>

typedef struct _REMOTE_ENTRY_INFOR_
{
	ULONG           HostProcessPID;
	UCHAR*          UserData;
	ULONG           UserDataSize;
}REMOTE_ENTRY_INFOR, *PREMOTE_ENTRY_INFOR;

HMODULE DllModule = NULL;


BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		DllModule = hModule;
		break;
	}
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

DWORD WINAPI MainThread(PVOID lParam)
{
	MessageBoxA(NULL, "Success", "EasyHookInject", MB_OK);

	FreeLibraryAndExitThread(DllModule, 0);
	return 0;
}

EXTERN_C __declspec(dllexport) VOID  _stdcall EasyHookInjectionEntry(PVOID Data)
{
	

	//FreeLibraryAndExitThread(DllModule, 0);	// ����������ͷ��Լ����˳����� - EasyHookDll �Ͳ��ܵõ������ͷ� - �����ºۼ�
	// ����Ӧ������һ���߳� - ������ִ�������Ĵ��� - ��������ͷź��˳��߳� �������޺�

	CreateThread(NULL, 0, MainThread, NULL, 0, NULL);

	return;
}

EXTERN_C __declspec(dllexport) VOID  _stdcall NativeInjectionEntryPoint(PVOID Data)
{
	//FreeLibraryAndExitThread(DllModule, 0);

	CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
	return;
}