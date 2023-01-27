This repository contains the ZWO ASI camera SDK along with some software that depends on this SDK.

# Installing the SDK

The [SDK](https://astronomy-imaging-camera.com/software-drivers) is provided by ZWO in the form of a header file and shared object files for Linux and Windows. Several versions of the SDK come bundled with this repository. Alternatively, the Linux version of the library can be installed on Debian-based systems (including Ubuntu) via a PPA as the `libasicamera2` package by following the steps in this section.

First, add the URL for the SDK PPA to a sources.list file:

    $ sudo bash -c 'echo "deb [trusted=yes] https://apt.fury.io/jgottula/ /" > /etc/apt/sources.list.d/jgottula.list'

Now it should be possible to update the package cache and install:

    $ apt update
    $ sudo apt install libasicamera2


# ZWO Fixer

`zwo_fixer` is a shim library which patches a bug in the ASI library. To compile a C++ compiler is required. On Debian-based Linux distributions (e.g. Ubuntu), you will need the `build-essential` package for this.

The following libraries are required for building the `zwo_fixer` shim library (Debian package names in parentheses):
- librt (libc6-dev)
- libpthread (libc6-dev)
- libdl (libc6-dev)
- libbsd (libbsd-dev)
- libelf (libelf-dev)
- libusb-1.0 (libusb-1.0-0-dev)

Once the dependencies are installed, run `make` from the `zwo_fixer/` subdirectory. This will generate `libzwo_fixer.so`.

See `zwo_fixer/zwo_fixer.hpp` for instructions on how to use it.


# Python Bindings

Python bindings for the ASI library are available in the python/ subdirectory as the `asi` Python package. This interface is generated using SWIG. This package has only been tested with Python 3. There's probably no reason it can't work with Python 2 but the developers are lazy and can't be bothered to test against multiple Python versions.

Installation:
1. Install the ZWO ASI SDK as described previously
2. Install some dependencies: `sudo apt install python3-numpy swig`.
3. Change to the python/ subdirectory in this repository
4. Run `python3 setup.py install` or `pip3 install .`

The API provided by this package matches the C API very closely with a few minor exceptions to be more Pythonic. For example, NumPy arrays were selected as the type for the data buffer returned by the API functions that retrieve data from the camera. This was found to be far more natural than attempting to awkwardly mimick the pattern of passing a pointer to a pre-allocated buffer to the function as would be done in C.


# Capture

`capture` is high-performance capture software compatible with the ASI library. Features include:

- Efficient memory and CPU resource management
- Real-time priorization of critical threads
- Writes raw camera data directly to disk in SER format
- Custom automatic gain control
- Live preview, implemented in a manner that minimizes likelihood of frame loss due to resource contention

To compile this software, a C++ compiler is required. On Debian-based Linux distributions (e.g. Ubuntu), you will need the `build-essential` package for this.

The following packages are required for building the `capture` program (Debian package names in parentheses):
- cmake (cmake)
- libbsd (libbsd-dev)
- libopencv-dev (libopencv-dev)
- libpthread (libc6-dev)
- librt (libc6-dev)
- libspdlog (libspdlog-dev)
- libusb-1.0 (libusb-1.0-0-dev)
- pkg-config (pkg-config)

Once the dependencies are installed, run the following commands starting from the `capture/` subdirectory:

```
mkdir build
cd build
cmake ..
make
```

This should generate a binary `capture/build/capture`. You can then optionally run `make install` to install it.

## Enabling Realtime Priorities for Non-Root Users

Generally you'll want to run the `capture` with realtime priority to reduce the likelihood of the OS scheduler causing pauses that would result in dropped data.

On Linux, running programs with realtime priority (using e.g. the `chrt` utility) is typically not allowed for non-root users, to prevent abuse of a shared system. As such, switching user to root or using `sudo` is necessary to run a program with RT priority. For dedicated systems, it's possible (and actually rather easy) to configure the system to allow non-root users to do so.

As root (or with `sudo`), create a new file named `/etc/security/limits.d/99-realtime-privileges.conf`.

To enable realtime privileges for **all** users on the system, put the following contents into the file:
```
* - rtprio  98
* - memlock unlimited
```

To enable realtime privileges just for users on the system who are members of a particular group (for this example, the group named `realtime`), then instead use these file contents:
```
@realtime - rtprio  98
@realtime - memlock unlimited
```

After this file has been created, users will need to log out and then log back in for the changes to take effect.

You can verify what the current policy is for a given user by running `ulimit -r` as that user (without `sudo`). If it shows `0`, then the user is only allowed to run processes with a maximum RT priority of 0; and since the actual allowable realtime priority levels on Linux are 1-99, this means the user is disallowed from using realtime priorities entirely. If the command instead shows `98`, then the user is allowed to run processes with a maximum RT priority of 98 (1 lower than the highest possible); and this value shows that the example configuration given above has taken effect.
