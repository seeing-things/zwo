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


// INTERNAL error state information ============================================

enum LibraryError
{
	LIBERR_OK = 0,
	LIBERR_LIBASICAMERA2_NOTINMEMORY,   // failed to find libASICamera2.so in the list of loaded libraries reported by dl_iterate_phdr
	LIBERR_LIBASICAMERA2_OPENFAILED,    // failed to get a handle to libASICamera2.so via a call to dlopen, despite dl_iterate_phdr having indicated its presence
	LIBERR_ASIGETSDKVERSION_NOTFOUND,   // failed failed to locate the ASIGetSDKVersion function via a call to dlsym
	LIBERR_ASIGETSDKVERSION_RETNULLPTR, // got nullptr return value when calling ASIGetSDKVersion
	LIBERR_VERSION_NOTRECOGNIZED,       // version reported by ASIGetSDKVersion was not in our list of known library versions
	LIBERR_VERSION_NOTSUPPORTED,        // version reported by ASIGetSDKVersion was a known library version, however we haven't implemented support for that version
};

// after at least one call has been made to IsLibASILoadedAndSupported():
// - if everything is fine, then this will remain LIBERR_OK
// - if something went wrong, then this will be set to the enum value corresponding to what went wrong
//   (the first error encountered will halt the load/support checking process, so only one error will ever be relevant)
static inline g_LibError = LIBERR_OK;

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

#error look into whether we can safely close g_LibASIHandle upon .fini / [[gnu::destructor]] instead of just leaking the handle

static inline const dl_phdr_info *g_LibASIInfo    = nullptr;
static inline       void         *g_LibASIHandle  = nullptr;
static inline const char         *g_LibASIVersion = nullptr;
static inline const OffsetMap_t  *g_LibASIOffsets = nullptr;

#error rework this function so that ON EVERY RUN, it re-ensures that libASICamera2 is still in memory (so that if we UNLOAD libraries in the wrong order, we don't explode things)
[[gnu::noinline]]
static inline bool IsLibASILoadedAndSupported()
{
	// only run these checks upon the first call; otherwise, just return the cached value of g_LibError
	static bool s_FirstCall = true;
	if (s_FirstCall) {
		s_FirstCall = false;
		
		#error TODO: add another check (add at first position in the error enum) for if we find MORE THAN ONE instance of libASICamera2 in memory
		#error (and add an appropriate error message indicating how many instances we did find)
		dl_iterate_phdr(
			[](dl_phdr_info *info, size_t size, void *data) -> int {
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
								#error 
							}
						} else {
							Msg(Color::RED, "Init failure: library loaded, but version \"%s\" not recognized\n", g_LibASIVersion);
						}
					} else {
						g_LibError = LIBERR_ASIGETSDKVERSION_NOTFOUND;
						Msg(Color::RED, "Init failure: library loaded, but ASIGetSDKVersion returned nullptr\n");
					}
				} else {
					g_LibError = LIBERR_ASIGETSDKVERSION_NOTFOUND;
					Msg(Color::RED, "Init failure: library loaded, but ASIGetSDKVersion not found (dlsym)\n");
				}
			} else {
				g_LibError = LIBERR_LIBASICAMERA2_OPENFAILED;
				Msg(Color::RED, "Init failure: failed to load library (dlopen)\n");
			}
		} else {
			g_LibError = LIBERR_LIBASICAMERA2_NOTINMEMORY;
			Msg(Color::RED, "Init failure: failed to locate library in memory (dl_iterate_phdr)\n");
		}
		
	}
	
	return (g_LibError == LIBERR_OK);
}

static inline uintptr_t GetAddr(const std::string& name)
{
	if (!IsLibASILoadedAndSupported()) return 0;
	
	// this will attempt to throw if 'name' is not present in the g_LibASIOffsets map
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
#define STATIC_FUNC_SFINAE(T) std::enable_if_t<std::is_function_v<T> && !std::is_member_pointer_v<T>, T>

enum PLTHookMode : uint_fast8_t
{
	PLTHOOK_MANUAL  = (1 << 0), // don't automatically install after construction
	PLTHOOK_PERSIST = (1 << 1), // don't automatically uninstall after destruction
	
	PLTHOOK_DEFAULT = 0,
};

#error TODO: finish this!
#error TODO: make this a member variable of each PLTHook!
#error TODO: make it possible for the public-facing API to get a list of [name, error_bits] entries!
enum PLTHookErrors : uint_fast8_t
{
	PLTHOOK_ERROR_INSTALL_SETWRITABLE_FAIL     = (1 << 0), // TODO
	PLTHOOK_ERROR_INSTALL_SETEXECUTABLE_FAIL   = (1 << 1), // TODO
	
	PLTHOOK_ERROR_UNINSTALL_SETWRITABLE_FAIL   = (1 << 2), // TODO
	PLTHOOK_ERROR_UNINSTALL_SETEXECUTABLE_FAIL = (1 << 3), // TODO
	
	PLTHOOK_ERROR_NONE = 0,
};

class PLTHook : public AutoInstanceList<PLTHook>
{
public:
	template<typename T>
	PLTHook(const char *name, uintptr_t plt_entry_addr, STATIC_FUNC_SFINAE(T) hook_func, PLTHookMode mode = PLTHOOK_DEFAULT) :
		m_Name(name), m_PLTEntryPtr((uint8_t *)plt_entry_addr), m_HookFuncAddr((uint64_t)hook_func), m_Mode(mode)
	{
	//	// non-SFINAE sanity check for disallowing hook func ptrs that are member functions
	//	static_assert(std::is_function_v<T> && !std::is_member_pointer_v<T>);
		
		if (!IsLibASILoadedAndSupported()) return;
		
		Msg(Color::WHITE, "PLTHook(%s): constructed\n", m_Name);
		if ((mode & PLTHOOK_MANUAL) == 0) {
			Install();
		}
	}
	
	PLTHook(PLTHook&) = delete;
	
	~PLTHook()
	{
		#error TODO: do a library-unload-time check here ~~instead~~ in addition: ensure that libASICamera2 is STILL THERE and STILL AT THE SAME ADDRESS AS BEFORE!
		if (!IsLibASILoadedAndSupported()) return;
		
		if ((m_Mode & PLTHOOK_PERSIST) == 0) {
			Uninstall();
		}
		Msg(Color::WHITE, "PLTHook(%s): destructed\n", m_Name);
	}
	
	void Install()
	{
		if (!IsLibASILoadedAndSupported()) return;
		if (m_Installed)                   return;
		
		if (InErrorState()) {
			Msg(Color::RED, "PLTHook(%s): cannot install: already in error state", m_Name);
			return;
		}
		
		if (!SetWritable()) {
			Msg(Color::RED, "PLTHook(%s): installation cannot proceed: failed to set memory protection to RW-", m_Name);
			m_Errors |= PLTHOOK_ERROR_INSTALL_SETWRITABLE_FAIL;
			return;
		}
		
		Backup();
		
		// custom absolute jump thunk: uses 14 out of 16 available bytes in a .PLT section entry
		struct AbsJmpThunk
		{
			explicit AbsJmpThunk(uint64_t target) : Target(target) {}
			
			uint8_t  Jmp [6] { 0xFF, 0x25, 0x02, 0x00, 0x00, 0x00 }; // +0x00  jmp [rip+0x2]
			uint8_t  Int3[2] { 0xCC, 0xCC };                         // +0x06  int3; int3
			uint64_t Target;                                         // +0x08  dq Target  <-- QWORD-aligned!
		};
		static_assert(sizeof(AbsJmpThunk) == 0x10);
		static_assert(offsetof(AbsJumpThunk, Target) == 0x08);
		
		AbsJmpThunk thunk(m_HookFuncAddr);
		memcpy(m_PLTEntryPtr, &thunk, sizeof(thunk));
		m_Installed = true;
		
		if (!SetExecutable()) {
			Msg(Color::RED, "PLTHook(%s): installation failed catastrophically: failed to revert memory protections from RW- to R-X", m_Name);
			m_Errors |= PLTHOOK_ERROR_INSTALL_SETEXECUTABLE_FAIL;
			return;
		}
		
		Msg(Color::WHITE, "PLTHook(%s): installed\n", m_Name);
	}
	
	void Uninstall()
	{
		if (!IsLibASILoadedAndSupported()) return;
		if (!m_Installed)                  return;
		
		if (InErrorState()) {
			Msg(Color::RED, "PLTHook(%s): cannot uninstall: already in error state", m_Name);
			return;
		}
		
		if (!SetWritable()) {
			Msg(Color::RED, "PLTHook(%s): uninstallation cannot proceed: failed to set memory protection to RW-", m_Name);
			m_Errors |= PLTHOOK_ERROR_UNINSTALL_SETWRITABLE_FAIL;
			return;
		}
		
		Restore();
		m_Installed = false;
		
		if (!SetExecutable()) {
			Msg(Color::RED, "PLTHook(%s): uninstallation failed catastrophically: failed to revert memory protections from RW- to R-X", m_Name);
			m_Errors |= PLTHOOK_ERROR_UNINSTALL_SETEXECUTABLE_FAIL;
			return;
		}
		
		Msg(Color::WHITE, "PLTHook(%s): uninstalled\n", m_Name);
	}
	
private:
	// strict W^X policy: R-X or RW- only; no RWX ever!
	bool SetWritable()   { return SetMemProt(PROT_READ | PROT_WRITE); }
	bool SetExecutable() { return SetMemProt(PROT_READ | PROT_EXEC);  }
	
	bool SetMemProt(int prot)
	{
		auto ptr = (void *)((uintptr_t)m_PLTEntryPtr & ~(PageSize() - 1));
		if (mprotect(ptr, 0x10, prot) == 0) return true;
		
		Msg(Color::RED, "PLTHook(%s): mprotect(%p, 0x10, 0x%X) failed: %s\n", m_Name, ptr, prot, strerror(errno));
		return false;
	}
	
	void Backup()        { memcpy(m_Backup, m_PLTEntryPtr, 0x10); }
	void Restore() const { memcpy(m_PLTEntryPtr, m_Backup, 0x10); }
	
	bool InErrorState() const { return (m_Errors != PLTHOOK_ERROR_NONE); }
	
	const char *m_Name;
	uint8_t *m_PLTEntryPtr;
	uint64_t m_HookFuncAddr;
	PLTHookMode m_Mode;
	
	uint8_t m_Backup[16];
	bool m_Installed = false;
	
	PLTHookErrors m_Errors = PLTHOOK_ERROR_NONE;
};

#undef STATIC_FUNC_SFINAE

// =============================================================================

#warning TODO: do some tests to ensure the following
// - put manual breakpoints (int3) into object ctors and dtors
// - test that init and fini breakpoints are hit in gdb with:
//   - a program linked against libzwo_fixer.so at link time with the linker
//   - a program that explicitly uses dlopen to load libzwo_fixer.so
//   - a program that explicitly uses dlopen to load libzwo_fixer.so and dlclose to unload libzwo_fixer.so
//   - programs that use dlopen/dlclose for both libASICamera2 and libzwo_fixer.so and which do things in the wrong order!
// - check that we break at appropriate times and that things work appropriately
