/**
 * @file test_env.cc
 * @brief 环境变量测试
 * @version 0.1
 * @date 2021-06-13
 */
#include "sylar/sylar.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT(); //g_logger 是一个全局日志器，用于记录日志。

sylar::Env *g_env = sylar::EnvMgr::GetInstance(); //g_env 是全局环境管理对象，使用单例模式获取 Env 的实例。

// argc 是arg count 参数的个数，这里 argv 是一个数组，每个元素都是一个 char*，指向一个字符串。
int main(int argc, char *argv[]) {
    // std::cout<< "argc:"<<argc<<std::endl;
    // for(int i =0;i<argc;++i){
    //     std::cout<< argv[i]<<std::endl;
    // }


    g_env->addHelp("h", "print this help message");

    bool is_print_help = false;
    // 调用g_env->init(argc, argv) 如果解析都成功，则返回true；否则返回false
    if(!g_env->init(argc, argv)) {
        is_print_help = true; // 有错误代表着需要print help 设置为true
    }
    if(g_env->has("h")) {
        is_print_help = true; //如果参数中有-h 则print help
    }

    if(is_print_help) {
        g_env->printHelp();
        return false;
    }
    SYLAR_LOG_INFO(g_logger)<< "exe: " << g_env->getExe();
    SYLAR_LOG_INFO(g_logger) <<"cwd: " << g_env->getCwd();
    SYLAR_LOG_INFO(g_logger) << "absoluth path of test: " << g_env->getAbsolutePath("test");
    SYLAR_LOG_INFO(g_logger) << "conf path:" << g_env->getConfigPath();
    SYLAR_LOG_INFO(g_logger) << "p:" << g_env->get("p");

    g_env->add("name", "jianing zhu");
    SYLAR_LOG_INFO(g_logger) << "name: " << g_env->get("name");

    g_env->setEnv("gender", "female");
    SYLAR_LOG_INFO(g_logger) << "gender: " << g_env->getEnv("gender");

    SYLAR_LOG_INFO(g_logger) << g_env->getEnv("PATH");

    SYLAR_LOG_INFO(g_logger) <<"Program: "<< g_env->getProgram();

    return 0;
}