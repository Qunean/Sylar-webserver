/**
 * @file thread.cc
 * @brief 线程封装实现
 * @version 0.1
 * @date 2021-06-15
 */
#include "thread.h"
#include "log.h"
#include "util.h"

namespace sylar {
// static thread_local 是c++中的一个关键词组合，用于定义静态线程本地存储变量。
//当一个变量被声明为static thread_local 时，它会在每个线程中拥有自己独立的静态时间实例，并且对其他线程不可见
// 这使得变量可以跨越多个函数调用和代码块，在整个程序运行期间保持其状态和值不变。
static thread_local Thread *t_thread          = nullptr; 
static thread_local std::string t_thread_name = "UNKNOW";

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Thread *Thread::GetThis() {
    return t_thread;
}

const std::string &Thread::GetName() {
    return t_thread_name;
}

void Thread::SetName(const std::string &name) {
    if (name.empty()) {
        return;
    }
    if (t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

Thread::Thread(std::function<void()> cb, const std::string &name)
    : m_cb(cb)
    , m_name(name) {
    if (name.empty()) {
        m_name = "UNKNOW";
    }
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this); // pthread_create 执行成功后，m_thread 将被设置为新线程的唯一标识符。如果函数调用失败（返回值不为0），则会记录错误日志并抛出异常。
    if (rt) {
        SYLAR_LOG_ERROR(g_logger) << "pthread_create thread fail, rt=" << rt
                                  << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait();
}

Thread::~Thread() {
    if (m_thread) {
        pthread_detach(m_thread);
    }
}

void Thread::join() {
    if (m_thread) { // 检查是否有一个有效的线程需要等待，如果没有是为0.
    // 调用 pthread_join 函数等待 m_thread 指定的线程结束。这个函数将阻塞调用者（即当前线程），直到 m_thread 执行完毕。nullptr 参数表示我们不需要获取线程的返回值。
        int rt = pthread_join(m_thread, nullptr); 
        if (rt) { //返回非0值，即发生错误
            SYLAR_LOG_ERROR(g_logger) << "pthread_join thread fail, rt=" << rt
                                      << " name=" << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void *Thread::run(void *arg) {
    // 拿到新创建的Thread对象
    Thread *thread = (Thread *)arg;
    // 更新当前线程
    t_thread       = thread;
    t_thread_name  = thread->m_name;
    // 设置当前线程的id
    // 只有进了run方法才是新线程在执行，创建时是由主线程完成的，threadId为主线程的
    thread->m_id   = sylar::GetThreadId();
    // 设置线程名称
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> cb; //用户需要提供一个这种类型的函数作为线程要执行的任务 我的理解是就是交换一下function'的调用方法
    cb.swap(thread->m_cb); //这一步是为了在当前运行的线程环境中取得控制权，同时确保原始的 m_cb 可以被安全地销毁或重置，因为一旦 swap 完成后，原 m_cb 将为空。

    // 在出构造函数之前，确保线程先跑起来，保证能够初始化id
    thread->m_semaphore.notify();

    cb(); 
    return 0;
}

} // namespace sylar
