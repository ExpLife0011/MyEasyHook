#include "common.h"

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


// TrampolineASM ���� - ��ʼ������
ULONG64 LhBarrierIntro(LOCAL_HOOK_INFO* InHandle, PVOID InRetAddr, PVOID* InAddrOfRetAddr)
{
    PTHREAD_RUNTIME_INFO ThreadRuntimeInfo = NULL;
    PRUNTIME_INFO        RuntimeInfo = NULL;

#ifdef _M_X64
	InHandle -= 1;
#endif

	if (IsLoaderLock())
	{

	}
}

BOOL IsLoaderLock()
{
#ifndef DRIVER
	BOOL IsLoaderLock = FALSE;
	return ()
#else

#endif
}