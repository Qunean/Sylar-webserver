#include "timer.h"
#include "util.h"
#include "macro.h"

namespace sylar {

bool Timer::Comparator::operator()(const Timer::ptr& lhs
                        ,const Timer::ptr& rhs) const {
    if(!lhs && !rhs) {
        return false;
    }
    // lhs 为空，rhs不为空。返回true，通常认为nullptr在排序中位于最小
    if(!lhs) {
        return true;
    }
    // rhs为空，lhs不为空 返回flase，rhs<lhs
    if(!rhs) {
        return false;
    }
    if(lhs->m_next < rhs->m_next) {
        // 如果左边定时器的下一次触发时间小于右边的，返回true。lhs排在rhs之前
        return true;
    }
    if(rhs->m_next < lhs->m_next) {
        // 如果左边定时器的下一次触发时间大于右边的，返回false。rhs排在lhs之前
        return false;
    }
    // 如果两个定时器的下一次触发时间相同，则比较它们在内存中的地址。
    return lhs.get() < rhs.get();
}


Timer::Timer(uint64_t ms, std::function<void()> cb,
             bool recurring, TimerManager* manager)
    :m_recurring(recurring)
    ,m_ms(ms)
    ,m_cb(cb)
    ,m_manager(manager) {
    //GetElapsedMS()函数返回一个 uint64_t 类型的值，表示经过的时间，单位是毫秒。
    m_next = sylar::GetElapsedMS() + m_ms; //m_next 存储的是定时器应该触发的绝对时间（以系统启动为基准的毫秒数）。
}

Timer::Timer(uint64_t next)
    :m_next(next) {
}

bool Timer::cancel() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(m_cb) {
        m_cb = nullptr;
        // 在set中找到自身定时器
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}
// 刷新执行时间
bool Timer::refresh() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    // 如果定时器的回调函数不存在（m_cb 是空的），则函数返回 false，表示没有必要刷新不存在的回调。
    if(!m_cb) {
        return false;
    }
    // 在set中找到自身定时器
    auto it = m_manager->m_timers.find(shared_from_this());
    // 如果定时器不在集合中（即 find 返回的迭代器指向 end()），则返回 false。
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    m_next = sylar::GetElapsedMS() + m_ms;
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now) {
    // from now 若周期相同，不按当前时间计算
    if(ms == m_ms && !from_now) {
        return true;
    }
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    uint64_t start = 0;
    if(from_now) {
        // 更新起始时间
        start = sylar::GetElapsedMS();
    } else {
        /* 起始时间为当时创建时的起始时间
         * m_next = sylar::GetCurrentMS() + m_ms; 
        */
        start = m_next - m_ms;
    }
    m_ms = ms;
    m_next = start + m_ms;
    m_manager->addTimer(shared_from_this(), lock);
    return true;

}

TimerManager::TimerManager() {
    m_previouseTime = sylar::GetElapsedMS();
}

TimerManager::~TimerManager() {
}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb
                                  ,bool recurring) {
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    // 添加到set中
    addTimer(timer, lock);
    return timer;
}

// weak_ptr 是一种弱引用，不会增加所指对象的引用计数，也不会阻止所指对象被销毁
// 这里的实现使用了 std::weak_ptr来引用条件对象，确保不会因为定时器的存在而阻止条件对象被适时地销毁。
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    std::shared_ptr<void> tmp = weak_cond.lock();
    if(tmp) {
        cb();
    }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb
                                    ,std::weak_ptr<void> weak_cond 
                                    ,bool recurring) {
    // 在定时器触发时会调用 OnTimer 函数，并在OnTimer函数中判断条件对象是否存在，如果存在则调用回调函数cb。
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

uint64_t TimerManager::getNextTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    // 不触发 onTimerInsertedAtFront
    m_tickled = false;
    // 如果没有定时器，返回一个最大值
    if(m_timers.empty()) {
        return ~0ull;
    }
    // 拿到第一个定时器
    const Timer::ptr& next = *m_timers.begin();
    uint64_t now_ms = sylar::GetElapsedMS();
    // 如果当前时间 >= 该定时器的执行时间，说明该定时器已经超时了，该执行了
    if(now_ms >= next->m_next) {
        return 0;
    } else {
        // 还没超时，返回还要多久执行
        return next->m_next - now_ms;
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()> >& cbs) {
    uint64_t now_ms = sylar::GetElapsedMS();
    std::vector<Timer::ptr> expired;
    {
        RWMutexType::ReadLock lock(m_mutex);
        if(m_timers.empty()) {
            return;
        }
    }
    RWMutexType::WriteLock lock(m_mutex);
    if(m_timers.empty()) {
        return;
    }
    bool rollover = false;
    if(SYLAR_UNLIKELY(detectClockRollover(now_ms))) {
        // 使用clock_gettime(CLOCK_MONOTONIC_RAW)，应该不可能出现时间回退的问题
        rollover = true;
    }
    if(!rollover && ((*m_timers.begin())->m_next > now_ms)) {
        return;
    }

    Timer::ptr now_timer(new Timer(now_ms));
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
    while(it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }
    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);
    cbs.reserve(expired.size());

    for(auto& timer : expired) {
        cbs.push_back(timer->m_cb);
        if(timer->m_recurring) {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } else {
            timer->m_cb = nullptr;
        }
    }
}

void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock) {
    // 添加到set中并且拿到迭代器
    auto it = m_timers.insert(val).first;
    bool at_front = (it == m_timers.begin()) && !m_tickled;
    if(at_front) {
        m_tickled = true;
    }
    lock.unlock();
    /* 
    * 触发onTimerInsertedAtFront()
    * onTimerInsertedAtFront()在IOManager中就是做了一次tickle()的操作 
    */
    if(at_front) {
        onTimerInsertedAtFront();
    }
}

bool TimerManager::detectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    if(now_ms < m_previouseTime &&
            now_ms < (m_previouseTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    m_previouseTime = now_ms;
    return rollover;
}

bool TimerManager::hasTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

}
