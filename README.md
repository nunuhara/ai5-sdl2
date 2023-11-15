ai5-sdl2
========

ai5-sdl2 is a cross-platform implementation of elf's "AI5Win" game engine.

Compatibility
-------------

This project is currently in the very early stages and cannot run any game at
present. I am focusing on the Windows port of YU-NO as the first supported
game.

Building
--------

First install the dependencies (corresponding Debian package in parentheses):

* meson (meson)
* libpng (libpng-dev)
* SDL2 (libsdl2-dev)
* SDL2\_ttf (libsdl2-ttf-dev)

Then build the ai5-sdl2 executable with meson,

    mkdir build
    meson build
    ninja -C Build

Finally install it to your system,

    ninja -C build install

Running
-------

You can run a game by passing the path to its game directory or .INI file to
the ai5-sdl2 executable.

    build/ai5 /path/to/game_directory
    build/ai5 /path/to/game_directory/AI5WIN.INI

Alternatively, run ai5-sdl2 from within the game directory,

    cd /path/to/game_directory
    ai5
