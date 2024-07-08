AI5-SDL2
========

AI5-SDL2 is a cross-platform implementation of elf's AI5WIN game engine.

Compatibility
-------------

Only the games listed below are supported.

| Game                                             | ID         |
| ------------------------------------------------ | ---------- |
| この世の果てで恋を唄う少女YU-NO (エルフclassics) | yuno       |
| YU-NO (English translation patch)                | yuno-eng   |
| 同級生 Windows版                                 | doukyuusei |

Additional Features
-------------------

AI5-SDL2 adds the following features which are not present in the original
AI5WIN engine:

* The game window can be stretched to any size.
* Built-in text hooking features (see the `--texthook-clipboard` and
  `--texthook-stdout` command line options).
* Various issues caused by AI5WIN running with an unlocked frame rate are
  fixed (by limiting the frame rate).
  * The speed of transition effects can be adjusted with the
    `--transition-speed` command line option.
  * The speed of message skipping (with CTRL) can be adjusted with the
    `--msg-skip-delay` command line option.

Key Bindings
------------

The following keybindings can be used in-game.

| Key | Action                                                    |
| --- | --------------------------------------------------------- |
| =   | Increase window size to the next highest integer multiple |
| -   | Decrease window size to the next lowest integer multiple  |
| F10 | Take a screenshot                                         |
| F11 | Toggle fullscreen mode (borderless)                       |
| F12 | Open debugger REPL (debug builds only)                    |

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

Configuration
-------------

AI5-SDL2 can be configured using command line options or an INI file.
You can use the AI5WIN.INI file that came with the game, or create a new INI
named `AI5SDL2.INI`. Options for AI5-SDL2 should go under the `[AI5SDL2]`
section. E.g.

```ini
[AI5SDL2]
FONT=myfont.ttf
FONTFACE=1
TRANSITIONSPEED=0.5
MSGSKIPDELAY=8
NOWARPMOUSE=1
TEXTHOOKCLIPBOARD=1
TEXTHOOKSTDOUT=1
MAPNOWALLSLIDE=1
```

The available configuration options are as follows:

| INI Name          | Command Line Name      | Description                                   |
| ----------------- | ---------------------- | --------------------------------------------- |
| FONT              | `--font`               | Font to use                                   |
| FONTFACE          | `--font-face`          | Font face to use                              |
| MSGSKIPDELAY      | `--msg-skip-delay`     | Message skip delay time                       |
| NOWARPMOUSE       | `--no-warp-mouse`      | Disable automatic mouse movement              |
| TEXTHOOKCLIPBOARD | `--texthook-clipboard` | Copy text to the system clipboard             |
| TEXTHOOKSTDOUT    | `--texthook-stdout`    | Copy text to standard output                  |
| TRANSITIONSPEED   | `--cg-load-frame-time` | Speed of transition effects (lower is faster) |
| MAPNOWALLSLIDE    | `--map-no-wallslide`   | Disable sliding along walls (Doukyuusei only) |

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
* ffmpeg (libavcodec-dev) (optional)

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
