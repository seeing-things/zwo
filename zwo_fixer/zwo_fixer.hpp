#pragma once


// ZWO Fixer's externally visible API


extern "C"
{
	// Call this BEFORE calling any ZWO API functions!
	// Returns true if fixes were able to be applied successfully
	// Returns false if there were problems applying some of the fixes
	bool ZWOFixerInit();
}
