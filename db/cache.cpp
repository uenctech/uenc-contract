#include "db/cache.h"
#include "include/logging.h"
#include <sys/sysinfo.h>

DBCache::DBCache()
{
    save_time_ = 24 * 60 * 60;
    save_key_num_ = 500000;
    struct sysinfo info;
    int ret = sysinfo(&info);
    if(0 == ret)
    {
        int num = info.totalram >> 30;
        num -= 4;
        if(num > 1)
        {
            save_key_num_ *= num;
        }
    }

    timer_.AsyncLoop(10 * 1000, [this](){
        ClearExpireData();
    });
}

DBCache::~DBCache()
{
    timer_.Cancel();
}

bool DBCache::GetData(const std::string &key, std::string &value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);
    if(data_.cend() == it)
    {
        return false;
    }
    value = it->second.second;
    return true;
}

bool DBCache::SetData(const std::string &key, const std::string &value)
{
    {
        std::set<std::string> data_key;
        data_key.insert(key);
        DeleteData(data_key);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t now = time(nullptr);
    data_.insert(std::make_pair(key, std::make_pair(now, value)));
    auto it = time_data_.find(now);
    if(time_data_.cend() == it)
    {
        std::set<std::string> keys;
        keys.insert(key);
        time_data_.insert(std::make_pair(now, keys));
    }
    else
    {
        it->second.insert(key);
    }
    return true;
}
bool DBCache::DeleteData(const std::string &key)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto data_it = data_.find(key);
    if(data_.cend() != data_it)
    {
        data_.erase(data_it);
    }
    auto it = time_data_.find(data_it->second.first);
    if(time_data_.cend() != it)
    {
        it->second.erase(key);
    }

    return true;
}

bool DBCache::AddData(const std::map<std::string, std::string> &add_data)
{
    {
        std::set<std::string> data_key;
        for(auto data : add_data)
        {
            data_key.insert(data.first);
        }
        DeleteData(data_key);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::string> keys;
    uint64_t now = 0;
    for(auto data : add_data)
    {
        now = time(nullptr);
        data_.insert(std::make_pair(data.first, std::make_pair(now, data.second)));
        auto it = time_data_.find(now);
        if(time_data_.cend() == it)
        {
            keys.clear();
            keys.insert(data.first);
            time_data_.insert(std::make_pair(now, keys));
        }
        else
        {
            it->second.insert(data.second);
        }
    }
    return true;
}

size_t DBCache::GetDataSize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t size = 0;
    size += (time_data_.size() * sizeof(uint64_t));
    size += (data_.size() * sizeof(uint64_t));
    for(auto data : data_)
    {
        size = size + data.first.size() + data.first.size() + data.second.second.size();
    }
    return size;
}
bool DBCache::DeleteData(const std::set<std::string> &keys)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto key : keys)
    {
        auto data_it = data_.find(key);
        if(data_.cend() == data_it)
        {
            continue;
        }
        data_.erase(data_it);
        auto it = time_data_.find(data_it->second.first);
        if(time_data_.cend() == it)
        {
            continue;
        }
        it->second.erase(key);
    }
    return true;
}
void DBCache::ClearExpireData()
{
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t now = time(nullptr);
    for(auto it = time_data_.cbegin(); time_data_.cend() != it; ++it)
    {
        if((save_time_ > (now - it->first)) || save_key_num_ > data_.size())
        {
            break;
        }
        for(auto key : it->second)
        {
            data_.erase(key);
        }
    }
}
