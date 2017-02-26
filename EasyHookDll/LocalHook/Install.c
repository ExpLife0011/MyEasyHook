#include "common.h"

// ��ǰ�ļ�ʹ�ú�����������
PVOID GetTrampolinePtr();
LONG GetTrampolineSize();

EASYHOOK_NT_API  LnInstallHook(PVOID InEntryPoint, PVOID InHookProc, PVOID InCallBack, TRACED_HOOK_HANDLE OutTracedHookHandle)
{
	NTSTATUS NtStatus = STATUS_SUCCESS;
	PLOCAL_HOOK_INFO  LocalHookInfo = NULL;
	ULONG	RelocSize = 0;


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

	// ���빳�� ׼��hook ���
	FORCE(LhAllocateHook(InEntryPoint, InHookProc, InCallBack, &LocalHookInfo, &RelocSize));

THROW_OUTRO:
	{
		return NtStatus;
	}
}

// ������������ת�ĵ�ַ���� 
// һ����8�ֽڣ�������64λ��������16�ֽ�
#define MAX_JMP_SIZE 16

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
	MemoryPtr = (PUCHAR)(LocalHookInfo + 1);	// LOCAL_HOOK_INFO
												//                �� MemoryPtr

#ifdef X64_DRIVER
	FORCE(EntrySize = LhRoundToNextInstruction(InEntryPoint, X64_DRIVER_JMPSIZE));
#else
	// HookProc �õ���һ��βƫ�ƴ���5��ָ���βƫ�� == ��һ��ƫ�ƴ���5��ָ���ƫ��-1
	FORCE(LhRoundToNextInstruction(InEntryPoint, 5, &EntrySize));
#endif

	LocalHookInfo->Size = sizeof(LOCAL_HOOK_INFO);
#if !_M_X64
	// 32λ�Գ�ʼ���ضϾ���Ĺر� - Ϊ����64λ����һ�ݴ���
	__pragma(warning(push))
	__pragma(warning(disbale:4305))
#endif
	LocalHookInfo->RandomValue = (PVOID)0x69FAB7309CB312EF;
#if !_M_X64
	__pragma(warning(pop))
#endif
	// �ṹ�帳ֵ - δ����
	LocalHookInfo->HookProc = InHookProc;
	LocalHookInfo->TargetProc = InEntryPoint;
	LocalHookInfo->EntrySize = EntrySize;
	LocalHookInfo->CallBack = InCallBack;
	//LocalHookInfo->IsExecutedPtr

	/*
	���彫��������������������û������hook����������ǰ��
	���ǽ�����һ����ȷ�Ļ����� fiber deadlock barrier �� ָ���Ļص�����
	*/
	// δʵ�ֺ���
	//LocalHookInfo->HookIntro = LhBarrierIntro;
	//LocalHookInfo->HookOutro = LhBarrierOutro;

	// ������תָ��
	LocalHookInfo->Trampoline = MemoryPtr; // MemoryPtr ��Խ��LocalHookInfo Ҳ���ǵ�ǰ�ṹ���β��
	MemoryPtr += GetTrampolineSize();	   // LOCAL_HOOK_INFO | TrampolineASM | 
										   //							      �� MemoryPtr

	LocalHookInfo->Size += GetTrampolineSize();

	// ���� Trampoline asm������
	RtlCopyMemory(LocalHookInfo->Trampoline, GetTrampolinePtr(), GetTrampolineSize());
	/*
		����������ڴ��볤�ȣ���Ϊ��Щ������뱻ֱ��д�뵽������ڴ�ռ��С�
		��Ϊ����Ҫ�ٳ�EIP/RIP�ͱ���֪������Ҫȥ��

		��ں������뽫�ᱻ���� Trampoline ���档
	*/
	*RelocSize = 0;
	LocalHookInfo->OldProc = MemoryPtr;

	FORCE(LhRelocateEntryPoint(LocalHookInfo->TargetProc, EntrySize, LocalHookInfo->OldProc, RelocSize));
	// ȷ���ռ仹���㹻
	MemoryPtr += (*RelocSize + MAX_JMP_SIZE);
	LocalHookInfo->Size += (*RelocSize + MAX_JMP_SIZE);

	// �����ת���뵽�µ���ڴ������
#ifdef X64_DRIVER


#else
	// TargetProc + EntrySize : Ŀ�꺯����ַ+���ָ���ַ
	// OldProc(�����ڷ�ԭ�����ĵ�ַ) + *RelocSize(����ָ����б仯���ָ���) + 5 : 
	RelAddr = (LONG64)((PUCHAR)LocalHookInfo->TargetProc + LocalHookInfo->EntrySize) - ((LONG64)LocalHookInfo->OldProc + *RelocSize + 5);

	// ƫ����û�в��32λ
	if (RelAddr != (LONG)RelAddr)
	{
		THROW(STATUS_NOT_SUPPORTED, L"The Given Entry Point Is Out Of Reach.");
	}

	((PUCHAR)LocalHookInfo->OldProc)[*RelocSize] = 0xE9;

	RtlCopyMemory((PUCHAR)LocalHookInfo->OldProc + *RelocSize + 1, &RelAddr, 4);

#endif

	LocalHookInfo->TargetBackup = *((PULONG64)LocalHookInfo->TargetProc);

#ifdef X64_DRIVER
	
#endif

#ifndef _M_X64



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