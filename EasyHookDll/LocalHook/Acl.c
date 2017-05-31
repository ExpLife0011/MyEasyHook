#include "common.h"

// EasyHook - ACL 
/*
	��ÿ��Hook��װ���㻹Ҫ���ö�Ӧ�̵߳�ACL��
	�� Trampoline_ASM ������ LhBarrierIntro ���жϺ����߳��Ƿ���ACL��
	�������Ƿ�ִ��Hook
*/

LONG LhSetACL(PHOOK_ACL InACL, BOOL InIsExclusive, PULONG InThreadIdList, LONG InThreadCount);

EASYHOOK_NT_API LhSetInclusiveACL(PULONG InThreadIdList, ULONG InThreadCount, TRACED_HOOK_HANDLE InHandle)
{
	PLOCAL_HOOK_INFO Handle = NULL;

	// �ж�Hook����Ƿ���Ч - ���������� LOCAL_HOOK_INFO
	if (!LhIsValidHandle(InHandle, &Handle))
		return STATUS_INVALID_PARAMETER_3;

	return LhSetACL(&Handle->LocalACL, FALSE, InThreadIdList, InThreadCount);
}

// �ڲ����� - ��ȫ�ֻ��߱���ACLs�ṩ���뷽��
LONG LhSetACL(PHOOK_ACL InACL, BOOL InIsExclusive, PULONG InThreadIdList, LONG InThreadCount)
{
	// InACL - �����Ҫ����ȫ�� HOOK_ACL,��һ�����봫��
	// InIsExclusive - ������������������̶߳������жϣ��봫 TRUE
	// InThreadIdList - �߳����飬�����Զ���Ϊ�����߳�
	// InThreadCount - �߳�ID������������ܳ��� MAX_ACE_COUNT

	ULONG Index = 0;

	ASSERT(IsValidPointer(InACL, sizeof(HOOK_ACL)), L"ACL.c - IsValidPointer(InACL, sizeof(HOOK_ACL))");

	// �̹߳���
	if (InThreadCount > MAX_ACE_COUNT)
		return STATUS_INVALID_PARAMETER_4;

	// �Ƿ�����
	if (!IsValidPointer(InThreadIdList, InThreadCount * sizeof(ULONG)))
		return STATUS_INVALID_PARAMETER_3;

	// �ձ��� - ��䵱ǰ�߳�ID
	for (Index = 0; Index < InThreadCount; Index++)
	{
		if (InThreadIdList[Index] == 0)
			InThreadIdList[Index] = GetCurrentThreadId();
	}

	// ���� ACL
	InACL->IsExclusive = InIsExclusive;
	InACL->Count = InThreadCount;

	RtlCopyMemory(InACL->Entries, InThreadIdList, InThreadCount * sizeof(ULONG));

	return STATUS_SUCCESS;
}

EASYHOOK_NT_API LhSetGlobalInclusiveACL(PULONG InThreadIdList, ULONG InThreadCount)
{
	return LhSetACL(LhBarrierGetACL(), FALSE, InThreadIdList, InThreadCount);
}