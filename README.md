Microwindows on DirectFB (implementation by Allan Shin, twitter: @AllnShn)

This project modifies Nano-X so that the underlying windowing system uses DirectFB rather than the Nano-X drivers.  This allows Win32 API to be used as the application layer for DirectFB devices, effectively converting DirectFB's windowing system to Win32API.  This project also using DirectFB font API to replace FreeType drivers in Nano-X.

The Nano-X Window System (formerly known as Microwindows) is an Open Source project aimed at bringing the features of modern graphical windowing environments to smaller devices and platforms.  Nano-X implements the Win32 API, making it possible to program for windows graphical UI that can be displayed on other platforms.

DirectFB is a thin library that provides hardware graphics acceleration, input device handling and abstraction, integrated windowing system with support for translucent windows and multiple display layers, not only on top of the Linux Framebuffer Device. It is a complete hardware abstraction layer with software fallbacks for every graphics operation that is not supported by the underlying hardware. DirectFB adds graphical power to embedded systems and sets a new standard for graphics under Linux.
