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

add_executable(kmsvnc)
set(kmsvnc_SOURCES kmsvnc.c drm.c input.c keymap.c va.c drm_master.c)

include(CheckIncludeFiles)
CHECK_INCLUDE_FILES("linux/uinput.h;linux/dma-buf.h" HAVE_LINUX_API_HEADERS)
IF(NOT HAVE_LINUX_API_HEADERS)
  message(FATAL_ERROR "linux-api-headers not found")
ENDIF()

include(CheckSymbolExists)
check_symbol_exists(SYS_pidfd_getfd "sys/syscall.h" HAVE_LIBC_SYS_pidfd_getfd)
IF(NOT HAVE_LIBC_SYS_pidfd_getfd)
  message(WARNING "pidfd_getfd syscall not found, the --screen-blank options will be disabled")
  target_compile_options(kmsvnc PUBLIC -DDISABLE_KMSVNC_SCREEN_BLANK)
  list(REMOVE_ITEM kmsvnc_SOURCES drm_master.c)
ENDIF()
include(CMakePushCheckState)
cmake_push_check_state()
set(CMAKE_REQUIRED_INCLUDES ${LIBDRM_INCLUDEDIR}/libdrm) # can't do anything about that
set(CMAKE_REQUIRED_LIBRARIES ${LIBDRM_LIBRARIES})
check_symbol_exists(drmGetFormatName "xf86drm.h" HAVE_LIBDRM_drmGetFormatName)
cmake_pop_check_state()
IF(NOT HAVE_LIBDRM_drmGetFormatName)
  message(WARNING "drmGetFormatName not found, format name printing will be disabled")
  target_compile_options(kmsvnc PUBLIC -DDISABLE_KMSVNC_drmGetFormatName)
ENDIF()


target_sources(kmsvnc PUBLIC
    ${kmsvnc_SOURCES}
)
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
