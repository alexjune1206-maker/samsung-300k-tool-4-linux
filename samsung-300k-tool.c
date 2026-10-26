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
#include <sys/wait.h>
#include <getopt.h>

/* ---- Platform guard ---------------------------------------------------- */

#ifdef __APPLE__
#error "This tool supports Linux only (it relies on sysfs for Samsung VID detection)."
#endif
#ifndef __linux__
#error "This tool supports Linux only (sysfs required for Samsung VID detection)."
#endif

/* ---- Metadata ---------------------------------------------------------- */

#ifdef TOOL_VERSION_CMAKE
#define TOOL_VERSION TOOL_VERSION_CMAKE
#else
#define TOOL_VERSION "1.2.1"
#endif

#define TOOL_AUTHOR  "alexjune1206-maker"
#define TOOL_GITHUB  "https://github.com/alexjune1206-maker"
#define TOOL_CONTACT "alexjune1206@gmail.com"

/* ---- Defaults ---------------------------------------------------------- */

#define SAMSUNG_VID "04e8"
#define DOWNLOAD_MODE_TIMEOUT_S 30
#define MAX_PORTS 16
#define SERIAL_MATCH_GRACE_S 5

static const char *PORT_PATTERNS[] = {
    "/dev/ttyACM*",
    "/dev/ttyUSB*",
    NULL
};

/* ---- Options ----------------------------------------------------------- */

struct options {
    int dry_run;
    int verbose;
    int timeout;
};

/* ---- Port list --------------------------------------------------------- */

struct port_list {
    char *paths[MAX_PORTS];
    char serials[MAX_PORTS][64];
    int count;
};

static void free_port_list(struct port_list *pl) {
    for (int i = 0; i < pl->count; i++) {
        free(pl->paths[i]);
        pl->paths[i] = NULL;
    }
    pl->count = 0;
}

/* ---- Signal handling --------------------------------------------------- */

static volatile sig_atomic_t g_abort = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_abort = 1;
}

/* ---- Usage / version --------------------------------------------------- */

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("Force connected Samsung Galaxy devices into Download Mode\n");
    printf("(Odin) via AT commands over USB serial interfaces.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help           Show this help and exit\n");
    printf("  -n, --dry-run        Detect devices and show the AT sequence\n");
    printf("                       without sending anything\n");
    printf("  -v, --verbose        Print modem responses for each command\n");
    printf("  -t, --timeout SEC    Seconds to wait for Download Mode\n");
    printf("                       (default: %d)\n", DOWNLOAD_MODE_TIMEOUT_S);
    printf("  -V, --version        Show version and exit\n");
    printf("\n");
    printf("Exit status:\n");
    printf("  0  success (all selected devices entered Download Mode)\n");
    printf("  1  failure (no device, or at least one device failed)\n");
    printf("  2  invalid command-line usage\n");
    printf("\n");
    printf("Made by %s\n", TOOL_AUTHOR);
    printf("GitHub:  %s\n", TOOL_GITHUB);
    printf("Contact: %s\n", TOOL_CONTACT);
}

static void print_version(void) {
    printf("samsung-300k-tool %s\n", TOOL_VERSION);
    printf("Made by %s\n", TOOL_AUTHOR);
    printf("GitHub:  %s\n", TOOL_GITHUB);
    printf("Contact: %s\n", TOOL_CONTACT);
}

/* ---- sysfs helpers ----------------------------------------------------- */

static int get_usb_serial(const char *devpath, char *out, size_t out_sz) {
    const char *base = strrchr(devpath, '/');
    if (!base) return -1;
    base++;

    char syspath[PATH_MAX];
    snprintf(syspath, sizeof(syspath),
             "/sys/class/tty/%s/device/../serial", base);

    FILE *f = fopen(syspath, "r");
    if (!f) return -1;

    if (!fgets(out, (int)out_sz, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
        out[--len] = '\0';
    }
    return (len > 0) ? 0 : -1;
}

static int is_samsung_port(const char *devpath) {
    const char *base = strrchr(devpath, '/');
    if (!base) return -1;
    base++;

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

/* ---- Download Mode detection ------------------------------------------- */

static int detect_download_mode_for_serial(const char *target_serial,
                                           char *pid_out, size_t pid_sz) {
    DIR *d = opendir("/sys/bus/usb/devices");
    if (!d) return 0;

    struct dirent *ent;
    int found = 0;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char path[PATH_MAX];
        char vid[16] = {0}, pid[16] = {0}, serial[64] = {0};

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

        if (strcasecmp(pid, "685d") != 0 &&
            strcasecmp(pid, "685e") != 0 &&
            strcasecmp(pid, "6860") != 0) continue;

        if (target_serial && target_serial[0] != '\0') {
            snprintf(path, sizeof(path),
                     "/sys/bus/usb/devices/%s/serial", ent->d_name);
            f = fopen(path, "r");
            if (!f) continue;
            if (!fgets(serial, sizeof(serial), f)) { fclose(f); continue; }
            fclose(f);

            size_t len = strlen(serial);
            while (len > 0 &&
                   (serial[len - 1] == '\n' || serial[len - 1] == '\r')) {
                serial[--len] = '\0';
            }

            if (strcmp(serial, target_serial) != 0) continue;
        }

        if (pid_out && pid_sz) snprintf(pid_out, pid_sz, "%s", pid);
        found = 1;
        break;
    }
    closedir(d);
    return found;
}

static int detect_download_mode_any(char *pid_out, size_t pid_sz) {
    return detect_download_mode_for_serial(NULL, pid_out, pid_sz);
}

static int wait_for_download_mode(const char *target_serial,
                                  char *pid_out, size_t pid_sz,
                                  int timeout_seconds) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    struct sigaction old_sa;
    sigaction(SIGINT, &sa, &old_sa);

    g_abort = 0;

    int elapsed_ms = 0;
    const int step_ms = 500;
    const int timeout_ms = timeout_seconds * 1000;
    const int grace_ms = SERIAL_MATCH_GRACE_S * 1000;

    printf("[*] Waiting for Download Mode (up to %d s, "
           "Ctrl+C to abort)...\n", timeout_seconds);

    while (elapsed_ms < timeout_ms) {
        if (g_abort) {
            sigaction(SIGINT, &old_sa, NULL);
            return 0;
        }

        int matched;
        if (target_serial && target_serial[0] != '\0' &&
            elapsed_ms < grace_ms) {
            matched = detect_download_mode_for_serial(target_serial,
                                                      pid_out, pid_sz);
        } else {
            matched = detect_download_mode_any(pid_out, pid_sz);
        }

        if (matched) {
            sigaction(SIGINT, &old_sa, NULL);
            return 1;
        }

        usleep(step_ms * 1000);
        elapsed_ms += step_ms;
    }

    sigaction(SIGINT, &old_sa, NULL);
    return -1;
}

/* ---- Serial port ------------------------------------------------------- */

static int configure_port(int fd) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) return -1;

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

    if (tcsetattr(fd, TCSANOW, &tty) != 0) return -1;

    tcflush(fd, TCIOFLUSH);
    return 0;
}

static int send_at_command(int fd, const char *cmd, int verbose) {
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    if (len < 0) return -1;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;

    if (verbose) printf("  >> %s\n", cmd);

    ssize_t w = write(fd, buf, (size_t)len);
    if (w != (ssize_t)len) return -1;

    usleep(150000);

    memset(buf, 0, sizeof(buf));
    int guard = 0;
    while (guard++ < 32) {
        ssize_t r = read(fd, buf, sizeof(buf) - 1);
        if (r <= 0) break;
        if (verbose && r > 0) {
            buf[r] = '\0';
            char *line = strtok(buf, "\r\n");
            while (line) {
                if (*line) printf("  << %s\n", line);
                line = strtok(NULL, "\r\n");
            }
        }
    }
    return 0;
}

/* ---- Port discovery ---------------------------------------------------- */

static void find_samsung_ports(struct port_list *pl) {
    pl->count = 0;

    for (int i = 0; PORT_PATTERNS[i] != NULL; i++) {
        glob_t glob_result;
        memset(&glob_result, 0, sizeof(glob_result));

        int rc = glob(PORT_PATTERNS[i], 0, NULL, &glob_result);
        if (rc != 0) {
            globfree(&glob_result);
            continue;
        }

        for (size_t j = 0; j < glob_result.gl_pathc; j++) {
            if (pl->count >= MAX_PORTS) break;

            const char *path = glob_result.gl_pathv[j];
            if (is_samsung_port(path) != 1) continue;

            int dup = 0;
            for (int k = 0; k < pl->count; k++) {
                if (strcmp(pl->paths[k], path) == 0) { dup = 1; break; }
            }
            if (dup) continue;

            int idx = pl->count;
            pl->paths[idx] = strdup(path);
            pl->serials[idx][0] = '\0';

            if (get_usb_serial(path, pl->serials[idx],
                               sizeof(pl->serials[idx])) != 0) {
                snprintf(pl->serials[idx], sizeof(pl->serials[idx]),
                         "unavailable");
            }

            pl->count++;
        }

        globfree(&glob_result);
    }
}

/* ---- Menu -------------------------------------------------------------- */

static int select_device(const struct port_list *pl) {
    printf("[*] Found %d Samsung device(s):\n", pl->count);

    for (int i = 0; i < pl->count; i++) {
        printf("    [%d] %s  (serial: %s)\n",
               i + 1, pl->paths[i], pl->serials[i]);
    }

    if (pl->count == 1) {
        printf("\n[*] Only one device found; selecting it automatically.\n");
        return 0;
    }

    printf("\nSelect device(s):\n");
    printf("  - Enter a number (1-%d) for one device\n", pl->count);
    printf("  - Enter 'a' for all devices\n");
    printf("  - Enter 'q' to quit\n");

    for (;;) {
        printf("\n> ");
        fflush(stdout);

        char line[64];
        if (!fgets(line, sizeof(line), stdin)) return -2;

        size_t len = strlen(line);
        while (len > 0 &&
               (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (len == 0) continue;
        if (line[0] == 'q' || line[0] == 'Q') return -2;
        if (line[0] == 'a' || line[0] == 'A') return -1;

        char *end = NULL;
        long n = strtol(line, &end, 10);
        if (end != line && *end == '\0' && n >= 1 && n <= pl->count) {
            return (int)(n - 1);
        }

        printf("Invalid input. Try again.\n");
    }
}

/* ---- Device processing ------------------------------------------------- */

static int process_device(const char *port_name, const char *hw_serial,
                          const struct options *opt) {
    printf("\n=== Processing %s (serial: %s) ===\n",
           port_name,
           (hw_serial && hw_serial[0]) ? hw_serial : "unavailable");

    const char *serial_arg =
        (hw_serial && strcmp(hw_serial, "unavailable") != 0) ? hw_serial : NULL;

    const char *seq[] = {
        "AT", "AT", "AT+FUS?", "ATE0",
        "AT+FUN=9,0", "AT+PFALS=0", "AT+REBOOT=4", NULL
    };

    if (opt->dry_run) {
        printf("[*] Dry-run: would send the following AT sequence:\n");
        for (int i = 0; seq[i] != NULL; i++) {
            printf("    %s\n", seq[i]);
        }
        printf("[*] Dry-run: would wait up to %d s for Download Mode.\n",
               opt->timeout);
        return 0;
    }

    int fd = open(port_name, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "[-] Could not open %s: %s\n",
                port_name, strerror(errno));
        return 1;
    }

    if (configure_port(fd) < 0) {
        fprintf(stderr, "[-] Failed to configure %s: %s\n",
                port_name, strerror(errno));
        close(fd);
        return 1;
    }

    int sent_any = 0;

    for (int i = 0; seq[i] != NULL; i++) {
        if (send_at_command(fd, seq[i], opt->verbose) < 0) {
            int is_disconnect = (errno == EIO ||
                                 errno == ENODEV ||
                                 errno == ENOENT);

            /* Fix: retry the first AT once if the port is still
             * initializing and returns EIO. */
            if (is_disconnect && !sent_any) {
                usleep(500000);
                if (send_at_command(fd, seq[i], opt->verbose) == 0) {
                    sent_any = 1;
                    continue;
                }
            }

            if (is_disconnect && sent_any) {
                printf("[+] Port disappeared after '%s' (%s).\n",
                       seq[i], strerror(errno));

                char pid[16] = {0};
                int r = wait_for_download_mode(serial_arg,
                                               pid, sizeof(pid),
                                               opt->timeout);

                if (r == 1) {
                    printf("[+] Confirmed %s in Download Mode "
                           "(04e8:%s).\n", port_name, pid);
                    close(fd);
                    return 0;
                } else if (r == 0) {
                    printf("[!] Aborted by user.\n");
                    close(fd);
                    return 0;
                } else {
                    printf("[-] Timed out waiting for %s.\n", port_name);
                    close(fd);
                    return 1;
                }
            }

            if (is_disconnect) {
                fprintf(stderr, "[-] Port not usable (%s).\n",
                        strerror(errno));
            } else {
                fprintf(stderr, "[-] Write failed on '%s': %s\n",
                        seq[i], strerror(errno));
            }
            close(fd);
            return 1;
        }
        sent_any = 1;
    }

    printf("[+] Sequence sent to %s.\n", port_name);

    char pid[16] = {0};
    int r = wait_for_download_mode(serial_arg, pid, sizeof(pid),
                                   opt->timeout);

    if (r == 1) {
        printf("[+] Confirmed %s in Download Mode (04e8:%s).\n",
               port_name, pid);
        close(fd);
        return 0;
    } else if (r == 0) {
        printf("[!] Aborted by user.\n");
        close(fd);
        return 0;
    }

    printf("[?] No Download Mode detected for %s within %d s.\n",
           port_name, opt->timeout);
    close(fd);
    return 1;
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char **argv) {
    struct options opt = {
        .dry_run = 0,
        .verbose = 0,
        .timeout = DOWNLOAD_MODE_TIMEOUT_S,
    };

    static struct option long_opts[] = {
        {"help",    no_argument,       0, 'h'},
        {"dry-run", no_argument,       0, 'n'},
        {"verbose", no_argument,       0, 'v'},
        {"timeout", required_argument, 0, 't'},
        {"version", no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "hnvt:V", long_opts, NULL)) != -1) {
        switch (c) {
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'n':
                opt.dry_run = 1;
                break;
            case 'v':
                opt.verbose = 1;
                break;
            case 't': {
                /* Fix: clearer message when --timeout has no value. */
                if (!optarg) {
                    fprintf(stderr, "Error: --timeout requires a value "
                            "(1-600 seconds)\n");
                    return 2;
                }
                char *end = NULL;
                long n = strtol(optarg, &end, 10);
                if (end == optarg || *end != '\0' || n < 1 || n > 600) {
                    fprintf(stderr, "Error: invalid timeout '%s' "
                            "(expected 1-600 seconds)\n", optarg);
                    return 2;
                }
                opt.timeout = (int)n;
                break;
            }
            case 'V':
                print_version();
                return 0;
            case '?':
                fprintf(stderr, "Try '%s --help' for more information.\n",
                        argv[0]);
                return 2;
            default:
                return 2;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "Error: unexpected argument '%s'\n", argv[optind]);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return 2;
    }

    if (isatty(STDOUT_FILENO)) {
        printf("samsung-300k-tool %s by %s\n\n",
               TOOL_VERSION, TOOL_AUTHOR);
    }

    printf("[*] Searching for connected Samsung devices...\n");

    struct port_list pl;
    find_samsung_ports(&pl);

    if (pl.count == 0) {
        fprintf(stderr, "[-] No Samsung serial port detected.\n");
        fprintf(stderr, "    Make sure the phone is connected via USB cable\n");
        fprintf(stderr, "    and that you have permission to read /sys and /dev.\n");
        return 1;
    }

    int sel = select_device(&pl);
    if (sel == -2) {
        printf("[*] Quit.\n");
        free_port_list(&pl);
        return 0;
    }

    if (!opt.dry_run) {
        printf("\n[*] Press [ENTER] to reboot into Download Mode, "
               "or [Ctrl+C] to abort...\n");
        if (getchar() == EOF) {
            free_port_list(&pl);
            return 1;
        }
    }

    int success = 0;
    int failed = 0;

    if (sel == -1) {
        printf("[*] Processing all %d device(s) in parallel...\n",
               pl.count);

        pid_t pids[MAX_PORTS];
        int npid = 0;

        for (int i = 0; i < pl.count; i++) {
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {
                int rc = process_device(pl.paths[i], pl.serials[i], &opt);
                fflush(stdout);
                _exit(rc);
            } else if (pid > 0) {
                pids[npid++] = pid;
            } else {
                perror("fork");
                failed++;
            }
        }

        for (int i = 0; i < npid; i++) {
            int status = 0;
            waitpid(pids[i], &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                success++;
            } else {
                failed++;
            }
        }
    } else {
        if (process_device(pl.paths[sel], pl.serials[sel], &opt) == 0) success++;
        else failed++;
    }

    printf("\n=== Summary ===\n");
    printf("    Successful: %d\n", success);
    if (failed) printf("    Failed:     %d\n", failed);

    free_port_list(&pl);
    return (failed == 0) ? 0 : 1;
}
