#include"webserver.h"
using namespace std;
WebServer::WebServer(int port,int trigMode,int timeoutMs,bool OptLinger,
                    int sqlPort,const char* sqlUser,const char* sqlPwd,
                    const char* dbName,int connPoolNum,int threadNum,
                    bool openLog,int logLevel,int logQueSize):
                    port_(port),openLinger_(OptLinger),timeoutMs_(timeoutMs),isClose_(false),
                    timer_(new HeapTimer()),threadpool_(new ThreadPool(threadNum)),epoller_(new Epoller())
    {
        // 获取当前运行目录
        srcDir_ = getcwd(nullptr, 256);
        assert(srcDir_);
        std::string pathStr(srcDir_);

        // 自动向上层目录探测 resources 文件夹
        // 如果当前目录下没有，就退到上一级找
        if (pathStr.find("build_x86") != std::string::npos) {
            // 如果路径里带有 build_x86，说明我们在子目录里
            strcat(srcDir_, "/../resources/");
        } 
        else {
            // 否则，默认在当前目录下找
            strcat(srcDir_, "/resources/");
        }
        HttpConn::userCount = 0;
        HttpConn::srcDir = srcDir_;
        // [ARM 移植] MySQL 连接池已移除，改用 SQLite（在 UserVerify 中直接打开 webserver.db）
        // SqlConnPool::Instance()->Init("localhost",sqlPort,sqlUser,sqlPwd,dbName,connPoolNum);
        //初始化事件和初始化socket
        InitEventMode_(trigMode);
        if(!InitSocket_()){isClose_ = true;}
        //是否打开ia日志标志
        if(openLog){
            Log::Instance()->init(logLevel,"./log",".log",logQueSize);
            if(isClose_){LOG_ERROR("========== Server init error!==========");}
            else{
                    LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SQLite mode, ThreadPool num: %d", threadNum);
            }
        }
    }
    WebServer::~WebServer(){
        close(listenFd_);
        isClose_ = true;
        free(srcDir_);//不用buf的话这里要手动释放
        // [ARM 移植] MySQL 连接池已移除
        // SqlConnPool::Instance()->ClosePool();
    }
    void WebServer::InitEventMode_(int trigMode){
        listenEvent_ = EPOLLRDHUP;//检测对方是否关闭了写操作
        connEvent_ = EPOLLONESHOT | EPOLLRDHUP;//第一个标志位是fd上的事件触发后会禁用对该fd的事件监听，要重新注册
        switch (trigMode)
        {
        case 0:
            break;
        case 1:
            connEvent_ |= EPOLLET;
            break;
        case 2:
            listenEvent_ |= EPOLLET;
            break;
        case 3:
            connEvent_ |= EPOLLET;
            listenEvent_ |= EPOLLET;
            break;
        default://默认启动高性能模式
            connEvent_ |= EPOLLET;
            listenEvent_ |= EPOLLET;
            break;
        }
        HttpConn::isET = (connEvent_ & EPOLLET);
    }
    void WebServer::Start(){
        int timeMs = -1;//epoll_wait第一个参数，无事件阻塞
        if(!isClose_){LOG_INFO("========== Server start ==========");}
        while(!isClose_){
            if(timeoutMs_ > 0){
                timeMs = timer_->GetNextTick();//获取下一次超时等待事件时间
            }
            int eventCnt = epoller_->Wait(timeMs);
            for(int i=0;i<eventCnt;i++){
                //处理事件
                int fd = epoller_->GetEventFd(i);
                uint32_t events = epoller_->GetEvents(i);
                if(fd == listenFd_){
                    DealListen_();
                }
                else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                    assert(users_.count(fd) > 0);
                    CloseConn_(&users_[fd]);
                }
                else if(events & EPOLLIN){
                    assert(users_.count(fd) > 0);
                    DealRead_(&users_[fd]);
                }
                else if(events & EPOLLOUT){
                    assert(users_.count(fd) > 0);
                    DealWrite_(&users_[fd]);
                }
                else{
                    LOG_ERROR("Unexpected event");
                }
            }
        }
    }
    void WebServer::SendError_(int fd,const char* info){
        assert(fd > 0);
        int ret = send(fd,info,strlen(info),0);
        if(ret < 0){
            LOG_WARN("send error to client [%d] error!",fd);
        }
        close(fd);
    }
    void WebServer::CloseConn_(HttpConn* client){
        assert(client);
        LOG_INFO("Client [%d] quit!",client->GetFd());
        epoller_->DelFd(client->GetFd());
        client->Close();
    }
    void WebServer::AddClinet_(int fd,sockaddr_in addr){
        assert(fd > 0);
        users_[fd].init(fd,addr);
        if(timeoutMs_ > 0){
            timer_->add(fd,timeoutMs_,std::bind(&WebServer::CloseConn_,this,&users_[fd]));
        }
        epoller_->AddFd(fd,EPOLLIN | connEvent_);
        SetFdNonblock(fd);
        LOG_INFO("Client [%d] in!",users_[fd].GetFd());
    }
    //处理监听套接字，主要逻辑三accept新的套接字，并加入timer和epoller中
    void WebServer::DealListen_(){
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        do{
            int fd = accept(listenFd_,(struct sockaddr*)&addr,&len);
            if(fd <= 0){return;}
            else if(HttpConn::userCount >= MAX_FD){
                SendError_(fd,"Server busy!");
                LOG_WARN("Clients is full!");
                return;
            }
            AddClinet_(fd,addr);
        }while(listenEvent_ & EPOLLET);
    }
    //处理读事件，将onRead加入线程池的任务队列中
    void WebServer::DealRead_(HttpConn* client){
        assert(client);
        ExtentTime_(client);
        threadpool_->AddTask(std::bind(&WebServer::OnRead_,this,client));
    }
    //处理写事件，将onWrite加入线程池的任务队列中
    void WebServer::DealWrite_(HttpConn* client){
        assert(client);
        ExtentTime_(client);
        threadpool_->AddTask(std::bind(&WebServer::OnWrite_,this,client));
    }
    void WebServer::ExtentTime_(HttpConn* client){
        assert(client);
        if(timeoutMs_ > 0){timer_->adjust(client->GetFd(),timeoutMs_); }
    }
    void WebServer::OnRead_(HttpConn* client){
        assert(client);
        int ret = -1;
        int readErrno = 0;
        ret = client->read(&readErrno); //读取客户端套借字的数据，读到httpconn的读缓冲区
        if(ret <= 0 && readErrno != EAGAIN){ //读异常
            CloseConn_(client);
            return;
        }
        //业务逻辑的处理
        OnProcess(client);
    }
    //处理读（请求）数据的函数
    void WebServer::OnProcess(HttpConn* client){
        //首先调用process（）进行逻辑处理
        if(client->process()){//根据返回的信息重新将fd置为EPOLLOUT（写）或EPOLLIN（读）
        //读完事件就跟内核说可以写了
            epoller_->MofFd(client->GetFd(),connEvent_ | EPOLLOUT);//响应成功，修改监听事件为写，等待OnWrite_（）发送
        }
        else{
        //写完事件就跟内核说可以读了
            epoller_->MofFd(client->GetFd(),connEvent_ | EPOLLIN);
        }
    }
    void WebServer::OnWrite_(HttpConn* client){
        assert(client);
        int ret = -1;
        int writeErrno = 0;
        ret = client->write(&writeErrno);
        if(client->ToWriteBytes() == 0){
            //传输完成
            if(client->IsKeepAlive()){
                epoller_->MofFd(client->GetFd(),connEvent_ | EPOLLIN);//回归换成监测读事件
                return;
            }
        }
        else if(ret < 0){
            if(writeErrno == EAGAIN){//缓冲区满了
                //继续传输
                epoller_->MofFd(client->GetFd(),connEvent_ | EPOLLOUT);
                return;
            }
        }
        CloseConn_(client);
    }
    bool WebServer::InitSocket_(){
        int ret;
        struct sockaddr_in addr;
        if(port_ > 65535 || port_ < 1024){
            LOG_ERROR("Port:%d error!",port_);
            return false;
        }
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port_);
        //优雅关闭
        {
            struct linger optLinger = {0};
            if(openLinger_){
                //优雅关闭：直到所剩数据发送完毕或超时
                optLinger.l_onoff = 1;
                optLinger.l_linger = 1;
            }
            listenFd_ = socket(AF_INET,SOCK_STREAM,0);
            if(listenFd_ < 0){
                LOG_ERROR("Create socket error!",port_);
                return false;
            }
            ret = setsockopt(listenFd_,SOL_SOCKET,SO_LINGER,&optLinger,sizeof(optLinger));
            if(ret < 0){
                close(listenFd_);
                LOG_ERROR("Init linger error!",port_);
                return false;
            }
        }
        int optval = 1;
        //端口复用 ，只有最后一个套接字会正常接收数据
        ret = setsockopt(listenFd_,SOL_SOCKET,SO_REUSEADDR,(const void*)&optval,sizeof(int));
        if(ret == -1){
            LOG_ERROR("set socket setsockopt error  !");
            close(listenFd_);
            return false;
        }
        //绑定
        ret = bind(listenFd_,(struct sockaddr*)&addr,sizeof(addr));
        if(ret<0){
            LOG_ERROR("Bind Port:%d error!",port_);
            close(listenFd_);
            return false;
        }
        //监听
        ret = listen(listenFd_,6);
        if(ret < 0){
            LOG_ERROR("Listen port:%d error!",port_);
            close(listenFd_);
            return false;
        }
        ret = epoller_->AddFd(listenFd_,listenEvent_ | EPOLLIN);//将监听套接字加入epoller
        if(ret == 0){
            LOG_ERROR("Add listen error!");
            close(listenFd_);
            return false;
        }
        SetFdNonblock(listenFd_);//设置非阻塞
        LOG_INFO("Server port:%d",port_);
        return true;
    }
    //设置非阻塞
    int WebServer::SetFdNonblock(int fd){
        assert(fd > 0);
        return fcntl(fd,F_SETFL,fcntl(fd,F_GETFD,0) | O_NONBLOCK);
    }