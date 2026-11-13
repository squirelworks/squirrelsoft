/* link: SquirrelDebug's ways to a target on the desktop (POSIX): a TCP
 * connection (QEMU's -gdb tcp::1234), a serial line, or a QEMU it starts
 * itself. Original code. */
#ifndef LINK_H
#define LINK_H

#include <stddef.h>

#include "dbg.h"

typedef struct {
    int fd;
    long pid; /* the QEMU it started, or 0 */
} Link;

/* "HOST:PORT", ":PORT" or "PORT" (localhost). 0, or -1 with why in err. */
int link_tcp(Link *l, const char *where, char *err, size_t cap);
/* A serial device, 8N1 with no flow control, at baud (115200 when 0) */
int link_serial(Link *l, const char *dev, long baud, char *err, size_t cap);
/* Start cmd (a QEMU command line) with " -S -gdb tcp:127.0.0.1:PORT" added
 * for a free PORT, and connect to it */
int link_launch(Link *l, const char *cmd, char *err, size_t cap);
DbgIo link_io(Link *l);
/* Close the link, and end the QEMU it started */
void link_close(Link *l);

#endif
