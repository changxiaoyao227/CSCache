//
// Created by zhenyus on 11/16/19.
//


#include "relaxed_belady.h"
#include "utils.h"

using namespace std;

bool RelaxedBeladyCache::lookup(const SimpleRequest &_req) {
    // SimpleRequest 是 AnnotatedRequest的基类，dy<派生>(基类) 是有风险的，反之无。要求这个req指向的是派生类对象
    //AnnotatedRequest多了一个next_req
    auto &req = dynamic_cast<const AnnotatedRequest &>(_req);
    //第几个seq,seq,id,size,next_req
    current_t = req.seq;
    //key_map 是在.h中定义的unordered_map<int64_t, pair<MetaT, uint32_t >> key_map
    //其中mateT是枚举类型within_boundary = 0, beyond_boundary = 1 是否在边界里.
    //
    auto it = key_map.find(req.id);
    //找到了当前req对应的id在key_map中.
    if (it != key_map.end()) {
        //update past timestamps,it->second.first对应于是否在边界中.
        auto list_idx = it->second.first;
        //list_idx在边界中
        if (within_boundary == list_idx) {
            if (req.next_seq - current_t >= belady_boundary) {
                //超出了边界,修改metaT, 后面一个参数是vector<RelaxedBeladyMeta> beyond_boundary_meta;
                //RelaxedBeladyMeta包含了key size past_t,future_t
                //TODO: should modify instead of insert
                it->second = {beyond_boundary, beyond_boundary_meta.size()};
                //不能隐式转换的AnnotatedRequest对象,用id做key,size做size,seq做past_t,next_seq做future_t
                beyond_boundary_meta.emplace_back(req);
            } else {
                //multimap<uint64_t, pair<int64_t, int64_t>, greater<uint64_t >> within_boundary_meta
                //A future_t,B id,C size  根据A进行降序排序
                within_boundary_meta.emplace(req.next_seq, pair(req.id, req.size));
            }
            //抹去当前seq在withinmeta中的内容
            within_boundary_meta.erase(req.seq);
        }
        //list_idx不在边界中 
        else {
            //得到位置
            auto pos = it->second.second;
            auto &meta = beyond_boundary_meta[pos];
            assert(meta._key == req.id);
            //内联方法,将past_t更新为req的seq,future_t更新为req的next_seq
            meta.update(req);
            //meta在这个边界中,但判定的时候,req不在这个边界中,?.
            //函数对pos的MetaT做了改变,也把meta加入到了within中
            //如果pos不是尾索引:
            //将尾元素移至pos位置,替换原pos元素
            //更新尾元素在keymap映射中的索引指向
            if (meta._future_timestamp - current_t < belady_boundary) {
                beyond_meta_remove_and_append(pos);
            }
        }
        return true;
    }
    return false;
}

void RelaxedBeladyCache::admit(const SimpleRequest &_req) {
    //老操作,将simple,检查后转换为Anreq,如果传进来是simple的对象,会报错欸
    auto &req = static_cast<const AnnotatedRequest &>(_req);
    //这些东西都是size的引用
    const uint64_t &size = req.size;
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req.id, size);
        return;
    }

    auto it = key_map.find(req.id);
    if (it == key_map.end()) {
        //这是没找到的情况.如果req的下次访问与当前时间大于boundary.会把它插入key_map中,
        if (req.next_seq - req.seq >= belady_boundary) {
            //但是C放了meta.size(),据说是pos,第几个
            key_map.insert({req.id, {beyond_boundary, beyond_boundary_meta.size()}});
            //超过界线meta也收seq队列
            beyond_boundary_meta.emplace_back(req);
        } else {
            //C放了0?
            key_map.insert({req.id, {within_boundary, 0}});
            //在队列中
            within_boundary_meta.emplace(req.next_seq, pair(req.id, req.size));
        }
        //当前的size++
        _currentSize += size;
    }
    //加入一个obj之后开始evict 合理的.
    // check more eviction needed?
    while (_currentSize > _cacheSize) {
        evict();
    }
}


pair<RelaxedBeladyCache::MetaT, uint32_t> RelaxedBeladyCache::rank() {
    //这样就可以生成一个随机整数
    auto rand_idx = _distribution(_generator);
    //如果超出界限的meta不为空
    while (!beyond_boundary_meta.empty()) {
        //那它有一个size 取模就是位置
        auto pos = rand_idx % beyond_boundary_meta.size();
        auto &meta = beyond_boundary_meta[pos];
        //它的f_t小于Belady,移除该位置的东西
        if (meta._future_timestamp - current_t < belady_boundary) {
            beyond_meta_remove_and_append(pos);
        } else {
            return {beyond_boundary, pos};
        }
    }
    //如果超出界限的meta为空,则返回within_boundry
    return {within_boundary, 0};
}

void RelaxedBeladyCache::evict() {
    //先调用了一个函数,让我看看呢
    auto epair = rank();
    auto &meta_type = epair.first;
    auto &old_pos = epair.second;
//
//    //record meta's future interval
//
//#ifdef EVICTION_LOGGING
//    {
//        auto &meta = meta_holder[0][old_pos];
//        //record eviction decision quality
//        unsigned int decision_qulity =
//                static_cast<double>(meta._future_timestamp - current_t) / (_cacheSize * 1e6 / byte_million_req);
//        decision_qulity = min((unsigned int) 255, decision_qulity);
//        eviction_distances.emplace_back(decision_qulity);
//    }
//#endif
    //如果返回结果是在boundary中
    if (within_boundary == meta_type) {
        auto it = within_boundary_meta.begin();
        //within_的类型,uint64_t, pair<int64_t, int64_t>,其中按照A future_t降序排列
        //B一般放的是key,C是size.
        key_map.erase(it->second.first);
        _currentSize -= it->second.second;
        within_boundary_meta.erase(it);
    } else {
        //在h文件中
        beyond_meta_remove(old_pos);
    }
}

