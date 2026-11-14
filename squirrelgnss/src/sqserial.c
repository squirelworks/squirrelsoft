/* sqserial: a receiver's serial port, raw, or a recorded stream. Original code. */
#ifndef _WIN32
#define _DEFAULT_SOURCE /* the faster baud rates and CRTSCTS beside POSIX termios */
#endif
#include "sqrecv.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#ifdef _WIN32

static int configure_comm(int fd, int baud, const char *path, char *err, size_t cap) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(err, cap, "%s: not a serial port", path);
        return -1;
    }
    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    if (!GetCommState(h, &dcb)) {
        /* a recording (pipe or ordinary file): leave it as it is */
        return 0;
    }
    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(h, &dcb)) {
        snprintf(err, cap, "%s: cannot set baud %d", path, baud);
        return -1;
    }
    COMMTIMEOUTS to;
    memset(&to, 0, sizeof to);
    to.ReadIntervalTimeout = 0;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(h, &to);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 0;
}

int sqrx_serial_open(const char *path, int baud, int write, char *err, size_t cap) {
    if (!strcmp(path, "-")) {
        if (write) {
            snprintf(err, cap, "cannot configure a receiver on standard input");
            return -1;
        }
        /* CRT stdin defaults to text mode: 0x1A is EOF and CR/LF is translated,
         * which truncates or corrupts a binary UBX stream on Windows. */
        if (_setmode(0, _O_BINARY) == -1) {
            snprintf(err, cap, "standard input: %s", strerror(errno));
            return -1;
        }
        return 0;
    }
    int fd = open(path, write ? O_RDWR | O_BINARY : O_RDONLY | O_BINARY);
    if (fd < 0) {
        snprintf(err, cap, "%s: %s", path, strerror(errno));
        return -1;
    }
    if (configure_comm(fd, baud, path, err, cap)) {
        close(fd);
        return -1;
    }
    return fd;
}

long sqrx_serial_read(int fd, unsigned char *buf, size_t cap) {
    for (;;) {
        int n = read(fd, buf, (unsigned)cap);
        if (n < 0 && errno == EINTR) continue;
        return (long)n;
    }
}

int sqrx_serial_write(int fd, const unsigned char *buf, size_t n) {
    while (n > 0) {
        int w = write(fd, buf, (unsigned)n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        buf += w;
        n -= (size_t)w;
    }
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h != INVALID_HANDLE_VALUE) FlushFileBuffers(h);
    return 0;
}

void sqrx_serial_close(int fd) {
    if (fd > 0) close(fd);
}

#else /* POSIX */

static int speed_of(int baud, speed_t *out) {
    static const struct {
        int baud;
        speed_t s;
    } T[] = {{4800, B4800},   {9600, B9600},   {19200, B19200}, {38400, B38400}, {57600, B57600},
             {115200, B115200}, {230400, B230400},
#ifdef B460800
             {460800, B460800},
#endif
#ifdef B921600
             {921600, B921600},
#endif
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (T[i].baud == baud) {
            *out = T[i].s;
            return 0;
        }
    return -1;
}

int sqrx_serial_open(const char *path, int baud, int write, char *err, size_t cap) {
    if (!strcmp(path, "-")) {
        if (write) {
            snprintf(err, cap, "cannot configure a receiver on standard input");
            return -1;
        }
        return 0;
    }
    int fd = open(path, (write ? O_RDWR : O_RDONLY) | O_NOCTTY);
    if (fd < 0) {
        snprintf(err, cap, "%s: %s", path, strerror(errno));
        return -1;
    }
    if (!isatty(fd)) {
        if (write) {
            snprintf(err, cap, "%s is not a serial port", path);
            close(fd);
            return -1;
        }
        return fd; /* a recording */
    }
    speed_t sp;
    if (speed_of(baud, &sp)) {
        snprintf(err, cap, "unsupported baud rate %d", baud);
        close(fd);
        return -1;
    }
    struct termios t;
    if (tcgetattr(fd, &t)) {
        snprintf(err, cap, "%s: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    /* raw 8N1: no echo, no line editing, no translation, no flow control */
    t.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cflag &= ~(tcflag_t)(CSIZE | PARENB | CSTOPB);
#ifdef CRTSCTS
    t.c_cflag &= ~(tcflag_t)CRTSCTS;
#endif
    t.c_cflag |= CS8 | CREAD | CLOCAL;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (cfsetispeed(&t, sp) || cfsetospeed(&t, sp) || tcsetattr(fd, TCSANOW, &t)) {
        snprintf(err, cap, "%s: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    tcflush(fd, TCIFLUSH);
    return fd;
}

long sqrx_serial_read(int fd, unsigned char *buf, size_t cap) {
    for (;;) {
        ssize_t n = read(fd, buf, cap);
        if (n < 0 && errno == EINTR) continue;
        return (long)n;
    }
}

int sqrx_serial_write(int fd, const unsigned char *buf, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, buf, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        buf += w;
        n -= (size_t)w;
    }
    return tcdrain(fd) == 0 || errno == ENOTTY ? 0 : -1;
}

void sqrx_serial_close(int fd) {
    if (fd > 0) close(fd);
}

#endif
