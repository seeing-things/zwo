#pragma once


// ZWO Fixer's externally visible API


extern "C"
{
	// Call this BEFORE calling any ZWO API functions!
	// Call this AFTER loading the ZWO shared library!
	//   (if you are opening it with dlopen rather than linking to it directly)
	// Returns true if fixes were able to be applied successfully
	// Returns false if there were problems applying some of the fixes
	bool ZWOFixerInit();
}
