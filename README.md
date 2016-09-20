AndroidNativeHidMouse
=====================

Android speaks native USB HID mouse.
--------------------------------

----------

Connect the Android phone to the computer via USB and the phone acts
as a touchpad device. The phone speaks native USB HID mouse interface.
The computer sees an HID-compliant mouse plug in.

The test program android_hid_mouse_test has following functions.

                 720
    +--------------------+------+
    |                    | [C]	|
    |                    +------+
    |                           |
    |                           |
    |                           |
    |                           |
    |           [D]             |
    |                           |1280
    |                           |
    |                           |
    |                           |
    |                           |
    |                           |
    +-------------+-------------+
    |             |             |
    |     [A]     |     [B]     | 
    +-------------+-------------+


    [A, D]: Tapping in this area makes a left-button mouse click
    [B]: Tapping in this area makes a right-button mouse click
    [C]: Move finger to this area to exit mouse function
    [A, B, D]: Mouse movement area
    [A, D]: Touch and hold your finger for 1 second in this area to make a
        left-button mouse pressed (vibration alert). Move your another 
        finger to have a drag action. Drop action is made once your fingers
        are off the touchpad.

TODO:
Add HID USB configuration in system\core\rootdir\init.usb.rc and then
we can 'setprop sys.usb.config hid' to enable HID mouse interface and 
automatically run test program.

Have fun.
Hsinko Yu <hsinkoyu@gmail.com>
