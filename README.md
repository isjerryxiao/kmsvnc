# kmsvnc

[![Build Status](https://drone.jerryxiao.cc/api/badges/Jerry/kmsvnc/status.svg)](https://drone.jerryxiao.cc/Jerry/kmsvnc)

## Introduction
A VNC server for DRM/KMS capable GNU/Linux devices.  
The goal is to simply have a universally working vncserver on X, wayland and even something like your linux VT.  
Currently in very early development stage.

## Notes
Intel made a great thing called CCS (Color Control Surface), however that won't work with kmsvnc. Please set `INTEL_DEBUG=noccs` globally, ideally in /etc/systemd/system.conf.d. Manpage is at `man 5 systemd-system.conf`. For example:
```
# /etc/systemd/system.conf.d/intel-no-ccs.conf 
[Manager]
DefaultEnvironment=INTEL_DEBUG=noccs
```
NixOS:
```
systemd.extraConfig = ''
  DefaultEnvironment=INTEL_DEBUG=noccs
''
```

If you plan to use the default vaapi driver for Intel and AMD GPUs, please make sure your vaapi configuration is working.  
Nvidia support is highly experimental (nvidia-legacy with drm enabled or nvidia-open). Only one X-TILED modifier is supported as of now.

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
