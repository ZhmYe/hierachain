#pragma once

#include <libdevcore/CommonData.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Address.h>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <queue>
#include <chrono>
#include <string>

using namespace std;

namespace dev {
namespace eth {
    class Transaction;
}

namespace consensus {
    extern int hiera_shard_number;
    extern int internal_groupId;
}
}

namespace dev {
namespace plugin {

class ExecuteVM;

#define PLUGIN_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("PLUGIN") << LOG_BADGE("PLUGIN")

// 交易类型
enum transactionType:int
{
    DeployContract=0,
    BatchedSubInterShard=1,
    InterShard=2,
    MasterChangeRequest=4,
    MasterChangePrePrepare=5,
    MasterChangePrepare=6,
    MasterChangeCommit=7,
    SingleIntraShard=8,
    CombinedIntraShard=9,
    ShuffleValue=10,
    ShuffleBlockedTx=11,
};

// 跨片子交易
struct subInterShardTx
{
	string dest_shardid; // 跨片子交易的目标分片Id
	string txrlp;  // 子交易Rlp
	string rwkey;  // 子交易读写集(假设每个跨片子交易仅访问一个状态)
};

// 计算最近公共祖先基础类
class Hierarchical_Shard {
    public:
        using Ptr = std::shared_ptr<Hierarchical_Shard>;
        Hierarchical_Shard(int shardid){ id = shardid; };
        Hierarchical_Shard::Ptr getLCA(Hierarchical_Shard::Ptr shard1, Hierarchical_Shard::Ptr shard2) {
			std::set<Ptr> ancestors;
			while (shard1) {
				ancestors.insert(shard1);
				shard1 = shard1->preshard;
			}
			while (shard2) {
				if (ancestors.find(shard2) != ancestors.end())
					return shard2;
				shard2 = shard2->preshard;
			}
			return nullptr;
		}
    public:
        int id = 0;
        Hierarchical_Shard::Ptr preshard = nullptr;
};

// 构建层级化信息
class Hierarchical_Structure {
    public:
        using Ptr = std::shared_ptr<Hierarchical_Structure>;
        Hierarchical_Structure() {
            shards = make_shared<vector<Hierarchical_Shard::Ptr>>();
            if(dev::consensus::hiera_shard_number == 3) {
                auto shard1 = make_shared<Hierarchical_Shard>(1);
                auto shard2 = make_shared<Hierarchical_Shard>(2);
                auto shard3 = make_shared<Hierarchical_Shard>(3);

                shard1->preshard = shard3;
                shard2->preshard = shard3;

                shards->push_back(shard1);
                shards->push_back(shard2);
                shards->push_back(shard3);
            }
            else if(dev::consensus::hiera_shard_number == 4) {
                auto shard1 = make_shared<Hierarchical_Shard>(1);
                auto shard2 = make_shared<Hierarchical_Shard>(2);
                auto shard3 = make_shared<Hierarchical_Shard>(3);
                auto shard4 = make_shared<Hierarchical_Shard>(4);

                shard1->preshard = shard4;
                shard2->preshard = shard4;
                shard3->preshard = shard4;

                shards->push_back(shard1);
                shards->push_back(shard2);
                shards->push_back(shard3);
                shards->push_back(shard4);
            }
            else if(dev::consensus::hiera_shard_number == 9) {
                auto shard1 = make_shared<Hierarchical_Shard>(1);
                auto shard2 = make_shared<Hierarchical_Shard>(2);
                auto shard3 = make_shared<Hierarchical_Shard>(3);
                auto shard4 = make_shared<Hierarchical_Shard>(4);
                auto shard5 = make_shared<Hierarchical_Shard>(5);
                auto shard6 = make_shared<Hierarchical_Shard>(6);
                auto shard7 = make_shared<Hierarchical_Shard>(7);
                auto shard8 = make_shared<Hierarchical_Shard>(8);
                auto shard9 = make_shared<Hierarchical_Shard>(9);

                shard1->preshard = shard4;
                shard2->preshard = shard4;
                shard3->preshard = shard4;
                shard5->preshard = shard8;
                shard6->preshard = shard8;
                shard7->preshard = shard8;
                shard4->preshard = shard9;
                shard8->preshard = shard9;

                shards->push_back(shard1);
                shards->push_back(shard2);
                shards->push_back(shard3);
                shards->push_back(shard4);
                shards->push_back(shard5);
                shards->push_back(shard6);
                shards->push_back(shard7);
                shards->push_back(shard8);
                shards->push_back(shard9);
            }
        }

    public:
        shared_ptr<vector<Hierarchical_Shard::Ptr>> shards;
};

// 来自其他分片的key的在其本地的征用率对象
class remoteKeyContention: public enable_shared_from_this<remoteKeyContention> {
    public:
        remoteKeyContention(string& _shardid, string& _contention, string& _stateAddress){
            shardid = _shardid;
            contention = _contention;
            stateAddress = _stateAddress;
        }
    public:
        string shardid;
        string contention;
        string stateAddress;
};

// 最近使用的本地状态、跨片交易使用情况
class LRU_StatesInfo: public std::enable_shared_from_this<LRU_StatesInfo> 
{
    public:
        LRU_StatesInfo() {
            m_LRU_localkeys_intraTx = make_shared<queue<string>>(); // 最近被片内交易访问的1000个本地状态
            m_LRU_localkeys_interTx = make_shared<map<string, shared_ptr<map<string, int>>>>(); // 记录最近被跨片交易访问的100个本地状态

            m_LRU_localkeys_intraTx_summarized = make_shared<map<string, int>>(); // 记录最近被片内交易访问的本地状态
            m_LRU_localkeys_interTx_summarized = make_shared<map<string, shared_ptr<map<string, int>>>>(); // 记录最近被访问的本地状态，是参与跨片交易的情况

            m_LRU_remotekeys_intraTx = make_shared<map<string, shared_ptr<remoteKeyContention>>>(); // 来自其他分片的状态在原分片的争用率

            intraTx_count = 0; // m_LRU_localkeys_intraTx 中已经缓存的key数目
            interTx_count = 0; // m_LRU_localkeys_interTx 中已经缓存的key数目
        }

        // 更新本地状态被片内交易访问的情况
        void update_lru_localkeys_intraTx(string& key) {
            std::unique_lock<std::mutex> locker(intraTx_queue_mutex); // 对队列上锁

            m_LRU_localkeys_intraTx->push(key); // 记录交易访问的本地状态
            intraTx_count++;

            if(m_LRU_localkeys_intraTx->size() > 1000) { // 只统计最近1000个
                m_LRU_localkeys_intraTx->pop();
            }

            if(intraTx_count == 1000) { // 每次新增1000个交易, 重新更新 m_LRU_localkeys_intraTx_summarized
                intraTx_count = 0;      // 重新计数

                std::unique_lock<std::mutex> locker(intraTx_summary_mutex); // 对队列上锁
                m_LRU_localkeys_intraTx_summarized->clear(); // 清除map
                
                int queue_size = m_LRU_localkeys_intraTx->size();
                for(int i = 0; i < queue_size; i++) {
                    string key = m_LRU_localkeys_intraTx->front();
                    if(m_LRU_localkeys_intraTx_summarized->count(key) == 0) {
                        m_LRU_localkeys_intraTx_summarized->insert(make_pair(key, 1));
                    }
                    else {
                        int number = m_LRU_localkeys_intraTx_summarized->at(key);
                        m_LRU_localkeys_intraTx_summarized->at(key) = number + 1;
                    }
                    m_LRU_localkeys_intraTx->pop();
                }
                // show_localkeys_intraTx_summarized(); // 每统计500个读写集展示一次
            }
        }

        // 更新本地状态被跨分片交易访问的情况
        void update_lru_localkeys_interTx(string& localkey, string& remotekey) {
            std::unique_lock<std::mutex> locker(interTx_queue_mutex); // 对队列上锁
            interTx_count++;
            if(m_LRU_localkeys_interTx->count(localkey) == 0) {
                auto item = make_shared<map<string, int>>();
                item->insert(make_pair(remotekey, 1));
                m_LRU_localkeys_interTx->insert(make_pair(localkey, item));
            }
            else {
                auto item = m_LRU_localkeys_interTx->at(localkey);
                if(item->count(remotekey) != 0) {
                    int number = item->at(remotekey);
                    item->at(remotekey) = number + 1;
                }
                else {
                    item->insert(make_pair(remotekey, 1));
                }
            }

            if(interTx_count == 500) {
                interTx_count = 0;
                std::unique_lock<std::mutex> locker(interTx_summary_mutex); // 对队列上锁
                // 将 m_LRU_localkeysByInterTx 的值拷贝给 m_LRU_localkeys_interTx_summarized
                *m_LRU_localkeys_interTx_summarized = *m_LRU_localkeys_interTx;
                m_LRU_localkeys_interTx->clear();
                // show_localkeys_interTx_summarized(); // 每统计500个读写集展示一次
            }
        }

        // 更新收到的其他分片状态在本地的争用情况
        void updateRemoteKeyContention(string& remotekey, string& number, string& shardid) { 
            std::unique_lock<std::mutex> locker(inter_remoteKey_mutex); // 对队列上锁
            if(m_LRU_remotekeys_intraTx->count(remotekey) != 0) {
                auto item = m_LRU_remotekeys_intraTx->at(remotekey);
                item->contention = number;
                item->shardid = shardid;
            }
            else {
                auto item = make_shared<remoteKeyContention>(shardid, number, remotekey);  
                m_LRU_remotekeys_intraTx->insert(make_pair(remotekey, item));
            }
        }

        // 返回本地状态key被片内交易的争用情况
        string getLocalKeyIntraContention(string& key){
            std::unique_lock<std::mutex> locker(intraTx_summary_mutex); // 对队列上锁
            if(m_LRU_localkeys_intraTx_summarized->count(key) != 0) {
                int accessedNumber = m_LRU_localkeys_intraTx_summarized->at(key);
                return to_string(accessedNumber);
            }
            else {
                return "0";
            }
        }

        // 返回跨片交易对本地状态的争用情况
        int getLocalKeyInterContention(string& key, string& remotekey) {
            std::unique_lock<std::mutex> locker(interTx_summary_mutex); // 对队列上锁
            if(m_LRU_localkeys_interTx_summarized->count(key) != 0) {
                auto item = m_LRU_localkeys_interTx_summarized->at(key);
                if(item->count(remotekey) != 0) {
                    int accessedNumber = item->at(remotekey);
                    return accessedNumber;
                }
                else {
                    return 0;
                }
            }
            else {
                return 0;
            }
        }

        // 返回其他分片的状态在原分片中的争用率
        void getRemoteKeyIntraContention(string& shardid, string& contention, string& remotekey){
            std::unique_lock<std::mutex> locker(inter_remoteKey_mutex); // 对队列上锁
            PLUGIN_LOG(INFO) << LOG_DESC("getRemoteKeyIntraContention...")
                            << LOG_KV("remotekey", remotekey);

            if(m_LRU_remotekeys_intraTx->count(remotekey) == 0){
                contention = "none";
                return;
            }

            auto keyinfo = m_LRU_remotekeys_intraTx->at(remotekey);
            shardid = keyinfo->shardid;
            contention = keyinfo->contention;
        }

        // string getIntraTxAccessedremotekey(string& remotekey) // 获取收到的其他分片状态的争用情况
        // {
        //     std::unique_lock<std::mutex> locker(inter_remoteKey_mutex); // 对队列上锁
        //     if(m_LRU_remotekeys_intraTx->count(remotekey) != 0)
        //     {
        //         auto item = m_LRU_remotekeys_intraTx->at(remotekey);
        //         string number = item->contention;
        //         item->contention = "0"; //使用后归0，用于每次都观察到最新值
        //         return number;
        //     }
        //     else
        //     {
        //         return "0";
        //     }
        // }

        void show_localkeys_intraTx_summarized(){
            // for(auto it = m_LRU_localkeys_intraTx_summarized->begin(); it != m_LRU_localkeys_intraTx_summarized->end(); it++) {
            //     string key = it->first;
            //     int accessedNumber = it->second;
            //     PLUGIN_LOG(INFO) << LOG_KV("key", key) << LOG_KV("accessedNumber", accessedNumber);
            // }

            PLUGIN_LOG(INFO) << LOG_DESC("按照value降序打印localkeys_intraTx_summarized 内容");

            vector<pair<string, int>> vtMap;
            for (auto it = m_LRU_localkeys_intraTx_summarized->begin(); it != m_LRU_localkeys_intraTx_summarized->end(); it++) {
                vtMap.push_back(make_pair(it->first, it->second));
            }

            sort(vtMap.begin(), vtMap.end(), 
                [](const pair<string, int> &x, const pair<string, int> &y) -> int {
                    return x.second > y.second;
            });

            for (auto it = vtMap.begin(); it != vtMap.end(); it++) {
                PLUGIN_LOG(INFO) << LOG_KV("key", it->first) << LOG_KV("accessed_number", it->second);
            }
        }

        void show_localkeys_interTx_summarized() {

            PLUGIN_LOG(INFO) << LOG_DESC("打印本地状态被跨片交易访问情况");
            for(auto summaryItem = m_LRU_localkeys_interTx_summarized->begin(); summaryItem != m_LRU_localkeys_interTx_summarized->end(); summaryItem++){
                string localKey = summaryItem->first;
                PLUGIN_LOG(INFO) << LOG_KV("localKey", localKey);
                auto item = summaryItem->second;
                for(auto it = item->begin(); it != item->end(); it++) {
                    string remoteKey = it->first;
                    int accessNumber = it->second;
                    PLUGIN_LOG(INFO) << LOG_KV("remoteKey", remoteKey)
                                     << LOG_KV("accessNumber", accessNumber);
                }
            }
        }

        // void clearInterTxAccessedlocalkeys() {
        //     std::unique_lock<std::mutex> locker(interTx_summary_mutex); // 对队列上锁
        //     PLUGIN_LOG(INFO) << LOG_DESC("清空本地状态被跨片交易访问情况");
        //     m_LRU_localkeys_interTx_summarized->clear();
        // }

    public:

        shared_ptr<queue<string>> m_LRU_localkeys_intraTx;
        shared_ptr<map<string, int>> m_LRU_localkeys_intraTx_summarized;

        shared_ptr<map<string, shared_ptr<map<string, int>>>> m_LRU_localkeys_interTx;
        shared_ptr<map<string, shared_ptr<map<string, int>>>> m_LRU_localkeys_interTx_summarized;

        shared_ptr<map<string, shared_ptr<remoteKeyContention>>> m_LRU_remotekeys_intraTx; // 记录其他分片发来的状态争用情况

        std::mutex intraTx_queue_mutex;
        std::mutex interTx_queue_mutex;
        std::mutex intraTx_summary_mutex;
        std::mutex interTx_summary_mutex;
        std::mutex inter_remoteKey_mutex;

        int intraTx_count;
        int interTx_count;
};

// 积攒的跨片子交易
class cachedSubTxsContainer: public std::enable_shared_from_this<cachedSubTxsContainer>
{
    public:
        cachedSubTxsContainer() {
            m_txNum = 0; // 交易总数
            m_epochId = 1; // epochId
            m_subtxs = make_shared<string>();        // 跨片子交易，后面需要完善(应该把整笔交易都发过去)
            m_intrashardtxs = make_shared<string>(); // 重组片内交易
            m_sourceshardId = dev::consensus::internal_groupId; // 当前分片作为协调者的分片id
        }

        cachedSubTxsContainer(int txNum, int epochId, int destinshardId, 
                                shared_ptr<string> subtxs, shared_ptr<string> intrashardtxs) {
            m_txNum = txNum;
            m_epochId = epochId;
            m_destinshardId = destinshardId;
            m_subtxs = subtxs;
            m_intrashardtxs = intrashardtxs;
            m_sourceshardId = dev::consensus::internal_groupId; // 当前分片作为协调者的分片id
        }

         // 插入跨片子交易后面需要完善(应该所有子交易都发过去)
        void insertSubTx(string& subtx){
            std::lock_guard<std::mutex> lock(containerLock);
            if(*m_subtxs == ""){
                *m_subtxs = subtx;
            }
            else{
                *m_subtxs = *m_subtxs + "&" + subtx;
            }
            m_txNum = m_txNum + 1;
        }

        // 插入重组片内交易
        void insertIntraShardTx(string& intrashardtx) {
            std::lock_guard<std::mutex> lock(containerLock);

            if(*m_intrashardtxs == ""){
                *m_intrashardtxs = intrashardtx;
            }
            else{
                *m_intrashardtxs = *m_intrashardtxs + "&" + intrashardtx;
            }
            m_txNum = m_txNum + 1;
        }

        // 获取当前缓存的所有交易，加清除功能
        void getTxs(std::shared_ptr<cachedSubTxsContainer> container){
            std::lock_guard<std::mutex> lock(containerLock);
            container->m_txNum = m_txNum;
            container->m_epochId = m_epochId;
            container->m_destinshardId = m_destinshardId;
            container->m_sourceshardId = m_sourceshardId;
            *container->m_subtxs = *m_subtxs;
            *container->m_intrashardtxs = *m_intrashardtxs;

            m_txNum = 0;
            // m_epochId++; // 修改为全部epoch++
            (*m_subtxs) = "";
            (*m_intrashardtxs) = "";
        }

        int getTxSize(){ // 获取当前缓存的所有交易总数
            std::lock_guard<std::mutex> lock(containerLock);
            return m_txNum;
        }

    public:
        int m_txNum;
        int m_epochId;
        int m_destinshardId;
        int m_sourceshardId;
        std::shared_ptr<std::string> m_subtxs;
        std::shared_ptr<std::string> m_intrashardtxs; // 记录当前epoch内积攒的片内交易
        std::mutex containerLock;
};

// 带读写集信息的交易
class transactionWithReadWriteSet: public std::enable_shared_from_this<transactionWithReadWriteSet>
{
    public:
        using Ptr = shared_ptr<transactionWithReadWriteSet>;

        transactionWithReadWriteSet() {}

        transactionWithReadWriteSet(shared_ptr<dev::eth::Transaction> _tx) {
            tx = _tx;
        }

        transactionWithReadWriteSet(shared_ptr<dev::eth::Transaction> _tx, string& _participantIds, string& _localreadwriteKey,
                                    string& _rwkeys, string& _interShardTxEpochID, string& _intershardTxid, string& _coordinator_shardid, 
                                    int _minimum_participant_shardid) {
            tx = _tx;
            participantIds = _participantIds;
            localreadwriteKey = _localreadwriteKey;
            rwkeys = _rwkeys;
            interShardTxEpochID = _interShardTxEpochID;
            intershardTxId = _intershardTxid;
            coordinator_shardid = _coordinator_shardid;
            minimum_participant_shardid = _minimum_participant_shardid;
        }

        transactionWithReadWriteSet(string& _participantIds, string& _localreadwriteKey,
                                    string& _rwkeys, string& _interShardTxEpochID, string& _intershardTxid, string& _coordinator_shardid, 
                                    int _minimum_participant_shardid) {
            // tx = _tx;
            participantIds = _participantIds;
            localreadwriteKey = _localreadwriteKey;
            rwkeys = _rwkeys;
            interShardTxEpochID = _interShardTxEpochID;
            intershardTxId = _intershardTxid;
            coordinator_shardid = _coordinator_shardid;
            minimum_participant_shardid = _minimum_participant_shardid;
            // txid = _txid;
        }
        
        void setrwkeysTosend(std::vector<std::string> _rwkeysTosend) {  // 设置需要发送的读写集信息
            rwkeysTosend = _rwkeysTosend;
        }

        void setTxid(string _txid){
            txid = _txid;
        }

    public:

        string txid;
        int minimum_participant_shardid; // 跨片交易参与方的最小分片id
        bool emptyTransaction = false;
        bool is_intershardTx = false;
        bool lastTxInEpoch = false; // 每个epoch最后一笔跨片交易的标记位
        bool lastTxInBatch = false; // 每个batch的最后一笔跨片交易的标记位
        bool shuffle_states = false; // 该笔交易处理结束后，是否进行状态shuffle

        string participantIds;
        string localreadwriteKey;
        string interShardTxEpochID;
        string interShardTxBatchId;
        string rwkeys;
        string intershardTxId;
        string coordinator_shardid;
        string shuffle_states_contents;

        vector<shared_ptr<dev::eth::Transaction>> txs;
        vector<string> rwkeysTosend;
        vector<string> rwkeyToReceive;
        vector<int> blockingQueue_keys;

        shared_ptr<dev::eth::Transaction> tx;
};

// 状态转移请求的 PrePrepare 消息
class StateMasterChangePrePrepareMsgs: public std::enable_shared_from_this<StateMasterChangePrePrepareMsgs>
{
    public:
        using Ptr = std::shared_ptr<StateMasterChangePrePrepareMsgs>;
        string sourceshardid;
        string destinshardid;
        string requestkeys;
        string messageid;

    public:
        StateMasterChangePrePrepareMsgs(string& _sourceshardid, string& _destinshardid, string& _keys, string& _messageid) {
            sourceshardid = _sourceshardid;
            destinshardid = _destinshardid;
            requestkeys = _keys;
            messageid = _messageid;
        }
};

// 状态转移请求的Commit消息
class StateMasterChangeCommitMsgs: public std::enable_shared_from_this<StateMasterChangeCommitMsgs>
{
    public:
        using Ptr = std::shared_ptr<StateMasterChangeCommitMsgs>;

        string sourceshardid;
        string destinshardid;
        string requestkeys;
        string messageid;

    public:
        StateMasterChangeCommitMsgs(string& _sourceshardid, string& _destinshardid, string& _keys, string& _messageid){
            sourceshardid = _sourceshardid;
            destinshardid = _destinshardid;
            requestkeys = _keys;
            messageid = _messageid;
        }
};

// 收发的读写集信息
class ReadWriteSetManager: public std::enable_shared_from_this<ReadWriteSetManager>
{
    public:
        ReadWriteSetManager() {
            receivedTxRWset = make_shared<std::map<string, int>>();
            receivedContentionRates = make_shared<std::map<string, string>>();
        }

        void deleteRwset(string key) {
            std::lock_guard<std::mutex> lock(receivedrwset_mutex);
            int number = receivedTxRWset->at(key);
            receivedTxRWset->at(key) = number - 1;
        }

        void insertReceivedRwset(string originalKey){
            std::lock_guard<std::mutex> lock(receivedrwset_mutex);
            if(receivedTxRWset->count(originalKey) == 0){
                receivedTxRWset->insert(make_pair(originalKey, 1));
            }
            else{
                int number = receivedTxRWset->at(originalKey);
                receivedTxRWset->at(originalKey) = number + 1;
            }
            // PLUGIN_LOG(INFO) << LOG_KV("originalKey", originalKey)
            //                  << LOG_KV("receivedTxRWset->at(originalKey)", receivedTxRWset->at(originalKey));
        }

        bool checkReceivedRwset(string key) {
            std::lock_guard<std::mutex> lock(receivedrwset_mutex);
            if(receivedTxRWset->count(key) == 0){
                return false;
            }
            else{
                if(receivedTxRWset->at(key) == 0){
                    return false;
                }
                else{
                    // int number = receivedTxRWset->at(key);
                    // receivedTxRWset->at(key) = number - 1;
                    return true;
                }
            }
        }

        bool checkReceivedRwset(string key, bool lastTxInEpoch) {

            std::lock_guard<std::mutex> lock(receivedrwset_mutex);
            if(receivedTxRWset->count(key) == 0){ // 若读写集key还没有
                return false;
            }
            else{ // 若读写集key已经存在
                if(receivedTxRWset->at(key) == 0){ // 若key的数目已经为0
                    return false;
                }
                else{ // 若key的数目不为0
                    if(lastTxInEpoch == true) { // 若当前交易为epoch中的最后一笔交易, 在其他地方对读写集进行减法操作了
                        // int number = receivedTxRWset->at(key);
                        // receivedTxRWset->at(key) = number - 1;
                        // PLUGIN_LOG(INFO) << LOG_KV("key", key)
                        //                  << LOG_KV("number", number)
                        //                  << LOG_KV("checkReceivedRwset key", receivedTxRWset->at(key));
                    }
                    return true;
                }
            }
        }

    public:
        shared_ptr<std::map<string, int>> receivedTxRWset;
        shared_ptr<std::map<string, string>> receivedContentionRates;
        std::mutex receivedrwset_mutex;
};

// 交易重排序使用的函数
class ReadWriteKeyCompare
{
    public:
        bool operator()(std::shared_ptr<transactionWithReadWriteSet> t1, std::shared_ptr<transactionWithReadWriteSet> t2) {
            return t1->participantIds <= t2->participantIds;
        }
};


template <typename T>
class BlockingQueue {
    public:
        BlockingQueue(size_t max_size = std::numeric_limits<size_t>::max()) : max_size_(max_size) {}
        BlockingQueue(const BlockingQueue&) = delete;
        BlockingQueue& operator=(const BlockingQueue&) = delete;

        template <typename... Args>
        void enqueue(Args&&... args) {
            std::unique_lock<std::mutex> locker(mtx_);
            not_full_.wait(locker, [this] { return queue_.size() < max_size_; });
            queue_.emplace(std::forward<Args>(args)...);
            not_empty_.notify_one();
        }

        T dequeue() {
            std::unique_lock<std::mutex> locker(mtx_);
            not_empty_.wait(locker, [this] { return queue_.size() > 0; });
            T x = std::move(queue_.front());
            queue_.pop();
            not_full_.notify_one();
            return x;
        }

        size_t size() {
            std::unique_lock<std::mutex> locker(mtx_);
            return queue_.size();
        }

        T front(){
            return queue_.front();
        }

    private:
        size_t max_size_;
        std::queue<T> queue_;
        std::mutex mtx_;
        std::condition_variable not_empty_;
        std::condition_variable not_full_;
};


    extern int global_internal_groupId;
    extern shared_ptr<ExecuteVM> executiveContext;
    extern string nodeIdHex;

    extern shared_ptr<map<string, bool>> m_validStateKey;
    extern shared_ptr<map<string, bool>> m_masterStateKey;
    extern shared_ptr<map<string, int>> m_masterRequestVotes;
    extern shared_ptr<map<string, int>> m_masterChangedKey;
    extern shared_ptr<set<string>> m_lockedStateKey;
    extern Address depositAddress;

    extern shared_ptr<map<string, long>> m_txid_to_starttime;
    extern shared_ptr<map<string, long>> m_txid_to_endtime;
    extern shared_ptr<map<string, string>> m_coordinator_epochId2data_str;


    }  // namespace plugin
}  // namespace dev