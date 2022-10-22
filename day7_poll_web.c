//第八章：poll开发web服务器代码
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "wrap.c"
#include "pub.c"
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>

//初始化poll数组
void InitPoll(struct pollfd * client, int length)
{
    int i = 0;
    for(i=0; i<length; ++i)
        client[i].fd = -1;
}

//查找poll数组的空位
int FindPoll(struct pollfd * client, int length)
{
    int i = 1;
    for(i=1; i<length; ++i)
    {
        if(client[i].fd == -1)
            return i;
    }
    return -1;
}

//寻找现存的连接数量
void Find_keep_alive(struct pollfd * client, int length)
{
    int i=1;
    int count = 0;
    for(i=1; i<length; ++i)
    {
        if(client[i].fd != -1)
            count++;
    }
    printf("现存连接数量为%d\n", count);
}

int http_request(int connfd, struct pollfd * client, int i, int *maxi);
int send_header(int connfd, char * code, char * msg, char * fileType, int len);
int send_file(int connfd, char * filename);

int main()
{
    //若web服务器给浏览器发送数据的时候, 浏览器已经关闭连接,
    //则web服务器就会收到SIGPIPE信号
    //signal(SIGPIPE, SIG_IGN);
    struct sigaction act;
    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGPIPE, &act, NULL);

    //改变工作路径
    chdir("../web");
    //检查工作路径
    char buf[80];

    getcwd(buf,sizeof(buf));

    printf("current working directory: %s\n", buf);
    //---------------------
    int listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    //允许端口复用
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

    //绑定
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(8888);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    Bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    //监听
    Listen(listenfd, 128);
    printf("listening\n");

    struct pollfd client[1024];
    int length = 1024;
    //初始化
    InitPoll(client, length);

    //将监听文件描述符加入poll
    client[0].fd = listenfd;
    client[0].events = POLLIN;

    //其他参数初始化
    int maxi=0;
    int connfd;
    int index;
    int nready;
    int i;
    while (1)
    {
        nready = poll(client, maxi+1, -1);
        if(nready < 0)
        {
            perror("poll error");
            break;
        }
        //两个事件：1、新客户的连接到，2、客户的数据到
        if(client[0].revents == POLLIN)
        {
            //1、新客户的连接到
            connfd = Accept(listenfd, NULL, NULL);
            index = FindPoll(client, length);
            if(index == -1)
            {
                printf("用户已满，连接失败\n");
                close(connfd);
                continue;
            }
            //将connfd改为非阻塞
            int flags = fcntl(connfd, F_GETFL, 0);
            fcntl(connfd, F_SETFL, flags | O_NONBLOCK);

            client[index].fd = connfd;
            client[index].events = POLLIN;
            //维护maxi
            if(index>maxi)
                maxi = index;
            if(--nready==0)
                continue;
        }
        for(i=1;i<=maxi;++i)
        {
            if(client[i].fd == -1)
                continue;
            if(client[i].revents == POLLIN)
            {
                //2、客户的数据到
                connfd = client[i].fd;
                http_request(connfd, client, i, &maxi);
                if(--nready==0)
                    break;
            }
        }
        Find_keep_alive(client, length);
    }
    close(listenfd);
    return 0;
}

int http_request(int connfd, struct pollfd * client, int i, int *maxi)
{
    int n;
    char buf[1024];
    //读第一行数据，分析要请求的资源文件名
    memset(buf,0x00, sizeof(buf));
    n = Readline(connfd, buf, sizeof(buf));
    if(n<=0)//浏览器关闭页面
    {
        close(connfd);
        printf("浏览器关闭页面！\n");
        client[i].fd=-1;
        return -1;
    }
//    printf("[%s]\n", buf);
    // GET /hanzi.c HTTP/1.1
    char reqType[16] = {0};
    char fileName[255] = {0};
    char protocal[16] = {0};
    sscanf(buf, "%[^ ] %[^ ] %[^\r\n]", reqType, fileName, protocal);

    //循环把缓冲区数据读完
    while((n=Readline(connfd, buf, sizeof(buf)))>0);

    char * pFile = fileName;
    if(strlen(fileName)<=1)
        strcpy(pFile,"./");
    else
        pFile = fileName+1;

    //转换汉字编码
    strdecode(pFile, pFile);
    printf("所请求的名字为：[%s]\n", pFile);

    struct stat st;
    //1、请求文件不存在
    if(stat(pFile, &st)<0)
    {
        printf("file not exist\n");
        send_header(connfd, "404", "NOT FOUND", get_mime_type(".html"), 0);//状态行、首部行、空行
        send_file(connfd, "error.html");//消息正文
    }
    //2文件存在
    else
    {
        //判断文件类型
        //普通文件
        if(S_ISREG(st.st_mode))
        {
//            printf("file exist\n");
            //发送头部信息
            send_header(connfd, "200", "OK", get_mime_type(pFile), st.st_size);

            //发送文件内容
            send_file(connfd, pFile);
        }
        else if(S_ISDIR(st.st_mode))
        {
//            printf("目录文件\n");

            char buffer[1024];
            //发送头部信息
            send_header(connfd, "200", "OK", get_mime_type(".html"), 0);

            //发送html文件头部
            send_file(connfd, "html/dir_header.html");

            //文件列表信息
            struct dirent **namelist;
            int num;

            num = scandir(pFile, &namelist, NULL, alphasort);
            if (num < 0)
            {
                perror("scandir");
                close(connfd);
                client[i].fd = -1;
                return -1;
            }
            else
            {
                while (num--)
                {
//                    printf("%s\n", namelist[num]->d_name);
                    memset(buffer, 0x00, sizeof(buffer));
                    if(namelist[num]->d_type==DT_DIR)//如果是目录
                    {
                        sprintf(buffer, "<li><a href=%s/>%s</a></li>", namelist[num]->d_name, namelist[num]->d_name);
                    }
                    else
                    {
                        sprintf(buffer, "<li><a href=%s>%s</a></li>", namelist[num]->d_name, namelist[num]->d_name);
                    }

                    free(namelist[num]);
                    Write(connfd, buffer, strlen(buffer));
                }
                free(namelist);
            }
            //发送html尾部
            send_file(connfd, "html/dir_tail.html");
        }
    }
    close(connfd);
    client[i].fd = -1;

    return 1;
}

int send_header(int connfd, char * code, char * msg, char * fileType, int len)
{
    char buf[1024] = {0};
    sprintf(buf, "HTTP/1.1 %s %s\r\n", code, msg);
    sprintf(buf+ strlen(buf),"Content-Type: %s\r\n", fileType);
    if(len>0)
    {
        sprintf(buf+ strlen(buf), "Content-Length: %d", len);
    }
    strcat(buf, "\r\n");
    Write(connfd, buf, strlen(buf));
    return 0;
}

int send_file(int connfd, char * filename)
{
    int fd = open(filename, O_RDONLY);
    //打开失败
    if(fd<0)
    {
        perror("open errpr");
        return -1;
    }
    else
    {
        char buf[1024];
        int n;
        while(1)
        {
            memset(buf,0x00, sizeof(buf));
            n = read(fd, buf, sizeof(buf));
            if(n<=0)
                break;
//            printf("%s", buf);
            Write(connfd,buf, n);
        }
        return 1;
    }
}
