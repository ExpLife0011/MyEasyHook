#include <common.h>


void LhCriticalFinalize()
{
	/*
	Description:

	Will be called in the DLL_PROCESS_DETACH event and just uninstalls
	all hooks. If it is possible also their memory is released.
	*/
	//LhUninstallAllHooks();

	//LhWaitForPendingRemovals();

	RtlDeleteLock(&GlobalHookLock);
}

EASYHOOK_NT_API LhuninstallHook(TRACED_HOOK_HANDLE InHandle)
{
	// �Ƴ�Hook, �����Ҫ�ͷŹ�������Դ������ LhWaitForPendingRemovals()��
	PLOCAL_HOOK_INFO LocalHookInfo = NULL;
	PLOCAL_HOOK_INFO HookList = NULL;
	PLOCAL_HOOK_INFO ListPrev = NULL;
	BOOLEAN			 IsAllocated = FALSE;
	NTSTATUS         NtStatus = STATUS_UNSUCCESSFUL;

	if (!IsValidPointer(InHandle, sizeof(TRACED_HOOK_HANDLE)))
		return FALSE;

	RtlAcquireLock(&GlobalHookLock);
	{
		if ((InHandle->Link != NULL) && LhIsValidHandle(InHandle, &LocalHookInfo))
		{
			// ���ָ�� - �����ͷ���Դ
			InHandle->Link = NULL;

			if (LocalHookInfo->HookProc != NULL)
			{
				LocalHookInfo->HookProc = NULL;
				IsAllocated = TRUE;
			}

			if (!IsAllocated)
			{
				RtlReleaseLock(&GlobalHookLock);

				RETURN;
			}

			// ��ȫ��Hook�������Ƴ�
			HookList = GlobalHookListHead.Next;
			ListPrev = &GlobalHookListHead;

			while (HookList != NULL)
			{
				if (HookList == LocalHookInfo)
				{
					ListPrev->Next = LocalHookInfo->Next;
					break;
				}

				HookList = HookList->Next;
			}

			// ��ӵ��Ƴ�����
			LocalHookInfo->Next = GlobalRemovalListHead.Next;
			GlobalRemovalListHead.Next = LocalHookInfo;
		}

		RtlReleaseLock(&GlobalHookLock);
		RETURN;
	}
	//THROW_OUTRO:
FINALLY_OUTRO:
	return NtStatus;
}

EASYHOOK_NT_API LhWaitForPendingRemovals()
{
	// Ϊ���ȶ��Կ��ǣ����е���Դ��������û����ʹ�õ�������ͷš�
	// ���ͷ���Դ��ж�ع��ӷֿ���������������ж�ع��ӣ�Ȼ�����������ͷ���Դ��
	PLOCAL_HOOK_INFO	LocalHookInfo = NULL;
	NTSTATUS			NtStatus = STATUS_SUCCESS;
	INT32				TimeOut = 1000;


#ifdef X64_DRIVER
	KIRQL	CurrentIRQL = PASSIVE_LEVEL;
#endif

	while (TRUE)
	{
		// ȡ��һ��Hook
		RtlAcquireLock(&GlobalHookLock);
		{
			LocalHookInfo = GlobalRemovalListHead.Next;
			if (LocalHookInfo == NULL)
			{
				RtlReleaseLock(&GlobalHookLock);
				break;
			}

			GlobalRemovalListHead.Next = LocalHookInfo->Next;
		}
		RtlReleaseLock(&GlobalHookLock);

		// Hook��ڻ�����Hook�ú��������?
		if (LocalHookInfo->HookSave == *((PULONG64)LocalHookInfo->TargetProc))
		{
#ifdef X64_DRIVER
			CurrentIRQL = KeGetCurrentIrql();
			RtlWPOFF()
#endif
			*((PULONG64)LocalHookInfo->TargetProc) = LocalHookInfo->TargetBackup;	// �ָ�ԭ��
#ifdef X64_DRIVER
			*((PULONG64)(LocalHookInfo->TargetProc + 8)) = LocalHookInfo->TargetBackup_x64;
			RtlWPON();
#endif
			while (TRUE)
			{
				if (*LocalHookInfo->IsExecutedPtr <= 0)	// ����ʹ��
				{
					// �ͷ� Slot
					if (GlobalSlotList[LocalHookInfo->HLSIndex] == LocalHookInfo->HLSIdent)
						GlobalSlotList[LocalHookInfo->HLSIndex] = 0;

					LhFreeMemory(&LocalHookInfo);
					break;
				}

				if (TimeOut < 0)
				{
					NtStatus = STATUS_TIMEOUT;
					break;
				}

				RtlSleep(25);
				TimeOut -= 25;
			}
		}
		else
		{

		}
	}
	
	return NtStatus;
}