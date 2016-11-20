#include "common.h"

#define UNMANAGED_ERROR(code) {ErrorCode = ((code) & 0xFF) | 0xF0000000; goto ABORT_ERROR;}
typedef VOID(_stdcall *pfnRemoteEntryProc)(PREMOTE_ENTRY_INFOR RemoteEntryInfo);

EASYHOOK_NT_INTERNAL CompleteUnmanagedInjection(PREMOTE_INFOR RemoteInfo);
EASYHOOK_NT_API HookCompleteInjection(PREMOTE_INFOR RemoteInfo);


EASYHOOK_NT_API HookCompleteInjection(PREMOTE_INFOR RemoteInfo)
{
	ULONG32		ErrorCode = 0;

	HMODULE		Kernel32Handle = GetModuleHandleA("Kernel32.dll");

	if (Kernel32Handle == NULL)
	{
		UNMANAGED_ERROR(3);
	}

	// ���»��һ�麯����ַ - �ṩ�ȶ���
	// ���������Ѿ������ڶԷ����̿ռ���
	RemoteInfo->LoadLibraryW = GetProcAddress(Kernel32Handle, "LoadLibraryW");
	RemoteInfo->FreeLibrary = GetProcAddress(Kernel32Handle, "FreeLibrary");
	RemoteInfo->GetLastError = GetProcAddress(Kernel32Handle, "GetLastError");
	RemoteInfo->GetProcAddress = GetProcAddress(Kernel32Handle, "GetProcAddress");
	RemoteInfo->ExitThread = GetProcAddress(Kernel32Handle, "ExitThread");
	RemoteInfo->VirtualProtect = GetProcAddress(Kernel32Handle, "VirtualProtect");
	RemoteInfo->VirtualFree = GetProcAddress(Kernel32Handle, "VirtualFree");
	
	// ���û������� - ʡ��


	// ���� TLS
	if (!RTL_SUCCESS(RhSetWakeUpThreadID(RemoteInfo->WakeUpThreadID)))  // ��Ŀ���߳����һ��TLS������ֵ
		UNMANAGED_ERROR(3);
	
	// ��������Ҫע���Dll
	if (RemoteInfo->IsManaged)
	{
		// .NET Hook
	}
	else
	{
		// Win32 Hook
		ErrorCode = CompleteUnmanagedInjection(RemoteInfo);
	}

ABORT_ERROR:
	if (RemoteInfo->RemoteSignalEvent != NULL)	// Hook ���������SetEvent ���� �쳣�˳� �������ĸ� ���Ƕ�Ӧ��
	{
		CloseHandle(RemoteInfo->RemoteSignalEvent);
	}

	return ErrorCode;
}

EASYHOOK_NT_INTERNAL CompleteUnmanagedInjection(PREMOTE_INFOR RemoteInfo)
{
	ULONG32		ErrorCode = 0;
	HMODULE		UserLibraryHandle = NULL;
	REMOTE_ENTRY_INFOR RemoteEntryInfor = { 0 };
	// ע����������� ������ UserLibrary ����ʵ�� - ע�⵼�������ĵ���Լ��һ���� _stdcall
	UserLibraryHandle = LoadLibraryW(RemoteInfo->UserInjectLibrary);
	if (UserLibraryHandle == NULL)
	{
		UNMANAGED_ERROR(20);
	}

	pfnRemoteEntryProc RemoteEntryProc = (pfnRemoteEntryProc)GetProcAddress(UserLibraryHandle,
#ifdef _M_X64
	"EasyHookInjectionEntry"
#else
	"_EasyHookInjectionEntry@4"
#endif
	);
	if (RemoteEntryProc == NULL)
	{
		UNMANAGED_ERROR(21);
	}

	if (!SetEvent(RemoteInfo->RemoteSignalEvent))	// ���� RhInject ���� ע��ɹ�
	{
		UNMANAGED_ERROR(22);
	}

	// ���ò���
	RemoteEntryInfor.HostProcessPID = RemoteInfo->HostProcessID;
	RemoteEntryInfor.UserData = (RemoteInfo->UserData) ? RemoteInfo->UserData : NULL;
	RemoteEntryInfor.UserDataSize = RemoteInfo->UserDataSize;

	RemoteEntryProc(&RemoteEntryInfor);

	// �ǲ��ǿ����������ͷ� UserInjectLibrary ???
	return STATUS_SUCCESS;

ABORT_ERROR:

	return ErrorCode;
}