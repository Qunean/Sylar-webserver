/**
 * @file config.cc
 * @brief 配置模块实现
 * @version 0.1
 * @date 2021-06-14
 */
#include "sylar/config.h"
#include "sylar/env.h"
#include "sylar/util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

ConfigVarBase::ptr Config::LookupBase(const std::string &name) {
    RWMutexType::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

//"A.B", 10
//A:
//  B: 10
//  C: str

// ?这个在头文件中没有声明？
// prefix 就是A.B 这样的前缀 初始为0.比如common.lr
static void ListAllMember(const std::string &prefix,
                          const YAML::Node &node,
                          std::list<std::pair<std::string, const YAML::Node>> &output) {
    // 检查键名的有效性
    if (prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") != std::string::npos) {
        SYLAR_LOG_ERROR(g_logger) << "Config invalid name: " << prefix << " : " << node;
        return;
    }
    // 保存当前节点
    output.push_back(std::make_pair(prefix, node));
    //递归遍历子节点
    if (node.IsMap()) {
        for (auto it = node.begin();
             it != node.end(); ++it) {
            ListAllMember(prefix.empty() ? it->first.Scalar()
                                         : prefix + "." + it->first.Scalar(),
                          it->second, output);
        }
    }
}

// 使用YAML::Node初始化配置模块
void Config::LoadFromYaml(const YAML::Node &root) {
    std::list<std::pair<std::string, const YAML::Node>> all_nodes;
    ListAllMember("", root, all_nodes);

    for (auto &i : all_nodes) {
        std::string key = i.first;
        if (key.empty()) {
            continue;
        }
        // 使用 std::transform 将键路径 key 中的所有字符转换为小写字母，以便在查找配置项时不区分大小写。
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        //调用 LookupBase(key) 通过键路径在配置系统中查找对应的配置变量 var。
        ConfigVarBase::ptr var = LookupBase(key);

        if (var) {
            if (i.second.IsScalar()) { // 判断这个var是否是一个标量 *单一值，字符串或者数字。
                var->fromString(i.second.Scalar());
            } else {
                std::stringstream ss;
                ss << i.second;
                var->fromString(ss.str());
            }
        }
    }
}

/// 记录每个文件的修改时间
static std::map<std::string, uint64_t> s_file2modifytime;
/// 是否强制加载配置文件，非强制加载的情况下，如果记录的文件修改时间未变化，则跳过该文件的加载
static sylar::Mutex s_mutex;

// 加载path文件夹里面的配置文件
// force 参数决定是否强制重新加载所有配置文件。
void Config::LoadFromConfDir(const std::string &path, bool force) {
    std::string absoulte_path = sylar::EnvMgr::GetInstance()->getAbsolutePath(path);
    std::vector<std::string> files;
    // 目录中所有yaml文件
    FSUtil::ListAllFile(files, absoulte_path, ".yml");

    for (auto &i : files) {
        {
            struct stat st;
            lstat(i.c_str(), &st); // 通过lstat获取文件的状态信息，
            sylar::Mutex::Lock lock(s_mutex);
            //如果 force 标志为 false 且文件的修改时间未变化
            //（即 s_file2modifytime[i] == (uint64_t)st.st_mtime），则跳过该文件的加载，避免重复加载未修改的文件。
            if (!force && s_file2modifytime[i] == (uint64_t)st.st_mtime) {
                continue;
            }
            s_file2modifytime[i] = st.st_mtime; // 如果force为true，或者已经修改。更新修改时间记录
        }
        try {
            YAML::Node root = YAML::LoadFile(i);
            LoadFromYaml(root);
            SYLAR_LOG_INFO(g_logger) << "LoadConfFile file="
                                     << i << " ok";
        } catch (...) {
            SYLAR_LOG_ERROR(g_logger) << "LoadConfFile file="
                                      << i << " failed";
        }
    }
}

void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
    RWMutexType::ReadLock lock(GetMutex());
    ConfigVarMap &m = GetDatas();
    for (auto it = m.begin();
         it != m.end(); ++it) {
        cb(it->second);
    }
}

} // namespace sylar
