#include "common.h"

typedef BOOL(__stdcall *pfnIsWow64Process)(HANDLE ProcessHandle, BOOL* Isx64);
typedef VOID(*pfnGetNativeSystemInfo)(LPSYSTEM_INFO SystemInfo);
typedef LONG(WINAPI* pfnNtCreateThreadEx)(PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess, LPVOID ObjectAttributes,
										  HANDLE ProcessHandle, LPTHREAD_START_ROUTINE ThreadStart, LPVOID ThreadParameter,
										  BOOL CreateSuspended, DWORD StackSize, LPVOID Unknown1, LPVOID Unknown2, LPVOID Unknown3);

PVOID		GetRemoteProcAddress(ULONG32 ProcessID, HANDLE ProcessHandle, CHAR* strModuleName, CHAR* strFunctioName);
HMODULE		GetRemoteModuleHandle(ULONG32 ProcessID, CHAR* strModuleName);
ULONG		GetShellCodeSize();
BYTE*		GetInjectionPtr();


EASYHOOK_NT_API RhInjectLibrary(INT32 TargetProcessID, INT32 WakeUpThreadID, INT32 InjectionOptions,
						         WCHAR* LibraryPath_x86, WCHAR* LibraryPath_x64, PVOID InPassThruBuffer, INT32 InPassThruSize)
{
	NTSTATUS NtStatus = STATUS_SUCCESS;
	HANDLE   TargetProcessHandle = NULL;
	BOOL	 bIs64BitTarget = FALSE;

	PREMOTE_INFOR RemoteInfo = NULL;
	PREMOTE_INFOR CorrectRemoteInfo = NULL;
	ULONG32  RemoteInfoLength = 0;
	ULONG_PTR CorrectValue = 0;

	HANDLE	 RemoteThreadHandle = NULL;
	HANDLE   RemoteSignalEvent = NULL;
	HANDLE   HandleArrary[2] = { 0 };
	WCHAR    UserInjectLibrary[MAX_PATH + 1] = { 0 };
	ULONG32  UserInjectLibraryLength = 0;
	WCHAR    EasyHookWorkPath[MAX_PATH + 1] = { 0 };	// ����Dll��ע������������·��
	ULONG32  EasyHookWorkPathLength = 0;
	WCHAR    EasyHookDllPath[MAX_PATH + 1] = { 0 };	    // ��ǰDll������·��
	ULONG32  EasyHookDllPathLength = 0;
	CHAR     EasyHookEntryProcName[MAX_PATH + 1] =
#ifndef _WIN64
		"_HookCompleteInjection@4";
#else
		"HookCompleteInjection";
#endif
	ULONG32  EasyHookEntryProcNameLength = 0;

	ULONG32 ShellCodeLength = 0;
	PUCHAR  RemoteShellCodeBase = NULL;

	ULONG32 Index = 0;
	ULONG32 ErrorCode = 0;
	SIZE_T  ReturnLength = 0;

	// �������Ϸ���
	if (InPassThruSize > MAX_PASSTHRU_SIZE)
	{
		THROW(STATUS_INVALID_PARAMETER_7, L"The given pass thru buffer is too large.");
	}
	if (InPassThruBuffer != NULL)
	{
		if (!IsValidPointer(InPassThruBuffer, InPassThruSize))
		{
			THROW(STATUS_INVALID_PARAMETER_6, L"The given pass thru buffer is invalid.");
		}
	}
	else if (InPassThruSize != 0)
	{
		THROW(STATUS_INVALID_PARAMETER_7, L"If no pass thru buffer is specified, the pass thru length also has to be zero.");
	}

	if (TargetProcessID == GetCurrentProcessId())	// ��֧�ֹ��Լ�
	{
		THROW(STATUS_NOT_SUPPORTED, L"For stability reasons it is not supported to inject into the calling process.");
	}

	if ((TargetProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, TargetProcessID)) == NULL)
	{
		if (GetLastError() == ERROR_ACCESS_DENIED)
			THROW(STATUS_ACCESS_DENIED, L"Unable to open target process. Consider using a system service.")
		else
			THROW(STATUS_NOT_FOUND, L"The given target process does not exist!");
	}

	// ���Ŀ��λ�� ֻ֧��32��32 64��64
#ifdef _M_X64
	FORCE(RhIsX64Process(TargetProcessID, &bIs64BitTarget));

	if (!bIs64BitTarget)
	{
		THROW(STATUS_WOW_ASSERTION, L"It is not supported to directly hook through the WOW64 barrier.");
	}

	if (!GetFullPathNameW(LibraryPath_x64, MAX_PATH, UserInjectLibrary, NULL))
	{
		THROW(STATUS_INVALID_PARAMETER_5, L"Unable to get full path to the given 64-bit library.");
	}
#else
	FORCE(RhIsX64Process(TargetProcessID, &bIs64BitTarget));

	if (bIs64BitTarget)
	{
		THROW(STATUS_WOW_ASSERTION, L"It is not supported to directly hook through the WOW64 barrier.");
	}

	if (!GetFullPathNameW(LibraryPath_x86, MAX_PATH, UserInjectLibrary, NULL))
	{
		THROW(STATUS_INVALID_PARAMETER_4, L"Unable to get full path to the given 32-bit library.");
	}
#endif
	// ���Ҫע���Library ��ȷ����
	if (!RtlFileExists(UserInjectLibrary))
	{
#ifdef _M_X64
		THROW(STATUS_INVALID_PARAMETER_5, L"The given 64-Bit library does not exist!");
#else
		THROW(STATUS_INVALID_PARAMETER_4, L"The given 32-Bit library does not exist!");
#endif
	}

	// �õ���ǰ����Ŀ¼ ע���������·�� - Ϊע����� ���û������� �����ַ�����׼��
	RtlGetWorkingDirectory(EasyHookWorkPath, MAX_PATH - 1);
	// �õ���ǰģ���·�� DllPath
	RtlGetCurrentModulePath(EasyHookDllPath, MAX_PATH);
	
	// �����ַ��������ĳ���
	EasyHookDllPathLength = (RtlUnicodeLength(EasyHookDllPath) + 1) * 2;
	EasyHookEntryProcNameLength = RtlAnsiLength(EasyHookEntryProcName) + 1;
	EasyHookWorkPathLength = (RtlUnicodeLength(EasyHookWorkPath) + 2) * 2;
	UserInjectLibraryLength = (RtlUnicodeLength(UserInjectLibrary) + 2) * 2;

	EasyHookWorkPath[EasyHookWorkPathLength / 2 - 2] = ';';
	EasyHookWorkPath[EasyHookWorkPathLength / 2 - 1] = 0;

	// ע��������ܳ�:�ṹ�峤�� + �����ַ����ĳ���
	RemoteInfoLength = EasyHookDllPathLength + EasyHookEntryProcNameLength + EasyHookWorkPathLength + InPassThruSize + UserInjectLibraryLength;
	RemoteInfoLength += sizeof(REMOTE_INFOR);

	RemoteInfo = (PREMOTE_INFOR)RtlAllocateMemory(TRUE, RemoteInfoLength);
	if (RemoteInfo == NULL)
	{
		THROW(STATUS_NO_MEMORY, L"Unable to allocate memory in current process.");
	}

	// Զ���öԷ�����һ���߳� �Է�ֹ�Է��ڴ�����ʱ�򱻹����� Kernel32û�б����ص���� 
	// ѧϰһ������������ʱ�� ����������Щ����
	FORCE(NtForceLdrInitializeThunk(TargetProcessHandle));

	// �ڶԷ����̿ռ��� �õ�������ַ
	RemoteInfo->LoadLibraryW   = (PVOID)GetRemoteProcAddress(TargetProcessID, TargetProcessHandle, "kernel32.dll", "LoadLibraryW");
	RemoteInfo->FreeLibrary    = (PVOID)GetRemoteProcAddress(TargetProcessID, TargetProcessHandle, "kernel32.dll", "FreeLibrary");
	RemoteInfo->GetProcAddress = (PVOID)GetRemoteProcAddress(TargetProcessID, TargetProcessHandle, "kernel32.dll", "GetProcAddress");
	RemoteInfo->VirtualFree    = (PVOID)GetRemoteProcAddress(TargetProcessID, TargetProcessHandle, "kernel32.dll", "VirtualFree");
	RemoteInfo->VirtualProtect = (PVOID)GetRemoteProcAddress(TargetProcessID, TargetProcessHandle, "kernel32.dll", "VirtualProtect");
	RemoteInfo->ExitThread     = (PVOID)GetRemoteProcAddress(TargetProcessID, TargetProcessHandle, "kernel32.dll", "ExitThread");
	RemoteInfo->GetLastError   = (PVOID)GetRemoteProcAddress(TargetProcessID, TargetProcessHandle, "kernel32.dll", "GetLastError");

	RemoteInfo->WakeUpThreadID = WakeUpThreadID;
	RemoteInfo->IsManaged = InjectionOptions & EASYHOOK_INJECT_MANAGED;		// ע��ѡ��

	ShellCodeLength = GetShellCodeSize();
	 
	RemoteShellCodeBase = (PUCHAR)VirtualAllocEx(TargetProcessHandle, NULL, ShellCodeLength + RemoteInfoLength, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (RemoteShellCodeBase == NULL)
	{
		THROW(STATUS_NO_MEMORY, L"Unable to allocate memory in target process.");
	}

	// ������ǰ�Լ����̿ռ����RemoteInfo����ַ���ָ��
	PBYTE Offset = (PBYTE)(RemoteInfo + 1);	// Խ���ṹ�屾�� ׼��д���ַ���

	RemoteInfo->EasyHookEntryProcName = (CHAR*)Offset;
	RemoteInfo->EasyHookDllPath = (WCHAR*)(Offset += EasyHookEntryProcNameLength);
	RemoteInfo->EasyHookWorkPath = (WCHAR*)(Offset += EasyHookDllPathLength);
	RemoteInfo->UserData = (PBYTE)(Offset += EasyHookWorkPathLength);
	RemoteInfo->UserInjectLibrary = (WCHAR*)(Offset += InPassThruSize);

	RemoteInfo->Size = RemoteInfoLength;
	RemoteInfo->HostProcessID = GetCurrentProcessId();
	RemoteInfo->UserDataSize = 0;

	Offset += UserInjectLibraryLength;	//  �ṹ����ַ���β�� - Ҳ�����ڵ�ǰ���̿ռ�����ռ��β��

	if ((ULONG)(Offset - (PBYTE)RemoteInfo) > RemoteInfo->Size)
	{
		THROW(STATUS_BUFFER_OVERFLOW, L"A buffer overflow in internal memory was detected.");
	}

	// ���ַ����������뵽�Ľṹ����
	RtlCopyMemory(RemoteInfo->EasyHookWorkPath, EasyHookWorkPath, EasyHookWorkPathLength);
	RtlCopyMemory(RemoteInfo->EasyHookDllPath, EasyHookDllPath, EasyHookDllPathLength);
	RtlCopyMemory(RemoteInfo->EasyHookEntryProcName, EasyHookEntryProcName, EasyHookEntryProcNameLength);
	RtlCopyMemory(RemoteInfo->UserInjectLibrary, UserInjectLibrary, UserInjectLibraryLength);

	// Hook �����Ĳ�������
	if (InPassThruBuffer != NULL)
	{
		RtlCopyMemory(RemoteInfo->UserData, InPassThruBuffer, InPassThruSize);

		RemoteInfo->UserDataSize = InPassThruSize;
	}

	// д��ShellCode - �ȷ��� ShellCode ������ RemoteInfo
	if (!WriteProcessMemory(TargetProcessHandle, RemoteShellCodeBase, (PVOID)GetInjectionPtr(), ShellCodeLength, &ReturnLength) || ReturnLength != ShellCodeLength)
	{
		THROW(STATUS_INTERNAL_ERROR, L"Unable to write into target process memory.");
	}

	// ����ͨ���¼�
	RemoteSignalEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (RemoteSignalEvent == NULL)
	{
		THROW(STATUS_INSUFFICIENT_RESOURCES, L"Unable to create event.");
	}

	// ���¼���� Duplicate ��������̿ռ�, ���ǽ�����µľ��ֵ�ݴ��ڵ�ǰ���̿ռ�
	if (!DuplicateHandle(GetCurrentProcess(), RemoteSignalEvent, TargetProcessHandle, &RemoteInfo->RemoteSignalEvent, EVENT_ALL_ACCESS, FALSE, 0))
	{
		THROW(STATUS_INTERNAL_ERROR, L"Failed to duplicate remote event.");
	}

	//  ���������ṹ�������ַ���ָ�� - �����ǵĵ�ַ ����Ϊ�Ƕ���������ֵ
	//  �ṹ������ָ�뱾��ָ���ǵ�ǰ���̿ռ���ĵ�ַ - Ҳ���ǽṹ������ַ������׵�ַ
	//  ���ǵ����ǰѽṹ��д�뵽�Է����̿ռ����ʱ����Щָ��ҲӦ��ָ��ṹ�����ĵ�ַ - ��������Ҫ����������
	CorrectRemoteInfo = (PREMOTE_INFOR)(RemoteShellCodeBase + ShellCodeLength);
	CorrectValue = (PUCHAR)CorrectRemoteInfo - (PUCHAR)RemoteInfo;

	RemoteInfo->EasyHookDllPath = (wchar_t*)(((PUCHAR)RemoteInfo->EasyHookDllPath) + CorrectValue);
	RemoteInfo->EasyHookEntryProcName = (char*)(((PUCHAR)RemoteInfo->EasyHookEntryProcName) + CorrectValue);
	RemoteInfo->EasyHookWorkPath = (wchar_t*)(((PUCHAR)RemoteInfo->EasyHookWorkPath) + CorrectValue);
	RemoteInfo->UserInjectLibrary = (wchar_t*)(((PUCHAR)RemoteInfo->UserInjectLibrary) + CorrectValue);

	if (RemoteInfo->UserData != NULL)
	{
		RemoteInfo->UserData = (PBYTE)(((PUCHAR)RemoteInfo->UserData) + CorrectValue);
	}

	RemoteInfo->RemoteEntryPoint = RemoteShellCodeBase;

	if (!WriteProcessMemory(TargetProcessHandle, CorrectRemoteInfo, RemoteInfo, RemoteInfoLength, &ReturnLength) || ReturnLength != RemoteInfoLength)
	{
		THROW(STATUS_INTERNAL_ERROR, L"Unable to write into target process memory.");
	}

	// ����Զ���߳�
	if ((InjectionOptions & EASYHOOK_INJECT_STEALTH) != 0)
	{
		FORCE(RhCreateStealthRemoteThread(TargetProcessID, (LPTHREAD_START_ROUTINE)RemoteShellCodeBase, CorrectRemoteInfo, &RemoteThreadHandle));
	}
	else
	{
		if (!RTL_SUCCESS(RtlNtCreateThreadEx(TargetProcessHandle, (LPTHREAD_START_ROUTINE)RemoteShellCodeBase, CorrectRemoteInfo, FALSE, &RemoteThreadHandle)))
		{
			RemoteThreadHandle = CreateRemoteThread(TargetProcessHandle, NULL, 0, (LPTHREAD_START_ROUTINE)RemoteShellCodeBase, CorrectRemoteInfo, 0, NULL);
			if (RemoteThreadHandle == NULL)
			{
				THROW(STATUS_ACCESS_DENIED, L"Unable to create remote thread.");
			}
		}
	}

	// ע��������������Ⱥ�˳��
	HandleArrary[1] = RemoteSignalEvent;
	HandleArrary[0] = RemoteThreadHandle;

	Index = WaitForMultipleObjects(2, HandleArrary, FALSE, INFINITE);

	if (Index == WAIT_OBJECT_0)	// ����ʵ���쳣���� - ��ShellCode���ù����� ����ASM ShellCode(����Զ���߳�) Ȼ�� entry�ȵȲ��� 
								// ����ڵ���������ں���֮ǰ SetEvent, �õȴ����أ���ʱ��ķ���ֵӦ���� 1 Ҳ������������
								// �����������֮ǰ���κεط������˴����߳̾ͻ��������ô��ʱ��ȴ���������ֵ����0 ��ô�ͻ���� if���� �õ�����������ʾ
	{
		GetExitCodeThread(RemoteThreadHandle, &ErrorCode);

		SetLastError(ErrorCode & 0x0FFFFFFF);

		switch (ErrorCode & 0xF0000000)
		{
		case 0x10000000:
		{
			THROW(STATUS_INTERNAL_ERROR, L"Unable to find internal entry point.");
		}
		case 0x20000000:
		{
			THROW(STATUS_INTERNAL_ERROR, L"Unable to make stack executable.");
		}
		case 0x30000000:
		{
			THROW(STATUS_INTERNAL_ERROR, L"Unable to release injected library.");
		}
		case 0x40000000:
		{
			THROW(STATUS_INTERNAL_ERROR, L"Unable to find EasyHook library in target process context.");
		}
		case 0xF0000000:
			// Error in C++ Injection Completeion
		{
			switch (ErrorCode & 0xFF)
			{
#ifdef _M_X64
			case 20:
			{
				THROW(STATUS_INVALID_PARAMETER_5, L"Unable to load the given 64-bit library into target process.");
			}
			case 21:
			{
				THROW(STATUS_INVALID_PARAMETER_5, L"Unable to find the required native entry point in the given 64-bit library.");
			}
			case 12:
			{
				THROW(STATUS_INVALID_PARAMETER_5, L"Unable to find the required managed entry point in the given 64-bit library.");
			}
#else
			case 20:
			{
				THROW(STATUS_INVALID_PARAMETER_4, L"Unable to load the given 32-bit library into target process.");
			}
			case 21:
			{
				THROW(STATUS_INVALID_PARAMETER_4, L"Unable to find the required native entry point in the given 32-bit library.");
			}
			case 12:
			{
				THROW(STATUS_INVALID_PARAMETER_4, L"Unable to find the required managed entry point in the given 32-bit library.");
			}
#endif
			case 13:
			{
				THROW(STATUS_DLL_INIT_FAILED, L"The user defined managed entry point failed in the target process. Make sure that EasyHook is registered in the GAC. Refer to event logs for more information.");
			}
			case 1: 
			{
				THROW(STATUS_INTERNAL_ERROR, L"Unable to allocate memory in target process.");
			}
			case 2: 
			{
				THROW(STATUS_INTERNAL_ERROR, L"Unable to adjust target's PATH variable.");
			}
			case 3:
			{
				THROW(STATUS_INTERNAL_ERROR, L"Can't get Kernel32 module handle.");
			}
			case 10: 
			{
				THROW(STATUS_INTERNAL_ERROR, L"Unable to load 'mscoree.dll' into target process.");
			}
			case 11: 
			{
				THROW(STATUS_INTERNAL_ERROR, L"Unable to bind NET Runtime to target process.");
			}
			case 22:
			{
				THROW(STATUS_INTERNAL_ERROR, L"Unable to signal remote event.");
			}
			default: 
				THROW(STATUS_INTERNAL_ERROR, L"Unknown error in injected C++ completion routine.");
			}
		}
		case 0:
		{
			THROW(STATUS_INTERNAL_ERROR, L"C++ completion routine has returned success but didn't raise the remote event.");
		}
		default:
		{
			THROW(STATUS_INTERNAL_ERROR, L"Unknown error in injected assembler code.");
		}
		}
	}
	else if (Index != WAIT_OBJECT_0 + 1)	// ���������û�з���
	{
		THROW(STATUS_INTERNAL_ERROR, L"Unable to wait for injection completion due to timeout. ");
	}

	RETURN;

THROW_OUTRO:
FINALLY_OUTRO:
	{
		if (TargetProcessHandle != NULL)
		{
			CloseHandle(TargetProcessHandle);
		}

		if (RemoteInfo != NULL)
		{
			RtlFreeMemory(RemoteInfo);
		}

		if (RemoteThreadHandle != NULL)
		{
			CloseHandle(RemoteThreadHandle);
		}

		if (RemoteSignalEvent != NULL)
		{
			CloseHandle(RemoteSignalEvent);
		}

		return NtStatus;
	}
}

EASYHOOK_NT_API RhIsX64Process(ULONG32 ProcessID, BOOL * bIsx64)
{
	NTSTATUS NtStatus = 0;
	BOOL     bTemp = FALSE;
	pfnIsWow64Process	IsWow64Process = NULL;
	HANDLE TargetProcessHandle = NULL;

#ifndef _M_X64
	pfnGetNativeSystemInfo		GetNativeSystemInfo = NULL;
	SYSTEM_INFO					SystemInfo = { 0 };
#endif

	if (bIsx64 == NULL)
	{
		THROW(STATUS_INVALID_PARAMETER_2, L"The Given Result Storage Is Invalid.");
	}
	TargetProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, ProcessID);
	if (TargetProcessHandle == NULL)
	{
		if (GetLastError() == ERROR_ACCESS_DENIED)
		{
			THROW(STATUS_ACCESS_DENIED, L"Unable To Open Target Process. Consider Using a System Service.");
		}
		else
		{
			THROW(STATUS_NOT_FOUND, L"The Given Target Process Does Not Exist!");
		}
	}

	// ����ֱ��GetModuleHandle ������
	// Wow64 ϵ�к����� 64Bit���� ����������Ϊ�� �����ж϶Է��� 32Bit
	IsWow64Process = (pfnIsWow64Process)GetProcAddress(GetModuleHandleW(L"Kernel32.dll"), "IsWow64Process");
	// ���IsWow64Process Ӧ��ֻ������ 32λϵͳ ����ֱ�ӽ���#else��
#ifdef _M_X64
	// ����Է����� Wow64���� ����64-bit
	if (!IsWow64Process(TargetProcessHandle, &bTemp))
	{
		THROW(STATUS_INTERNAL_ERROR, L"Unable To Detect Wether Target Process Is 64-bit Or Not.");
	}

	bTemp = !bTemp;		// bTempһ��ʼ�Ǳ�ʾ �Ƿ��� Wow64 ��������Ҫ������ʾ�Ƿ��� 64bit ����ȡһ�η�
#else
	if (IsWow64Process != NULL)	// Ϊ��һ����32bit Proc
	{
		GetNativeSystemInfo = (pfnGetNativeSystemInfo)GetProcAddress(GetModuleHandle(L"Kernel32.dll"), "GetNativeSystemInfo");

		if (GetNativeSystemInfo == NULL)
		{
			GetNativeSystemInfo(&SystemInfo);

			if (SystemInfo.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_INTEL)  // PROCESSOR_ARCHITECTURE_INTEL - x86 ��־
			{
				// ��ǰ��64λϵͳ �Է���ӵ��Wowϵ����
				if (!IsWow64Process(TargetProcessHandle, &bTemp))
				{
					THROW(STATUS_INTERNAL_ERROR, L"Unable to detect wether target process is 64-bit or not.");
				}
				bTemp = !bTemp;
			}
		}
	}
#endif
	// ��������ṩWow���� ֱ��������һ��
	*bIsx64 = bTemp;
	RETURN(STATUS_SUCCESS);

THROW_OUTRO:
FINALLY_OUTRO:
	if (TargetProcessHandle != NULL)
	{
		CloseHandle(TargetProcessHandle);	
	}
	return NtStatus;
}

EASYHOOK_NT_INTERNAL NtForceLdrInitializeThunk(HANDLE ProcessHandle)
{
	HANDLE		RemoteThreadHandle = NULL;
	BYTE		ShellCode[3] = { 0 };
	ULONG32		ShellCodeSize = 0;
	SIZE_T		WriteSize = 0;
	PUCHAR		RemoteBuffer = NULL;
	NTSTATUS	NtStatus = STATUS_SUCCESS;

#ifdef _M_X64
	// 64λ ���Ĵ������ݲ��� ���Բ���Ҫ��ջ�ָ� ֱ��ret
	ShellCode[0] = 0xC3;	// ret
	ShellCodeSize = 1;
#else
	// 32λ ���̻ص����� ��һ������ ����32λ�ǿ�ѹ�������� ���Ժ�������Ӧ�ö�ջƽ��
	ShellCode[0] = 0xC2;	// ret 0x4
	ShellCode[1] = 0x04;
	ShellCode[2] = 0x00;
	ShellCodeSize = 3;
#endif
	// �ڶԷ����������ڴ� 
	RemoteBuffer = (PUCHAR)VirtualAllocEx(ProcessHandle, NULL, ShellCodeSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (RemoteBuffer == NULL)
	{
		THROW(STATUS_NO_MEMORY, L"Unable To Allocate Memory In Target Process.");
	}
	//  д��ShellCode
	if (!WriteProcessMemory(ProcessHandle, RemoteBuffer, ShellCode, ShellCodeSize, &WriteSize) || WriteSize != ShellCodeSize)
	{
		THROW(STATUS_INTERNAL_ERROR, L"Unable To Write Into Target Process Memory.");
	}
	// ����Զ���߳�
	if (!RTL_SUCCESS(RtlNtCreateThreadEx(ProcessHandle, (LPTHREAD_START_ROUTINE)RemoteBuffer, NULL, FALSE, &RemoteThreadHandle)))
	{
		// ��һ�ַ���
		RemoteThreadHandle = CreateRemoteThread(ProcessHandle, NULL, 0, (LPTHREAD_START_ROUTINE)RemoteBuffer, NULL, 0, NULL);
		if (RemoteThreadHandle == NULL)
		{
			THROW(STATUS_ACCESS_DENIED, L"Unable To Create Remote Thread.");
		}
	}
	// �ȴ�Զ���߳�ִ����� - Ҳ���ǶԷ����л�����ʼ����� Kernel32�������
	WaitForSingleObject(RemoteThreadHandle, INFINITE);

	RETURN;

THROW_OUTRO:
FINALLY_OUTRO:
	return NtStatus;
}

EASYHOOK_NT_INTERNAL RtlNtCreateThreadEx(HANDLE ProcessHandle, LPTHREAD_START_ROUTINE ThreadStart, PVOID ThreadParameter, BOOL IsThreadSuspended, HANDLE * ThreadHandle)
{
	HANDLE		TempHandle = NULL;
	NTSTATUS	NtStatus = STATUS_SUCCESS;
	pfnNtCreateThreadEx NtCreateThreadEx = NULL;

	if (ThreadHandle == NULL)
	{
		THROW(STATUS_INVALID_PARAMETER_4, L"The Given Handle Storage Is Invalid.");
	}

	NtCreateThreadEx = (pfnNtCreateThreadEx)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx");
	if (NtCreateThreadEx == NULL)
	{
		THROW(STATUS_NOT_SUPPORTED, L"NtCreateThreadEx() Is Not Supported.");
	}

	FORCE(NtCreateThreadEx(&TempHandle, 0x1FFFFF, NULL, ProcessHandle, (LPTHREAD_START_ROUTINE)ThreadStart, ThreadParameter, IsThreadSuspended, 0, NULL, NULL, NULL));

	*ThreadHandle = TempHandle;

	RETURN;

THROW_OUTRO:
FINALLY_OUTRO:
	return NtStatus;
}

PVOID  GetRemoteProcAddress(ULONG32 ProcessID, HANDLE ProcessHandle, CHAR* strModuleName, CHAR* strFunctioName)
{// Ϊɶ�������ݵ������� string ������ wstring ��Ϊ��������ݹ� - Ҳ����ת�����������ʱ �ӵ����������dll���ͺ����� ���ǵ��� ��������Ҫ��string
	HMODULE	   RemoteModuleHandle = NULL;
	ULONG_PTR  RemoteExportBase = 0;
	ULONG32    RemoteExportSize = 0;
	ULONG32*   RemoteExportNameTable = NULL;
	USHORT*	   RemoteExportNameOrdinalTable = NULL;
	ULONG32*   RemoteExportAddressTable = NULL;
	ULONG_PTR  RemoteFunctionAddress = 0;
	ULONG_PTR  RemoteNameAddress = 0;
	WORD       RemoteFunctionOdinal = 0;
	CHAR       RemoteFunctionName[MAX_PATH] = { 0 };
	IMAGE_DOS_HEADER		RemoteDosHeader = { 0 };
	IMAGE_NT_HEADERS		RemoteNtHeaders = { 0 };
	IMAGE_EXPORT_DIRECTORY	RemoteExportDirectory = { 0 };


	// �õ�ģ����
	RemoteModuleHandle = GetRemoteModuleHandle(ProcessID, strModuleName);
	if (RemoteModuleHandle == NULL)
	{
		return NULL;
	}

	// ��ȡDosͷ����
	if (!ReadProcessMemory(ProcessHandle, (PVOID)RemoteModuleHandle, &RemoteDosHeader, sizeof(IMAGE_DOS_HEADER), NULL) || RemoteDosHeader.e_magic != IMAGE_DOS_SIGNATURE)
	{
		return NULL;
	}
	// ��ȡNtͷ����
	if (!ReadProcessMemory(ProcessHandle, (PVOID)((DWORD_PTR)RemoteModuleHandle + RemoteDosHeader.e_lfanew), &RemoteNtHeaders, sizeof(IMAGE_NT_HEADERS), NULL) || RemoteNtHeaders.Signature != IMAGE_NT_SIGNATURE)
	{
		return NULL;
	}
	// ��ȡ������Ŀ¼
	if (!GetRemoteModuleExportDirectory(ProcessHandle, RemoteModuleHandle, &RemoteExportDirectory, RemoteDosHeader, RemoteNtHeaders))
	{
		return NULL;
	}

	// ���뵼������������ڴ�ռ�
	RemoteExportNameTable = (ULONG32*)malloc(RemoteExportDirectory.NumberOfNames * sizeof(ULONG32));
	RemoteExportNameOrdinalTable = (USHORT*)malloc(RemoteExportDirectory.NumberOfNames * sizeof(USHORT));
	RemoteExportAddressTable = (ULONG32*)malloc(RemoteExportDirectory.NumberOfFunctions * sizeof(ULONG32));

	// ���ڴ��ȡ�������ڴ�
	// ��ַ��
	if (!ReadProcessMemory(ProcessHandle, (PVOID)((ULONG_PTR)RemoteModuleHandle + (ULONG_PTR)RemoteExportDirectory.AddressOfFunctions),
		RemoteExportAddressTable, RemoteExportDirectory.NumberOfFunctions * sizeof(ULONG32), NULL))
	{
		free(RemoteExportNameTable);
		free(RemoteExportNameOrdinalTable);
		free(RemoteExportAddressTable);
		return NULL;
	}

	// ������
	if (!ReadProcessMemory(ProcessHandle, (PVOID)((ULONG_PTR)RemoteModuleHandle + (ULONG_PTR)RemoteExportDirectory.AddressOfNames),
		RemoteExportNameTable, RemoteExportDirectory.NumberOfNames * sizeof(ULONG32), NULL))
	{
		free(RemoteExportNameTable);
		free(RemoteExportNameOrdinalTable);
		free(RemoteExportAddressTable);
		return NULL;
	}

	// ���������� ��Ϊ�������ǰ��������������ɵ� ���Խڵ����������һ��
	if (!ReadProcessMemory(ProcessHandle, (PVOID)((ULONG_PTR)RemoteModuleHandle + (ULONG_PTR)RemoteExportDirectory.AddressOfNameOrdinals),
		RemoteExportNameOrdinalTable, RemoteExportDirectory.NumberOfNames * sizeof(WORD), NULL))
	{
		free(RemoteExportNameTable);
		free(RemoteExportNameOrdinalTable);
		free(RemoteExportAddressTable);
		return NULL;
	}

	RemoteExportBase = ((ULONG_PTR)RemoteModuleHandle + RemoteNtHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
	RemoteExportSize = RemoteNtHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;

	// ����������
	for (ULONG32 i = 0; i < RemoteExportDirectory.NumberOfNames; i++)
	{
		RemoteFunctionOdinal = RemoteExportNameOrdinalTable[i];
		RemoteNameAddress = (ULONG_PTR)RemoteModuleHandle + RemoteExportNameTable[i];
		RemoteFunctionAddress = (ULONG_PTR)RemoteModuleHandle + RemoteExportAddressTable[RemoteFunctionOdinal];

		ZeroMemory(RemoteFunctionName, MAX_PATH);

		if (!ReadProcessMemory(ProcessHandle, (PVOID)RemoteNameAddress, RemoteFunctionName, MAX_PATH, NULL))	// ȷ�϶Է��������ɶ��� - �����Ϊ����֤�Է���ȷ�ǰ����Ƶ����ģ�
		{
			continue;
		}

		if (_stricmp(RemoteFunctionName, strFunctioName) != 0)		// �Ա����� �ҵ�����Ѱ�ҵ�����
		{
			continue;
		}

		if (RemoteFunctionOdinal >= RemoteExportDirectory.NumberOfNames)
		{
			return NULL;
		}

		// �����ַ���ڵ�����Χ�� ��˵��ת������
		if (RemoteFunctionAddress >= RemoteExportBase && RemoteFunctionAddress <= RemoteExportBase + RemoteExportSize)
		{
			CHAR	  SourceDllName[MAX_PATH] = { 0 };
			CHAR	  TargetFunctionName[MAX_PATH] = { 0 };
			CHAR      szSourceFilePath[MAX_PATH] = { 0 };

			if (!ReadProcessMemory(ProcessHandle, (PVOID)RemoteFunctionAddress, szSourceFilePath, MAX_PATH, NULL))
			{
				continue;
			}

			CHAR* Temp = strchr(szSourceFilePath, '.');		// ת���������ĵ�ַ ʵ����һ���ַ��� (ԴDLL��).(ָ��ĺ�����)

															// ���Ŀ�꺯����
			strcpy(TargetFunctionName, Temp + 1);
			// ����ԴDLL��
			memcpy(SourceDllName, szSourceFilePath, (ULONG_PTR)Temp - (ULONG_PTR)szSourceFilePath);
			strcat(SourceDllName, ".dll");

			free(RemoteExportNameTable);
			free(RemoteExportNameOrdinalTable);
			free(RemoteExportAddressTable);

			return GetRemoteProcAddress(ProcessID, ProcessHandle, SourceDllName, TargetFunctionName);		// ������Ϊ��һ���Ĵ��� ����return��ʽ��ͳһ ������ ǰ����THROWһ������
		}

		free(RemoteExportNameTable);
		free(RemoteExportNameOrdinalTable);
		free(RemoteExportAddressTable);

		return (PVOID)RemoteFunctionAddress;
	}

	free(RemoteExportNameTable);
	free(RemoteExportNameOrdinalTable);
	free(RemoteExportAddressTable);

	return NULL;
}

HMODULE GetRemoteModuleHandle(ULONG32 ProcessID, CHAR* strModuleName)
{
	MODULEENTRY32	ModuleEntry32 = { 0 };
	HANDLE			ToolHelp32SnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ProcessID);
	CHAR			szModuleName[MAX_PATH] = { 0 };
	size_t			TransferSize = 0;

	ModuleEntry32.dwSize = sizeof(MODULEENTRY32);
	Module32First(ToolHelp32SnapshotHandle, &ModuleEntry32);
	do
	{
		wcstombs_s(&TransferSize, szModuleName, MAX_PATH, ModuleEntry32.szModule, MAX_PATH);

		if (!_stricmp(szModuleName, strModuleName))		// �ҵ�Ŀ��ģ������
		{
			CloseHandle(ToolHelp32SnapshotHandle);
			return ModuleEntry32.hModule;
		}
	} while (Module32Next(ToolHelp32SnapshotHandle, &ModuleEntry32));

	CloseHandle(ToolHelp32SnapshotHandle);
	return NULL;
}

BOOL EASYHOOK_API GetRemoteModuleExportDirectory(HANDLE ProcessHandle, HMODULE ModuleHandle,
	PIMAGE_EXPORT_DIRECTORY RemoteExportDirectory, IMAGE_DOS_HEADER RemoteDosHeader, IMAGE_NT_HEADERS RemoteNtHeaders)
{
	DWORD	   ExportTableAddr = 0;
	PBYTE	   RemoteModulePEHeader = NULL;
	PIMAGE_SECTION_HEADER	RemoteSectionHeader = NULL;

	if (RemoteExportDirectory == NULL)
	{
		return FALSE;
	}

	RemoteModulePEHeader = (PBYTE)malloc(1024 * sizeof(PBYTE));		// PEͷ һ��ֻ�� 1024��
	ZeroMemory(RemoteExportDirectory, sizeof(IMAGE_EXPORT_DIRECTORY));

	if (!ReadProcessMemory(ProcessHandle, (PVOID)ModuleHandle, RemoteModulePEHeader, 1024, NULL))
	{
		return FALSE;
	}

	// RemoteModulePEHeader ����������ģ����ڴ���ʼ��ַ + e_lfanew ��PEͷ + sizeof(IMAGE_NT_HEADERS) ���˽ڱ��ͷ��
	RemoteSectionHeader = (PIMAGE_SECTION_HEADER)(RemoteModulePEHeader + RemoteDosHeader.e_lfanew + sizeof(IMAGE_NT_HEADERS));

	for (int i = 0; i < RemoteNtHeaders.FileHeader.NumberOfSections; i++, RemoteSectionHeader++)
	{
		if (RemoteSectionHeader == NULL)
		{
			continue;
		}

		// �ҵ��ڱ��е� .edata Ҳ���ǵ�����Ľڱ�
		if (_stricmp((CHAR*)(RemoteSectionHeader->Name), ".edata") == 0)
		{
			// VirtualAddress ���ӻ���ַ ��- Դ����û�� ���Լ�����
			if (!ReadProcessMemory(ProcessHandle, (PVOID)(ModuleHandle + RemoteSectionHeader->VirtualAddress), RemoteExportDirectory, sizeof(IMAGE_EXPORT_DIRECTORY), NULL))
			{
				continue;
			}

			free(RemoteModulePEHeader);
			return TRUE;
		}
	}

	// ֱ�Ӵӿ�ѡͷ���RVA
	ExportTableAddr = RemoteNtHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
	if (ExportTableAddr == 0)
	{
		return FALSE;
	}

	// ��������
	if (!ReadProcessMemory(ProcessHandle, (PVOID)((ULONG_PTR)ModuleHandle + ExportTableAddr), RemoteExportDirectory, sizeof(IMAGE_EXPORT_DIRECTORY), NULL))
	{
		return FALSE;
	}

	free(RemoteModulePEHeader);
	return TRUE;
}

// ASM �ļ� ����
static DWORD InjectionSize = 0;

#ifdef _M_X64
	EXTERN_C VOID Injection_ASM_x64();
#else
EXTERN_C VOID __stdcall Injection_ASM_x86();
#endif

BYTE* GetInjectionPtr()	// �õ�ASM�����׵�ַ
{
#ifdef _M_X64
	BYTE* Ptr = (BYTE*)Injection_ASM_x64;
#else
	BYTE* Ptr = (BYTE*)Injection_ASM_x86;
#endif

	// stdcall ���� - ��һ����ת ��ȥ����һ����ת E9 = jump, �õ�E9�������תƫ��
	if (*Ptr == 0xE9)
	{
		Ptr += *((int*)(Ptr + 1)) + 5;
		// Ptr + 1 - ���� E9����int* ����һ��,�õ���ת��ƫ�ơ���ת�Ļ���ַ����һ��ָ��Ļ���ַ Ptr += 5�� ���õ�������ת����ʵ�ʵ�ַ��Ҳ���Ǻ���ʵ�ֵ�ʵ�ʵ�ַ��
	}

	return Ptr;
}

ULONG GetShellCodeSize()
{
	UCHAR*          Ptr;
	UCHAR*          BasePtr;
	ULONG           Index;
	ULONG           Signature;

	if (InjectionSize != 0)
		return InjectionSize;

	// ����Ӳ���� �õ�����
	BasePtr = Ptr = GetInjectionPtr();

	for (Index = 0; Index < 2000 /* some always large enough value*/; Index++)
	{
		Signature = *((ULONG32*)Ptr);

		if (Signature == 0x12345678)		// �Լ�ASM�ļ�ĩβ�ֶ�д�ñ�־
		{
			InjectionSize = (ULONG)(Ptr - BasePtr);

			return InjectionSize;
		}

		Ptr++;
	}

	ASSERT(FALSE, L"thread.c - ULONG GetInjectionSize()");

	return 0;
}

extern DWORD RhTlsIndex;
EASYHOOK_NT_INTERNAL RhSetWakeUpThreadID(ULONG32 InThreadID)
{
	NTSTATUS NtStatus;

	if (!TlsSetValue(RhTlsIndex, (PVOID)(size_t)InThreadID))
	{
		THROW(STATUS_INTERNAL_ERROR, L"Unable to set TLS value.");
	}

	RETURN;

THROW_OUTRO:
FINALLY_OUTRO:
	return NtStatus;

}