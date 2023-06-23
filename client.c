// gcc -o client client.c -lpthread -lwiringPi

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/i2c-dev.h>

#include <wiringPi.h>
#include <wiringSerial.h>

#define MSG_MAX 1024

// I2C
#define PULSE_PERIOD 500
#define CMD_PERIOD 4100
#define BACKLIGHT 8
#define DATA 1

static int iBackLight = BACKLIGHT;
static int file_i2c = -1;

// GPIO
#define BUFFER_MAX 3
#define LED_PIN 20
#define LIGHT_PIN 21
#define VALUE_MAX 40
#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1
#define GPIO_NUM 26 // speaker
unsigned int melody[] = {150, 0, 150, 0, 150}; // Frequencies in Hz (C4, pause, E4, pause, G4)
unsigned int durations[] = {2000, 100, 2000, 100, 2000}; // Durations in milliseconds

#define BUT1 17
#define BUT11 27

#define PIN3 19 //BUTTON1
#define POUT3 26 //BUTTON1
#define PIN2 6 //BUTTON2
#define POUT2 13 //BUTTON2
#define PIN1 23 //BUTTON3
#define POUT1 24 //BUTTON3

static int GPIOExport(int pin) {
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;
    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open export for writing!\n");
        return (-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int GPIODirection(int pin, int dir) {
    static const char s_directions_str[] = "in\0out";
#define DIRECTION_MAX 35
    char path[DIRECTION_MAX];
    int fd;
    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return (-1);
    }
    if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3],
                    IN == dir ? 2 : 3)) {
        fprintf(stderr, "Failed to set direction!\n");
        return (-1);
    }
    close(fd);
    return (0);
}

static int GPIORead(int pin) {
    char path[VALUE_MAX];
    char value_str[3];
    int fd;
    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return (-1);
    }
    if (-1 == read(fd, value_str, 3)) {
        fprintf(stderr, "Failed to read value!\n");
        return (-1);
    }
    close(fd);
    return (atoi(value_str));
}

static int GPIOWrite(int pin, int value) {
    static const char s_values_str[] = "01";
    char path[VALUE_MAX];
    int fd;
    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return (-1);
    }
    if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
        fprintf(stderr, "Failed to write value!\n");
        return (-1);
    }
    close(fd);
    return (0);
}

static int GPIOUnexport(int pin) {
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;
    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return (-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static void WriteCommand(unsigned char ucCMD) {
unsigned char uc;

    uc = (ucCMD & 0xf0) | iBackLight; // most significant nibble sent first
    write(file_i2c, &uc, 1);
    usleep(PULSE_PERIOD); // manually pulse the clock line
    uc |= 4; // enable pulse
    write(file_i2c, &uc, 1);
    usleep(PULSE_PERIOD);
    uc &= ~4; // toggle pulse
    write(file_i2c, &uc, 1);
    usleep(CMD_PERIOD);
    uc = iBackLight | (ucCMD << 4); // least significant nibble
    write(file_i2c, &uc, 1);
    usleep(PULSE_PERIOD);
        uc |= 4; // enable pulse
        write(file_i2c, &uc, 1);
        usleep(PULSE_PERIOD);
        uc &= ~4; // toggle pulse
        write(file_i2c, &uc, 1);
    usleep(CMD_PERIOD);

}

int lcd1602Control(int bBacklight, int bCursor, int bBlink) {
unsigned char ucCMD = 0xc; // display control

    if (file_i2c < 0)
        return 1;
    iBackLight = (bBacklight) ? BACKLIGHT : 0;
    if (bCursor)
        ucCMD |= 2;
    if (bBlink)
        ucCMD |= 1;
    WriteCommand(ucCMD);
     
    return 0;
}

// Write an ASCII string (up to 16 characters at a time)
int lcd1602WriteString(char *text) {
unsigned char ucTemp[2];
int i = 0;

    if (file_i2c < 0 || text == NULL)
        return 1;

    while (i<16 && *text)
    {
        ucTemp[0] = iBackLight | DATA | (*text & 0xf0);
        write(file_i2c, ucTemp, 1);
        usleep(PULSE_PERIOD);
        ucTemp[0] |= 4; // pulse E
        write(file_i2c, ucTemp, 1);
        usleep(PULSE_PERIOD);
        ucTemp[0] &= ~4;
        write(file_i2c, ucTemp, 1);
        usleep(PULSE_PERIOD);
        ucTemp[0] = iBackLight | DATA | (*text << 4);
        write(file_i2c, ucTemp, 1);
        ucTemp[0] |= 4; // pulse E
                write(file_i2c, ucTemp, 1);
                usleep(PULSE_PERIOD);
                ucTemp[0] &= ~4;
                write(file_i2c, ucTemp, 1);
                usleep(CMD_PERIOD);
        text++;
        i++;
    }
    return 0;
}

// Erase the display memory and reset the cursor to 0,0
int lcd1602Clear(void) {
    if (file_i2c < 0)
        return 1;
    WriteCommand(0x0E); // clear the screen
    return 0;

}

// Open a file handle to the I2C device
// Set the controller into 4-bit mode and clear the display
// returns 0 for success, 1 for failure
int lcd1602Init(int iChannel, int iAddr) {
char szFile[32];
int rc;

    sprintf(szFile, "/dev/i2c-%d", iChannel);
    file_i2c = open(szFile, O_RDWR);
    if (file_i2c < 0)
    {
        fprintf(stderr, "Error opening i2c device; not running as sudo?\n");
        return 1;
    }
    rc = ioctl(file_i2c, I2C_SLAVE, iAddr);
    if (rc < 0)
    {
        close(file_i2c);
        fprintf(stderr, "Error setting I2C device address\n");
        return 1;
    }
    iBackLight = BACKLIGHT; // turn on backlight
    WriteCommand(0x02); // Set 4-bit mode of the LCD controller
    WriteCommand(0x28); // 2 lines, 5x8 dot matrix
    WriteCommand(0x0c); // display on, cursor off
    WriteCommand(0x06); // inc cursor to right when writing and don't scroll
    WriteCommand(0x80); // set cursor to row 1, column 1
    lcd1602Clear();        // clear the memory

    return 0;
}

// Set the LCD cursor position
int lcd1602SetCursor(int x, int y) {
unsigned char cCmd;

    if (file_i2c < 0 || x < 1 || x > 20 || y < 1 || y > 4)
        return 1;
    int pos[5]={0,0,0x40,0x14,0x54};
    cCmd = 0x80 + pos[y];
    cCmd += x-1;
    WriteCommand(cCmd);
    return 0;

}

void lcd1602Shutdown(void) {
    iBackLight = 0; // turn off backlight
    WriteCommand(0x08); // turn off display, cursor and blink
    close(file_i2c);
    file_i2c = -1;
}

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

void play_sound(unsigned int gpio, unsigned int frequency, unsigned int duration) {
    unsigned int period = (1e6 / frequency) / 2;
    struct timespec delay = {0, period * 300};

    for (unsigned int i = 0; i < (duration * 300) / (period * 2); i++) {
        GPIOWrite(gpio, 1);
        nanosleep(&delay, NULL);
        GPIOWrite(gpio, 0);
        nanosleep(&delay, NULL);
    }
}

void *gps_thd(void *arg) {
    int serv_sock = *((int *) arg);

    int serial_port, bytes_send;
    char dat,buff[100],GGA_code[3];
    unsigned char IsitGGAstring=0;
    unsigned char GGA_index=0;
    unsigned char is_GGA_received_completely = 0;
  
    if ((serial_port = serialOpen ("/dev/ttyS0", 9600)) < 0)    /* open serial port */
    {
        fprintf (stderr, "Unable to open serial device: %s\n", strerror (errno)) ;
        return NULL;
    }

    if (wiringPiSetup () == -1)                                 /* initializes wiringPi setup */
    {
        fprintf (stdout, "Unable to start wiringPi: %s\n", strerror (errno)) ;
        return NULL ;
    }

    while(1){
        if(serialDataAvail (serial_port) ) {                    /* check for any data available on serial port */
            dat = serialGetchar(serial_port);                   /* receive character serially */
            if(dat == '$'){
                IsitGGAstring = 0;
                GGA_index = 0;
            }
            else if(IsitGGAstring ==1){
                buff[GGA_index++] = dat;
                if(dat=='\r')
                    is_GGA_received_completely = 1;
            }
            else if(GGA_code[0]=='G' && GGA_code[1]=='G' && GGA_code[2]=='A'){
                IsitGGAstring = 1;
                GGA_code[0]= 0;
                GGA_code[0]= 0;
                GGA_code[0]= 0;
                }
            else{
                GGA_code[0] = GGA_code[1];
                GGA_code[1] = GGA_code[2];
                GGA_code[2] = dat;
            }
        }
          
        if(is_GGA_received_completely==1){
            buff[0] = 'G';
            printf("%s\n",buff);
            bytes_send = send(serv_sock, buff, strlen(buff), 0);

            is_GGA_received_completely = 0;
            sleep(60);
        }
    }
    return 0;
}

void *lcd_thd(void *arg) {
    int serv_sock = *((int *) arg);
    int bytes_recv;
    char buffer[MSG_MAX];
    int rc;
    char recv_msg[17];
    
    while((bytes_recv = recv(serv_sock, recv_msg, 17, 0)) > 0) {
        if (rc) {
            printf("Initialization failed; aborting...\n");
            return 0;
        }

        fflush(stdout);
        recv_msg[bytes_recv] = '\0';
        rc = lcd1602Init(1, 0x27);
        
        if (recv_msg[0] == 'w') { // warn_message

            // 경고음 출력
            for (size_t i = 0; i < sizeof(melody) / sizeof(melody[0]); i++) {
                if (melody[i] != 0) {
                    play_sound(GPIO_NUM, melody[i], durations[i]);
                } else {
                    usleep(durations[i] * 1000);
                }
            }

            lcd1602SetCursor(1, 1);
            lcd1602WriteString("                ");
            lcd1602SetCursor(1, 1);
            lcd1602WriteString(recv_msg + 1);
        } else if (recv_msg[0] == 'p') { // commnunication
            printf("proc: %s\n", recv_msg+1);
            lcd1602SetCursor(1, 2);
            lcd1602WriteString(recv_msg + 1);
        } else if (recv_msg[0] == 't') { // processing
            printf("receive warrning: %s\n", recv_msg+1);
            lcd1602SetCursor(9, 2);
            lcd1602WriteString(recv_msg + 1);
        }
        lcd1602Control(1,0,1); // 깜빡깜빡
    }

    return NULL;
}

void *light_thd(void *arg) {
    int lightsensor;

    if(GPIOExport(LED_PIN) == -1 || GPIOExport(LIGHT_PIN) == -1){
        return NULL;
    }
    if(GPIODirection(LED_PIN, OUT) == -1 || GPIODirection(LIGHT_PIN, IN) == -1){
        return NULL;
    }

    while(1){
        lightsensor = GPIORead(LIGHT_PIN);
        if(lightsensor == 1){
            GPIOWrite(LED_PIN, 0);
        } else {
            GPIOWrite(LED_PIN,1);
        }
    }

    if(GPIOUnexport(LIGHT_PIN) == -1 || GPIOUnexport(LED_PIN) == -1 )
    {
        return NULL;
    }
}

void *camera_thd(void *arg) {
    int clnt_sock = *((int *) arg);
    int cnt = 0;
    char str[40] = "";
    char str2[20] = "";

    if (-1 == GPIOExport(BUT11) || -1 == GPIOExport(BUT1))
        return NULL;

    if (-1 == GPIODirection(BUT1, IN) || -1 == GPIODirection(BUT11, OUT))
        return NULL;

    if (-1 == GPIOWrite(BUT11, OUT))
        return NULL;

    while(1) {
        if (0 == GPIORead(BUT1)) {
            sprintf(str, "raspivid -t 10000 -o video%d.h264", cnt);
            sprintf(str2, "video%d.h264", cnt);
            puts("filming...");
            system(str);
            puts("complete!");
            cnt++;

            // 파일 열기
            FILE *file = fopen(str2, "rb");
            if (file == NULL) {
                perror("File open failed");
                return NULL;
            }

            // 파일 크기 계산
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            rewind(file);

            // 파일 크기 전송
            char file_size_str[32];
            sprintf(file_size_str, "%ld", file_size);
            send(clnt_sock, file_size_str, sizeof(file_size_str), 0);

            // 파일 데이터 전송
            char buffer[BUFFER_MAX];
            size_t total_sent = 0;
            while (total_sent < file_size) {
                size_t to_send = BUFFER_MAX;
                if (file_size - total_sent < BUFFER_MAX) {
                    to_send = file_size - total_sent;
                }
                size_t read = fread(buffer, 1, to_send, file);
                if (read <= 0) {
                    perror("File read failed");
                    return NULL;
                }
                ssize_t sent = send(clnt_sock, buffer, read, 0);
                if (sent < 0) {
                    perror("File sending failed");
                    return NULL;
                }
                total_sent += sent;
            }
            
            printf("File sent successfully.\n");
            fclose(file);
        }
    }

    return NULL;
}

void* button_thd(void* arg) {
    int serv_sock = *((int *) arg);

	if(GPIOExport(PIN1) == -1 || GPIOExport(POUT1) == -1 || GPIOExport(PIN2) == -1 || GPIOExport(POUT2) == -1 || GPIOExport(PIN3) == -1 || GPIOExport(POUT3) == -1){
        return NULL;
    }

	if(GPIODirection(PIN1, IN) == -1 || GPIODirection(POUT1, OUT) == -1 || GPIODirection(PIN2, IN) == -1 || GPIODirection(POUT2, OUT) == -1 || GPIODirection(PIN3, IN) == -1 || GPIODirection(POUT3, OUT) == -1){
        return NULL;
    }

    GPIOWrite(POUT1, 1);
    GPIOWrite(POUT2, 1);
    GPIOWrite(POUT3, 1);

	while (1) {
		if(GPIORead(PIN1) == 0){
			send(serv_sock, "tstop", 5, 0);
			usleep(150000);
		}
		else if(GPIORead(PIN2) == 0){
			send(serv_sock, "tstart", 6, 0);
			usleep(150000);
		} else if (GPIORead(PIN3) == 0){
			send(serv_sock, "turgent", 8, 0);
			usleep(150000);
		}

		usleep(15000);
	}

    GPIOUnexport(PIN1);
    GPIOUnexport(POUT1);
    GPIOUnexport(PIN2);
    GPIOUnexport(POUT2);
    GPIOUnexport(PIN3);
    GPIOUnexport(POUT3);
}

int main(int argc, char *argv[]) {
    pthread_t thd[5];
    int sock;
    struct sockaddr_in serv_addr;
    int status[5];
    
    // 실행 오류
    if (argc != 3) {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    if (-1 == GPIOExport(GPIO_NUM))
        return 0;
    if (-1 == GPIODirection(GPIO_NUM, OUT))
        return 0;

    // 소켓 연결 부분
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1)
        error_handling("socket() error");
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    if (pthread_create(&thd[0], NULL, gps_thd, &sock) != 0) {
        error_handling("Failed create gps thread");
    }
    if (pthread_create(&thd[1], NULL, lcd_thd, &sock) != 0) {
        error_handling("Failed create lcd thread");
    }
    if (pthread_create(&thd[2], NULL, light_thd, NULL) != 0) {
        error_handling("Failed create light thread");
    }
    if (pthread_create(&thd[3], NULL, camera_thd, &sock) != 0) {
        error_handling("Failed create camera thread");
    }
    if (pthread_create(&thd[4], NULL, button_thd, &sock) != 0) {
        error_handling("Failed create button thread");
    }
    
    pthread_join(thd[0], (void**) &status[0]);
    pthread_join(thd[1], (void**) &status[1]);
    pthread_join(thd[2], (void**) &status[2]);
    pthread_join(thd[3], (void**) &status[3]);
    pthread_join(thd[4], (void**) &status[4]);

    pthread_detach(thd[0]);
    pthread_detach(thd[1]);
    pthread_detach(thd[2]);
    pthread_detach(thd[3]);
    pthread_detach(thd[4]);

    close(sock);

    return 0;
}