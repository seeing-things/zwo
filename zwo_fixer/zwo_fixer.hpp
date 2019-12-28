#pragma once


// ZWO Fixer's externally visible API


extern "C"
{
	#error finish this
	enum ZWOFixerStatus
	{
		FIXER_OK = 0,
		FIXER_
	};
	
	// NOTES ON RUNTIME LINKING (dlopen, dlclose):
	// If you are using dlopen/dlclose to load libASICamera2 and/or libzwo_fixer at runtime,
	// there are several important considerations that you must make:
	// 1. 
	// 1. If runtime-loading libASICamera2, you 
	#error finish this documentation
	
	// IMPORTANT: Call this function BEFORE calling any ZWO API functions!
	//
	// IMPORTANT: Call this function AFTER loading the ZWO shared library!
	//   (note that this is only relevant if you are loading libASICamera2 at runtime with dlopen;
	//    if you are instead linking to libASICamera2 at build time with the linker, you _should_ be fine)
	//
	// Return value:
	// - 
	ZWOFixerStatus ZWOFixerInit();
	#error TODO: find all callers of this function and update them to the revised return value semantics!
}
