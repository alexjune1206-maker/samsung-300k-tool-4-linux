/*
 * samsung-300k-tool.c
 *
 * Forces connected Samsung Galaxy devices into Download Mode (Odin)
 * via AT commands over USB serial interfaces.
 *
 * Linux only: relies on sysfs for Samsung VID detection.
 *
 * License: GNU General Public License v3.0 (see LICENSE file).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <glob.h>
#include <errno.h>
#include <limits.h>

/* ---- Platform guard ---------------------------------------------------- */

#ifdef __APPLE__
#error "This tool supports Linux only (it relies on sysfs for Samsung VID detection)."
#endif
#ifndef __linux__
#error "This tool supports Linux only (sysfs required for Samsung VID detection)."
#endif

/* Samsung USB Vendor ID. sysfs reports it lowercase, without 0x. */
#define SAMSUNG_VID "04e8"

/* Common patterns for USB serial modem ports on Linux. */
static const char *PORT_PATTERNS[] = {
    "/dev/ttyACM*",  /* Linux: standard CDC/ACM USB modems */
    "/dev/ttyUSB*",  /* Linux: alternative USB-to-serial   */
    NULL
};

/* ---- Helpers ----------------------------------------------------------- */

/*
 * Check whether the tty device at `devpath` belongs to a Samsung device
 * by reading its USB vendor ID from sysfs.
 *
 * For /dev/ttyACM0 we try:
 *   /sys/class/tty/ttyACM0/device/../idVendor
 *   /sys/class/tty/ttyACM0/device/idVendor
 *
 * Returns:
 *   1  -> matches Samsung VID
 *   0  -> does not match
 *  -1  -> cannot determine (missing sysfs entry, etc.)
 */
static int is_samsung_port(const char *devpath) {
    const char *base = strrchr(devpath, '/');
    if (!base) return -1;
    base++; /* skip '/' */

    char syspath[PATH_MAX];

    snprintf(syspath, sizeof(syspath),
             "/sys/class/tty/%s/device/../idVendor", base);

    FILE *f = fopen(syspath, "r");
    if (!f) {
        snprintf(syspath, sizeof(syspath),
                 "/sys/class/tty/%s/device/idVendor", base);
        f = fopen(syspath, "r");
        if (!f) return -1;
    }

    char vid[16] = {0};
    if (fscanf(f, "%15s", vid) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);

    return (strcasecmp(vid, SAMSUNG_VID) == 0) ? 1 : 0;
}

/*
 * Configure the serial port: 115200 8N1, raw mode, no flow control,
 * non-blocking read with a short inter-byte timeout.
 */
static int configure_port(int fd) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        return -1;
    }

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    /* 8N1, enable receiver, ignore modem control lines */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    /* Raw input: no CR/NL translation, no software flow control */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                     | INLCR | IGNCR | ICRNL
                     | IXON | IXOFF | IXANY);

    /* Raw output */
    tty.c_oflag &= ~OPOST;

    /* Raw local mode */
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    /* Non-blocking read, 200 ms inter-byte timeout */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 2;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return 0;
}

/*
 * Send an AT command followed by CRLF, wait briefly, then drain any
 * response from the modem. Returns 0 on success, -1 on write error.
 */
static int send_at_command(int fd, const char *cmd) {
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    if (len < 0) return -1;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;

    ssize_t w = write(fd, buf, (size_t)len);
    if (w != (ssize_t)len) {
        return -1;
    }

    /* Give the modem a moment to ingest the bytes */
    usleep(150000);

    /* Drain incoming data without blocking forever */
    memset(buf, 0, sizeof(buf));
    int guard = 0;
    while (guard++ < 32) {
        ssize_t r = read(fd, buf, sizeof(buf) - 1);
        if (r <= 0) break;
    }
    return 0;
}

/*
 * Scan the known device patterns and return a strdup'd path to the first
 * port whose sysfs idVendor matches Samsung. Returns NULL if none found.
 *
 * Ports whose VID cannot be determined are skipped (safe default).
 */
static char *find_samsung_port(void) {
    for (int i = 0; PORT_PATTERNS[i] != NULL; i++) {
        glob_t glob_result;
        memset(&glob_result, 0, sizeof(glob_result));

        int rc = glob(PORT_PATTERNS[i], 0, NULL, &glob_result);
        if (rc == GLOB_NOMATCH) {
            globfree(&glob_result);
            continue;
        }
        if (rc != 0) {
            globfree(&glob_result);
            continue;
        }

        char *found = NULL;
        for (size_t j = 0; j < glob_result.gl_pathc; j++) {
            const char *path = glob_result.gl_pathv[j];
            if (is_samsung_port(path) == 1) {
                found = strdup(path);
                break;
            }
        }

        globfree(&glob_result);
        if (found) return found;
    }
    return NULL;
}

/* ---- main -------------------------------------------------------------- */

int main(void) {
    printf("[*] Searching for connected Samsung device...\n");
    char *port_name = find_samsung_port();

    if (!port_name) {
        fprintf(stderr, "[-] No Samsung serial port detected.\n");
        fprintf(stderr, "    Make sure the phone is connected via USB cable\n");
        fprintf(stderr, "    and that you have permission to read /sys and /dev.\n");
        return 1;
    }

    printf("[+] Detected port: %s\n", port_name);

    printf("\n[*] Press [ENTER] to reboot phone into Download Mode, "
           "or [Ctrl+C] to abort...\n");
    if (getchar() == EOF) {
        free(port_name);
        return 1;
    }

    printf("[*] Opening port and sending payload...\n");
    int fd = open(port_name, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "[-] Could not open %s: %s\n",
                port_name, strerror(errno));
        fprintf(stderr, "    Try running with 'sudo'.\n");
        free(port_name);
        return 1;
    }

    if (configure_port(fd) < 0) {
        fprintf(stderr, "[-] Failed to configure %s: %s\n",
                port_name, strerror(errno));
        close(fd);
        free(port_name);
        return 1;
    }

    /* Payload sequence. Stop early if the port dies. */
    const char *seq[] = {
        "AT",
        "AT",
        "AT+FUS?",
        "ATE0",
        "AT+FUN=9,0",
        "AT+PFALS=0",
        "AT+REBOOT=4",
        NULL
    };

    for (int i = 0; seq[i] != NULL; i++) {
        if (send_at_command(fd, seq[i]) < 0) {
            fprintf(stderr, "[-] Write failed while sending '%s': %s\n",
                    seq[i], strerror(errno));
            close(fd);
            free(port_name);
            return 1;
        }
    }

    printf("[+] Done! The device should now reboot into Download Mode.\n");

    close(fd);
    free(port_name);
    return 0;
}
