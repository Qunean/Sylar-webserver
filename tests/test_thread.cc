/**
 * @file test_thread.cc
 * @brief 线程模块测试
 * @version 0.1
 * @date 2021-06-15
 */
#include "sylar/sylar.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int count = 0;
sylar::Mutex s_mutex;

void func1(void *arg) {
    SYLAR_LOG_INFO(g_logger) << "name:" << sylar::Thread::GetName()
        << " this.name:" << sylar::Thread::GetThis()->getName()
        << " thread name:" << sylar::GetThreadName()
        << " id:" << sylar::GetThreadId()
        << " this.id:" << sylar::Thread::GetThis()->getId();
    SYLAR_LOG_INFO(g_logger) << "arg: " << *(int*)arg;
    for(int i = 0; i < 10000; i++) {
        sylar::Mutex::Lock lock(s_mutex);
        ++count;
    }
}

int main(int argc, char *argv[]) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    std::vector<sylar::Thread::ptr> thrs;
    int arg = 123456;
    for(int i = 0; i < 3; i++) {
        // 带参数的线程用std::bind进行参数绑定
        sylar::Thread::ptr thr(new sylar::Thread(std::bind(func1, &arg), "thread_" + std::to_string(i))); // 创建了3个线程，每个线程都被绑定到func1上，并传递相同的arg
        // 创建的线程被立即启动，并行执行。这意味着 func1 函数在三个不同的线程中几乎同时运行，每个线程尝试访问和修改共享的 count 变量。

        thrs.push_back(thr);
    }

    for(int i = 0; i < 3; i++) {
        thrs[i]->join(); // 在所有线程开始后，主函数通过调用 join 方法等待每个线程完成：
    }
    
    SYLAR_LOG_INFO(g_logger) << "count = " << count;
    return 0;
}

