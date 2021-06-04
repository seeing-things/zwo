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
 * For ASIGetROIFormat which returns several values by pointer
 */
%apply int *OUTPUT { int *piWidth, int *piHeight, int *piBin, int *pImg_type };

/*
 * For ASIGetStartPos which returns X and Y coordinates by pointer to int
 */
%apply int *OUTPUT { int *piStartX, int *piStartY };

/*
 * For ASIGetDroppedFrames which returns an int by pointer
 */
%apply int *OUTPUT { int *piDropFrames };

/*
 * For ASIGetExpStatus which returns status by pointer
 */
%apply int *OUTPUT { ASI_EXPOSURE_STATUS *pExpStatus }

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
 * Redefine the Python function definition for certain functions to make them more Pythonic or
 * just easier to use.
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

def ASIGetCameraProperty(camera_index):
    info = ASI_CAMERA_INFO()
    rtn = _asi.ASIGetCameraProperty(info, camera_index)
    return rtn, info

def ASIGetCameraPropertyByID(camera_id):
    info = ASI_CAMERA_INFO()
    rtn = _asi.ASIGetCameraPropertyByID(camera_id, info)
    return rtn, info

def ASIGetControlCaps(camera_id, control_index):
    caps = ASI_CONTROL_CAPS()
    rtn = _asi.ASIGetControlCaps(camera_id, control_index, caps)
    return rtn, caps

def ASIGetSupportedBins(camera_info):
    supported_bins = []
    for i in range(16):
        bin = camera_info.get_supported_bins(i);
        if bin == 0:
            break
        else:
            supported_bins.append(bin)
    return supported_bins

def ASIGetSupportedVideoFormats(camera_info):
    supported_formats = []
    for i in range(8):
        format = camera_info.get_supported_video_format(i)
        if format == _asi.ASI_IMG_END:
            break
        else:
            supported_formats.append(format)
    return supported_formats

class ASIError(Exception):
    """Raised by ASICheck() when the status code returned by an API call is not ASI_SUCCESS"""

def ASICheck(return_values):
    """Check status return code from ASICamera2 API calls for errors.

    This function can be used to eliminate the need for boilerplate if-statements after every API
    call to check for error conditions. Typical usage is to wrap an API call like so:
    exposure_status = asi.ASICheck(asi.ASIGetExpStatus(camera_info.CameraID))

    Args:
        return_values: The return value or values from a ASICamera2 API call.

    Returns:
        The return_values but with the status code removed. If the status code was the only return
        value from the API call, this function returns None.

    Raises:
        ASIError if the status code was something other than ASI_SUCCESS.
    """
    if isinstance(return_values, (tuple, list)):
        status_code = return_values[0]
        return_values = return_values[1:] if len(return_values) > 2 else return_values[1]
    else:
        status_code = return_values
        return_values = None

    if status_code != _asi.ASI_SUCCESS:
        raise ASIError('ASI return code: {}'.format(status_code))

    return return_values
%}

/*
 * Otherwise it's impossible to access the elements of these arrays from Python
 */
%extend _ASI_CAMERA_INFO
{
    int get_supported_bins(int index)
    {
        return self->SupportedBins[index];
    }

    ASI_IMG_TYPE get_supported_video_format(int index)
    {
        return self->SupportedVideoFormat[index];
    }
}
