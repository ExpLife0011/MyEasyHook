#include "common.h"

// ����һҳ�ڴ���Hook������ڣ����������볤��
PVOID LhAllocateMemoryEx(PVOID InEntryPoint, PULONG OutPageSize)
{
	PUCHAR Result = NULL;
	// user-mode 64λ ��������
#if defined(_M_X64) && !defined(DRIVER)
	LONGLONG		Base = 0;
	LONGLONG		Start = 0;
	LONGLONG		End = 0;
	LONGLONG		Index = 0;
#endif

	// User-mode ����
#if !defined(DRIVER)
	SYSTEM_INFO SystemInfo = { 0 };
	ULONG		PageSize = 0;

	GetSystemInfo(&SystemInfo);
	PageSize = SystemInfo.dwPageSize;
	*OutPageSize = PageSize;
#endif

#if defined(_M_X64) && !defined(DRIVER)
	Start = ((LONGLONG)InEntryPoint) - ((LONGLONG)0x7FFFFF00);	// �����ڴ�ռ伫�� - Ϊ�������ں�����ת��ʱ�� ���������ƫ�� - �����þ���λ��
	End = ((LONGLONG)InEntryPoint) + ((LONGLONG)0x7FFFFF00);	// 64bit user-mode ʹ�� E9��ΪHook��ת Ҳ���� jump ________ 4�ֽڵ�ƫ�� ���ƫ����INT(32bit)
																// ����Ϊ��������� 31bit�ķ�Χ ��������͸���ƫ�� ���λ 0x7FFFFF00

	// �ô�������С���Է��ʵ�ַ - ��ֹ�����/����
	if (Start < (LONGLONG)SystemInfo.lpMinimumApplicationAddress)
	{
		Start = (LONGLONG)SystemInfo.lpMinimumApplicationAddress;
	}
	if (End < (LONGLONG)SystemInfo.lpMaximumApplicationAddress)
	{
		Start = (LONGLONG)SystemInfo.lpMaximumApplicationAddress;
	}

	for (Base = (LONGLONG)InEntryPoint, Index = 0; ; Index += PageSize)
	{
		// ʵ�������ַ - �����ܿ���EntryPoint
		BOOLEAN bEnd = TRUE;
		if (Base + Index < End)
		{
			Result = (PUCHAR)VirtualAlloc((PVOID)(Base + Index), PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (Result != NULL)
			{
				break;
			}
			bEnd = FALSE;
		}
		if (Base - Index > Start)
		{
			Result = (PUCHAR)VirtualAlloc((PVOID)(Base - Index), PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (Result != NULL)
			{
				break;
			}
			bEnd = FALSE;
		}
		if (bEnd)
		{
			break;
		}
	}
	if (Result == NULL)
	{
		return NULL;
	}
#else
	// 32-bits/ driver  �������Ϳ��� E9 ����������
	*OutPageSize = PageSize;
	Result = (PUCHAR)RtlAllocateMemory(TRUE, PageSize);
	if (Result != NULL)
	{
		return NULL;
	}
#endif

	return Result;
}

VOID LhFreeMemory(PLOCAL_HOOK_INFO* HookInfo)
{
#if defined(_M_X64) && !defined(DRIVER)
	VirtualFree(*HookInfo, 0, MEM_RELEASE);
#else
	RtlFreeMemory(*HookInfo);
#endif
	*HookInfo = NULL;

	return;
}