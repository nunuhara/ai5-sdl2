Controller Input
================

AI5-SDL2 supports controller input. Options related to controller input can be
set in the INI file (either `AI5WIN.INI` or `AI5SDL2.INI`).

Disabling Controller Input
--------------------------

Controller input can be disabled by passing `--controller-disable` on the
command line, or by setting the `ENABLED` INI option to `0`. E.g.

    [CONTROLLER]
    ENABLED=0

Configuring the Analog Sticks
-----------------------------

The left and right analog sticks can be configured to either control the
cursor, emulate a D-pad, or they can be disabled altogether. E.g.

    [CONTROLLER]
    LEFTANALOG=CURSOR
    RIGHTANALOG=DPAD

or to disable both sticks:

    [CONTROLLER]
    LEFTANALOG=DISABLED
    RIGHTANALOG=DISABLED

Configuring the Cursor Movement Speed
-------------------------------------

The analog sticks are used to control the cursor. The stick position is polled
approximately 30 times per second, and the cursor speed represents the maximum
number of pixels that the cursor can move at each polling interval. By default
the cursor speed is 16, so the cursor can move up to 480 pixels per second in
each direction.

The cursor speed can be configured by passing the `--controller-cursor-speed`
option on the command line, or by setting the "CURSORSPEED" INI option. E.g.

    $ ai5 --controller-cursor-speed=32

or

    [CONTROLLER]
    CURSORSPEED=32

Configuring the Analog Stick Dead Zone
--------------------------------------

The analog stick dead zone can be set by passing the `--controller-dead-zone`
option on the command line, or by setting the `DEADZONE` INI option. E.g.

    $ ai5 --controller-dead-zone=0.2

or

    [CONTROLLER]
    DEADZONE=0.2

Valid values are numbers between 0.0 and 1.0. In the above examples, the
bottom 20% of the analog stick range is ignored. By default the dead zone is
0.15 (15%).

Changing Button Mappings
------------------------

Button mappings can be changed by editing the INI file. E.g.

    [CONTROLLER]
    A=ENTER
    B=ESCAPE
    X=CTRL
    Y=SPACE
    UP=UP
    DOWN=DOWN
    LEFT=LEFT
    RIGHT=RIGHT
    LEFTSHOULDER=PAGEUP
    RIGHTSHOULDER=PAGEDOWN

The available button names are:
* A
* B
* X
* Y
* BACK
* GUIDE
* START
* LEFTSTICK
* RIGHTSTICK
* LEFTSHOULDER
* RIGHTSHOULDER
* UP
* DOWN
* LEFT
* RIGHT
* MISC1
* PADDLE1
* PADDLE2
* PADDLE3
* PADDLE4
* TOUCHPAD
* LEFTTRIGGER
* RIGHTTRIGGER

The available mappings are:
* NONE
* ENTER
* ESCAPE
* UP
* DOWN
* LEFT
* RIGHT
* SHIFT
* CTRL
* SPACE
* BACKSPACE
* PAGEUP
* PAGEDOWN
