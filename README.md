## Build Dependencies

To compile the capture software, a C++ compiler is required. On Debian-based Linux distributions (e.g. Ubuntu), you will need the build-essential package for this.

The following libraries are required for building the `zwo_fixer` shim library (Debian package names in parentheses):
- librt (libc6-dev)
- libpthread (libc6-dev)
- libdl (libc6-dev)
- libbsd (libbsd-dev)
- libelf (libelf-dev)
- libusb-1.0 (libusb-1.0-0-dev)

The following libraries are required for building the `capture` program (Debian package names in parentheses):
- librt (libc6-dev)
- libpthread (libc6-dev)
- libusb-1.0 (libusb-1.0-0-dev)
- libopencv-core (libopencv-core-dev)
- libopencv-highui (libopencv-highui-dev)
- libopencv-imgproc (libopencv-imgproc-dev)

## Enabling Realtime Priorities for Non-Root Users

Generally you'll want to run the software with realtime priority to reduce the likelihood of the OS scheduler causing pauses that would result in dropped data.

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
