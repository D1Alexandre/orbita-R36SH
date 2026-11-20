# Linux WebRTC dependencies

`build.sh` uses libpeer `9319aa434cb9e893faed0293ba9d2a21eca59c8b`, its pinned
mbedTLS 3.4.0, libsrtp 2.4.2 and usrsctp submodules, and the existing Orbita
nonblocking WebRTC patches and build. The `host` target means native Linux,
including AArch64; it does not impose an x86 instruction set.

`libpeer-green-nx.patch` is copied verbatim from
https://github.com/rmrf404/green-nx/blob/b52dd73d2bcdd043316a2623d5871dcbd2e5000b/deps/patches/libpeer-switch.patch
(green-nx, GPL-3.0). Only its `src/*` changes are applied; Switch build files
and submodule changes are excluded. It supplies the xCloud data channels,
SDP and RTCP helpers expected by Orbita. `patch_libpeer.py` is then applied
to a build copy, with exact anchors checked. The downloaded repositories
and static libraries stay outside the tracked source tree.
