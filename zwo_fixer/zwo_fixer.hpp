#pragma once


// C++ only ====================================================================

#ifndef __cplusplus
#error
#endif

// =============================================================================


// Includes ====================================================================

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// C
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <cinttypes>
#include <cassert>

// C++
#include <type_traits>

// STL
#include <forward_list>

// Linux
#include <unistd.h>
//#include <fcntl.h>
#include <sys/mman.h>

// BSD
//#include <bsd/string.h>

// libdl etc
#include <dlfcn.h>
#include <link.h>

// libusb
#include <libusb-1.0/libusb.h>

// =============================================================================


// Linux =======================================================================

uintptr_t PageSize()
{
	static auto page_size = (uintptr_t)sysconf(_SC_PAGESIZE);
	return page_size;
}

// =============================================================================


// libdl etc ===================================================================

template<typename T> static T dlsym_safe(void *handle, const char *symbol) { return (T)dlsym(handle, symbol); }
#define dlsym dlsym_safe

// =============================================================================


// libusb ======================================================================

#ifdef __cplusplus
#define libusb_strerror(errcode) libusb_strerror(static_cast<enum libusb_error>(errcode))
#endif

// =============================================================================


// Globals =====================================================================

static inline bool g_Fail = false;

// =============================================================================


// Helper: colored console messages ============================================

// https://en.wikipedia.org/wiki/ANSI_escape_code#Colors
enum class Color : int
{
	RED     = 91,
	GREEN   = 92,
	YELLOW  = 93,
	BLUE    = 94,
	MAGENTA = 95,
	CYAN    = 96,
	WHITE   = 97,
};

// TODO: maybe make this a variadic template function instead...?
// - make color be a template parameter
// - see if we can still keep [[gnu::format(printf, 1, 2)]]
// TODO: don't do snprintf-to-local-buffer rubbish if at all feasible
// - would want to still find a way to keep the color-start, fmt, color-end parts atomically together
[[gnu::format(printf, 2, 3)]]
static inline void Msg(Color color, const char *fmt, ...)
{
	char fmt_color[8192];
	snprintf(fmt_color, sizeof(fmt_color), "\e[%dm" "[ZWOFixer " ZWO_VERSION_STRING "]" "%s" "\e[0m", static_cast<int>(color), fmt);
	
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt_color, va);
	va_end(va);
}

// =============================================================================


// Helper: find base address of dynamic library ================================

static inline uintptr_t FindLibBaseAddr(const char *lib_name)
{
	struct LambdaInfo
	{
		const char *lib_name;
		uintptr_t lib_base;
	} l_data = { lib_name, 0x0 };
	
	bool success = (dl_iterate_phdr([](struct dl_phdr_info *info, size_t size, void *data) -> int {
		auto p_data = reinterpret_cast<decltype(l_data) *>(data);
		
//		Msg(Color::WHITE, "p_data->lib_name: \"%s\"\n", p_data->lib_name);
//		Msg(Color::WHITE, "p_data->lib_base: 0x%016" PRIXPTR "\n", p_data->lib_base);
		
//		Msg(Color::WHITE, "info->dlpi_name: \"%s\"\n", info->dlpi_name);
//		Msg(Color::WHITE, "info->dlpi_addr: 0x%016" PRIXPTR "\n", (uintptr_t)info->dlpi_addr);
		
		if (info != nullptr && info->dlpi_name != nullptr && strstr(info->dlpi_name, p_data->lib_name) != nullptr) {
//			Msg(Color::WHITE, "return 1\n");
			p_data->lib_base = info->dlpi_addr;
			return 1;
		}
		
//		Msg(Color::WHITE, "return 0\n");
		return 0;
	}, &l_data) != 0);
	
	if (success) {
		Msg(Color::GREEN, "Found library %s base address: 0x%016" PRIXPTR "\n", lib_name, l_data.lib_base);
	} else {
		Msg(Color::RED, "Cannot locate the in-memory base address for library %s.\n", lib_name);
		g_Fail = true;
	}
	
	return l_data.lib_base;
}

// =============================================================================


// Helper: auto-instance-tracking base class ===================================

template<typename T>
class AutoInstanceList
{
public:
	static const std::forward_list<T *>& Instances() { return s_List; }
	
protected:
	AutoInstanceList() { s_List.push_front(static_cast<T *>(this)); }
	AutoInstanceList(AutoInstanceList&) = delete;
	
	// non-virtual dtor: only an idiot would intentionally destroy a ptr to this base class
	~AutoInstanceList() { s_List.remove(static_cast<T *>(this)); }
	
private:
	static inline std::forward_list<T *> s_List;
};

// =============================================================================


// Helper: PLT-hooking class ===================================================

// only allow function pointers; and disallow non-static member function pointers
// (don't want any 'this' ptr crap, just a nice simple 64-bit func ptr)
//#define STATIC_FUNC_SFINAE std::enable_if_t<std::is_function_v<T> && !std::is_member_pointer_v<T>, T>

enum PLTHookMode : uint_fast8_t
{
	PLTHOOK_MANUAL  = (1 << 0), // don't automatically install after construction
	PLTHOOK_PERSIST = (1 << 1), // don't automatically uninstall after destruction
	
	PLTHOOK_DEFAULT = 0,
};

class PLTHook : public AutoInstanceList<PLTHook>
{
public:
//	template<typename T>
//	PLTHook(uintptr_t plt_entry_offset, STATIC_FUNC_SFINAE hook_func) :
//		m_PLTEntryOffset(plt_entry_offset), m_HookFuncAddr((uint64_t)hook_func) {}
	
	template<typename F_RET, typename... F_PARAMS>
	PLTHook(const char *name, uintptr_t plt_entry_addr, F_RET (*hook_func)(F_PARAMS...), PLTHookMode mode = PLTHOOK_DEFAULT) :
		m_Name(name), m_PLTEntryPtr((uint8_t *)plt_entry_addr), m_HookFuncAddr((uint64_t)hook_func), m_Mode(mode)
	{
		Msg(Color::WHITE, "PLTHook(%s): constructed\n", m_Name);
		if ((mode & PLTHOOK_MANUAL) == 0) {
			Install();
		}
	}
	
	PLTHook(PLTHook&) = delete;
	
	~PLTHook()
	{
		if ((m_Mode & PLTHOOK_PERSIST) == 0) {
			Uninstall();
		}
		Msg(Color::WHITE, "PLTHook(%s): destructed\n", m_Name);
	}
	
	void Install()
	{
		if (m_Installed) return;
		m_Installed = true;
		
		SetWritable(true);
		
		Backup();
		
		uint8_t *ptr = m_PLTEntryPtr;
		
		// jmp qword [rip+0x00000000]
		ptr[0x0] = 0xFF;
		ptr[0x1] = 0x25;
		ptr[0x2] = 0x00;
		ptr[0x3] = 0x00;
		ptr[0x4] = 0x00;
		ptr[0x5] = 0x00;
		
		// dq Hook
		*reinterpret_cast<uint64_t *>(ptr + 0x6) = m_HookFuncAddr;
		
		// int3; int3
		ptr[0xe] = 0xCC;
		ptr[0xf] = 0xCC;
		
		// custom absolute jump thunk: (uses 14 out of 16 available bytes in a .PLT section entry)
		// 00:  FF 25 00 00 00 00        jmp [rip+0x00000000]
		// 06:  XX XX XX XX XX XX XX XX  dq ADDR
		// 0E:  CC                       int3
		// 0F:  CC                       int3
		
		SetWritable(false);
		
		Msg(Color::WHITE, "PLTHook(%s): installed\n", m_Name);
	}
	
	void Uninstall()
	{
		if (!m_Installed) return;
		m_Installed = false;
		
		SetWritable(true);
		Restore();
		SetWritable(false);
		
		Msg(Color::WHITE, "PLTHook(%s): uninstalled\n", m_Name);
	}
	
private:
	void SetWritable(bool writable)
	{
		auto ptr = (void *)((uintptr_t)m_PLTEntryPtr & ~(PageSize() - 1));
		int prot = (writable ? (PROT_READ | PROT_WRITE) : (PROT_READ | PROT_EXEC));
		
		assert(mprotect(ptr, 0x10, prot) == 0);
	}
	
	void Backup()        { memcpy(m_Backup, m_PLTEntryPtr, 0x10); }
	void Restore() const { memcpy(m_PLTEntryPtr, m_Backup, 0x10); }
	
	const char *m_Name;
	uint8_t *m_PLTEntryPtr;
	uint64_t m_HookFuncAddr;
	PLTHookMode m_Mode;
	
	uint8_t m_Backup[16];
	bool m_Installed = false;
};

// =============================================================================


// libASICamera2 addresses =====================================================

#if !defined(ZWO_VERSION_MAJOR) || !defined(ZWO_VERSION_MINOR) || !defined(ZWO_VERSION_REVISION) || !defined(ZWO_VERSION) || !defined(ZWO_VERSION_STRING)
#error
#endif


static inline const uintptr_t libASICamera2_base = FindLibBaseAddr("libASICamera2.so." ZWO_VERSION_STRING);


#if ZWO_VERSION == 0x00070503U

#define libASICamera2_INIT                              (libASICamera2_base + 0x039290)
#define libASICamera2_PLT                               (libASICamera2_base + 0x0392A8)
#define libASICamera2_TEXT                              (libASICamera2_base + 0x03D250)
#define libASICamera2_FINI                              (libASICamera2_base + 0x1069C8)
#define libASICamera2_RODATA                            (libASICamera2_base + 0x1069E0)
#define libASICamera2_DATARELRO                         (libASICamera2_base + 0x333040)
#define libASICamera2_GOT                               (libASICamera2_base + 0x337918)
#define libASICamera2_GOTPLT                            (libASICamera2_base + 0x337C48)
#define libASICamera2_DATA                              (libASICamera2_base + 0x339C40)
#define libASICamera2_BSS                               (libASICamera2_base + 0x33F4E0)
#define libASICamera2_EXTERN                            (libASICamera2_base + 0x3817F0)

#define libASICamera2_PLT__libusb_open                  (libASICamera2_base + 0x039868)
#define libASICamera2_PLT__libusb_submit_transfer       (libASICamera2_base + 0x03A988)
#define libASICamera2_PLT__libusb_cancel_transfer       (libASICamera2_base + 0x039588)
#define libASICamera2_PLT__CCameraFX3_startAsyncXfer    (libASICamera2_base + 0x03A188)

#define libASICamera2_TEXT__callbackUSBTransferComplete (libASICamera2_base + 0x0FB750)

#define libASICamera2_DATA__lin_XferLen                 (libASICamera2_base + 0x33F3E0)

#define libASICamera2_BSS__len_get                      (libASICamera2_base + 0x37B8D0)
#define libASICamera2_BSS__lin_XferCallbacked           (libASICamera2_base + 0x37B8D4)
#define libASICamera2_BSS__XferErr                      (libASICamera2_base + 0x37B8E0)

#else

#error unsupported version

#endif


void (*callbackUSBTransferComplete)(struct libusb_transfer *) = reinterpret_cast<decltype(callbackUSBTransferComplete)>(libASICamera2_TEXT__callbackUSBTransferComplete);

auto lin_XferLen        = reinterpret_cast<int  *>(libASICamera2_DATA__lin_XferLen);

auto len_get            = reinterpret_cast<int  *>(libASICamera2_BSS__len_get);
auto lin_XferCallbacked = reinterpret_cast<bool *>(libASICamera2_BSS__lin_XferCallbacked);
auto XferErr            = reinterpret_cast<int  *>(libASICamera2_BSS__XferErr);

// =============================================================================


// libASICamera2 dummy types ===================================================

class CCameraFX3
{
public:
	 CCameraFX3()            = delete;
	 CCameraFX3(CCameraFX3&) = delete;
	~CCameraFX3()            = delete;
	
	uint8_t __pad0000[0x0008];
	libusb_device_handle *dev_handle;
	uint8_t __pad0010[0x0048];
};
static_assert(sizeof(CCameraFX3) == 0x0058);

class CCameraBase : public CCameraFX3
{
public:
	         CCameraBase()             = delete;
	         CCameraBase(CCameraBase&) = delete;
	virtual ~CCameraBase()             = delete;
	
	uint8_t __pad0060[0x06C0];
};
static_assert(sizeof(CCameraBase) == 0x0720);

using CCameraS178MC = CCameraBase;

// =============================================================================
