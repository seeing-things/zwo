%module asi
%{
#define SWIG_FILE_WITH_INIT
#include "ASICamera2.h"
%}

%include "carrays.i"
%include "numpy.i"
%include "typemaps.i"

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

/*
 * For ASIGetNumOfControls which returns an int by pointer in C
 */
%apply int *OUTPUT { int * piNumberOfControls };

/*
 * For ASIGetControlValue which returns a long and a bool enum by pointers
 */
%apply int *OUTPUT { long *plValue, int *pbAuto };

/*
 * For use with functions that expect an array to be passed in by pointer, such as ASIGetProductIDs
 */
%array_class(int, intArray);

%include "ASICamera2.h"

%inline
%{
    int GetNumProductIDs()
    {
        return ASIGetProductIDs(0);
    }
%}

/*
 * Redefine the Python version of ASIGetProductIDs to make it more Pythonic. Rather than requiring
 * the caller to call once to get the length, allocate an array of ints, and pass a pointer to that
 * array it just returns a list of ints.
 */
%pythoncode
%{
def ASIGetProductIDs():
    num_ids = _asi.GetNumProductIDs()
    id_array = intArray(num_ids)
    _asi.ASIGetProductIDs(id_array)
    id_list = []
    for i in range(num_ids):
        id_list.append(id_array[i])
    return id_list
%}
