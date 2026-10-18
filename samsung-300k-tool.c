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
#include <dirent.h>
#include <signal.h>

/* ---- Platform guard ---------------------------------------------------- */

#ifdef __APPLE__
#error "This tool supports Linux only (it relies on sysfs for Samsung VID detection)."
#endif
#ifndef __linux__
#error "This tool supports Linux only (sysfs required for Samsung VID detection)."
#endif

/* Samsung USB Vendor ID. sysfs reports it lowercase, without 0x. */
#define SAMSUNG_VID "04e8"

/* How long to wait for the device to re-enumerate in Download Mode. */
#define DOWNLOAD_MODE_TIMEOUT_S 30

/* Common patterns for USB serial modem ports on Linux. */
static const char *PORT_PATTERNS[] = {
    "/dev/ttyACM*",  /* Linux: standard CDC/ACM USB modems */
    "/dev/ttyUSB*",  /* Linux: alternative USB-to-serial   */
    NULL
};

/* ---- Signal handling --------------------------------------------------- */

static volatile sig_atomic_t g_abort = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_abort = 1;
}

/* ---- Helpers ----------------------------------------------------------- */

/*
 * Check whether the tty device at `devpath` belongs to a Samsung device
 * by reading its USB vendor ID from sysfs.
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
 * Scan sysfs for a Samsung device in Download Mode (Odin).
 * Known Download Mode PIDs: 685d, 685e, 6860 (non-exhaustive).
 *
 * Returns 1 if found (and writes the PID into pid_out), 0 otherwise.
 */
static int detect_download_mode(char *pid_out, size_t pid_sz) {
    DIR *d = opendir("/sys/bus/usb/devices");
    if (!d) return 0;

    struct dirent *ent;
    int found = 0;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char path[PATH_MAX];
        char vid[16] = {0};
        char pid[16] = {0};

        snprintf(path, sizeof(path),
                 "/sys/bus/usb/devices/%s/idVendor", ent->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fscanf(f, "%15s", vid) != 1) { fclose(f); continue; }
        fclose(f);

        if (strcasecmp(vid, SAMSUNG_VID) != 0) continue;

        snprintf(path, sizeof(path),
                 "/sys/bus/usb/devices/%s/idProduct", ent->d_name);
        f = fopen(path, "r");
        if (!f) continue;
        if (fscanf(f, "%15s", pid) != 1) { fclose(f); continue; }
        fclose(f);

        if (strcasecmp(pid, "685d") == 0 ||
            strcasecmp(pid, "685e") == 0 ||
            strcasecmp(pid, "6860") == 0) {
            if (pid_out && pid_sz) {
                snprintf(pid_out, pid_sz, "%s", pid);
            }
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

/*
 * Wait for the device to re-enumerate in Download Mode.
 * Polls sysfs every 500 ms. Aborts on Ctrl+C.
 *
 * Returns:
 *   1 -> Download Mode detected (pid_out filled)
 *   0 -> aborted by user
 *  -1 -> timed out without detection
 */
static int wait_for_download_mode(char *pid_out, size_t pid_sz,
                                  int timeout_seconds) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* no SA_RESTART: let usleep be interrupted */

    struct sigaction old_sa;
    sigaction(SIGINT, &sa, &old_sa);

    g_abort = 0;

    int elapsed_ms = 0;
    const int step_ms = 500;
    const int timeout_ms = timeout_seconds * 1000;

    printf("[*] Waiting for Download Mode device (up to %d s, "
           "Ctrl+C to abort)...\n", timeout_seconds);

    while (elapsed_ms < timeout_ms) {
        if (g_abort) {
            sigaction(SIGINT, &old_sa, NULL);
            return 0;
        }

        if (detect_download_mode(pid_out, pid_sz)) {
            sigaction(SIGINT, &old_sa, NULL);
            return 1;
        }

        usleep(step_ms * 1000);
        elapsed_ms += step_ms;
    }

    sigaction(SIGINT, &old_sa, NULL);
    return -1;
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

    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                     | INLCR | IGNCR | ICRNL
                     | IXON | IXOFF | IXANY);

    tty.c_oflag &= ~OPOST;

    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

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
 * response. Returns 0 on success, -1 on write error (errno set).
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

    usleep(150000);

    memset(buf, 0, sizeof(buf));
    int guard = 0;
    while (guard++ < 32) {
        ssize_t r = read(fd, buf, sizeof(buf) - 1);
        if (r <= 0) break;
    }
    return 0;
}

/*
 * Return a strdup'd path to the first Samsung serial port found.
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

    /* Payload sequence. */
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

    int sent_any = 0;

    for (int i = 0; seq[i] != NULL; i++) {
        if (send_at_command(fd, seq[i]) < 0) {
            int is_disconnect = (errno == EIO ||
                                 errno == ENODEV ||
                                 errno == ENOENT);

            if (is_disconnect && sent_any) {
                /*
                 * The phone left the bus after at least one successful
                 * command. That's almost certainly Download Mode.
                 */
                printf("[+] Port disappeared after '%s' (%s).\n",
                       seq[i], strerror(errno));
                printf("[+] This probably means the phone restarted "
                       "into Download Mode.\n");

                char pid[16] = {0};
                int r = wait_for_download_mode(pid, sizeof(pid),
                                               DOWNLOAD_MODE_TIMEOUT_S);

                if (r == 1) {
                    printf("[+] Confirmed: device is in Download Mode "
                           "(04e8:%s).\n", pid);
                    close(fd);
                    free(port_name);
                    return 0;
                } else if (r == 0) {
                    printf("[!] Aborted by user. The phone probably "
                           "rebooted into Download Mode anyway.\n");
                    printf("    Check with: lsusb | grep 04e8\n");
                    close(fd);
                    free(port_name);
                    return 0;  /* port disappeared, still success */
                } else {
                    printf("[-] Timed out after %d s: no Download Mode "
                           "device detected.\n",
                           DOWNLOAD_MODE_TIMEOUT_S);
                    printf("    The phone may have failed to enter "
                           "Download Mode.\n");
                    printf("    Check with: lsusb | grep 04e8\n");
                    close(fd);
                    free(port_name);
                    return 1;
                }
            }

            if (is_disconnect) {
                fprintf(stderr, "[-] Port not usable (%s). "
                        "Check cable, driver, and permissions.\n",
                        strerror(errno));
            } else {
                fprintf(stderr, "[-] Write failed while sending '%s': %s\n",
                        seq[i], strerror(errno));
            }
            close(fd);
            free(port_name);
            return 1;
        }
        sent_any = 1;
    }

    /*
     * Full sequence sent without the port disappearing. On some models
     * (e.g. S8) the phone re-enumerates a few seconds after AT+REBOOT=4
     * rather than mid-sequence, so give it a chance to show up.
     */
    printf("[+] Done! The device should now reboot into Download Mode.\n");

    char pid[16] = {0};
    int r = wait_for_download_mode(pid, sizeof(pid),
                                   DOWNLOAD_MODE_TIMEOUT_S);

    if (r == 1) {
        printf("[+] Confirmed: device is in Download Mode "
               "(04e8:%s).\n", pid);
    } else if (r == 0) {
        printf("[!] Aborted by user.\n");
    } else {
        printf("[?] No Download Mode device detected within %d s.\n",
               DOWNLOAD_MODE_TIMEOUT_S);
        printf("    Check with: lsusb | grep 04e8\n");
    }

    close(fd);
    free(port_name);
    return 0;  /* sequence completed; phone should be rebooting */
}
