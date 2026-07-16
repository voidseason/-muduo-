#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include<cstring>
#include <cassert>
#include <functional>
#include <memory>
#include <typeinfo>
#include <unistd.h> 
#include <unordered_map>
#include <signal.h>
#include "log.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <mutex>
#include <condition_variable>
#include <bits/std_thread.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#define BUFFER_DEFAULT_SIZE 1024
#define MAX_LISTEN 8192
using namespace ns_log;

/*********
 * Buffer设计：
 * 1.将底层接收数据到存到缓冲区
 * 2.将底层发送数据存到缓冲区
 *
 * 面向对象：
 * 所有的Reactor
 *
 * 是否涉及线程安全：
 * 后期看具体使用场景，大概率存在线程安全
 * */
class Buffer
{
private:
    std::vector<char> _buffer; // 使⽤vector进行内存空间管理
    uint64_t _reader_idx;
    uint64_t _writer_idx;

public:
    Buffer() : _reader_idx(0), _writer_idx(0), _buffer(BUFFER_DEFAULT_SIZE) {}
    char *Begin() { return &*_buffer.begin(); }
    // 获取当前写入起始地址,_buffer的空间地址，写上偏移量
    char *WriterPosition() { return Begin() + _writer_idx; }
    // 获取当前读取起始地址
    char *ReadPosition() { return Begin() + _reader_idx; }
    // 获取缓冲区末尾空闲空间⼤⼩-写偏移之后的空闲空间, 总体空间⼤⼩减去写偏移
    uint64_t TailIdleSize() { return _buffer.size() - _writer_idx; }
    // 获取缓冲区起始空闲空间⼤⼩-读偏移之前的空闲空间
    uint64_t HeadIdleSize() { return _reader_idx; }
    // 获取可读数据大⼩ = 写偏移 - 读偏移
    uint64_t ReadAbleSize() { return _writer_idx - _reader_idx; }

    // 确保可写空间足够（整体空闲空间够了就移动数据，否则就扩容）
    void EnsureWriteSpace(uint64_t len)
    {
        if (TailIdleSize() >= len)
            return;
        if (len <= TailIdleSize() + HeadIdleSize())
        {
            uint64_t rsz = ReadAbleSize();
            std::copy(ReadPosition(), ReadPosition() + rsz, Begin()); // 把可读数据拷贝到起始位置
            _reader_idx = 0;
            _writer_idx = rsz;
        }
        else
        {
            _buffer.resize(_writer_idx + len);
        }
    }

    /*对外接口*/
public:
    // 将读偏移向后移动
    void MoveReadOffset(uint64_t len)
    {
        if (len == 0)
            return;
        assert(len <= ReadAbleSize());
        _reader_idx += len;
    }
    // 将写偏移向后移动
    void MoveWriteOffset(uint64_t len)
    {
        assert(len <= TailIdleSize());
        _writer_idx += len;
    }
    // 写⼊数据
    void Write(const void *data, uint64_t len)
    {
        if (len == 0)
            return;
        EnsureWriteSpace(len);
        const char *d = (const char *)data;
        std::copy(d, d + len, WriterPosition());
    }
    void WriteAndPush(const void *data, uint64_t len)
    {
        Write(data, len);
        MoveWriteOffset(len);
    }
    void WriteString(const std::string &data)
    {
        return Write(data.c_str(), data.size());
    }
    void WriteStringAndPush(const std::string &data)
    {
        WriteString(data);
        MoveWriteOffset(data.size());
    }
    void WriteBuffer(Buffer &data)
    {
        return Write(data.ReadPosition(), data.ReadAbleSize());
    }
    void WriteBufferAndPush(Buffer &data)
    {
        WriteBuffer(data);
        MoveWriteOffset(data.ReadAbleSize());
    }
    // 读取数据
    void Read(void *buf, uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::copy(ReadPosition(), ReadPosition() + len, (char *)buf);
    }
    void ReadAndPop(void *buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadOffset(len);
    }
    std::string ReadAsString(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::string str;
        str.resize(len);
        Read(&str[0], len);
        return str;
    }
    std::string ReadAsStringAndPop(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::string str = ReadAsString(len);
        MoveReadOffset(len);
        return str;
    }
    char *FindCRLF()
    {
        char *res = (char *)memchr(ReadPosition(), '\n', ReadAbleSize());
        return res;
    }
    std::string GetLine()
    {
        char *pos = FindCRLF();
        if (pos == NULL)
        {
            return "";
        }
        // +1是为了把换行字符也取出来。
        return ReadAsString(pos - ReadPosition() + 1);
    }
    std::string GetLineAndPop()
    {
        std::string str = GetLine();
        MoveReadOffset(str.size());
        return str;
    }
    void Clear()
    {
        _reader_idx = 0;
        _writer_idx = 0;
    }
};

/*该类后续想改进的话，可以用工厂模式来设计分别针对客户端、服务端的套接字*/
/******
 * Socket类：
 * 主要作用：主要是创建一个套接字，进行这个套接字的数据接收与发送的功能
 * 1.创建监听套接字
 * 2.绑定套接字
 * 3.监听套接字
 * 4.连接
 * 5.接收
 * 6.创建服务端套接字
 * 7.创建客户端套接字
 * 8.创建
 * 
 *
 * 是否多线程，以及有线程安全问题：
 * 没有，每个线程独立拥有该Socket对象
 * 所以不存在线程安全问题
 */
class Socket
{
private:
    int _sockfd;

public:
    Socket() : _sockfd(-1) {}
    Socket(int fd) : _sockfd(fd) {}
    ~Socket() { Close(); }
    int Fd() { return _sockfd; }
    bool Create()
    {
        _sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (_sockfd < 0)
        {
            LOG(ERROR) << "创建套接字失败" << std::endl;
            return false;
        }
        return true;
    }

    bool Bind(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        socklen_t len = sizeof(struct sockaddr_in);
        int ret = bind(_sockfd, (struct sockaddr *)&addr, len);
        if (ret < 0)
        {
            LOG(ERROR) << "bind错误" << std::endl;
            return false;
        }
        return true;
    }

    bool Listen(int backlog = MAX_LISTEN)
    {
        int ret = listen(_sockfd, backlog);
        if (ret < 0)
        {
            LOG(ERROR) << "listen监听失败" << std::endl;
            return false;
        }
        return true;
    }

    bool Connect(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        socklen_t len = sizeof(struct sockaddr_in);
        int ret = connect(_sockfd, (struct sockaddr *)&addr, len);
        if (ret < 0)
        {
            LOG(ERROR) << "connect错误" << std::endl;
            return false;
        }
        return true;
    }

    int Accept()
    {
        int newfd = accept(_sockfd, NULL, NULL);
        if (newfd < 0)
        {
            LOG(ERROR) << "Accpect失败" << std::endl;
            return -1;
        }
        return newfd;
    }

    ssize_t Recv(void *buf, size_t len, int flag = 0)
    {
        ssize_t ret = recv(_sockfd, buf, len, flag);
        if (ret <= 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            LOG(ERROR) << "SOCKET Recv失败" << std::endl;
            return -1;
        }
        return ret;
    }
    ssize_t NonBlockRecv(void *buf, size_t len)
    {
        return Recv(buf, len, MSG_DONTWAIT); // MSG_DONTWAIT 表示当前接收为非阻塞
    }

    ssize_t Send(const void *buf, size_t len, int flag = 0)
    {
        // ssize_t send(int sockfd, void *data, size_t len, int flag);
        ssize_t ret = send(_sockfd, buf, len, flag);
        if (ret < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            LOG(ERROR) << "SOCKET Send失败";
            return -1;
        }
        return ret; // 实际发送的数据长度
    }
    ssize_t NonBlockSend(void *buf, size_t len)
    {
        if (len == 0)
            return 0;
        return Send(buf, len, MSG_DONTWAIT); // MSG_DONTWAIT 表示当前发送为非阻塞。
    }
    /*后续看代码是否会用到NonBlockSend和NonBlockRecv接口，因为NonBlock接口显得其他两个接口有点多余*/
    void NonBlock()
    {
        // int fcntl(int fd, int cmd, ... /* arg */ );
        int flag = fcntl(_sockfd, F_GETFL, 0);
        fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
    }
    void Close()
    {
        if (_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }

    // 创建服务端Socket类
    bool CreateServer(uint16_t port, const std::string &ip = "0.0.0.0", bool block_flag = false)
    {
        // 1. 创建套接字，2. 绑定地址，3. 开始监听，4. 设置非阻塞， 5. 启动地址重⽤
        if (Create() == false)
            return false;
        if (block_flag)
            NonBlock();
        if (Bind(ip, port) == false)
            return false;
        if (Listen() == false)
            return false;
        ReuseAddress();
        return true;
    }
    // 客户端
    bool CreateClient(uint16_t port, const std::string &ip)
    {
        // 1. 创建套接字，2.指向连接服务器
        if (Create() == false)
            return false;
        if (Connect(ip, port) == false)
            return false;
        return true;
    }
    void ReuseAddress()
    {
        int val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(int));
        val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, (void *)&val, sizeof(int));
    }
};

/*******
 * Channel模块：
 * 主要作用：可以单个文件描述符进行一个监控的修改，事件的回调处理
 */
class Poller;
class EventLoop;
class Channel
{
private:
    int _fd;
    EventLoop *_loop;
    uint32_t _events;  // 当前需要监控的事件
    uint32_t _revents; // 当前连接触发的事件
    using EventCallback = std::function<void()>;
    EventCallback _read_callback;  // 可读事件监控回调函数
    EventCallback _write_callback; // 可写事件监控回调函数
    EventCallback _error_callback; // 错误事件监控回调函数
    EventCallback _close_callback; // 连接事件监控回调函数
    EventCallback _event_callback; // 任意事件监控回调函数
public:
    Channel(EventLoop *loop, int fd) : _fd(fd), _events(0), _revents(0), _loop(loop) {}
    int Fd() { return _fd; }
    void SetREvents(uint32_t events) { _revents = events; } // 设置实际就绪的事件
    uint32_t Events() { return _events; }                   // 获取想要监控的事件
    void SetReadCallback(const EventCallback &cb) { _read_callback = cb; }
    void SetWriteCallback(const EventCallback &cb) { _write_callback = cb; }
    void SetErrorCallback(const EventCallback &cb) { _error_callback = cb; }
    void SetCloseCallback(const EventCallback &cb) { _close_callback = cb; }
    void SetEventCallback(const EventCallback &cb) { _event_callback = cb; }
    bool ReadAble() { return (_events & EPOLLIN); }   // 是否监控了可读
    bool WriteAble() { return (_events & EPOLLOUT); } // 是否监控了可写
    // 启动读事件监控
    void EnableRead()
    {
        _events |= EPOLLIN;
        Update();
    }
    // 启动写事件监控
    void EnableWrite()
    {
        _events |= EPOLLOUT;
        Update();
    }
    // 关闭读事件监控
    void DisableRead()
    {
        _events &= ~EPOLLIN;
        Update();
    }
    // 关闭写事件监控
    void DisableWrite()
    {
        _events &= ~EPOLLOUT;
        Update();
    }
    // 关闭所有事件监控
    void DisableAll()
    {
        _events = 0;
        Update();
    }
    // 移除某事件监控
    void Remove();
    // 向事件监控集中更新或者修改某事件监控
    void Update();
    // 检测到事件就绪后执行该函数
    void HandleEvent()
    {
        if ((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            if (_read_callback)
                _read_callback();
        }
        // 有可能会释放连接的操作事件一次只处理一次
        if (_revents & EPOLLOUT)
        {
            if (_write_callback)
                _write_callback();
        }
        if (_revents & EPOLLERR)
        {
            if (_error_callback)
                _error_callback();
        }
        if (_revents & EPOLLHUP)
        {
            if (_close_callback)
                _close_callback();
        }
        if (_event_callback)
            _event_callback();
    }
};

#define MAX_EPOLLEVENTS 1024
// Poller:Poller模块是对epoll进行封装的⼀个模块，主要实现epoll的IO事件监控的添加，修改，移除等，以及获取活跃连接功能
// 也就是可以Channel模块进行一个大量的管理
// Poller可以对每个channel进行读写监控修改，以及获取活跃连接时，会对其对应获取的所有channel进行其HanleEvent处理，也就是事件回调
//Poller不在乎你是什么事件回调，而是会把你的revent设置对应的channel，也就是告诉你的channel什么事件就绪了
//然后channel进行HandleEvent处理的时候，是拿设置好的revent与是否启动读写事件对应
//Poller也提供接口对其Channel进行一个读写事件的修改
class Poller
{
private:
    int _epfd;
    struct epoll_event _evs[MAX_EPOLLEVENTS];
    std::unordered_map<int, Channel *> _channels;

private:
    void Update(Channel *channel, int op)
    {
        int fd = channel->Fd();
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = channel->Events();
        int ret = epoll_ctl(_epfd, op, fd, &ev);
        if (ret < 0)
        {
            LOG(ERROR) << "EPOLLCTL FAILED!" << std::endl;
            abort(); // 退出程序
        }
        return;
    }
    // 判断一个Channel是否已经添加了事件监控
    bool HasChannel(Channel *channel)
    {
        auto it = _channels.find(channel->Fd());
        if (it == _channels.end())
        {
            return false;
        }
        return true;
    }

public:
    Poller()
    {
        _epfd = epoll_create(MAX_EPOLLEVENTS);
        if (_epfd < 0)
        {
            LOG(ERROR) << "Epoll Create ERR: " << strerror(errno) << std::endl;
            abort(); // 退出程序
        }
    }
    // 添加或修改监控事件
    void UpdateEvent(Channel *channel)
    {
        bool ret = HasChannel(channel);
        if (ret == false)
        {
            // 不存在则添加
            _channels.insert(std::make_pair(channel->Fd(), channel));
            return Update(channel, EPOLL_CTL_ADD);
        }
        return Update(channel, EPOLL_CTL_MOD);
    }
    // 移除监控
    void RemoveEvent(Channel *channel)
    {
        auto it = _channels.find(channel->Fd());
        if (it != _channels.end())
        {
            _channels.erase(it);
        }
        Update(channel, EPOLL_CTL_DEL);
    }

    // 开始监控，返回活跃连接
    void Poll(std::vector<Channel *> *active)
    {
        // 阻塞监控
        int nfds = epoll_wait(_epfd, _evs, MAX_EPOLLEVENTS, -1);
        if (nfds < 0)
        {
            if (errno == EINTR)
            {
                return;
            }
            LOG(ERROR) << "EPOLL WAIT ERROR: " << strerror(errno) << std::endl;
            abort();
        }

        for (int i = 0; i < nfds; i++)
        {
            auto it = _channels.find(_evs[i].data.fd);
            assert(it != _channels.end());
            it->second->SetREvents(_evs[i].events);
            active->push_back(it->second);
        }
        return;
    }
};

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;
class TimerTask
{
private:
    uint64_t _id;         // 定时任务对象ID
    uint32_t _timeout;    // 定时任务的超时时间
    bool _cancel;         // false-表示没有被取消   true-被取消
    TaskFunc _task_cb;    // 定时器对象要执行的定时任务
    ReleaseFunc _release; // 用于删除TimerWheel中保存的定时器对象信息
public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc &cb) : _id(id), _timeout(delay), _task_cb(cb), _cancel(false)
    {
    }
    ~TimerTask()
    {
        if (_cancel == false)
            _task_cb();
        _release();
    }
    void Cancel()
    {
        _cancel = true;
    }
    void SetRelease(const ReleaseFunc &cb) { _release = cb; }
    uint32_t DelayTime()
    {
        return _timeout;
    }
};

class EventLoop;
class TimerWheel
{
private:
    using WeakTask = std::weak_ptr<TimerTask>;
    using PtrTask = std::shared_ptr<TimerTask>;
    int _tick;     // 当前的秒针，走到哪里，释放哪里，就相当于执行哪里的任务
    int _capacity; // 表盘最大数量
    std::vector<std::vector<PtrTask>> _wheel;
    std::unordered_map<uint64_t, WeakTask> _timers;

    EventLoop *_loop;
    int _timerfd; // 定时器描述符--可读事件回调就是读取计数器，执行定时任务
    std::unique_ptr<Channel> _timer_channel;

private:
    void RemoveTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
        {
            _timers.erase(it);
        }
    }
    static int CreateTimerfd()
    {
        int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (timerfd < 0)
        {
            LOG(ERROR) << "TIMERFD CREATE FAILED!" << std::endl;
            abort();
        }
        struct itimerspec itime;
        itime.it_value.tv_sec = 1; // 第一次超时时间为1s后
        itime.it_value.tv_nsec = 0;
        itime.it_interval.tv_sec = 1; // 第一次超时后，之后的超时时间
        itime.it_interval.tv_nsec = 0;
        timerfd_settime(timerfd, 0, &itime, nullptr);
        return timerfd;
    }
    int ReadTimerfd()
    {
        uint64_t times;
        int ret = read(_timerfd, &times, 8);
        if (ret < 0)
        {
            LOG(ERROR) << "READ TIMERFD FAILED!" << std::endl;
            abort();
        }
        return times;
    }

    // 这个函数应该每秒钟执行一次，相当于指针向后走了一秒
    void RunTimerTask()
    {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear(); // 清空指定位置的数组，就会把数组中保存的所有管理定时器对象的shared_ptr释放掉
    }
    void OnTime()
    {
        //根据实际的超时次数，执行对应的定时任务
        int times = ReadTimerfd();
        for(int i=0;i<times;i++)
        {
            RunTimerTask();
        }
    }

    // 添加定时任务
    void TimerAddInLoop(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        PtrTask pt(new TimerTask(id, delay, cb));
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
        _timers[id] = WeakTask(pt);
    }

    void TimerRefreshInLoop(uint64_t id)
    {
        // 通过保存的定时器对象的weakptr生成构造一个shared_ptr出来，添加到一个轮子里
        auto it = _timers.find(id);
        if (it == _timers.end())
        {
            return; // 没找到定时任务
        }
        PtrTask pt = it->second.lock(); // 获取WeakPtr管理的对象对应的Shared_ptr
        int delay = pt->DelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }

    void TimerCancelInLoop(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
        {
            return; // 没找到定时任务
        }
        PtrTask pt = it->second.lock();
        if(pt)pt->Cancel();
    }

public:
    TimerWheel(EventLoop *loop) : _capacity(60), _tick(0), _wheel(_capacity), _loop(loop),
                                  _timerfd(CreateTimerfd()), _timer_channel(new Channel(_loop, _timerfd))
    {
        _timer_channel->SetReadCallback(std::bind(&TimerWheel::OnTime, this));
        _timer_channel->EnableRead();
    }
    /*因为很多定时任务都涉及到对连接的操作，因此需要考虑线程安全*/
    /*如果不想加锁，那就把对定时器的所有操作，都放到一个线程中,也就是EventLoop里面的RunInLoop*/
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb);

    // 刷新/延迟定时任务
    void TimerRefresh(uint64_t id);

    void TimerCancel(uint64_t id);
    /*该接口存在线程安全问题，只能在对应的EventLoop线程中执行*/
    bool HasTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
        {
            return false;
        }
        return true;
    }
};





/*****
 * EventLoop模块：主要是有一个主Channel,然后有一个Poller对象
 * 也就是说我们之前的Channel,Poller并不直接暴露在外面，而是通过我们的EventLoop对我们的Channel,Poller进行管理
 * 相当于我们的主Reactor，然后对从属Reactor进行一个管理模块
 * 接口设计：
 * 总体设计就是创建其线程，然后对该线程的任务进行管理，但是注意这里没有我们的一个对文件描述符进行一
 */
class EventLoop
{
    using Functor = std::function<void()>;

private:
    std::thread::id _thread_id;
    int _event_fd; // eventfd唤醒IO事件监控有可能导致的阻塞
    std::unique_ptr<Channel> _event_channel;
    Poller _poller;              // 进行所有描述符的事件监控
    std::vector<Functor> _tasks; // 任务池-----重要：防止线程不安全，每次等线程先处理完自己的事情
    std::mutex _mutex;           // 实现任务池操作的线程安全
    TimerWheel _timer_wheel;     // 定时器模块
public:
    void RunAllTask()
    {
        std::vector<Functor> functor;
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.swap(functor);
        }
        for (auto &f : functor)
            f();
        return;
    }
    static int CreateEventFd()
    {
        int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd < 0)
        {
            LOG(ERROR) << "eventfd failed!" << std::endl;
            abort();
        }
        return efd;
    }
    void ReadEventfd()
    {
        uint64_t res = 0;
        int ret = read(_event_fd, &res, sizeof(res));
        if (ret < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                return;
            }
            LOG(ERROR) << "READ EVENTFD FAILED!" << std::endl;
            abort();
        }
        return;
    }
    void WeakUpEventFd()
    {
        uint64_t val = 1;
        int ret = write(_event_fd, &val, sizeof(val));
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                return;
            }
            LOG(ERROR) << "WRITE EVENTFD FAILED!" << std::endl;
            abort();
        }
        return;
    }

public:
    EventLoop() : _thread_id(std::this_thread::get_id()),
                  _event_fd(CreateEventFd()),
                  _event_channel(new Channel(this, _event_fd)),
                  _timer_wheel(this)
    {
        // 给eventfd添加刻度事件回调函数,读取eventfd
        _event_channel->SetReadCallback(std::bind(&EventLoop::ReadEventfd, this));
        _event_channel->EnableRead();
    }

    // 启动EnventLoop模块,三步走--事件监控->就绪事件处理->执行任务
    void Start()
    {
        while(1)
        {
            std::vector<Channel *> actives;
            // 这里的Poll()是一个阻塞式获取监控，但是由于eventfd的设置，每次都会设置eventfd被唤醒了，所以无需担心
            // 看看是否可以把Poll换成非阻塞式呢？不行的，因为我们这是一个Loop循环模块，所以阻塞的话会让cpu一直在这个循环上下执行
            _poller.Poll(&actives);
            for (auto &channel : actives)
            {
                channel->HandleEvent();
            }
            RunAllTask();
        }

    }
    // 用于判断当前线程是否是EventLoop对应的线程
    bool IsInLoop()
    {
        return _thread_id == std::this_thread::get_id();
    }
    void AssertInLoop()
    {
        assert(_thread_id == std::this_thread::get_id());
    }
    // 判断要执行的任务是否处于当前线程中，如果是则执行，不是则压入队列
    void RunInLoop(const Functor &cb)
    {
        if (IsInLoop())
        {
            return cb();
        }
        QueueInLoop(cb);
    }

    // 将操作压入队列
    void QueueInLoop(const Functor &cb)
    {
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.push_back(cb);
        }
        // 唤醒有可能因为没有事件就绪，而导致的epoll阻塞
        // 其实就是给eventfd写入一个数据,eventfd就会触发可读事件
        WeakUpEventFd();
    }

    // 添加/修改描述符的事件监控
    void UpdateEvent(Channel *channel)
    {
        _poller.UpdateEvent(channel);
    }
    // 移除描述符的事件监控
    void RemoveEvent(Channel *channel)
    {
        _poller.RemoveEvent(channel);
    }
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        return _timer_wheel.TimerAdd(id, delay, cb);
    }
    void TimerRefresh(uint64_t id)
    {
        return _timer_wheel.TimerRefresh(id);
    }
    void TimerCancel(uint64_t id)
    {
        return _timer_wheel.TimerCancel(id);
    }
    bool HasTimer(uint64_t id)
    {
        return _timer_wheel.HasTimer(id);
    }
};


class LoopThread
{
    private:
    /*用于实现_loop获取的同步关系,避免线程创建了，但是_loop还没有实例化之前去获取loop*/
    std::mutex _mutex;//互斥锁
    std::condition_variable _cond;//条件变量
    /*---------------------------------*/

    EventLoop *_loop;//EventLoop指针变量，这个对象需要在线程内实例化
    std::thread _thread;//EventLoop对应的线程
    private:
    /*实例化EventLoop对象，唤醒_cond上有可能阻塞的线程，并且开始运行EventLoop模块的功能*/
    void ThreadEntry()
    {
        EventLoop loop;//这里不用new一个对象是因为我们想这个loop生命周期随着LoopThread
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _loop = &loop;
            _cond.notify_all();
        }
        loop.Start();
    }
    public:
    //创建线程，设定线程入口函数
    LoopThread():_loop(nullptr),_thread(std::thread(&LoopThread::ThreadEntry,this)){}
    //返回当前线程关联的EventLoop对象指针
    EventLoop *GetLoop()
    {
        EventLoop *loop =nullptr;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cond.wait(lock,[&](){return _loop!=nullptr;});
            loop = _loop;
        }
        return loop;
    }
};
class LoopThreadPool
{
    private:
    int _thread_count;//丛属线程数量
    int _next_idx;
    EventLoop *_baseloop;//主Reactor，运行在主线程,若从属线程数量为0，则所有操作都在baseloop中进行
    std::vector<LoopThread*> _threads;//保存所有的LoopThread对象
    std::vector<EventLoop *> _loops;//从属线程数量>0，则从_loops中进行线程EventLoop分配
    public:
    LoopThreadPool(EventLoop *baseloop):_baseloop(baseloop),_thread_count(0),_next_idx(0)
    {}
    //设置线程数量
    void SetThreadCount(int count){_thread_count = count;}
    //创建并运行所有的从属Reactor线程
    void Create()
    {
        if(_thread_count>0)
        {
            _threads.resize(_thread_count);
            _loops.resize(_thread_count);
            for(int i=0;i<_thread_count;i++)
            {
                _threads[i] = new LoopThread();
                _loops[i] = _threads[i]->GetLoop();
            }
        }
        return ;
    }
    EventLoop *NextLoop()
    {
        if(_thread_count==0)
        {
            return _baseloop;
        }
        _next_idx = (_next_idx+1)%_thread_count;
        return _loops[_next_idx];
    }

};
void Channel::Remove() // 移除监控
{
    return _loop->RemoveEvent(this);
}
void Channel::Update()
{
    return _loop->UpdateEvent(this);
}
void TimerWheel::TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerAddInLoop, this, id, delay, cb));
}

// 刷新/延迟定时任务
void TimerWheel::TimerRefresh(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerRefreshInLoop, this, id));
}

void TimerWheel::TimerCancel(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerCancelInLoop, this, id));
}

class Any
{
    private:
    class holder
    {
        public:
        virtual ~holder()
        {}
        virtual const std::type_info& type() = 0;
        virtual holder *clone() = 0;
    };

    template<class T>
    class placeholder:public holder
    {
        public:
        placeholder(const T &val) :_val(val){}
        virtual const std::type_info& type()
        {
            return typeid(T);
        }
        virtual holder *clone()
        {return new placeholder(_val);}
        public:
        T _val;
    };
    holder *_content;
    public:
    Any():_content(nullptr)
    {}
    template<class T>
    Any(const T &val):_content(new placeholder<T>(val))
    {}
    Any(const Any &other):_content(other._content ? other._content->clone(): nullptr){}
    ~Any(){delete _content;}

    Any &swap(Any &other)
    {
        std::swap(_content,other._content);
        return *this;
    }
    //返回子类对象保存的数据的指针
    template<class T>
    T *get()
    {
        assert(typeid(T)==_content->type());
        return &((placeholder<T>*)_content)->_val;
    }
    template<class T>
    Any &operator=(const T &val)
    {
        Any(val).swap(*this);
        return *this;
    }
    Any &operator=(const Any &other)
    {
        Any(other).swap(*this);
        return *this;
    }
};

class Connection;
// DISCONNECTED----连接关闭状态  CONNECTING --- 连接建立成功-待处理状态
// CONNECTED----连接建立完成，可以通信的状态    DISCONNECTING---待关闭状态
typedef enum
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
} ConnStatu;
using PtrConnection = std::shared_ptr<Connection>;

class Connection : public std::enable_shared_from_this<Connection>
{
private:
    uint64_t _conn_id; // 连接的唯一ID，便于连接的管理和查找
    // uint64_t  _timer_id;  //定时器ID,必须是唯一的，这块为了简化操作使用_conn_id作为定时器ID
    int _sockfd;                   // 连接管理的描述符
    bool _enable_inactive_release; // 连接是否启动非活跃销毁的判断标志,默认为false
    ConnStatu _statu;              //
    EventLoop *_loop;              // 连接所关联的EventLoop
    Socket _socket;                // 套接字管理连接
    Channel _channel;              // 连接的事件管理
    Buffer _in_buffer;             // 输入缓冲区----存放从socket中读取到的数据
    Buffer _out_buffer;            // 输出缓冲区-----存放要发送给对端的数据
    Any _context;                  // 请求的接收处理上下文

    /*这四个回调函数,是让服务器模块来设置的（其实服务器模块的处理回调也是组件使用者设置的）*/
    using ConnectedCallback = std::function<void(const PtrConnection &)>;
    using MessageCallback = std::function<void(const PtrConnection &, Buffer *)>;
    using ClosedCallback = std::function<void(const PtrConnection &)>;
    using AnyEventCallback = std::function<void(const PtrConnection &)>;
    ConnectedCallback _connected_callback;
    MessageCallback _message_callback;
    ClosedCallback _closed_callback;
    AnyEventCallback _event_callback;
    /*组件内的连接关闭回调--组件内设置，因为服务器组件内会把所有的连接管理起来*/
    /*一旦某个连接关闭，就应该从管理的地方移除掉*/
    ClosedCallback _server_closed_callback;

private:
    /*-------五个channel的事件回调函数----------*/
    // 描述符触发可读事件后调用的函数，接收socket数据放到接收缓冲区中,然后调用_messsage_callback
    void HandleRead()
    {
        // 1.接收socket的数据,放到缓冲区
        char buf[65536];
        while (true) {
          ssize_t ret = _socket.NonBlockRecv(buf, sizeof(buf) - 1);
          if (ret < 0)
            return ShutdownInLoop(); // 出错了，但是不能直接关闭连接
          if (ret == 0) // 这里的ret等于0表示的是没有读取到数据，而不是连接断开了，连接断开返回的是-1
            break;
          _in_buffer.WriteAndPush(buf, ret); // 将数据放入输入缓冲区
        }
        // 2.调用message_callback进行业务处理
        if (_in_buffer.ReadAbleSize() > 0)
        {
            // shared_from_this---从当前对象自身获取自身的shared_ptr
            return _message_callback(shared_from_this(), &_in_buffer);
        }
    }
    // 描述符触发可写事件后调用的函数，将发送缓冲区数据进行发送
    void HandleWrite()
    {
        ssize_t ret = _socket.NonBlockSend(_out_buffer.ReadPosition(), _out_buffer.ReadAbleSize());
        if (ret < 0)
        {
            // 发送错误就该关闭连接
            if (_in_buffer.ReadAbleSize() > 0)
            {
                _message_callback(shared_from_this(), &_in_buffer);
            }
            return Release(); // 这时候就是实际的关闭操作
        }
        _out_buffer.MoveReadOffset(ret);
        if (_out_buffer.ReadAbleSize() == 0)
        {
            _channel.DisableWrite(); // 关闭写事件监控
            // 如果当前是连接待关闭状态，则有数据，发送完数据释放连接，没有数据则直接释放
            if (_statu == DISCONNECTING)
            {
                return Release();
            }
        }
    }
    // 描述符触发挂断事件
    void HandleClose()
    {
        if (_in_buffer.ReadAbleSize() > 0)
        {
            _message_callback(shared_from_this(), &_in_buffer);
        }
        return Release();
    }
    // 描述符触发出错事件
    void HandleError()
    {
        if (_in_buffer.ReadAbleSize() > 0)
        {
            _message_callback(shared_from_this(), &_in_buffer);
        }
        return Release();
    }
    // 描述符触发任意事件:1.刷新连接的活跃度--延迟定时销毁任务   2.调用组件使用者的任意事件回调
    void HandleEvent()
    {
        if(_enable_inactive_release==true)
        {
            _loop->TimerRefresh(_conn_id);
        }
        if(_event_callback)
        {
            _event_callback(shared_from_this());
        }
    }
    /*------------------------------------*/

    // 连接获取之后，所处的状态下要进行各种设置(给一个channel设置事件回调，启动读监控)
    void EstablishedInLoop()
    {
        //1.修改连接
        assert(_statu==CONNECTING);//当前的状态必须一定是上层的半连接状态
        _statu = CONNECTED;
        //2.启动读事件监控
        //一旦启动读事件监控就有可能会立即触发读事件，如果这时候启动了非活跃连接销毁
        _channel.EnableRead();
        //3.调用回调函数
        if(_connected_callback) _connected_callback(shared_from_this());
    }
    // 这个接口才是实际的释放接口
    void ReleaseInLoop()
    {
        //1.修改连接状态，将其置为DISCONNECTED
        _statu = DISCONNECTED;
        //2.移除连接的事件监控
        _channel.Remove();
        //3.关闭描述符
        _socket.Close();
        //4.如果当前定时器队列中还有任务，则取消任务
        if(_loop->HasTimer(_conn_id)) CancelInactiveReleaseInLoop();
        //5.调用关闭回调函数，避免先移除服务器管理的连接信息导致Connection被释放，再去处理会出错，因此先调用用户的回调函数
        if(_closed_callback) _closed_callback(shared_from_this());
        //移除服务器内部管理的连接信息
        if(_server_closed_callback) _server_closed_callback(shared_from_this());
    }

    //这个接口并不是实际的发送接口，而只是把数据放到了发送缓冲区，启动了可写事件监控，而事件发送的接口是HandleWrite
    void SendInLoop(Buffer &buf)
    {
        if(_statu == DISCONNECTED) return ;
        _out_buffer.WriteBufferAndPush(buf);
        if(!_channel.WriteAble()) 
        {
            _channel.EnableWrite();
        }
    }
    //这个关闭操作并不是实际的连接释放操作，需要判断还有没有数据待处理，待发送
    void ShutdownInLoop()
    {
        _statu = DISCONNECTING;//设置连接为半关闭状态
        if(_in_buffer.ReadAbleSize()>0)
        {
            if(_message_callback) _message_callback(shared_from_this(),&_in_buffer);
        }
        if(_out_buffer.ReadAbleSize()>0)
        {
            if(!_channel.WriteAble()) 
            {
                _channel.EnableWrite();
            } 
        }
        if(_out_buffer.ReadAbleSize()==0)
        {
            Release();
        }
    }
    //启动非活跃连接超时释放规则
    void EnableInactiveReleaseInLoop(int sec)
    {
        //1.将判断标志 _enable_inactive_release 置为true
        _enable_inactive_release = true;
        //2.如果当前定时销毁任务已存在，那就刷新延迟一下即可
        if(_loop->HasTimer(_conn_id)) _loop->TimerRefresh(_conn_id);
        //3.如果不存在定时销毁任务，则新增
        _loop->TimerAdd(_conn_id,sec,std::bind(&Connection::Release,this));
    }
    void CancelInactiveReleaseInLoop()
    {
        _enable_inactive_release = false;
        if(_loop->HasTimer(_conn_id))
        _loop->TimerCancel(_conn_id);
    }
    void UpgradeInLoop(const Any &context, 
                    const ConnectedCallback &conn,
                    const MessageCallback &msg,
                    const ClosedCallback &closed, 
                    const AnyEventCallback &event)
    {
        _context = context;
        _connected_callback = conn;
        _message_callback = msg;
        _closed_callback = closed;
        _event_callback = event;
    }

public:
    Connection(EventLoop *loop, uint64_t conn_id, int sockfd)
    :_conn_id(conn_id),_sockfd(sockfd),_enable_inactive_release(false),_loop(loop),_statu(CONNECTING),
    _socket(sockfd),_channel(loop,_sockfd)
    {
        _channel.SetCloseCallback(std::bind(&Connection::HandleClose,this));
        _channel.SetEventCallback(std::bind(&Connection::HandleEvent,this));
        _channel.SetReadCallback(std::bind(&Connection::HandleRead,this));
        _channel.SetWriteCallback(std::bind(&Connection::HandleWrite,this));
        _channel.SetErrorCallback(std::bind(&Connection::HandleError,this));
    }
    ~Connection()
    {
        LOG(DEBUG)<<"RELEASE CONNECTION: "<<this<<std::endl;
    }
    // 获取连接所管理的描述符
    int Fd(){return _sockfd;}
    // 获取连接的ID
    int ID(){return _conn_id;}
    // 是否处于CONNECTED状态
    bool Connected(){return (_statu==CONNECTED);}
    // 设置上下文---连接建立完成时进行调用
    void SetContext(const Any &context){_context = context;}
    // 获取上下文，返回的是指针
    Any *GetContext(){return &_context;}
    // 设置连接回调函数
    void SetConnectedCallback(const ConnectedCallback &cb){_connected_callback = cb;}
    void SetMessageCallback(const MessageCallback &cb){_message_callback = cb;}
    void SetClosedCallback(const ClosedCallback &cb){_closed_callback = cb;}
    void SetSrvClosedCallback(const ClosedCallback &cb){_server_closed_callback = cb;}
    void SetAnyEventCallback(const AnyEventCallback &cb){_event_callback = cb;}
    // 连接建立就绪后，进行channel回调设置,启动读监控，调用_connected_callback
    void Established()
    {
        _loop->RunInLoop(std::bind(&Connection::EstablishedInLoop,this));
    }
    // 发送数据，将数据放到发送缓冲区，启动写事件监控
    void Send(const char *data, size_t len)
    {
        //有个问题:外界传入的data，可能是个临时的空间，我们现在只是把发送操作压入了任务池，有可能并没有被立即执行
        //因此有可能执行的时候，data指向的空间可能已经被释放
        Buffer buf;
        buf.WriteAndPush(data,len);
        _loop->RunInLoop(std::bind(&Connection::SendInLoop,this,std::move(buf)));
    }
    // 提供给组件使用者的关闭接口--并不直接关闭，需要关闭有没有数据待处理
    void Shutdown()
    {
        _loop->RunInLoop(std::bind(&Connection::ShutdownInLoop,this));
    }
    void Release()
    {
        _loop->QueueInLoop(std::bind(&Connection::ReleaseInLoop,this));
    }
    // 启动非活跃销毁，并定时多次时间无通信就是非活跃，添加定时任务
    void EnableInactiveRelease(int sec)
    {
        _loop->RunInLoop(std::bind(&Connection::EnableInactiveReleaseInLoop,this,sec));
    }
    // 取消非活跃销毁
    void CancelInactiveRelease()
    {
        _loop->RunInLoop(std::bind(&Connection::CancelInactiveReleaseInLoop,this));
    }
    // 切换协议---重启上下文以及阶段性处理-----非线程安全
    //这个接口必须在EventLoop线程中立即执行，防备新的事件触发后，处理的时候，切换任务还没有被执行--会导致数据使用原协议处理
    void Upgrade(const Any &context, const ConnectedCallback &conn, const MessageCallback &msg,
                 const ClosedCallback &closed, const AnyEventCallback &event)
    {
        _loop->AssertInLoop();
        _loop->RunInLoop(std::bind(&Connection::UpgradeInLoop,this,context,conn,msg,closed,event));
    }
};



class Acceptor
{
    private:
    Socket _socket;//用于创建监听套接字
    EventLoop *_loop;//用于对监听套接字做事件监控
    Channel _channel;//用于监听套接字进行事件管理
    using AcceptCallback = std::function<void(int)>;
    AcceptCallback _accept_callback;
    private:
    /*监听套接字的读事件回调处理函数---获取新连接，调用_accept_callback函数进行新连接处理*/
    void HandleRead()
    {
        int newfd = _socket.Accept();
        if(newfd<0)
        {
            return ;
        }
        // 新连接必须设为非阻塞
        int flag = fcntl(newfd, F_GETFL, 0);
        fcntl(newfd, F_SETFL, flag | O_NONBLOCK);
        if (_accept_callback)
          _accept_callback(newfd);
    }
    int CreateServer(int port)
    {
        bool ret = _socket.CreateServer(port);
        assert(ret);
        return _socket.Fd();
    }
    public:
    //这里有个问题：不能将启动读事件监控，放在构造函数中，必须在设置回调函数后，再去启动
    //因为我们一旦在构造函数启动读事件监控，万一还没SetAcceptCallback就来了个连接，就处理不了,资源泄漏
    Acceptor(EventLoop *loop,int port):_socket(CreateServer(port)),_loop(loop),_channel(loop,_socket.Fd())
    {
        _channel.SetReadCallback(std::bind(&Acceptor::HandleRead,this));
    }
    void SetAcceptCallback(const AcceptCallback &cb){_accept_callback = cb;}
    void Listen()
    {
        _channel.EnableRead();
    }
};


class TcpServer
{
    private:
    int _port;
    uint64_t _next_id;//自动增长的连接ID
    int _timeout;//非活跃连接的统计时间----多长时间无通信就是非活跃连接
    bool _enable_inactive_release;//是否启动了非活跃连接的标志
    EventLoop _baseloop; // 这是主线程的EventLoop对象，负责监听事件的处理
    Acceptor _acceptor;//这是监听套接字的管理对象
    LoopThreadPool _pool;//这是从属EventLoop线程池
    std::unordered_map<uint64_t,PtrConnection> _conns;//保存所有连接对应的shared_ptr对象

    using ConnectedCallback = std::function<void(const PtrConnection &)>;
    using MessageCallback = std::function<void(const PtrConnection &, Buffer *)>;
    using ClosedCallback = std::function<void(const PtrConnection &)>;
    using AnyEventCallback = std::function<void(const PtrConnection &)>;
    using Functor = std::function<void()>;
    ConnectedCallback _connected_callback;
    MessageCallback _message_callback;
    ClosedCallback _closed_callback;
    AnyEventCallback _event_callback;

    private:
    //为新连接构造一个Connection进行管理
    void NewConnection(int fd)
    {
        _next_id++;
        PtrConnection conn = std::make_shared<Connection>(_pool.NextLoop(),_next_id,fd);
        conn->SetMessageCallback(_message_callback);
        conn->SetClosedCallback(_closed_callback);
        conn->SetConnectedCallback(_connected_callback);
        conn->SetAnyEventCallback(_event_callback);
        conn->SetSrvClosedCallback(std::bind(&TcpServer::RemoveConnection,this,std::placeholders::_1));
        if(_enable_inactive_release) conn->EnableInactiveRelease(_timeout);
        conn->Established();//就绪初始化
        _conns.insert(std::make_pair(_next_id,conn));
    }
    
    void RemoveConnectionInLoop(const PtrConnection &conn)
    {
        int id = conn->ID();
        auto it = _conns.find(id);
        if(it!=_conns.end())
        {
            _conns.erase(it);
        }
    }
    //从管理Connection的_conns中移除连接信息
    void RemoveConnection(const PtrConnection &conn)
    {
        _baseloop.RunInLoop(std::bind(&TcpServer::RemoveConnectionInLoop,this,conn));
    }
    void RunAfterInLoop(const Functor &task,int delay)
    {
        _next_id++;
        _baseloop.TimerAdd(_next_id,delay,task);
    }
    public:
    TcpServer(int port):_port(port),_next_id(0),_enable_inactive_release(false),_acceptor(&_baseloop,port),
    _pool(&_baseloop){
        _acceptor.SetAcceptCallback(std::bind(&TcpServer::NewConnection,this,std::placeholders::_1));
        _acceptor.Listen();//将监听套接字挂到baseloop上开始监控事件
    }
    void SetThreadCount(int count)
    {
        return _pool.SetThreadCount(count);
    }
    void SetConnectedCallback(const ConnectedCallback &cb){_connected_callback = cb;}
    void SetMessageCallback(const MessageCallback &cb){_message_callback = cb;}
    void SetClosedCallback(const ClosedCallback &cb){_closed_callback = cb;}
    void SetAnyEventCallback(const AnyEventCallback &cb){_event_callback = cb;}
    void EnableInactiveRelease(int timeout)
    {
        _timeout=timeout;
        _enable_inactive_release = true;
    }
    //用于添加一个定时任务
    void RunAfter(const Functor &task,int delay)
    {
        _baseloop.RunInLoop(std::bind(&TcpServer::RunAfterInLoop,this,task,delay));
    }
    void Start()
    {
        _pool.Create();//创建线程池中的从属线程
        _baseloop.Start();
    }

};

class NetWork
{
    public:
    NetWork()
    {
        LOG(DEBUG)<<"SIGPIPE INIT"<<std::endl;
        signal(SIGPIPE,SIG_IGN);//防止连接关闭时，继续发送数据，会导致服务器发送异常
    }
};
static NetWork net_work;

