#pragma once

#include "kmsvnc.h"

#define DRM_IOCTL_MUST(...) do{ int e; if (e = drmIoctl(__VA_ARGS__)) KMSVNC_FATAL("DRM ioctl error %d on line %d\n", e, __LINE__); } while(0)
#define DRM_IOCTL_MAY(...) do{ int e; if (e = drmIoctl(__VA_ARGS__)) fprintf(stderr, "DRM ioctl error %d on line %d\n", e, __LINE__); } while(0)
#define DRM_R_IOCTL_MAY(...) do{ int e; if (e = ioctl(__VA_ARGS__)) fprintf(stderr, "DRM ioctl error %d on line %d\n", e, __LINE__); } while(0)


void drm_cleanup();
int drm_open();
int drm_vendors();
