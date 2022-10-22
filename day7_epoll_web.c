//第八章：epoll开发web服务器代码
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "wrap.h"
#include <sys/epoll.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include "pub.h"

int http_request(int connfd, int epfd);
void send_header(int connfd, char * code, char * msg, char * fileType, int len);
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
    chdir("./web");
    //检查工作路径
    char buf[80];

    getcwd(buf,sizeof(buf));

    printf("current working directory: %s\n", buf);
    //--------------------
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

    //创建一课树
    int epfd =  epoll_create(1);

    //将listenfd挂上树
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

    //构建事件结构体数组，作为传出参数用于接收epoll_wait中发生变化的事件
    struct epoll_event event[1024];

    //其他参数初始化
    int nready;//用于接收epoll_wait返回的发生变化的事件个数
    int i;
    int connfd;
    while(1)
    {
        //委托内核监控
        nready = epoll_wait(epfd, event, 1024, -1);
        if(nready < 0)
        {
            if(errno == EINTR)
                continue;
            break;
        }
        //两个事件：1、新客户的连接到，2、客户的数据到
        for(i=0;i<nready;++i)
        {
            //1、新客户的连接到
            if(event[i].data.fd == listenfd)
            {
                connfd = Accept(listenfd, NULL, NULL);
                //新连接的客户上树
                ev.events = EPOLLIN;
                ev.data.fd = connfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);

                //将connfd改为非阻塞
                int flags = fcntl(connfd, F_GETFL, 0);
                fcntl(connfd, F_SETFL, flags | O_NONBLOCK);
            }
            //2、客户的数据到
            else
            {
                connfd = event[i].data.fd;
                http_request(connfd, epfd);
            }
        }
    }
    close(listenfd);
    return 0;
}

int http_request(int connfd, int epfd)
{
    int n;
    char buf[1024];
    //读第一行数据，分析要请求的资源文件名
    memset(buf,0x00, sizeof(buf));
    n = Readline(connfd, buf, sizeof(buf));
    if(n<=0)//浏览器关闭页面
    {
        close(connfd);
//        printf("浏览器关闭页面！\n");
        //将文件描述符从epoll树上删除
        epoll_ctl(epfd, EPOLL_CTL_DEL, connfd, NULL);
        return -1;
    }
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
//    printf("所请求的名字为：[%s]\n", pFile);

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
        if(S_ISREG(st.st_mode))//普通文件
        {
            //发送头部信息
            send_header(connfd, "200", "OK", get_mime_type(pFile), st.st_size);

            //发送文件内容
            send_file(connfd, pFile);
        }
        else if(S_ISDIR(st.st_mode))//目录文件
        {

            char buffer[1024];
            //发送html头部信息
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
                epoll_ctl(epfd, EPOLL_CTL_DEL, connfd, NULL);
                return -1;
            }
            else
            {
                while (num--)
                {
                    memset(buffer, 0x00, sizeof(buffer));//如果是目录

                    //将每个目录循环写入buffer
                    if(namelist[num]->d_type==DT_DIR)
                    {
                        //虽然写入buffer的是类似：<li><a href=test.c>test.c</a></li>，
                        // 不是真正的http://192.168.1.213:8888/test.c，但是浏览器会自己加上"http://192.168.1.213:8888/"
                        sprintf(buffer, "<li><a href=%s/>%s</a></li>", namelist[num]->d_name, namelist[num]->d_name);
                    }
                    else//如果是普通文件
                    {
                        sprintf(buffer, "<li><a href=%s>%s</a></li>", namelist[num]->d_name, namelist[num]->d_name);
                    }
                    free(namelist[num]);

                    Write(connfd, buffer, strlen(buffer));
                }
                free(namelist);
            }
            //发送html尾部信息
            send_file(connfd, "html/dir_tail.html");
        }
    }
    //是不是可以加这个
    close(connfd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, connfd, NULL);
    return 1;
}

void send_header(int connfd, char *code, char *msg, char *fileType, int len)
{
    char buf[1024] = {0};
    sprintf(buf, "HTTP/1.1 %s %s\r\n", code, msg);
    sprintf(buf+strlen(buf), "Content-Type:%s\r\n", fileType);
    if(len>0)
    {
        sprintf(buf+strlen(buf), "Content-Length:%d\r\n", len);
    }
    strcat(buf, "\r\n");
    Write(connfd, buf, strlen(buf));
    return ;
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
            n = Read(fd, buf, sizeof(buf));
            if(n<=0)
                break;
            Write(connfd,buf, n);
        }
        return 1;
    }
}
