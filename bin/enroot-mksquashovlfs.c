/*
 * Copyright (c) 2018-2019, NVIDIA CORPORATION. All rights reserved.
 */

#define _GNU_SOURCE
#include <err.h>
#include <paths.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "common.h"

#ifndef MOUNTPOINT
# define MOUNTPOINT "/mnt"
#endif

static struct capabilities_v3 caps;

static void
init_capabilities(void)
{
        CAP_INIT_V3(&caps);

        if (capget(&caps.hdr, caps.data) < 0)
                err(EXIT_FAILURE, "failed to get capabilities");

        CAP_FOREACH(&caps, n) {
                if (n == CAP_DAC_READ_SEARCH || n == CAP_DAC_OVERRIDE)
                        continue;
                CAP_CLR(&caps, permitted, n);
                CAP_CLR(&caps, effective, n);
                CAP_CLR(&caps, inheritable, n);
        }
        CAP_SET(&caps, permitted, CAP_SYS_ADMIN);
        CAP_SET(&caps, effective, CAP_SYS_ADMIN);

        if (capset(&caps.hdr, caps.data) < 0)
                err(EXIT_FAILURE, "failed to set capabilities");
}

int
main(int argc, char *argv[])
{
        char *mountopts = NULL;

        if (argc < 3) {
                printf("Usage: %s DIR[:DIR...] DEST [options]\n", argv[0]);
                return (0);
        }

        init_capabilities();

        /*
         * Ideally we would like to do this as an unprivileged user since some distributions support mounting
         * overlayfs inside a userns (e.g. Ubuntu). However, due to the lack of support for trusted xattrs,
         * opaque whiteouts would be silently ignored (cf. aufs2ovlfs).
         */
#if 0
        if (unshare_userns(false) < 0)
                err(EXIT_FAILURE, "failed to unshare user namespace");
#endif
        if (unshare(CLONE_NEWNS) < 0)
                err(EXIT_FAILURE, "failed to unshare mount namespace");
        if (mount(NULL, "/", NULL, MS_PRIVATE|MS_REC, NULL) < 0)
                err(EXIT_FAILURE, "failed to set mount propagation");

        if (asprintf(&mountopts, "lowerdir=%s", argv[1]) < 0 ||
            mount(NULL, MOUNTPOINT, "overlay", 0, mountopts) < 0)
                err(EXIT_FAILURE, "failed to mount overlay: %s", argv[1]);
        free(mountopts);

        CAP_CLR(&caps, permitted, CAP_SYS_ADMIN);
        CAP_CLR(&caps, effective, CAP_SYS_ADMIN);
        if (capset(&caps.hdr, caps.data) < 0 || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
                err(EXIT_FAILURE, "failed to drop privileges");

        argv[1] = (char *)MOUNTPOINT;
        if (execvpe("mksquashfs", argv, (char * const []){(char *)"PATH="_PATH_STDPATH, NULL}) < 0)
                err(EXIT_FAILURE, "failed to execute mksquashfs");
        return (0);
}
