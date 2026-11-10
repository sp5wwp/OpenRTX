/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <reent.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \internal
 * _write_r, write to a file. No console is available on this platform.
 */
int _write_r(struct _reent *ptr, int fd, const void *buf, size_t cnt)
{
    (void)fd;
    (void)buf;
    (void)cnt;

    ptr->_errno = EBADF;
    return -1;
}

/**
 * \internal
 * _read_r, read from a file. No console is available on this platform.
 */
int _read_r(struct _reent *ptr, int fd, void *buf, size_t cnt)
{
    (void)fd;
    (void)buf;
    (void)cnt;

    ptr->_errno = EBADF;
    return -1;
}

#ifdef __cplusplus
}
#endif
