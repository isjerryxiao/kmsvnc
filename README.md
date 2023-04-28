# kmsvnc

## Introduction
A VNC server for DRM/KMS capable GNU/Linux devices.  
The goal is to simply have a universally working vncserver on X, wayland and even something like kmscon.  
Currently in very early stage.

## Dependencies
 * cmake
 * libvncserver
 * libxkbcommon
 * libdrm
 * libva

## Building
```
mkdir build
cd build
cmake ..
make
```

## Running
Helps are available via `kmsvnc --help`.  
For example, `kmsvnc -p 5901 -b 0.0.0.0 -4 -d /dev/dri/card2`  
Note that no security is currently supported.
