The asi package wraps the ZWO ASI camera Linux SDK for Python using SWIG. This package has only been tested with Python 3. There's probably no reason it can't work with Python 2 but the developers are lazy and can't be bothered to test against multiple Python versions.

Installation:
1. Install the ZWO ASI SDK
2. Install numpy, preferably from the system package manager. For example, `sudo apt install python3-numpy`.
3. Run `python3 setup.py install` or `pip3 install .` from this directory

The API provided by this package matches the C API very closely with a few minor exceptions to be more Pythonic. For example, NumPy arrays were selected as the type for the data buffer returned by the API functions that retrieve data from the camera. This was found to be far more natural than attempting to awkwardly mimick the pattern of passing a pointer to a pre-allocated buffer to the function as would be done in C.
