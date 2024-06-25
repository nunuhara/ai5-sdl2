AI5-SDL2
========

AI5-SDL2 is a cross-platform implementation of elf's AI5WIN game engine.

Compatibility
-------------

The only game supported so far is the "elf Classics" version of YU-NO
(including the English translated version).

Additional Features
-------------------

AI5-SDL2 adds the following features which are not present in the original
AI5WIN engine:

* The game window can be stretched to any size.
* The frame timing for animated CG loads is throttled so that the animation
  is actually visible on modern systems (AI5WIN draws as fast as the CPU can
  handle, leading to CGs popping in without any transition effect). This can
  be configured using the `--cg-load-frame-time` option.
* A small delay is added when skipping messages with CTRL. This avoids the
  issue in AI5WIN where pressing CTRL can cause an entire scene to be skipped
  almost instantly. This can be configured using the `--msg-skip-delay` option.

Fonts
-----

AI5-SDL2 ships with suitable fonts on non-Windows platforms, but they are not
perfect. In particular, the font used for the English version of YU-NO gets
cut-off at the bottom on certain characters.

You may want to copy `msgothic.ttc` from a Windows installation and use it
instead of the default fonts. Custom fonts can be specified using the `--font`
option.

On Windows this is not a problem, as the system's `msgothic.ttc` is used by
default.

Building
--------

This repository uses git submodules. You must either pass the
`--recurse-submodules` option when cloning this repo, or initialize the
submodules manually:

    git submodule init
    git submodule update
    cd subprojects/libai5
    git submodule init
    git submodule update
    cd ../..

Then install the dependencies (corresponding Debian package in parentheses):

* meson (meson)
* libpng (libpng-dev)
* libsndfile (libsndfile-dev)
* SDL2 (libsdl2-dev)
* SDL2\_ttf (libsdl2-ttf-dev)

Then build the ai5 executable with meson:

    meson build
    ninja -C build

### Windows

ai5-sdl2 can be build on Windows using MSYS2.

First, install MSYS2, and then open the MINGW64 shell and run the following command,

    pacman -S nasm diffutils \
        mingw-w64-x86_64-gcc \
        mingw-w64-x86_64-meson \
        mingw-w64-x86_64-pkg-config \
        mingw-w64-x86_64-SDL2 \
        mingw-w64-x86_64-SDL2_ttf \
        mingw-w64-x86_64-libpng \
        mingw-w64-x86_64-libsndfile

To build with FFmpeg support, you should compile FFmpeg as a static library:

    git clone https://github.com/FFmpeg/FFmpeg.git
    cd FFmpeg
    git checkout n7.0.1
    ./configure --disable-everything \
        --enable-decoder=cinepak \
        --enable-decoder=indeo3 \
        --enable-decoder=pcm_s16le \
        --enable-demuxer=avi \
        --enable-demuxer=wav \
        --enable-protocol=file \
        --enable-filter=scale \
        --enable-static \
        --disable-shared \
        --extra-libs=-static \
        --extra-cflags=--static
    make
    make install
    cd ..
    export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"

Then build the xsystem4 executable with meson,

    mkdir build
    meson build
    ninja -C build

Installing
----------

If building from source, run:

    ninja -C build install

to install AI5-SDL2 to your system.

### Nix

Run the following command to install AI5-SDL2 to your user profile (note:
flakes must be enabled):

    nix profile install git+https://github.com/nunuhara/ai5-sdl2?submodules=1

### Windows

The recommended way to install on Windows is to copy the executable into the
game directory. Then you can simply double click ai5.exe to run the game.

Windows builds can be found on the [releases](https://github.com/nunuhara/ai5-sdl2/releases)
page.

Running
-------

You can run a game by passing the path to its game directory or .INI file to
the ai5 executable.

    build/ai5 /path/to/game_directory
    build/ai5 /path/to/game_directory/AI5WIN.INI

Alternatively, run AI5-SDL2 from within the game directory,

    cd /path/to/game_directory
    ai5
