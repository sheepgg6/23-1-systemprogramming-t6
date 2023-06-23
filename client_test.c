// gcc -o client client.c -lpthread

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

// GPIO
#define BUFFER_MAX 3
#define VALUE_MAX 40
#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1

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

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
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
	int sock;
    struct sockaddr_in serv_addr;
    pthread_t thd[5];
    int status[5];
    int bytes_recv;
	char recv_msg[17];
    
    // 실행 오류
    if (argc != 3) {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }

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

	if (pthread_create(&thd[4], NULL, button_thd, &sock) != 0) {
		error_handling("Failed create button thread");
	}

	while((bytes_recv = recv(sock, recv_msg, 17, 0)) > 0) {
		printf("%s\n", recv_msg);
	}
	
	pthread_join(thd[4], (void**) &status[4]);

	pthread_detach(thd[4]);

    close(sock);

	return 0;
}