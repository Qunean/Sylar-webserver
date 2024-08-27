/**
 * @file mutex.cc
 * @brief 信号量实现
 * @version 0.1
 * @date 2021-06-09
 */

#include "mutex.h"

namespace sylar {

Semaphore::Semaphore(uint32_t count) {
    // 函数原型：int sem_init(sem_t *sem, int pshared, unsigned int value);
    // 参数分别是：要初始化的信号量的指针；信号量是进程内共享还是跨进程共享，如果设置为0，则是进程内共享；value是信号量的初始值
    // 函数成功时返回0 否则返回-1
    if(sem_init(&m_semaphore, 0, count)) {
        throw std::logic_error("sem_init error");
    }
}

Semaphore::~Semaphore() {
    sem_destroy(&m_semaphore); //释放信号量使用的资源。
}

void Semaphore::wait() {
    if(sem_wait(&m_semaphore)) { // 减少信号量的计数值
        throw std::logic_error("sem_wait error");
    }
}

void Semaphore::notify() {
    if(sem_post(&m_semaphore)) { // 增加信号量的计数值
    // 如果有进程或者线程正在等待该信号量，那么其中一个将被唤醒以继续执行
        throw std::logic_error("sem_post error");
    }
}

} // namespace sylar
