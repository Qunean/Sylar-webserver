/**
 * @file scheduler.h
 * @brief 协程调度器实现
 * @version 0.1
 * @date 2021-06-15
 */

#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <functional>
#include <list>
#include <memory>
#include <string>
#include "fiber.h"
#include "log.h"
#include "thread.h"

namespace sylar {

/**
 * @brief 协程调度器
 * @details 封装的是N-M的协程调度器
 *          内部有一个线程池,支持协程在线程池里面切换
 */
class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;
    typedef Mutex MutexType;

    /**
     * @brief 创建调度器
     * @param[in] threads 线程数
     * @param[in] use_caller 是否将当前线程也作为调度线程
     * @param[in] name 名称
     */
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string &name = "Scheduler");

    /**
     * @brief 析构函数
     */
    virtual ~Scheduler();

    /**
     * @brief 获取调度器名称
     */
    const std::string &getName() const { return m_name; }

    /**
     * @brief 获取当前线程调度器指针
     */
    static Scheduler *GetThis();

    /**
     * @brief 获取当前线程的主协程
     */
    static Fiber *GetMainFiber();

    /**
     * @brief 添加调度任务
     * @tparam FiberOrCb 调度任务类型，可以是协程对象或函数指针
     * @param[] fc 协程对象或指针
     * @param[] thread 指定运行该任务的线程号，-1表示任意线程
     */
    template <class FiberOrCb> // 因为想要协程对象和函数指针都可以被调度，所以使用模板类
    
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            // 将任务加入到队列中，若任务队列中已经有任务了，则tickle()
            need_tickle = scheduleNoLock(fc, thread);
        }

        if (need_tickle) {
            tickle(); // 唤醒idle协程
        }
    }

    /**
     * @brief 启动调度器
     */
    void start();

    /**
     * @brief 停止调度器，等所有调度任务都执行完了再返回
     */
    void stop();

protected:
    /**
     * @brief 通知协程调度器有任务了
     */
    virtual void tickle();

    /**
     * @brief 协程调度函数
     */
    void run();

    /**
     * @brief 无任务调度时执行idle协程
     */
    virtual void idle();

    /**
     * @brief 返回是否可以停止
     */
    virtual bool stopping();

    /**
     * @brief 设置当前的协程调度器
     */
    void setThis();

    /**
     * @brief 返回是否有空闲线程
     * @details 当调度协程进入idle时空闲线程数加1，从idle协程返回时空闲线程数减1
     */
    bool hasIdleThreads() { return m_idleThreadCount > 0; }

private:
    /**
     * @brief 添加调度任务，无锁
     * @tparam FiberOrCb 调度任务类型，可以是协程对象或函数指针
     * @param[] fc 协程对象或指针
     * @param[] thread 指定运行该任务的线程号，-1表示任意线程
     */
    template <class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread) {
        bool need_tickle = m_tasks.empty(); // 检查队列是否为空，如果为空则任务添加后需要tickle() 唤醒或者激活处理任务的线程
        ScheduleTask task(fc, thread);
        if (task.fiber || task.cb) {
            m_tasks.push_back(task);
        }
        return need_tickle;
    }

private:
    /**
     * @brief 调度任务，协程/函数二选一，可指定在哪个线程上调度
     */
    struct ScheduleTask {
        // 协程
        Fiber::ptr fiber;
        // 协程执行函数
        std::function<void()> cb;
        // 协程id 协程在哪个线程上
        int thread;

        // 接收ptr 和 线程号thr 用来初始化一个协程任务
        ScheduleTask(Fiber::ptr f, int thr) {
            fiber  = f; // 将传入的协程指针直接赋值给成员变量fiber
            thread = thr; // 设置任务应该运行的线程号
        }
        // 通过引用交换的协程任务构造函数
        ScheduleTask(Fiber::ptr *f, int thr) {
            fiber.swap(*f);
            thread = thr;
        }
        // 接收一个函数对象和线程号
        ScheduleTask(std::function<void()> f, int thr) {
            cb     = f; // 将传入的函数对象赋值给成员变量cb
            thread = thr; // 设置任务应该运行的线程号
        }
        // 默认构造
        ScheduleTask() { thread = -1; }

        // 重置
        void reset() {
            fiber  = nullptr;
            cb     = nullptr;
            thread = -1;
        }
    };

private:
    /// 协程调度器名称
    std::string m_name;
    /// 互斥锁
    MutexType m_mutex;
    /// 线程池
    std::vector<Thread::ptr> m_threads;
    /// 任务队列
    std::list<ScheduleTask> m_tasks;
    /// 线程池的线程ID数组
    std::vector<int> m_threadIds;
    /// 工作线程数量，不包含use_caller的主线程
    size_t m_threadCount = 0;
    /// 活跃线程数
    std::atomic<size_t> m_activeThreadCount = {0};
    /// idle线程数
    std::atomic<size_t> m_idleThreadCount = {0};

    /// 是否use caller
    bool m_useCaller;
    /// use_caller为true时，调度器所在线程的调度协程
    Fiber::ptr m_rootFiber;
    /// use_caller为true时，调度器所在线程的id
    int m_rootThread = 0;

    /// 是否自动停止
    bool m_stopping = false;
};

} // end namespace sylar

#endif
