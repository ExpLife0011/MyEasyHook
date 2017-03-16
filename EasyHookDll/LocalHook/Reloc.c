#include "common.h"
#include "udis86\udis86.h"

/*
http://udis86.sourceforge.net/
Udis86 is a disassembler for the x86 and x86-64 class of instruction set
architectures. It consists of a C library called libudis86 which
provides a clean and simple interface to decode a stream of raw binary
data, and to inspect the disassembled instructions in a structured
manner.
*/

// �õ������ַ��һ�����ָ��ĳ���
EASYHOOK_NT_INTERNAL LhGetInstructionLength(PVOID InPtr, PULONG OutLength)
{
	ud_t ud_obj = { 0 };
	ULONG Length = -1;
	if (!IsValidPointer(OutLength, sizeof(ULONG32)))
	{
		return STATUS_INVALID_PARAMETER_2;
	}

	ud_init(&ud_obj);
#ifdef _M_X64
	ud_set_mode(&ud_obj, 64);
#else
	ud_set_mode(&ud_obj, 32);
#endif

	ud_set_input_buffer(&ud_obj, (uint8_t*)InPtr, 32);
	Length = ud_disassemble(&ud_obj);
	if (Length > 0)
	{
		*OutLength = Length;
		return STATUS_SUCCESS;;
	}

	return STATUS_INVALID_PARAMETER;
}

// ������InCodePtr��InCodeSize��Χ�⣬��һ��ָ���βƫ��
EASYHOOK_NT_INTERNAL LhRoundToNextInstruction(PVOID InCodePtr, ULONG InCodeSize, PULONG OutOffset)
{
	PUCHAR Ptr = (PUCHAR)InCodePtr;
	PUCHAR BasePtr = Ptr;
	NTSTATUS NtStatus = STATUS_SUCCESS;
	ULONG InstructionLength = 0;

	if (!IsValidPointer(OutOffset, sizeof(ULONG32)))
	{
		return STATUS_INVALID_PARAMETER_3;
	}

	// ���� InCodeSize �˳�ѭ��
	while (Ptr < BasePtr + InCodeSize)
	{
		// �õ���ǰPtr�µĵ�һ��ָ���
		FORCE(LhGetInstructionLength(Ptr, &InstructionLength));
		Ptr += InstructionLength;	//  ����
		InstructionLength = 0;
	}

	*OutOffset = (ULONG)(Ptr - BasePtr); // ��ȷ�˳�ѭ������ǰ����ָ���βƫ�Ƴ����� InCodeSize
	RETURN;

FINALLY_OUTRO:
THROW_OUTRO:
{
	return NtStatus;
}
}


EASYHOOK_NT_INTERNAL LhRelocateEntryPoint(PVOID InEntryPoint, ULONG InEPSize, PVOID Buffer, PULONG OutRelocSize)
{
	// ��InEntryPoint���·���Buffer������Ǩ�Ƶĳ��ȷ���RelocSize
	PUCHAR   OldAddr = InEntryPoint;
	PUCHAR   NewAddr = Buffer;
	UCHAR    FirstCode = 0;
	UCHAR    SecondCode = 0;
	BOOL     b16bit = FALSE;
	BOOL     bIsRIPRelatieve = FALSE;
	NTSTATUS NtStatus = STATUS_SUCCESS;
	ULONG    OpCodeLength = 0;		// ��תָ����������
	ULONG    InstrLength = 0;
	LONG_PTR AbsAddr = 0;			// �������Ե�ַ


	ASSERT(InEPSize < 20, L"reloc.c - InEPSize < 20");

	while (OldAddr < (PUCHAR)InEntryPoint + InEPSize)
	{
		FirstCode = *OldAddr;
		SecondCode = *(OldAddr + 1);


		// ���ǰ׺
		switch (FirstCode)
		{
			// 16 λ����������ж�
		case 0x67:
		{
			b16bit = TRUE;
			OldAddr++;
			continue;
		}
		}

		// �������תָ�� - �õ���ת��ֱ�ӵ�ַ
		switch (FirstCode)
		{
		case 0xE9:	// jmp imm16/imm32 - jmp ָ��
		{
			if (OldAddr != InEntryPoint)
			{
				THROW(STATUS_NOT_SUPPORTED, L"Hooking far jumps is only supported if they are the first instruction.");
			}
		}
		case 0xE8:	// call imm16/imm32 -- E8/E9 ָ������ת�Ĵ����� 32bit/64bit ����û�������
		{
			if (b16bit)
			{
				AbsAddr = *((INT16*)(OldAddr + 1));
				OpCodeLength = 3;
			}
			else    // ����ĺ�����ת
			{
				AbsAddr = *((INT32*)(OldAddr + 1));	//ȡԭƫ��
				OpCodeLength = 5;
			}
			break;
		}
		case 0xEB:	// jmp imm8
		{
			AbsAddr = *((INT8*)(OldAddr + 1));
			OpCodeLength = 2;
			break;
		}
		// �����ת����������ת - ��֧�֣�������
		case 0xE3: // jcxz imm8
		{
			THROW(STATUS_NOT_SUPPORTED, L"Hooking near (conditional) jumps is not supported.");
			break;
		}
		case 0x0F:
		{
			if ((SecondCode & 0xF0) == 0x80) // jcc imm16/imm32
				THROW(STATUS_NOT_SUPPORTED, L"Hooking far conditional jumps is not supported.");
			break;
		}
		}

		if ((FirstCode & 0xF0) == 0x70)     // jcc imm8
		{
			THROW(STATUS_NOT_SUPPORTED, L"Hooking near conditional jumps is not supported.");
		}

		// �������ת���� - ȡ����ת���յ�ַ
		// ����ֱ����ת������
		if (OpCodeLength > 0)
		{
			// 1. ���� mov eax(rax), AbsAddr
			// TargetAddress = EIP(��ǰָ���ַ + ָ���) + Offset
			AbsAddr = AbsAddr + (LONG_PTR)(OldAddr + OpCodeLength);

			// 6 λ��ʹ�� REX.W-perfix ǰ׺��
#ifdef _M_X64
			*NewAddr = 0x48;	// 0100(4) 1000(8) - ��ʾʹ��64λ�������� 
			NewAddr++;
#endif
			*NewAddr = 0xB8;	// mov eax
			NewAddr++;
			*((LONG_PTR*)NewAddr) = AbsAddr;

			NewAddr += sizeof(PVOID);	// Խ��Ŀ���ַ����

			// ��ת��ʵ�ʵ�ַ �Ƿ�������???
			if (((LONGLONG)NewAddr >= (LONGLONG)InEntryPoint) && (AbsAddr < (LONGLONG)InEntryPoint + InEPSize))
			{
				THROW(STATUS_NOT_SUPPORTED, L"Hooking jumps into the hooked entry point is not supported.");
			}

			// ����ֻ�趼���� call/jmp eax ��Ϊǰ����Reloc��ʱ�������ƫ�Ʋ��ᳬ�� 32bit������32λ�ļĴ����͹����ˡ�
			switch (FirstCode)
			{
			case 0xE8:	// call eax
			{
				*NewAddr = 0xFF;
				NewAddr++;
				*NewAddr = 0xD0;
				NewAddr++;

				break;
			}
			case 0xE9:	// jmp eax
			case 0xEB:	// jmp imm8
			{
				*NewAddr = 0xFF;
				NewAddr++;
				*NewAddr = 0xE0;
				NewAddr++;

				break;
			}
			}
			/*
				���ϵ�ת���Ǳ����
				����Ŀ�꺯���Ѿ���Hook��ʹ���˷�ֹHook�ķ��������һ����Ч�Ĵ��롣
				����EasyHook���õ����ַ��������ظ�Hookͬһ������������������δ֪��Hook����Hook EasyHook�Ѿ�Hook�ķ�����
				ֻ�е�EasyHookȥHook����Hook���Ѿ�Hook�ķ������ܻ��������ȶ���������ر���һЩ���ȶ��Ŀ��Լ�
			*/
			*OutRelocSize = (ULONG)(NewAddr - (PUCHAR)Buffer);
		}
		else
		{	
			// ������תָ�� - �жϵ�ǰָ���Ƿ��и� RIP/EIP �й�
			FORCE(LhRelocateRIPRelativeInstruction((ULONGLONG)OldAddr, (ULONGLONG)NewAddr, &bIsRIPRelatieve));
		}
		
		// �����16λ��ǰ��OldAddr��ǰ�ƶ���һλ-Ϊ�˽����жϡ����ڽ���ָ�������һλ�����п�����
		if (b16bit)
		{
			OldAddr--;
		}

		// �õ���һ��ָ�� ��������
		FORCE(LhGetInstructionLength(OldAddr, &InstrLength));
		// ���������תָ�����Ҳû�� RIP ��� - ֱ�ӿ���
		if (OpCodeLength == 0)
		{
			if (!bIsRIPRelatieve)
			{
				RtlCopyMemory(NewAddr, OldAddr, InstrLength);
			}

			NewAddr += InstrLength;
		}

		OldAddr += InstrLength;
		bIsRIPRelatieve = FALSE;
		b16bit = FALSE;
	}

	// ���ظ��䳤��
	*OutRelocSize = (ULONG)(NewAddr - (PUCHAR)Buffer);
	RETURN;
FINALLY_OUTRO:
THROW_OUTRO:
	{
		return NtStatus;
	}
}

/// \brief �ж���ָ���Ƿ��RIP ���
EASYHOOK_NT_INTERNAL LhRelocateRIPRelativeInstruction(ULONGLONG InOffset, ULONGLONG InTargetOffset, PBOOL OutWasRelocated)
{
#ifndef _M_X64
	return FALSE;
#else
#ifndef MAX_INSTR
#define MAX_INSTR 100
#endif

	ULONG32			AsmSize = 0;
	CHAR			DisassembleBuffer[MAX_INSTR] = { 0 };
	CHAR			Offset[MAX_INSTR] = { 0 };
	NTSTATUS		NtStatus = STATUS_SUCCESS;
	LONG64			MemDelta = InTargetOffset - InOffset;
	ULONG64		    NextInstr = 0;
	LONG		    Pos = 0;
	ULONG64			RelAddrOffset = 0;
	LONG64			RelAddr = 0;
	LONG64		    RelAddrSign = 1;	/// <��ʾ�����������RIP�Ǽӻ��Ǽ�

	// ���Ӧ���� 31bit (��һλ����λ 
	ASSERT(MemDelta == (LONG)MemDelta, L"reloc.c - MemDelta == (LONG)MemDelta");
	*OutWasRelocated = FALSE;

	// ������һ������
	if (!RTL_SUCCESS(LhDisassembleInstruction((PVOID)InOffset, &AsmSize, DisassembleBuffer, sizeof(DisassembleBuffer), &NextInstr)))
		THROW(STATUS_INVALID_PARAMETER_1, L"Unable to disassemble entry point. ");

	// �鿴 ���������� �Ƿ��� [ ���š����� mov rax, qword ptr [rip+0x4h]
	Pos = RtlAnsiIndexOf(DisassembleBuffer, '[');
	if (Pos < 0)
		RETURN;

	if (DisassembleBuffer[Pos + 1] == 'r' && DisassembleBuffer[Pos + 2] == 'i' && DisassembleBuffer[Pos + 3] == 'p' && 
	   (DisassembleBuffer[Pos + 4] == '+' || DisassembleBuffer[Pos + 4] == '-'))
	{
		/*
			֧�� RIP �Ӽ�������ֱ���޸�ƫ��ֵ����ֱ����ת��Hook����
			e.g 
			Entry Point:																	   Relocated:
			66 0F 2E 05 DC 25 FC FF   ucomisd xmm0, [rip-0x3da24]   IP:ffc46d4		---->	   66 0F 2E 05 10 69 F6 FF   ucomisd xmm0, [rip-0x996f0]   IP:100203a0
		*/
		if (DisassembleBuffer[Pos + 4] == '-')
		{
			RelAddrSign = -1;	// ����� -��Sign �ͱ�Ϊ -1�������ֵ���� RelAddrSign ��ת��Ϊʵ�ʶ��� RIP ��ƫ��ֵ
		}

		Pos += 4;	// ���� RIP -/+
		// �õ��Ӵ� - ʵ�ʵĲ�����
		if (RtlAnsiSubString(DisassembleBuffer, Pos + 1, RtlAnsiIndexOf(DisassembleBuffer, ']' - Pos - 1), Offset, MAX_INSTR) <= 0)
		{
			RETURN;
		}

		// ��ʮ�����Ʋ�����ת��Ϊʮ������
		RelAddr = RtlAnsiHexToLong64(Offset, MAX_INSTR);
		if (!RelAddr)
			RETURN;

		RelAddr *= RelAddrSign;			// �����λ���
		if (RelAddr != (LONG)RelAddr)	// ƫ�Ʊ�����32λ���ڵ�(1λ����31λƫ��
			RETURN;

		// ȷ��ת����ֵַ����ȷ
		for (Pos = 1; Pos <= NextInstr - (InOffset + 4); Pos++)
		{
			// �ҵ�ƥ��ֵ - ��¼ƫ�ƣ�Ϊ����ĸ�����׼��
			if (*((LONG*)(InOffset + Pos)) == RelAddr)
			{	
				// ����һ��ƥ�� - �޷�ȷ�� �����˳�
				if (RelAddrOffset != 0)
				{
					RelAddrOffset = 0;
					break;
				}
				RelAddrOffset = Pos;
			}
		}
		if (RelAddrOffset == 0) 
		{
			THROW(STATUS_INTERNAL_ERROR, L"The given entry point contains a RIP-relative instruction for which we can't determine the correct address offset!");
		}

		// ��ֵ������
		RelAddr = RelAddr - MemDelta;	// ԭƫ�� - Relocƫ�� = ����ƫ�� Ϊɶ�� - ���� - ���ﹹ���ƫ���Ǵ�Reloc��������ԭ������Ҫ��ת�ĵط�����ôMemDelta����Rloc��ԭ�����ƫ��.
										// ���Ǽ���MemDelta��ʱ���� Reloc - Old(Old��Reloc�����ƫ��)����������Ӽ��ţ���ΪReloc��old�����ƫ��
		if (RelAddr != (LONG)RelAddr)	// ����ƫ�Ƴ����� 31λ ��
			THROW(STATUS_NOT_SUPPORTED, L"The given entry point contains at least one RIP-Relative instruction that could not be relocated!");

		RtlCopyMemory((void*)InTargetOffset, (void*)InOffset, (ULONG)(NextInstr - InOffset));	// ��������ָ��
		*((LONG*)(InTargetOffset + RelAddrOffset)) = (LONG)RelAddr;	// �����²�����

		*OutWasRelocated = TRUE;
	}

	RETURN;
THROW_OUTRO:
FINALLY_OUTRO:
	{
		return NtStatus;
	}
#endif
}


///	\brief ʹ�� udis86�� �������õ�ָ��
/// 
///	\param InPtr - ���뻺��
/// \param Length -  ����
/// \param Buffer - �����������
/// \param BufferSize - ����
/// \param NextInstr - ��һ��ָ���
///	������Կ� https://github.com/vmt/udis86
EASYHOOK_NT_INTERNAL LhDisassembleInstruction(PVOID InPtr, PULONG Length, PSTR Buffer, LONG BufferSize, PULONG64 NextInstr)
{
	ud_t ud_obj = { 0 };
	ud_init(&ud_obj);			// ��ʼ��

	// ����ģʽ
#ifdef _M_X64
	ud_set_mode(&ud_obj, 64);   
#else
	ud_set_mode(&ud_obj, 32);
#endif

	// ���û������ - intel
	ud_set_syntax(&ud_obj, UD_SYN_INTEL);
	// ���÷���໺��
	ud_set_asm_buffer(&ud_obj, Buffer, BufferSize);
	// �������뻺��
	ud_set_input_buffer(&ud_obj, (UINT8*)InPtr, 32);
	// �����
	*Length = ud_disassemble(&ud_obj);

	*NextInstr = (ULONG64)InPtr + *Length;

	if (Length > 0)
		return STATUS_SUCCESS;
	else
		return STATUS_INVALID_PARAMETER;
}