#pragma once


/* magic. */
#define _DEF_PTR_P(TYPE)   using  P  ## TYPE =       TYPE *;
#define _DEF_PTR_LP(TYPE)  using LP  ## TYPE =       TYPE *;
#define _DEF_PTR_PC(TYPE)  using  PC ## TYPE = const TYPE *;
#define _DEF_PTR_LPC(TYPE) using LPC ## TYPE = const TYPE *;
#define _DEF_PTR_x(TYPE)
#define _DEF_PTR_xx(TYPE)
#define _DEF_PTR_xxx(TYPE)

#define _DEF_PTR(TYPE, P1, P2, P3, P4) _DEF_PTR_##P1(TYPE) _DEF_PTR_##P2(TYPE) _DEF_PTR_##P3(TYPE) _DEF_PTR_##P4(TYPE)

#define DEF_WINAPI_TYPE(NAME, TYPE, P1, P2, P3, P4) using NAME = TYPE; _DEF_PTR(NAME, P1, P2, P3, P4)


/////////////// NAME======= TYPE============ POINTERS=======

DEF_WINAPI_TYPE(VOID,       void,            P, PC, LP, LPC)

DEF_WINAPI_TYPE( BYTE,      uint8_t,         P, PC, LP, LPC)
DEF_WINAPI_TYPE( WORD,      uint16_t,        P, xx, LP, xxx)
DEF_WINAPI_TYPE(DWORD,      uint32_t,        P, xx, LP, xxx)
DEF_WINAPI_TYPE(QWORD,      uint32_t,        P, xx, xx, xxx)

DEF_WINAPI_TYPE(DWORD32,    uint32_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE(DWORD64,    uint64_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE(DWORDLONG,  uint64_t,        P, xx, xx, xxx)

DEF_WINAPI_TYPE(BOOL,        int32_t,        P, PC, LP, xxx)
DEF_WINAPI_TYPE(BOOLEAN,    uint8_t,         P, xx, xx, xxx)

DEF_WINAPI_TYPE( CHAR,      char,            P, PC, xx, xxx)
DEF_WINAPI_TYPE(UCHAR,      unsigned char,   P, PC, xx, xxx)
DEF_WINAPI_TYPE(WCHAR,      wchar_t,         P, PC, xx, LPC)
DEF_WINAPI_TYPE(UNICODE,    wchar_t,         P, xx, xx, xxx)

DEF_WINAPI_TYPE(FLOAT,      float,           P, xx, xx, xxx)
DEF_WINAPI_TYPE(DOUBLE,     double,          x, xx, xx, xxx)

DEF_WINAPI_TYPE( SHORT,      int16_t,        P, PC, xx, xxx)
DEF_WINAPI_TYPE(USHORT,     uint16_t,        P, PC, xx, xxx)
DEF_WINAPI_TYPE( INT,        int32_t,        P, xx, LP, xxx)
DEF_WINAPI_TYPE(UINT,       uint32_t,        P, xx, LP, xxx)
DEF_WINAPI_TYPE( LONG,       int32_t,        P, PC, LP, xxx)
DEF_WINAPI_TYPE(ULONG,      uint32_t,        P, PC, LP, xxx)
DEF_WINAPI_TYPE( LONGLONG,   int64_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE(ULONGLONG,  uint64_t,        P, xx, xx, xxx)

DEF_WINAPI_TYPE( INT8,       int8_t,         P, xx, xx, xxx)
DEF_WINAPI_TYPE(UINT8,      uint8_t,         P, xx, xx, xxx)
DEF_WINAPI_TYPE( INT16,      int16_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE(UINT16,     uint16_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE( INT32,      int32_t,        P, PC, xx, xxx)
DEF_WINAPI_TYPE(UINT32,     uint32_t,        P, PC, xx, xxx)
DEF_WINAPI_TYPE( INT64,      int64_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE(UINT64,     uint64_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE( LONG32,     int32_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE(ULONG32,    uint32_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE( LONG64,     int64_t,        P, xx, xx, xxx)
DEF_WINAPI_TYPE(ULONG64,    uint64_t,        P, xx, xx, xxx)

DEF_WINAPI_TYPE(DWORD_PTR,  uintptr_t,       P, xx, xx, xxx)
DEF_WINAPI_TYPE(  INT_PTR,   intptr_t,       P, xx, xx, xxx)
DEF_WINAPI_TYPE( UINT_PTR,  uintptr_t,       P, xx, xx, xxx)
DEF_WINAPI_TYPE( LONG_PTR,   intptr_t,       P, xx, xx, xxx)
DEF_WINAPI_TYPE(ULONG_PTR,  uintptr_t,       P, xx, xx, xxx)

DEF_WINAPI_TYPE(POINTER_32, uint32_t,        x, xx, xx, xxx)
DEF_WINAPI_TYPE(POINTER_64, uint64_t,        x, xx, xx, xxx)

DEF_WINAPI_TYPE( SIZE_T,    uintptr_t,       P, xx, xx, xxx)
DEF_WINAPI_TYPE(SSIZE_T,     intptr_t,       P, xx, xx, xxx)

DEF_WINAPI_TYPE(HANDLE,     void *,          P, xx, LP, xxx)
DEF_WINAPI_TYPE(NTSTATUS,   int32_t,         P, PC, xx, xxx)
DEF_WINAPI_TYPE(STRING,     unsigned char *, P, xx, xx, xxx)

////////////////////////////////////////////////////////////


#undef _DEF_PTR_P
#undef _DEF_PTR_LP
#undef _DEF_PTR_PC
#undef _DEF_PTR_LPC
#undef _DEF_PTR_x
#undef _DEF_PTR_xx
#undef _DEF_PTR_xxx
#undef _DEF_PTR
#undef DEF_WINAPI_TYPE
