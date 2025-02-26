#include <stdio.h> // fprintf(), perror()
#include <stdlib.h> // exit()
#include <string.h> // memset()
#include <signal.h> // signal()
#include <fcntl.h> // open()
#include <errno.h>
#include <unistd.h> // read(), write(), close()
#include <termios.h>
#include <sys/socket.h> // socket(), connect()
#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/types.h> // mkfifo
#include <sys/stat.h> // mkfifo
#include <netinet/in.h> // struct sockaddr_in
#include <arpa/inet.h>  // htons()

#define BUF_SIZE 256
#define TEA_PRICE 50
#define HAM_PRICE 90
#define SAN_PRICE 60
#define SIGNAL_KEY 1234
#define shm_tablepipeID 1422
#define ser_fifo_path "deltoser_fifo" //和delivery接的pipe
#define deltocarpipe_path "deltocar_fifo" //delivery和car接的pipe，先建
const char* tablepipe_path[4]={"table1_fifo", "table2_fifo", "table3_fifo", "table4_fifo"};

struct Client_Order {
    int tea_num;
    int hamburger_num;
    int sandwich_num;
    int table;
    int priority;
    int price;
} order;

int semaphore;
int sharememory_order_list;
int sharememory_count;
int sharememory_tablepipe_fd;
int *shm_tablepipe_fd;
int server_fd; //socket fd
int fd_ser; //pipe fd

struct sigaction sa; // for setting signal
char send_buffer[BUF_SIZE];
char msgbuffer[BUF_SIZE]; // 暫存訊息的緩衝區，pipe中讀到的東西
int has_new_data = 0; // 標誌：是否有新資料

int cal_total_price(struct Client_Order order) {
    return order.tea_num * TEA_PRICE + order.hamburger_num * HAM_PRICE + order.sandwich_num * SAN_PRICE + order.priority;
}

void display_order(struct Client_Order *order, int *count) {
    for(int i=0; i<(*count); i++){
        printf("order %d: ", i);
        printf("tea %d, hamburger %d, sandwich %d, table %d, priority %d\n",
        order[i].tea_num, order[i].hamburger_num, order[i].sandwich_num, order[i].table, order[i].priority);
    }
}

void insert_order(struct Client_Order *order, struct Client_Order new_order, int *count) {
    int i = 0;
    struct Client_Order temp[(*count)];
    for(; i<(*count); i++) {
        if(new_order.priority > order[i].priority) { break; }
    }
    for(int j=i; j<(*count); j++) {
        temp[j] = order[j];
    }
    order[i] = new_order;
    for(int j=i; j<(*count); j++) {
        order[j+1] = temp[j];
    }
    (*count) += 1;
}

struct Client_Order divide_order(char *buf, int client_fd) {
    struct Client_Order temp;
    char *p = strtok(buf, " ");
    temp.tea_num = atoi(p);
    p = strtok(NULL, " ");
    temp.hamburger_num = atoi(p);
    p = strtok(NULL, " ");
    temp.sandwich_num = atoi(p);
    p = strtok(NULL, " ");
    temp.table = atoi(p);
    p = strtok(NULL, " ");
    temp.priority = atoi(p);
    
    return temp;
}

// void process_message(char *message) {
//     int num = atoi(strtok(message, " "));
//     int food[3] = {0};
//     int table;

//     // 儲存訂單訊息的動態字串
//     char *s = (char *)malloc(512 * sizeof(char)); // 根據需要調整大小
//     if (s == NULL)
//     {
//         perror("Failed to allocate memory for message");
//         return;
//     }
//     printf("client_fd = %d\n", record_clientfd[1]);

//     // 初始化 s
//     strcpy(s, "your");

//     for (int i = 0; i < num; i++)
//     {
//         table = atoi(strtok(NULL, " "));
//         food[0] = atoi(strtok(NULL, " "));
//         food[1] = atoi(strtok(NULL, " "));
//         food[2] = atoi(strtok(NULL, " "));

//         // 處理訂單
//         if (food[0] != 0)
//         {
//             strcat(s, " tea");
//         }
//         if (food[1] != 0)
//         {
//             strcat(s, " hamburger");
//         }
//         if (food[2] != 0)
//         {
//             strcat(s, " sandwich");
//         }
//         strcat(s, " are delivery.");

//         // 將結果發送給相應的client
//         sprintf(send_buffer, "%s", s);
//         send(6, send_buffer, BUF_SIZE, 0);
//         printf("table: %d, send: %s", table, send_buffer);

//         // 清空 send_buffer
//         memset(send_buffer, 0, BUF_SIZE);

//         // 重置 s 並準備下一個訂單
//         memset(s, 0, 512);
//         strcpy(s, "your");
//     }
//     free(s);
// }

int P(int semaphore) {
    struct sembuf sop;
    sop.sem_num=0;
    sop.sem_op=-1;//semaphore value minus one
    sop.sem_flg=0;
    if(semop(semaphore,&sop,1)<0){
        fprintf(stderr,"P():semaphore opration failed:%s\n",strerror(errno));
        return -1;
    }
    else {
        return 0;
    }
}

int V(int semaphore) {
    struct sembuf sop;
    sop.sem_num=0;
    sop.sem_op=1;//semaphore value plus one
    sop.sem_flg=0;
    if(semop(semaphore,&sop,1)<0){
        fprintf(stderr,"V():semaphore opration failed:%s\n",strerror(errno));
        return -1;
    }
    else {
        return 0;
    }
}

void handler(int signum) {
    while(waitpid(-1,NULL,WNOHANG)>0);
}

void signal_handler(int signum) {
    if(signum == SIGUSR1) {
        printf("SIGUSR1 received.\n");

        //讀pipe fd_ser
        int bytes_read = read(fd_ser, msgbuffer, sizeof(msgbuffer));
        if (bytes_read > 0)
        { // pipe有讀到東西
            msgbuffer[bytes_read-1] = '\0';
            printf("read: %s\n", msgbuffer);
        }
        else if (bytes_read == 0)
        { // pipe已關閉
            // printf("pipe closed\n");
        }
        else
        { // pipe read error
            perror("read failed");
            exit(1);
        }
    }
}

void rm_semaphore(int signum) { //handler to SIGINT
    static int executed=0;
    if(executed)return;
    executed=1;
    if(semctl(semaphore, 0, IPC_RMID, 0)<0) {perror("semaphore semctl IPC_RMID failed");}
    if(shmctl(sharememory_order_list, IPC_RMID, NULL)<0) {perror("sharememory_order_list shmctl IPC_RMID failed");}
    if(shmctl(sharememory_count, IPC_RMID, NULL)<0) {perror("sharememory_count shmctl IPC_RMID failed");}
    close(server_fd);
    for(int i=0; i<4; i++) {
        close(shm_tablepipe_fd[i]);
    }

    if(access(ser_fifo_path, F_OK) == 0) { //如果ser_fifo_path存在，刪除
        if(unlink(ser_fifo_path) == -1) {
            perror("unlink failed");
        }
        else {
            printf("FIFO '%s' deleted successfully.\n", ser_fifo_path);
        }
    }

    if(access(deltocarpipe_path, F_OK) == 0) { //如果ser_fifo_path存在，刪除
        if(unlink(deltocarpipe_path) == -1) {
            perror("unlink failed");
        }
        else {
            printf("FIFO '%s' deleted successfully.\n", deltocarpipe_path);
        }
    }

    for(int i=0; i<4; i++) {
        if (access(tablepipe_path[i], F_OK) == 0)
        { // 如果ser_fifo_path存在，刪除
            if (unlink(tablepipe_path[i]) == -1)
            {
                perror("unlink failed");
            }
            else
            {
                printf("FIFO '%s' deleted successfully.\n", tablepipe_path[i]);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    signal(SIGCHLD, handler);
    signal(SIGINT, rm_semaphore);
    if(argc != 2) {
        fprintf(stderr, "Usage: ./server <port>");
        exit(EXIT_FAILURE);
    }
    printf("[server pid] : %d\n", getpid()); // provide for delivery
    /* register handler to SIGNAL SIGUSR1 */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sa.sa_flags = SA_RESTART; /* 保證系統調用在signal後自動重啟 */
    if (sigaction(SIGUSR1, &sa, NULL) < 0)
    {
        perror("sigaction SIGINT");
        exit(EXIT_FAILURE);
    }

    ///socket program///
    int client_fd;
    int port = atoi(argv[1]);
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    int yes=1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;        
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(port);      
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    ///socket program///
    
    ///semaphore, sharememory///
    long int semaphoreID = 6789;
    semaphore = semget(semaphoreID, 1, IPC_CREAT|0666);
    if(semaphore < 0) {
        fprintf(stderr,"%s: creation of semaphore %ld failed: %s\n", argv[0], semaphoreID, strerror(errno));
        exit(1);
    }
    if(semctl(semaphore, 0, SETVAL, 1) == -1) { //initialize the value is one expressing only one can acqurire it
        perror("semctl error");
    }

    long int sharememoryID_order_list = 1234;
    long int sharememoryID_count = 2345;
    
    sharememory_order_list = shmget(sharememoryID_order_list, sizeof(struct Client_Order)*10, IPC_CREAT|0666);
    sharememory_count = shmget(sharememoryID_count, sizeof(int), IPC_CREAT|0666);
    sharememory_tablepipe_fd = shmget(shm_tablepipeID, sizeof(int)*4, IPC_CREAT | 0666);
    int *sharememory_pt_count;
    struct Client_Order *sharememory_pt_list;

    if(sharememory_order_list<0 || sharememory_count<0 || sharememory_tablepipe_fd<0) {
        perror("sheget");
        exit(1);
    }
    if((sharememory_pt_list=(struct Client_Order *)shmat(sharememory_order_list,NULL,0)) == (struct Client_Order *)-1) {
    	perror("shmat");
    	exit(1);
    }
    if((sharememory_pt_count=(int *)shmat(sharememory_count,NULL,0)) == (int *)-1) {
    	perror("shmat");
    	exit(1);
    }
    memset(sharememory_pt_count, 0, sizeof(int));
    if((shm_tablepipe_fd=(int *)shmat(sharememory_tablepipe_fd, NULL, 0)) == (int *)-1) {
        perror("shmat");
    	exit(1);
    }
    memset(shm_tablepipe_fd, 0, sizeof(int)*4);
    ///semaphore, sharememory///

    ///named pipe///
    if(access(ser_fifo_path, F_OK) == 0) { //如果ser_fifo_path已存在，先刪除
        unlink(ser_fifo_path);
    }
    if(mkfifo(ser_fifo_path, 0666) == -1) {
        perror("mkfifo deltoser");
        exit(1);
    }

    if(access(deltocarpipe_path, F_OK) == 0) { //如果deltocarpipe_path已存在，先刪除
        unlink(deltocarpipe_path);
    }
    if(mkfifo(deltocarpipe_path, 0666) == -1) {
        perror("mkfifo deltocar");
        exit(1);
    }

    for(int i=0; i<4; i++) {
        if (access(tablepipe_path[i], F_OK) == 0)
        { // 如果ser_fifo_path已存在，先刪除
            unlink(tablepipe_path[i]);
        }
        if (mkfifo(tablepipe_path[i], 0666) == -1)
        {
            perror("mkfifo tablepipe");
            exit(1);
        }
    }

    fd_ser = open(ser_fifo_path, O_RDONLY); //會block在這，需要執行寫端deliver，讀端server才會被unblock
    if(fd_ser == -1) {
        perror("open for write ser_fifo_path failed");
        exit(1);
    }

    for(int i=0; i<4; i++) { //先開讀端
        shm_tablepipe_fd[i] = open(tablepipe_path[i], O_RDONLY);
        if (shm_tablepipe_fd[i] == -1)
        {
            perror("open for read tablepipe_fd failed");
            exit(1);
        }
    }
    ///named pipe///

    char received_buf[BUF_SIZE];
    int order_price=0;

    pid_t childpid;
    while(1) {
        if((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            exit(EXIT_FAILURE);
        }
        else {
            childpid = fork();
            if(childpid >= 0) {
                if(childpid == 0) { //child process
                    printf("client accept\n");
                    int table, tea_last, ham_last, san_last; //桌號和餐點剩餘數量
                    while(1) {
                        memset(received_buf, 0, BUF_SIZE);
                        int recv_len = recv(client_fd, received_buf, BUF_SIZE, 0);
                        if(recv_len < 0) { perror("Receive failed\n"); }
                        order = divide_order(received_buf, client_fd);
                        table = order.table;
                        tea_last = order.tea_num;
                        ham_last = order.hamburger_num;
                        san_last = order.sandwich_num;
                        order_price=cal_total_price(order);
                        order.price = order_price;

                        P(semaphore);
                        insert_order(sharememory_pt_list, order, sharememory_pt_count);
                        display_order(sharememory_pt_list, sharememory_pt_count);
                        printf("\n");
                        V(semaphore);

                        while(tea_last>0 || ham_last>0 || san_last>0) { //如果此child process還有剩餘餐點，在pipe等待讀取送餐
                            
                            char tablepipe_read[6];
                            int bytes_read = read(shm_tablepipe_fd[table-1], tablepipe_read, sizeof(char) * 5); //讀取安排出餐的
                            if (bytes_read > 0)
                            { // pipe有讀到東西
                                tablepipe_read[6-1] = '\0';
                                printf("table: %d, read: %s\n", table, tablepipe_read);
                                ssize_t bytes_sent = send(client_fd, tablepipe_read, (strlen(tablepipe_read)+1), 0);
                                if (bytes_sent < 0) {
                                    perror("Error sending message");
                                }
                            }
                            else if (bytes_read == 0)
                            { // pipe已關閉
                                printf("pipe closed\n");
                            }
                            else
                            { // pipe read error
                                perror("read failed");
                                exit(1);
                            }
                            //剩餘的扣掉安排出餐的
                            char *p = strtok(tablepipe_read, " ");
                            tea_last -= atoi(p);
                            p = strtok(NULL, " ");
                            ham_last -= atoi(p);
                            p = strtok(NULL, " ");
                            san_last -= atoi(p);
                            printf("table: %d, last: %d %d %d\n", table, tea_last, ham_last, san_last);
                        }
                    }
                    exit(0);
                }
                else{ //parent process

                }
            }
            else {
                perror("fork error");
                exit(0);
            }
            close(client_fd);
        }
    }
    return 0;
}