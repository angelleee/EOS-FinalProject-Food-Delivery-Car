#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <errno.h>     /* Errors */
#include <sys/types.h> /* Primitive System Data Types */
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/shm.h>
#define BUFFER_SIZE 256
#define PROJECT_ID 'R' // 用於 ftok 的項目 ID

int bytes_received;
void cat();
void mainmenu();
int getch();
void handler(int s);
void cleanup_handler(int signo) ;
struct shared_data
{
    int food[3];
};
int food[3]={0};
struct shared_data *shm_ptr;

int table = 1; // 暫時設定的
int priority = 2;
const char *name[3] = {"tea", "hamburger", "sandwich"};
int shmid ;
int main(int argc, char *argv[])
{
    signal(SIGINT, cleanup_handler);
    signal(SIGTERM, cleanup_handler);
    signal(SIGCHLD, handler);
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <ip> <port> <table>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    /* shared memory food initializtion */
    // 使用程序路徑和 table number 生成唯一的 key
    char temp_path[256];
    if (getcwd(temp_path, sizeof(temp_path)) == NULL)
    {
        perror("getcwd failed");
        exit(EXIT_FAILURE);
    }

    // 使用當前目錄和 table number 生成唯一的 key
    key_t shm_key = ftok(temp_path, PROJECT_ID + atoi(argv[3]));
    if (shm_key == -1)
    {
        perror("ftok failed");
        exit(EXIT_FAILURE);
    }

    // 創建shared memory
    shmid = shmget(shm_key, sizeof(struct shared_data), IPC_CREAT | 0666);
    if (shmid == -1)
    {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    // 附加到shared memory
    shm_ptr = (struct shared_data *)shmat(shmid, NULL, 0);
    if (shm_ptr == (void *)-1)
    {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    memset(shm_ptr->food, 0, sizeof(shm_ptr->food));

    int client_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char recbuffer[BUFFER_SIZE];
    int port = atoi(argv[2]);
    const char *ip = argv[1];
    table = atoi(argv[3]);

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

    // connection successful
    cat();
    pid_t childpid = fork();

    while (1)
    {
        if (childpid >= 0)
        { /* fork succeeded */
            if (childpid == 0)
            { /* fork() returns 0 to the child process */
                while (1)
                {
                    mainmenu();

                    if (food[0] != 0 || food[1] != 0 || food[2] != 0)
                    {
                        // 可送餐
                        printf("Please wait for a few minutes...\n");
                        sprintf(buffer, "%d %d %d %d %d", food[0], food[1], food[2], table, priority);
                        printf("%s\n", buffer);
                        // 傳送資料到 server
                        if (send(client_fd, buffer, strlen(buffer), 0) < 0)
                        {
                            perror("Send failed");
                            break;
                        }
                        memset(buffer, 0, BUFFER_SIZE);
                        
                        // reset
                        shm_ptr->food[0] += food[0];
                        shm_ptr->food[1] += food[1];
                        shm_ptr->food[2] += food[2];
                        food[0] = 0;
                        food[1] = 0;
                        food[2] = 0;
                        
                        priority = 0;
                        getch();
                        getch();
                    }
                }
            }
            else
            {   /* fork() returns new pid to the parent process */
                /* 給client隨時可以收到server msg */
                while (1)
                {
                    bytes_received = recv(client_fd, recbuffer, BUFFER_SIZE, 0);
                    if (bytes_received <= 0)
                    {
                        if (bytes_received == 0)
                        {
                            printf("Server disconnected.\n");
                        }
                        else
                        {
                            perror("Receive failed");
                        }
                        break;
                    }
                    printf("%s\n", recbuffer);
                    printf("your ");
                    char *p = strtok(recbuffer, " ");
                    //int tmp_food[3] = {0};
                    if(atoi(p) > 0){
                        printf("tea(%d) ",atoi(p));
                        shm_ptr->food[0] -= atoi(p);
                    }
                    p = strtok(NULL, " ");
                    if (atoi(p) > 0)
                    {
                        printf("hamburger(%d) ", atoi(p));
                        shm_ptr->food[1] -= atoi(p);
                    }
                    p = strtok(NULL, " ");
                    if (atoi(p) > 0)
                    {
                        printf("sandwich(%d) ", atoi(p));
                        shm_ptr->food[2] -= atoi(p);
                    }
                    printf("are delivery\n");
                    printf("=====================================================================\n");
                    printf("your order remain : tea(%d) hamburger(%d) sandwich(%d) \n", shm_ptr->food[0], shm_ptr->food[1], shm_ptr->food[2]);
                    printf("=====================================================================\n");
                    memset(buffer, 0, BUFFER_SIZE);
                }
            }
        }
        else
        {                   /* fork returns -1 on failure */
            perror("fork"); /* display error message */
            exit(0);
        }
    }
    // 傳送資料

    // 關閉連線
    close(client_fd);
    // printf("Thank you for coming! We hope to see you again soon.\n");
    return 0;
}
void cleanup_handler(int signo)
{
    printf("\nCleaning up shared memory...\n");

    // 分離共享內存
    if (shm_ptr != NULL && shm_ptr != (void *)-1)
    {
        if (shmdt(shm_ptr) == -1)
        {
            perror("shmdt failed");
        }
    }

    // 刪除共享內存段
    if (shmid != -1)
    {
        if (shmctl(shmid, IPC_RMID, NULL) == -1)
        {
            perror("shmctl failed");
        }
    }

    // 如果是因為信號而調用，則退出程序
    if (signo != 0)
    {
        exit(EXIT_SUCCESS);
    }
}

void handler(int s)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
} // 收回已退出的child process 避免zomble process

void cat()
{
    printf("       /\\_____/\\\n");
    printf("      /  o   o  \\\n");
    printf("     ( ==  ^  == )\n");
    printf("      )           (\n");
    printf("     (             )\n");
    printf("     (  (  )   (  ) )\n");
    printf("      \\               /\n");
    printf("       \\__|_|_|_|__/\n");
    printf("Meow! Welcome to cat paradise!\n");
    printf("Welcome to our cat cafe!\n");
    printf("          ~~~~            _________          ___________   \n");
    printf("         (    )          /         \\        /           \\  \n");
    printf("        (      )        |  ~~~~~~   |      |   ~~~~~~~   | \n");
    printf("         \\____/         | | BUN  |  |      |  BREAD    | |\n");
    printf("       --[____]--      [| |______|  |]    [|___________| |]\n");
    printf("          (__)          | | BURGER| |      |  FILLING   | |\n");
    printf("          (__)           \\_________/        \\___________/  \n");
    printf("         _|__|_         ~~~~~~~~~~~~        ~~~~~~~~~~~~~  \n");
    printf("\n");
    printf("            Tea              Burger              Sandwich  \n");
}
void mainmenu()
{
    int option = 0;
    while (1)
    {
        printf("Please choose from 1~6\n");
        printf("1. tea: 50\n");
        printf("2. hamburger: $90\n");
        printf("3. sandwich: $60\n");

        printf("4. confirm\n");
        printf("5. cancel\n");
        printf("6. check remaining order\n");

        scanf("%d", &option);
        if (option == 1 || option == 2 || option == 3)
        {
            int num;
            printf("How many %s do you order?\n", name[option - 1]);
            scanf("%d", &num);
            food[option - 1] += num;
            // printf("num = %d , %d\n", num, food[option -1]);
        }
        else if (option == 5)
        {
            // cancel
            for (int i = 0; i < 3; i++)
            {
                food[i] = 0; // reset
            }
            return;
        }
        else if (option == 4)
        {
            printf("Your order: Tea x %d, Burger x %d, Sandwich x %d. Confirm? (y/n): ", food[0], food[1], food[2]);
            char confirm;
            // char ch;

            scanf(" %c", &confirm);
            if (confirm != 'y' && confirm != 'Y')
            {
                continue; // 返回主選單
            }
            // 開始送餐

            // 確認訂單後詢問是否提升優先權
            printf("Do you want to pay extra to prioritize your order? (y/n): ");

            getch();
            scanf("%c", &confirm);
            if (confirm == 'y' || confirm == 'Y')
            {
                printf("Please set a priority level for your order? ( 1+ for extra priority): ");
                int user_priority;
                scanf("%d", &user_priority);
                priority = user_priority; // 使用者自定義優先權
                printf("You chose priority level %d. You will be charged an additional $%d.\n", priority, priority);
                printf("The total payment for your order is $%d\n", 50 * food[0] + 90 * food[1] * 60 * food[2] + priority);
            }
            else
            {
                priority = 0; // 保持普通優先權
                printf("Your order will be prepared as usual.\n");
                printf("The total payment for your order is $%d\n", 50*food[0]+90*food[1]*60*food[2]);
            }
            return;
        }
        else if (option == 6){ /*check remain order*/
            printf("=====================================================================\n");
            printf("your order remain : tea(%d) hamburger(%d) sandwich(%d) \n", shm_ptr->food[0], shm_ptr->food[1], shm_ptr->food[2]);
            printf("=====================================================================\n");
        }
        else
        {
            // 無效輸入處理
            printf("Invalid option. Please choose from 1 to 4.\n");
        }
    }
}

int getch()
{
    int ch;
    struct termios oldt, newt;

    tcgetattr(STDIN_FILENO, &oldt);
    memcpy(&newt, &oldt, sizeof(newt));
    newt.c_lflag &= ~(ECHO | ICANON | ECHOE | ECHOK |
                      ECHONL | ECHOPRT | ECHOKE | ICRNL);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    return ch;
}
