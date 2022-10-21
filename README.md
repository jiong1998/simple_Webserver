# simple_Webserver

它是个什么项目？——Linux下C的简易的多并发的Web服务器，通过输入网址可以浏览本地主机的所设置的工作路径的目录及文件，助力初学者快速实践网络编程，搭建属于自己的服务器。
- 使用了 epoll+信号处理+守护进程+多线程的并发模型。
- 目前仅支持解析HTTP的GET请求行
- 未来将参考github有名的项目：TinyWebServer来改进该项目
- 本地环境：阿里云服务器linux

注意：要开始这个项目，需要对linux编程、网络编程有一定的了解，这方面书籍推荐《**Unix网络编程**》

## Web服务器开发流程分析
整体项目分为两个部分：
- 第一个部分是通用的epoll的服务器开发部分。
- 第二个部分是**处理客户端请求**。

//未优化
通用的epoll的服务器开发部分流程如下图所示：
![在这里插入图片描述](https://img-blog.csdnimg.cn/800a38ac9bcf4eaf89128ec4fd8d4cba.png)
epoll的具体流程就不作介绍了，之前已经做过相应的服务器开发了。

处理客户端请求部分流程如下图所示：
![在这里插入图片描述](https://img-blog.csdnimg.cn/f6a2c3c2d2374a159f0c4ba5d9467fae.png)
处理客户端请求的流程：
```cpp
 int http_request(int cfd)
 {
 	//读取请求行
	Readline();
	//分析请求行，得到要请求的资源文件名file
		如：GET /hanzi.c /HTTP1.1
	//循环读完剩余的内核缓冲区的数据,不然数据还会留在缓冲区
	while((n = Readline())>0)；
	//判断文件是否存在
	stat()；
	
	1文件不存在
		返回错误页
			组织应答信息：http响应格式消息+错误页正文内容
	2文件存在
		判断文件类型：
			2.1普通文件
				组织应答信息：http响应格式消息＋消息正文
			2.2 目录文件
				组织应答消息：http响应格式消息thtml格式文件
 }
```

## Web服务器开发问题具体分析：
### 1.工作路径问题
### 2.信号SIGPIPE问题
考虑具体的场景：浏览器关闭了与服务器的连接，而如果此时web服务器正在给浏览器发送数据，就会导致内核会给服务器端发送SIGPIPE信号，而SIGPIPE信号的默认处理动作是终止进程。而终止服务器这不是我们想要看到的，因此需要**捕获SIGPIPE信号**，并设置为**SIG_IGN**（忽略该信号）。
具体
```cpp
//使用signal函数版本
signal(SIGPIPE, SIG_IGN);

//使用sigaction函数版本
struct sigaction act;
act.sa_handler = SIG_IGN;
sigemptyset(&act.sa_mask);
act.sa_flags = 0;
sigaction(SIGPIPE, &act, NULL);
```
注意：如果希望项目是可移植的，建议使用sigaction函数。

### connfd改为非阻塞模式
为什么connfd要改为非阻塞模式？

当我们获取到http发送的请求报文时，我们仅需要第一行的请求行，剩下的部分我们并不需要，而我们又需要读完它避免粘包（上一次连接的数据影响到下一次的连接）。所以需要使用循环来读。代码如下：

```cpp
    while((n=Readline(connfd, buf, sizeof(buf)))>0);
```
而循环读完缓冲区数据后，Readline函数会阻塞等待缓冲区的数据到来，所以将connfd设置成非阻塞。

### 

### .优化：多线程
### .改为守护进程




最后，其实了解原理后，epoll可以很轻松的换select/poll/甚至是libevent的网络框架。笔者只做了epoll和poll的版本，经过对比，很轻易的可以看出 epoll的效率要远高于 poll。

## 存在的问题及缺陷
对于大文件的浏览还是存在速度过慢的问题。
