#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <glob.h>

// Common patterns for USB serial modem ports on Linux and macOS
const char *PORT_PATTERNS[] = {
    "/dev/ttyACM*",       // Linux (Standard USB Modems)
    "/dev/ttyUSB*",       // Linux (Alternative USB-to-Serial interfaces)
    "/dev/tty.usbmodem*", // macOS (CDC/ACM USB ports)
    NULL
};

int configure_port(int fd) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        return -1;
    }

    // Set Baud Rate to 115200
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    // 8N1 Format with no echo and minimal overhead
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; 
    tty.c_iflag &= ~IGNBRK;                     
    tty.c_lflag = 0;                            
    tty.c_oflag = 0;                            
    tty.c_cc[VMIN]  = 0;                        
    tty.c_cc[VTIME] = 2;                        // Lower timeout for smoother execution

    tty.c_cflag |= (CLOCAL | CREAD);            
    tty.c_cflag &= ~(PARENB | PARODD);       
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;                    

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        return -1;
    }
    return 0;
}

void send_at_command(int fd, const char *cmd) {
    char buf[128];
    
    // Format the command string with Carriage Return and Line Feed
    snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    write(fd, buf, strlen(buf));
    
    // Brief pause for the modem hardware to ingest bytes
    usleep(150000); 

    // Silently flush/drain the incoming buffer to ignore any ERROR or BOOTING messages
    memset(buf, 0, sizeof(buf));
    while (read(fd, buf, sizeof(buf) - 1) > 0) {
        // Purposely empty loop to discard the phone's responses
    }
}

char *find_samsung_port() {
    glob_t glob_result;
    for (int i = 0; PORT_PATTERNS[i] != NULL; i++) {
        if (glob(PORT_PATTERNS[i], 0, NULL, &glob_result) == 0) {
            for (size_t j = 0; j < glob_result.gl_pathc; j++) {
                char *found_port = strdup(glob_result.gl_pathv[j]);
                globfree(&glob_result);
                return found_port;
            }
            globfree(&glob_result);
        }
    }
    return NULL;
}

int main() {
    printf("[*] Searching for connected Samsung device...\n");
    char *port_name = find_samsung_port();

    if (!port_name) {
        fprintf(stderr, "[-] No compatible active serial port detected.\n");
        fprintf(stderr, "    Make sure the phone is connected via USB cable.\n");
        return 1;
    }

    printf("[+] Detected port: %s\n", port_name);

    printf("\n[*] Press [ENTER] to reboot phone into Download Mode, or [Ctrl+C] to abort...\n");
    getchar(); 

    printf("[*] Opening port and sending payload...\n");
    int fd = open(port_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        fprintf(stderr, "[-] Could not open %s (Try running with 'sudo').\n", port_name);
        free(port_name);
        return 1;
    }

    fcntl(fd, F_SETFL, 0);

    if (configure_port(fd) < 0) {
        close(fd);
        free(port_name);
        return 1;
    }

    // Execution sequence with hidden output
    send_at_command(fd, "AT");          
    send_at_command(fd, "AT");          
    send_at_command(fd, "AT+FUS?");     
    send_at_command(fd, "ATE0");        // Disables echo on the modem side
    send_at_command(fd, "AT+FUN=9,0");   
    send_at_command(fd, "AT+PFALS=0"); 
    send_at_command(fd, "AT+REBOOT=4"); 

    printf("[+] Done! The device should now reboot into Download Mode.\n");

    close(fd);
    free(port_name);
    return 0;
}
