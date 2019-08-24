%module asi
%{
#define SWIG_FILE_WITH_INIT
#include "ASICamera2.h"
%}

%include "numpy.i"

%init
%{
    import_array();
%}

/*
 * This applies a Numpy SWIG template to all C functions that have arguments matching this
 * pattern (argument types and names). The resulting Python functions will return a Numpy array.
 * This is meant to match the ASIGetVideoData and ASIGetDataAfterExp functions.
 */
%apply (unsigned char *ARGOUT_ARRAY1, int DIM1) {(unsigned char *pBuffer, long lBuffSize)};

%include "ASICamera2.h"
