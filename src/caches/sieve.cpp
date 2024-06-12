//
// Created by zhenyus on 11/2/19.
//

#include "sieve.h"

void SIEVECache::hit(sieveCacheMapType::const_iterator it) {
    //typedef std::unordered_map<uint64_t, ListIteratorType> sieveCacheMapType; 是这么一个类型，一种map，第二个是ListIteratorType
    //typedef std::list<uint64_t>::iterator ListIteratorType; 其实是一种list<> 的迭代器
    //是一个list的方法，第一个表示插入的位置迭代器，第二个是要插入的源list，第三个是范围起始，是将B中的C-D插入A的相应位置
    cache_list.splice(cache_list.begin(), cache_list, it->second);
}


void SIEVECache::async_lookup(const uint64_t &key) {
    //first update the metadata: insert/update, which can trigger pending data.mature
    auto it = cache_map.find(key);
    if (it != cache_map.end()) {
        hit(it);
    }
}

void SIEVECache::async_admit(const uint64_t &key, const int64_t &size,
                                   const uint16_t extra_features[max_n_extra_feature]) {
    auto it = cache_map.find(key);
    if (it == cache_map.end()) {
        bool seen = filter.exist_or_insert(key);
        if (!seen)
            goto Lnoop;

        cache_list.push_front(key);
        cache_map[key] = cache_list.begin();
        _currentSize += size;
        //slow to insert before local metadata, because in the future there will be additional fetch step
        auto shard_id = key%n_shard;
        size_map_mutex[shard_id].lock();
        size_map[shard_id].insert({key, size});
        size_map_mutex[shard_id].unlock();
    } else {
        //already in the cache
        goto Lnoop;
    }

    // check more eviction needed?
    while (_currentSize > _cacheSize) {
        evict();
    }
    //no logical op is performed
    Lnoop:
    return;
}

void SIEVECache::evict() {
    //list<uint64_t> cache_list; 
    //end得到最后一个的下一个，--到最后一个
    auto lit = --(cache_list.end());
    //得到lit指向的元素
    uint64_t key = *lit;
    //fast to remove before local metadata, because in the future will have async admission
    auto shard_id = key%n_shard;
    auto sit = size_map[shard_id].find(key);
    uint64_t size = sit->second;
    size_map_mutex[shard_id].lock();
    size_map[shard_id].erase(key);
    size_map_mutex[shard_id].unlock();
    _currentSize -= size;
    cache_map.erase(key);
    cache_list.erase(lit);
}