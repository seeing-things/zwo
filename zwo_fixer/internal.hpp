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
//#include <bsd/err.h>
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

static const OffsetMap_t g_Offsets_v1_16_3 = {
	{ ".plt:libusb_cancel_transfer",       0x0516B0 },
	{ ".text:callbackUSBTransferComplete", 0x187A20 },
	{ ".data:lin_XferLen",                 0x3E7580 },
	{ ".bss:lin_XferCallbacked",           0x437D14 },
};

static const OffsetMap_t g_Offsets_v1_14_1119 = {
	{ ".plt:libusb_cancel_transfer",       0x046D30 },
	{ ".text:callbackUSBTransferComplete", 0x1514D0 },
	{ ".data:lin_XferLen",                 0x3A1540 },
	{ ".bss:lin_XferCallbacked",           0x3F1B74 },
};

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
	{ "1, 16, 3, 0", &g_Offsets_v1_16_3    }, // 2020-12-31
	{ "1, 16, 2, 0", nullptr               }, // 2020-??-??
	{ "1, 16, 1, 0", nullptr               }, // 2020-??-??
	{ "1, 16, 0",    nullptr               }, // 2020-??-??
	{ "1, 15, 0915", nullptr               }, // 2020-09-18
	{ "1, 15, 0819", nullptr               }, // 2020-08-19-ish
	{ "1, 15, 0617", nullptr               }, // 2020-06-17
	{ "1, 15, 0610", nullptr               }, // 2020-06-10
	{ "1, 15, 0430", nullptr               }, // 2020-04-30
	{ "1, 14, 1119", &g_Offsets_v1_14_1119 }, // 2019-11-19
	{ "1, 14, 0715", &g_Offsets_v1_14_0715 }, // 2019-07-15
	{ "1, 14, 0425", nullptr               }, // 2019-04-25-ish
	{ "1, 14, 0227", nullptr               }, // 2019-02-27
	{ "1, 13, 0930", nullptr               }, // 2018-09-30
	{ "1, 13, 0821", nullptr               }, // 2018-08-21
	{ "0,  7, 0503", &g_Offsets_v0_07_0503 }, // 2018-05-23 aka 1.13.0523
	{ "0,  7, 0118", nullptr               }, // 2018-01-19 aka 1.13.1.12
	{ "0,  6, 0921", nullptr               }, // 2017-09-21 aka 1.13.1.4
	{ "0,  6, 0504", nullptr               }, // 2017-05-04 aka 1.13.?.?
	{ "0,  6, 0414", nullptr               }, // 2017-04-14 aka 1.13.0.16
	{ "0,  6, 0328", nullptr               }, // 2017-03-28 aka 1.13.0.13
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
			// calling dlsym appears to invalidate the dl_phdr_info ptr we got,
			// so make sure we copy the info locally before that happens
			static dl_phdr_info s_LibASIInfo;
			memcpy(&s_LibASIInfo, g_LibASIInfo, sizeof(s_LibASIInfo));
			g_LibASIInfo = &s_LibASIInfo;
			
		//	Msg(Color::WHITE, "g_LibASIInfo->dlpi_name: \"%s\"\n",                       g_LibASIInfo->dlpi_name);
		//	Msg(Color::WHITE, "g_LibASIInfo->dlpi_addr: 0x%016" PRIXPTR "\n", (uintptr_t)g_LibASIInfo->dlpi_addr);
			
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
							if (g_LibASIOffsets != nullptr) {
								s_Supported = true;
							} else {
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

inline auto& lin_XferLen        = GetRef<int >(".data:lin_XferLen");
inline auto& lin_XferCallbacked = GetRef<bool>(".bss:lin_XferCallbacked");

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
		
		// custom absolute jump thunk: uses 14 out of 16 available bytes in a .PLT section entry
		struct AbsJmpThunk
		{
			explicit AbsJmpThunk(uint64_t target) : Target(target) {}
			
			uint8_t  Jmp [6] { 0xFF, 0x25, 0x02, 0x00, 0x00, 0x00 }; // jmp [rip+0x2]
			uint8_t  Int3[2] { 0xCC, 0xCC };                         // int3; int3
			uint64_t Target;                                         // dq Target  <-- QWORD-aligned!
		};
		static_assert(sizeof(AbsJmpThunk) == 0x10);
		
		AbsJmpThunk thunk(m_HookFuncAddr);
		memcpy(m_PLTEntryPtr, &thunk, sizeof(thunk));
		
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
		
		if (mprotect(ptr, 0x10, prot) != 0) {
			Msg(Color::RED, "PLTHook(%s): mprotect(%p, 0x10, 0x%X) failed: %s\n", m_Name, ptr, prot, strerror(errno));
			exit(1); // <-- TODO: see if there's anything we can do to avoid this
		}
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
