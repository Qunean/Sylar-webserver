/**
 * @file env.cc
 * @brief 环境变量管理接口实现
 * @version 0.1
 * @date 2021-06-13
 * @todo 命令行参数解析应该用getopt系列接口实现，以支持选项合并和--开头的长选项
 */
#include "env.h"
#include "sylar/log.h"
#include <string.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <stdlib.h>
#include "config.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
// static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

bool Env::init(int argc, char **argv) {
    char link[1024] = {0};
    char path[1024] = {0};
    sprintf(link, "/proc/%d/exe", getpid());
    readlink(link, path, sizeof(path)); // readlink 函数读取符号链接（symlink）所指向的路径，并将结果存储在 path 数组中。
    // /path/xxx/exe 符号链接，它指向当前进程正在执行的可执行文件的路径。
    m_exe = path; //可执行文件在硬盘中的位置？

    // m_exe = /home/zjn/sylar-from-scratch/bin/test_env
    // m_cwd = /home/zjn/sylar-from-scratch/bin/ (current work directory)
    auto pos = m_exe.find_last_of("/"); //找到最后一个 / 的位置索引
    m_cwd    = m_exe.substr(0, pos) + "/"; //从 m_exe 字符串中提取从位置 0 开始，长度为 pos 的子字符串，即从路径的起始位置到最后一个 / 之前的所有内容。

    m_program = argv[0];
    // -config /path/to/config -file xxxx -d
    const char *now_key = nullptr; // 表示当前正在处理的命令行参数键。
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (strlen(argv[i]) > 1) {  //长度大于1 证明是一个有效的键 
                // 如果是-p -l 1 这一种，检查到-l 的时候发现now_key 是p 但是未赋值，所以要给前一个key 一个默认值
                if (now_key) {
                    add(now_key, "");
                }
                now_key = argv[i] + 1; // 将键的名字（去掉前面的 -）赋值给 now_key。
            } else { // 无效的键
                SYLAR_LOG_INFO(g_logger) << "invalid arg idx=" << i
                                          << " val=" << argv[i];
                return false;
            }
        } else { //当前参数不是以 - 开头，说明它是一个值。
            if (now_key) { // 当前参数前面有一个键 now_key 将当前参数作为这个键的值并存储
                add(now_key, argv[i]);
                now_key = nullptr; //赋值（绑定后），设置为空指针避免错误。
            } else {
                SYLAR_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                                          << " val=" << argv[i];
                return false;
            }
        }
    }
    //在循环结束后，如果还有未处理的键（即没有对应的值），将其存储并赋值为空字符串。
    if (now_key) {
        add(now_key, "");
    }
    return true;
}

void Env::add(const std::string &key, const std::string &val) {
    RWMutexType::WriteLock lock(m_mutex);
    m_args[key] = val;
}

bool Env::has(const std::string &key) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end();
}

void Env::del(const std::string &key) {
    RWMutexType::WriteLock lock(m_mutex);
    m_args.erase(key);
}

std::string Env::get(const std::string &key, const std::string &default_value) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end() ? it->second : default_value;
}

// 增添help中的参数描述性文本
void Env::addHelp(const std::string &key, const std::string &desc) {
    removeHelp(key); // 如果有之前的描述性文本，先删除
    RWMutexType::WriteLock lock(m_mutex);
    m_helps.push_back(std::make_pair(key, desc));
}

void Env::removeHelp(const std::string &key) {
    RWMutexType::WriteLock lock(m_mutex);
    for (auto it = m_helps.begin();
         it != m_helps.end();) {
        if (it->first == key) {
            it = m_helps.erase(it);
        } else {
            ++it;
        }
    }
}

void Env::printHelp() {
    RWMutexType::ReadLock lock(m_mutex);
    std::cout << "Usage: " << m_program << " [options]" << std::endl;
    for (auto &i : m_helps) {
        std::cout << std::setw(5) << "-" << i.first << " : " << i.second << std::endl;
    }
}

// setenv 和 getenv 是标准C库函数
bool Env::setEnv(const std::string &key, const std::string &val) {
    // setenv(const char *name, const char *value, int overwrite);
    // overwrite=1 环境变量将改为value所指的变量内容，如果overwrite=0 且该环境变量已有内容，则参数value会被忽略
    // 执行成功返回0，有错误返回-1 所以为!setenv()
    return !setenv(key.c_str(), val.c_str(), 1);
}

std::string Env::getEnv(const std::string &key, const std::string &default_value) {
    const char *v = getenv(key.c_str());// 返回给定的环境变量值，如果未定义则返回空串
    if (v == nullptr) { // 如果是空串，返回默认参数
        return default_value;
    }
    // v 是const char* ，函数的返回值为std:string 。有一个隐式的转换
    return v;
}

// std::string Env::getProgram() const{
//     return m_program;
// }


std::string Env::getAbsolutePath(const std::string &path) const {
    if (path.empty()) {
        return "/";
    }
    if (path[0] == '/') {
        return path;
    }
    return m_cwd + path;
}

std::string Env::getAbsoluteWorkPath(const std::string& path) const {
    if(path.empty()) {
        return "/";
    }
    if(path[0] == '/') {
        return path;
    }
    static sylar::ConfigVar<std::string>::ptr g_server_work_path =
        sylar::Config::Lookup<std::string>("server.work_path");
    return g_server_work_path->getValue() + "/" + path;
}

std::string Env::getConfigPath() {
    return getAbsolutePath(get("c", "conf")); //从环境变量/配置文件/命令行参数等等找c，如果没有找到value 设置value为conf。
}

} // namespace sylar
