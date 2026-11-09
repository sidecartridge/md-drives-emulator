# Build settings for emumd's fork of Hatari, given to CMake by
# tools/dev/build_hatari_fork.sh through the CMAKE_TOOLCHAIN_FILE environment
# variable (emumd's build script takes no CMake options).
#
# An old XQuartz in /opt/X11 puts -I/opt/X11/include ahead of the other
# headers, and its png.h is libpng 1.5's: Hatari then runs against libpng 1.6
# and every screenshot fails ("Failed to save PNG screen dump"). Hatari does
# not need X11 on macOS, so CMake ignores that prefix.
list(APPEND CMAKE_IGNORE_PREFIX_PATH /opt/X11)
list(APPEND CMAKE_IGNORE_PATH /opt/X11/include /opt/X11/lib)

# SDL2 is found as /Library/Frameworks/SDL2.framework, but Hatari's build
# leaves that folder out of the binary's rpath, so it would only start with
# DYLD_FRAMEWORK_PATH set, which macOS strips when a program is started
# through /usr/bin/env (as emumd's mdfw is).
list(APPEND CMAKE_BUILD_RPATH /Library/Frameworks)
