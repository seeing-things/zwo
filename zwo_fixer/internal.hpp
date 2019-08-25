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
#include <string>
#include <type_traits>

// STL
#include <forward_list>
#include <map>
#include <unordered_map>

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

inline uintptr_t PageSize()
{
	static auto page_size = (uintptr_t)sysconf(_SC_PAGESIZE);
	return page_size;
}

// =============================================================================


// libusb ======================================================================

#ifdef __cplusplus
#define libusb_strerror(errcode) libusb_strerror(static_cast<enum libusb_error>(errcode))
#endif

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
	snprintf(fmt_color, sizeof(fmt_color), "\e[%dm" "[ZWOFixer] " "%s" "\e[0m", static_cast<int>(color), fmt);
	
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt_color, va);
	va_end(va);
}

// =============================================================================


// libASICamera2 versions, offsets, etc ========================================

using OffsetMap_t  = std::unordered_map<std::string, uintptr_t>;
using VersionMap_t = std::map<std::string, const OffsetMap_t *>;

static const OffsetMap_t g_Offsets_v1_14_0715 = {
	{ ".plt:libusb_cancel_transfer",       0x043D28 },
	{ ".text:callbackUSBTransferComplete", 0x1402E0 },
	{ ".data:lin_XferLen",                 0x38D260 },
	{ ".bss:lin_XferCallbacked",           0x3DD854 },
};

static const OffsetMap_t g_Offsets_v0_07_0503 = {
	{ ".plt:libusb_cancel_transfer",       0x039588 },
	{ ".text:callbackUSBTransferComplete", 0x0FB750 },
	{ ".data:lin_XferLen",                 0x33F3E0 },
	{ ".bss:lin_XferCallbacked",           0x37B8D4 },
};

static const VersionMap_t g_KnownLibASIVersions = {
	{ "1, 14, 0715", &g_Offsets_v1_14_0715 },
	{ "1, 14, 0425", nullptr               },
	{ "1, 14, 0227", nullptr               },
	{ "1, 13, 0930", nullptr               },
	{ "1, 13, 0821", nullptr               },
	{ "0,  7, 0503", &g_Offsets_v0_07_0503 },
	{ "0,  7, 0118", nullptr               },
	{ "0,  6, 0921", nullptr               },
	{ "0,  6, 0504", nullptr               },
	{ "0,  6, 0414", nullptr               },
	{ "0,  6, 0328", nullptr               },
};

static const dl_phdr_info *g_LibASIInfo    = nullptr;
static       void         *g_LibASIHandle  = nullptr;
static const char         *g_LibASIVersion = nullptr;
static const OffsetMap_t  *g_LibASIOffsets = nullptr;

static inline bool IsLibASILoadedAndSupported()
{
	static bool s_Loaded    = false;
	static bool s_Supported = false;
	
	static bool s_First = true;
	if (s_First) {
		dl_iterate_phdr(
			[](dl_phdr_info *info, size_t size, void *data) -> int{
				(void)data;
				
				if (info == nullptr)                                        return 0;
				if (info->dlpi_name == nullptr)                             return 0;
				if (strstr(info->dlpi_name, "libASICamera2.so") == nullptr) return 0;
				
				g_LibASIInfo = info;
				return 1;
			},
			nullptr);
		
		if (g_LibASIInfo != nullptr) {
		//	Msg(Color::WHITE, "g_LibASIInfo->lib_name: \"%s\"\n",                       g_LibASIInfo->dlpi_name);
		//	Msg(Color::WHITE, "g_LibASIInfo->lib_base: 0x%016" PRIXPTR "\n", (uintptr_t)g_LibASIInfo->dlpi_addr);
			
			if ((g_LibASIHandle = dlopen(g_LibASIInfo->dlpi_name, RTLD_LAZY | RTLD_NOLOAD)) != nullptr) {
				s_Loaded = true;
				
				char *(*ASIGetSDKVersion)() = nullptr;
				*reinterpret_cast<void **>(&ASIGetSDKVersion) = dlsym(g_LibASIHandle, "ASIGetSDKVersion");
				if (ASIGetSDKVersion != nullptr) {
					g_LibASIVersion = (*ASIGetSDKVersion)();
					if (g_LibASIVersion != nullptr) {
					//	Msg(Color::WHITE, "g_LibASIVersion: \"%s\"\n", g_LibASIVersion);
						
						auto it = g_KnownLibASIVersions.find(g_LibASIVersion);
						if (it != g_KnownLibASIVersions.end()) {
							g_LibASIOffsets = it->second;
							if (g_LibASIOffsets == nullptr) {
								Msg(Color::RED, "Init failure: library loaded, but version \"%s\" not supported\n", g_LibASIVersion);
							}
						} else {
							Msg(Color::RED, "Init failure: library loaded, but version \"%s\" not recognized\n", g_LibASIVersion);
						}
					} else {
						Msg(Color::RED, "Init failure: library loaded, but ASIGetSDKVersion returned nullptr\n");
					}
				} else {
					Msg(Color::RED, "Init failure: library loaded, but ASIGetSDKVersion not found (dlsym)\n");
				}
			} else {
				Msg(Color::RED, "Init failure: failed to load library\n");
			}
		} else {
			Msg(Color::RED, "Init failure: failed to locate library in memory\n");
		}
		
		s_First = false;
	}
	
	return (s_Loaded && s_Supported);
}

static inline uintptr_t GetAddr(const std::string& name)
{
	if (!IsLibASILoadedAndSupported()) return 0;
	
	// will attempt to throw if not present in the map
	return static_cast<uintptr_t>(g_LibASIInfo->dlpi_addr) + g_LibASIOffsets->at(name);
}

template<typename T> static T  GetPtr(const std::string& name) { return reinterpret_cast<T>(GetAddr(name)); }
template<typename T> static T& GetRef(const std::string& name) { return *GetPtr<T *>(name); }

// =============================================================================


// libASICamera2 pointers ======================================================

inline void (*callbackUSBTransferComplete)(libusb_transfer *) =
	GetPtr<decltype(callbackUSBTransferComplete)>(".text:callbackUSBTransferComplete");

inline auto lin_XferLen        = GetRef<int >(".data:lin_XferLen");
inline auto lin_XferCallbacked = GetRef<bool>(".bss:lin_XferCallbacked");

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
		if (!IsLibASILoadedAndSupported()) return;
		
		Msg(Color::WHITE, "PLTHook(%s): constructed\n", m_Name);
		if ((mode & PLTHOOK_MANUAL) == 0) {
			Install();
		}
	}
	
	PLTHook(PLTHook&) = delete;
	
	~PLTHook()
	{
		if (!IsLibASILoadedAndSupported()) return;
		
		if ((m_Mode & PLTHOOK_PERSIST) == 0) {
			Uninstall();
		}
		Msg(Color::WHITE, "PLTHook(%s): destructed\n", m_Name);
	}
	
	void Install()
	{
		if (!IsLibASILoadedAndSupported()) return;
		
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
		if (!IsLibASILoadedAndSupported()) return;
		
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
