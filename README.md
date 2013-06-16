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

