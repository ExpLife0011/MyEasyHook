#include "common.h"

// ��ǰ�ļ�ʹ�ú�����������
PVOID GetTrampolinePtr();
LONG GetTrampolineSize();

// ȫ��Hook����
LOCAL_HOOK_INFO GlobalHookListHead;
// ȫ���Ѿ��Ƴ�Hook����
LOCAL_HOOK_INFO GlobalRemovalListHead;
RTL_SPIN_LOCK GlobalHookLock;
ULONG		  GlobalSlotList[MAX_HOOK_COUNT] = { 0 };

static ULONG HLSCounter = 0x10000000;


void LhCriticalInitialize()
{
	RtlZeroMemory(&GlobalHookListHead, sizeof(GlobalHookListHead));
	RtlZeroMemory(&GlobalRemovalListHead, sizeof(GlobalRemovalListHead));

	RtlInitializeLock(&GlobalHookLock);
}

// ������������ת�ĵ�ַ���� 
// һ����8�ֽڣ�������64λ��������16�ֽ�
#define MAX_JMP_SIZE 16

EASYHOOK_NT_API LhInstallHook(PVOID InEntryPoint, PVOID InHookProc, PVOID InCallBack, TRACED_HOOK_HANDLE OutTracedHookHandle)
{
	BOOL     Exists = FALSE;
	ULONG	 RelocSize = 0;
	LONG64	 RelOffset = 0;
	NTSTATUS NtStatus = STATUS_SUCCESS;
	PLOCAL_HOOK_INFO  LocalHookInfo = NULL;
	// ��ת���Ӳ���� 
	UCHAR    Jumper[MAX_JMP_SIZE] = { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	ULONG64  EntrySave = 0;

	// �������
	if (!IsValidPointer(InEntryPoint, 1))
	{
		THROW(STATUS_INVALID_PARAMETER_1, L"Invalid EntryPoint.");
	}
	if (!IsValidPointer(InHookProc, 1))
	{
		THROW(STATUS_INVALID_PARAMETER_2, L"Invalid HooKProc.");
	}
	if (!IsValidPointer(OutTracedHookHandle, sizeof(HOOK_TRACE_INFO)))
	{
		THROW(STATUS_INVALID_PARAMETER_4, L"The Hook Trace Handle Is Expected To Be Allocated By The Called.");
	}
	if (OutTracedHookHandle->Link != NULL)
	{
		THROW(STATUS_INVALID_PARAMETER_4, L"The Given Hook Trace Handle Seems To Already By Associated With A Hook.");
	}

	// ���빳�� ׼����ת���� - Hook������� - ���ĺ���
	FORCE(LhAllocateHook(InEntryPoint, InHookProc, InCallBack, &LocalHookInfo, &RelocSize));

#ifdef X64_DRIVER


#else
	// ����ʵ����תƫ�ƾ���
	RelOffset = (LONG64)LocalHookInfo->Trampoline - ((LONG64)LocalHookInfo->TargetProc + 5);	// TargetProc ���� E9 + Offset(4�ֽ�) - ������ת����ʼ��ַ�� TargetProc + 5
	if (RelOffset != (LONG)RelOffset)	// ���ʵ��ƫ�Ƴ�����31λ
		THROW(STATUS_NOT_SUPPORTED , L"The Given Entry Point Is Out Of Reach.");

	// ����ƫ��
	RtlCopyMemory(Jumper + 1, &RelOffset, 4);
	// �޸����ҳ�汣������
	FORCE(RtlProtectMemory(LocalHookInfo->TargetProc, LocalHookInfo->EntrySize, PAGE_EXECUTE_READWRITE));
#endif

	// ��¼��Ϣ
	RtlAcquireLock(&GlobalHookLock);
	{
		LocalHookInfo->HLSIdent = HLSCounter++;
		Exists = FALSE;

		// �� GlobalSlotList �м�¼��ǰ HLS - ������LocalHookInfo�����ö�Ӧ��Index
		for (LONG Index = 0; Index < MAX_HOOK_COUNT; Index++)
		{
			if (GlobalSlotList[Index] == 0)
			{
				GlobalSlotList[Index] = LocalHookInfo->HLSIdent;
				LocalHookInfo->HLSIndex = Index;
				Exists = TRUE;

				break;
			}
		}
	}
	RtlReleaseLock(&GlobalHookLock);
	// ���ע��ʧ��
	if (!Exists)
		THROW(STATUS_INSUFFICIENT_RESOURCES, L"Not more than MAX_HOOK_COUNT hooks are supported simultaneously.");

#ifdef X64_DRIVER

#else

	// ����ԭ������ڴ��� - 5�ֽ� ������Hook
	EntrySave = *((ULONG64*)LocalHookInfo->TargetProc);
	{
		RtlCopyMemory(&EntrySave, Jumper, 5);

		// ����ԭ����
		LocalHookInfo->HookOldSave = EntrySave;
	}
	*((ULONG64*)LocalHookInfo->TargetProc) = EntrySave;

#endif

	// ��� ��ǰHook��Ϣ��ȫ��Hook����ͷ��ؾ��
	RtlAcquireLock(&GlobalHookLock);
	{
		LocalHookInfo->Next = GlobalHookListHead.Next;
		GlobalHookListHead.Next = LocalHookInfo;
	}
	RtlReleaseLock(&GlobalHookLock);

	LocalHookInfo->Signature = LOCAL_HOOK_SIGNATURE;
	LocalHookInfo->Tracking = OutTracedHookHandle;	// ��¼
	OutTracedHookHandle->Link = LocalHookInfo;

	RETURN(STATUS_SUCCESS);

THROW_OUTRO:
FINALLY_OUTRO:
	{
		if (!RTL_SUCCESS(NtStatus))
		{
			if (LocalHookInfo != NULL)
				LhFreeMemory(&LocalHookInfo);
		}
		return NtStatus;
	}
}


EASYHOOK_NT_INTERNAL LhAllocateHook(PVOID InEntryPoint, PVOID InHookProc, PVOID InCallBack, PLOCAL_HOOK_INFO* OutLocalHookInfo, PULONG RelocSize)
{
	NTSTATUS NtStatus = STATUS_SUCCESS;
	ULONG	 PageSize = 0;
	ULONG	 EntrySize = 0;		// ���ָ���
	PUCHAR   MemoryPtr = NULL;
	LONG64   RelAddr = 0;
	PLOCAL_HOOK_INFO LocalHookInfo = NULL;
	

#ifdef X64_DRIVER

#endif

#ifndef _M_X64
	LONG	Index = 0;
	PUCHAR  Ptr = NULL;
#endif

	
	// �������
	if (!IsValidPointer(InEntryPoint, 1))
	{
		THROW(STATUS_INVALID_PARAMETER_1, L"Invalid EntryPoint.");
	}
	if (!IsValidPointer(InHookProc, 1))
	{
		THROW(STATUS_INVALID_PARAMETER_2, L"Invalid HooKProc.");
	}

	// �����ڴ�ռ� - AllocateEx ��������ڲ���ѧ�ʣ�������ĵ�ַ�������� 32bitƫ��֮��
	*OutLocalHookInfo = (PLOCAL_HOOK_INFO)LhAllocateMemoryEx(InEntryPoint, &PageSize);
	if (*OutLocalHookInfo == NULL)
	{
		THROW(STATUS_NO_MEMORY, L"Failed To Allocate Memory.");
	}
	LocalHookInfo = *OutLocalHookInfo;

	// �޸�ҳ������
	FORCE(RtlProtectMemory(LocalHookInfo, PageSize, PAGE_EXECUTE_READWRITE));
	// ��MemoryPtr���õ� LOCAL_HOOK_INFO ��β�� ���ǽ���ת��ShellCode��ԭCode����������ط�
	// Ҳ��˵Hook��� Func ������: LOCAL_HOOK_INFO + ShellCode + OldProc
	MemoryPtr = (PUCHAR)(LocalHookInfo + 1);	// LOCAL_HOOK_INFO |
												//                �� MemoryPtr

#ifdef X64_DRIVER
	FORCE(EntrySize = LhRoundToNextInstruction(InEntryPoint, X64_DRIVER_JMPSIZE));
#else
	// HookProc �õ���һ��βƫ�ƴ���5��ָ���βƫ�� == ��һ��ƫ�ƴ���5��ָ���ƫ��
	// һ��WINAPI����stdcall - ������ڶ��ǵڶ�����תָ�����Ҳ����5
	FORCE(LhRoundToNextInstruction(InEntryPoint, 5, &EntrySize));
#endif

	// ��ʼ�ṹ�帳ֵ
	LocalHookInfo->Size = sizeof(LOCAL_HOOK_INFO);
#if !_M_X64
	// 32λ�Գ�ʼ���ضϾ���Ĺر� - Ϊ����64λ����һ�ݴ���
	__pragma(warning(push))
	__pragma(warning(disable:4305))
#endif
	LocalHookInfo->RandomValue = (PVOID)0x69FAB7309CB312EF;
#if !_M_X64
	__pragma(warning(pop))
#endif
	// �ṹ�帳ֵ
	LocalHookInfo->HookProc = InHookProc;
	LocalHookInfo->TargetProc = InEntryPoint;
	LocalHookInfo->EntrySize = EntrySize;
	LocalHookInfo->CallBack = InCallBack;
	LocalHookInfo->IsExecutedPtr = (PINT)((PUCHAR)LocalHookInfo + 2048);		// ������ҳ��(0x1000)ƫ��Ϊ2048�ĵط�
	*LocalHookInfo->IsExecutedPtr = 0;

	/*
	���彫��������������������û������hook����������ǰ��
	����Intro�ж�ACL - �����Ƿ�ִ��Hook����
	*/
	// δʵ�ֺ���
	LocalHookInfo->HookIntro = LhBarrierIntro;
	LocalHookInfo->HookOutro = LhBarrierOutro;

	// ������תָ��
	LocalHookInfo->Trampoline = MemoryPtr; // MemoryPtr ��Խ�� LocalHookInfo Ҳ���ǵ�ǰ�ṹ���β��
	MemoryPtr += GetTrampolineSize();	   // LOCAL_HOOK_INFO | TrampolineASM | 
										   //							      �� MemoryPtr

	LocalHookInfo->Size += GetTrampolineSize();	// ���ȸ���

	// ���� Trampoline asm������ - ע����x64λ�� ǰ���fixedֵ���ᱻ������ȥ��asm��ʹ�����ּ��������ٷ��ʽṹ��������ֵ��
	RtlCopyMemory(LocalHookInfo->Trampoline, GetTrampolinePtr(), GetTrampolineSize());
	/*
		����������ڴ��볤�ȣ���Ϊ��Щ������뱻ֱ��д�뵽������ڴ�ռ��С�
		֮����Ҫ�ع���ڴ��� - ��Ϊ�˵����ǵ�Hook��ִ�е�ʱ������Ҫֱ��ȥ����ԭ����������Ҫ��ԭ����ڵ���ת�����EIP��ش�������ع�
		�ع�����ڴ��뽫������ Trampoline ֮��
	*/
	// ��ʼ�ع�ԭ����ڴ���
	*RelocSize = 0;
	LocalHookInfo->OldProc = MemoryPtr;

	FORCE(LhRelocateEntryPoint(LocalHookInfo->TargetProc, EntrySize, LocalHookInfo->OldProc, RelocSize));
	// ȷ���ռ仹���㹻 - RelocCode֮��Ҫ������ָ��
	// ��Ϊ�����ں�������һ����תָ�ֻ��һ�������Ĳ���ָ���ô������ִ����ɺ�Ӧ������ԭ��������һ���ַ����������ִ�С�
	MemoryPtr += (*RelocSize + MAX_JMP_SIZE);		// LOCAL_HOOK_INFO | TrampolineASM |  Old Proc |
													//							       | EntrySize |�� MemoryPtr
	LocalHookInfo->Size += (*RelocSize + MAX_JMP_SIZE);	// �����㹻�ռ�

	// �����ת���뵽�µ���ڴ������
#ifdef X64_DRIVER


#else
	// ����ƫ�� - ����ڴ��� ��ת�� ԭ��������ڴ��������ع�����
	// TargetProc + EntrySize : Ŀ�꺯����ַ+��ڴ��볤��(Ҳ�������Ƿ������ĳ���)													
	// OldProc(���ڷ�ԭ�����ĵ�ַ) + *RelocSize(��ԭ��ڴ�����б仯���ָ���) + 5(��ǰ�����תָ��ĳ���) : 
	RelAddr = (LONG64)((PUCHAR)LocalHookInfo->TargetProc + LocalHookInfo->EntrySize) - ((LONG64)LocalHookInfo->OldProc + *RelocSize + 5);

	// ƫ����û�в��32λ
	if (RelAddr != (LONG)RelAddr)
	{
		THROW(STATUS_NOT_SUPPORTED, L"The Given Entry Point Is Out Of Reach.");
	}

	// д����תָ�� 
	((PUCHAR)LocalHookInfo->OldProc)[*RelocSize] = 0xE9;

	RtlCopyMemory((PUCHAR)LocalHookInfo->OldProc + *RelocSize + 1, &RelAddr, 4);

#endif

	// ����һ�� ��Hook������ڵ�8�ֽ�
	LocalHookInfo->TargetBackup = *((PULONG64)LocalHookInfo->TargetProc);

#ifdef X64_DRIVER
	
#endif

#ifndef _M_X64
	// 32bit-asm ��Ҫ����д��ʵ�ʲ����ĵ�ַ
	// �滻ASM��ԭ����ռλ�� - ���ɶ�Ӧ����ĵ�ֵַ
	Ptr = LocalHookInfo->Trampoline;

	for (Index = 0; Index < GetTrampolineSize(); Index++)
	{
#pragma warning(disable:4311)	// �رսضϾ���
		switch (*((PULONG32)Ptr))
		{
			case 0x1A2B3C05:	// LocalHookInfo
			{
				*((PULONG32)Ptr) = (ULONG32)LocalHookInfo;
				break;
			}
			case 0x1A2B3C03:	// NETEntry
			{
				*((ULONG*)Ptr) = (ULONG)LocalHookInfo->HookIntro;
				break;
			}
			case 0x1A2B3C01:	// OldProc
			{
				*((PULONG32)Ptr) = (ULONG32)LocalHookInfo->OldProc;
				break;
			}
			case 0x1A2B3C07:	// HookProc(Ptr)
			{
				*((PULONG32)Ptr) = (ULONG)&LocalHookInfo->HookProc;
				break;
			}
			case 0x1A2B3C00:	// HookProc
			{
				*((PULONG32)Ptr) = (ULONG)LocalHookInfo->HookProc;
				break;
			}
			case 0x1A2B3C06:	// UnmanagedOutro
			{
				*((PULONG32)Ptr) = (ULONG)LocalHookInfo->HookOutro;
				break;
			}
			case 0x1A2B3C02:	// IsExecuted
			{
				*((PULONG32)Ptr) = (ULONG)LocalHookInfo->IsExecutedPtr;
				break;
			}
			case 0x1A2B3C04:	// RetAddr
			{
				*((PULONG32)Ptr) = (ULONG)((ULONG_PTR)LocalHookInfo->Trampoline + 92);
				break;
			}
		}
		Ptr++;
	}

#endif

	RETURN;

THROW_OUTRO:
FINALLY_OUTRO:
	{
		if (!RTL_SUCCESS(NtStatus))
		{
			if (LocalHookInfo != NULL)
			{
				LhFreeMemory(&LocalHookInfo);
			}
		}
		return NtStatus;
	}
}

// ASM������غ���
ULONG TrampolineSize = 0;
#ifdef _M_X64
	EXTERN_C VOID __stdcall Trampoline_ASM_x64();
#else
	EXTERN_C VOID __stdcall Trampoline_ASM_x86();
#endif

PVOID GetTrampolinePtr()
{
#ifdef _M_X64
	PUCHAR Ptr = (PUCHAR)Trampoline_ASM_x64;
#else
	PUCHAR Ptr = (PUCHAR)Trampoline_ASM_x86;
#endif

	if (*Ptr == 0xE9)
	{
		Ptr += *((INT*)(Ptr + 1)) + 5;
	}

#ifdef _M_X64
	return Ptr + 5 * 8; // 5������
#else
	return Ptr;
#endif
}

LONG GetTrampolineSize()
{
	PUCHAR Ptr = GetTrampolinePtr();
	PUCHAR BasePtr = Ptr;
	ULONG Index = 0;
	ULONG Signature = 0;

	if (TrampolineSize != 0)
	{
		return TrampolineSize;
	}

	for (Index = 0; Index < 1000; Index++)
	{
		Signature = *((PULONG)Ptr);

		if (Signature == 0x12345678)
		{
			TrampolineSize = (ULONG32)((ULONG_PTR)Ptr - (ULONG_PTR)BasePtr);
			
			return TrampolineSize;
		}
		Ptr++;
	}
	ASSERT(FALSE, L"install.c - ULONG GetTrampolineSize()");

	return 0;
}

// �жϴ���ľ���Ƿ����
EASYHOOK_BOOL_INTERNAL LhIsValidHandle(TRACED_HOOK_HANDLE InTracedHandle, PLOCAL_HOOK_INFO* OutHandle)
{
	// �жϱ�׼ - �ṹ��ָ��ָ����Ч��ַ����־λ��Ч���Ͼ��Ѿ���װHook
	if (!IsValidPointer(InTracedHandle, sizeof(HOOK_TRACE_INFO)))
		return FALSE;

	// LOCAL_HOOK_INFO ������?
	if (!IsValidPointer(InTracedHandle->Link, sizeof(LOCAL_HOOK_INFO)))
		return FALSE;

	if (InTracedHandle->Link->Signature != LOCAL_HOOK_SIGNATURE)
		return FALSE;

	// �����ShellCode������?
	if (!IsValidPointer(InTracedHandle->Link, InTracedHandle->Link->Size))
		return FALSE;

	// Hook����?
	if (InTracedHandle->Link->HookProc == NULL)
		return FALSE;

	if (OutHandle != NULL)
		*OutHandle = InTracedHandle->Link;

	return TRUE;
}