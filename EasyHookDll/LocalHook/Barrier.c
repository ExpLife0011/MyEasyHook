#include "common.h"

#ifndef DRIVER
#include <aux_ulib.h>
#endif

typedef struct _RUNTIME_INFO_
{
    BOOL    IsExecuting;   // TRUE - �����ǰ�߳����������Hook�������
    DWORD   HLSIdent;      // 
    PVOID   RetAddress;    //     
    PVOID*  AddrOfRetAddr; // Hook���ش���ķ��ص�ַ ��
}RUNTIME_INFO, *PRUNTIME_INFO;

typedef struct _THREAD_RUNTIME_INFO_
{
    PRUNTIME_INFO Entries;
    PRUNTIME_INFO Current;
    PVOID         CallBack;
    BOOL          IsProtected;
}THREAD_RUNTIME_INFO, *PTHREAD_RUNTIME_INFO;

typedef struct _THREAD_LOCAL_STORAGE_
{
	THREAD_RUNTIME_INFO Entries[MAX_THREAD_COUNT];
	DWORD				ThreadIdList[MAX_THREAD_COUNT];			// �߳�ID List
	RTL_SPIN_LOCK		ThreadLock;
}THREAD_LOCAL_STORAGE;

typedef struct _BARRIER_UNIT_
{
	HOOK_ACL			 GlobalACL;
	BOOL				 IsInitialized;
	THREAD_LOCAL_STORAGE TLS;
}BARRIER_UNIT, *PBARRIER_UNIT;

static BARRIER_UNIT BarrierUnit;

// TrampolineASM ���� - ��ʼ������
ULONG64 LhBarrierIntro(LOCAL_HOOK_INFO* InHandle, PVOID InRetAddr, PVOID* InAddrOfRetAddr)
{
    PTHREAD_RUNTIME_INFO ThreadRuntimeInfo = NULL;
    PRUNTIME_INFO        RuntimeInfo = NULL;
	BOOL				 bIsRegister = FALSE;

#ifdef _M_X64
	InHandle -= 1;
#endif

	// ����ϵͳ ������ - ���彲�� https://blogs.msdn.microsoft.com/oldnewthing/20040128-00/?p=40853
	// ��������ϵͳ���еĶ�������ִ���ض��ĺ�����Code��ʱ�����롣���� DllMain GetModuleFileName 
	if (IsLoaderLock())
	{
		// �����ǰ������ϵͳ LoaderLock ��ʱ��ִ�����������ܵ��²���Ԥ�����Ϊ
		return FALSE;
	}

	bIsRegister = TlsGetCurrentValue(&BarrierUnit.TLS, &ThreadRuntimeInfo);

	if (!bIsRegister)
	{
		if (!TlsAddCurrentThread(&BarrierUnit.TLS))
			return FALSE;
	}

	/*
		Ϊ���ò���Hook��API�����ܵ��٣�����ʹ�����ұ���
		�⽫�����κ���Hook�κ�API������Щ��Ҫ���ұ�����

		���ұ�����ֹ�κκ�����Hook�жϵ�ǰ�Ĳ����������ǽ��� �߳������谭ǽ
	*/

	if (!AcquireSelfProtection())
	{
		// �������ʧ�� - ������ֱ��ʧ�� ���ٵ��� LhBarrierOutro
		return FALSE;
	}

	ASSERT(InHandle->HLSIndex < MAX_HOOK_COUNT, L"Barrier.c - InHandle->HLSIndex < MAX_HOOK_COUNT");
	 
	if (!bIsRegister)
	{
		TlsGetCurrentValue(&BarrierUnit.TLS, &ThreadRuntimeInfo);

		ThreadRuntimeInfo->Entries = (PRUNTIME_INFO)RtlAllocateMemory(TRUE, sizeof(RUNTIME_INFO) * MAX_HOOK_COUNT);

		if (ThreadRuntimeInfo->Entries == NULL)
			goto DONT_INTERCEPT;
	}

	// �õ�Hook������Ϣ
	RuntimeInfo = &ThreadRuntimeInfo->Entries[InHandle->HLSIndex];
	if (RuntimeInfo->HLSIdent != InHandle->HLSIdent)
	{
		// ����������Ϣ
		RuntimeInfo->HLSIdent = InHandle->HLSIdent;
		RuntimeInfo->IsExecuting = FALSE;
	}

	// ����ѭ�����Լ�
	if (RuntimeInfo->IsExecuting)
	{
		// �Լ����Լ� - �����߳�����ǽ
		// ���ٵ��� LhBarrierOutro
		goto DONT_INTERCEPT;
	}

	ThreadRuntimeInfo->CallBack = InHandle->CallBack;
	ThreadRuntimeInfo->Current = RuntimeInfo;

	// ?
#ifndef DRIVER
	RuntimeInfo->IsExecuting = IsThreadIntercepted(&InHandle->LocalACL, GetCurrentThreadId());
#else

#endif



DONT_INTERCEPT:

}

// �жϳ�ʼ�������Ƿ���԰�ȫ����
BOOL IsLoaderLock()
{
	// ֻ�к������� FALSEʱ���ſɰ�ȫִ��
#ifndef DRIVER
	BOOL IsLoaderLock = FALSE;
	// AuxUlibIsDLLSynchronizationHeld - �жϵ�ǰ�߳��Ƿ��ڵȴ�һ��ͬ���¼� - ͬʱҪ��δ��ʼ��ʼ��
	return (!AuxUlibIsDLLSynchronizationHeld(&IsLoaderLock) || IsLoaderLock || !BarrierUnit.IsInitialized);
#else
	return FALSE;
#endif
}

// ��ȫ�� Tls�б��� ��ѯ��ǰ�̵߳� THREAD_RUNTIME_INFO
// Ҫ��ǰ�̱߳����Ѿ��� Tls ��ע��������ҵ����� TlsAddCurrentThread() ȥ��Ӵ洢
BOOL TlsGetCurrentValue(THREAD_LOCAL_STORAGE* InTls, PTHREAD_RUNTIME_INFO* OutValue)
{
#ifndef DRIVER
	ULONG		CurrentId = (ULONG)GetCurrentThreadId();
#else
	ULONG		CurrentId = (ULONG)PsGetCurrentThread();
#endif
	LONG Index = 0;
	for (Index = 0; Index < MAX_THREAD_COUNT; Index++)
	{
		if (InTls->ThreadIdList[Index] == CurrentId)
		{
			*OutValue = &InTls->Entries[Index];

			return TRUE;
		}
	}

	return FALSE;
}

BOOL TlsAddCurrentThread(THREAD_LOCAL_STORAGE* InTls)
{
#ifndef DRIVER
	ULONG		CurrentId = (ULONG)GetCurrentThreadId();
#else
	ULONG		CurrentId = (ULONG)PsGetCurrentThreadId();
#endif
	LONG		Index = -1;

	RtlAcquireLock(&InTls->ThreadLock);	// �����ٽ���

	for (LONG i = 0; i < MAX_THREAD_COUNT; i++)
	{
		// ����� ThreadIdList �е�һ��û�з�ֵ�Ľڵ�
		if ((InTls->ThreadIdList[i] == 0) && Index == -1)
			Index = i;

		// ����߳�ID�Ѿ�ע�� ��������
		ASSERT(InTls->ThreadIdList[i] != CurrentId, L"Barrier.c - InTls->ThreadIdList[i] != CurrentId");
	}

	if (Index == -1)
	{
		// ������ - ʧ��
		RtlReleaseLock(&InTls->ThreadLock);

		return FALSE;
	}

	// ע���߳�ID
	InTls->ThreadIdList[Index] = CurrentId;
	RtlZeroMemory(&InTls->Entries[Index], sizeof(THREAD_RUNTIME_INFO));	// ��ʼ���߳�������Ϣ
	RtlReleaseLock(&InTls->ThreadLock);	// �뿪�ٽ���

	return TRUE;
}

BOOL AcquireSelfProtection()
{
	PTHREAD_RUNTIME_INFO	Runtime = NULL;

	if (!TlsGetCurrentValue(&BarrierUnit.TLS, &Runtime) || Runtime->IsProtected)
		return FALSE;

	Runtime->IsProtected = TRUE;

	return TRUE;
}