#include <arpa/inet.h>  // htons()
#include <errno.h>
#include <fcntl.h> // open()
#include <netinet/in.h> // struct sockaddr_in
#include <signal.h> // signal()
#include <stdbool.h> 
#include <stdio.h> // fprintf(), perror()
#include <stdlib.h> // exit()
#include <string.h> // memset()
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h> // socket(), connect()
#include <sys/stat.h> // mkfifo
#include <sys/time.h> // struct timeval
#include <sys/types.h> // mkfifo
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h> // read(), write(), close()

#define MEALS_PER_BATCH 3 //車子一次最多送幾份餐
#define fifo_path "deliver_fifo"
#define ser_fifo_path "deltoser_fifo" //和server接的pipe
#define deltocarpipe_path "deltocar_fifo" //delivery和car接的pipe，先建
// const char* tablepipe_path[4]={"table1_fifo", "table2_fifo", "table3_fifo", "table4_fifo"};

#define SIGNAL_KEY 1234
#define semaphoreID 6789
#define sharememoryID_order_list 1234
#define sharememoryID_count 2345
#define shm_stopID 1213

#define TEA_WAIT_TIME 2
#define HAM_WAIT_TIME 4
#define SAN_WAIT_TIME 3

int semaphore;

int sharememory_order_list;
int sharememory_count;
struct Client_Order *sharememory_pt_list;
int *sharememory_pt_count;
int shm_stop;
int *stop; //Ctrl+C結束delivery後，讓兩個process退出while並結束

pid_t server_pid; //signal server用
pid_t deliverpid; //送餐的process
pid_t cook2; //第二位廚師

int P(int semaphore);
int V(int semaphore);
void initial();
void tea_wait();
void ham_wait();
void san_wait();
void display_order(struct Client_Order *order, int *count);
void cooking(struct Client_Order *order, int *count, char *cook_done, int semaphore, int *cookornot);
bool is_done(struct Client_Order *order);
void delivery(struct Client_Order *order,int *count);
int pipe_empty(int fd);
void handler(int signum);
void handler_end(int signum);

struct Client_Order {
    int tea_num;
    int hamburger_num;
    int sandwich_num;
    int table;
    int priority;
    int price;
} order;

int main(int argc, char *argv[]) {
    signal(SIGCHLD, handler);
    signal(SIGINT, handler_end);

    if(argc != 2){
        fprintf(stderr, "Usage: %s <serverpid> \n", argv[0]);
        exit(EXIT_FAILURE);
    }
    server_pid = atoi(argv[1]);
    
    initial();

    cook2 = fork();
    deliverpid = fork();
    if(deliverpid >= 0 && cook2 >= 0) {
        if(deliverpid == 0 && cook2 != 0) { //deliver process
            int fd = open(fifo_path, O_RDONLY);
            if(fd == -1) {
                perror("open for read failed");
                exit(1);
            }
            int fd_ser = open(ser_fifo_path, O_WRONLY);
            if(fd_ser == -1) {
                perror("open for write ser_fifo_path failed");
                exit(1);
            }
            int fd_car = open(deltocarpipe_path, O_WRONLY);
            if(fd_car == -1) {
                perror("open for write deltocarpipe_path, O_WRONLY failed");
                exit(1);
            }
            // int tablepipe_fd[4] = {0};
            // for(int i=0; i<4; i++) {
            //     tablepipe_fd[i] = open(tablepipe_path[i], O_WRONLY);
            //     if (tablepipe_fd[i] == -1)
            //     {
            //         perror("open for write tablepipe_fd failed");
            //         exit(1);
            //     }
            // }

            while(*stop) {
                int deliver_item[4][3] = {0}; //統計車上的餐哪桌有哪些，桌號照優先序排
                char deliver_str[24]; //send to car最終字串

                char batch[MEALS_PER_BATCH][4]; //一次送哪些餐
                int batch_count;
                for(batch_count=0; batch_count<MEALS_PER_BATCH; batch_count++) { //讀MEALS_PER_BATCH次pipe
                    int retval = pipe_empty(fd);
                    if(retval == -1) {
                        perror("select()");
                        break;
                    }
                    else if(retval == 2) { //Ctrl+C
                        break;
                    }
                    else if(retval == 0) { //超過設定的timeout，pipe沒有資料
                        if((*sharememory_pt_count) > 0) { //還有訂單
                            printf("wait for preparing order\n");
                            sleep(1);
                            batch_count--; //消除這次
                            continue; //重來
                        }
                        else { //沒有訂單了，不用等直接走
                            printf("no order anymore\n");
                            break;
                        }
                    }
                    else { //pipe中有資料，retval==1
                        int bytes_read = read(fd, batch[batch_count], sizeof(char)*4);
                        if(bytes_read > 0) { //pipe有讀到東西
                            batch[batch_count][bytes_read-1] = '\0';
                            printf("read: %s\n", batch[batch_count]);
                        }
                        else if(bytes_read == 0) { //pipe已關閉
                            printf("pipe closed\n");            
                        }
                        else { //pipe read error
                            perror("read failed");
                            exit(1);
                        }
                    }
                }
                
                if(batch_count != 0) { //代表有餐點需要送出
                    for(int i=0; i<batch_count; i++) { //整理batch中的餐點到deliver_item
                        int table;
                        char *token;
                        token = strtok(batch[i], " ");
                        table = atoi(token);
                        token = strtok(NULL, " ");
                        if(strcmp(token, "t") == 0) {
                            deliver_item[table-1][0]++;
                        }
                        else if(strcmp(token, "h") == 0) {
                            deliver_item[table-1][1]++;
                        }
                        else {
                            deliver_item[table-1][2]++;
                        }
                    }

                    //送處理字串給車子
                    sprintf(deliver_str, "%d", deliver_item[0][0]);
                    for(int i=0; i<4; i++) {
                        for(int j=0; j<3; j++) {
                            if(i!=0 || j!=0) {
                                sprintf(deliver_str + strlen(deliver_str), " %d", deliver_item[i][j]);
                            }
                        }
                    }
                    deliver_str[24-1] = '\0';
                    write(fd_car, deliver_str, strlen(deliver_str)+1); //write to 給car的pipe
                    write(fd_ser, deliver_str, strlen(deliver_str)+1); //write to 給server的pipe
                    printf("deliver str: %s\n", deliver_str); //要送給車子的字串

                    //發送 SIGUSR1 訊號通知 server
                    if(kill(server_pid, SIGUSR1) == -1) {
                        perror("Error sending signal to server");
                        exit(1);
                    }

                    //送處理字串給server's child process，3的數字
                    // for(int i=0; i<4; i++) {
                    //     if(deliver_item[i][0]>0 || deliver_item[i][1]>0  || deliver_item[i][2]>0) {
                    //         char totablei_pipe[6];
                    //         sprintf(totablei_pipe, "%d", deliver_item[i][0]);
                    //         sprintf(totablei_pipe + strlen(totablei_pipe), " %d", deliver_item[i][1]);
                    //         sprintf(totablei_pipe + strlen(totablei_pipe), " %d", deliver_item[i][2]);
                    //         totablei_pipe[6-1]='\0';
                    //         if(write(tablepipe_fd[i], totablei_pipe, strlen(totablei_pipe) + 1) < 0) {
                    //             perror("deliver write failed\n");
                    //         }
                    //         else {
                    //             printf("server's children str: %s, fd: %d\n", totablei_pipe, tablepipe_fd[i]); // 要送給server's child process的字串
                    //         }
                    //     }
                    // }
                }
                else {
                    sleep(10);
                }
            }
            close(fd);
            close(fd_car);
            // for(int i=0; i<4; i++) {
            //     printf("close pipe fd: %d\n", tablepipe_fd[i]);
            //     close(tablepipe_fd[i]);
            // }
            exit(0);
        }
        if(cook2 == 0 && deliverpid != 0) { //cooking process 2
            int fd = open(fifo_path, O_WRONLY);
            if(fd == -1) {
                perror("open for write failed");
                exit(1);
            }

            semaphore = semget(semaphoreID, 0, 0666); // 子進程重新獲取 semaphore ID
            if (semaphore < 0) {
                perror("child semget failed");
                exit(1);
            }

            while(*stop) {
                if((*sharememory_pt_count) > 0) {
                    // printf("\ncook2: ");
                    sleep(1);
                    char cook_done[4]; //煮完的東西
                    int cookornot = 0;
                    cooking(sharememory_pt_list, sharememory_pt_count, cook_done, semaphore, &cookornot);
                    if(cookornot) {
                        if(write(fd, cook_done, strlen(cook_done)+1) < 0) {
                            perror("cook2 write failed\n");
                        }
                        else {
                            printf("cook2 write: %s\n", cook_done);
                        }
                    }
                    sleep(1);
                }
                else {
                    sleep(10);
                }
            }
            close(fd);
        }
        if(deliverpid != 0 && cook2 != 0) { //cooking process 1
            int fd = open(fifo_path, O_WRONLY);
            if(fd == -1) {
                perror("open for write failed");
                exit(1);
            }

            while(*stop) {
                if((*sharememory_pt_count) > 0) {
                    // printf("\ncook1: ");
                    sleep(1);
                    char cook_done[4]; //煮完的東西
                    int cookornot = 0;
                    cooking(sharememory_pt_list, sharememory_pt_count, cook_done, semaphore, &cookornot);
                    if(cookornot) {
                        if(write(fd, cook_done, strlen(cook_done)+1) < 0) {
                            perror("cook1 write failed\n");
                        }
                        else {
                            printf("cook1 write: %s\n", cook_done);
                        }
                    }
                    sleep(1);
                }
                else {
                    sleep(10);
                }
            }
            close(fd);
        }
    }
    else {
        perror("fork error");
        exit(0);
    }

    return 0;
}

int P(int semaphore) {
    struct sembuf sop;
    sop.sem_num = 0;
    sop.sem_op = -1; //semaphore value minus one
    sop.sem_flg = 0;
    if(semop(semaphore, &sop, 1) < 0) {
        fprintf(stderr, "P():semaphore operation failed:%s\n", strerror(errno));
        return -1;
    }
    else {
        return 0;
    }
}

int V(int semaphore) {
    struct sembuf sop;
    sop.sem_num = 0;
    sop.sem_op = 1; //semaphore value plus one
    sop.sem_flg = 0;
    if(semop(semaphore, &sop, 1) < 0) {
        fprintf(stderr, "V():semaphore operation failed:%s\n", strerror(errno));
        return -1;
    }
    else {
        return 0;
    }
}

void initial() {
    ///semaphore, sharememory///
    semaphore = semget(semaphoreID, 0, 0666);
    if(semaphore < 0) {
        fprintf(stderr,"creation of semaphore %d failed: %s\n", semaphoreID, strerror(errno));
        exit(1);
    }
    if(semctl(semaphore, 0, SETVAL, 1) == -1) { //initialize the value is one expressing only one can acqurire it
        perror("semctl error");
    }
    
    sharememory_order_list = shmget(sharememoryID_order_list, sizeof(struct Client_Order)*10, IPC_CREAT|0666);
    sharememory_count = shmget(sharememoryID_count, sizeof(int), IPC_CREAT|0666);
    shm_stop = shmget(shm_stop, sizeof(int), IPC_CREAT|0666);

    if(sharememory_order_list<0 || sharememory_count<0 || shm_stop<0) {
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
    if((stop=(int *)shmat(shm_stop,NULL,0)) == (int *)-1) {
    	perror("shmat");
    	exit(1);
    }
    ///semaphore, sharememory///

    *stop = 1;
    ///named pipe///
    if(access(fifo_path, F_OK) == 0) { //如果fifo_path已存在，先刪除
        unlink(fifo_path);
    }
    if(mkfifo(fifo_path, 0666) == -1) {
        perror("mkfifo fifo");
        exit(1);
    }
    ///named pipe///
}

void tea_wait(){sleep(TEA_WAIT_TIME);}
void ham_wait(){sleep(HAM_WAIT_TIME);}
void san_wait(){sleep(SAN_WAIT_TIME);}

void display_order(struct Client_Order *order, int *count) {
    for(int i=0; i<(*count); i++){
        printf("order %d: ", i);
        printf("tea %d, hamburger %d, sandwich %d, table %d, priority %d\n",
        order[i].tea_num, order[i].hamburger_num, order[i].sandwich_num, order[i].table, order[i].priority);
    }
}

void cooking(struct Client_Order *order, int *count, char *cook_done, int semaphore, int *cookornot) {
    P(semaphore);
    // display_order(order, count);
    int item = 0;
    int if_done = 0;
    if((*sharememory_pt_count) > 0) {
        sprintf(cook_done, "%d", order[0].table); //cook_done存"桌號 煮完的東西"
        if(order[0].tea_num > 0) {
            // printf("doing table %d tea\n", order[0].table);
            strcat(cook_done, " t"); //tea
            order[0].tea_num -= 1;
            item = 1;
        }
        else if(order[0].hamburger_num > 0) {
            // printf("doing table %d hamburger\n", order[0].table);
            strcat(cook_done, " h"); //hamburger
            order[0].hamburger_num -= 1;
            item = 2;
        }
        else if(order[0].sandwich_num > 0) {
            // printf("doing table %d sandwich\n", order[0].table);
            strcat(cook_done, " s"); //sandwich
            order[0].sandwich_num -= 1;
            item = 3;
        }
        // display_order(order, count);
        if(is_done(order)) { //移除first訂單
            delivery(order, count);
            if_done = 1;
        }
        (*cookornot) = 1;
    }
    else {
        (*cookornot) = 0;
    }
    V(semaphore);

    if(item == 1) {
        tea_wait();
    }
    else if(item == 2) {
        ham_wait();
    }
    else if(item == 3) {
        san_wait();
    }

    if(if_done) {
        printf("order cooked done\n\n");
    }
}

bool is_done(struct Client_Order *order) {
    if(order[0].tea_num==0 && order[0].sandwich_num==0 && order[0].hamburger_num==0)
        return true; 
    else return false;
}

void delivery(struct Client_Order *order,int *count) { //移除first訂單
    memmove(&order[0], &order[1], sizeof(struct Client_Order) * ((*count) - 1));
    (*count) -= 1;
}

int pipe_empty(int fd) { //用select()看pipe裡面有沒有做好但未送出的餐點
    fd_set readfd_set;
    struct timeval timeout;
    int retval;

    FD_ZERO(&readfd_set);
    FD_SET(fd, &readfd_set);
    timeout.tv_sec = 7;
    timeout.tv_usec = 0;
    retval = select(fd+1, &readfd_set, NULL, NULL, &timeout);
    if(retval < 0) {
        if(errno == EINTR) {
            printf("select interrupted by signal\n"); //select收到ctrl+c的interrupt
            *stop = 0;
            return 2;
        }
        else {
            perror("select()");
            return -1;
        }
    }
    else if(retval == 0) { //超過設定的timeout，pipe沒有資料
        printf("No data within 7 seconds.\n");
        return 0;
    }
    else { //pipe中有資料
        if(FD_ISSET(fd, &readfd_set)) { //fd是否在readfd_set中
            return 1;
        }
        else {
            return -1;
        }
    }
}

void handler(int signum) {
    while(waitpid(-1,NULL,WNOHANG)>0);
}

void handler_end(int signum) {
    if(getpid() == deliverpid || getpid() == cook2) {
        return; //子進程直接返回
    }
    //程式結束後刪掉named pipe file
    printf("end one time\n");
    *stop = 0;
    if(access(fifo_path, F_OK) == 0) { //如果fifo_path存在，刪除
        if(unlink(fifo_path) == -1) {
            perror("unlink failed");
        }
        else {
            printf("FIFO '%s' deleted successfully.\n", fifo_path);
        }
    }
}