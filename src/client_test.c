#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

using namespace std;

#define BUFFER_SIZE 256

int main(int argc, char *argv[])
{
    if (argc != 3){
        printf("Usage:​​​​./client_test <ip> <port> \n");
        exit(EXIT_FAILURE);
    }

    int client_fd;
    struct sockaddr_in server_addr;
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    // 建立 socket
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 設定 server 地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address or address not supported");
        close(client_fd);
        exit(EXIT_FAILURE);
    }
    // 連接到 server
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(client_fd);
        exit(EXIT_FAILURE);
    }
	
    // 傳送資料到 server
    while(1){
        char state[BUFFER_SIZE];
        if(fgets(state, sizeof(state), stdin) != NULL) {
            if(send(client_fd, state, BUFFER_SIZE, 0) < 0) {
                perror("Send failed");
            }
        }
    }

    // 關閉連線
    close(client_fd);
    return 0;
}