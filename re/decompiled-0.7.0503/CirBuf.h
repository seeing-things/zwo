#pragma once


#include "Platform.h"


// TODO: figure this thing out; it seems to be a configurable number
constexpr int NUM_BUFS = 2;


inline CRITICAL_SECTION m_cs_buf[NUM_BUFS];


class CirBuf
{
public:
	enum : int // return value of InsertBuff
	{
		INSERTBUFF_0 = 0, // TODO: name and meaning
		INSERTBUFF_1 = 1, // TODO: name and meaning
		INSERTBUFF_2 = 2, // TODO: name and meaning
	};
	
	CirBuf(long size);
	~CirBuf();
	
	void ResetCirBuff();
	int InsertBuff(unsigned char *a2, int a3, unsigned short a4, int a5, unsigned short a6, int a7, int a8, int a9);
	
	bool ReadBuff(unsigned char *a2, int a3, int a4);
	bool IsBuffHeadCorrect(unsigned int a2, int a3);
	
	void StartInstBufThr();
	void StopInstBufThr();
	
private:
	int32_t            m_BufIdxRead; // unsure of exact type
	THREAD             m_InsertThread;
	long               m_Size;
	uint8_t           *m_Buffers[NUM_BUFS];
	uint8_t           *m_DataPtr1;
	uint8_t           *m_DataPtr2;
	int16_t            field_34; // unsure of exact type
	int16_t            field_36; // unsure of exact type
	int16_t            field_38; // unsure of exact type
	int32_t            field_3C; // unsure if these are even 4-byte vars or what exactly
	int32_t            field_40; // unsure if these are even 4-byte vars or what exactly
	int32_t            field_44; // unsure if these are even 4-byte vars or what exactly
	int32_t            field_48; // unsure if these are even 4-byte vars or what exactly
	int8_t             field_54; // unsure of exact type
	int8_t             field_55; // unsure of exact type
	int8_t             field_56; // unsure of exact type
	int32_t            field_58; // unsure of exact type or if this is an 8-byte var
	int32_t            field_5C; // unsure of exact type or if this is an 8-byte var
	int32_t            field_60; // unsure of exact type
	bool               m_bShouldRunInsertThread;
	int8_t             field_65; // unsure of exact type
	int32_t            m_BufIdxWrite; // unsure of exact type
	CONDITION_VARIABLE m_InsThdFinishedWork_CondVar;
	CONDITION_VARIABLE m_InsThdStartWorking_CondVar;
	CRITICAL_SECTION   m_InsThdStartWorking_Mutex;
	CRITICAL_SECTION   m_InsThdFinishedWork_Mutex;
	
	// TODO: inline initializers (if appropriate / if any)
};


#ifdef _WIN32
void InsertBufThd(void *pCirBuf);
#else
void *InsertBufThd(void *pCirBuf);
#endif
