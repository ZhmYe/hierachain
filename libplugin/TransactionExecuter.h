#pragma once

#include "Common.h"
#include "libplugin/Benchmark.h"
#include <libconsensus/pbft/Common.h>
#include <libplugin/BlockingTxQueue.h>
#include <libp2p/P2PInterface.h>
#include <queue>
#include <libexecutive/Executive.h>
#include <librpc/Common.h>
#include <libethcore/Transaction.h>
#include <libplugin/ExecuteVM.h>
#include <libconsensus/pbft/Common.h>
#include <libconsensus/pbft/PBFTEngine.h>
#include <thread>
#include <libsync/Common.h>
#include <test/unittests/libevm/FakeExtVMFace.h>
#include <evmc/evmc.h>
#include <chrono>
#include <libdevcore/Common.h>
#include <libethcore/Transaction.h>
#include <fstream>
#include <sys/time.h>
#include <stdlib.h>
#include <iterator>
#include <librpc/Rpc.h>

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::plugin;
using namespace dev::consensus;


namespace dev{
    namespace plugin
    {
        class TransactionExecuter:public enable_shared_from_this<TransactionExecuter>
        {
            public:
                TransactionExecuter(shared_ptr <P2PInterface> group_p2p_service, shared_ptr <P2PInterface> p2p_service, 
                    PROTOCOL_ID group_protocolID, PROTOCOL_ID protocolID, string& upper_shardids, string& lower_shardids, shared_ptr<dev::rpc::Rpc> rpc_service)
                {
                    string path = "./" + to_string(internal_groupId);
                    dev::plugin::executiveContext = make_shared<ExecuteVM>(path);
                    exec = dev::plugin::executiveContext->getExecutive();
                    vm = dev::plugin::executiveContext->getExecutiveInstance();

                    m_hierarchical_structure = make_shared<Hierarchical_Structure>();
                    // m_blocking_intrashardTxQueue = make_shared<BlockingTxQueue>();
                    m_blockingTxQueues = make_shared<map<string, shared_ptr<BlockingTxQueue>>>();
                    m_txQueues = make_shared<map<string, shared_ptr<queue<transactionWithReadWriteSet::Ptr>>>>();
                    m_LRU_StatesInfo = make_shared<LRU_StatesInfo>();
                    m_readWriteSetManager = make_shared<ReadWriteSetManager>();
                    m_subTxsContainers = make_shared<map<int, shared_ptr<cachedSubTxsContainer>>>();
                    m_valid_state = make_shared<map<string, bool>>();
                    m_master_state = make_shared<map<string, bool>>();
                    m_blocked_intraTxContents = make_shared<queue<string>>();
                    m_blocked_originTxRlps = make_shared<queue<string>>();
                    m_blocked_originTxRlps_Queues = make_shared<map<int, shared_ptr<BlockingTxQueue>>>();
                    m_cachedBatchTxs = make_shared<map<string, shared_ptr<map<int, string>>>>();
                    // m_intershardTxids = make_shared<map<int, int>>();
                    m_committed_intershardTxid = make_shared<map<int, shared_ptr<vector<string>>>>();
                    m_shuffleValue_destshardid = make_shared<map<string, int>>();
                    m_shuffleTxRLP_destshardid = make_shared<map<string, int>>();
                    m_stateTransfer_Rules = make_shared<map<string, shared_ptr<vector<string>>>>();

                    m_processed_intrashardTxNum = 0;
                    m_processed_intershardTxNum = 0;
                    m_upper_shardids = upper_shardids;
                    m_lower_shardids = lower_shardids;
                    m_group_p2p_service = group_p2p_service;
                    m_group_protocolID = group_protocolID;
                    m_p2p_service = p2p_service;
                    m_protocolID = protocolID;

                    m_rpc_service = rpc_service;
                    init_upperlower_shardids(m_upper_shardids, m_lower_shardids); // 初始化上下层信息
                    init_processedEpochIds(upper_shardids); // 为每一个上层分片初始化一个已经处理的跨片交易批ID
                    init_blockingTxQueues(upper_shardids); // 
                    init_subTxsContainers(); // 初始化向每个下层分片发送的子交易队列
                    init_blocked_originTxRlps_Queues(); // 初始化作为副写分片时为每个主写分片阻塞的交易队列
                    init_simulateTx();
                    
                    // load_statekeys_distribution(); // 初始化分片间的状态划分
                    // load_state_transfer_rule(); // 读取写权限转移的规则
                    // state_right_transfer_simulation(); // 模拟分片间的状态写权限转移后的结果

                    m_lastLogTime = std::chrono::steady_clock::now();
                    m_commit_finished_flag = false;
                }

                u256 generateRandomValue(){ // 用于生成交易nonce字段的随机数
                    auto randomValue = h256::random();
                    return u256(randomValue);
                }

                void init_upperlower_shardids(string& upper_shardids, string& lower_shardids);

                void init_processedEpochIds(string& upper_shardids);

                void init_blockingTxQueues(string& upper_shardids);

                void init_subTxsContainers();

                void init_simulateTx();

                void init_blocked_originTxRlps_Queues(); // 作为副写分片阻塞的交易队列

                void load_statekeys_distribution();

                void load_state_transfer_rule();

                void state_right_transfer_simulation(); // 非必要不启动，否则世界将会爆炸💥

                shared_ptr<dev::executive::Executive> getExecutive() { return exec; }

                string getValueByStateAddress(string& rwkey);

                void processConsensusBlock();

                void processInterShardTx(string& data_str);

                void accumulateSubTxs(string& data_str);

                string dataToHexString(bytes data);

                int checkTransactionType(string& hex_m_data_str);

                void checkSubTxsContainers(); // 每隔一个epoch将积攒的跨片子交易发送给相应分片，更新batchId

                void processOrderedBatchedSubInterShardTxs(string& data_str);

                void processBatchSubInterShardTxs(string coordinator_shardid, vector<string>* txcontents);

                void processReorderedTxs(string coordinator_shardid, shared_ptr<set<transactionWithReadWriteSet::Ptr, ReadWriteKeyCompare>> reorderedTxs, string& interShardTxEpochID, bool _shuffle_states, string& _shuffle_states_contents);

                void processSingleIntraShardTx(shared_ptr<dev::eth::Transaction> tx, string & data_str);

                void batchRequestForMasterChange(vector<string>& requests);

                void processStateMasterRequestTx(shared_ptr<Transaction> tx);

                void processMasterChangePrePrepareTx(shared_ptr<Transaction> tx);

                void processMasterChangePrepareTx(shared_ptr<dev::eth::Transaction> tx);

                void processMasterChangeCommitTx(shared_ptr<Transaction> tx);

                void sendMasterChangePrePrepareMsg(shared_ptr<map<string, StateMasterChangePrePrepareMsgs::Ptr>> PrePareMsgs);

                void sendMasterChangePrepareMsg(string& sourceshardids, string& destinshardids, 
                                        string& readwritekey, string &requestmessageid, string &coordinatorshardid);

                void sendMasterChangeCommitMsg(shared_ptr<map<string, StateMasterChangeCommitMsgs::Ptr>> CommitMsgs);

                void responseCommitToCoordinator(string& _crossshardtxid, std::string& _intershardTxids);

                void sendBatchDistributedTxMsg(int coordinator_shardid, int epochId, int destshardId, string& subTxs, string& intrashardTxs, string& _shuffle_states_contents);

                void sendReadWriteSet(string& rwKeysToTransfer, string& rwValuesToTransfer, string& rwContentionRateToTransfer, string& participantIds);

                bool checkReadWriteKeyReady(vector<string>& rwkeyToReceive, bool lastTxInBatch);

                void executeTx(shared_ptr<Transaction> tx, bool deploytx);

                void executeDeployContractTx(shared_ptr<dev::eth::Transaction> tx);

                void replyCommitToCoordinator();

                void processBlockingTxsPlus(); // 增加了状态迁移写的问题

                void analyseWorkload();

                void shuffleStateValue(string& shuffle_states_contents); // 向状态原分片发送当前状态的最新值

                void shuffleBlockedTxs(string& shuffle_states_contents);

                void processShuffleValueTx(shared_ptr<Transaction> tx);

                void processShuffleTx(shared_ptr<Transaction> tx);

                void setAttribute(shared_ptr<dev::blockchain::BlockChainInterface> _blockchainManager);

                // 计算交易平均延时
                void average_latency();

                // 线程安全访问 m_processed_intrashardTxNum
                void add_processedTxNum(int number){
                    lock_guard<mutex> lock(processedTxNum_lock);
                    // m_processed_intrashardTxNum = m_processed_intrashardTxNum + number;
                    // cout << "add processed Txnum: " << number << endl;
                    m_processed_intrashardTxNum += number;
                    // PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数test_commit_number为", m_processed_intrashardTxNum);
                    // if (std::chrono::duration<double>(std::chrono::steady_clock::now() - m_lastLogTime).count() >= 5) {
                    //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数test_commit_number为", m_processed_intrashardTxNum);
                    //     m_lastLogTime = std::chrono::steady_clock::now();
                    // }
                    // for(int i = 0; i < number; i++){
                    //     m_processed_intrashardTxNum++;
                    //     if(m_processed_intrashardTxNum % 100 == 0){
                    //         PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", m_processed_intrashardTxNum);
                    //     }
                    // }
                    
                }
                void log_commit_TxNum() {
                    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - m_lastLogTime).count() >= 1 && !m_commit_finished_flag) {
                        if (m_processed_intrashardTxNum == dev::plugin::total_injectNum) {
                            PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数test_commit_number", m_processed_intrashardTxNum);
                            // m_lastLogTime = std::chrono::steady_clock::now();
                            cout << "目前已提交的交易总数test_commit_number=" << m_processed_intrashardTxNum << endl;
                            cout << "commit finished..." << endl;
                            m_commit_finished_flag = true;
                            return;
                        }
                        PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数test_commit_number", m_processed_intrashardTxNum);
                        cout << "目前已提交的交易总数test_commit_number=" << m_processed_intrashardTxNum << endl;
                        m_lastLogTime = std::chrono::steady_clock::now();
                    }
                }
                void add_processedTxNum(){
                    lock_guard<mutex> lock(processedTxNum_lock);
                    m_processed_intrashardTxNum++;
                    if(m_processed_intrashardTxNum % 1000 == 0){
                        PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", m_processed_intrashardTxNum);
                    }
                }

                int get_processedTxNum(){
                    lock_guard<mutex> lock(processedTxNum_lock);
                    return m_processed_intrashardTxNum;
                }

                // 线程安全访问 m_committed_intershardTxid, 将intershardTxid 插入到 coordinator 所属的容器中
                void add_intershardTxid(int coordinator, string intershardTxid){
                    lock_guard<mutex> lock(committed_intershardTxid_lock);
                    if(m_committed_intershardTxid->count(coordinator) != 0){
                        auto intershardTxids = m_committed_intershardTxid->at(coordinator);
                        intershardTxids->push_back(intershardTxid);
                    }
                    else{
                        auto intershardTxids = make_shared<vector<string>>();
                        intershardTxids->push_back(intershardTxid);
                        m_committed_intershardTxid->insert(make_pair(coordinator, intershardTxids));
                    }
                }

                // 获取当前积攒的所有 intershardTxids
                void get_committed_intershardTxid(shared_ptr<map<int, shared_ptr<vector<int>>>> committed_intershardTxid){
                    lock_guard<mutex> lock(committed_intershardTxid_lock);
                    m_committed_intershardTxid->clear();
                }

                // 判断当前 m_committed_intershardTxid 是否为空
                bool isEmpty_committed_intershardTxid(){
                    lock_guard<mutex> lock(committed_intershardTxid_lock);
                    if(m_committed_intershardTxid->size() == 0){
                        return true;
                    }
                    else{
                        return false;
                    }
                }

                // 清空 m_committed_intershardTxid
                void clear_committed_intershardTxid(){
                    lock_guard<mutex> lock(committed_intershardTxid_lock);
                    m_committed_intershardTxid->clear();
                }

                // 线程安全访问 other_blocked_states
                void add_otherState(string rwkey){
                    lock_guard<mutex> lock(other_states_lock);
                    if(other_blocked_states.count(rwkey) == 0){
                        other_blocked_states.insert(make_pair(rwkey, 1));
                    }
                    else{
                        int number = other_blocked_states.at(rwkey);
                        other_blocked_states.at(rwkey) = number + 1;
                    }
                }

                // 判断key在other_blocked_states中是否存在
                int count_otherState(string rwkey){
                    lock_guard<mutex> lock(other_states_lock);
                    if(other_blocked_states.count(rwkey) == 0){
                        return 0;
                    }
                    else{
                        int number = other_blocked_states.at(rwkey);
                        return number;
                    }
                }

                // 从other_blocked_states中删除rwkey
                void delete_otherState(string rwkey){
                    lock_guard<mutex> lock(other_states_lock);
                    if(other_blocked_states.at(rwkey) != 0){
                        int number = other_blocked_states.at(rwkey);
                        other_blocked_states.at(rwkey) = number - 1;
                    }
                }

                // 线程安全访问 m_blocked_intraTxContents
                void add_blocked_intraTxContent(string& convertedIntraTxContents){
                    lock_guard<mutex> lock(blocked_intraTxContents_lock);
                    m_blocked_intraTxContents->push(convertedIntraTxContents);
                }

                int size_blocked_intraTxContent(){
                    lock_guard<mutex> lock(blocked_intraTxContents_lock);
                    int number = m_blocked_intraTxContents->size();
                    return number;
                }

                string front_blocked_intraTxContent(){
                    lock_guard<mutex> lock(blocked_intraTxContents_lock);
                    string convertedIntraTxContents = m_blocked_intraTxContents->front();
                    return convertedIntraTxContents;
                }

                void pop_blocked_intraTxContent(){
                    lock_guard<mutex> lock(blocked_intraTxContents_lock);
                    m_blocked_intraTxContents->pop();
                }

                // 线程安全访问 m_blocked_originTxRlps(存放多点写时被阻塞的片内交易)
                void add_blocked_originTxRlps(string& originTxRlps){
                    lock_guard<mutex> lock(blocked_originTxRlps_lock);
                    m_blocked_originTxRlps->push(originTxRlps);
                }

                int size_blocked_originTxRlps(){
                    lock_guard<mutex> lock(blocked_originTxRlps_lock);
                    return m_blocked_originTxRlps->size();
                }

                string front_blocked_originTxRlps(){
                    lock_guard<mutex> lock(blocked_originTxRlps_lock);
                    string originTxRlps = m_blocked_originTxRlps->front();
                    return originTxRlps;
                }

                void pop_blocked_originTxRlps(){
                    lock_guard<mutex> lock(blocked_originTxRlps_lock);
                    m_blocked_originTxRlps->pop();
                }

                // 并发安全访问 m_shuffleValue_destshardid 和 shuffleTxRLP_destshardid(暂不考虑状态写权限撤回功能的实现，安排在后期工作)      
                void add_shuffleTxRLP_destshardid(string& statekey, int destshardid){
                    lock_guard<mutex> lock(txRLP_destshardid_lock);
                    m_shuffleTxRLP_destshardid->insert(make_pair(statekey, destshardid));
                }

                void get_shuffleTxRLP_destshardid(map<string, int>* destinshardids){
                    lock_guard<mutex> lock(txRLP_destshardid_lock);
                    for(auto it = m_shuffleTxRLP_destshardid->begin(); it != m_shuffleTxRLP_destshardid->end(); it++){
                        string statekey = it->first;
                        int shardid = it->second;
                        destinshardids->insert(make_pair(statekey, shardid));
                    }
                }

                void add_shuffleValue_destshardid(string& statekey, int destshardid){
                    lock_guard<mutex> lock(value_destshardid_lock);
                    m_shuffleValue_destshardid->insert(make_pair(statekey, destshardid));
                }

                void get_shuffleValue_destshardid(map<string, int>* destinshardids){
                    lock_guard<mutex> lock(value_destshardid_lock);
                    for(auto it = m_shuffleValue_destshardid->begin(); it != m_shuffleValue_destshardid->end(); it++){
                        string statekey = it->first;
                        int shardid = it->second;
                        destinshardids->insert(make_pair(statekey, shardid));                        
                    }
                }

                // void calculateTPS();

                // void executeTx(shared_ptr<Transaction> tx);

                // void processBlockingTxs();

                // void processCombinedIntraShardTx(shared_ptr<dev::eth::Transaction> tx);

                // void processBatchSubInterShardTxs(string& data_str);

                // bool checkReadWriteKeyReady(vector<string>& rwkeyToReceive);

                // shared_ptr<dev::executive::Executive> getExecutive() { return exec_intra; }

                // void executeTx(shared_ptr<Transaction> tx, bool intraTx);

                // void requestForMasterChange(string& ancestorGroupId, string& sourceShardId, 
                //                                 string& destinShardId, string& readwritekey, string& crossshardtxid);

                // void processblocked_intraTxContent();

                // void processvice_blockedTxs();

                // void insertInterShardTxBatchInfo(shared_ptr<dev::eth::Transaction> tx, string& corssshardTxId, int messageid)
                // {
                //     lock_guard<mutex> lock(batchLock);
                //     txHash2InterShardTxId.insert(make_pair(tx->hash(), corssshardTxId));
                //     txHash2BatchId.insert(make_pair(tx->hash(), messageid)); // 记录当前交易hash所对应的batchid
                // }

                // int getBatchIdByTransactionHash(h256 tx_hash)
                // {
                //     lock_guard<mutex> lock(batchLock);
                //     int batchId = txHash2BatchId.at(tx_hash);
                //     return batchId;
                // }

                // int txHash2batchId_count(h256 tx_hash)
                // {
                //     lock_guard<mutex> lock(batchLock);
                //     int number = txHash2BatchId.count(tx_hash);
                //     return number;
                // }

                // string txHash2InterShardTxId_at(h256 tx_hash)
                // {
                //     lock_guard<mutex> lock(batchLock);
                //     return txHash2InterShardTxId.at(tx_hash);
                // }

                // void processSubInterShardTransaction(shared_ptr<dev::eth::Transaction> tx);

                // int getStateContention(string& rwkey);

                // int getRemoteStateAccessNum(string& rwkey);

                // void compareStateContention(string& crossshardtxid, std::vector<std::string>& participantItems, 
                //                         vector<string>& rwKeyItems, int itemIndex);

                // void processStateMasterRequestTx(shared_ptr<Transaction> tx);

                // void insertInterShardTxReadWriteSetInfo(string &_crossshardtxid, int _participantNum);

                // void processAccumulatedSubTxs();

                // void saveKeyRemoteAccessedNumber(shared_ptr<transactionWithReadWriteSet> tx);

                // void savelocalAccessedNumber(shared_ptr<Transaction> tx);

                // void savelocalAccessedNumber(shared_ptr<transactionWithReadWriteSet> tx);

                // double average_latency(){
                //     int totalTime = 0;
                //     int totalTxNum = 0;

                //     // 计算交易平均延时
                //     for(auto iter = m_txid_to_endtime->begin(); iter != m_txid_to_endtime->end(); iter++){
                //         string txid = iter->first;
                //         int endtime = iter->second;
                //         int starttime = 0;
                //         if(m_txid_to_starttime->count(txid) != 0){
                //             starttime = m_txid_to_starttime->at(txid);
                //             int interval = endtime - starttime;
                //             totalTime = totalTime + interval;
                //             totalTxNum++;
                //         }
                //     }
                //     double avgLatency = double(totalTime) / double(totalTxNum);
                //     PLUGIN_LOG(INFO) << LOG_KV("total_txNum", totalTxNum)
                //                      << LOG_KV("total_time", totalTime)
                //                      << LOG_KV("avg_latency", avgLatency);
                //     return avgLatency;
                // }

            public:



                shared_ptr<dev::executive::Executive> exec;
                shared_ptr<dev::eth::EVMInterface> vm;

                shared_ptr<P2PInterface> m_p2p_service;
                shared_ptr<P2PInterface> m_group_p2p_service;
                shared_ptr<dev::blockchain::BlockChainInterface> m_blockchainManager;
                shared_ptr<map<int, shared_ptr<cachedSubTxsContainer>>> m_subTxsContainers;

                // 状态转移使用到的一些变量(需要设置线程安全，稍后处理)
                bool masterwriteBlocked = false;
                bool vicewriteBlocked = false;

                int stateChangeRequestId = 0; // 记录当前分片发出的状态转移请求Id
                int stateChangeRequestBatchId = 0; // 记录当前分片发出的状态转移请求Id

                map<string, shared_ptr<vector<string>>> states_need_shuffle; // 一段时间内积累的需要shuffle状态和交易的分片，key 为 shardid，value 需要shuffle的key
                map<string, string> transfered_states; // 已经被转移的状态, (state --> shardid)
                map<string, int> other_blocked_states; // 被附加阻塞的状态
                map<string, string> m_addedTransferedStates; // 新增的临时来源于其他分片的状态(state --> shardid)
                BlockingQueue<vector<string>> blockedTxs; // 因状态被迁移而被阻塞的交易Rlp: vector<txrlp, shardid, shardid...>

                shared_ptr<dev::rpc::Rpc> m_rpc_service;

                set<string> requestedStates; // 已经请求过转移的状态

                // shared_ptr<set<int>> m_shuffleValue_destshardid;
                // shared_ptr<set<int>> m_shuffleTxRLP_destshardid;

                shared_ptr<map<string, int>> m_shuffleValue_destshardid;
                shared_ptr<map<string, int>> m_shuffleTxRLP_destshardid;

                shared_ptr<queue<string>> m_blocked_intraTxContents; // 被临时阻塞的重组片内交易，里面的每一个元素代表了一个Batch内的所有重组片内交易
                shared_ptr<queue<string>> m_blocked_originTxRlps; // 副写分片被阻塞的原始片内交易

                shared_ptr<map<int, shared_ptr<BlockingTxQueue>>> m_blocked_originTxRlps_Queues; // 副写分片阻塞的即将发送给主写分片的交易  shardid --> blocktxQueue

                shared_ptr<Transaction> simulatedTx;
                shared_ptr<Hierarchical_Structure> m_hierarchical_structure; // 层级化结构
                shared_ptr<LRU_StatesInfo> m_LRU_StatesInfo;            // 近期访问的key信息
                shared_ptr<ReadWriteSetManager> m_readWriteSetManager;  // 读写集信息
                shared_ptr<map<string, shared_ptr<BlockingTxQueue>>> m_blockingTxQueues; // 存放所有上层排序后的跨片交易
                shared_ptr<map<string, shared_ptr<queue<transactionWithReadWriteSet::Ptr>>>> m_txQueues; // 存放从线程安全队列取出的所有交易
                shared_ptr<map<int, shared_ptr<vector<string>>>> m_committed_intershardTxid;
                shared_ptr<map<string, bool>> m_valid_state;
                shared_ptr<map<string, bool>> m_master_state;
                shared_ptr<map<string, shared_ptr<vector<string>>>> m_stateTransfer_Rules;

                shared_ptr<map<string, shared_ptr<map<int, string>>>> m_cachedBatchTxs;
                set<h256> m_sendedrwsetTx;
                map<int, int> maxEpochIds;
                map<string, int> remoteReadWriteSetNum; // 跨片交易应当收到的其他参与方读写集个数

                string m_upper_shardids;
                string m_lower_shardids;
                vector<int> upper_groupids;
                vector<int> lower_groupids;
                atomic<int> m_processed_intrashardTxNum; // 统计处理的交易数目，留作计算TPS和延时
                atomic<int> m_processed_intershardTxNum; // 统计处理的交易数目，留作计算TPS和延时
                map<string, int> m_precessedEpochIds;
            
                PROTOCOL_ID m_group_protocolID;
                PROTOCOL_ID m_protocolID;

                mutex batchLock;
                mutex executeLock;
                mutex other_states_lock;
                mutex blocked_intraTxContents_lock;
                mutex blocked_originTxRlps_lock;
                mutex value_destshardid_lock;
                mutex txRLP_destshardid_lock;
                mutex committed_intershardTxid_lock;
                mutex processedTxNum_lock;
                std::chrono::steady_clock::time_point m_lastLogTime; // 最后一次输出日志的时间, modify by ZhmYe 
                bool m_commit_finished_flag;
                // set<string> m_multiwriteStateKey; // 记录当前多点写的key
                // int m_precessedEpochId = 0; // 收到的上层排序后交易包Id，确保按顺序处理

                // map<int, string> cachedBatchTxs;
                // shared_ptr<map<int, int>> m_intershardTxids; // intershardTx --> participantNum
                // int testNumber = 0;
                // shared_ptr<BlockingTxQueue> m_blocking_intrashardTxQueue;  // 存放当前所有子队列阻塞的状态以及被阻塞的片内交易(包括重组片内交易)
                // shared_ptr<dev::executive::Executive> exec_intra;
                // shared_ptr<dev::eth::EVMInterface> vm_intra;
                // shared_ptr<dev::executive::Executive> exec_inter;
                // shared_ptr<dev::eth::EVMInterface> vm_inter;
                // int intrashardtx_num = 0; // 已经处理的片内交易数目
                // int intershardtx_num = 0; // 已经处理的跨片交易数目
                // map<string, int> crossshardtxid2sourceshardid;
        };
    }
}