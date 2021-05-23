#pragma once


// C++ only ====================================================================

#ifndef __cplusplus
#error
#endif

// =============================================================================


// OS and CPU Architecture =====================================================

// Linux    x86    UNSUPPORTED (maybe later)
// Linux    x64      SUPPORTED
// Linux    ARMv5  UNSUPPORTED (maybe later)
// Linux    ARMv6  UNSUPPORTED (maybe later)
// Linux    ARMv7    SUPPORTED (untested; Thumb mode transitions are evil)
// Linux    ARMv8    SUPPORTED (untested)
// Windows  x86    UNSUPPORTED (probably never)
// Windows  x64    UNSUPPORTED (probably never)
// MacOS    x86    UNSUPPORTED (probably never)
// MacOS    x64    UNSUPPORTED (probably never)

#if defined(__linux__)
	#if defined(__i386__)
		#define FIXER_X86 1
		#warning zwo_fixer does not support the x86 architecture (yet)!
		#warning functionality will be disabled!
	#elif defined(__x86_64__)
		#define FIXER_X64       1
		#define FIXER_SUPPORTED 1
	#elif defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH == 5
		#define FIXER_ARMV5 1
		#warning zwo_fixer does not support the ARMv5 architecture (yet)!
		#warning functionality will be disabled!
	#elif defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH == 6
		#define FIXER_ARMV6 1
		#warning zwo_fixer does not support the ARMv6 architecture (yet)!
		#warning functionality will be disabled!
	#elif defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH == 7
		#define FIXER_ARMV7 1
		#define FIXER_SUPPORTED 1
	#elif defined(__aarch64__) && defined(__ARM_ARCH) && __ARM_ARCH == 8
		#define FIXER_ARMV8     1
		#define FIXER_SUPPORTED 1
	#else
		#error zwo_fixer does not support whatever CPU architecture this is!
	#endif
#elif defined(__APPLE__)
	#error zwo_fixer does not support the MacOS platform!
#elif defined(_WIN32) || defined(_WIN64)
	#error zwo_fixer does not support the Windows platform!
#else
	#error zwo_fixer does not support whatever platform this is!
#endif

#ifndef FIXER_SUPPORTED
#define FIXER_SUPPORTED 0
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
//#include <sys/mman.h>

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

static const OffsetMap_t g_Offsets_v1_18 = {
#if FIXER_X64
	{ ".text:callbackUSBTransferComplete", 0x1A5C20 },
	{ ".got.plt:libusb_cancel_transfer",   0x434A90 },
	{ ".data:lin_XferLen",                 0x43DD40 },
	{ ".bss:lin_XferCallbacked",           0x515554 },
#elif FIXER_ARMV7
	{ ".text:callbackUSBTransferComplete", 0x124948 },
	{ ".got.plt:libusb_cancel_transfer",   0x176FD8 }, // actually in .got (there is no .got.plt)
	{ ".data:lin_XferLen",                 0x17CD90 },
	{ ".bss:lin_XferCallbacked",           0x21497C },
#elif FIXER_ARMV8
	{ ".text:callbackUSBTransferComplete", 0x183C00 },
	{ ".got.plt:libusb_cancel_transfer",   0x222190 },
	{ ".data:lin_XferLen",                 0x229E20 },
	{ ".bss:lin_XferCallbacked",           0x332198 },
#endif
};

static const OffsetMap_t g_Offsets_v1_17;

static const OffsetMap_t g_Offsets_v1_16_3 = {
#if FIXER_X64
	{ ".text:callbackUSBTransferComplete", 0x187A20 },
	{ ".got.plt:libusb_cancel_transfer",   0x3DECB0 },
	{ ".data:lin_XferLen",                 0x3E7580 },
	{ ".bss:lin_XferCallbacked",           0x437D14 },
#elif FIXER_ARMV7
	{ ".text:callbackUSBTransferComplete", 0x10E498 },
	{ ".got.plt:libusb_cancel_transfer",   0x13B964 }, // actually in .got (there is no .got.plt)
	{ ".data:lin_XferLen",                 0x141234 },
	{ ".bss:lin_XferCallbacked",           0x181DF0 },
#elif FIXER_ARMV8
	{ ".text:callbackUSBTransferComplete", 0x1664A0 },
	{ ".got.plt:libusb_cancel_transfer",   0x1D44C0 },
	{ ".data:lin_XferLen",                 0x1DB7F0 },
	{ ".bss:lin_XferCallbacked",           0x238B00 },
#endif
};

static const OffsetMap_t g_Offsets_v1_16_2;
static const OffsetMap_t g_Offsets_v1_16_1;
static const OffsetMap_t g_Offsets_v1_16_0;
static const OffsetMap_t g_Offsets_v1_15_0915;
static const OffsetMap_t g_Offsets_v1_15_0819;
static const OffsetMap_t g_Offsets_v1_15_0617;
static const OffsetMap_t g_Offsets_v1_15_0610;
static const OffsetMap_t g_Offsets_v1_15_0430;
static const OffsetMap_t g_Offsets_v1_14_1227;

static const OffsetMap_t g_Offsets_v1_14_1119 = {
#if FIXER_X64
	{ ".text:callbackUSBTransferComplete", 0x1514D0 },
	{ ".got.plt:libusb_cancel_transfer",   0x3993D0 },
	{ ".data:lin_XferLen",                 0x3A1540 },
	{ ".bss:lin_XferCallbacked",           0x3F1B74 },
#elif FIXER_ARMV7
	{ ".text:callbackUSBTransferComplete", 0x0E8118 },
	{ ".got.plt:libusb_cancel_transfer",   0x11460C }, // actually in .got (there is no .got.plt)
	{ ".data:lin_XferLen",                 0x119C10 },
	{ ".bss:lin_XferCallbacked",           0x15A740 },
#elif FIXER_ARMV8
	{ ".text:callbackUSBTransferComplete", 0x135760 },
	{ ".got.plt:libusb_cancel_transfer",   0x193FB0 },
	{ ".data:lin_XferLen",                 0x19ACF0 },
	{ ".bss:lin_XferCallbacked",           0x1F7EF0 },
#endif
};

static const OffsetMap_t g_Offsets_v1_14_0715 = {
#if FIXER_X64
	{ ".text:callbackUSBTransferComplete", 0x1402E0 },
	{ ".got.plt:libusb_cancel_transfer",   0x385390 },
	{ ".data:lin_XferLen",                 0x38D260 },
	{ ".bss:lin_XferCallbacked",           0x3DD854 },
#endif
};

static const OffsetMap_t g_Offsets_v1_14_0425;
static const OffsetMap_t g_Offsets_v1_14_0227;
static const OffsetMap_t g_Offsets_v1_13_0930;
static const OffsetMap_t g_Offsets_v1_13_0821;

static const OffsetMap_t g_Offsets_v0_07_0503 = {
#if FIXER_X64
	{ ".text:callbackUSBTransferComplete", 0x0FB750 },
	{ ".got.plt:libusb_cancel_transfer",   0x337DC8 },
	{ ".data:lin_XferLen",                 0x33F3E0 },
	{ ".bss:lin_XferCallbacked",           0x37B8D4 },
#endif
};

static const OffsetMap_t g_Offsets_v0_07_0118;
static const OffsetMap_t g_Offsets_v0_06_0921;
static const OffsetMap_t g_Offsets_v0_06_0504;
static const OffsetMap_t g_Offsets_v0_06_0414;
static const OffsetMap_t g_Offsets_v0_06_0328;

static const VersionMap_t g_KnownLibASIVersions = {
	{ "1, 18",       &g_Offsets_v1_18      }, // 2021-04-23
	{ "1, 17",       &g_Offsets_v1_17      }, // 2021-03-17
	{ "1, 16, 3, 0", &g_Offsets_v1_16_3    }, // 2020-12-31
	{ "1, 16, 2, 0", &g_Offsets_v1_16_2    }, // 2020-12-23
	{ "1, 16, 1, 0", &g_Offsets_v1_16_1    }, // 2020-12-18
	{ "1, 16, 0",    &g_Offsets_v1_16_0    }, // 2020-11-19
	{ "1, 15, 0915", &g_Offsets_v1_15_0915 }, // 2020-09-18
	{ "1, 15, 0819", &g_Offsets_v1_15_0819 }, // 2020-08-19-ish
	{ "1, 15, 0617", &g_Offsets_v1_15_0617 }, // 2020-06-17
	{ "1, 15, 0610", &g_Offsets_v1_15_0610 }, // 2020-06-10
	{ "1, 15, 0430", &g_Offsets_v1_15_0430 }, // 2020-04-30
	{ "1, 14, 1119", &g_Offsets_v1_14_1119 }, // 2019-11-19
	{ "1, 14, 0715", &g_Offsets_v1_14_0715 }, // 2019-07-15
	{ "1, 14, 0425", &g_Offsets_v1_14_0425 }, // 2019-04-25-ish
	{ "1, 14, 0227", &g_Offsets_v1_14_0227 }, // 2019-02-27
	{ "1, 13, 0930", &g_Offsets_v1_13_0930 }, // 2018-09-30
	{ "1, 13, 0821", &g_Offsets_v1_13_0821 }, // 2018-08-21
	{ "0,  7, 0503", &g_Offsets_v0_07_0503 }, // 2018-05-23 aka 1.13.0523
	{ "0,  7, 0118", &g_Offsets_v0_07_0118 }, // 2018-01-19 aka 1.13.1.12
	{ "0,  6, 0921", &g_Offsets_v0_06_0921 }, // 2017-09-21 aka 1.13.1.4
	{ "0,  6, 0504", &g_Offsets_v0_06_0504 }, // 2017-05-04 aka 1.13.?.?
	{ "0,  6, 0414", &g_Offsets_v0_06_0414 }, // 2017-04-14 aka 1.13.0.16
	{ "0,  6, 0328", &g_Offsets_v0_06_0328 }, // 2017-03-28 aka 1.13.0.13
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
							if (g_LibASIOffsets != nullptr && !g_LibASIOffsets->empty()) {
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

// NOTE: potentially consider using std::hardware_(con|de)structive_interference_size
//       to deduce L1D cache line size info

enum PLTHookMode : uint_fast8_t
{
	PLTHOOK_MANUAL  = (1 << 0), // don't automatically install after construction
	PLTHOOK_PERSIST = (1 << 1), // don't automatically uninstall after destruction
	
	PLTHOOK_DEFAULT = 0,
};

class PLTHook : public AutoInstanceList<PLTHook>
{
private:
#if FIXER_X64
	static constexpr size_t PLT_ENTRY_SIZE     = 16;
	static constexpr size_t PLT_ENTRY_ALIGN    =  4;
	static constexpr size_t GOT_PLT_SLOT_SIZE  =  8;
	static constexpr size_t GOT_PLT_SLOT_ALIGN =  8;
#elif FIXER_ARMV7
	static constexpr size_t PLT_ENTRY_SIZE     = 12;
	static constexpr size_t PLT_ENTRY_ALIGN    =  4;
	static constexpr size_t GOT_PLT_SLOT_SIZE  =  4;
	static constexpr size_t GOT_PLT_SLOT_ALIGN =  4;
#elif FIXER_ARMV8
	static constexpr size_t PLT_ENTRY_SIZE     = 16;
	static constexpr size_t PLT_ENTRY_ALIGN    = 16;
	static constexpr size_t GOT_PLT_SLOT_SIZE  =  8;
	static constexpr size_t GOT_PLT_SLOT_ALIGN =  8;
#endif
	
#if FIXER_SUPPORTED
	// TODO: use C++ <atomic>, if we can ever figure out how the hell to use it properly with pre-existent external pointers...
	static_assert(__atomic_always_lock_free(GOT_PLT_SLOT_SIZE, nullptr));
	// NOTE: ouch, we don't appear to pass the __atomic_always_lock_free test with GOT_PLT_SLOT_ALIGN :(
	
	static constexpr int MEM_ORDER = __ATOMIC_SEQ_CST;
#endif
	
public:
	template<typename F_RET, typename... F_PARAMS>
	PLTHook(const char *name, uintptr_t got_plt_slot_addr, F_RET (*hook_func)(F_PARAMS...), PLTHookMode mode = PLTHOOK_DEFAULT) :
		m_Name(name), m_GOTPLTSlotPtr((uintptr_t *)got_plt_slot_addr), m_HookFuncAddr((uintptr_t)hook_func), m_Mode(mode)
	{
		if (!IsLibASILoadedAndSupported()) return;
		
#if !FIXER_SUPPORTED
		Msg(Color::YELLOW, "PLTHook(%s): architecture not supported\n", m_Name);
		return;
#endif
		
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
	
#if FIXER_SUPPORTED
	void Install()
	{
		if (!IsLibASILoadedAndSupported()) return;
		
		if (m_Installed) return;
		m_Installed = true;
		
		// ATOMIC EXCHANGE OPERATION
		m_SlotBackup = __atomic_exchange_n(m_GOTPLTSlotPtr, m_HookFuncAddr, MEM_ORDER);
		
		Msg(Color::WHITE, "PLTHook(%s): installed\n", m_Name);
	}
	
	void Uninstall()
	{
		if (!IsLibASILoadedAndSupported()) return;
		
		if (!m_Installed) return;
		m_Installed = false;
		
		// ATOMIC STORE OPERATION
		__atomic_store_n(m_GOTPLTSlotPtr, m_SlotBackup, MEM_ORDER);
		
		Msg(Color::WHITE, "PLTHook(%s): uninstalled\n", m_Name);
	}
#else
	void Install()   {}
	void Uninstall() {}
#endif
	
private:
	const char *m_Name;
	uintptr_t  *m_GOTPLTSlotPtr;
	uintptr_t   m_HookFuncAddr;
	PLTHookMode m_Mode;
	
#if FIXER_SUPPORTED
	bool      m_Installed = false;
	uintptr_t m_SlotBackup;
#endif
};

// =============================================================================
