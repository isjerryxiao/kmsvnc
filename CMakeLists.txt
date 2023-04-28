cmake_minimum_required(VERSION 3.13)
project(kmsvnc LANGUAGES C)

IF(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_search_module(LIBDRM REQUIRED libdrm)
pkg_search_module(LIBVNCSERVER REQUIRED libvncserver)
pkg_search_module(XKBCOMMON REQUIRED xkbcommon)
pkg_search_module(LIBVA REQUIRED libva)
pkg_search_module(LIBVA_DRM REQUIRED libva-drm)

include(CheckIncludeFiles)
CHECK_INCLUDE_FILES("linux/uinput.h;linux/dma-buf.h" HAVE_LINUX_API_HEADERS)
IF(NOT HAVE_LINUX_API_HEADERS)
  message(FATAL_ERROR "linux-api-headers not found")
ENDIF()

add_executable(kmsvnc kmsvnc.c drm.c input.c keymap.c va.c)
target_include_directories(kmsvnc PUBLIC
  ${LIBDRM_INCLUDEDIR}
  ${LIBDRM_INCLUDEDIR}/libdrm
  ${LIBVNCSERVER_INCLUDEDIR}
  ${XKBCOMMON_INCLUDEDIR}
  ${LIBVA_INCLUDEDIR}
  ${LIBVA_DRM_INCLUDEDIR}
)
target_link_libraries(kmsvnc PUBLIC
  m
  ${LIBDRM_LIBRARIES}
  ${LIBVNCSERVER_LIBRARIES}
  ${XKBCOMMON_LIBRARIES}
  ${LIBVA_LIBRARIES}
  ${LIBVA_DRM_LIBRARIES}
)
install(TARGETS kmsvnc RUNTIME DESTINATION bin)
