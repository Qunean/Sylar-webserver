/**
 * @file iomanager.cc
 * @brief IO协程调度器实现
 * @version 0.1
 * @date 2021-06-16
 */

#include <unistd.h>    // for pipe()
#include <sys/epoll.h> // for epoll_xxx()
#include <fcntl.h>     // for fcntl()
#include "iomanager.h"
#include "log.h"
#include "macro.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

enum EpollCtlOp {
};

static std::ostream &operator<<(std::ostream &os, const EpollCtlOp &op) {
    switch ((int)op) {
#define XX(ctl) \
    case ctl:   \
        return os << #ctl;
        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
#undef XX
    default:
        return os << (int)op;
    }
}

static std::ostream &operator<<(std::ostream &os, EPOLL_EVENTS events) {
    if (!events) {
        return os << "0";
    }
    bool first = true;
#define XX(E)          \
    if (events & E) {  \
        if (!first) {  \
            os << "|"; \
        }              \
        os << #E;      \
        first = false; \
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}

IOManager::FdContext::EventContext &IOManager::FdContext::getEventContext(IOManager::Event event) {
    switch (event) {
    case IOManager::READ:
        return read;
    case IOManager::WRITE:
        return write;
    default:
        SYLAR_ASSERT2(false, "getContext");
    }
    throw std::invalid_argument("getContext invalid event");
}

void IOManager::FdContext::resetEventContext(EventContext &ctx) {
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    // 待触发的事件必须已被注册过
    SYLAR_ASSERT(events & event);
    /**
     *  清除该事件，表示不再关注该事件了
     * 也就是说，注册的IO事件是一次性的，如果想持续关注某个socket fd的读写事件，那么每次触发事件之后都要重新添加
     */
    events = (Event)(events & ~event);
    // 调度对应的协程
    EventContext &ctx = getEventContext(event);
    if (ctx.cb) {
        // 使用地址传入就会将cb的引用计数-1
        ctx.scheduler->schedule(ctx.cb);
    } else {
        // 使用地址传入就会将fiber的引用计数-1 并没有看到哪里-1嘞
        ctx.scheduler->schedule(ctx.fiber);
    }
    resetEventContext(ctx);
    return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
    : Scheduler(threads, use_caller, name) {
    // 创建一个epollfd
    m_epfd = epoll_create(5000);
    // 成功时，这些系统调用将返回非负文件描述符。如果出错，则返回-1，并且将errno设置为指示错误
    SYLAR_ASSERT(m_epfd > 0);

    // 创建管道，用于进程间通信
    int rt = pipe(m_tickleFds);
    SYLAR_ASSERT(!rt);

    // 关注pipe读句柄的可读事件，用于tickle协程
    epoll_event event;
    memset(&event, 0, sizeof(epoll_event)); //以event为头，分配一些地址，内存区域被设置为0是为了清除或初始化数据，确保没有任何旧数据或垃圾值留在内存中。
    // 注册读事件，设置边缘触发模式
    event.events  = EPOLLIN | EPOLLET; // | 是位或运算符，如果任意一位为1，则结果位为1；否则为0
    // fd关联pipe的读端
    event.data.fd = m_tickleFds[0];

    // 非阻塞方式，配合边缘触发
    // F_SETFL: 获取/设置文件状态标志
    // O_NONBLOCK 使I/O变成非阻塞模式，在读取不到数据或是写入缓冲区已满会马上return，而不会阻塞等待
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    SYLAR_ASSERT(!rt);

    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    SYLAR_ASSERT(!rt);
    
    // 初始化socket事件上下文vector
    contextResize(32);
    // 启动调度器
    start();
}

IOManager::~IOManager() {
    stop();
    close(m_epfd);
    // 释放pipe
    close(m_tickleFds[0]); //读端 用于接收数据
    close(m_tickleFds[1]); // 写端，用于发送数据
    
    // 释放 m_fdContexts 内存
    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

void IOManager::contextResize(size_t size) {
    m_fdContexts.resize(size);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (!m_fdContexts[i]) {
            m_fdContexts[i]     = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    // 找到fd对应的FdContext，如果不存在，那就分配一个
    FdContext *fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) { //如果所有文件描述符上下文的容易已经大于fd的值，那就是已经存在，可以直接获取
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } else {
        lock.unlock(); // 不存在在所有文件描述符上下文中，释放读锁
        RWMutexType::WriteLock lock2(m_mutex); //加写锁
        contextResize(fd * 1.5); // 扩展m_fdcontext容器
        fd_ctx = m_fdContexts[fd]; //重新获取上下文
    }

    // 同一个fd不允许重复添加相同的事件
    FdContext::MutexType::Lock lock2(fd_ctx->mutex); //使用互斥锁确保对fdContext的访问是线程安全的。
    if (SYLAR_UNLIKELY(fd_ctx->events & event)) { //检查是否已经注册了相同的事件，如果是，则记录错误并断言失败。
        SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
                                  << " event=" << (EPOLL_EVENTS)event
                                  << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        SYLAR_ASSERT(!(fd_ctx->events & event));
    }

    // 将新的事件加入epoll_wait，使用epoll_event的私有指针存储FdContext的位置
    // 如果 fd_ctx->events（已经注册的事件）不为零，说明已经在 epoll 实例中注册过此 fd，因此应使用 EPOLL_CTL_MOD 操作来修改这个 fd 的事件监听。
    // 如果 fd_ctx->events 为零，说明这是首次为此 fd 注册事件，因此应使用 EPOLL_CTL_ADD 操作来添加新的事件监听。
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events   = EPOLLET | fd_ctx->events | event; // 边缘模式 | 之前已经注册的事件 | 新添加的事件 位或操作可以组合多个事件标志
    epevent.data.ptr = fd_ctx; // data.ptr 是一个void* 指针，可以存储用户定义的数据。在这里它用来存储指向fd_ctx的指针，这样在事件被触发的时，可以快速访问到与该事件相关的上下文信息

    int rt = epoll_ctl(m_epfd, op, fd, &epevent); // 使用 epoll_ctl 函数更新 epoll 实例。
    if (rt) { //如果rt返回-1则代表返回错误
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events="
                                  << (EPOLL_EVENTS)fd_ctx->events;
        return -1;
    }

    // 待执行IO事件数加1
    ++m_pendingEventCount;

    // 找到这个fd的event事件对应的EventContext，对其中的scheduler, cb, fiber进行赋值
    fd_ctx->events                     = (Event)(fd_ctx->events | event);
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event); // 获取或创建与此事件类型对应的 EventContext。
    SYLAR_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb); 

    // 赋值scheduler和回调函数，如果回调函数为空，则把当前协程当成回调执行体
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb) {
        event_ctx.cb.swap(cb); // 如果提供了回调函数 cb，则将其设置到 EventContext。
    } else {
        event_ctx.fiber = Fiber::GetThis();  //如果没有提供，那么使用当前协程作为回调执行体，
        SYLAR_ASSERT2(event_ctx.fiber->getState() == Fiber::RUNNING, "state=" << event_ctx.fiber->getState());// 并断言当前协程是在运行状态。
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event) {
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex); //读锁
    if ((int)m_fdContexts.size() <= fd) {
        return false; //没有这个上下文，不需要删除
    }
    FdContext *fd_ctx = m_fdContexts[fd]; //读取
    lock.unlock(); //解锁

    FdContext::MutexType::Lock lock2(fd_ctx->mutex); //互斥锁
    // 若没有要删除的事件
    if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    // 清除指定的事件，表示不关心这个事件了，如果清除之后结果为0，则从epoll_wait中删除该文件描述符

    Event new_events = (Event)(fd_ctx->events & ~event);
    // 若还有事件则是修改，若没事件了则删除
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    // 边缘触发模式，新的注册事件
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 待执行事件数减1
    --m_pendingEventCount;
    // 重置该fd对应的event事件上下文
    fd_ctx->events                     = new_events;
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;
}

bool IOManager::cancelEvent(int fd, Event event) {
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    // 删除事件
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 删除之前触发一次事件
    fd_ctx->triggerEvent(event);
    // 活跃事件数减1
    --m_pendingEventCount;
    return true;
}

bool IOManager::cancelAll(int fd) {
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if ((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (!fd_ctx->events) {
        return false;
    }

    // 使用 EPOLL_CTL_DEL 来从 epoll 实例中删除一个文件描述符（fd）的全部事件，确实并没有显示的循环过程。
    int op = EPOLL_CTL_DEL; // EPOLL_CTL_DEL 是一个操作码，用于指示 epoll_ctl 函数从 epoll 实例中删除一个文件描述符（fd）
    epoll_event epevent;
    epevent.events   = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 触发全部已注册的事件
    if (fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if (fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    SYLAR_ASSERT(fd_ctx->events == 0);
    return true;
}

IOManager *IOManager::GetThis() {
    return dynamic_cast<IOManager *>(Scheduler::GetThis());
}

/**
 * 通知调度协程、也就是Scheduler::run()从idle中退出
 * Scheduler::run()每次从idle协程中退出之后，都会重新把任务队列里的所有任务执行完了再重新进入idle
 * 如果没有调度线程处理于idle状态，那也就没必要发通知了
 */
void IOManager::tickle() {
    SYLAR_LOG_DEBUG(g_logger) << "tickle";
    // 没有在执行 idle 的线程
    if(!hasIdleThreads()) {
        return;
    }
    // 有任务来了，就往 pipe 里发送1个字节的数据，这样 epoll_wait 就会唤醒
    int rt = write(m_tickleFds[1], "T", 1);
    SYLAR_ASSERT(rt == 1);
}

bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

bool IOManager::stopping(uint64_t &timeout) {
    // 对于IOManager而言，必须等所有待调度的IO事件都执行完了才可以退出
    // 增加定时器功能后，还应该保证没有剩余的定时器待触发
    timeout = getNextTimer();
    // ~0ull 表示没有计划的定时器事件。
    // m_pendingEventCount 记录了待处理的事件数量，只有当其为 0 时，才表示所有 IO 事件都已处理完毕。
    // 调用基类 Scheduler 的 stopping 方法，该方法通常检查是否所有的任务都已完成，确保没有其他活动的调度任务或者子任务。
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

/**
 * 调度器无调度任务时会阻塞idle协程上，对IO调度器而言，idle状态应该关注两件事，一是有没有新的调度任务，对应Schduler::schedule()，
 * 如果有新的调度任务，那应该立即退出idle状态，并执行对应的任务；二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行
 * IO事件对应的回调函数
 */
void IOManager::idle() {
    SYLAR_LOG_DEBUG(g_logger) << "idle";

    // 一次epoll_wait最多检测256个就绪事件，如果就绪事件超过了这个数，那么会在下轮epoll_wati继续处理
    const uint64_t MAX_EVNETS = 256;
    epoll_event *events       = new epoll_event[MAX_EVNETS]();
    //创建一个智能指针 shared_events 管理 events 数组。当 shared_events 被销毁时，它会自动释放 events 数组。
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr) {
        delete[] ptr;
    });

    while (true) {
        // 获取下一个定时器的超时时间，顺便判断调度器是否停止
        uint64_t next_timeout = 0;
        if( SYLAR_UNLIKELY(stopping(next_timeout))) {
            SYLAR_LOG_DEBUG(g_logger) << "name=" << getName() << "idle stopping exit";
            break;
        }

        // 阻塞在epoll_wait上，等待事件发生或定时器超时
        int rt = 0;
        do{
            // 默认超时时间5秒，如果下一个定时器的超时时间大于5秒，仍以5秒来计算超时，避免定时器超时时间太大时，epoll_wait一直阻塞
            // 毫秒级精度
            static const int MAX_TIMEOUT = 5000;
            if(next_timeout != ~0ull) {
                // 睡眠时间为next_timeout，但不超过MAX_TIMEOUT
                next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
            } else {
                // 没定时器任务就睡眠MAX_TIMEOUT
                next_timeout = MAX_TIMEOUT;
            }
            /*  
             * 阻塞在这里，但有3中情况能够唤醒epoll_wait
             * 1. 超时时间到了
             * 2. 关注的 soket 有数据来了
             * 3. 通过 tickle 往 pipe 里发数据，表明有任务来了
             */
            rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout); //监听事件
            /* 这里就是源码 ep_poll() 中由操作系统中断返回的 EINTR
             * 需要重新尝试 epoll_Wait 
            */
            if(rt < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        } while(true);

        // 收集所有已超时的定时器，执行回调函数
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        if(!cbs.empty()) {
            for(const auto &cb : cbs) {
                schedule(cb);
            }
            cbs.clear();
        }
        
        // 遍历所有发生的事件，根据epoll_event的私有指针找到对应的FdContext，进行事件处理
        for (int i = 0; i < rt; ++i) {
            epoll_event &event = events[i];
            if (event.data.fd == m_tickleFds[0]) {
                // ticklefd[0]用于通知协程调度，这时只需要把管道里的内容读完即可
                uint8_t dummy[256];
                while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0)
                    ;
                continue;
            }

            FdContext *fd_ctx = (FdContext *)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);
            /**
             * EPOLLERR: 出错，比如写读端已经关闭的pipe
             * EPOLLHUP: 套接字对端关闭
             * 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
             */ 
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events; //如果事件包含错误或挂起，则确保同时触发读写事件处理，防止忽略错误情况。
            }
            int real_events = NONE;
            if (event.events & EPOLLIN) {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            if ((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            // 剔除已经发生的事件，将剩下的事件重新加入epoll_wait
            int left_events = (fd_ctx->events & ~real_events);
            // 如果执行完该事件还有事件则修改，若无事件则删除
            int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            // 更新新的事件
            event.events    = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) {
                SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                          << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                                          << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }

            // 处理已经发生的事件，也就是让调度器调度指定的函数或协程
            if (real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            if (real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        } // end for

        /**
         * 一旦处理完所有的事件，idle协程yield，这样可以让调度协程(Scheduler::run)重新检查是否有新任务要调度
         * 上面triggerEvent实际也只是把对应的fiber重新加入调度，要执行的话还要等idle协程退出
         */ 
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr   = cur.get();
        cur.reset();

        raw_ptr->yield();
    } // end while(true)
}

void IOManager::onTimerInsertedAtFront() {
    tickle();
}

} // end namespace sylar