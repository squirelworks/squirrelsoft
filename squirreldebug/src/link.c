/* link: TCP, serial and launched-QEMU links for SquirrelDebug (POSIX).
 * Original code. */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE /* B57600/B115200 and INADDR_LOOPBACK on macOS */
#define _POSIX_C_SOURCE 200809L
#include "link.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
int link_tcp(Link *l, const char *where, char *err, size_t cap) {
    (void)l;
    (void)where;
    snprintf(err, cap, "links are not built for Windows yet");
    return -1;
}
int link_serial(Link *l, const char *dev, long baud, char *err, size_t cap) {
    (void)baud;
    return link_tcp(l, dev, err, cap);
}
int link_launch(Link *l, const char *cmd, char *err, size_t cap) { return link_tcp(l, cmd, err, cap); }
static long nop_write(void *c, const void *p, size_t n) {
    (void)c, (void)p, (void)n;
    return -1;
}
static long nop_read(void *c, void *p, size_t n, int ms) {
    (void)c, (void)p, (void)n, (void)ms;
    return -1;
}
DbgIo link_io(Link *l) {
    DbgIo io = {l, nop_write, nop_read};
    return io;
}
void link_close(Link *l) { (void)l; }
#else

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int tcp_connect(const char *host, const char *port, char *err, size_t cap) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc) {
        snprintf(err, cap, "%s: %s", host, gai_strerror(rc));
        return -1;
    }
    int fd = -1, e = 0;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        e = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        snprintf(err, cap, "%s:%s: %s", host, port, strerror(e ? e : ECONNREFUSED));
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

int link_tcp(Link *l, const char *where, char *err, size_t cap) {
    char host[256] = "localhost", port[16];
    const char *colon = strrchr(where, ':');
    if (colon) {
        size_t n = (size_t)(colon - where);
        if (n >= sizeof host) n = sizeof host - 1;
        if (n) {
            memcpy(host, where, n);
            host[n] = 0;
        }
        snprintf(port, sizeof port, "%s", colon + 1);
    } else
        snprintf(port, sizeof port, "%s", where);
    if (!port[0] || strspn(port, "0123456789") != strlen(port)) {
        snprintf(err, cap, "%s: give HOST:PORT", where);
        return -1;
    }
    l->pid = 0;
    l->fd = tcp_connect(host, port, err, cap);
    return l->fd < 0 ? -1 : 0;
}

static speed_t baud_of(long b) {
    switch (b) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
#ifdef B230400
    case 230400: return B230400;
#endif
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    }
    return 0;
}

int link_serial(Link *l, const char *dev, long baud, char *err, size_t cap) {
    struct termios t;
    speed_t sp = baud_of(baud ? baud : 115200);
    if (!sp) {
        snprintf(err, cap, "%ld: not a baud rate (9600 to 921600)", baud);
        return -1;
    }
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        snprintf(err, cap, "%s: %s", dev, strerror(errno));
        return -1;
    }
    if (tcgetattr(fd, &t) == 0) {
        t.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF);
        t.c_oflag &= ~(tcflag_t)OPOST;
        t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        t.c_cflag &= ~(tcflag_t)(CSIZE | PARENB | CSTOPB);
#ifdef CRTSCTS
        t.c_cflag &= ~(tcflag_t)CRTSCTS;
#endif
        t.c_cflag |= CS8 | CREAD | CLOCAL;
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        cfsetispeed(&t, sp);
        cfsetospeed(&t, sp);
        tcsetattr(fd, TCSANOW, &t);
        tcflush(fd, TCIOFLUSH);
    } /* not a terminal (a pipe or a pty pair end in tests): use it as it is */
    l->fd = fd;
    l->pid = 0;
    return 0;
}

static int free_port(void) {
    struct sockaddr_in a;
    socklen_t n = sizeof a;
    int fd = socket(AF_INET, SOCK_STREAM, 0), port = -1;
    if (fd < 0) return -1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) == 0 && getsockname(fd, (struct sockaddr *)&a, &n) == 0)
        port = ntohs(a.sin_port);
    close(fd);
    return port;
}

int link_launch(Link *l, const char *cmd, char *err, size_t cap) {
    int port = free_port();
    if (port < 0) {
        snprintf(err, cap, "no free port for QEMU");
        return -1;
    }
    size_t n = strlen(cmd) + 64;
    char *line = malloc(n);
    if (!line) {
        snprintf(err, cap, "out of memory");
        return -1;
    }
    snprintf(line, n, "exec %s -S -gdb tcp:127.0.0.1:%d", cmd, port);
    pid_t pid = fork();
    if (pid < 0) {
        free(line);
        snprintf(err, cap, "cannot start QEMU: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0) dup2(null, 0);
        execl("/bin/sh", "sh", "-c", line, (char *)NULL);
        _exit(127);
    }
    free(line);
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    for (int tries = 0; tries < 200; tries++) { /* up to ten seconds */
        int status;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            snprintf(err, cap, "QEMU ended before it could be reached (%s %d)", WIFEXITED(status) ? "exit" : "signal",
                     WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
            return -1;
        }
        int fd = tcp_connect("127.0.0.1", ps, err, cap);
        if (fd >= 0) {
            l->fd = fd;
            l->pid = pid;
            return 0;
        }
        struct timespec ts = {0, 50 * 1000000L};
        nanosleep(&ts, NULL);
    }
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    snprintf(err, cap, "QEMU did not open its gdb port");
    return -1;
}

static long lk_write(void *ctx, const void *p, size_t n) {
    Link *l = ctx;
    ssize_t w;
    do w = write(l->fd, p, n);
    while (w < 0 && errno == EINTR);
    return w < 0 ? -1 : (long)w;
}

static long lk_read(void *ctx, void *p, size_t n, int ms) {
    Link *l = ctx;
    struct pollfd pf = {l->fd, POLLIN, 0};
    int r;
    do r = poll(&pf, 1, ms);
    while (r < 0 && errno == EINTR);
    if (r < 0) return -1;
    if (r == 0) return 0;
    ssize_t k;
    do k = read(l->fd, p, n);
    while (k < 0 && errno == EINTR);
    if (k == 0 && (pf.revents & (POLLHUP | POLLERR | POLLIN))) return -1; /* closed */
    return k < 0 ? (errno == EAGAIN ? 0 : -1) : (long)k;
}

DbgIo link_io(Link *l) {
    DbgIo io = {l, lk_write, lk_read};
    return io;
}

void link_close(Link *l) {
    if (l->fd >= 0) close(l->fd);
    l->fd = -1;
    if (l->pid > 0) {
        int status;
        for (int i = 0; i < 40; i++) { /* the debugger's "kill" may already have ended it */
            if (waitpid((pid_t)l->pid, &status, WNOHANG) == (pid_t)l->pid) {
                l->pid = 0;
                return;
            }
            struct timespec ts = {0, 25 * 1000000L};
            nanosleep(&ts, NULL);
        }
        kill((pid_t)l->pid, SIGTERM);
        waitpid((pid_t)l->pid, &status, 0);
        l->pid = 0;
    }
}
#endif
