/**
 * @file test_fiber.cc
 * @brief 协程测试
 * @version 0.1
 * @date 2021-06-15
 */
#include "sylar/sylar.h"
#include <string>
#include <vector>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void run_in_fiber2() {
    SYLAR_LOG_INFO(g_logger) << "run_in_fiber2 begin";
    SYLAR_LOG_INFO(g_logger) << "run_in_fiber2 end";
}

void run_in_fiber() {
    SYLAR_LOG_INFO(g_logger) << "run_in_fiber begin";

    SYLAR_LOG_INFO(g_logger) << "before run_in_fiber yield";
    sylar::Fiber::GetThis()->yield();
    SYLAR_LOG_INFO(g_logger) << "after run_in_fiber yield";

    SYLAR_LOG_INFO(g_logger) << "run_in_fiber end";
    // fiber结束之后会自动返回主协程运行
}

void test_fiber() {
    SYLAR_LOG_INFO(g_logger) << "test_fiber begin";

    // 初始化线程主协程
    sylar::Fiber::GetThis();
    // 创建一个新协程:
    sylar::Fiber::ptr fiber(new sylar::Fiber(run_in_fiber, 0, false));
    SYLAR_LOG_INFO(g_logger) << "use_count:" << fiber.use_count(); // 查看并记录协程的引用计数:初始情况下，引用计数为 1，只有 fiber 这一个智能指针指向协程对象。

    SYLAR_LOG_INFO(g_logger) << "before test_fiber resume";
    fiber->resume();
    SYLAR_LOG_INFO(g_logger) << "after test_fiber resume";

    /** 
     * 关于fiber智能指针的引用计数为3的说明：
     * 一份在当前函数的fiber指针，一份在MainFunc的cur指针
     * 还有一份在在run_in_fiber的GetThis()结果的临时变量里
     */
    SYLAR_LOG_INFO(g_logger) << "use_count:" << fiber.use_count(); // 3 在协程执行期间，其引用计数增加，说明协程对象在其他地方（如 Fiber::MainFunc 和协程自己的 GetThis()）被额外引用。

    SYLAR_LOG_INFO(g_logger) << "fiber status: " << fiber->getState(); // READY 表示协程可能是挂起状态，准备再次运行。

    SYLAR_LOG_INFO(g_logger) << "before test_fiber resume again";
    fiber->resume();
    SYLAR_LOG_INFO(g_logger) << "after test_fiber resume again";

    SYLAR_LOG_INFO(g_logger) << "use_count:" << fiber.use_count(); // 1
    SYLAR_LOG_INFO(g_logger) << "fiber status: " << fiber->getState(); // TERM

    fiber->reset(run_in_fiber2); // 当协程完成后，可以用新的函数 run_in_fiber2 重置协程，复用其栈空间。
    fiber->resume();

    SYLAR_LOG_INFO(g_logger) << "use_count:" << fiber.use_count(); // 1
    SYLAR_LOG_INFO(g_logger) << "test_fiber end";
}

int main(int argc, char *argv[]) {
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    sylar::SetThreadName("main_thread");
    SYLAR_LOG_INFO(g_logger) << "main begin";

    std::vector<sylar::Thread::ptr> thrs;
    for (int i = 0; i < 2; i++) {
        thrs.push_back(sylar::Thread::ptr(
            new sylar::Thread(&test_fiber, "thread_" + std::to_string(i))));
    }

    for (auto i : thrs) {
        i->join();
    }

    SYLAR_LOG_INFO(g_logger) << "main end";
    return 0;
}