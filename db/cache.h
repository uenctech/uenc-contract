#ifndef EBPC_DB_REDIS_H_
#define EBPC_DB_REDIS_H_

#include "utils/CTimer.hpp"
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

class DBCache
{
public:
    DBCache();
    ~DBCache();
    DBCache(DBCache &&) = delete;
    DBCache(const DBCache &) = delete;
    DBCache &operator=(DBCache &&) = delete;
    DBCache &operator=(const DBCache &) = delete;
    bool GetData(const std::string &key, std::string &value);
    bool SetData(const std::string &key, const std::string &value);
    bool DeleteData(const std::string &key);
    bool AddData(const std::map<std::string, std::string> &add_data);
    bool DeleteData(const std::set<std::string> &keys);
    size_t GetDataSize();
    size_t GetKeyNum() { return data_.size(); }

private:
    void ClearExpireData();
    std::mutex mutex_;
    std::unordered_map<std::string, std::pair<uint64_t, std::string>> data_;
    std::map<uint64_t, std::set<std::string>> time_data_;
    CTimer timer_;
    //最多保存 save_key_num 个key
    uint64_t save_key_num_;
    //最多保存 save_time 长时间（秒）
    uint64_t save_time_;
};

#endif
