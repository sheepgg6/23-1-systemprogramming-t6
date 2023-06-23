// gcc -o server server.c -lpthread -lm

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/in.h>
#include <math.h>

#define BUFFER_MAX 1024
#define MTN_DATA_LENGTH 4
#define GPS_DATA_LENGTH 2

int client_sock[5] = { -1, -1, -1, -1, -1}, clnt_cnt = 0;
struct sockaddr_in clnt_addr[5];
socklen_t clnt_addr_size[5];

struct gps {
    float latitude;
    float longitude;
    char warn_message[16];
};

struct gps warn_gps[GPS_DATA_LENGTH] = {
    { .latitude = 37.1655, .longitude = 127.0268, .warn_message = "fall rock" },
    { .latitude = 37.1692, .longitude = 127.0265, .warn_message = "fall" }
};
struct gps mtn_gps[MTN_DATA_LENGTH] = {
    { .latitude = 37.2725, .longitude = 127.0416, .warn_message = "" }, // 매탄시장
    { .latitude = 37.1655, .longitude = 127.0268, .warn_message = "" }, // 유메야
    { .latitude = 37.1692, .longitude = 127.0265, .warn_message = "" }, // 도서관 카페
    { .latitude = 37.2851, .longitude = 127.0453, .warn_message = "" }, // 산학원
};

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

// 위도, 경도를 통한 거리 계산
float haversin(float x1, float x2, float y1, float y2) {
    float distance;
    float radius = 6371;
    float toRadian = M_PI / 180;

    float deltaLatitude = fabs(x1 - x2) * toRadian;
    float deltaLongitude = fabs(y1 - y2) * toRadian;

    float sinDeltaLat = sin(deltaLatitude / 2);
    float sinDeltaLng = sin(deltaLongitude /2);
    float squareRoot = sqrt(sinDeltaLat * sinDeltaLng + cos(x1 * toRadian) * cos(x2 * toRadian) * sinDeltaLng * sinDeltaLng);

    distance = 2 * radius * asin(squareRoot) * 1000;

    return distance;
}

// Client의 메시지를 받아 처리하여 응답
void *receive_thd(void *arg) {
    int clnt_sock = *((int *) arg);
    char recv_msg[BUFFER_MAX];
    int bytes_recv;

    while ((bytes_recv = recv(clnt_sock, recv_msg, BUFFER_MAX - 1, 0)) > 0) {
        if (recv_msg[0] == 'G') { // receive GPS data
            char parsing[13][20] = {};
            char *result = NULL;
            int i = 0, warn_min_idx = 0, mtn_min_idx = 0;
            float latitude = 0, longitude = 0;   
            float temp, warn_distance = 1E10, mtn_distance = 1E10, processivity = 0;
            char send_msg[17] = "";   

            recv_msg[bytes_recv] = '\0';

            result = strtok(recv_msg, ",");
            while (result != NULL) {
                strcpy(parsing[i++], result);
                result = strtok(NULL, ",");
            }
            
            latitude = atof(parsing[1]) / 100;
            longitude = atof(parsing[3]) / 100;
            
            printf("receive gps data\n client %d: latitude: %.4f longitude: %.4f\n", clnt_sock, latitude, longitude); 

            // 위험 지역 거리 계산
            for (int i = 0; i < GPS_DATA_LENGTH; i++) {
                temp = haversin(latitude, warn_gps[i].latitude, longitude, warn_gps[i].longitude);
                if(warn_distance > temp) {
                    warn_distance = temp;
                    warn_min_idx = i;
                }
            }            
            
            printf("warning distance: %.4f\n", warn_distance);
            if (warn_distance < 50) {
                sprintf(send_msg, "%c%s ", 'w', warn_gps[warn_min_idx].warn_message);
                printf("warn msg: %s\n", send_msg + 1);
                send(clnt_sock,  send_msg, 17, 0);
            }

            // 현재 진행도 파악
            for (int i = 0; i < MTN_DATA_LENGTH; i++) {
                temp = haversin(latitude, mtn_gps[i].latitude, longitude, mtn_gps[i].longitude);
                if(mtn_distance > temp) {
                    mtn_distance = temp;
                    mtn_min_idx = i;
                }
            }

            processivity = (float) mtn_min_idx / (MTN_DATA_LENGTH - 1) * 100;
            sprintf(send_msg, "%c%3.0f%% ", 'p', processivity);
            send(clnt_sock, send_msg, 17, 0);
        }  else if (recv_msg[0] == 't') { // receive Communication data

            for (int i = 0; i < clnt_cnt; i++) {
                printf("send warn msg: client %d %s\n", client_sock[i], recv_msg);
                recv_msg[bytes_recv] = '\0';
                send(client_sock[i], recv_msg, bytes_recv, 0);
            }
        } else { // receive Camera data
            int cnt = 0;
            char str[20] = "";

            while (1) {
                long file_size = atol(recv_msg);

                sprintf(str, "server_videoRecv%d.h264", cnt);

                // 코드 파일 수신 및 저장
                FILE *file = fopen(str, "wb");
                if (file == NULL) {
                    perror("File open failed");
                    return NULL;
                }

                size_t total_received = 0;
                char buffer[BUFFER_MAX];
                while (total_received < file_size) {
                    ssize_t to_receive = BUFFER_MAX;
                    if (file_size - total_received < BUFFER_MAX) {
                        to_receive = file_size - total_received;
                    }
                    ssize_t received = recv(clnt_sock, buffer, to_receive, 0);
                    if (received <= 0) {
                        perror("File receiving failed");
                        return NULL;
                    }
                    fwrite(buffer, 1, received, file);
                    total_received += received;
                }
                cnt++;
                fclose(file);
            }
        }
        memset(recv_msg, 0, BUFFER_MAX);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    int serv_sock;
    int status;
    struct sockaddr_in serv_addr;
    pthread_t tid[5];

    // 실행 오류
    if (argc != 2) {
        printf("Usage : %s <port>\n", argv[0]);
    }

    // 소켓 통신
    serv_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serv_sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error"); 

    while (1) {
        printf("[*] wait for client ...\n");

        if (client_sock[clnt_cnt] < 0) {
            clnt_addr_size[clnt_cnt] = sizeof(clnt_addr[clnt_cnt]);
            client_sock[clnt_cnt] = accept(serv_sock, (struct sockaddr*) &clnt_addr[clnt_cnt], &clnt_addr_size[clnt_cnt]);
            
            if (client_sock[clnt_cnt] == -1)
                error_handling("accept() error");

            printf("connected client %d\n", client_sock[clnt_cnt]);
        }

        if (pthread_create(&tid[clnt_cnt], NULL, receive_thd, &client_sock[clnt_cnt]) != 0) {
            error_handling("Failed create thread");
            close(client_sock[clnt_cnt]);
        }

        printf("[*] client %d connected\n", client_sock[clnt_cnt]);
        clnt_cnt++;
    }

    for (int i = 0; i < clnt_cnt; i++) {
        pthread_detach(tid[i]);
        close(client_sock[i]);
    }
    close(serv_sock);

    return 0;
}
