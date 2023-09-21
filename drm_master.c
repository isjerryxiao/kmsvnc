#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

#include "drm_master.h"

extern struct kmsvnc_data *kmsvnc;


static inline int clone_fd(pid_t pid, int target_fd) {
    int pidfd = syscall(SYS_pidfd_open, pid, 0);
    if (pidfd <= 0) {
        perror("pidfd_open");
        return -1;
    }
    int cloned = syscall(SYS_pidfd_getfd, pidfd, target_fd, 0);
    if (cloned <= 0) {
        perror("pidfd_getfd");
    }
    close(pidfd);
    return cloned;
}

static inline int cmp_fds(pid_t pid, const char *drm_pth) {
    char path[PATH_MAX+1];
    snprintf(path, PATH_MAX+1, "/proc/%d/fd", pid);

    struct dirent **fdlist;
    int count = scandir(path, &fdlist, NULL, versionsort);
    int ret = -1;
    if (count >= 0) {
        for (int n = 0; n < count; n++) {
            if (ret == -1 && fdlist[n]->d_type == DT_LNK) {
                char link_pth[PATH_MAX+1];
                char real_pth[PATH_MAX+1];
                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wformat-truncation"
                snprintf(link_pth, PATH_MAX+1, "%s/%s", path, fdlist[n]->d_name);
                #pragma GCC diagnostic pop
                memset(real_pth, 0, PATH_MAX+1);
                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wunused-result"
                realpath(link_pth, real_pth);
                #pragma GCC diagnostic pop
                if (!strncmp(real_pth, drm_pth, PATH_MAX)) {
                    int fd = atoi(fdlist[n]->d_name);
                    if (fd > 0) {
                        int cloned = clone_fd(pid, fd);
                        if (cloned > 0 && drmIsMaster(cloned)) {
                            ret = cloned;
                            if (kmsvnc->debug_enabled) {
                                fprintf(stderr, "found drm master pid=%d, fd=%d, cloned=%d\n", pid, fd, cloned);
                            }
                        }
                        else {
                            if (cloned > 0) close(cloned);
                        }
                    }
                }
            }
            free(fdlist[n]);
            fdlist[n] = NULL;
        }
        free(fdlist);
        fdlist = NULL;
    }
    return ret;
}

int drm_get_master_fd() {
    char drm_pth[PATH_MAX+1];
    memset(drm_pth, 0, PATH_MAX+1);
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-result"
    realpath(kmsvnc->card, drm_pth);
    #pragma GCC diagnostic pop

    struct dirent **proclist;
    int count = scandir("/proc", &proclist, NULL, versionsort);
    int ret = -1;
    if (count >= 0) {
        for (int n = 0; n < count; n++) {
            if (ret == -1 && proclist[n]->d_type == DT_DIR) {
                pid_t pid = (pid_t)atoi(proclist[n]->d_name);
                if (pid > 0) {
                    int cloned = cmp_fds(pid, drm_pth);
                    if (cloned > 0) {
                        ret = cloned;
                    }
                }
            }
            free(proclist[n]);
            proclist[n] = NULL;
        }
        free(proclist);
        proclist = NULL;
    }
    else {
        perror("open /proc");
    }
    return ret;
}
