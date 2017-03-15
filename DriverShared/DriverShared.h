#ifndef _DRIVERSHARED_H_
#define _DRIVERSHARED_H_

#include "Rtl/Rtl.h"

#define EASYHOOK_NT_INTERNAL            EXTERN_C NTSTATUS __stdcall
#define EASYHOOK_BOOL_INTERNAL          EXTERN_C BOOL __stdcall

#define EASYHOOK_INJECT_MANAGED			0x00000001

// Local Hook

// ACL - ������Щ�߳̿���ִ��HookProc����
typedef struct _HOOK_ACL_
{
	ULONG		    Count;
	BOOL			IsExclusive;
	ULONG			Entries[MAX_ACE_COUNT];	// ACE - Access Control Entry
}HOOK_ACL, *PHOOK_ACL;

#define LOCAL_HOOK_SIGNATURE ((ULONG)0x6A910BE2)

typedef struct _LOCAL_HOOK_INFO_
{
	PLOCAL_HOOK_INFO Next;
	ULONG			 Size;

	PVOID			 TargetProc;		// ��Hook����
	ULONG64			 TargetBackup;	    // Ŀ�걸�ݺ���
	ULONGLONG		 TargetBackup_x64;  // X64-Driverʹ��
	ULONG64			 HookOldSave;		// ����Hook���ԭ����
	ULONG			 EntrySize;			// ���ָ��� (>5
	PVOID			 Trampoline;
	ULONG			 HLSIndex;			// GlobalSlotList ע������
	ULONG			 HLSIdent;			// ʵ��ע��ID
	PVOID			 CallBack;			// �ص�����
	HOOK_ACL		 LocalACL;			// Access Control List
	ULONG			 Signature;			// ע���־λ - �Ƿ��Ѿ���Hook

	TRACED_HOOK_HANDLE      Tracking;   // ָ�� ������ǰHook��Ϣ��Handle

	PVOID			 RandomValue;	
	PVOID			 HookIntro;			// ACL�ж� - Tramp ��ʼ������
	PVOID			 OldProc;			// �洢�����ǵ�ԭ��ڴ���
	PVOID			 HookProc;			// ʵ��Hook����
	PVOID			 HookOutro;			// ���л�����ʼ������

	INT*			 IsExecutedPtr;		// ? 
}LOCAL_HOOK_INFO, *PLOCAL_HOOK_INFO;

// Local Hook ȫ�ֱ���
extern RTL_SPIN_LOCK GlobalHookLock;

//EasyHookDll/LocalHook/reloc.c �ڲ����� - udis86
EASYHOOK_NT_INTERNAL LhRoundToNextInstruction(PVOID InCodePtr, ULONG InCodeSize, PULONG OutOffset);
EASYHOOK_NT_INTERNAL LhGetInstructionLength(PVOID InPtr, PULONG OutLength);
EASYHOOK_NT_INTERNAL LhRelocateEntryPoint(PVOID InEntryPoint, ULONG InEPSize, PVOID Buffer, PULONG OutRelocSize);
EASYHOOK_NT_INTERNAL LhRelocateRIPRelativeInstruction(ULONGLONG InOffset, ULONGLONG InTargetOffset, PBOOL OutWasRelocated);
EASYHOOK_NT_INTERNAL LhDisassembleInstruction(PVOID InPtr, PULONG Length, PSTR Buffer, LONG BufferSize, PULONG64 NextInstr);


// EasyHookDll/LocalHook/alloc.c �ڲ�����
PVOID LhAllocateMemoryEx(PVOID InEntryPoint, PULONG OutPageSize);
VOID LhFreeMemory(PLOCAL_HOOK_INFO* HookInfo);

// EasyHookDll/LocalHook/install.c ���غ���
EASYHOOK_NT_INTERNAL   LhAllocateHook(PVOID InEntryPoint, PVOID InHookProc, PVOID InCallBack, PLOCAL_HOOK_INFO* OutLocalHookInfo, PULONG RelocSize);
EASYHOOK_BOOL_INTERNAL LhIsValidHandle(TRACED_HOOK_HANDLE InTracedHandle, PLOCAL_HOOK_INFO* OutHandle);

void LhCriticalInitialize();

// EasyHookDll/LocalHook/Barrier.c �ڲ�����
ULONG64 LhBarrierIntro(LOCAL_HOOK_INFO* InHandle, PVOID InRetAddr, PVOID* InAddrOfRetAddr);

// EasyHookDll/LocalHook/Uninstall.c 
void LhCriticalFinalize();

// EasyHookDll/RemoteHook/thead.c �ǵ�������
EASYHOOK_NT_INTERNAL RtlNtCreateThreadEx(HANDLE ProcessHandle, LPTHREAD_START_ROUTINE ThreadStart, PVOID ThreadParameter, BOOL IsThreadSuspended, HANDLE * ThreadHandle);
EASYHOOK_NT_INTERNAL NtForceLdrInitializeThunk(HANDLE ProcessHandle);
EASYHOOK_NT_INTERNAL RhSetWakeUpThreadID(ULONG32 InThreadID);

#endif