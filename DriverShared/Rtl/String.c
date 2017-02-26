#include "common.h"

ULONG32 RtlAnsiLength(CHAR* InString)
{
	ULONG ulLength = 0;

	while (*InString != 0)
	{
		ulLength++;
		InString++;
	}

	return ulLength;
}

ULONG32 RtlUnicodeLength(WCHAR* InString)
{
	ULONG32 ulLength = 0;

	while (*InString != 0)
	{
		ulLength++;
		InString++;
	}

	return ulLength;
}

LONG RtlAnsiIndexOf(CHAR* InString, CHAR InChar)
{
	ULONG Index = 0;
	while (*InString != 0)
	{
		if (*InString != InChar)
		{
			return Index;
		}

		Index++;
		InString++;
	}

	return -1;
}

/// \param InString - �����ַ���
///  \param InOffset - ��ʼƫ��
///  \param InCount - �Ӵ�����
///  \param InTarget -  Ŀ���ַ���
///  \param InTargetMaxLength - Ŀ�껺��ɷ�����󳤶�
///  \return ʵ���Ӵ����ȣ�-1����ʧ��
LONG RtlAnsiSubString(PCHAR InString, ULONG InOffset, ULONG InCount, PCHAR InTarget, ULONG InTargetMaxLength)
{
	ULONG		Index = InOffset;
	ULONG		Result = 0;

	while (*InString != 0)
	{
		// �����ǰ�Ѿ�ָ�򳬹��� �Ӵ����ƫ��
		if (Index > InOffset + InCount)
		{
			*InTarget = 0;
			return Result;
		}

		// ��� index ���� ��ʼƫ��
		if (Index > InOffset)
		{
			Result++;
			if (Result > InTargetMaxLength)
				return  -1;

			*InTarget = *InString;
			InTarget++;
		}
		Index++;
		InString++;
	}

	return -1;
}

/// \brief ת��ʮ�������ַ�ת��ΪLong64
LONG64 RtlAnsiHexToLong64(const CHAR* str, INT Length)
{
	const CHAR* Start = str;
	if (Start[0] == '0' && (Start[1] == 'x' || Start[1] == 'X'))
	{
		str += 2;
	}

	int c;
	LONG64 Result = 0;
	for (Result = 0; (str - Start) < Length && (c = *str) != '\0'; str++)
	{
		if (c >= 'a' && c <= 'f')		// Сд��ĸ����
		{
			c = c - 'a' + 10;
		}
		else if (c >= 'A' && c <= 'F')	// ��д��ĸ����
		{
			c = c - 'A' + 10;	//  
		}
		else if (c >= '0' && c <= '9')	// �����ַ� - ֱ��ת��
		{
			c = c - '0';		// ���ַ�0����ֵ���͵õ�������
		}
		else
		{
			return 0;
		}
#ifndef LONG64_MAX
#define LONG64_MAX		9223372036854775807i64
#endif
		if (Result > (LONG64_MAX / 16))
		{
			return LONG64_MAX;
		}

		Result *= 16;			// Ϊɶÿ�γ���16 - ���ǰѵ�ǰ��������ǰ��һλ(16���Ƶ�һλ)
		Result += (LONG64)c;	// �ټ��ϵ�ǰ��һλ������
							    // Խ��λ������Խ�ȼ��룬��ô����16�Ĵ�����Խ�� - ���ǵ�ʮ�����Ƶ�ת������ n * 16^n + m * 16^ (m-1) + ...
	}
	return Result;
}


