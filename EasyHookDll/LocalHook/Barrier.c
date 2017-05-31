#include "common.h"

#ifndef DRIVER
#include <aux_ulib.h>
#endif

#pragma comment(lib, "Aux_ulib.lib")

typedef struct _RUNTIME_INFO_
{
	BOOL    IsExecuting;   // ��ǰ�߳��Ƿ���ACL��?�Ƿ����ִ��HookProc
	DWORD   HLSIdent;      // ��TLS�е��±�
	PVOID   RetAddress;    //     
	PVOID*  AddrOfRetAddr; // Hook���ش���ķ��ص�ַ ��
}RUNTIME_INFO, *PRUNTIME_INFO;

typedef struct _THREAD_RUNTIME_INFO_
{
	PRUNTIME_INFO Entries;			// ACE	
	PRUNTIME_INFO Current;			// ��ǰ RUNTIME_INFO
	PVOID         CallBack;
	BOOL          IsProtected;
}THREAD_RUNTIME_INFO, *PTHREAD_RUNTIME_INFO;

typedef struct _THREAD_LOCAL_STORAGE_
{
	THREAD_RUNTIME_INFO Entries[MAX_THREAD_COUNT];				// ÿ���̸߳���ӵ��
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

// EasyHookDll/LocalHook/Barrier.c
BOOL IsLoaderLock();
BOOL TlsGetCurrentValue(THREAD_LOCAL_STORAGE* InTls, PTHREAD_RUNTIME_INFO* OutValue);
BOOL TlsAddCurrentThread(THREAD_LOCAL_STORAGE* InTls);
BOOL AcquireSelfProtection();
void ReleaseSelfProtection();
BOOL ACLContains(PHOOK_ACL InACL, ULONG InCheckID);


#ifndef DRIVER
BOOL IsThreadIntercepted(PHOOK_ACL LocalACL, ULONG InThreadId);
#else
BOOL IsProcessIntercepted(PHOOK_ACL LocalACL, ULONG InProcessID);
#endif

ULONG64 _stdcall LhBarrierIntro(LOCAL_HOOK_INFO* InHandle, PVOID InRetAddr, PVOID* InAddrOfRetAddr)
{
	// TrampolineASM ���� - �õ���ǰThreadRuntimeInfo��ע�ᵱǰ�̡߳��жϵ�ǰ�߳��Ƿ���ACL�У������Ƿ�ִ��Hook
    PTHREAD_RUNTIME_INFO ThreadRuntimeInfo = NULL;
    PRUNTIME_INFO        RuntimeInfo = NULL;
	BOOL				 bIsRegister = FALSE;

#ifdef _M_X64
	InHandle -= 1;	     // x64 ������� LocalHookInfo ��β��ַ
#endif

	// ����ϵͳ ������ - ���彲�� https://blogs.msdn.microsoft.com/oldnewthing/20040128-00/?p=40853
	// ��������ϵͳ���еĶ�������ִ���ض��ĺ�����Code��ʱ�����롣���� DllMain GetModuleFileName 
	if (IsLoaderLock())
	{
		// �����ǰ������ϵͳ LoaderLock ��ʱ��ִ�����������ܵ��²���Ԥ�����Ϊ
		return FALSE;
	}

	// ��ǰ�߳��Ƿ��Ѿ�TLS� - �����߳�ID����
	bIsRegister = TlsGetCurrentValue(&BarrierUnit.TLS, &ThreadRuntimeInfo);

	// δע�� - ����ע�ᣬ��Ҫ�ǽ��߳�ID����TLS��,ͬʱ�����Ӧ ThreadRuntimeInfo
	if (!bIsRegister)
	{
		if (!TlsAddCurrentThread(&BarrierUnit.TLS))
			return FALSE;
	}

	/*
		Ϊ���ò���Hook��API�����ܵ��٣�����ʹ�����ұ���
		�⽫�����κ���Hook�κ�API������Щ��Ҫ���ұ�����

		���ұ�����ֹ�κκ�����Hook�жϵ�ǰ�Ĳ����������ǽ��� �߳̽��������谭ǽ
	*/
	if (!AcquireSelfProtection())
	{
		// �������ʧ�� - ֱ��ȥԭ����
		return FALSE;
	}

	ASSERT(InHandle->HLSIndex < MAX_HOOK_COUNT, L"Barrier.c - InHandle->HLSIndex < MAX_HOOK_COUNT");
	
	// ���û��ע�� - ��ʼ��ThreadRuntimeInfo
	if (!bIsRegister)
	{
		TlsGetCurrentValue(&BarrierUnit.TLS, &ThreadRuntimeInfo);

		// ���� RUNTIME_INFO
		ThreadRuntimeInfo->Entries = (PRUNTIME_INFO)RtlAllocateMemory(TRUE, sizeof(RUNTIME_INFO) * MAX_HOOK_COUNT);

		if (ThreadRuntimeInfo->Entries == NULL)
			goto DONT_INTERCEPT;
	}

	// ����HookΨһID �õ� �̶߳�Ӧ������ӵ�RuntimeInfo
	RuntimeInfo = &ThreadRuntimeInfo->Entries[InHandle->HLSIndex];	// HLSIndex - ��ȫ����ע���Indexͬʱ��Ӧ��BarrierUnit���Entrise��Index
	if (RuntimeInfo->HLSIdent != InHandle->HLSIdent)
	{
		// ����������Ϣ
		RuntimeInfo->HLSIdent = InHandle->HLSIdent;
		RuntimeInfo->IsExecuting = FALSE;
	}

	// ��һ�������ﹳ�˶�� �ܾ������������
	if (RuntimeInfo->IsExecuting)
	{
		// �Լ����Լ� - �����߳�����ǽ
		// ���ٵ��� LhBarrierOutro
		goto DONT_INTERCEPT;
	}

	// ��¼�ص���������Ϣ
	ThreadRuntimeInfo->CallBack = InHandle->CallBack;
	ThreadRuntimeInfo->Current = RuntimeInfo;

	// �жϵ�ǰ�߳��Ƿ�����ִ�� HookProc
#ifndef DRIVER
	RuntimeInfo->IsExecuting = IsThreadIntercepted(&InHandle->LocalACL, GetCurrentThreadId());
#else
	RuntimeInfo->IsExecuting = IsProcessIntercepted(&InHandle->LocalACL, (ULONG)PsGetCurrentProcessId());
#endif
	// ACL�ܾ�ִ��
	if (!RuntimeInfo->IsExecuting)
		goto DONT_INTERCEPT;

	// ���淵����Ϣ
	RuntimeInfo->RetAddress = InRetAddr;
	RuntimeInfo->AddrOfRetAddr = InAddrOfRetAddr;

	ReleaseSelfProtection();

	return TRUE;

DONT_INTERCEPT:
	{
		if (ThreadRuntimeInfo != NULL)
		{
			ThreadRuntimeInfo->CallBack = NULL;
			ThreadRuntimeInfo->Current = NULL;

			ReleaseSelfProtection();
		}
		return FALSE;
	}

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

void ReleaseSelfProtection()
{
	PTHREAD_RUNTIME_INFO ThreadRuntimeInfo = NULL;

	ASSERT(TlsGetCurrentValue(&BarrierUnit.TLS, &ThreadRuntimeInfo) && ThreadRuntimeInfo->IsProtected, L"Barrier.c - &BarrierUnit.TLS, &ThreadRuntimeInfo) && ThreadRuntimeInfo->IsProtected");

	ThreadRuntimeInfo->IsProtected = FALSE;
}

// �ж�Ŀ���߳�/���� GlobalACL - LocalACL ��ͬ����
#ifndef DRIVER
BOOL IsThreadIntercepted(PHOOK_ACL LocalACL, ULONG InThreadId)
#else
BOOL IsProcessIntercepted(PHOOK_ACL LocalACL, ULONG InProcessID)
#endif
{
	ULONG CheckID = 0;

#ifndef DRIVER
	if (InThreadId == 0)
		CheckID = GetCurrentThreadId();
	else
		CheckID = InThreadId;
#else
	if (InProcessID == 0)
		CheckID = (ULONG)PsGetCurrentProcessId();
	else
		CheckID = InProcessID;
#endif

	// ȫ��ACL���Ƿ���Ŀ��ID?
	// �ڲ��� GlobalACL ���� ����ִ�еľ����Ǹ���GlobalACL����ȡ��
	if (ACLContains(&BarrierUnit.GlobalACL, CheckID))
	{
		if (ACLContains(LocalACL, CheckID))
		{
			if (LocalACL->IsExclusive)	
				return FALSE;			// �ڵ�ǰACL����Ŀ��ID��ָ���ܾ�ִ��Hook
		}		
		else
		{
			if (!LocalACL->IsExclusive)	// ����LocalACL�У����ҵ�ǰLocalACL��ִ��Hook����ô�ܾ�������LocalACL�е�
				return FALSE;
		}
			
		return !BarrierUnit.GlobalACL.IsExclusive;	// ��GlobalACL��
												    // 1. ��LocalACL�У����ҵ�ǰLocalACLִ��Hook      
											        // 2. ����LocalACL�У����ҵ�ǰLocalACL��ִ��Hook  
		// ��˵�� ִ��Hook,����Ȩ   GlobalACL > LocalACL
		//        ��ִ��Hook,����Ȩ  LocalACL > GlobalACL
	}
	else
	{
		if (ACLContains(LocalACL, CheckID))
		{
			if (LocalACL->IsExclusive)
				return FALSE;
		}			
		else
		{
			if (!LocalACL->IsExclusive)
				return FALSE;
		}
		
		// ����Global���ִ��Ȩ������LocalACL�С�
		// ִ��Ȩ������Global������������ȡ�����ѡ�
		return BarrierUnit.GlobalACL.IsExclusive;
	}
}

/// \��������ACL����CheckId�������棬��֮��
BOOL ACLContains(PHOOK_ACL InACL, ULONG InCheckID)
{
	ULONG Index = 0;

	for (Index = 0; Index < InACL->Count; Index++)
	{
		if (InACL->Entries[Index] == InCheckID)
			return TRUE;
	}

	return FALSE;
}

// ��Dll���ص�ʱ����� - ��ʼ�����н��޽ṹ��
NTSTATUS LhBarrierProcessAttach()
{
	RtlZeroMemory(&BarrierUnit, sizeof(BARRIER_UNIT));

	BarrierUnit.GlobalACL.IsExclusive = TRUE;	// ��ֹ�ж�

	RtlInitializeLock(&BarrierUnit.TLS.ThreadLock);

#ifndef DRIVER
	// AuxUlibInitialize - ��ʼ�� Aux_ulib �� - ������������� Aux_ulib �κκ���ǰ����
	BarrierUnit.IsInitialized = AuxUlibInitialize() ? TRUE : FALSE;
	return STATUS_SUCCESS;
#else
	
#endif
}

void LhBarrierProcessDetach()
{
#ifdef DRIVER

#endif
	RtlDeleteLock(&BarrierUnit.TLS.ThreadLock);
	for (LONG Index = 0; Index < MAX_THREAD_COUNT; Index++)
	{
		if (BarrierUnit.TLS.Entries[Index].Entries != NULL)
			RtlFreeMemory(BarrierUnit.TLS.Entries[Index].Entries);
	}

	RtlZeroMemory(&BarrierUnit, sizeof(BARRIER_UNIT));
}

PVOID _stdcall LhBarrierOutro(PLOCAL_HOOK_INFO InHandle, PVOID* InAddrOfRetAddr)
{
	// Outro ��ʵ��Hook����ִ����ɺ� ִ��
	// �ؼ��⿪ IsExecuting ��
	PRUNTIME_INFO Runtime = NULL;
	PTHREAD_RUNTIME_INFO ThreadRuntimeInfo = NULL;

#ifdef _M_X64
	InHandle -= 1;
#endif

	ASSERT(AcquireSelfProtection(), L"Barrier.c - AcquireSelfProtection()");
	ASSERT(TlsGetCurrentValue(&BarrierUnit.TLS, &ThreadRuntimeInfo) && (ThreadRuntimeInfo != NULL), 
		   L"Barrier.c - TlsGetCurrentValue(&BarrierUnit.TLS, &ThreadRuntimeInfo) && (ThreadRuntimeInfo != NULL)");

	Runtime = &ThreadRuntimeInfo->Entries[InHandle->HLSIndex];

	// ���������
	ThreadRuntimeInfo->Current = NULL;
	ThreadRuntimeInfo->CallBack = NULL;

	ASSERT(Runtime != NULL, L"Barrier.c - Runtime != NULL");

	ASSERT(Runtime->IsExecuting, L"Barrier.c - Runtime->IsExecuting");

	Runtime->IsExecuting = FALSE;

	ASSERT(*InAddrOfRetAddr == NULL, L"Barrier.c - *InAddrOfRetAddr == NULL");

	*InAddrOfRetAddr = Runtime->RetAddress;

	ReleaseSelfProtection();

	return InHandle;
}

PHOOK_ACL LhBarrierGetACL()
{
	return &BarrierUnit.GlobalACL;
}