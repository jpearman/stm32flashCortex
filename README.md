stm32flashCortex
================

VEX cortex version of the stm32flash download software

Major changes as follows:
Ability to enter the user boot mode by either RTS toggling or
C9 commands.
Auto baud retry up to 4 times.
removal of reset code download to RAM, not necessary with the
refactoring of this file for easier reading
change of default baudrate to 115200, the only speed at which
this will work with the cortex.
addition of a "quiet" mode to skip debug and progress printing

Changes Oct 28 2013

Fixed bug in win32 open com port code, ports above COM9 were not
handled correctly.

Improved handling of initialization, the code is now even more
specific to the cortex.

Changed the read/write transfer status display to be more friendly
to the eclipse IDE.  Eclipse does not handle "/r" so status was
scrolling of the console.
