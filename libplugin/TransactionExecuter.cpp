#include "Common.h"
#include <libplugin/TransactionExecuter.h>
#include<libplugin/Benchmark.h>
using namespace dev;
using namespace std;
using namespace dev::eth;
using namespace dev::p2p;
using namespace dev::plugin;

namespace dev
{
namespace eth
{
class Transaction;
}
}

namespace dev
{
namespace plugin {

/**
 * NOTES: 读取当前分片最近上层分片和所有下层分片信息
 * */
void TransactionExecuter::init_upperlower_shardids(string& upper_shardids, string& lower_shardids){

    PLUGIN_LOG(INFO) << LOG_DESC("init_upperlower_shardids")
                     << LOG_KV("upper_shardids", upper_shardids);

    if(upper_shardids != "N/A") {
        vector<string> items;
        boost::split(items, upper_shardids, boost::is_any_of(","), boost::token_compress_on);
        int item_size = items.size();
        for(int i = 0; i < item_size; i++){
            upper_groupids.push_back(atoi(items.at(i).c_str()));
        }
    }

    if(lower_shardids != "N/A"){
        vector<string> items;
        boost::split(items, lower_shardids, boost::is_any_of(","), boost::token_compress_on);
        int item_size = items.size();
        for(int i = 0; i < item_size; i++) {
            lower_groupids.push_back(atoi(items.at(i).c_str()));
        }
        lower_groupids.push_back(dev::consensus::internal_groupId); // 增加发向自己分片
    }
}

/** 
 * NOTES: 初始化 map<string, int> m_precessedEpochIds, shardid --> precessedEpochId
 * */
void TransactionExecuter::init_processedEpochIds(string& upper_shardids){
    vector<string> items;
    boost::split(items, upper_shardids, boost::is_any_of(","), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中
    int item_size = items.size();
    for (int i = 0; i < item_size; i++) {
        string shardid = items.at(i);
        m_precessedEpochIds.insert(make_pair(shardid, 0));
    }
}


/** 
 * NOTES: 初始化 map<string, shared_ptr<BlockingTxQueue>> m_blockingTxQueues
 * */
void TransactionExecuter::init_blockingTxQueues(string& upper_shardids){
    vector<string> items;
    boost::split(items, upper_shardids, boost::is_any_of(","), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中
    int item_size = items.size();
    for(int i = 0; i < item_size; i++){
        string shardid = items.at(i);
        auto blockingTxQueue = make_shared<BlockingTxQueue>();
        m_blockingTxQueues->insert(make_pair(shardid, blockingTxQueue));
        auto txQueue = make_shared<queue<transactionWithReadWriteSet::Ptr>>();
        m_txQueues->insert(make_pair(shardid, txQueue));
    }
}

/**
 * NOTES: 初始化缓存每个epoch子交易的队列, 需要维护所有的下层容器
 * */
void TransactionExecuter::init_subTxsContainers() 
{
    int group_size = lower_groupids.size();
    if(group_size != 0) { // 非叶子分片
        for(int i = 0; i < group_size; i++) { // 初始化 maxEpochIds
            int groupid = lower_groupids.at(i);
            int epochId = 1; // 赋予每个子分片跨片子交易的初始epochId
            maxEpochIds.insert(make_pair(groupid, epochId));
        }

        for(int i = 0; i < group_size; i++){ // 初始化 subTxBatches
            int txNum = 0;
            int groupid = lower_groupids.at(i); // 注: lower_groupids中包括的本分片的id
            // PLUGIN_LOG(INFO) << LOG_KV("init groupid", groupid);
            int epochId = maxEpochIds.at(groupid);
            shared_ptr<string> subtxs = make_shared<string>("");
            shared_ptr<string> intrashardtxs = make_shared<string>("");
            auto subTxsContainer = make_shared<cachedSubTxsContainer>(txNum, epochId, groupid, subtxs, intrashardtxs);
            m_subTxsContainers->insert(make_pair(groupid, subTxsContainer));
        }
    }
}

/** 
 * NOTES:读取模拟交易
 * */
void TransactionExecuter::init_simulateTx(){
    Json::Reader reader;
    Json::Value root;
    ifstream infile("simulateTx.json", ios::binary); // 读取文件 simulateTx.json
    if(reader.parse(infile, root)) {
        string txrlp = root[0].asString();
        // PLUGIN_LOG(INFO) << LOG_KV("simulateTx RLP", txrlp);
        Transaction::Ptr tx = std::make_shared<Transaction>(jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
        simulatedTx = tx;
    }
}

/** 
 * NOTES:作为副写分片阻塞的交易队列
 * */
void TransactionExecuter::init_blocked_originTxRlps_Queues(){

    // 为除了本分片以外的其他分片创建一个多写功能的交易阻塞队列
    for(int i = 1; i <= dev::consensus::hiera_shard_number; i++){
        if(i != dev::consensus::internal_groupId){
            auto blocked_originTxRlps_Queue = shared_ptr<BlockingTxQueue>();
            m_blocked_originTxRlps_Queues->insert(make_pair(i, blocked_originTxRlps_Queue));
        }
    }
}

/** 
 * NOTES: 部署模拟执行需要的合约状态
 * */
void TransactionExecuter::load_statekeys_distribution(){

    vector<string> valid_state; // 划分在本分片中的状态
    vector<string> invalid_state; // 非划分在本分片中的状态

    ifstream fin;
    fin.open("statekeys_distribution.ini");
	if(!fin.is_open()){
        PLUGIN_LOG(INFO) << LOG_DESC("statekeys_distribution.ini文件不存在");
	}
    else{
        string buff;
        while (getline(fin, buff)){
            PLUGIN_LOG(INFO) << LOG_KV("buff", buff);
            vector<string> items;
            boost::split(items, buff, boost::is_any_of(","), boost::token_compress_on);
            string state = items.at(0);
            string shardid = items.at(1);

            if(shardid == to_string(dev::consensus::internal_groupId)){
                valid_state.push_back(state);
            }
            else{
                invalid_state.push_back(state);
            }
		}
    }

    // 使用 valid_state 更新 m_valid_state 和 m_master_state
    int state_size = valid_state.size();
    for(int i = 0; i < state_size; i++){
        string state = valid_state.at(i);
        m_valid_state->insert(make_pair(state, true)); // 初始化分的状态有效且为主
        m_master_state->insert(make_pair(state, true));
    }

    // 使用 invalid_state 更新 m_valid_state 和 m_master_state
    state_size = invalid_state.size();
    for(int i = 0; i < state_size; i++){
        string state = invalid_state.at(i);
        m_valid_state->insert(make_pair(state, false)); // 初始化分的状态无效且为不为主
        m_master_state->insert(make_pair(state, false));
    }

    fin.close();
}

/** 
 * NOTES: 读取写权限转移的规则
 * */
void TransactionExecuter::load_state_transfer_rule()
{
    ifstream fin;
    fin.open("state_transfer_rule.ini");
	if(!fin.is_open()){
        PLUGIN_LOG(INFO) << LOG_DESC("state_transfer_rule.ini文件不存在");
	}
    else{

        string buff;
        while (getline(fin, buff)){
            // PLUGIN_LOG(INFO) << LOG_KV("buff", buff);
            vector<string> items;
            boost::split(items, buff, boost::is_any_of(","), boost::token_compress_on);
            string coordinator_shardid = items.at(0);
            string sourceshard_id = items.at(1);
            string destinshard_id = items.at(2);
            string statekey = items.at(3);

            string flag = "|";
            string transfer_rule = sourceshard_id + flag + destinshard_id + flag + statekey;
            if(m_stateTransfer_Rules->count(coordinator_shardid) != 0){
                m_stateTransfer_Rules->at(coordinator_shardid)->push_back(transfer_rule);
            }
            else{
                auto rules = make_shared<vector<string>>();
                rules->push_back(transfer_rule);
                m_stateTransfer_Rules->insert(make_pair(coordinator_shardid, rules));
            }
		}
    }
    fin.close();
}

/** 
 * NOTES: 手动模拟状态写权限转移后的情况
 * */
void TransactionExecuter::state_right_transfer_simulation()
{
    // 模拟的状态划分，key为公共公共祖先分片id，value(vector<int>())中存状态转移规则(sourceshardid_destinshardid_statekey), 状态从sourceshardid分片转移到destinshardid分片
    for(auto it = m_stateTransfer_Rules->begin(); it != m_stateTransfer_Rules->end(); it++){

        string coordinator_shardid = it->first;
        auto rules = it->second; // vector<string> "sourceshardid_destinshardid_statekey"

        for(const std::string& s:*rules){
            vector<std::string> items;
            boost::split(items, s, boost::is_any_of("_"), boost::token_compress_on);
            string sourceshardid = items.at(0);
            string destinshardid = items.at(1);
            string transfered_statekey = items.at(2);

            // 修改协调者分片的 m_masterChangedKey
            if(dev::consensus::internal_groupId == atoi(coordinator_shardid.c_str())){
                dev::plugin::m_masterChangedKey->insert(make_pair(transfered_statekey, 1));
            }

            // 修改相应参与者中的 m_shuffleValue_destshardid, m_shuffleTxRLP_destshardid、m_valid_state、m_master_state、m_addedTransferedStates、transfered_states
            // 修改destinshardid分片中的信息
            else if(dev::consensus::internal_groupId == atoi(destinshardid.c_str())){
                m_valid_state->insert(make_pair(transfered_statekey, 1));
                m_master_state->insert(make_pair(transfered_statekey, 1));
                m_addedTransferedStates.insert(make_pair(transfered_statekey, sourceshardid));
                m_shuffleValue_destshardid->insert(make_pair(transfered_statekey, atoi(sourceshardid.c_str())));
            }
            // 修改sourceshardid分片中的信息
            else if(dev::consensus::internal_groupId == atoi(sourceshardid.c_str())){
                m_valid_state->insert(make_pair(transfered_statekey, 1));
                m_master_state->insert(make_pair(transfered_statekey, 0));
                transfered_states.insert(make_pair(transfered_statekey, "1"));
                m_shuffleTxRLP_destshardid->insert(make_pair(transfered_statekey, atoi(destinshardid.c_str())));
            }
        }
    }
}

/**
 * NOTES: 获取指定状态key当前的最新值
 * */
string TransactionExecuter::getValueByStateAddress(string& rwkey)
{
    string rwKeyValue = "";
    try {
        // 向其他参与者分片所有节点广播本地的读写集消息
        // auto vm = dev::plugin::executiveContext->getExecutiveInstance();
        // exec->setVM(vm);

        /* 先模拟取状态最新值
        Address addr = Address(rwkey);
        u256 key = u256(0);
        evmc_address code_addr = toEvmC(addr);
        evmc_bytes32 evm_key = toEvmC(key);

        auto m_ext = exec->getExt();
        while(m_ext == nullptr) {
            PLUGIN_LOG(INFO) << LOG_DESC("m_ext为空指针, 无法执行...");
            this_thread::sleep_for(chrono::milliseconds(1000)); // 1s重试
        }

        auto host_interface = m_ext->interface;
        evmc_host_context* common_ext = m_ext.get();
        auto value = host_interface->get_storage(common_ext, &code_addr, &evm_key);
        // PLUGIN_LOG(INFO) << LOG_KV("The latested state value is", toString(fromEvmC(value)));
        rwKeyValue = toString(fromEvmC(value));
        */

        rwKeyValue = "1"; // 假设状态最新值为1
        // dev::plugin::executiveContext->m_vminstance_pool.push(vm);
    }
    catch(const std::exception& e) {
        cerr << e.what() << '\n';
        PLUGIN_LOG(INFO) << LOG_KV("e.what()", e.what());
    }
    return rwKeyValue;
}

/**
 * NOTES:逐个处理共识结束后区块中的交易11111
 * */
void TransactionExecuter::processConsensusBlock(){
    int blockid = 0;
    int currentblockid = 0;
    // int lastId = 0;
    int transactions_size = 0;
    int scannedTxNum = 0;
    // std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
    shared_ptr<Block> currentBlock;
    while(true){
        currentblockid = m_blockchainManager->number(); // 当前块高
        if(currentblockid > blockid) {
            blockid++;
            currentBlock = m_blockchainManager->getBlockByNumber(blockid);
            transactions_size = currentBlock->getTransactionSize();
            scannedTxNum += transactions_size;
            PLUGIN_LOG(INFO) << LOG_KV("当前区块大小", transactions_size)
                             << LOG_KV("已共识结束的交易总数", scannedTxNum);  // (scannedTxNum数目可能小于实际导入的交易数目，共识失败？)
            auto transactions = currentBlock->transactions(); // 逐个处理交易
            for(int i = 0; i < transactions_size; i++) {
                auto tx = transactions->at(i);
                string data_str = dataToHexString(tx->get_data());
                switch (checkTransactionType(data_str)){
                // 判断交易类型
                case InterShard:   // 协调者分片(公共祖先)处理原始跨片交易
                    processInterShardTx(data_str);
                    break;
                case BatchedSubInterShard: // 一批跨片子交易(内可能包含由跨片交易转变而来的片内交易)
                    processOrderedBatchedSubInterShardTxs(data_str);
                    break;
                case SingleIntraShard:  // 原始片内交易(重组片内交易不在这里处理，在processOrderedBatchedSubInterShardTxs中处理了)
                    processSingleIntraShardTx(tx, data_str);
                    break;
                case DeployContract:    // 部署合约交易
                    executeDeployContractTx(tx);
                    break;
                case MasterChangeRequest: // 权限转移请求交易
                    processStateMasterRequestTx(tx);
                    break;
                case MasterChangePrePrepare: // 权限转移PrePrepare交易
                    processMasterChangePrePrepareTx(tx);
                    break;
                case MasterChangePrepare: // 权限转移Prepare交易
                    processMasterChangePrepareTx(tx);
                    break;
                case MasterChangeCommit: // 权限转移Commit交易
                    processMasterChangeCommitTx(tx);
                    break;
                case ShuffleValue:  // 主分片状态分享交易
                    processShuffleValueTx(tx);
                    break;
                case ShuffleBlockedTx:  // 副分片交易分享交易
                    processShuffleTx(tx);
                    break;
                default:
                    break;
                }
            }
            if (dev::plugin::cross_rate == 20) {
                if (blockid % 10 == 0) {
                    checkSubTxsContainers();
                }
            } else {
                checkSubTxsContainers();
            }
        }
        else {
            // if (blockid != lastId) {
            //     // 如果3s内不变，就把积攒的发了
            //     if (chrono::duration_cast<chrono::milliseconds>(std::chrono::duration<double>(std::chrono::steady_clock::now() - lastTime)).count() >= 300) {
            //         // checkSubTxsContainers();
            //         m_commit_finished_flag = true;
            //         // lastId = blockid;
            //         // lastTime = std::chrono::steady_clock::now(); // 重置时间
            //     }
            // }
            // else {
            //     lastTime = std::chrono::steady_clock::now(); // 重置时间
            // }
            this_thread::sleep_for(chrono::milliseconds(10));
        }
}}

/** 
 * NOTES: 协调者处理原始跨片交易(将跨片交易拆分成多笔片内子交易，缓存下来待发送给下层处理)
 * */
void TransactionExecuter::processInterShardTx(string& data_str){
    // 只需要转发节点处理，积攒后批量发送至参与者
    if(dev::plugin::hieraShardTree->is_forward_node(dev::consensus::internal_groupId, nodeIdHex)) {
        accumulateSubTxs(data_str); // 积攒跨片子交易
    }
}

/**
 * NOTES: 根据交易中指定的跨片交易参与方，以及当前协调者中记录的状态最新状态迁移情况，将子交易发送至正确的分片(因为采用确定性事务机制，因此所有的跨片子交易需要发送给所有参与者)
*/
void TransactionExecuter::accumulateSubTxs(string& data_str){
    // PLUGIN_LOG(INFO) << LOG_DESC("开始积攒跨片子交易") << LOG_KV("data_str", data_str);
    // 1. 积攒跨片交易的所有子交易及其最新的目标分片
    // 2. 若所有的子交易的目标分片都是一样的，那么表示交易已经转成了片内交易，将其封装成一个片内交易，否则将所有的子交易打包成一笔交易发送给所有的参与者

    // 积攒所有子分片以及其当前的目标分片等信息
    vector<std::string> dataitems;
    boost::split(dataitems, data_str, boost::is_any_of("|"), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中
    int participantNum = (dataitems.size() - 1) / 3;
    string intershardTxid = dataitems.at(1);

    int participantIndex = 2;
    int txRlpIndex = 3;
    int rwkeysIndex = 4;

    // 确定最新的参与者分片
    bool multi_write = false;
    vector<string> multi_write_statekeys;

    vector<string> origional_participant_shardids;
    vector<string> current_participant_shardids;
    for(int j = 0; j < participantNum; j++){
        string original_destinshardid = dataitems.at(participantIndex); // 原来参与者分片id
        string current_destinshardid = original_destinshardid;

        // 检查key当前的主分片是否发生变化
        string rwkeys = dataitems.at(rwkeysIndex).c_str(); // rwkeys
        vector<string> rwkeyitems;
        boost::split(rwkeyitems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);
        string rwkey = rwkeyitems.at(j).c_str(); // rwkey

        if(dev::plugin::m_masterChangedKey->count(rwkey) != 0) {
            current_destinshardid = to_string(dev::plugin::m_masterChangedKey->at(rwkey)); // 子交易的最新目标分片Id
            multi_write = true; // 若状态转移，则需要多地写
            multi_write_statekeys.push_back(rwkey);
        }

        origional_participant_shardids.push_back(original_destinshardid); // 原始所有参与者的分片id(在状态没有迁移前)
        vector<string>::iterator result = find(current_participant_shardids.begin(), current_participant_shardids.end(), current_destinshardid); // 查找current_destinshardid
        if(result == current_participant_shardids.end()){ // 没找到
            current_participant_shardids.push_back(current_destinshardid);
        }
        rwkeysIndex+=3;
        participantIndex+=3;
    }

    // 本分片涉及到了多点写状态
    // 消息随着一批交易同时发出去，收到消息的分片处理完交易后，开始shuffle里面的状态
    if(multi_write == true){
        int multi_write_statekeys_size = multi_write_statekeys.size();
        int origional_participant_size = origional_participant_shardids.size();

        for(int i = 0; i < multi_write_statekeys_size; i++){
            string rwkey = multi_write_statekeys.at(i);
            for(int j = 0; j < origional_participant_size; j++){
                string shardid = origional_participant_shardids.at(j);
                if(states_need_shuffle.count(shardid) == 0){
                    auto states = make_shared<vector<string>>();
                    states->push_back(rwkey);
                    states_need_shuffle.insert(make_pair(shardid, states));
                }
                else{
                    auto states = states_need_shuffle.at(shardid);
                    states->push_back(rwkey);
                }
            }
        }
    }

    // 保存该笔跨片交易中的所有跨片子交易
    string flag = "_";
    string txRlps = "";
    string participant_shardids = "";
    string rwkeys = dataitems.at(4).c_str();
    int current_participantNum = current_participant_shardids.size(); // 当前所有参与者分片数目
    auto subInterShardTxs = make_shared<vector<subInterShardTx>>();
    for(int j = 0; j < current_participantNum; j++){
        string shardid = current_participant_shardids.at(j);
        string txRlp = dataitems.at(txRlpIndex); // txrlp

        // 更新 participant_shardids
        if(j == 0){
            participant_shardids = shardid;
            txRlps = txRlp;
        }
        else{
            participant_shardids = participant_shardids + flag + shardid;
            txRlps = txRlps + flag + txRlp;
        }
        txRlpIndex+=3;
    }

    // // 打印刚刚积攒的跨片子交易
    // PLUGIN_LOG(INFO) << LOG_DESC("输出积攒的跨片子交易");
    // PLUGIN_LOG(INFO) << LOG_KV("participant_shardids", participant_shardids)
    //                  << LOG_KV("txRlps", txRlps)
    //                  << LOG_KV("rwkeys", rwkeys);

    // 处理刚刚积攒的一笔跨片交易




    // 若现在这笔跨片交易仍然涉及到多方
    if(current_participantNum > 1){
        string flag = "|";
        string subIntershardTxs = intershardTxid + flag + participant_shardids + flag + txRlps + flag + rwkeys;
        for(int i = 0; i < current_participantNum; i++){
            string shardid = current_participant_shardids.at(i);
            auto cachedSubTxsContainer = m_subTxsContainers->at(atoi(shardid.c_str()));
            cachedSubTxsContainer->insertSubTx(subIntershardTxs); // 保存交易，并发安全
        }
    }

    // 若现在这笔跨片交易只涉及到一方(转为片内交易了)
    else if(current_participantNum == 1){
        string flag = "|";
        string subIntershardTxs = intershardTxid + flag + participant_shardids + flag + txRlps + flag + rwkeys;
        for(int i = 0; i < current_participantNum; i++){
            string shardid = current_participant_shardids.at(i);
            auto cachedSubTxsContainer = m_subTxsContainers->at(atoi(shardid.c_str())); // 依然只需要发送给真正执行的分片，其他分片根据 states_need_shuffle 来进行shuffle
            cachedSubTxsContainer->insertSubTx(subIntershardTxs);
        }
    }
}

/**
 * NOTES: bytes类型转string
 * */
string TransactionExecuter::dataToHexString(bytes data){
    string res2 = "";
    string temp;
    stringstream ioss;

    int count = 0;
    for(auto const &ele:data){
        count++;
        ioss << std::hex << ele;

        if(count > 30){
            ioss >> temp;
            res2 += temp;
            temp.clear();
            ioss.clear();
            count = 0;
        }
    }
    ioss >> temp;
    res2 += temp;
    
    return res2;
}

/**
 * NOTES: 检查交易类型
 * */
int TransactionExecuter::checkTransactionType(string& hex_m_data_str){
    int index = -1;
    if((index = hex_m_data_str.find("0x999000111", 0)) != -1){ // BatchDistributedTxMsg
        hex_m_data_str = hex_m_data_str.substr(index);
        return 1;
    }
    else if((index = hex_m_data_str.find("0x111222333", 0)) != -1){ // CrossShardTransaction
        hex_m_data_str = hex_m_data_str.substr(index);
        return 2;
    }
    else if(hex_m_data_str.find("0x777888999", 0) != -1){// 原始状态权限转移请求交易
        return 4;
    }
    else if(hex_m_data_str.find("0x000111222", 0) != -1){ // 状态权限转移PrePrepareMsg
        return 5; 
    }
    else if(hex_m_data_str.find("0x333444555", 0) != -1){ // 状态权限转移总PrepareMsg
        return 6;
    }
    else if(hex_m_data_str.find("0x666777888", 0) != -1){ // 状态权限转移Commit交易
        return 7;
    }
    else if((index = hex_m_data_str.find("0x444555666", 0)) != -1){ // 原始片内交易
        hex_m_data_str = hex_m_data_str.substr(index);
        return 8;
    }
    else if(hex_m_data_str.find("0x222333444", 0) != -1){ // 重组片内交易
        return 9;
    }
    else if(hex_m_data_str.find("0x000000001", 0) != -1){ // shuffleValueTx交易
        return 10;
    }
    else if(hex_m_data_str.find("0x000000002", 0) != -1){ // shuffleBlockedTx交易
        return 11;
    }
    else{
        return 0; // 部署合约交易
    }
}

/** 
 * NOTES: 检查是否有积攒的跨片子交易(或者重组片内交易)需要发送给下层分片
 * */
void TransactionExecuter::checkSubTxsContainers(){
    for(auto it = m_subTxsContainers->begin(); it != m_subTxsContainers->end(); it++) {
        auto cachedSubTxsContainer = it->second;
        int tx_size = cachedSubTxsContainer->getTxSize();

        PLUGIN_LOG(INFO) << LOG_KV("积攒的跨片交易数目tx_size", tx_size);

        if(tx_size > 0) {
            auto container = make_shared<dev::plugin::cachedSubTxsContainer>();
            cachedSubTxsContainer->getTxs(container);
            // PLUGIN_LOG(INFO) << LOG_DESC("上层分片即将转发本区块内的所有跨片子交易:")
            //                  << LOG_KV("testNumber", testNumber);
            //                  << LOG_KV("m_txNum", container->m_txNum)
            //                  << LOG_KV("m_epochId", container->m_epochId)
            //                  << LOG_KV("m_destinshardId", container->m_destinshardId)
            //                  << LOG_KV("m_sourceshardId", container->m_sourceshardId)
            //                  << LOG_KV("m_subtxs", *(container->m_subtxs))
            //                  << LOG_KV("m_intrashardtxs", *(container->m_intrashardtxs));

            // 准备即将要发送给下层的数据
            int coordinator_shardid = dev::consensus::internal_groupId; // 这一批跨片交易的排序者分片id
            int epochId = container->m_epochId;
            int destshardId = container->m_destinshardId;
            string subTxs = *(container->m_subtxs);
            string intrashardTxs = *(container->m_intrashardtxs);

            // 检查states_need_shuffle，判断每个分片是否有状态需要进行shuffle
            string flag = "|";
            string shuffle_states_contents = "";
            if(states_need_shuffle.count(to_string(coordinator_shardid)) != 0){
                auto shuffle_states_items = states_need_shuffle.at(to_string(coordinator_shardid));
                int shuffle_states_size = shuffle_states_items->size();
                for(int i = 0; i < shuffle_states_size; i++){
                    if(i == 0){
                        shuffle_states_contents = shuffle_states_items->at(i);
                    }
                    else{
                        string item = flag + shuffle_states_items->at(i);
                        shuffle_states_contents += item;
                    }
                }
            }

            // 向下层发送子交易和重组片内交易
            sendBatchDistributedTxMsg(coordinator_shardid, epochId, destshardId, subTxs, intrashardTxs, shuffle_states_contents);
            // 将container中的交易batch+1
            cachedSubTxsContainer->m_epochId++;
            PLUGIN_LOG(INFO) << LOG_DESC("公共祖先向下层分片发送批量跨片交易");
        }
    }
}


/** 
 * NOTES: 按照epoch顺序, 依次处理上层下发的跨片子交易(可能包括转化后的片内交易)
 * */
void TransactionExecuter::processOrderedBatchedSubInterShardTxs(string& data_str){

    // 获取 coordinatorId 和 epochId
    vector<string> items;
    boost::split(items, data_str, boost::is_any_of("|"), boost::token_compress_on);
    string coordinator_shardid = items.at(1);
    int epochId = atoi(items.at(2).c_str());
    // string coordinatorId_epochId = coordinator_shardid + to_string(epochId);

    PLUGIN_LOG(INFO) << LOG_KV("coordinator_shardid", coordinator_shardid);
    //                  << LOG_KV("epochId", epochId);

    // 按epochId顺序处理跨片子交易集
    int precessedEpochId = m_precessedEpochIds.at(coordinator_shardid); // 当前已经处理完的来自于coordinator_shardid的批量跨片交易
    if(epochId == precessedEpochId + 1) {

        PLUGIN_LOG(INFO) << LOG_DESC("000000");

        // // 获取实际data_str
        // string m_data_str = m_coordinator_epochId2data_str->at(coordinatorId_epochId);
        // PLUGIN_LOG(INFO) << LOG_KV("search coordinatorId_epochId", coordinatorId_epochId);

        vector<string> txcontents;
        boost::split(txcontents, data_str, boost::is_any_of("#"), boost::token_compress_on);

        PLUGIN_LOG(INFO) << LOG_DESC("aaaaaa");

        processBatchSubInterShardTxs(coordinator_shardid, &txcontents);
        m_precessedEpochIds.at(coordinator_shardid) = epochId;
        
        if(m_cachedBatchTxs->count(coordinator_shardid) != 0){ // 有阻塞来自coordinator_shardid的交易
            int attemptEpochId = epochId + 1;
            auto cachedBatchTxs = m_cachedBatchTxs->at(coordinator_shardid);
            while(cachedBatchTxs->count(attemptEpochId) != 0) { // 试探下一笔交易是否也接收到
                string data_str = cachedBatchTxs->at(attemptEpochId);
                boost::split(txcontents, data_str, boost::is_any_of("#"), boost::token_compress_on);
                PLUGIN_LOG(INFO) << LOG_DESC("bbbbbb");
                processBatchSubInterShardTxs(coordinator_shardid, &txcontents);
                m_precessedEpochIds.at(coordinator_shardid) = attemptEpochId;
                attemptEpochId++;
            }
        }
    }
    else{
        if(m_cachedBatchTxs->count(coordinator_shardid) != 0){
            m_cachedBatchTxs->at(coordinator_shardid)->insert(make_pair(epochId, data_str));
        }
        else{
            auto cachedBatchTxs = make_shared<map<int, string>>();
            cachedBatchTxs->insert(make_pair(epochId, data_str));
            m_cachedBatchTxs->insert(make_pair(coordinator_shardid, cachedBatchTxs));
        }
    }




    // // 获取真实的data
    // vector<string> items;
    // boost::split(items, data_str, boost::is_any_of("|"), boost::token_compress_on);
    // string coordinatorId = items.at(1);
    // string epochId_str = items.at(2);
    // string coordinatorId_epochId = coordinatorId + epochId_str;
    // PLUGIN_LOG(INFO) << LOG_KV("coordinatorId_epochId", coordinatorId_epochId);
    // string m_data_str = m_coordinator_epochId2data_str->at(coordinatorId_epochId);
    // // PLUGIN_LOG(INFO) << LOG_KV("m_data_str", m_data_str);


    // // 提取 coordinator_shardid, epochId 信息
    // vector<string> txcontents;
    // boost::split(txcontents, m_data_str, boost::is_any_of("#"), boost::token_compress_on);
    // string prefix = txcontents.at(0);

    // // PLUGIN_LOG(INFO) << LOG_KV("prefix", prefix);

    // vector<string> prefixItems;
    // boost::split(prefixItems, prefix, boost::is_any_of("|"), boost::token_compress_on);
    // string coordinator_shardid = prefixItems.at(1).c_str(); // 产生该排序后跨片交易的协调者分片id
    // int epochId = atoi(prefixItems.at(2).c_str()); // 该批交易的epochId

}

/**
 * NOTES: 处理上层发送下来一批子交易(包括跨片子交易、重组片内交易)
 * */
void TransactionExecuter::processBatchSubInterShardTxs(string coordinator_shardid, vector<string>* txcontents){

    PLUGIN_LOG(INFO) << LOG_DESC("开始处理共识完的上层批量跨片交易")
                     << LOG_KV("txcontents.size()", txcontents->size());

    // 提取data_str中的跨片子交易、重组片内交易、epochId字段等
    string prefix = txcontents->at(0);
    string subtxsContents = txcontents->at(1);
    string convertedIntraTxContents = txcontents->at(2); // 所有转化成的片内子交易
    string shuffle_states_contents = txcontents->at(3);  // 需要shuffle的状态

    bool shuffle_states = false; // 最后一笔跨片交易处理完是否进行状态shuffle
    // 有状态需要shuffle，主分片将最新值发送给副写分片，副写分片将阻塞的交易发送给主写分片(正确做法: 需要等前面所有的交易处理完)
    if(shuffle_states_contents != ""){
        shuffle_states = true;
    }

    vector<string> items;
    boost::split(items, prefix, boost::is_any_of("|"), boost::token_compress_on);
    string currentEpochId = items.at(1);
    // PLUGIN_LOG(INFO) << LOG_KV("prefix", prefix)
    //                  << LOG_KV("subtxsContents", subtxsContents)
    //                  << LOG_KV("convertedIntraTxContents", convertedIntraTxContents)
    //                  << LOG_KV("currentEpochId", currentEpochId);

    if(subtxsContents != "none"){ // 若跨片交易不为空
        // 使用set对跨片交易进行重排序
        std::shared_ptr<set<transactionWithReadWriteSet::Ptr, ReadWriteKeyCompare>> reorderedTxs = 
                            std::make_shared<set<transactionWithReadWriteSet::Ptr, ReadWriteKeyCompare>>(); 

        // 遍历当前epoch中的所有跨片子交易，统计状态访问信息，并对交易进行重排序
        vector<string> subTxs;
        boost::split(subTxs, subtxsContents, boost::is_any_of("&"), boost::token_compress_on);
        int transaction_size = subTxs.size();
        PLUGIN_LOG(INFO) << LOG_KV("batch中包含的跨片子交易数目为", transaction_size);
        for(int i = 0; i < transaction_size; i++){
            vector<string> subItems;
            string subtxContent = subTxs.at(i); // intershardTxid + flag + participant_shardids + flag + txRlps + flag + rwkeys;
            boost::split(subItems, subtxContent, boost::is_any_of("|"), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中

            string intershardTxid = subItems.at(0); // 跨分片交易id
            string participant_shardids = subItems.at(1); // 跨片交易全体参与方
            string txRlps = subItems.at(2); // 跨片子交易 rlp
            string rwKeys = subItems.at(3); // 跨片交易读写集(假设一笔跨片交易仅访问每个分片一个状态)
            // PLUGIN_LOG(INFO) << LOG_KV("intershardTxid", intershardTxid);
            // PLUGIN_LOG(INFO) << LOG_KV("participant_shardids", participant_shardids)
            //                  << LOG_KV("txRlps", txRlps)
            //                  << LOG_KV("rwKeys", rwKeys);

            // 计算参与者分片中最小的 shardid
            int minimum_participant_shardid = 0;
            vector<string> shardIds;
            boost::split(shardIds, participant_shardids, boost::is_any_of("_"), boost::token_compress_on);
            int index = 0;
            for(size_t i = 0; i < shardIds.size(); i++) {
                if(atoi(shardIds.at(i).c_str()) == dev::consensus::internal_groupId) { index = i; }
                int shardid = atoi(shardIds.at(i).c_str());
                if(i == 0){
                    minimum_participant_shardid = shardid;
                }
                else if(minimum_participant_shardid > shardid)
                {
                    minimum_participant_shardid = shardid;
                }
            }

            // 获取跨片交易访问的本地状态信息
            vector<string> rwKeyItems;
            boost::split(rwKeyItems, rwKeys, boost::is_any_of("_"), boost::token_compress_on);
            string localrwKey = rwKeyItems.at(index); // 跨片交易访问的本地状态
            int key_size = rwKeyItems.size(); // 记录与本地状态 localRWKey 发生交易的其他状态
            for(int i = 0; i < key_size; i++) {
                if(i != index) {
                    string remoteKey = rwKeyItems.at(i);
                    m_LRU_StatesInfo->update_lru_localkeys_interTx(localrwKey, remoteKey);
                }
            }

            // 根据以上信息初始化带读写集的交易
            auto txWithRWSet = make_shared<transactionWithReadWriteSet>(participant_shardids, localrwKey, rwKeys, currentEpochId, 
                                                            intershardTxid, coordinator_shardid, minimum_participant_shardid);
            string txid = ""; // 交易id

            // 将所有子交易赋值给tx的txs字段中
            vector<string> txRLP_items;
            boost::split(txRLP_items, txRlps, boost::is_any_of("_"), boost::token_compress_on);
            int subtx_size = txRLP_items.size();
            for(int i = 0; i < subtx_size; i++){
                string subtx_rlp = txRLP_items.at(i);
                auto subtx_ptr = std::make_shared<Transaction>(
                    jsToBytes(subtx_rlp, OnFailed::Throw), CheckTransaction::Everything);

                txWithRWSet->txs.push_back(subtx_ptr);

                // 获取跨片交易的txid，因为子交易的txid都一样，因此只需要拿一次就可以
                if(i == 0){
                    string data_str = dataToHexString(subtx_ptr->get_data());
                    vector<string> dataItems;
                    boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                    bytes testdata = fromHex(dataItems.at(1));
                    txid = dataToHexString(testdata);
                }
            }

            txWithRWSet->setTxid(txid);
            txWithRWSet->emptyTransaction = false; // 非空交易
            txWithRWSet->is_intershardTx = true; // 跨片交易


                // 
                // string txid = txWithRWSet->txid;
                if(txid == "a3084"){
                    PLUGIN_LOG(INFO) << LOG_DESC("第一次处理跨片交易时间点");
                    struct timeval tv;
                    gettimeofday(&tv, NULL);
                    long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;

                    PLUGIN_LOG(INFO) << LOG_KV("txid", txid) << LOG_KV("time_usec", time_msec);
                }




            // 对跨片子交易根据参与者id(participant_shardid)进行排序
            reorderedTxs->insert(txWithRWSet);
        }
        // 处理排序后的跨片子交易(包括: 分析每个交易需要发送的读写集、和接收的读写集，然后插入缓存队列)
        processReorderedTxs(coordinator_shardid, reorderedTxs, currentEpochId, shuffle_states, shuffle_states_contents);
    }

    // 注意！这里即使立即处理重组片内交易，也是满足严格的可串行化的:
    // 不同的重组跨片交易之间: 系统按照epochid依次处理上层发下来的重组交易，因此满足原先的顺序
    // 重组片内交易与跨片交易之间: 即使此处立即处理了片内交易，但是会检查读写集，若重组跨片交易的已经被阻塞，那么交易会被阻塞
    // if(convertedIntraTxContents != "none" && masterwriteBlocked == true && size_blocked_intraTxContent() == 0){
    //     // 检查是否有重组片内交易，若有则进行处理，直接当片内交易处理，先记录重组片内交易的状态访问情况，然后尝试执行交易
    //     vector<string> convertedIntraTxs;
    //     boost::split(convertedIntraTxs, convertedIntraTxContents, boost::is_any_of("&"), boost::token_compress_on);
    //     PLUGIN_LOG(INFO) << LOG_KV("准备处理的重组片内交易数目为", convertedIntraTxs.size());

    //     int convertedIntraTxs_size = convertedIntraTxs.size();
    //     if(convertedIntraTxs_size > 1){ //  rlp_rlp...|key1_key2... & rlp_rlp...|key1_key2...
    //         // PLUGIN_LOG(INFO) << LOG_DESC("发现重组片内交易");
    //         for(int i = 0; i < convertedIntraTxs_size; i++) {
    //             string intraTxContent = convertedIntraTxs.at(i);
    //             vector<string> contentItems;
    //             boost::split(contentItems, intraTxContent, boost::is_any_of("|"), boost::token_compress_on);
    //             string txrlps = contentItems.at(0);
    //             string rwkeys = contentItems.at(1);
    //             vector<string> txrlpItems; // txrlps --> tx
    //             boost::split(txrlpItems, txrlps, boost::is_any_of("_"), boost::token_compress_on);

    //             vector<Transaction::Ptr> subtxs;
    //             for(int i = 0; i < txrlpItems.size(); i++) {
    //                 auto tx = make_shared<Transaction>(jsToBytes(txrlpItems.at(i), OnFailed::Throw), CheckTransaction::Everything); // 包装成交易
    //                 subtxs.push_back(tx);
    //             }

    //             vector<string> keyItems; // 记录转换后的片内访问的片内状态
    //             boost::split(keyItems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);
    //             int rwkeys_size = keyItems.size();
    //             for(int i = 0; i < rwkeys_size; i++) {
    //                 string localkey = keyItems.at(i);
    //                 m_LRU_StatesInfo->update_lru_localkeys_intraTx(localkey);
    //             }
                
    //             // 尝试执行交易
    //             bool isblocked = false; // 检查当前交易的读写集key是否被阻塞，若交易的读写集未被阻塞, 立即执行交易，否则缓存交易
    //             int rwkey_size = keyItems.size();
    //             vector<string> rwkeys_blocking_shardids; // 重组片内交易读写集涉及到的协调者分片id，原始片内交易也这么处理(因为原始片内交易或者重组片内交易可能涉及到多个阻塞队列)

    //             int uppershards_size = upper_groupids.size();
    //             for(int i = 0; i < rwkey_size; i++) {
    //                 string rwkey = keyItems.at(i);
    //                 for(int i = 0; i < uppershards_size; i++){
    //                     int coordinator_shardid = upper_groupids.at(i);
    //                     auto blockingTxQueue = m_blockingTxQueues->at(to_string(coordinator_shardid));
    //                     if(blockingTxQueue->isBlocked(rwkey) == true) {
    //                         rwkeys_blocking_shardids.push_back(to_string(coordinator_shardid));
    //                     }
    //                 }
    //             }
    //             if(isblocked == false) { // 若交易的读写集未被阻塞, 立即执行交易
    //                 // PLUGIN_LOG(INFO) << LOG_DESC("重组交易未被阻塞, 立即执行");
    //                 int subtxs_size = subtxs.size();
    //                 for(size_t i = 0; i < subtxs_size; i++){
    //                     auto tx = subtxs.at(i);
    //                     executeTx(tx, false);
    //                 }
    //                 add_processedTxNum();
    //                 if(get_processedTxNum() % 1000 == 0){
    //                     PLUGIN_LOG(INFO) << LOG_DESC("执行阻塞的重组片内交易");
    //                     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
    //                 }
    //             }

    //             else { // 交易被阻塞，缓存交易
    //                 auto txwithrwset = std::make_shared<dev::plugin::transactionWithReadWriteSet>();
    //                 txwithrwset->rwkeys = rwkeys;
    //                 int subtxs_size = subtxs.size();
    //                 for(int i = 0; i < subtxs_size; i++) {
    //                     auto tx = subtxs.at(i);
    //                     txwithrwset->txs.push_back(tx);
    //                 }

    //                 txwithrwset->is_intershardTx = false;

    //                 // 将交易 txwithrwset 插入至 rwkeys_blocking_shardids
    //                 int rwkeys_blocking_shardids_size = rwkeys_blocking_shardids.size();
    //                 for(int i = 0; i < rwkeys_blocking_shardids_size; i++){
    //                     string shardid = rwkeys_blocking_shardids.at(i);
    //                     auto blockingTxQueue = m_blockingTxQueues->at(shardid);
    //                     blockingTxQueue->insertTx(txwithrwset);
    //                 }
    //             }
    //         }
    //     }
    // }
    // else if(convertedIntraTxContents != "none" && size_blocked_intraTxContent() != 0){ // 缓存重组片内交易等待处理
    //     add_blocked_intraTxContent(convertedIntraTxContents);
    // }
}

/**
 * NOTES:
 * 对排序后的交易进行处理
*/
void TransactionExecuter::processReorderedTxs(string coordinator_shardid, 
                shared_ptr<set<transactionWithReadWriteSet::Ptr, ReadWriteKeyCompare>> reorderedTxs, string& interShardTxEpochID, bool _shuffle_states, string& _shuffle_states_contents){
    // 函数接收排序后的一批跨片子交易(记为一个epoch), 相同参与方的跨片交易摆在一起(记为一个batch)，每一个batch只在处理第一笔交易时发一次对方需要的所有读写集初始值
    // 每个batch第一笔交易中记录所要发送的读写集(格式:epochID_batchId_keyAddress)，所有交易都记录需要接受的读写集(格式:epochID_batchId_keyAddress)
    // 注：这里临时 使用 keyAddress 代替 epochID_batchId_keyAddress 为了避免处理不一致的问题！

    shared_ptr<vector<transactionWithReadWriteSet::Ptr>> reorderedTxSet = make_shared<vector<transactionWithReadWriteSet::Ptr>>();

    // 初始化相关变量
    int currentBatchId = 1; // 本epoch内跨片交易的初始BatchId
    int processedEpochTxNum = 0; // 当前epoch中已经被处理的交易数目
    string lastParticipantIds = ""; // 上一笔跨片交易参与方
    string currentEpochId = interShardTxEpochID; // 当前一批交易的EpochId
    vector<string> rwkeyToSend; // 每个batch中第一笔交易需要发送的读写集(格式:epochID_batchId_keyAddress, 改为keyAddress！)
    vector<string> rwkeyToReceive; // 记录每笔交易需要收到的读写集【key:epochID_batchId_keyAddress，改为keyAddress！】
    vector<transactionWithReadWriteSet::Ptr> cachedTransactions; // 缓存一个batch中的交易，扫描完整个batch后，再插入缓存队列

    // 遍历重排序后的交易
    transactionWithReadWriteSet::Ptr currentTx;
    transactionWithReadWriteSet::Ptr previousTx;
    int txIndex = 0;
    for(auto &iter:(*reorderedTxs)) {
        if(txIndex == 0){
            currentTx = iter;
        }else{
            previousTx = currentTx;
            currentTx = iter;
            // PLUGIN_LOG(INFO) << LOG_KV("txIndex", txIndex)
            //                  << LOG_KV("previousTx->tx->hash()", previousTx->tx->hash())
            //                  << LOG_KV("currentTx->tx->hash()", currentTx->tx->hash());
        }
        string participantIds = currentTx->participantIds; // 当前这笔交易的所有参与方Id
        // 如果当前交易是整个epoch中的第一笔交易
        if(processedEpochTxNum == 0){
            lastParticipantIds = participantIds; // 初始化lastParticipantIds
            string localrwkey = currentTx->localreadwriteKey; // 准备需要发送的读写集, 将当前交易访问的本地读写集放入要发送的读写集中
            string key = localrwkey; // 将key临时改为localrwkey！，避免处理不一致问题
            vector<string>::iterator it = find(rwkeyToSend.begin(), rwkeyToSend.end(), key); // 检查 localrwkey 是否在 rwkeyToSend 中
            if(it == rwkeyToSend.end()) { // 本读写集之前没有缓存过
                rwkeyToSend.push_back(key);
            }
            cachedTransactions.push_back(currentTx); // 缓存当前batch中的交易
        }
        // 如果当前交易不是整个epoch中的第一笔交易
        else{
            // 与上一笔交易的参与方相同(相同batch)
            if(lastParticipantIds == participantIds) {
                string localrwkey = currentTx->localreadwriteKey; // 将当前交易访问的本地读写集放入要发送的读写集中
                // string key = currentEpochId + "_" + to_string(currentBatchId) + "_" + localrwkey;
                string key = localrwkey; // 将key临时改为localrwkey！，避免处理不一致问题
                vector<string>::iterator it = find(rwkeyToSend.begin(), rwkeyToSend.end(), key); // 检查 localrwkey 是否在 rwkeyToSend 中
                if(it == rwkeyToSend.end()) { // 本读写集之前没有缓存过
                    rwkeyToSend.push_back(key);
                    // PLUGIN_LOG(INFO) << LOG_KV("需要发送的读写集key", key);
                }
                currentTx->interShardTxBatchId = to_string(currentBatchId); // 初始化交易的batchId(epochID在前面已经赋值过)
                cachedTransactions.push_back(currentTx); // 缓存当前batch中的交易
            }
            // 与上一笔交易的参与方不相同(进入新的batch)
            else if(lastParticipantIds != participantIds) {
                previousTx->lastTxInBatch = true; // 上一笔交易设置为batch中的最后一笔交易
                
                for(int i = 0; i < cachedTransactions.size(); i++) { // 首先处理缓存的上一个batch的交易
                    auto cachedTx = cachedTransactions.at(i);

                    if(i == 0){
                        cachedTx->setrwkeysTosend(rwkeyToSend);
                    }
                    cachedTx->interShardTxBatchId = to_string(currentBatchId); // 设置交易的BatchId

                    // 计算当前交易的 rwkeyToReceive
                    vector<string> keyItems;
                    string rwkeys = cachedTx->rwkeys;
                    boost::split(keyItems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);

                    vector<string> participantItems;
                    string participantIds = cachedTx->participantIds;
                    boost::split(participantItems, participantIds, boost::is_any_of("_"), boost::token_compress_on);
                    
                    for(int i = 0; i < participantItems.size(); i++) {
                        if(participantItems.at(i) != to_string(dev::consensus::internal_groupId)) {
                            // string key = currentEpochId + "_" + to_string(currentBatchId) + "_" + keyItems.at(i);
                            string key = keyItems.at(i);; // 将key临时改为localrwkey！，避免处理不一致问题
                            rwkeyToReceive.push_back(key);
                        }
                    }
                    cachedTx->rwkeyToReceive = rwkeyToReceive; // 设置交易的 rwkeyToReceive
                    // auto blockingTxQueue = m_blockingTxQueues->at(coordinator_shardid);
                    // blockingTxQueue->insertTx(cachedTx); // 将交易插入缓存队列

                    reorderedTxSet->push_back(cachedTx);

                    rwkeyToReceive.clear(); // 清空rwkeyToReceive
                    // PLUGIN_LOG(INFO) << LOG_KV("cachedTx->participantIds", cachedTx->participantIds)
                    //                  << LOG_KV("cachedTx->localreadwriteKey", cachedTx->localreadwriteKey)
                    //                  << LOG_KV("cachedTx->interShardTxEpochID", cachedTx->interShardTxEpochID)
                    //                  << LOG_KV("cachedTx->interShardTxBatchId", cachedTx->interShardTxBatchId);
                }
                rwkeyToSend.clear(); // 清空 rwkeyToSend
                cachedTransactions.clear(); // 清空 cachedTransactions(一个batch交易)
                currentBatchId++; // 更新 currentBatchId

                // 处理当前新batch中的第一笔交易
                lastParticipantIds = participantIds; // 更新lastParticipantIds
                if(!currentTx->emptyTransaction){ // 非空交易执行以下步骤
                    string localrwkey = currentTx->localreadwriteKey; // 当前交易访问的本地读写集
                    // string key = currentEpochId + "_" + to_string(currentBatchId) + "_" + localrwkey;
                    string key = localrwkey; // 将key临时改为localrwkey！，避免处理不一致问题
                    rwkeyToSend.push_back(key);  // 本batch 第一笔交易需要发送给其他分片的读写集
                    currentTx->interShardTxBatchId = to_string(currentBatchId);
                    cachedTransactions.push_back(currentTx); // 缓存该笔交易(需要接收的读写集在最后统一设置)
                }
            }
        }

        // 若epoch中的所有交易全部处理完毕, 开始将缓存的最后一个batch的交易插入缓存队列
        if(processedEpochTxNum == reorderedTxs->size() - 1) { // 所有交易处理完毕
            int cache_size = cachedTransactions.size();
            for(int i = 0; i < cache_size; i++) {
                auto cachedTx = cachedTransactions.at(i); // 获取交易
                if(i == 0){
                    cachedTx->setrwkeysTosend(rwkeyToSend);
                }
                if(i == cache_size - 1) { // 设置epocha内最后一笔交易的标记位 lastTxInEpoch
                    cachedTx->lastTxInBatch = true; // 当前交易也为batch中的最后一笔交易
                    cachedTx->lastTxInEpoch = true;
                    cachedTx->shuffle_states = _shuffle_states;
                    cachedTx->shuffle_states_contents = _shuffle_states_contents;
                }

                // 计算当前交易的 rwkeyToReceive
                vector<string> keyItems;
                string rwkeys = cachedTx->rwkeys;
                boost::split(keyItems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);

                vector<string> participantItems;
                string participantIds = cachedTx->participantIds;
                boost::split(participantItems, participantIds, boost::is_any_of("_"), boost::token_compress_on);

                for(int i = 0; i < participantItems.size(); i++) {
                    if(participantItems.at(i) != to_string(dev::consensus::internal_groupId)) {
                        // string key = currentEpochId + "_" + to_string(currentBatchId) + "_" + keyItems.at(i);
                        string key = keyItems.at(i); // 将key临时改为localrwkey！，避免处理不一致问题
                        rwkeyToReceive.push_back(key);
                    }
                }
                cachedTx->rwkeyToReceive = rwkeyToReceive; // 设置交易的 rwkeyToReceive
                // auto blockingTxQueue = m_blockingTxQueues->at(coordinator_shardid);
                // blockingTxQueue->insertTx(cachedTx); // 将交易插入缓存队列

                reorderedTxSet->push_back(cachedTx);

                rwkeyToReceive.clear(); // 清空rwkeyToReceive
                // PLUGIN_LOG(INFO) << LOG_KV("cachedTx->lastTxInBatch", cachedTx->lastTxInBatch);
                // PLUGIN_LOG(INFO) << LOG_KV("cachedTx->participantIds", cachedTx->participantIds)
                //                  << LOG_KV("cachedTx->localreadwriteKey", cachedTx->localreadwriteKey)
                //                  << LOG_KV("cachedTx->interShardTxEpochID", cachedTx->interShardTxEpochID)
                //                  << LOG_KV("cachedTx->interShardTxBatchId", cachedTx->interShardTxBatchId);
            }
        }
        processedEpochTxNum++;
        txIndex++;
    }


    // 将 reorderedTxSet 中的交易一次性插入缓冲队列

    auto blockingTxQueue = m_blockingTxQueues->at(coordinator_shardid);
    blockingTxQueue->insertTxs(reorderedTxSet);

}

/** 
 * NOTES: // 原始片内交易，可能会遇到交易要访问的状态已经被迁移走(重组片内交易不在这里处理，是在 processOrderedBatchedSubInterShardTxs 中处理)
 * */
void TransactionExecuter::processSingleIntraShardTx(shared_ptr<Transaction> tx, string& data_str){
    vector<string> items;
    boost::split(items, data_str, boost::is_any_of("|"), boost::token_compress_on);
    string rwkeys = items.at(1);
    vector<string> rwkeyitems;
    boost::split(rwkeyitems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);

    // 保存交易访问的读写集信息
    int rwkeys_size = rwkeyitems.size();
    for(int i = 0; i < rwkeys_size; i++){
        string rwkey = rwkeyitems.at(i);
        m_LRU_StatesInfo->update_lru_localkeys_intraTx(rwkey);
    }

    // 宏观solution:
    // 1. 判断是否有写权限已经被转移走的状态，或者是附加被阻塞的状态；若有，则阻塞交易，更新附加被阻塞的状态列表，否则进入2
    // 2. 按照原先逻辑处理交易
    // 3. 当启动shuffleTx时，将目前已积攒的交易发送给其他分片并本地保存一份，并清空附加被阻塞的状态列表（不用担心此时有其他交易进来，因为是线程是阻塞的）
    // 4. 当收到主分片发送来的状态值时，副分片将发送给主分片的交易也执行掉（这里也为了简化代码，只要收到一个来自某个分片的消息，就立即执行交易，其实是应该收齐所有分片的消息才是最正确的，后面再优化吧）

    // 判断当前交易是否包含临时转移状态
    bool isBlocked = false;
    for(int i = 0; i < rwkeys_size; i++){
        string rwkey = rwkeyitems.at(i);
        if(transfered_states.count(rwkey) != 0 || count_otherState(rwkey) != 0){
            isBlocked = true;
            break;
        }
    }

    if(isBlocked == true){ // 若交易被阻塞
        // 更新 other_blocked_states
        for(int i = 0; i < rwkeys_size; i++){
            string rwkey = rwkeyitems.at(i);
            add_otherState(rwkey); // 将交易 rwkey 添加到 other_blocked_states 中
        }

        // 缓存该笔交易
        vector<string> item; // <txrlp, shardid, shardid...>
        string txrlp = toHex(tx->rlp());
        item.push_back(txrlp);

        for(int i = 0; i < rwkeys_size; i++){
            string rwkey = rwkeyitems.at(i);
            if(transfered_states.count(rwkey) != 0){
                string shardid = transfered_states.at(rwkey);
                item.push_back(shardid);
            }
        }
        blockedTxs.enqueue(item);
    }


    else { // 否则开始按照原先流程立即处理交易
        isBlocked = false;
        int blockingQueue_key = 0; // 阻塞交易的队列key

        std::vector<int> blockingQueue_keys;
        int uppershards_size = upper_groupids.size();
        for(int i = 0; i < uppershards_size; i++){
            int coordinator_shardid = upper_groupids.at(i);
            auto blockingTxQueue = m_blockingTxQueues->at(to_string(coordinator_shardid));

            // 检查交易的每个读写集是否被阻塞，先假设每个交易的读写集只会被一个队列阻塞
            for(int i = 0; i < rwkeys_size; i++) {
                string rwkey = rwkeyitems.at(i);
                if(blockingTxQueue->isBlocked(rwkey) == true){
                    isBlocked = true;
                    blockingQueue_keys.push_back(coordinator_shardid);
                    break;
                }
            }
        }

        // PLUGIN_LOG(INFO) << LOG_KV("isBlocked", isBlocked);
        // 若交易的所有状态都未被阻塞, 立即执行交易
        if(isBlocked == false){
            // executeTx(tx);
            executeTx(tx, false);
            add_processedTxNum();
            // if(get_processedTxNum() % 1000 == 0){
            //     PLUGIN_LOG(INFO) << LOG_DESC("执行原始片内交易");
            //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
            // }

            // // 交易已经提交，记录交易的endtime
            string data_str = dataToHexString(tx->get_data());
            vector<string> dataItems;
            boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
            string txid = dataItems.at(2).c_str();

            struct timeval tv;
            gettimeofday(&tv, NULL);
            long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;
            m_txid_to_endtime->insert(make_pair(txid, time_msec)); // 记录txid的开始时间

            // PLUGIN_LOG(INFO) << LOG_KV("commit txid", txid)
            //                  << LOG_KV("time_msec", time_msec);

        }
        else{ // 将交易包装成 transactionWithReadWriteSet 类型并加入相应的缓冲队列
            // PLUGIN_LOG(INFO) << LOG_DESC("将交易包装成 transactionWithReadWriteSet 类型并加入缓冲队列");
            
            string data_str = dataToHexString(tx->get_data());
            vector<string> dataItems;
            boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
            string txid = dataItems.at(2).c_str();
            
            auto txwithrwset = make_shared<dev::plugin::transactionWithReadWriteSet>();
            txwithrwset->txs.push_back(tx);
            txwithrwset->txid = txid;
            txwithrwset->rwkeys = rwkeys;
            txwithrwset->is_intershardTx = false;
            txwithrwset->blockingQueue_keys = blockingQueue_keys;

            int blockingQueue_keys_size = blockingQueue_keys.size(); // 若 blockingQueue_keys == 1时，表示只被一个队列阻塞，处理的时候只需要从当前的队列中删除即可
            // PLUGIN_LOG(INFO) << LOG_KV("blockingQueue_keys_size", blockingQueue_keys_size);


            // if(blockingQueue_keys_size > 1){
            //     PLUGIN_LOG(INFO) << LOG_DESC("发现被多个队列阻塞的片内交易") << LOG_KV("blockingQueue_keys.at(0)", blockingQueue_keys.at(0))
            //                                                             << LOG_KV("blockingQueue_keys.at(1)", blockingQueue_keys.at(1))
            //                                                             << LOG_KV("blockingQueue_keys.size()", blockingQueue_keys.size())
            //                                                             << LOG_KV("rwkeys", rwkeys);
            // //     int queuekey1 = blockingQueue_keys.at(0);
            // //     int queuekey2 = blockingQueue_keys.at(1);
            // //     auto blockingTxQueue1 = m_blockingTxQueues->at(to_string(queuekey1));
            // //     auto blockingTxQueue2 = m_blockingTxQueues->at(to_string(queuekey2));
            // //     PLUGIN_LOG(INFO) << LOG_KV("blockingTxQueue1_size", blockingTxQueue1->txsize());
            // //     PLUGIN_LOG(INFO) << LOG_KV("blockingTxQueue2_size", blockingTxQueue2->txsize());
            // }

            // for(int i = 0; i < blockingQueue_keys_size; i++){gut
            //     auto blockingTxQueue = m_blockingTxQueues->at(to_string(blockingQueue_keys.at(i)));
            //     blockingTxQueue->insertTx(txwithrwset);
            // }

            auto blockingTxQueue = m_blockingTxQueues->at(to_string(blockingQueue_keys.at(0)));
            blockingTxQueue->insertTx(txwithrwset);

        }
    }
}

/**
 * NOTES: 批从destinShardId分片将一批状态readwritekey的写权限改回到sourceShardId
 * */
void TransactionExecuter::batchRequestForMasterChange(vector<string>& requests){
    // PLUGIN_LOG(INFO) << LOG_DESC("In batchRequestForMasterChange");
    string requestkeys = "";
    string flag = "_";
    string localshardid = "";
    string destinshardid = "";
    string ancestorgroupid = "";

    int request_size = requests.size();
    for(int i = 0; i < request_size; i++) {
        string requestcontent = requests.at(i);
        PLUGIN_LOG(INFO) << LOG_KV("requestcontent", requestcontent); // requestcontent=3|1|2|stateE|randomId

        vector<string> contentitems;
        boost::split(contentitems, requestcontent, boost::is_any_of("|"), boost::token_compress_on);
        ancestorgroupid = contentitems.at(0);
        localshardid = contentitems.at(1);
        destinshardid = contentitems.at(2);
        string requestkey = contentitems.at(3);

        if(i == 0) {
            requestkeys = requestkey;
        }
        else {
            requestkeys = requestkeys + flag + requestkey;
        }
    }

    // 假设现在有多个状态需要转移，例如requestkeys = stateE|stateF
    // requestkeys = "stateE_stateF";
    
    PLUGIN_LOG(INFO) << LOG_KV("ancestorgroupid", ancestorgroupid)
                     << LOG_KV("requestkeys", requestkeys); // requestcontent=3|1|2|stateE|randomId

    // 根据 requestkeys, localshardid, destinshardid 和 ancestorgroupid 来发起状态转移请求
    protos::RequestForMasterShardMsg requestMsg;
    requestMsg.set_sourceshardid(atoi(localshardid.c_str()));
    requestMsg.set_destinshardid(atoi(destinshardid.c_str()));
    requestMsg.set_readwritekey(requestkeys);
    requestMsg.set_requestmessageid(to_string(dev::consensus::internal_groupId)+to_string(stateChangeRequestBatchId)); // messageid = 分片id + requestBatchId
    stateChangeRequestBatchId++;

    string serializedrequestMsg;
    requestMsg.SerializeToString(&serializedrequestMsg);
    auto msgBytes = asBytes(serializedrequestMsg);
    dev::sync::SyncRequestForMasterShardMsg retPacket;
    retPacket.encode(msgBytes);
    auto msg = retPacket.toMessage(m_group_protocolID);
    m_group_p2p_service->asyncSendMessageByNodeID(dev::plugin::hieraShardTree->get_forward_nodeId(atoi(ancestorgroupid.c_str()) - 1),msg, CallbackFuncWithSession(), dev::network::Options());
    // m_group_p2p_service->asyncSendMessageByNodeID(dev::consensus::forwardNodeId.at(atoi(ancestorgroupid.c_str()) - 1), msg, CallbackFuncWithSession(), dev::network::Options());
}

/** 
 * NOTES: 处理权限转移请求交易
 * */
void TransactionExecuter::processStateMasterRequestTx(shared_ptr<Transaction> tx){
    PLUGIN_LOG(INFO) << LOG_DESC("processStateMasterRequestTx");

    // 开始解析交易的data字段，获取权限请求信息
    string data_str = dataToHexString(tx->get_data());
    // PLUGIN_LOG(INFO) << LOG_KV("data_str", data_str); // 例：0x777888999|1|2|stateE_stateF|, 不同sourceshardid和destinshardid之间的状态转移请求分开处理了，不需要再缓存

    vector<string> dataitems;
    boost::split(dataitems, data_str, boost::is_any_of("|"), boost::token_compress_on);
    string sourceshardid = dataitems.at(1);
    string destinshardid = dataitems.at(2);
    string requestkeys = dataitems.at(3);
    string messageid = dataitems.at(4);
    // PLUGIN_LOG(INFO) << LOG_KV("sourceshardid", sourceshardid)
    //                  << LOG_KV("destinshardid", destinshardid)
    //                  << LOG_KV("requestkeys", requestkeys)
    //                  << LOG_KV("messageid", messageid);

    // 保存所有待发给每个节点的PrePareMsg
    auto sourcePrePareMsg = make_shared<StateMasterChangePrePrepareMsgs>(sourceshardid, destinshardid, requestkeys, messageid);
    auto destinPrePareMsg = make_shared<StateMasterChangePrePrepareMsgs>(sourceshardid, destinshardid, requestkeys, messageid);
    auto PrePareMsgs = make_shared<map<string, StateMasterChangePrePrepareMsgs::Ptr>>(); // 缓存需要发送给不同分片的状态转移请求prepare消息
    PrePareMsgs->insert(make_pair(sourceshardid, sourcePrePareMsg));
    PrePareMsgs->insert(make_pair(destinshardid, destinPrePareMsg));

    // 由当前分片的转发节点将PrePare消息发送至下层分片
    if(dev::plugin::hieraShardTree->is_forward_node(internal_groupId, nodeIdHex)) {
        sendMasterChangePrePrepareMsg(PrePareMsgs);
    }
}

/** 
 * NOTES: 下层分片处理上层发送的状态转移请求的PrePrepare消息
 * */
void TransactionExecuter::processMasterChangePrePrepareTx(shared_ptr<Transaction> tx){
    // 开始解析交易的data字段，获取权限请求的内容
    PLUGIN_LOG(INFO) << LOG_DESC("processMasterChangePrePrepareTx...");

    string data_str = dataToHexString(tx->get_data());
    // PLUGIN_LOG(INFO) << LOG_KV("data_str", data_str);
    vector<string> dataItems;
    boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
    string sourceshardids = dataItems.at(1);
    string destinshardids = dataItems.at(2);
    string requestkeys = dataItems.at(3);
    string messageids = dataItems.at(4);
    string coordinatorshardid = dataItems.at(5);

    vector<string> requestKeyItems;
    boost::split(requestKeyItems, requestkeys, boost::is_any_of("_"), boost::token_compress_on);
    int key_size = requestKeyItems.size();
    for(int i = 0; i < key_size; i++) { // 对Key进行上锁
        string rwkey = requestKeyItems.at(i);
        dev::plugin::m_lockedStateKey->insert(rwkey); // abort没做，先不做了
    }

    // 成功上锁，回复协调者 MasterShardPrepareMsg 消息, 由当前分片的转发节点回复协调者
    if(dev::plugin::hieraShardTree->is_forward_node(internal_groupId, dev::plugin::nodeIdHex)) {
        sendMasterChangePrepareMsg(sourceshardids, destinshardids, requestkeys, messageids, coordinatorshardid);
    }
}

/** 
 * NOTES: 上层处理下层回复的状态转移Prepare消息
 * */
void TransactionExecuter::processMasterChangePrepareTx(shared_ptr<Transaction> tx){
    PLUGIN_LOG(INFO) << LOG_DESC("In processMasterChangePrepareTx");
    // 开始解析交易的data字段，获取权限请求的内容
    string data_str = dataToHexString(tx->get_data());
    // PLUGIN_LOG(INFO) << LOG_KV("data_str", data_str); // m_data_hex_str = 0x777888999|2|3|0x32fd50c7489b657eedfa544b4b6a3276edcbb20b

    vector<string> dataItems;
    boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
    string sourceshardids = dataItems.at(1);
    string destinshardids = dataItems.at(2);
    string readwritekeys = dataItems.at(3);
    string requestmessageids = dataItems.at(4);
    string coordinatorshardid = dataItems.at(5);
    // PLUGIN_LOG(INFO) << LOG_KV("sourceshardids", sourceshardids)
    //                  << LOG_KV("destinshardids", destinshardids)
    //                  << LOG_KV("readwritekeys", readwritekeys)
    //                  << LOG_KV("requestmessageids", requestmessageids)
    //                  << LOG_KV("coordinatorshardid", coordinatorshardid);

    PLUGIN_LOG(INFO) << LOG_DESC("收齐了requestmessageids的所有票, 依次向所有参与者分片发送commit消息");
    auto sourceCommitMsg = make_shared<StateMasterChangeCommitMsgs>(sourceshardids, destinshardids, readwritekeys, requestmessageids);
    auto destCommitMsg = make_shared<StateMasterChangeCommitMsgs>(sourceshardids, destinshardids, readwritekeys, requestmessageids);
    auto CommitMsgs = make_shared<map<string, StateMasterChangeCommitMsgs::Ptr>>(); // 缓存需要发送给不同分片的状态转移请求commit消息
    CommitMsgs->insert(make_pair(sourceshardids, sourceCommitMsg));
    CommitMsgs->insert(make_pair(destinshardids, destCommitMsg));

    // 由当前分片的转发节点回复所有参与者commit消息
    if(dev::plugin::hieraShardTree->is_forward_node(internal_groupId, nodeIdHex)) {
        sendMasterChangeCommitMsg(CommitMsgs);
    }
}

/** 
 * NOTES: 下层分片处理上层发送下来的Commit消息
 * */
void TransactionExecuter::processMasterChangeCommitTx(shared_ptr<Transaction> tx){
    // 开始解析交易的data字段，获取权限请求的内容
    string data_str = dataToHexString(tx->get_data());
    // PLUGIN_LOG(INFO) << LOG_KV("data_str", data_str);
    vector<string> dataItems;
    boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
    
    string sourceshardids = dataItems.at(1);
    string destinshardids = dataItems.at(2);
    string requestkeys = dataItems.at(3);
    // string messageids = dataItems.at(4);
    // string coordinatorshardid = dataItems.at(5);

    // 所有节点更新 m_valid_state 和 m_master_state
    // shourceShard: m_valid_state(key, 0) -> m_valid_state(key, 1); m_master_state(key, 0) -> m_master_state(key, 1)
    // destinshard: m_valid_state(key, 1) -> m_valid_state(key, 1); m_master_state(key, 1) -> m_master_state(key, 0)

    vector<string> keyItems;
    boost::split(keyItems, requestkeys, boost::is_any_of("_"), boost::token_compress_on);

    int key_size = keyItems.size();
    for(int i = 0; i < key_size; i++) {
        string rwkey = keyItems.at(i);
        PLUGIN_LOG(INFO) << LOG_KV("更改权限的Key为", rwkey);

        if(atoi(sourceshardids.c_str()) == dev::consensus::internal_groupId){
            m_valid_state->at(rwkey) = 1;
            m_master_state->at(rwkey) = 1;
            // PLUGIN_LOG(INFO) << LOG_DESC("sourceshard,") << LOG_KV("m_valid_state:", m_valid_state->at(rwkey))
            //                                              << LOG_KV("m_master_state:", m_master_state->at(rwkey));
            // 更新 m_addedTransferedStates
            if(m_addedTransferedStates.count(rwkey) != 0) {
                m_addedTransferedStates.at(rwkey) = destinshardids;
            }
            else{
                m_addedTransferedStates.insert(make_pair(rwkey, destinshardids)); // 本地新增的状态rwkey来源于分片destinshardids
            }

            // 更新 m_shuffleValue_destshardid
            int shardid = atoi(destinshardids.c_str());
            add_shuffleValue_destshardid(rwkey, shardid);

            
        }

        else if(atoi(destinshardids.c_str()) == dev::consensus::internal_groupId){
            m_master_state->at(rwkey) = 0;

            // 更新 transfered_states
            if(transfered_states.count(rwkey) != 0) {
                transfered_states.at(rwkey) = sourceshardids;
            }
            else {
                transfered_states.insert(make_pair(rwkey, sourceshardids)); // 本地转移的状态流向分片sourceshardids
            }

            // PLUGIN_LOG(INFO) << LOG_DESC("destinshard,") << LOG_KV("m_valid_state", m_valid_state->at(rwkey))
            //                                              << LOG_KV("m_master_state", m_master_state->at(rwkey));
            // 更新 m_shuffleTxRLP_destshardid
            int shardid = atoi(sourceshardids.c_str());
            add_shuffleTxRLP_destshardid(rwkey, shardid);
        }
    }
}

/** 
 * NOTES: 向下层分片发送状态转移请求的PrePare消息
 * */
void TransactionExecuter::sendMasterChangePrePrepareMsg(shared_ptr<map<string, StateMasterChangePrePrepareMsgs::Ptr>> PrePareMsgs){
    PLUGIN_LOG(INFO) << LOG_DESC("In sendMasterChangePrePrepareMsg.....");

    // 开始向相关分片发送 MasterShardPrePrepareMsg 消息
    for(auto iter = (*PrePareMsgs).rbegin(); iter != (*PrePareMsgs).rend(); iter++) {
        string shardid = iter->first;
        auto PrePareMsg = iter->second;

        protos::MasterShardPrePrepareMsg Msg;
        Msg.set_sourceshardids(PrePareMsg->sourceshardid);
        Msg.set_destinshardids(PrePareMsg->destinshardid);
        Msg.set_readwritekeys(PrePareMsg->requestkeys);
        Msg.set_requestmessageid(PrePareMsg->messageid);
        Msg.set_coordinatorshardid(dev::consensus::internal_groupId);
        // PLUGIN_LOG(INFO) << LOG_KV("PrePareMsg->sourceshardid", PrePareMsg->sourceshardid)
        //                  << LOG_KV("PrePareMsg->destinshardid", PrePareMsg->destinshardid)
        //                  << LOG_KV("PrePareMsg->requestkeys", PrePareMsg->requestkeys)
        //                  << LOG_KV("PrePareMsg->messageid", PrePareMsg->messageid);

        string serializedPrePrepareMsg;
        Msg.SerializeToString(&serializedPrePrepareMsg);
        auto msgBytes = asBytes(serializedPrePrepareMsg);

        dev::sync::SyncMasterShardPrePrepareMsg retPacket;
        retPacket.encode(msgBytes);
        auto msg = retPacket.toMessage(m_group_protocolID);

        // 向所有参与者节点分片的转发节点发送 prePrepareMsg
        m_group_p2p_service->asyncSendMessageByNodeID(dev::plugin::hieraShardTree->get_forward_nodeId(atoi(shardid.c_str()) - 1), msg, CallbackFuncWithSession(), dev::network::Options());
    }
}

/** 
 * NOTES: 向协调者所有节点回复状态转移的PrepareMsg
 * */
void TransactionExecuter::sendMasterChangePrepareMsg(string& sourceshardids, string& destinshardids, string& requestkeys, string &messageids, string &coordinatorshardid){

    // PLUGIN_LOG(INFO) << LOG_DESC("In sendMasterChangePrepareMsg...");
    // PLUGIN_LOG(INFO) << LOG_KV("sourceshardids", sourceshardids)
    //                  << LOG_KV("destinshardids", destinshardids)
    //                  << LOG_KV("requestkeys", requestkeys)
    //                  << LOG_KV("messageids", messageids)
    //                  << LOG_KV("coordinatorshardid", coordinatorshardid);

    // 成功上锁，回复协调者 MasterShardPrepareMsg 消息, 由当前分片的转发节点回复协调者
    protos::MasterShardPrepareMsg Msg;
    Msg.set_sourceshardids(sourceshardids);
    Msg.set_destinshardids(destinshardids);
    Msg.set_readwritekeys(requestkeys);
    Msg.set_requestmessageids(messageids);
    Msg.set_coordinatorshardid(atoi(coordinatorshardid.c_str()));

    string serializedPrepareMsg;
    Msg.SerializeToString(&serializedPrepareMsg);
    auto msgBytes = asBytes(serializedPrepareMsg);

    dev::sync::SyncMasterShardPrepareMsg retPacket;
    retPacket.encode(msgBytes);
    auto msg = retPacket.toMessage(m_group_protocolID);

    // 向所有协调者节点广播发送 prepareMsg
    h512s nodeIDs = dev::plugin::hieraShardTree->get_nodeIDs(atoi(coordinatorshardid.c_str()));
    for (int k = 0; k < nodeIDs.size(); k++) {
        m_group_p2p_service->asyncSendMessageByNodeID(nodeIDs.at(k), msg, CallbackFuncWithSession(),dev::network::Options());
    }
    // for(size_t k = 0; k < 4; k++) {
    //     m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((atoi(coordinatorshardid.c_str()) - 1) * 4 + k), msg, CallbackFuncWithSession(),dev::network::Options());
    // }
}

/** 
 * NOTES: 向下层发送状态请求Commit消息
 * */
void TransactionExecuter::sendMasterChangeCommitMsg(shared_ptr<map<string, StateMasterChangeCommitMsgs::Ptr>> CommitMsgs) {
    PLUGIN_LOG(INFO) << LOG_DESC("In sendMasterChangeCommitMsg");

    string sourceshardid = "";
    string destinshardid = "";
    string requestkeys = "";

    // 开始向相关分片发送 MasterShardCommitMsg 消息
    for(auto iter = (*CommitMsgs).rbegin(); iter != (*CommitMsgs).rend(); iter++) {
        string shardid = iter->first;
        auto commitMsg = iter->second;
        
        // 开始向相关分片发送 MasterShardCommitMsg 消息
        protos::MasterShardCommitMsg Msg;
        Msg.set_sourceshardids(commitMsg->sourceshardid);
        Msg.set_destinshardids(commitMsg->destinshardid);
        Msg.set_readwritekeys(commitMsg->requestkeys);
        Msg.set_requestmessageids(commitMsg->messageid);
        Msg.set_coordinatorshardid(dev::consensus::internal_groupId);

        string serializedCommitMsgMsg;
        Msg.SerializeToString(&serializedCommitMsgMsg);
        auto msgBytes = asBytes(serializedCommitMsgMsg);

        dev::sync::SyncMasterShardCommitMsg retPacket;
        retPacket.encode(msgBytes);
        auto msg = retPacket.toMessage(m_group_protocolID);

        // 向所有参与者节点分片的转发节点发送 commitMsg
        h512s nodeIDs = dev::plugin::hieraShardTree->get_nodeIDs(atoi(shardid.c_str()));
        for (int k = 0; k < nodeIDs.size(); k++) {
            m_group_p2p_service->asyncSendMessageByNodeID(nodeIDs.at(k), msg, CallbackFuncWithSession(),dev::network::Options());
        }
        // for(size_t k = 0; k < 4; k++) {
        //     m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((atoi(shardid.c_str()) - 1) * 4 + k), msg, CallbackFuncWithSession(),dev::network::Options());
        // }

        sourceshardid = commitMsg->sourceshardid;
        destinshardid = commitMsg->destinshardid;
        requestkeys = commitMsg->requestkeys;
    }

    // 更新协调者中的信息
    vector<string> keyitems;
    boost::split(keyitems, requestkeys, boost::is_any_of("_"), boost::token_compress_on);

    int key_size = keyitems.size();
    for(int i = 0; i < key_size; i++) {
        string rwkey = keyitems.at(i);
        // 更新 masterChangedKey，后面的跨片交易都需要从这里检查主点权限是否发生变化
        if(dev::plugin::m_masterChangedKey->count(rwkey) == 0) {
            dev::plugin::m_masterChangedKey->insert(std::make_pair(rwkey, atoi(sourceshardid.c_str())));
        }
        else{
            dev::plugin::m_masterChangedKey->at(rwkey) = atoi(sourceshardid.c_str());
        }
        // PLUGIN_LOG(INFO) << LOG_DESC("m_masterChangedKey:")
        //                 << LOG_KV("readwritekey", rwkey)
        //                 << LOG_KV("masterShardId", dev::plugin::m_masterChangedKey->at(rwkey));
    }
}

/**
 * NOTES: 跨片交易参与者之一向协调者回复commit消息
 * */
void TransactionExecuter::responseCommitToCoordinator(string& _coordinator_shardid, string& _intershardTxids){
    // PLUGIN_LOG(INFO) << LOG_DESC("responseCommitToCoordinator");

    protos::CommitResponseToCoordinator commitMsg;
    commitMsg.set_participantshardid(dev::consensus::internal_groupId);
    commitMsg.set_intershardtxids(_intershardTxids);

    string serializedcommitMsg;
    commitMsg.SerializeToString(&serializedcommitMsg);
    auto txBytes = asBytes(serializedcommitMsg);

    dev::sync::SyncCommitResponseToCoordinatorMsg retPacket;
    retPacket.encode(txBytes);
    auto msg = retPacket.toMessage(m_group_protocolID);

    // 向协调者分片的所有节点广播commit消息
    h512s nodeIDs = dev::plugin::hieraShardTree->get_nodeIDs(atoi(_coordinator_shardid.c_str()));
    for (int k = 0; k < nodeIDs.size(); k++) {
        m_group_p2p_service->asyncSendMessageByNodeID(nodeIDs.at(k), msg, CallbackFuncWithSession(),dev::network::Options());
    }
    // for(size_t k = 0; k < 4; k++) {
    //     // PLUGIN_LOG(INFO) << LOG_KV("正在发送给协调者节点广播CommitResponseToCoordinator消息", dev::consensus::shardNodeId.at((atoi(_coordinator_shardid.c_str()) - 1) * 4 + k));
    //     m_group_p2p_service->asyncSendMessageByNodeID(dev::consensus::shardNodeId.at((atoi(_coordinator_shardid.c_str()) - 1) * 4 + k), msg, CallbackFuncWithSession(),dev::network::Options());
    // }
}

/** 
 * NOTES: 向下层子分片发送子交易和重组片内交易
 * */
void TransactionExecuter::sendBatchDistributedTxMsg(int coordinator_shardid, int _epochId, int destshardId, string& subTxs, string& intrashardTxs, string& _shuffle_states_contents){
    // 开始将消息转发给下层分片节点
    protos::BatchDistributedTxMsg txs;
    txs.set_coordinatorshardid(coordinator_shardid);
    txs.set_id(_epochId);
    txs.set_txcontents(subTxs);
    txs.set_intrashard_txcontents(intrashardTxs);
    txs.set_shuffle_states_contents(_shuffle_states_contents);

    string serializedTxs;
    txs.SerializeToString(&serializedTxs);
    auto txBytes = asBytes(serializedTxs);

    dev::sync::SyncBatchDistributedTxMsg retPacket;
    retPacket.encode(txBytes);
    auto msg = retPacket.toMessage(m_group_protocolID);
    

    // 加上向自身分片发送交易的逻辑
    if(destshardId == dev::consensus::internal_groupId){
        // 直接发起共识

            PLUGIN_LOG(INFO) << LOG_DESC("收到来自上层分片的批量跨片交易(包含shuffle_states_contents)");
            long unsigned coordinator_shardid = dev::consensus::internal_groupId;//上层id
            long unsigned epochId = _epochId;//
            string txcontents = subTxs;//交易内容
            // string sendedreadwriteset = msg_batchsubCrossShardTxs.tosendreadwriteset();
            string intrashard_txcontents = intrashardTxs; // 转换成的片内交易
            string shuffle_states_contents = _shuffle_states_contents; // 需要shuffle的状态，以及与状态相关的交易
            
            if(txcontents == "") {
                txcontents = "none";
            }
            if(intrashard_txcontents == ""){
                intrashard_txcontents = "none";
            }

            // PLUGIN_LOG(INFO) << LOG_KV("epochId", epochId)
            //                  << LOG_KV("txcontents", txcontents);
            //                  << LOG_KV("sendedreadwriteset", sendedreadwriteset)
            //                  << LOG_KV("intrashard_txcontents", intrashard_txcontents);
            // intrashard_txcontents 样例:
            // 0x222333444|rlp1_rlp2|key1_key2


            string requestLabel = "0x999000111";
            // string data_str = requestLabel + "|" + to_string(coordinator_shardid) + "|" + to_string(epochId) + "#" + txcontents + "#" + intrashard_txcontents + "#" + shuffle_states_contents; // 跨片子交易和重组片内交易之间用"#"号隔开
            // string coordinator_epochId = to_string(coordinator_shardid) + to_string(epochId);
            // PLUGIN_LOG(INFO) << LOG_KV("coordinator_epochId", coordinator_epochId);
            // m_coordinator_epochId2data_str->insert(make_pair(coordinator_epochId, data_str)); // 先把交易的data字段缓存起来，共识只对hash进行共识


            // 转发节点发起交易共识
            if(dev::plugin::hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
                // 将 BatchDistributedTxMsg 中的内容组装到一个交易中, 重组片内交易放在跨片子交易后面
                string requestLabel = "0x999000111";
                string hex_m_testdata_str = requestLabel + "|" + to_string(coordinator_shardid) + "|" + to_string(epochId) + "#" + txcontents + "#" + intrashard_txcontents + "#" + shuffle_states_contents; // 跨片子交易和重组片内交易之间用"#"号隔开
                
                // string hex_m_testdata_str = requestLabel + "|" + to_string(coordinator_shardid) + "|" + to_string(epochId);
                auto testdata_str_bytes = hex_m_testdata_str.c_str();
                int bytelen = strlen(testdata_str_bytes);
                bytes hex_m_testdata;
                hex_m_testdata.clear();
                for(int i = 0; i < bytelen; i++){
                    hex_m_testdata.push_back((uint8_t)testdata_str_bytes[i]);
                }

                std::shared_ptr<Transaction> subintershard_txs = std::make_shared<Transaction>(0, 1000, 0, dev::plugin::depositAddress, hex_m_testdata);
                subintershard_txs->setNonce(generateRandomValue());
                auto keyPair = KeyPair::create();
                auto sig = dev::crypto::Sign(keyPair, subintershard_txs->hash(WithoutSignature));
                subintershard_txs->updateSignature(sig);
                string txrlp = toHex(subintershard_txs->rlp());
                m_rpc_service->sendRawTransaction(dev::consensus::internal_groupId, txrlp);
            }
    }
    else
    {
        m_group_p2p_service->asyncSendMessageByNodeID(dev::plugin::hieraShardTree->get_nodeIDs(destshardId).at(0), msg, nullptr); // 只需要向转发节点发送即可
        // m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((destshardId - 1) * 4 + 0), msg, nullptr); // 只需要向转发节点发送即可

        // for(int k = 0; k < 4; k++) {
        //     // PLUGIN_LOG(INFO) << LOG_KV("向下层分片发送子交易和重组片内交易", shardNodeId.at((destshardId - 1) * 4 + k));
        //     m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((destshardId - 1) * 4 + k), msg, nullptr);
        // }

        // m_group_p2p_service->asyncSendMessageByNodeID(dev::consensus::forwardNodeId.at(destshardId - 1), msg, nullptr);
    }
}

/**
 * NOTES: 向其他分片发送读写集
 * */
void TransactionExecuter::sendReadWriteSet(string& rwKeysToTransfer, string& rwValuesToTransfer, 
                                                string& rwContentionRatesToTransfer, string& participantIds){
    vector<string> idItems;
    boost::split(idItems, participantIds, boost::is_any_of("_"), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中
    int participants_size = idItems.size();

    for(size_t i = 0; i < participants_size; i++) {
        int shardid = atoi(idItems.at(i).c_str());
        if(shardid != dev::consensus::internal_groupId) {
            protos::csTxRWset rwset;
            // rwset.set_crossshardtxid(crossshardtxid);
            rwset.set_readwritekey(rwKeysToTransfer);
            rwset.set_value(rwValuesToTransfer);
            rwset.set_contentionrate(rwContentionRatesToTransfer);
            rwset.set_sourceshardid(dev::consensus::internal_groupId);
            // rwset.set_accessnum(rwContentionRateToTransfer);
            
            string serializedrwset;
            rwset.SerializeToString(&serializedrwset);
            auto msgBytes = asBytes(serializedrwset);

            dev::sync::SyncReadWriteSetMsg retPacket;
            retPacket.encode(msgBytes);
            auto msg = retPacket.toMessage(m_group_protocolID);

            // 准备向对方分片的所有节点广播读写集消息
            h512s nodeIDs = dev::plugin::hieraShardTree->get_nodeIDs(shardid);
            for (int k = 0; k < nodeIDs.size(); k++) {
                m_group_p2p_service->asyncSendMessageByNodeID(nodeIDs.at(k), msg, CallbackFuncWithSession(),dev::network::Options());
            }}
            // for(size_t k = 0; k < 4; k++) {
            //     // PLUGIN_LOG(INFO) << LOG_KV("正在向对方分片的所有节点广播读写集消息", shardNodeId.at((shardid - 1) * 4 + k));
            //     m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((shardid - 1) * 4 + k), msg, CallbackFuncWithSession(), dev::network::Options());
            // }}
    }
}

/** 
 * NOTES: 检查是否收齐了交易的所有读写集，只有rwkeyToReceive中的读写集都收齐了
 * */
bool TransactionExecuter::checkReadWriteKeyReady(vector<string>& rwkeyToReceive, bool lastTxInBatch){
    // 检查rwkeyToReceive中的key在 m_readWriteSetManager->receivedTxRWset 中是否都存在
    bool rwkeyReady = true;
    int rwkey_size = rwkeyToReceive.size();
    for(int i = 0; i < rwkey_size; i++) {
        string key = rwkeyToReceive.at(i);
        rwkeyReady = m_readWriteSetManager->checkReceivedRwset(key);
        if(rwkeyReady == false) {
            // PLUGIN_LOG(INFO) << LOG_KV("缺少的读写集key", key);
            break;
        }
        // if(m_readWriteSetManager->receivedTxRWset->count(key) == 0) {  // 有未收到的读写集
        //     rwkeyReady = false;
        //     // break;
        // }
        // else if(m_readWriteSetManager->receivedTxRWset->at(key) != -1){
        //     m_readWriteSetManager->receivedTxRWset->at(key) = -1; // 置无效
        //     return rwkeyReady;
        // }
    }

    if(rwkeyReady == true && lastTxInBatch == true) { // 所有读写收集完毕，开始对收到的读写集做减法
        int rwkey_size = rwkeyToReceive.size();
        for(int i = 0; i < rwkey_size; i++) {
            string key = rwkeyToReceive.at(i);
            // PLUGIN_LOG(INFO) << LOG_KV("key", key) << LOG_DESC(" in delete...");
            m_readWriteSetManager->deleteRwset(key);
        }
    }
    return rwkeyReady;
}

/** 
 * NOTES: 使用EVMInstance执行交易 shared_ptr<Transaction> tx
 * */
void TransactionExecuter::executeTx(shared_ptr<Transaction> tx, bool deploytx){
    std::lock_guard<std::mutex> lock(executeLock);
    if(deploytx == false){
        tx = simulatedTx;
        // vm = dev::plugin::executiveContext->getExecutiveInstance();
        // exec->setVM(vm);
        // dev::plugin::executiveContext->executeTransaction(exec, tx);
        // dev::plugin::executiveContext->m_vminstance_pool.push(vm);
    }
    else{
        // vm = dev::plugin::executiveContext->getExecutiveInstance();
        exec->setVM(vm);
        // dev::plugin::executiveContext->executeTransaction(exec, tx);
        // dev::plugin::executiveContext->m_vminstance_pool.push(vm);
    }
}

/** 
 * NOTES: 执行部署合约交易
 * */
void TransactionExecuter::executeDeployContractTx(shared_ptr<Transaction> tx){
    try{
        executeTx(tx, true);
        add_processedTxNum();
        // if(get_processedTxNum() % 1000 == 0){
        //     PLUGIN_LOG(INFO) << LOG_DESC("执行部署合约交易");
        //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
        // }

        // intrashardtx_num++;
        // if(intrashardtx_num % 100 == 0) {
        //     PLUGIN_LOG(INFO) << LOG_KV("已经执行的片内交易数目为", intrashardtx_num);
        // }

        // // 查看新部署的合约地址
        // auto exec = getExecutive();
        // auto m_ext = exec->getExt();
        // while(m_ext == nullptr) {
        //     PLUGIN_LOG(INFO) << LOG_DESC("m_ext空指针, 无法执行");
        //     this_thread::sleep_for(chrono::milliseconds(1000)); // 1s重试
        // }

        // string contractAddress = "0x" + m_ext->myAddress().hex();
        // PLUGIN_LOG(INFO) << LOG_KV("新部署的合约地址为", contractAddress);

        // auto destshardid = tx->groupId();
        // if(destshardid == dev::consensus::internal_groupId) {
        //     PLUGIN_LOG(INFO) << LOG_DESC("本分片是合约的主分片");
        //     m_valid_state->insert(make_pair(contractAddress, true)); // 状态有效
        //     m_master_state->insert(make_pair(contractAddress, true)); // 状态的master分片
        // }
        // else {
        //     PLUGIN_LOG(INFO) << LOG_DESC("本分片是合约的副分片");
        //     m_valid_state->insert(make_pair(contractAddress, false));
        //     m_master_state->insert(make_pair(contractAddress, false));
        // }
    }
    catch(const exception& e){
        PLUGIN_LOG(INFO) << LOG_KV("Exception", e.what());
    }
}

/**
 * NOTES:检查已经提交的交易id，并发送给协调者分片的所有节点
 * */
void TransactionExecuter::replyCommitToCoordinator(){
    shared_ptr<map<int, shared_ptr<vector<int>>>> committed_intershardTxid;

    // 简化流程
    if(dev::plugin::hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){ // 由转发节点投递到交易池
        while(true){
            if(isEmpty_committed_intershardTxid() == false){
                get_committed_intershardTxid(committed_intershardTxid);

                // 遍历committed_intershardTxid, 获取要发给不同分片的Txid
                for(auto it = committed_intershardTxid->begin(); it != committed_intershardTxid->end(); it++){
                    string coordinator_shardid = to_string(it->first);
                    auto value_contents = it->second;
                    string intershardTxids = "";
                    int intershardTxid_size = value_contents->size();
                    for(int i = 0; i < intershardTxid_size; i++){
                        int intershardTxid = value_contents->at(i);
                        string flag = "|";
                        if(i == 0){
                            intershardTxids = intershardTxid;
                        }
                        else{
                            intershardTxids = intershardTxids + flag + to_string(intershardTxid);
                        }
                    }

                    // 将 intershardTxids 发送至分片 coordinator_shardid
                    responseCommitToCoordinator(coordinator_shardid, intershardTxids);
                }

                clear_committed_intershardTxid();
                committed_intershardTxid->clear();
            }
            else{
                this_thread::sleep_for(chrono::milliseconds(50));
            }
        }
    }
}



/**
 * NOTES: 依次处理被阻塞的交易, 每当处理完一个来自上层的epoch的交易时，检查是否需要shuffle消息11111
*/

// void TransactionExecuter::processBlockingTxsPlus(){
//     while(true) {
//         bool existBlockedTx = false;
//         int uppershards_size = upper_groupids.size();
//         for(int i = 0; i < uppershards_size; i++){
//             int coordinator_shardid = upper_groupids.at(i);
//             auto blockingTxQueue = m_blockingTxQueues->at(to_string(coordinator_shardid));
//             if(blockingTxQueue->txsize() == 0){
//                 continue;
//             }

//             auto txplusrwset = blockingTxQueue->frontTx();

//             if(txplusrwset != nullptr) {
//                 existBlockedTx = true;
//                 if(txplusrwset->is_intershardTx == true) { // 若交易是跨片子交易
//                     auto tx_hash = txplusrwset->txs.at(0)->hash(); // 用第一笔交易的hash代替
//                     /* 检查是否需要发送读写集, 当前交易需要发送新的读写集=1_1_0x362de179294eb3070a36d13ed00c61f59bcfb542(epochId+batchId+key),
//                     同时检查读写集是否已经发送过，只需要发送一次 */
//                     if(txplusrwset->rwkeysTosend.size() != 0 && m_sendedrwsetTx.count(tx_hash) == 0) {  // 需要发送读写集且没发送过
//                         // PLUGIN_LOG(INFO) << LOG_DESC("发现需要发送读写集的交易");
//                         m_sendedrwsetTx.insert(tx_hash); // 更新 m_sendedrwsetTx
//                         // 提取真实MPT的key以及当前的 value，然后一次性发送出去
//                         string rwKeysToTransfer = ""; // 即将要转发出去的读写集key
//                         string rwValuesToTransfer = ""; // value
//                         string rwContentionRatesToTransfer = "";
//                         map<string, string> rwsetcontentionRates; // 所有读写集在本地的争用率

//                         for(int i = 0; i < txplusrwset->rwkeysTosend.size(); i++) {
//                             string content = txplusrwset->rwkeysTosend.at(i); // 发送的key
//                             std::vector<std::string> dataItems;
//                             boost::split(dataItems, content, boost::is_any_of("_"), boost::token_compress_on);
//                             // string epochId = dataItems.at(0);
//                             // string batchId = dataItems.at(1);
//                             // string key = dataItems.at(2);

//                             string key = dataItems.at(0); // 因为为了避免处理不一致问题, epochID_batchId_keyAddress 临时改为了keyAddress！
//                             string value = getValueByStateAddress(key);
//                             string contentionRate = m_LRU_StatesInfo->getLocalKeyIntraContention(key); // 获取当前key在本地真实的争用率
//                             // PLUGIN_LOG(INFO) << LOG_KV("key", key)
//                             //                 << LOG_KV("contentionRate", contentionRate);
//                             // string contentionRate = "0.1"; // 假设当前key在本地的争用率

//                             if(i == 0) {
//                                 rwKeysToTransfer = content;
//                                 rwValuesToTransfer = value;
//                                 rwContentionRatesToTransfer = contentionRate;
//                             }
//                             else {
//                                 rwKeysToTransfer = rwKeysToTransfer + "|" + content;
//                                 rwValuesToTransfer = rwValuesToTransfer + "|" + value;
//                                 rwContentionRatesToTransfer = rwContentionRatesToTransfer + "|" + contentionRate;
//                             }
//                         }

//                         PLUGIN_LOG(INFO) << LOG_DESC("需要传输的读写集")
//                                         << LOG_KV("rwKeysToTransfer", rwKeysToTransfer)
//                                         << LOG_KV("rwValuesToTransfer", rwValuesToTransfer)
//                                         << LOG_KV("rwContentionRatesToTransfer", rwContentionRatesToTransfer);

//                         // 由转发节点先签名，后发送读写集
//                         if(dev::plugin::nodeIdHex == toHex(dev::consensus::forwardNodeId.at(dev::consensus::internal_groupId - 1))) {
//                             PLUGIN_LOG(INFO) << LOG_DESC("发送读写集...");
//                             string participantIds = txplusrwset->participantIds;
//                             sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
//                         }

//                         // 将积攒的所有读写集发送给其他分片
//                         // string participantIds = txplusrwset->participantIds;
//                         // if(txplusrwset->lastTxInEpoch == true) {
//                         //     this_thread::sleep_for(chrono::milliseconds(50));
//                         //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
//                         // }
//                         // else{
//                         //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
//                         // }
//                     }

//                     // 检查是否收齐读写集，收齐即可立即执行交易，否则继续轮询
//                     bool lastTxInBatch = txplusrwset->lastTxInBatch;
//                     bool executeSucc = checkReadWriteKeyReady(txplusrwset->rwkeyToReceive, lastTxInBatch);

//                     string coordinator_shardid = txplusrwset->coordinator_shardid;
//                     // string intershardtx_id = txplusrwset->intershardTxId;
//                     // PLUGIN_LOG(INFO) << LOG_KV("coordinator_shardid", coordinator_shardid)
//                     //                  << LOG_KV("intershardtxid", intershardtxid);
//                     if(executeSucc == true){
//                         // PLUGIN_LOG(INFO) << LOG_DESC("读写集准备完毕，开始执行交易");
//                         // 读写集准备完毕，开始执行交易
//                         // PLUGIN_LOG(INFO) << LOG_DESC("参与者提交的部分跨片交易")
//                         //                  << LOG_KV("coordinator_shardid", coordinator_shardid)
//                         //                  << LOG_KV("intershardtxid", intershardtxid);
//                         // executeTx(txplusrwset->tx);
//                         executeTx(txplusrwset->tx, false);
//                         executeTx(txplusrwset->tx, false);
//                         // 注：当统计交易延迟的时候，以下步骤不需要执行，需要注释掉


//                         // 临时
//                         add_processedTxNum();
//                         // if(get_processedTxNum() % 1000 == 0) {
//                             // calculateTPS();
//                             // PLUGIN_LOG(INFO) << LOG_DESC("执行跨片交易");
//                             // PLUGIN_LOG(INFO) << LOG_KV("目前已执行的跨片交易总数为", get_processedTxNum());
//                         // }

//                         string txid = txplusrwset->txid;
//                         // 注：当统计交易吞吐的时候，以下步骤不需要执行，需要注释掉
//                         if(dev::consensus::internal_groupId == txplusrwset->minimum_participant_shardid
//                            && dev::plugin::nodeIdHex == toHex(dev::consensus::forwardNodeId.at(dev::consensus::internal_groupId - 1)) ){ // 若本分片为shardid最小的参与方且为转发节点, 那么负责计数（因为其他分片重复执行，只统计一次）

//                             m_processed_intershardTxNum++;
//                             // PLUGIN_LOG(INFO) << LOG_DESC("统计跨片交易") << LOG_KV("m_processed_intershardTxNum", m_processed_intershardTxNum);
                            
//                             // 向 m_committed_intershardTxid 中添加 intershardtxid
//                             int coordinator = atoi(coordinator_shardid.c_str());
//                             if(m_committed_intershardTxid->count(coordinator) != 0){ // 这里有bug, 需要注意并发安全
//                                 auto intershardTxids = m_committed_intershardTxid->at(coordinator);
//                                 intershardTxids->push_back(txid);
//                             }
//                             else{
//                                 auto intershardTxids = make_shared<vector<string>>();
//                                 intershardTxids->push_back(txid);
//                                 m_committed_intershardTxid->insert(make_pair(coordinator, intershardTxids));
//                             }

//                             if(m_processed_intershardTxNum % 10 == 0){ // 每执行1000笔跨片交易，向相应协调者回复交易数目
//                                 // shared_ptr<map<int, shared_ptr<vector<int>>>> committed_intershardTxid;
//                                 // get_committed_intershardTxid(committed_intershardTxid);
//                                 // 遍历committed_intershardTxid, 获取要发给不同分片的Txid
//                                 for(auto it = m_committed_intershardTxid->begin(); it != m_committed_intershardTxid->end(); it++){
//                                     string coordinator_shardid = to_string(it->first);
//                                     auto value_contents = it->second;
//                                     string intershardTxids = "";
//                                     int intershardTxid_size = value_contents->size();
//                                     for(int i = 0; i < intershardTxid_size; i++){
//                                         string intershardTxid = value_contents->at(i);
//                                         string flag = "|";
//                                         if(i == 0){
//                                             intershardTxids = intershardTxid;
//                                         }
//                                         else{
//                                             intershardTxids = intershardTxids + flag + intershardTxid;
//                                         }
//                                     }

//                                     // 将 intershardTxids 发送至分片 coordinator_shardid
//                                     responseCommitToCoordinator(coordinator_shardid, intershardTxids);
//                                 }
//                                 m_committed_intershardTxid->clear();
//                             }
//                         }

//                         // // 检查该笔交易是否为epoch中的最后一笔交易且需要shuffle交易/状态的最新值
//                         // if(txplusrwset->lastTxInEpoch == true && txplusrwset->shuffle_states == true) {
//                         //     PLUGIN_LOG(INFO) << LOG_DESC("准备shuffle副写分片被阻塞的交易/主写分片状态的最新值");
//                         //     auto shuffle_states_contents = txplusrwset->shuffle_states_contents;
//                         //     shuffleStateValue(shuffle_states_contents); // 主分片向副分片发送积攒的最新状态
//                         //     shuffleBlockedTxs(shuffle_states_contents); // 副分片向主分片发送积攒的交易
//                         //     // analyseWorkload(); // 若某个状态频繁与其他分片的状态发生跨片交易，且其他分片对状态的争用较低，则向上层分片请求该状态的“主写权”，实验阶段手动设置
//                         // }
                        
//                         blockingTxQueue->deleteTx(); // 清空该笔交易
//                     }

//                     // 临时补救措施，交易可能因为读写集没有收齐一直阻塞，长时间没有pop强制执行（后面需要完善此功能）
//                     else{

//                         uint64_t last_failture_pop = blockingTxQueue->last_failture_pop;
//                         if(last_failture_pop == 0){
//                             blockingTxQueue->last_failture_pop = utcSteadyTime();
//                         }

//                         else{ // 队首元素上一次pop过但是失败了

//                             int current_time = utcSteadyTime(); // 获取当前时间
//                             if(current_time - last_failture_pop > 1000){
//                                 blockingTxQueue->deleteTx(); // 强制pop
//                                 // PLUGIN_LOG(INFO) << LOG_DESC("启动强制pop...")
//                                 //                 << LOG_KV("last_failture_pop", last_failture_pop)
//                                 //                 << LOG_KV("current_time", current_time);
//                             }
//                             m_processed_intershardTxNum++;
//                         }
//                     }
//                 }
//                 else if(txplusrwset->is_intershardTx == false) { // 下一笔交易为片内交易

//                     // 判断交易占用的阻塞队列个数
//                     auto blockingQueue_keys = txplusrwset->blockingQueue_keys;
//                     int blockingQueue_keys_size = blockingQueue_keys.size();


//                     // // 为了debug，遇到片内交易直接删除
//                     // blockingTxQueue->deleteTx(); // 清空交易

//                     if(blockingQueue_keys.size() == 1){ // 若只占用了1个阻塞队列，按照原先的流程处理
//                         auto txs = txplusrwset->txs;
//                         int txs_size = txs.size(); // 片内交易也可能是多笔子交易
//                         for(int i = 0; i < txs_size; i++) {
//                             auto tx = txs.at(i);
//                             // executeTx(tx);
//                             executeTx(tx, false);
//                         }
//                         add_processedTxNum();
//                         // PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//                         // if(get_processedTxNum() % 1000 == 0) {
//                         //     // calculateTPS();
//                         //     PLUGIN_LOG(INFO) << LOG_DESC("执行被阻塞的片内交易");
//                         //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//                         // }
//                         blockingTxQueue->deleteTx(); // 清空交易
//                     }
//                     else { // 若片内交易只占用了多个阻塞队列，只有所有的相关队列的队首交易都相同时才能够执行

//                         // bool hash_equal = true;
//                         // auto tx_hash = txplusrwset->txs.at(0)->hash(); // 用第一笔交易标记整体hash

//                         // for(int i = 0; i < blockingQueue_keys_size; i++){
//                         //     auto blockingTxQueue = m_blockingTxQueues->at(to_string(blockingQueue_keys.at(i)));

//                         //     auto front_txplusrwset = blockingTxQueue->frontTx();
//                         //     if(front_txplusrwset == nullptr) {
//                         //         hash_equal = false;
//                         //         break;
//                         //     }
//                         //     else if(front_txplusrwset->txs.at(0)->hash() != tx_hash){
//                         //         hash_equal = false;
//                         //         break;
//                         //     }

//                         //     // blockingTxQueue->deleteTx();
//                         // }

//                         // if(hash_equal == true){ // 所有相关的队列首交易都一样，将所有的都删除
//                         //     PLUGIN_LOG(INFO) << LOG_DESC("所有相关的队列首交易都一样，将所有的都删除");
//                         //     int blockingQueue_keys_size = blockingQueue_keys.size(); // 用于应对一笔交易同时阻塞在多个队列中
//                         //     for(int i = 0; i < blockingQueue_keys_size; i++){
//                         //         auto blockingTxQueue = m_blockingTxQueues->at(to_string(blockingQueue_keys.at(i)));
//                         //         blockingTxQueue->deleteTx();
//                         //     }
//                         // }


//                         add_processedTxNum();
//                         // PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//                         // if(get_processedTxNum() % 1000 == 0) {
//                         //     // calculateTPS();
//                         //     PLUGIN_LOG(INFO) << LOG_DESC("执行被阻塞的片内交易");
//                         //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//                         // }




//                         blockingTxQueue->deleteTx(); // 清空交易
//                     }
//                 }
//             }
//         }

        
//         if(existBlockedTx == false) { // 所有队列都不存在被阻塞交易
//             this_thread::sleep_for(chrono::milliseconds(10));
//         }
//     }
// }


void TransactionExecuter::processBlockingTxsPlus(){
    int uppershards_size = upper_groupids.size();
    while(true) {
        bool existBlockedTx = false;
        for(int i = 0; i < uppershards_size; i++){
            int coordinator_shardid = upper_groupids.at(i);

            auto txQueue = m_txQueues->at(to_string(coordinator_shardid));
            if(txQueue->size() == 0){
                // 每当队列中的元素处理完, 尝试从阻塞队列中添加新加入的交易
                auto blockingTxQueue = m_blockingTxQueues->at(to_string(coordinator_shardid)); // 阻塞队列
                blockingTxQueue->batch_pop(txQueue);
            }
            else{
                // 对txQueue中的第一个交易进行处理
                // auto blockingTxQueue = m_blockingTxQueues->at(to_string(coordinator_shardid));
                if(txQueue->size() == 0){
                    continue;
                }

                auto txplusrwset = txQueue->front();
                if(txplusrwset != nullptr) {
                    existBlockedTx = true;
                    if(txplusrwset->is_intershardTx == true) { // 若交易是跨片子交易
                        auto tx_hash = txplusrwset->txs.at(0)->hash(); // 用第一笔交易的hash代替
                        /* 检查是否需要发送读写集, 当前交易需要发送新的读写集=1_1_0x362de179294eb3070a36d13ed00c61f59bcfb542(epochId+batchId+key),
                        同时检查读写集是否已经发送过，只需要发送一次 */
                        if(txplusrwset->rwkeysTosend.size() != 0 && m_sendedrwsetTx.count(tx_hash) == 0) {  // 需要发送读写集且没发送过
                            // PLUGIN_LOG(INFO) << LOG_DESC("发现需要发送读写集的交易");
                            m_sendedrwsetTx.insert(tx_hash); // 更新 m_sendedrwsetTx
                            // 提取真实MPT的key以及当前的 value，然后一次性发送出去
                            string rwKeysToTransfer = ""; // 即将要转发出去的读写集key
                            string rwValuesToTransfer = ""; // value
                            string rwContentionRatesToTransfer = "";
                            map<string, string> rwsetcontentionRates; // 所有读写集在本地的争用率

                            for(int i = 0; i < txplusrwset->rwkeysTosend.size(); i++) {
                                string content = txplusrwset->rwkeysTosend.at(i); // 发送的key
                                std::vector<std::string> dataItems;
                                boost::split(dataItems, content, boost::is_any_of("_"), boost::token_compress_on);
                                // string epochId = dataItems.at(0);
                                // string batchId = dataItems.at(1);
                                // string key = dataItems.at(2);

                                string key = dataItems.at(0); // 因为为了避免处理不一致问题, epochID_batchId_keyAddress 临时改为了keyAddress！
                                string value = getValueByStateAddress(key);
                                string contentionRate = m_LRU_StatesInfo->getLocalKeyIntraContention(key); // 获取当前key在本地真实的争用率
                                // PLUGIN_LOG(INFO) << LOG_KV("key", key)
                                //                 << LOG_KV("contentionRate", contentionRate);
                                // string contentionRate = "0.1"; // 假设当前key在本地的争用率

                                if(i == 0) {
                                    rwKeysToTransfer = content;
                                    rwValuesToTransfer = value;
                                    rwContentionRatesToTransfer = contentionRate;
                                }
                                else {
                                    rwKeysToTransfer = rwKeysToTransfer + "|" + content;
                                    rwValuesToTransfer = rwValuesToTransfer + "|" + value;
                                    rwContentionRatesToTransfer = rwContentionRatesToTransfer + "|" + contentionRate;
                                }
                            }

                            // PLUGIN_LOG(INFO) << LOG_DESC("需要传输的读写集")
                            //                 << LOG_KV("rwKeysToTransfer", rwKeysToTransfer)
                            //                 << LOG_KV("rwValuesToTransfer", rwValuesToTransfer)
                            //                 << LOG_KV("rwContentionRatesToTransfer", rwContentionRatesToTransfer);

                            // 由转发节点先签名，后发送读写集
                            if(dev::plugin::hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) {
                                PLUGIN_LOG(INFO) << LOG_DESC("发送读写集...");
                                string participantIds = txplusrwset->participantIds;
                                sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
                            }

                            // 将积攒的所有读写集发送给其他分片
                            // string participantIds = txplusrwset->participantIds;
                            // if(txplusrwset->lastTxInEpoch == true) {
                            //     this_thread::sleep_for(chrono::milliseconds(50));
                            //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
                            // }
                            // else{
                            //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
                            // }
                        }

                        // 检查是否收齐读写集，收齐即可立即执行交易，否则继续轮询
                        bool lastTxInBatch = txplusrwset->lastTxInBatch;
                        bool executeSucc = checkReadWriteKeyReady(txplusrwset->rwkeyToReceive, lastTxInBatch);

                        string coordinator_shardid = txplusrwset->coordinator_shardid;
                        // string intershardtx_id = txplusrwset->intershardTxId;
                        // PLUGIN_LOG(INFO) << LOG_KV("coordinator_shardid", coordinator_shardid)
                        //                  << LOG_KV("intershardtxid", intershardtxid);
                        if(executeSucc == true){
                            // PLUGIN_LOG(INFO) << LOG_DESC("读写集准备完毕，开始执行交易");
                            // 读写集准备完毕，开始执行交易
                            // PLUGIN_LOG(INFO) << LOG_DESC("参与者提交的部分跨片交易")
                            //                  << LOG_KV("coordinator_shardid", coordinator_shardid)
                            //                  << LOG_KV("intershardtxid", intershardtxid);
                            // executeTx(txplusrwset->tx);
                            executeTx(txplusrwset->tx, false);
                            executeTx(txplusrwset->tx, false);
                            // 注：当统计交易延迟的时候，以下步骤不需要执行，需要注释掉
                            // 临时
                            // add_processedTxNum();
                            // if(get_processedTxNum() % 1000 == 0) {
                                // calculateTPS();
                                // PLUGIN_LOG(INFO) << LOG_DESC("执行跨片交易");
                                // PLUGIN_LOG(INFO) << LOG_KV("目前已执行的跨片交易总数为", get_processedTxNum());
                            // }

                            string txid = txplusrwset->txid;

                            if(dev::consensus::internal_groupId == txplusrwset->minimum_participant_shardid
                            && dev::plugin::hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex) ){ // 若本分片为shardid最小的参与方且为转发节点, 那么负责计数（因为其他分片重复执行，只统计一次）

                                m_processed_intershardTxNum++;
                                // PLUGIN_LOG(INFO) << LOG_DESC("统计跨片交易") << LOG_KV("m_processed_intershardTxNum", m_processed_intershardTxNum);
                                // 向 m_committed_intershardTxid 中添加 intershardtxid

                                // string txid = txids.at(i);
                                struct timeval tv;
                                gettimeofday(&tv, NULL);
                                long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;

                                int coordinator = atoi(coordinator_shardid.c_str());
                                if(m_committed_intershardTxid->count(coordinator) != 0){ // 这里有bug, 需要注意并发安全
                                    auto intershardTxids = m_committed_intershardTxid->at(coordinator);
                                    intershardTxids->push_back(txid + "_" + to_string(time_msec));
                                }
                                else{
                                    auto intershardTxids = make_shared<vector<string>>();
                                    intershardTxids->push_back(txid + "_" + to_string(time_msec));
                                    m_committed_intershardTxid->insert(make_pair(coordinator, intershardTxids));
                                }
                                // todo 这里如果最终的交易总数不是100的倍数，会有一部分交易没法提交
                                // 是否可以考虑加个计时器(?)来使得其超过一定时间后自动将剩下的提交
                                // 现在暂时先改成有就提交
                                // if(m_processed_intershardTxNum % 100 == 0){ // 每执行100笔跨片交易，向相应协调者回复交易数目
                                    // 遍历committed_intershardTxid, 获取要发给不同分片的Txid
                                    for(auto it = m_committed_intershardTxid->begin(); it != m_committed_intershardTxid->end(); it++){
                                        string coordinator_shardid = to_string(it->first);
                                        auto value_contents = it->second;
                                        string intershardTxids = "";
                                        int intershardTxid_size = value_contents->size();
                                        // todo p2p过程中无法与自己通信，如果节点id较小的参与者是协调者自己，那么发送的commit的消息无法被收到和统计
                                        // 此时直接对本地统计数据进行增加
                                        // modify by ZhmYe
                                        if (dev::consensus::internal_groupId == atoi(coordinator_shardid.c_str())) {
                                            // cout << "send commit message to myself..." << endl;
                                            PLUGIN_LOG(INFO) << LOG_DESC("向自己发送commit消息") << LOG_KV("commitToMyself Number: ", intershardTxid_size);
                                            add_processedTxNum(intershardTxid_size);
                                            continue;
                                        }   
                                        for(int i = 0; i < intershardTxid_size; i++){
                                            string intershardTxid = value_contents->at(i);
                                            string flag = "|";
                                            if(i == 0){
                                                intershardTxids = intershardTxid;
                                            }
                                            else{
                                                intershardTxids = intershardTxids + flag + intershardTxid;
                                            }
                                        }

                                        // 将 intershardTxids 发送至分片 coordinator_shardid
                                        responseCommitToCoordinator(coordinator_shardid, intershardTxids);
                                    }
                                    m_committed_intershardTxid->clear();

                                // }
                            }

                            // // 检查该笔交易是否为epoch中的最后一笔交易且需要shuffle交易/状态的最新值
                            // if(txplusrwset->lastTxInEpoch == true && txplusrwset->shuffle_states == true) {
                            //     PLUGIN_LOG(INFO) << LOG_DESC("准备shuffle副写分片被阻塞的交易/主写分片状态的最新值");
                            //     auto shuffle_states_contents = txplusrwset->shuffle_states_contents;
                            //     shuffleStateValue(shuffle_states_contents); // 主分片向副分片发送积攒的最新状态
                            //     shuffleBlockedTxs(shuffle_states_contents); // 副分片向主分片发送积攒的交易
                            //     // analyseWorkload(); // 若某个状态频繁与其他分片的状态发生跨片交易，且其他分片对状态的争用较低，则向上层分片请求该状态的“主写权”，实验阶段手动设置
                            // }
                            // blockingTxQueue->deleteTx(); // 清空该笔交易
                            txQueue->pop();
                        }

                        // // 临时补救措施，交易可能因为读写集没有收齐一直阻塞，长时间没有pop强制执行（后面需要完善此功能）
                        // else{
                        //     uint64_t last_failture_pop = blockingTxQueue->last_failture_pop;
                        //     if(last_failture_pop == 0){
                        //         blockingTxQueue->last_failture_pop = utcSteadyTime();
                        //     }
                        //     else{ // 队首元素上一次pop过但是失败了
                        //         int current_time = utcSteadyTime(); // 获取当前时间
                        //         if(current_time - last_failture_pop > 1000){
                        //             blockingTxQueue->deleteTx(); // 强制pop
                        //             // PLUGIN_LOG(INFO) << LOG_DESC("启动强制pop...")
                        //             //                 << LOG_KV("last_failture_pop", last_failture_pop)
                        //             //                 << LOG_KV("current_time", current_time);
                        //         }
                        //         m_processed_intershardTxNum++;
                        //     }
                        // }
                    }
                    else if(txplusrwset->is_intershardTx == false) { // 下一笔交易为片内交易

                        // 判断交易占用的阻塞队列个数
                        auto blockingQueue_keys = txplusrwset->blockingQueue_keys;
                        int blockingQueue_keys_size = blockingQueue_keys.size();

                        // // 为了debug，遇到片内交易直接删除
                        // blockingTxQueue->deleteTx(); // 清空交易

                        if(blockingQueue_keys.size() == 1){ // 若只占用了1个阻塞队列，按照原先的流程处理
                            auto txs = txplusrwset->txs;
                            int txs_size = txs.size(); // 片内交易也可能是多笔子交易
                            for(int i = 0; i < txs_size; i++) {
                                auto tx = txs.at(i);
                                // executeTx(tx);
                                executeTx(tx, false);
                            }
                            add_processedTxNum();

                            string txid = txplusrwset->txid;
                            struct timeval tv;
                            gettimeofday(&tv, NULL);
                            long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;
                            m_txid_to_endtime->insert(make_pair(txid, time_msec)); // 记录txid的开始时间
                            // PLUGIN_LOG(INFO) << LOG_KV("commit txid", txid)
                            //                 << LOG_KV("time_msec", time_msec);

                            // PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
                            // if(get_processedTxNum() % 1000 == 0) {
                            //     // calculateTPS();
                            //     PLUGIN_LOG(INFO) << LOG_DESC("执行被阻塞的片内交易");
                            //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
                            // }
                            // blockingTxQueue->deleteTx(); // 清空交易
                            txQueue->pop();

                        }
                        else { // 若片内交易只占用了多个阻塞队列，只有所有的相关队列的队首交易都相同时才能够执行

                            // bool hash_equal = true;
                            // auto tx_hash = txplusrwset->txs.at(0)->hash(); // 用第一笔交易标记整体hash

                            // for(int i = 0; i < blockingQueue_keys_size; i++){
                            //     auto blockingTxQueue = m_blockingTxQueues->at(to_string(blockingQueue_keys.at(i)));

                            //     auto front_txplusrwset = blockingTxQueue->frontTx();
                            //     if(front_txplusrwset == nullptr) {
                            //         hash_equal = false;
                            //         break;
                            //     }
                            //     else if(front_txplusrwset->txs.at(0)->hash() != tx_hash){
                            //         hash_equal = false;
                            //         break;
                            //     }

                            //     // blockingTxQueue->deleteTx();
                            // }

                            // if(hash_equal == true){ // 所有相关的队列首交易都一样，将所有的都删除
                            //     PLUGIN_LOG(INFO) << LOG_DESC("所有相关的队列首交易都一样，将所有的都删除");
                            //     int blockingQueue_keys_size = blockingQueue_keys.size(); // 用于应对一笔交易同时阻塞在多个队列中
                            //     for(int i = 0; i < blockingQueue_keys_size; i++){
                            //         auto blockingTxQueue = m_blockingTxQueues->at(to_string(blockingQueue_keys.at(i)));
                            //         blockingTxQueue->deleteTx();
                            //     }
                            // }


                            add_processedTxNum();
                            string txid = txplusrwset->txid;
                            struct timeval tv;
                            gettimeofday(&tv, NULL);
                            long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;
                            m_txid_to_endtime->insert(make_pair(txid, time_msec)); // 记录txid的开始时间

                            // PLUGIN_LOG(INFO) << LOG_KV("commit txid", txid)
                            //                 << LOG_KV("time_msec", time_msec);


                            // PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
                            // if(get_processedTxNum() % 1000 == 0) {
                            //     // calculateTPS();
                            //     PLUGIN_LOG(INFO) << LOG_DESC("执行被阻塞的片内交易");
                            //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
                            // }

                            // blockingTxQueue->deleteTx(); // 清空交易
                            txQueue->pop();
                        }
                    }
                }
            }
        }

        if(existBlockedTx == false) { // 所有队列都不存在被阻塞交易
            this_thread::sleep_for(chrono::milliseconds(10));
        }
    }
}

/**
 * NOTES: // 分析近段时间内负载是否出现了局部性特征
 * */
void TransactionExecuter::analyseWorkload(){
    // 处理步骤:
    // 1. 遍历本epoch内发生高频跨片交易的本地状态(X)
    // 2. 收集与(X)发生跨片交易的其他分片状态及其在原分片的争用率Y
    // 3. 比较Y与X在本地争用的情况，若X>>Y, 将Y的写权限迁移到本分片中

    PLUGIN_LOG(INFO) << LOG_DESC("In analyseWorkload.....");
    map<string, string> requestState; // 分片需要请求写权限的状态，key为状态的地址，value为当前写权限所在的分片
    auto localkeysAccessedByInterTx = *(m_LRU_StatesInfo->m_LRU_localkeys_interTx_summarized); // 发生跨片交易的本地状态信息，使用深拷贝，避免线程不安全

    for(auto summaryItem = localkeysAccessedByInterTx.begin(); summaryItem != localkeysAccessedByInterTx.end(); summaryItem++) {
        string localKey = summaryItem->first; // 发生跨片交易的本地状态
        string localKeyContention = m_LRU_StatesInfo->getLocalKeyIntraContention(localKey); // 查看localKey在本分片的争用率

        auto item = summaryItem->second;
        for(auto it = item->begin(); it != item->end(); it++){
            string remoteKey = it->first;  // 与本地状态localKey发生跨片交易的其他分片状态
            int accessednumber = it->second; // localKey与remoteKey发生的跨片交易次数

            string shardid = "";
            string remoteKeyContention = "";
            m_LRU_StatesInfo->getRemoteKeyIntraContention(shardid, remoteKeyContention, remoteKey); // remoteKey在原分片中的争用率

            if(remoteKeyContention == "none") { continue; } // 还未收到读写集，暂时忽略

            PLUGIN_LOG(INFO) << LOG_KV("本地状态localkey", localKey)
                             << LOG_KV("localkey在本分片中的争用率为", localKeyContention)
                             << LOG_KV("与其发生跨片交易的状态remotekey", remoteKey)
                             << LOG_KV("localkey与remotekey发生跨片交易次数为", accessednumber)
                             << LOG_KV("remotekey在原分片中的争用率为", remoteKeyContention);

            int localKeyContention_No = atoi(localKeyContention.c_str());
            int remoteKeyContention_No = atoi(remoteKeyContention.c_str());

            // 本地高争用状态 && 与 remoteKey 是 localKey 的高频跨片交易状态 && remoteKey在原分片是低争用状态 && 之前未请求过该状态(保证相同状态不会被重复请求，暂时不考虑状态转移回原地的情况)
            if(localKeyContention_No > 200 && accessednumber > 100 && remoteKeyContention_No < 100 && requestedStates.count(remoteKey) == 0) {
            	requestedStates.insert(remoteKey);
                requestState.insert(make_pair(remoteKey, shardid)); // 将需要进行状态转移的状态进行缓存，其中key为需要转移的状态，value为状态此时所在的分片
                PLUGIN_LOG(INFO) << LOG_DESC("添加需要请求写权限的状态")
                 				 << LOG_KV("remoteKey", remoteKey)
                 				 << LOG_KV("shardid", shardid);
            }
    }}

    if(requestState.size() != 0){ // 存在需要修改写权限的状态
    	// 缓存需要发送给不同公共祖先的状态更改请求
        map<string, vector<string>> cachedRequest;
        // 将 requestState 中的状态写权限转移到目标分片中
        for(auto it = requestState.begin(); it != requestState.end(); it++){
            string key = it->first;
            string localshardid = to_string(dev::consensus::internal_groupId);
            string destinshardid = it->second;
            PLUGIN_LOG(INFO) << LOG_DESC("发现新的需要被转移状态")
	                         << LOG_KV("requestkey", key)
	                         << LOG_KV("localshardid", localshardid)
	                         << LOG_KV("destinshard", destinshardid);

            Hierarchical_Shard::Ptr localshard = m_hierarchical_structure->shards->at(atoi(localshardid.c_str()) - 1);
            Hierarchical_Shard::Ptr destinshard = m_hierarchical_structure->shards->at(atoi(destinshardid.c_str()) - 1);
            auto shard = localshard->getLCA(localshard, destinshard);
            PLUGIN_LOG(INFO) << LOG_KV("最近公共祖先分片为", shard->id);

            // 整理请求
            string ancestorgroupid = to_string(shard->id);
            string requestcontent = ancestorgroupid + "|" + localshardid + "|" + destinshardid + "|" + key + "|" + to_string(stateChangeRequestId); // stateChangeRequestId并无实际用

            if(cachedRequest.count(destinshardid) != 0){
                cachedRequest.at(destinshardid).push_back(requestcontent);
            }
            else {
                vector<string> requests;
                requests.push_back(requestcontent);
                cachedRequest.insert(make_pair(destinshardid, requests));
            }
        }

        // 遍历所有积攒的状态请求
        for(auto it = cachedRequest.begin(); it != cachedRequest.end(); it++){
            string destinshardid = it->first;
            vector<string> requests = it->second;
            batchRequestForMasterChange(requests);
            stateChangeRequestId++;
        }
    }
}

/**
 * NOTES: // 作为状态此时的主写分片向副写分片分享状态的最新值
 * */
void TransactionExecuter::shuffleStateValue(string& shuffle_states_contents){

        masterwriteBlocked = true;





        map<string, int> destinshardids;
        get_shuffleValue_destshardid(&destinshardids);
        map<int, shared_ptr<vector<string>>> shardid_2_shuffle_states; // 记录需要分享给不同分片的状态值
        
        // 根据 shuffle_states_contents 和 destinshardids 获取需要分享给不同分片的不同状态最新值
        vector<string> statesItems;
        boost::split(statesItems, shuffle_states_contents, boost::is_any_of("|"), boost::token_compress_on);
        for(string &s: statesItems){
            int shardid = destinshardids.at(s); // 获取s的目标分片id
            if(shardid_2_shuffle_states.count(shardid) != 0){
                auto shuffle_states = shardid_2_shuffle_states.at(shardid);
                shuffle_states->push_back(s);
            }
            else{
                auto shuffle_states = make_shared<vector<string>>();
                shuffle_states->push_back(s);
                shardid_2_shuffle_states.insert(make_pair(shardid, shuffle_states));
            }
        }

        // 遍历 shardid_2_shuffle_states, 将最新状态的值分享给相应分片，key为目标分片id, value为状态的statekey(value先假定为1，后面完善时用真实值)
        for(auto it = shardid_2_shuffle_states.begin(); it != shardid_2_shuffle_states.end(); it++){
            string shardid = to_string(it->first);
            auto shuffle_states = it->second;
            int state_size = shuffle_states->size();

            string statekeys = "";
            string statevalues = "";
            string flag = "|";
            for(int i = 0; i < state_size; i++){
                if(i == 0){
                    statekeys = shuffle_states->at(i);
                    statevalues = "0";
                }
                else{
                    statekeys += flag + shuffle_states->at(i);
                    statevalues += flag + "0";
                }
            }

            // 将 statekeys 和 statevalues 发送给分片shardid
            // 将积攒的状态发送给相应分片的所有节点
            protos::ShuffleStateValue shuffleMsg;
            shuffleMsg.set_stateaddresses(statekeys);
            shuffleMsg.set_values(statevalues);
            shuffleMsg.set_sourceshardid(shardid);

            string serializedShuffleMsg;
            shuffleMsg.SerializeToString(&serializedShuffleMsg);
            auto msgBytes = asBytes(serializedShuffleMsg);

            dev::sync::SyncShuffleStateValueMsg retPacket;
            retPacket.encode(msgBytes);
            auto msg = retPacket.toMessage(m_group_protocolID);

            if(dev::plugin::hieraShardTree->is_forward_node(dev::consensus::internal_groupId, nodeIdHex)) {
                h512s nodeIDs = dev::plugin::hieraShardTree->get_nodeIDs(atoi(shardid.c_str()));
                for (int k = 0; k < nodeIDs.size(); k++) {
                    PLUGIN_LOG(INFO) << LOG_KV("向副写分片发送状态最新值", nodeIDs.at(k));
                    m_group_p2p_service->asyncSendMessageByNodeID(nodeIDs.at(k), msg, CallbackFuncWithSession(),dev::network::Options());
                }
                // for(int k = 0; k < 4; k++) {
                //     PLUGIN_LOG(INFO) << LOG_KV("向副写分片发送状态最新值", shardNodeId.at((atoi(shardid.c_str()) - 1) * 4 + k));
                //     m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((atoi(shardid.c_str()) - 1) * 4 + k), msg, CallbackFuncWithSession(), dev::network::Options());
                // }
            }
        }








        // if(destinshardids.size() != 0){ // 存在需要 shuffle value 的分片，执行以下步骤
        //     // 获取需要发送最新值的状态
        //     auto stateValues = make_shared<map<string, shared_ptr<map<string, string>>>>(); // <shardid, <key,value>>
        //     for(auto it = m_addedTransferedStates.begin(); it != m_addedTransferedStates.end(); it++){
        //         string statekey = it->first;
        //         string shardid = it->second;
        //         string value = "testValue"; // 模拟获取状态的本地最新值, 真实值应该是: string value = getValueByStateAddress(rwkey);

        //         if(stateValues->count(shardid) != 0){
        //             auto item = stateValues->at(shardid);
        //             item->insert(make_pair(statekey, value));
        //         }
        //         else{
        //             auto item = make_shared<map<string, string>>();
        //             item->insert(make_pair(statekey, value));
        //             stateValues->insert(make_pair(shardid, item));
        //         }
        //     }

        //     // 将stateValues中记录的最新值发送给相应的分片
        //     for(auto it = stateValues->begin(); it != stateValues->end(); it++) {
        //         int shardid = atoi((it->first).c_str());
        //         auto item = it->second;

        //         // 遍历需要发送给 shardid 的所有状态及其 value
        //         int state_number = 0;
        //         string statekeys = "";
        //         string statevalues = "";
        //         string flag = "|";

        //         for(auto itemit = item->begin(); itemit != item->end(); itemit++) {
        //             string key = itemit->first;
        //             string value = itemit->second;

        //             if(state_number == 0){
        //                 statekeys = key;
        //                 statevalues = value;
        //             }
        //             else{
        //                 statekeys = statekeys + flag + key;
        //                 statevalues = statevalues + flag + value;
        //             }
        //         }

        //         // 将积攒的状态发送给相应分片的所有节点
        //         protos::ShuffleStateValue shuffleMsg;
        //         shuffleMsg.set_stateaddresses(statekeys);
        //         shuffleMsg.set_values(statevalues);

        //         string serializedShuffleMsg;
        //         shuffleMsg.SerializeToString(&serializedShuffleMsg);
        //         auto msgBytes = asBytes(serializedShuffleMsg);

        //         dev::sync::SyncShuffleStateValueMsg retPacket;
        //         retPacket.encode(msgBytes);
        //         auto msg = retPacket.toMessage(m_group_protocolID);

        //         if(nodeIdHex == toHex(forwardNodeId.at(internal_groupId - 1))) { // 只向转发节点发送
        //             m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((shardid - 1) * 4), msg, CallbackFuncWithSession(), dev::network::Options());
        //             // for(int k = 0; k < 4; k++) {
        //             //     // PLUGIN_LOG(INFO) << LOG_KV("向副写分片发送状态最新值", shardNodeId.at((shardid - 1) * 4 + k));
        //             //     m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((shardid - 1) * 4 + k), msg, CallbackFuncWithSession(), dev::network::Options());
        //             // }
        //         }
        //     }
        // }
}

/**
 * NOTES: // 分片将作为副写分片时阻塞的与shuffle_states_contents相关的交易转发给相应的主写分片
 * */
void TransactionExecuter::shuffleBlockedTxs(string& shuffle_states_contents){

    vicewriteBlocked = true;
    map<string, int> destinshardids;
    get_shuffleTxRLP_destshardid(&destinshardids); // statekey --> shardid

    // // // 遍历输出 destinshardids 中的元素
    // // for(set<int>::iterator it = destinshardids.begin(); it != destinshardids.end(); it++) {
    // //     PLUGIN_LOG(INFO) << LOG_KV("m_shuffleTxRLP_destshardid item", *it);
    // // }

    vector<string> shuffle_states_items;
    boost::split(shuffle_states_items, shuffle_states_contents, boost::is_any_of("|"), boost::token_compress_on);
    vector<int> shardids;
    int shuffle_states_size = shuffle_states_items.size();
    for(int i = 0; i < shuffle_states_size; i++){
        string statekey = shuffle_states_items.at(i);
        int shardid = destinshardids.at(statekey);
        shardids.push_back(shardid);
    }

    int shardids_size = shardids.size();
    for(int i = 0; i < shardids_size; i++) {
        int shardid = shardids.at(i);
        string txrlps = ""; // 给予空交易

        // 发送给相应分片的所有节点
        protos::ShuffleTxRlps shuffleTxMsg;
        shuffleTxMsg.set_txrlps(txrlps);

        string serializedshuffleTxMsg;
        shuffleTxMsg.SerializeToString(&serializedshuffleTxMsg);
        auto msgBytes = asBytes(serializedshuffleTxMsg);

        dev::sync::SyncShuffleTxRlpsMsg retPacket;
        retPacket.encode(msgBytes);
        auto msg = retPacket.toMessage(m_group_protocolID);

        if(dev::plugin::hieraShardTree->is_forward_node(dev::consensus::internal_groupId, nodeIdHex)) {
            h512s nodeIDs = dev::plugin::hieraShardTree->get_nodeIDs(shardid);
            for (int k = 0; k < nodeIDs.size(); k++) {
                PLUGIN_LOG(INFO) << LOG_KV("向副写分片发送状态最新值", nodeIDs.at(k));
                m_group_p2p_service->asyncSendMessageByNodeID(nodeIDs.at(k), msg, CallbackFuncWithSession(),dev::network::Options());
            }
            // for(int k = 0; k < 4; k++) {
            //     PLUGIN_LOG(INFO) << LOG_KV("向主写分片发送阻塞的交易", shardNodeId.at((shardid - 1) * 4 + k));
            //     m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((shardid - 1) * 4 + k), msg, CallbackFuncWithSession(), dev::network::Options());
            // }
        }
    }



    // int current_blockedTx_size = blockedTxs.size();
    // if(current_blockedTx_size != 0){
    //     map<string, string> blockedTxsRlps; // 从队列 blockedTxs 中取出目前为止被阻塞的全部交易（其实只要提取出与阻塞状态相关的交易就行，需要制作交易依赖图，这里先简化）,并更新 other_blocked_states
    //     string blocked_txrlps = ""; // 记录blockedTxs中当前被阻塞的交易rlp
    //     for(int i = 0; i < current_blockedTx_size; i++){
    //         auto item = blockedTxs.front(); // vector<txrlp, shardid, shardid...>
    //         string txrlp = item.at(0);
    //         vector<string> shardids;
    //         int item_size = item.size();
    //         for(int i = 1; i < item_size; i++){
    //             shardids.push_back(item.at(i));
    //         }
    //         blockedTxs.dequeue();

    //         // 依次将准备发送给不同分片的交易rlp缓存下来
    //         int shard_size = shardids.size();
    //         for(int j = 0; j < shard_size; j++){
    //             string shardid = shardids.at(j);
    //             if(blockedTxsRlps.count(shardid) != 0){
    //                 string txrlps = blockedTxsRlps.at(shardid);
    //                 txrlps = txrlps + "_" + txrlp;
    //                 blockedTxsRlps.insert(make_pair(shardid, txrlps));
    //             }
    //             else{
    //                 blockedTxsRlps.insert(make_pair(shardid, txrlp));
    //             }
    //         }

    //         // 将交易占用的读写集从 other_blocked_states 中删除
    //         Transaction::Ptr tx = std::make_shared<Transaction>(
    //             jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);

    //         string rwkeys = ""; // 后面从交易中解析出来获得
    //         vector<string> rwkeyitems;
    //         boost::split(rwkeyitems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);
    //         int rwkey_size = rwkeyitems.size();  // 将交易占用的读写集删除
    //         for(int i = 0; i < rwkey_size; i++){
    //             string rwkey = rwkeyitems.at(i);
    //             delete_otherState(rwkey);
    //         }

    //         // 更新 blocked_txrlps
    //         if(i == 0){
    //             blocked_txrlps = txrlp;
    //         }
    //         else{
    //             blocked_txrlps = blocked_txrlps + "_" + txrlp;
    //         }
    //     }
    //     add_blocked_originTxRlps(blocked_txrlps);

    //     if(blockedTxsRlps.size() != 0){
    //         // 将积攒的交易rlp发送给相应的分片
    //         for(set<int>::iterator it = destinshardids.begin(); it != destinshardids.end(); it++) {
    //             string shardid = to_string(*it);
    //             string txrlps = "";

    //             if(blockedTxsRlps.count(shardid) != 0){ // 若shardid的key不存在, 则txrlps继续只保留空
    //                 txrlps = blockedTxsRlps.at(shardid);
    //             }

    //             // 将积攒的状态发送给相应分片的所有节点
    //             protos::ShuffleTxRlps shuffleTxMsg;
    //             shuffleTxMsg.set_txrlps(txrlps);

    //             string serializedshuffleTxMsg;
    //             shuffleTxMsg.SerializeToString(&serializedshuffleTxMsg);
    //             auto msgBytes = asBytes(serializedshuffleTxMsg);

    //             dev::sync::SyncShuffleTxRlpsMsg retPacket;
    //             retPacket.encode(msgBytes);
    //             auto msg = retPacket.toMessage(m_group_protocolID);

    //             if(nodeIdHex == toHex(forwardNodeId.at(internal_groupId - 1))) { // 转发节点负责发送消息
    //                 for(int k = 0; k < 4; k++) {
    //                     PLUGIN_LOG(INFO) << LOG_KV("向主写分片发送阻塞的交易", shardNodeId.at((atoi(shardid.c_str()) - 1) * 4 + k));
    //                     m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((atoi(shardid.c_str()) - 1) * 4 + k), msg, CallbackFuncWithSession(), dev::network::Options());
    //                 }
    //             }
    //         }
    //     }
    //     else if(blockedTxsRlps.size() == 0){ // 这段时期内无积攒的交易，则发空消息给所有主写分片

            // for(auto it = destinshardids.begin(); it != destinshardids.end(); it++) {
            //     int shardid = *it;
            //     string txrlps = ""; // 给予空交易

            //     // 发送给相应分片的所有节点
            //     protos::ShuffleTxRlps shuffleTxMsg;
            //     shuffleTxMsg.set_txrlps(txrlps);

            //     string serializedshuffleTxMsg;
            //     shuffleTxMsg.SerializeToString(&serializedshuffleTxMsg);
            //     auto msgBytes = asBytes(serializedshuffleTxMsg);

            //     dev::sync::SyncShuffleTxRlpsMsg retPacket;
            //     retPacket.encode(msgBytes);
            //     auto msg = retPacket.toMessage(m_group_protocolID);

            //     if(nodeIdHex != toHex(forwardNodeId.at(internal_groupId - 1))) {
            //         for(int k = 0; k < 4; k++) {
            //             PLUGIN_LOG(INFO) << LOG_KV("向主写分片发送阻塞的交易", shardNodeId.at((shardid - 1) * 4 + k));
            //             m_group_p2p_service->asyncSendMessageByNodeID(shardNodeId.at((shardid - 1) * 4 + k), msg, CallbackFuncWithSession(), dev::network::Options());
            //         }
            //     }
            // }
    //     }
    // }

}



/**
 * NOTES: 处理来自主分片状态分享交易
 * */
void TransactionExecuter::processShuffleValueTx(shared_ptr<Transaction> tx){

    // 解析主写分片分享的statekey的最新值
    string data_str = dataToHexString(tx->get_data());
    vector<string> dataItems;
    boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中

    string stateKeys = dataItems.at(1);
    string stateValues = dataItems.at(2); // 将stateKeys中包含的key的最新值更新为stateValues中相应的值
    int sourceshardid = atoi(dataItems.at(3).c_str());

    // 随后，副写分片立即执行被阻塞的交易(也就是之前传输给主分片的交易)
    /*
        Attention Please！
        这里还需要更进一步对阻塞的交易进行区分，构造交易依赖图，将与之前状态相关的交易执行掉)
        这里先就一次性执行完所有的，张焕、真瑜靠你们了！)
    */

    // 当收到主副本发来的最新状态时，立即执行被阻塞的交易(也就是之前传输给主分片的交易)
    if(size_blocked_originTxRlps() > 0){
        string blockedTxRlps = front_blocked_originTxRlps();
        vector<string> txrlps;
        boost::split(txrlps, blockedTxRlps, boost::is_any_of("_"), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中
        int tx_size = txrlps.size();
        // PLUGIN_LOG(INFO) << LOG_KV("tx_size",tx_size)
        //                  << LOG_KV("blockedTxRlps",blockedTxRlps);
        if(tx_size > 1){
            for(int i = 0; i < tx_size; i++){
                Transaction::Ptr tx = std::make_shared<Transaction>(
                        jsToBytes(txrlps.at(i), OnFailed::Throw), CheckTransaction::Everything);

                executeTx(tx, false);
                // processedTxNum++;
                // if(processedTxNum % 1000 == 0){
                //     calculateTPS();
                //     PLUGIN_LOG(INFO) << LOG_KV("执行因状态被迁移而被阻塞的交易，目前已执行的交易总数为", processedTxNum);
                // }
            }
        }
        pop_blocked_originTxRlps();
    }
    vicewriteBlocked = false; // 当交易执行结束时将 vicewriteBlocked 置false
}

/**
 * NOTES: 副分片交易分享交易
 * */
void TransactionExecuter::processShuffleTx(shared_ptr<Transaction> tx){
    string data_str = dataToHexString(tx->get_data());
    vector<std::string> dataitems;
    boost::split(dataitems, data_str, boost::is_any_of("|"), boost::token_compress_on);
    string blocked_txRLPs = dataitems.at(1);

    vector<string> txRLPs;
    boost::split(txRLPs, blocked_txRLPs, boost::is_any_of("_"), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中
    int tx_size = txRLPs.size();

    if(tx_size > 1){
        for(int i = 0; i < tx_size; i++){
            string txrlp = txRLPs.at(i);
            // PLUGIN_LOG(INFO) << LOG_KV("txrlp", txrlp);
            Transaction::Ptr tx = std::make_shared<Transaction>(
                jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
            executeTx(tx, false);
            add_processedTxNum();
            // if(get_processedTxNum() % 1000 == 0){
            //     // calculateTPS();
            //     PLUGIN_LOG(INFO) << LOG_DESC("执行副写分片分享来的被阻塞交易");
            //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
            // }
        }
    }

    // 当执行完收到的阻塞交易后，将 masterwriteBlocked 的值设置为false，此时被阻塞的交易可以执行
    masterwriteBlocked = false;

    // 当收到来自其他分片阻塞的交易时，处理前一批被阻塞的交易
    if(size_blocked_intraTxContent() > 0){
        string convertedIntraTxContents = front_blocked_intraTxContent();
        vector<string> convertedIntraTxs;
        boost::split(convertedIntraTxs, convertedIntraTxContents, boost::is_any_of("&"), boost::token_compress_on);
        int tx_size = convertedIntraTxs.size();
        PLUGIN_LOG(INFO) << LOG_DESC("开始处理之前阻塞的重组片内交易") << LOG_KV("交易数目为", tx_size);

        if(tx_size > 1){
            for(int i = 0; i < tx_size; i++){
                string intraTxContent = convertedIntraTxs.at(i);
                vector<string> contentItems;
                boost::split(contentItems, intraTxContent, boost::is_any_of("|"), boost::token_compress_on);
                string txrlps = contentItems.at(0);
                string rwkeys = contentItems.at(1);

                vector<string> txrlpItems; // txrlps --> tx
                boost::split(txrlpItems, txrlps, boost::is_any_of("_"), boost::token_compress_on);

                vector<string> keyItems; // 记录转换后的片内访问的片内状态
                boost::split(keyItems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);
                int rwkeys_size = keyItems.size();
                for(int i = 0; i < rwkeys_size; i++) {
                    string localkey = keyItems.at(i);
                    m_LRU_StatesInfo->update_lru_localkeys_intraTx(localkey);
                }

                bool isblocked = false; // 检查当前交易的读写集key是否被阻塞，若交易的读写集未被阻塞, 立即执行交易，否则缓存交易
                int rwkey_size = keyItems.size();
                for(int i = 0; i < rwkey_size; i++) {
                    string rwkey = keyItems.at(i);
                    // if(m_blockingTxQueue->isBlocked(rwkey) == true) { //此处有bug
                    //     isblocked = true;
                    //     break;
                    // }
                
                }

                vector<Transaction::Ptr> subtxs;
                for(int i = 0; i < txrlpItems.size(); i++) {
                    auto tx = make_shared<Transaction>(jsToBytes(txrlpItems.at(i), OnFailed::Throw), CheckTransaction::Everything); // 包装成交易
                    subtxs.push_back(tx);
                }
                if(isblocked == false) // 若交易的读写集未被阻塞, 立即执行交易
                {
                    // PLUGIN_LOG(INFO) << LOG_DESC("重组交易未被阻塞, 立即执行");
                    int subtxs_size = subtxs.size();
                    for(size_t i = 0; i < subtxs_size; i++) {
                        auto tx = subtxs.at(i);
                        // m_transactionExecuter->executeTx(tx); // 执行所有子交易
                        executeTx(tx, false);
                    }

                    add_processedTxNum();
                    // PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
                    // if(get_processedTxNum() % 1000 == 0){
                    //     // calculateTPS();
                    //     PLUGIN_LOG(INFO) << LOG_DESC("执行被阻塞的重组片内交易");
                    //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
                    // }
                }
                else { // 交易被阻塞，缓存交易
                    // PLUGIN_LOG(INFO) << LOG_DESC("重组交易被阻塞，开始缓存");
                    auto txwithrwset = std::make_shared<dev::plugin::transactionWithReadWriteSet>();
                    txwithrwset->rwkeys = rwkeys;
                    int subtxs_size = subtxs.size();
                    for(int i = 0; i < subtxs_size; i++) {
                        auto tx = subtxs.at(i);
                        txwithrwset->txs.push_back(tx);
                    }
                    txwithrwset->is_intershardTx = false;
                    // m_blockingTxQueue->insertTx(txwithrwset); // 此处可能有bug，需再检查
                }
            }
        }
        pop_blocked_intraTxContent();
    }
}


/**
 * NOTES: 设置_blockchainManager指针
 * */
void TransactionExecuter::setAttribute(shared_ptr<dev::blockchain::BlockChainInterface> _blockchainManager){
    m_blockchainManager = _blockchainManager;
}

/**
 * NOTES: 计算分片发出的交易平均延迟
 * */
void TransactionExecuter::average_latency(){
    long totalTime = 0;
    int totalTxNum = 0;

    // 计算交易平均延时
    for(auto iter = m_txid_to_endtime->begin(); iter != m_txid_to_endtime->end(); iter++){
        string txid = iter->first;
        long endtime = iter->second;
        long starttime = 0;
        if(m_txid_to_starttime->count(txid) != 0){
            starttime = m_txid_to_starttime->at(txid);
            long interval = endtime - starttime;
            totalTime = totalTime + interval;
            totalTxNum++;
        }
    }
    double avgLatency = double(totalTime) / double(totalTxNum) * 0.001;
    PLUGIN_LOG(INFO) << LOG_KV("total_txNum", totalTxNum)
                        << LOG_KV("total_time", totalTime)
                        << LOG_KV("average_latency", avgLatency);
    cout << "average_latency=" << avgLatency << endl;
}


// /** 
//  * NOTES:计算本分片的交易TPS
//  * */
// void TransactionExecuter::calculateTPS()
// {
//     // if(get_processedTxNum() == startProcessedNum)
//     // {
//     //     time(&start); // 开始时间
//     // }
//     // else if(get_processedTxNum() == endProcessedNum)
//     // {
//     //     time(&end); // 结束时间
//     //     time_t interval = end - start;
//     //     double tps = (endProcessedNum - startProcessedNum) / interval;
//     //     double latency = (double)interval / (endProcessedNum - startProcessedNum);
//     //     PLUGIN_LOG(INFO) << LOG_KV("startProcessedNum", startProcessedNum)
//     //                      << LOG_KV("endProcessedNum", endProcessedNum)
//     //                      << LOG_KV("endTime", end)
//     //                      << LOG_KV("startTime", start)
//     //                      << LOG_KV("interval", interval)
//     //                      << LOG_KV("TPS", tps)
//     //                      << LOG_KV("Latency", latency);
//     // }
// }





/** 
 * NOTES: 使用EVMInstance执行交易 shared_ptr<Transaction> tx
 * */
// void TransactionExecuter::executeTx(shared_ptr<Transaction> tx)
// {
    // if(m_processed_intrashardTxNum == 10){
    //     simulatedTx = tx;
    // }

    // vm = dev::plugin::executiveContext->getExecutiveInstance();
    // exec->setVM(vm);
    // dev::plugin::executiveContext->executeTransaction(exec, tx);
    // dev::plugin::executiveContext->m_vminstance_pool.push(vm);
// }



// /** 
//  * NOTES: // 原始片内交易，可能会遇到交易要访问的状态已经被迁移走(重组片内交易不在这里处理，是在 processOrderedBatchedSubInterShardTxs 中处理)
//  * */
// void TransactionExecuter::processSingleIntraShardTx(shared_ptr<Transaction> tx, string& data_str)
// {
//     vector<string> items;
//     boost::split(items, data_str, boost::is_any_of("|"), boost::token_compress_on);
//     string rwkeys = items.at(1);
//     vector<string> rwkeyitems;
//     boost::split(rwkeyitems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);

//     // 保存交易访问的读写集信息
//     int rwkeys_size = rwkeyitems.size();
//     for(int i = 0; i < rwkeys_size; i++){
//         string rwkey = rwkeyitems.at(i);
//         m_LRU_StatesInfo->updateLocalKeyContentionByIntraTx(rwkey);
//     }

//     // 宏观solution:
//     // 1. 判断是否有写权限已经被转移走的状态，或者是附加被阻塞的状态；若有，则阻塞交易，更新附加被阻塞的状态列表，否则进入2
//     // 2. 按照原先逻辑处理交易
//     // 3. 当启动shuffleTx时，将目前已积攒的交易发送给其他分片并本地保存一份，并清空附加被阻塞的状态列表（不用担心此时有其他交易进来，因为是线程是阻塞的）
//     // 4. 当收到主分片发送来的状态值时，副分片将发送给主分片的交易也执行掉（这里也为了简化代码，只要收到一个来自某个分片的消息，就立即执行交易，其实是应该收齐所有分片的消息才是最正确的，后面再优化吧）

//     // 判断当前交易是否包含临时转移状态
//     bool isBlocked = false;
//     for(int i = 0; i < rwkeys_size; i++){
//         string rwkey = rwkeyitems.at(i);
//         if(transfered_states.count(rwkey) != 0 || count_otherState(rwkey) != 0){
//             isBlocked = true;
//             break;
//         }
//     }

//     if(isBlocked == true){ // 若交易被阻塞
//         // 更新 other_blocked_states
//         for(int i = 0; i < rwkeys_size; i++){
//             string rwkey = rwkeyitems.at(i);
//             add_otherState(rwkey); // 将交易 rwkey 添加到 other_blocked_states 中
//         }

//         // 缓存该笔交易
//         vector<string> item; // <txrlp, shardid, shardid...>
//         string txrlp = toHex(tx->rlp());
//         item.push_back(txrlp);

//         for(int i = 0; i < rwkeys_size; i++){
//             string rwkey = rwkeyitems.at(i);
//             if(transfered_states.count(rwkey) != 0){
//                 string shardid = transfered_states.at(rwkey);
//                 item.push_back(shardid);
//             }
//         }
//         blockedTxs.enqueue(item); // 因状态转移而需要临时阻塞的片内交易
//     }
//     else { // 否则开始按照原先流程立即处理交易
//         isBlocked = false;
//         int blockingQueue_key = 0; // 阻塞交易的队列key

//         int uppershards_size = upper_groupids.size();
//         for(int i = 0; i < uppershards_size; i++){
//             int coordinator_shardid = upper_groupids.at(i);
//             auto blockingTxQueue = m_blockingTxQueues->at(to_string(coordinator_shardid));

//             // 检查交易的每个读写集是否被阻塞，先假设每个交易的读写集只会被一个队列阻塞
//             for(int i = 0; i < rwkeys_size; i++) {
//                 string rwkey = rwkeyitems.at(i);
//                 if(m_blockingTxQueue->isBlocked(rwkey) == true){
//                     isBlocked = true;
//                     blockingQueue_key = coordinator_shardid;
//                     break;
//                 }
//             }
//         }

//         // PLUGIN_LOG(INFO) << LOG_KV("isBlocked", isBlocked);
//         // 若交易的所有状态都未被阻塞, 立即执行交易
//         if(isBlocked == false){
//             // executeTx(tx);
//             executeTx(tx, false);
//             add_processedTxNum();
//             if(get_processedTxNum() % 1000 == 0){
//                 calculateTPS();
//                 PLUGIN_LOG(INFO) << LOG_DESC("执行原始片内交易");
//                 PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//             }
//         }
//         else{ // 将交易包装成 transactionWithReadWriteSet 类型并加入相应的缓冲队列
//             // PLUGIN_LOG(INFO) << LOG_DESC("将交易包装成 transactionWithReadWriteSet 类型并加入缓冲队列");
//             auto txwithrwset = make_shared<dev::plugin::transactionWithReadWriteSet>();
//             txwithrwset->txs.push_back(tx);
//             txwithrwset->rwkeys = rwkeys;
//             txwithrwset->is_intershardTx = false;
//             auto blockingTxQueue = m_blockingTxQueues->at(to_string(blockingQueue_key));
//             blockingTxQueue->insertTx(txwithrwset);
//         }
//     }
// }


// /** 
//  * NOTES: // 原始片内交易，可能会遇到交易要访问的状态已经被迁移走(重组片内交易不在这里处理，是在 processOrderedBatchedSubInterShardTxs 中处理)
//  * */
// void TransactionExecuter::processSingleIntraShardTx(shared_ptr<Transaction> tx, string& data_str)
// {
//     // PLUGIN_LOG(INFO) << LOG_KV("处理的原始片内交易 data_str", data_str); // info|2023-02-13 22:42:25.450311|[PLUGIN][PLUGIN],处理的原始片内交易 data_str=0x444555666|state619|           
//     vector<string> items;
//     boost::split(items, data_str, boost::is_any_of("|"), boost::token_compress_on);
//     string rwkeys = items.at(1);
//     vector<string> rwkeyitems;
//     boost::split(rwkeyitems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);

//     // 保存交易访问的读写集信息
//     int rwkeys_size = rwkeyitems.size();
//     for(int i = 0; i < rwkeys_size; i++)
//     {
//         string rwkey = rwkeyitems.at(i);
//         m_LRU_StatesInfo->updateLocalKeyContentionByIntraTx(rwkey);
//     }

//     // 宏观solution:
//     // 1. 判断是否有写权限已经被转移走的状态，或者是附加被阻塞的状态；若有，则阻塞交易，更新附加被阻塞的状态列表，否则进入2
//     // 2. 按照原先逻辑处理交易
//     // 3. 当启动shuffleTx时，将目前已积攒的交易发送给其他分片并本地保存一份，并清空附加被阻塞的状态列表（不用担心此时有其他交易进来，因为是线程是阻塞的）
//     // 4. 当收到主分片发送来的状态值时，副分片将发送给主分片的交易也执行掉（这里也为了简化代码，只要收到一个来自某个分片的消息，就立即执行交易，其实是应该收齐所有分片的消息才是最正确的，后面再优化吧）

//     // step1. 判断当前交易是否包含临时转移状态
//     bool txIsBlocked = false;
//     for(int i = 0; i < rwkeys_size; i++) 
//     {
//         string rwkey = rwkeyitems.at(i);
//         if(transfered_states.count(rwkey) != 0 || count_otherState(rwkey) != 0)
//         {
//             txIsBlocked = true;
//             break;
//         }
//     }
//     // step2. 若交易被阻塞，更新 other_blocked_states
//     if(txIsBlocked == true)
//     {
//         for(int i = 0; i < rwkeys_size; i++)
//         {
//             string rwkey = rwkeyitems.at(i);
//             add_otherState(rwkey); // 将 rwkey 添加到 other_blocked_states 中
//         }
//         // step2.1 缓存该笔交易
//         vector<string> item; // <txrlp, shardid, shardid...>
//         string txrlp = toHex(tx->rlp());
//         item.push_back(txrlp);

//         for(int i = 0; i < rwkeys_size; i++)
//         {
//             string rwkey = rwkeyitems.at(i);
//             if(transfered_states.count(rwkey) != 0)
//             {
//                 string shardid = transfered_states.at(rwkey);
//                 item.push_back(shardid);
//             }
//         }
//         blockedTxs.enqueue(item); // 因状态转移而需要临时阻塞的片内交易
//     }
//     else // 否则开始按照正常流程立即处理交易
//     {
//         // PLUGIN_LOG(INFO) << LOG_DESC("交易不包含临时转移状态，按照正常流程处理交易");
//         // 依次检查交易的每个读写集是否被阻塞
//         bool txIsBlocked = false;
//         for(int i = 0; i < rwkeys_size; i++)
//         {
//             string rwkey = rwkeyitems.at(i);
//             if(m_blockingTxQueue->isBlocked(rwkey) == true) 
//             {
//                 txIsBlocked = true;
//                 break;
//             }
//         }

//         // PLUGIN_LOG(INFO) << LOG_KV("txIsBlocked", txIsBlocked);
//         // 若交易的所有状态都未被阻塞, 立即执行交易
//         if(txIsBlocked == false) 
//         {
//             // executeTx(tx);
//             executeTx(tx, false);
//             add_processedTxNum();
//             if(get_processedTxNum() % 1000 == 0)
//             {
//                 calculateTPS();
//                 PLUGIN_LOG(INFO) << LOG_DESC("执行原始片内交易");
//                 PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//             }
//         }
//         else // 将交易包装成 transactionWithReadWriteSet 类型并加入缓冲队列
//         {
//             PLUGIN_LOG(INFO) << LOG_DESC("将交易包装成 transactionWithReadWriteSet 类型并加入缓冲队列");
//             auto txwithrwset = make_shared<dev::plugin::transactionWithReadWriteSet>();
//             txwithrwset->txs.push_back(tx);
//             txwithrwset->rwkeys = rwkeys;
//             txwithrwset->is_intershardTx = false;

//             m_blockingTxQueue->insertTx(txwithrwset); // 这里出了问题！！！
//         }
//     }
// }


// /**
//  * NOTES: 依次处理被阻塞的交易, 每当处理完一个来自上层的epoch的交易时，检查是否需要shuffle消息
// */
// void TransactionExecuter::processBlockingTxsPlus() 
// {
//     while(true) {
//         bool existBlockedTx = false;
//         int uppershards_size = upper_groupids.size();
//         for(int i = 0; i < uppershards_size; i++){
//             int coordinator_shardid = upper_groupids.at(i);
//             auto blockingTxQueue = m_blockingTxQueues->at(to_string(coordinator_shardid));
//             auto txplusrwset = blockingTxQueue->frontTx();

//             if(txplusrwset != nullptr) {
//                 existBlockedTx = true;
//                 if(txplusrwset->is_intershardTx == true) { // 若交易是跨片子交易
//                     auto tx_hash = txplusrwset->tx->hash();
//                     /* 检查是否需要发送读写集, 当前交易需要发送新的读写集=1_1_0x362de179294eb3070a36d13ed00c61f59bcfb542(epochId+batchId+key),
//                     同时检查读写集是否已经发送过，只需要发送一次 */
//                     if(txplusrwset->rwkeysTosend.size() != 0 && m_sendedrwsetTx.count(tx_hash) == 0) {  // 需要发送读写集且没发送过
//                         // PLUGIN_LOG(INFO) << LOG_DESC("发现需要发送读写集的交易");
//                         m_sendedrwsetTx.insert(tx_hash); // 更新 m_sendedrwsetTx
//                         // 提取真实MPT的key以及当前的 value，然后一次性发送出去
//                         string rwKeysToTransfer = ""; // 即将要转发出去的读写集key
//                         string rwValuesToTransfer = ""; // value
//                         string rwContentionRatesToTransfer = "";
//                         map<string, string> rwsetcontentionRates; // 所有读写集在本地的争用率

//                         for(int i = 0; i < txplusrwset->rwkeysTosend.size(); i++) {
//                             string content = txplusrwset->rwkeysTosend.at(i); // 发送的key
//                             std::vector<std::string> dataItems;
//                             boost::split(dataItems, content, boost::is_any_of("_"), boost::token_compress_on);
//                             // string epochId = dataItems.at(0);
//                             // string batchId = dataItems.at(1);
//                             // string key = dataItems.at(2);

//                             string key = dataItems.at(0); // 因为为了避免处理不一致问题, epochID_batchId_keyAddress 临时改为了keyAddress！
//                             string value = getValueByStateAddress(key);
//                             string contentionRate = m_LRU_StatesInfo->getLocalKeyIntraContention(key); // 获取当前key在本地真实的争用率
//                             // PLUGIN_LOG(INFO) << LOG_KV("key", key)
//                             //                 << LOG_KV("contentionRate", contentionRate);
//                             // string contentionRate = "0.1"; // 假设当前key在本地的争用率

//                             if(i == 0) {
//                                 rwKeysToTransfer = content;
//                                 rwValuesToTransfer = value;
//                                 rwContentionRatesToTransfer = contentionRate;
//                             }
//                             else {
//                                 rwKeysToTransfer = rwKeysToTransfer + "|" + content;
//                                 rwValuesToTransfer = rwValuesToTransfer + "|" + value;
//                                 rwContentionRatesToTransfer = rwContentionRatesToTransfer + "|" + contentionRate;
//                             }
//                         }

//                         PLUGIN_LOG(INFO) << LOG_DESC("需要传输的读写集")
//                                         << LOG_KV("rwKeysToTransfer", rwKeysToTransfer)
//                                         << LOG_KV("rwValuesToTransfer", rwValuesToTransfer)
//                                         << LOG_KV("rwContentionRatesToTransfer", rwContentionRatesToTransfer);

//                         // 由转发节点先签名，后发送读写集
//                         if(dev::plugin::nodeIdHex == toHex(dev::consensus::forwardNodeId.at(dev::consensus::internal_groupId - 1))) {
//                             PLUGIN_LOG(INFO) << LOG_DESC("发送读写集...");
//                             string participantIds = txplusrwset->participantIds;
//                             sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
//                         }

//                         // 将积攒的所有读写集发送给其他分片
//                         // string participantIds = txplusrwset->participantIds;
//                         // if(txplusrwset->lastTxInEpoch == true) {
//                         //     this_thread::sleep_for(chrono::milliseconds(50));
//                         //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
//                         // }
//                         // else{
//                         //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
//                         // }
//                     }

//                     // 检查是否收齐读写集，收齐即可立即执行交易，否则继续轮询
//                     bool lastTxInBatch = txplusrwset->lastTxInBatch;
//                     bool executeSucc = checkReadWriteKeyReady(txplusrwset->rwkeyToReceive, lastTxInBatch);

//                     string coordinator_shardid = txplusrwset->coordinator_shardid;
//                     string intershardtx_id = txplusrwset->intershardTxId;
//                     // PLUGIN_LOG(INFO) << LOG_KV("coordinator_shardid", coordinator_shardid)
//                     //                  << LOG_KV("intershardtxid", intershardtxid);
//                     if(executeSucc == true)
//                     {
//                         // PLUGIN_LOG(INFO) << LOG_DESC("读写集准备完毕，开始执行交易");
//                         // 读写集准备完毕，开始执行交易
//                         // PLUGIN_LOG(INFO) << LOG_DESC("参与者提交的部分跨片交易")
//                         //                  << LOG_KV("coordinator_shardid", coordinator_shardid)
//                         //                  << LOG_KV("intershardtxid", intershardtxid);
//                         // executeTx(txplusrwset->tx);
//                         executeTx(txplusrwset->tx, false);
//                         // 注：当统计交易延迟的时候，以下步骤不需要执行，需要注释掉
//                         // add_processedTxNum();
//                         // if(get_processedTxNum() % 1000 == 0) {
//                         //     calculateTPS();
//                         //     PLUGIN_LOG(INFO) << LOG_DESC("执行跨片交易");
//                         //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//                         // }

//                         // 注：当统计交易吞吐的时候，以下步骤不需要执行，需要注释掉
//                         if(dev::consensus::internal_groupId == txplusrwset->minimum_participant_shardid
//                            && dev::plugin::nodeIdHex == toHex(dev::consensus::forwardNodeId.at(dev::consensus::internal_groupId - 1)) ){ // 若本分片为shardid最小的参与方且为转发节点, 那么负责计数（因为其他分片重复执行，只统计一次）

//                             m_processed_intershardTxNum++;
//                             // PLUGIN_LOG(INFO) << LOG_DESC("统计跨片交易") << LOG_KV("m_processed_intershardTxNum", m_processed_intershardTxNum);
                            
//                             // 向 m_committed_intershardTxid 中添加 intershardtxid
//                             int coordinator = atoi(coordinator_shardid.c_str());
//                             int intershardtxid = atoi(intershardtx_id.c_str());

//                             if(m_committed_intershardTxid->count(coordinator) != 0)
//                             {
//                                 auto intershardTxids = m_committed_intershardTxid->at(coordinator);
//                                 intershardTxids->push_back(intershardtxid);
//                             }
//                             else
//                             {
//                                 auto intershardTxids = make_shared<vector<int>>();
//                                 intershardTxids->push_back(intershardtxid);
//                                 m_committed_intershardTxid->insert(make_pair(coordinator, intershardTxids));
//                             }

//                             if(m_processed_intershardTxNum % 1000 == 0) // 每执行1000笔跨片交易，向相应协调者回复交易数目
//                             {
//                                 // shared_ptr<map<int, shared_ptr<vector<int>>>> committed_intershardTxid;
//                                 // get_committed_intershardTxid(committed_intershardTxid);
//                                 // 遍历committed_intershardTxid, 获取要发给不同分片的Txid
//                                 for(auto it = m_committed_intershardTxid->begin(); it != m_committed_intershardTxid->end(); it++)
//                                 {
//                                     string coordinator_shardid = to_string(it->first);
//                                     auto value_contents = it->second;
//                                     string intershardTxids = "";
//                                     int intershardTxid_size = value_contents->size();
//                                     for(int i = 0; i < intershardTxid_size; i++)
//                                     {
//                                         int intershardTxid = value_contents->at(i);
//                                         string flag = "|";
//                                         if(i == 0)
//                                         {
//                                             intershardTxids = intershardTxid;
//                                         }
//                                         else
//                                         {
//                                             intershardTxids = intershardTxids + flag + to_string(intershardTxid);
//                                         }
//                                     }

//                                     // 将 intershardTxids 发送至分片 coordinator_shardid
//                                     responseCommitToCoordinator(coordinator_shardid, intershardTxids);
//                                 }
//                                 m_committed_intershardTxid->clear();
//                             }
//                         }

//                         // 检查该笔交易是否为epoch中的最后一笔交易
//                         if(txplusrwset->lastTxInEpoch == true) {
//                             PLUGIN_LOG(INFO) << LOG_DESC("epoch中的最后一笔跨片交易处理结束, 开始分析负载是否有局部性特征");
//                             // analyseWorkload(); // 若某个状态频繁与其他分片的状态发生跨片交易，且其他分片对状态的争用较低，则向上层分片请求该状态的“主写权”
//                             // shuffleStateValue(); // 主分片向副分片发送积攒的最新状态
//                             // shuffleBlockedTxs(); // 副分片向主分片发送积攒的交易
//                         }
                        
//                         blockingTxQueue->deleteTx(); // 清空该笔交易
//                     }
//                 }
//                 else if(txplusrwset->is_intershardTx == false) { // 下一笔交易为片内交易, 立即执行







//                     // PLUGIN_LOG(INFO) << LOG_DESC("下一笔交易为片内交易, 立即执行");
//                     auto txs = txplusrwset->txs;
//                     vector<int> blockingQueue_keys = txplusrwset->blockingQueue_keys;

//                     int txs_size = txs.size(); // 片内交易也可能是多笔子交易
//                     for(int i = 0; i < txs_size; i++) {
//                         auto tx = txs.at(i);
//                         // executeTx(tx);
//                         executeTx(tx, false);
//                     }
//                     add_processedTxNum();
//                     if(get_processedTxNum() % 1000 == 0) {
//                         calculateTPS();
//                         PLUGIN_LOG(INFO) << LOG_DESC("执行被阻塞的片内交易");
//                         PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//                     }
//                     // m_blockingTxQueue->deleteTx(); // 清空交易
//                     // blockingTxQueue->deleteTx(); // 清空交易


//                     // blockingTxQueue->deleteTx();

//                     // int blockingQueue_keys_size = blockingQueue_keys.size(); // 用于应对一笔交易同时阻塞在多个队列中
//                     // for(int i = 0; i < blockingQueue_keys_size; i++){
//                     //     auto blockingTxQueue = m_blockingTxQueues->at(to_string(coordinator_shardid));
//                     //     blockingTxQueue->deleteTx();
//                     // }

//                 }
//             }
//         }
//         if(existBlockedTx == false) { // 不存在被阻塞交易
//             this_thread::sleep_for(chrono::milliseconds(10));
//         }
//     }
// }


// /**
//  * NOTES: 依次处理被阻塞的交易, 每当处理完一个来自上层的epoch的交易时，检查是否需要shuffle消息
// */
// void TransactionExecuter::processBlockingTxsPlus()
// {
//     while(true) {
//         bool existBlockedTx = false;
//         int uppershards_size = upper_groupids.size();
//         for(int i = 0; i < uppershards_size; i++){
//             int coordinator_shardid = upper_groupids.at(i);
//             auto blockingTxQueue = m_blockingTxQueues->at(to_string(coordinator_shardid));
//             auto txplusrwset = blockingTxQueue->frontTx();

//             if(txplusrwset != nullptr) {
//                 existBlockedTx = true;
//                 if(txplusrwset->is_intershardTx == true) { // 若交易是跨片子交易
//                     auto tx_hash = txplusrwset->tx->hash();
//                     /* 检查是否需要发送读写集, 当前交易需要发送新的读写集=1_1_0x362de179294eb3070a36d13ed00c61f59bcfb542(epochId+batchId+key),
//                     同时检查读写集是否已经发送过，只需要发送一次 */
//                     if(txplusrwset->rwkeysTosend.size() != 0 && m_sendedrwsetTx.count(tx_hash) == 0) {  // 需要发送读写集且没发送过
//                         // PLUGIN_LOG(INFO) << LOG_DESC("发现需要发送读写集的交易");
//                         m_sendedrwsetTx.insert(tx_hash); // 更新 m_sendedrwsetTx
//                         // 提取真实MPT的key以及当前的 value，然后一次性发送出去
//                         string rwKeysToTransfer = ""; // 即将要转发出去的读写集key
//                         string rwValuesToTransfer = ""; // value
//                         string rwContentionRatesToTransfer = "";
//                         map<string, string> rwsetcontentionRates; // 所有读写集在本地的争用率

//                         for(int i = 0; i < txplusrwset->rwkeysTosend.size(); i++) {
//                             string content = txplusrwset->rwkeysTosend.at(i); // 发送的key
//                             std::vector<std::string> dataItems;
//                             boost::split(dataItems, content, boost::is_any_of("_"), boost::token_compress_on);
//                             // string epochId = dataItems.at(0);
//                             // string batchId = dataItems.at(1);
//                             // string key = dataItems.at(2);

//                             string key = dataItems.at(0); // 因为为了避免处理不一致问题, epochID_batchId_keyAddress 临时改为了keyAddress！
//                             string value = getValueByStateAddress(key);
//                             string contentionRate = m_LRU_StatesInfo->getLocalKeyIntraContention(key); // 获取当前key在本地真实的争用率
//                             // PLUGIN_LOG(INFO) << LOG_KV("key", key)
//                             //                 << LOG_KV("contentionRate", contentionRate);
//                             // string contentionRate = "0.1"; // 假设当前key在本地的争用率

//                             if(i == 0) {
//                                 rwKeysToTransfer = content;
//                                 rwValuesToTransfer = value;
//                                 rwContentionRatesToTransfer = contentionRate;
//                             }
//                             else {
//                                 rwKeysToTransfer = rwKeysToTransfer + "|" + content;
//                                 rwValuesToTransfer = rwValuesToTransfer + "|" + value;
//                                 rwContentionRatesToTransfer = rwContentionRatesToTransfer + "|" + contentionRate;
//                             }
//                         }

//                         PLUGIN_LOG(INFO) << LOG_DESC("需要传输的读写集")
//                                         << LOG_KV("rwKeysToTransfer", rwKeysToTransfer)
//                                         << LOG_KV("rwValuesToTransfer", rwValuesToTransfer)
//                                         << LOG_KV("rwContentionRatesToTransfer", rwContentionRatesToTransfer);

//                         // 由转发节点先签名，后发送读写集
//                         if(dev::plugin::nodeIdHex == toHex(dev::consensus::forwardNodeId.at(dev::consensus::internal_groupId - 1))) {
//                             PLUGIN_LOG(INFO) << LOG_DESC("发送读写集...");
//                             string participantIds = txplusrwset->participantIds;
//                             sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
//                         }

//                         // 将积攒的所有读写集发送给其他分片
//                         // string participantIds = txplusrwset->participantIds;
//                         // if(txplusrwset->lastTxInEpoch == true) {
//                         //     this_thread::sleep_for(chrono::milliseconds(50));
//                         //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
//                         // }
//                         // else{
//                         //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
//                         // }
//                     }

//                     // 检查是否收齐读写集，收齐即可立即执行交易，否则继续轮询
//                     bool lastTxInBatch = txplusrwset->lastTxInBatch;
//                     bool executeSucc = checkReadWriteKeyReady(txplusrwset->rwkeyToReceive, lastTxInBatch);

//                     string coordinator_shardid = txplusrwset->coordinator_shardid;
//                     string intershardtx_id = txplusrwset->intershardTxId;
//                     // PLUGIN_LOG(INFO) << LOG_KV("coordinator_shardid", coordinator_shardid)
//                     //                  << LOG_KV("intershardtxid", intershardtxid);
//                     if(executeSucc == true)
//                     {
//                         // PLUGIN_LOG(INFO) << LOG_DESC("读写集准备完毕，开始执行交易");
//                         // 读写集准备完毕，开始执行交易
//                         // PLUGIN_LOG(INFO) << LOG_DESC("参与者提交的部分跨片交易")
//                         //                  << LOG_KV("coordinator_shardid", coordinator_shardid)
//                         //                  << LOG_KV("intershardtxid", intershardtxid);
//                         // executeTx(txplusrwset->tx);
//                         executeTx(txplusrwset->tx, false); // 跨片交易，需要重复执行
//                         executeTx(txplusrwset->tx, false);
//                         // 注：当统计交易延迟的时候，以下步骤不需要执行，需要注释掉
//                         // add_processedTxNum();
//                         // if(get_processedTxNum() % 1000 == 0) {
//                         //     calculateTPS();
//                         //     PLUGIN_LOG(INFO) << LOG_DESC("执行跨片交易");
//                         //     PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//                         // }

//                         // 注：当统计交易吞吐的时候，以下步骤不需要执行，需要注释掉
//                         if(dev::consensus::internal_groupId == txplusrwset->minimum_participant_shardid
//                            && dev::plugin::nodeIdHex == toHex(dev::consensus::forwardNodeId.at(dev::consensus::internal_groupId - 1)) ){ // 若本分片为shardid最小的参与方且为转发节点, 那么负责计数（因为其他分片重复执行，只统计一次）

//                             m_processed_intershardTxNum++;
//                             // PLUGIN_LOG(INFO) << LOG_DESC("统计跨片交易") << LOG_KV("m_processed_intershardTxNum", m_processed_intershardTxNum);
                            
//                             // 向 m_committed_intershardTxid 中添加 intershardtxid
//                             int coordinator = atoi(coordinator_shardid.c_str());
//                             int intershardtxid = atoi(intershardtx_id.c_str());

//                             if(m_committed_intershardTxid->count(coordinator) != 0)
//                             {
//                                 auto intershardTxids = m_committed_intershardTxid->at(coordinator);
//                                 intershardTxids->push_back(intershardtxid);
//                             }
//                             else
//                             {
//                                 auto intershardTxids = make_shared<vector<int>>();
//                                 intershardTxids->push_back(intershardtxid);
//                                 m_committed_intershardTxid->insert(make_pair(coordinator, intershardTxids));
//                             }

//                             if(m_processed_intershardTxNum % 1000 == 0) // 每执行1000笔跨片交易，向相应协调者回复交易数目
//                             {
//                                 // shared_ptr<map<int, shared_ptr<vector<int>>>> committed_intershardTxid;
//                                 // get_committed_intershardTxid(committed_intershardTxid);
//                                 // 遍历committed_intershardTxid, 获取要发给不同分片的Txid
//                                 for(auto it = m_committed_intershardTxid->begin(); it != m_committed_intershardTxid->end(); it++)
//                                 {
//                                     string coordinator_shardid = to_string(it->first);
//                                     auto value_contents = it->second;
//                                     string intershardTxids = "";
//                                     int intershardTxid_size = value_contents->size();
//                                     for(int i = 0; i < intershardTxid_size; i++)
//                                     {
//                                         int intershardTxid = value_contents->at(i);
//                                         string flag = "|";
//                                         if(i == 0)
//                                         {
//                                             intershardTxids = intershardTxid;
//                                         }
//                                         else
//                                         {
//                                             intershardTxids = intershardTxids + flag + to_string(intershardTxid);
//                                         }
//                                     }

//                                     // 将 intershardTxids 发送至分片 coordinator_shardid
//                                     responseCommitToCoordinator(coordinator_shardid, intershardTxids);
//                                 }
//                                 m_committed_intershardTxid->clear();
//                             }
//                         }

//                         // 检查该笔交易是否为epoch中的最后一笔交易
//                         if(txplusrwset->lastTxInEpoch == true) {
//                             PLUGIN_LOG(INFO) << LOG_DESC("epoch中的最后一笔跨片交易处理结束, 开始分析负载是否有局部性特征");
//                             // analyseWorkload(); // 若某个状态频繁与其他分片的状态发生跨片交易，且其他分片对状态的争用较低，则向上层分片请求该状态的“主写权”
//                             // shuffleStateValue(); // 主分片向副分片发送积攒的最新状态
//                             // shuffleBlockedTxs(); // 副分片向主分片发送积攒的交易
//                         }
                        
//                         blockingTxQueue->deleteTx(); // 清空该笔交易
//                     }
//                 }
//                 else if(txplusrwset->is_intershardTx == false) { // 下一笔交易为片内交易, 立即执行
//                     // PLUGIN_LOG(INFO) << LOG_DESC("下一笔交易为片内交易, 立即执行");
//                     auto txs = txplusrwset->txs;
//                     int txs_size = txs.size(); // 片内交易也可能是多笔子交易
//                     for(int i = 0; i < txs_size; i++) {
//                         auto tx = txs.at(i);
//                         // executeTx(tx);
//                         executeTx(tx, false);
//                     }
//                     add_processedTxNum();
//                     if(get_processedTxNum() % 1000 == 0) {
//                         calculateTPS();
//                         PLUGIN_LOG(INFO) << LOG_DESC("执行被阻塞的片内交易");
//                         PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", get_processedTxNum());
//                     }
//                     // m_blockingTxQueue->deleteTx(); // 清空交易
//                     blockingTxQueue->deleteTx(); // 清空交易
//                 }
//             }
//         }
//         if(existBlockedTx == false) { // 不存在被阻塞交易
//             this_thread::sleep_for(chrono::milliseconds(10));
//         }
//     }
// }



/** 
 * NOTES: 并发处理被阻塞的交易(跨片交易和片内交易), 不包括多点写逻辑
 * */
// void TransactionExecuter::processBlockingTxs() {
    
    // while(true) {
    //     if(m_blockingTxQueue->txs->size() != 0) {
    //         // PLUGIN_LOG(INFO) << LOG_KV("m_blockingTxQueue->txs->size()", m_blockingTxQueue->txs->size());
    //         auto txplusrwset = m_blockingTxQueue->txs->front();
    //         if(txplusrwset->is_intershardTx == true) { // 若交易是跨片子交易
    //             auto tx_hash = txplusrwset->tx->hash();
    //             /* 检查是否需要发送读写集, 当前交易需要发送新的读写集=1_1_0x362de179294eb3070a36d13ed00c61f59bcfb542(epochId+batchId+key),
    //                同时检查读写集是否已经发送过，只需要发送一次 */
    //             if(txplusrwset->rwkeysTosend.size() != 0 && m_sendedrwsetTx.count(tx_hash) == 0) {  // 需要发送读写集且没发送过

    //                 PLUGIN_LOG(INFO) << LOG_DESC("发现需要发送读写集的交易");

    //                 m_sendedrwsetTx.insert(tx_hash); // 更新 m_sendedrwsetTx
    //                 // 提取真实MPT的key以及当前的 value，然后一次性发送出去
    //                 string rwKeysToTransfer = ""; // 即将要转发出去的读写集key
    //                 string rwValuesToTransfer = ""; // value
    //                 string rwContentionRatesToTransfer = "";
    //                 map<string, string> rwsetcontentionRates; // 所有读写集在本地的争用率

    //                 for(int i = 0; i < txplusrwset->rwkeysTosend.size(); i++) {
    //                     string content = txplusrwset->rwkeysTosend.at(i); // 发送的key
    //                     std::vector<std::string> dataItems;
    //                     boost::split(dataItems, content, boost::is_any_of("_"), boost::token_compress_on);
    //                     // string epochId = dataItems.at(0);
    //                     // string batchId = dataItems.at(1);
    //                     // string key = dataItems.at(2);

    //                     string key = dataItems.at(0); // 因为为了避免处理不一致问题，epochID_batchId_keyAddress 临时改为了keyAddress！
    //                     string value = getValueByStateAddress(key);
    //                     string contentionRate = m_LRU_StatesInfo->getLocalKeyIntraContention(key); // 获取当前key在本地真实的争用率
    //                     PLUGIN_LOG(INFO) << LOG_KV("key", key)
    //                                      << LOG_KV("contentionRate", contentionRate);
    //                     // string contentionRate = "0.1"; // 假设当前key在本地的争用率

    //                     if(i == 0) {
    //                         rwKeysToTransfer = content;
    //                         rwValuesToTransfer = value;
    //                         rwContentionRatesToTransfer = contentionRate;
    //                     }
    //                     else {
    //                         rwKeysToTransfer = rwKeysToTransfer + "|" + content;
    //                         rwValuesToTransfer = rwValuesToTransfer + "|" + value;
    //                         rwContentionRatesToTransfer = rwContentionRatesToTransfer + "|" + contentionRate;
    //                     }
    //                 }

    //                 PLUGIN_LOG(INFO) << LOG_DESC("需要传输的读写集")
    //                                  << LOG_KV("rwKeysToTransfer", rwKeysToTransfer)
    //                                  << LOG_KV("rwValuesToTransfer", rwValuesToTransfer)
    //                                  << LOG_KV("rwContentionRatesToTransfer", rwContentionRatesToTransfer);

    //                 // 将积攒的所有读写集发送给其他分片
    //                 string participantIds = txplusrwset->participantIds;
    //                 // if(txplusrwset->lastTxInEpoch == true)
    //                 // {
    //                 //     this_thread::sleep_for(chrono::milliseconds(50));
    //                 //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
    //                 // }
    //                 // else{
    //                 //     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
    //                 // }
    //                 // 由转发节点先签名，后发送读写集
    //                 if(dev::plugin::nodeIdHex == toHex(dev::consensus::forwardNodeId.at(dev::consensus::internal_groupId - 1))) {
    //                     PLUGIN_LOG(INFO) << LOG_DESC("发送读写集...");
    //                     string participantIds = txplusrwset->participantIds;
    //                     sendReadWriteSet(rwKeysToTransfer, rwValuesToTransfer, rwContentionRatesToTransfer, participantIds);
    //                 }
    //             }

    //             // 检查是否收齐读写集，收齐即可立即执行交易，否则继续轮询
    //             bool lastTxInEpoch = txplusrwset->lastTxInEpoch;
    //             bool executeSucc = checkReadWriteKeyReady(txplusrwset->rwkeyToReceive, lastTxInEpoch);
    //             // PLUGIN_LOG(INFO) << LOG_DESC("检查是否收齐读写集，收齐即可立即执行交易，否则继续轮询")
    //             //                  << LOG_KV("executeSucc", executeSucc);
    //             if(executeSucc == true) { // 读写集准备完毕，开始执行交易
    //                 executeTx(txplusrwset->tx);
    //                 m_processed_intrashardTxNum++;
    //                 PLUGIN_LOG(INFO) << LOG_KV("已经执行交易数目为", m_processed_intrashardTxNum);

    //                 // if(m_processed_intrashardTxNum % 100 == 0) {
    //                 //     PLUGIN_LOG(INFO) << LOG_KV("已经执行交易数目为", m_processed_intrashardTxNum);
    //                 // }

    //                 // 检查该笔交易是否为epoch中的最后一笔交易
    //                 if(txplusrwset->lastTxInEpoch == true) {
    //                     PLUGIN_LOG(INFO) << LOG_DESC("epoch中的最后一笔跨片交易处理结束, 开始分析负载是否有局部性特征");
    //                     analyseWorkload(); // 若某个状态频繁与其他分片的状态发生跨片交易，且其他分片对状态的争用较低，则向上层分片请求该状态的“主写权”
    //                     // shuffleAddedStateValue();
    //                 }
    //                 m_blockingTxQueue->deleteTx(); // 清空该笔交易
    //             }
    //         }
    //         else if(txplusrwset->is_intershardTx == false) { // 下一笔交易为片内交易, 立即执行
    //             auto txs = txplusrwset->txs;
    //             int txs_size = txs.size(); // 片内交易也可能是多笔子交易
    //             for(int i = 0; i < txs_size; i++) {
    //                 auto tx = txs.at(i);
    //                 executeTx(tx);
    //             }
    //             m_processed_intrashardTxNum++;
    //             PLUGIN_LOG(INFO) << LOG_KV("已经执行交易数目为", m_processed_intrashardTxNum);

    //             // if(m_processed_intrashardTxNum % 100 == 0) {
    //             //     PLUGIN_LOG(INFO) << LOG_KV("已经执行交易数目为", m_processed_intrashardTxNum);
    //             // }
    //             m_blockingTxQueue->deleteTx(); // 清空交易
    //         }
    //     }
    //     else { // 无被阻塞的跨片交易或者片内交易
    //         this_thread::sleep_for(chrono::milliseconds(10));
    // }}
// }

/** 
 * NOTES: 使用EVMInstance执行交易 shared_ptr<Transaction> tx，启动多个instance，避免线程不安全
 * */
// void TransactionExecuter::executeTx(shared_ptr<Transaction> tx, bool intraTx)
// {
//     if(intraTx == true)
//     {
//         exec_intra->setVM(vm_intra);
//         dev::plugin::executiveContext->executeTransaction(exec_intra, tx);
//     }
//     else
//     {
//         exec_inter->setVM(vm_inter);
//         dev::plugin::executiveContext->executeTransaction(exec_inter, tx);
//     }
// }

// /** 
//  * NOTES: 执行由多笔子交易组合形成的片内交易
//  * */
// void TransactionExecuter::processCombinedIntraShardTx(shared_ptr<Transaction> tx)
// {
//     // 提取所有子交易
//     vector<Transaction::Ptr> subtxs;
//     string data_str = dataToHexString(tx->get_data());
//     vector<string> dataitems;
//     boost::split(dataitems, data_str, boost::is_any_of("|"), boost::token_compress_on);
//     size_t subtx_size = (dataitems.size() - 2) / 2;

//     size_t subtxIndex = 2;
//     for(size_t i = 0; i < subtx_size; i++){
//         string txRlp = dataitems.at(subtxIndex);
//         subtxIndex += 2;
//         auto tx = make_shared<Transaction>(jsToBytes(txRlp, OnFailed::Throw), CheckTransaction::Everything); // 包装成交易
//         subtxs.push_back(tx);
//     }

//     // 解析当前片内交易的所有读写集
//     string rwkeys = dev::plugin::intrashardtxhash2rwkeys->at(tx->hash()).c_str(); // 片内交易读写集
//     vector<string> rwkeysitems;
//     boost::split(rwkeysitems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);
//     int rwkeys_size = rwkeysitems.size();

//     // 记录交易的读写集
//     for(int i = 0; i < rwkeys_size; i++) {
//         string rwkey = rwkeysitems.at(i);
//         m_LRU_StatesInfo->updateLocalKeyContentionByIntraTx(rwkey);
//     }

//     // 依次检查交易的每个读写集是否被阻塞
//     bool isblocked = false;
//     for(int i = 0; i < rwkeys_size; i++){
//         string rwkey = rwkeysitems.at(i);
//         if(m_blockingTxQueue->isBlocked(rwkey) == true){
//             isblocked = true;
//             break;
//         }
//     }

//     // 若交易的读写集未被阻塞, 立即执行交易
//     if(isblocked == false) {
//         int subtxs_size = subtxs.size();
//         for(int i = 0; i < subtxs_size; i++){
//             auto tx = subtxs.at(i);
//             executeTx(tx, false);
//             // executeTx(tx); // 执行所有子交易
//         }

//         m_processed_intrashardTxNum++;
//         if(m_processed_intrashardTxNum % 1000 == 0) {
//             calculateTPS();
//             PLUGIN_LOG(INFO) << LOG_KV("已经执行交易数目为(In processCombinedIntraShardTx):", m_processed_intrashardTxNum);
//         }

//         // intershardtx_num++; // 算在跨片交易里了
//         // if(intershardtx_num % 100 == 0) {
//         //     PLUGIN_LOG(INFO) << LOG_KV("已经执行的跨片交易数目为", intershardtx_num);
//         // }

//     }
//     else { // 交易被阻塞，缓存交易
//         auto txwithrwset = std::make_shared<dev::plugin::transactionWithReadWriteSet>();
//         txwithrwset->rwkeys = rwkeys;
//         int subtxs_size = subtxs.size();
//         for(int i = 0; i < subtxs_size; i++) {
//             auto tx = subtxs.at(i);
//             txwithrwset->txs.push_back(tx);
//         }
//         m_blockingTxQueue->insertTx(txwithrwset);
//     }
// }

// /** 
//  * NOTES: 检查是否收齐了交易的所有读写集
//  * */
// bool TransactionExecuter::checkReadWriteKeyReady(vector<string>& rwkeyToReceive)
// {
//     // 检查rwkeyToReceive中的key在 m_readWriteSetManager->receivedTxRWset 中是否都存在
//     bool rwkeyReady = true;
//     int rwkey_size = rwkeyToReceive.size();
//     for(int i = 0; i < rwkey_size; i++) {
//         string key = rwkeyToReceive.at(i);
//         rwkeyReady = m_readWriteSetManager->checkReceivedRwset(key);
//         if(rwkeyReady == false) {
//             break;
//         }
//         // if(m_readWriteSetManager->receivedTxRWset->count(key) == 0) {  // 有未收到的读写集
//         //     rwkeyReady = false;
//         //     // break;
//         // }
//         // else if(m_readWriteSetManager->receivedTxRWset->at(key) != -1){
//         //     m_readWriteSetManager->receivedTxRWset->at(key) = -1; // 置无效
//         //     return rwkeyReady;
//         // }
//     }
//     return rwkeyReady;
// }

// /** 
//  * NOTES: 检查是否收齐了交易的所有读写集
//  * */
// bool TransactionExecuter::checkReadWriteKeyReady(vector<string>& rwkeyToReceive, bool lastTxInEpoch)
// {
//     // 检查rwkeyToReceive中的key在 m_readWriteSetManager->receivedTxRWset 中是否都存在
//     bool rwkeyReady = true;
//     int rwkey_size = rwkeyToReceive.size();
//     for(int i = 0; i < rwkey_size; i++) {
//         string key = rwkeyToReceive.at(i);
//         rwkeyReady = m_readWriteSetManager->checkReceivedRwset(key, lastTxInEpoch);
//         if(rwkeyReady == false) {
//             break;
//         }
//         // if(m_readWriteSetManager->receivedTxRWset->count(key) == 0) {  // 有未收到的读写集
//         //     rwkeyReady = false;
//         //     // break;
//         // }
//         // else if(m_readWriteSetManager->receivedTxRWset->at(key) != -1){
//         //     m_readWriteSetManager->receivedTxRWset->at(key) = -1; // 置无效
//         //     return rwkeyReady;
//         // }
//     }
//     return rwkeyReady;
// }

// /** 
//  * NOTES: 检查是否收齐了交易的所有读写集
//  * */
// bool TransactionExecuter::checkReadWriteKeyReady(vector<string>& rwkeyToReceive)
// {
//     // 检查rwkeyToReceive中的key在 m_readWriteSetManager->receivedTxRWset 中是否都存在
//     bool rwkeyReady = true;
//     size_t rwkey_size = rwkeyToReceive.size();
//     for(size_t i = 0; i < rwkey_size; i++) {
//         string key = rwkeyToReceive.at(i);
//         if(m_readWriteSetManager->receivedTxRWset->count(key) == 0) {  // 有未收到的读写集
//             rwkeyReady = false;
//             break;
//         }
//     }
//     return rwkeyReady;
// }

// /**
//  * NOTES: 从destinShardId分片将状态readwritekey的写权限改回到sourceShardId
//  * */
// void TransactionExecuter::requestForMasterChange(std::string& ancestorGroupId, std::string& sourceShardId, std::string& destinShardId, 
//                                                     std::string& readwritekey, std::string& crossshardtxid)
// {
//     PLUGIN_LOG(INFO) << LOG_DESC("片内转发节点开始向祖先分片的转发节点发送状态写权限更改请求...");


//     // 假设现在有多个状态需要转移，例如requestkeys = stateE|stateF
//     readwritekey = "stateE_stateF";

//     protos::RequestForMasterShardMsg requestMsg;
//     requestMsg.set_sourceshardid(atoi(sourceShardId.c_str()));
//     requestMsg.set_destinshardid(atoi(destinShardId.c_str()));
//     requestMsg.set_readwritekey(readwritekey);
//     requestMsg.set_requestmessageid(crossshardtxid);

//     string serializedrequestMsg;
//     requestMsg.SerializeToString(&serializedrequestMsg);
//     auto msgBytes = asBytes(serializedrequestMsg);
//     dev::sync::SyncRequestForMasterShardMsg retPacket;
//     retPacket.encode(msgBytes);
//     auto msg = retPacket.toMessage(m_group_protocolID);

//     m_group_p2p_service->asyncSendMessageByNodeID(dev::consensus::forwardNodeId.at(atoi(ancestorGroupId.c_str()) - 1), msg, CallbackFuncWithSession(), dev::network::Options());
// }

// /**
//  * NOTES: 对排序后的交易进行处理
//  * */
// void TransactionExecuter::processReorderedTxs(
//                 shared_ptr<set<transactionWithReadWriteSet::Ptr, ReadWriteKeyCompare>> reorderedTxs, std::string& interShardTxEpochID)
// {
//     /**
//      * NOTES: 函数接收排序后的跨片子交易（记为一个epoch）, 相同跨片参与方的交易相邻（记为一个batch），每一个batch只在处理第一笔交易时发一次读写集
//      * 每个batch第一笔交易中记录所要发送的读写集(格式:epochID_batchId_keyAddress)，所有交易都记录需要接受的读写集(格式:epochID_batchId_keyAddress)
//      * 注：这里临时使用 keyAddress 代替 epochID_batchId_keyAddress，为了避免处理不一致的问题
//     */

//     PLUGIN_LOG(INFO) << LOG_DESC("进入processReorderedTxs...");

//     // 初始化相关变量
//     int currentBatchId = 1; // 本epoch内跨片交易的BatchId
//     int processedEpochTxNum = 0; // 当前epoch已经处理的交易数目
//     string lastParticipantIds = ""; // 上一笔跨片交易参与方
//     string currentEpochId = interShardTxEpochID; // 当前交易所在的EpochId
//     vector<string> rwkeyToSend; // 每个batch中第一笔交易需要发送的读写集(格式:epochID_batchId_keyAddress, 改为keyAddress！)
//     vector<string> rwkeyToReceive; // 记录每笔交易需要收到的读写集【key:epochID_batchId_keyAddress，改为keyAddress！】
//     vector<transactionWithReadWriteSet::Ptr> cachedTransactions; // 缓存一个batch中的交易，扫描完整个batch后，再插入缓存队列

//     // 遍历重排序后的交易
//     transactionWithReadWriteSet::Ptr currentTx;
//     transactionWithReadWriteSet::Ptr previousTx;
//     int txIndex = 0;
//     for(auto &iter:(*reorderedTxs)) {

//         if(txIndex == 0){
//             currentTx = iter;
//         }else{
//             previousTx = currentTx;
//             currentTx = iter;
//         }


//         string participantIds = iter->participantIds; // 当前这笔交易的所有参与方Id
//         // PLUGIN_LOG(INFO) << LOG_KV("当前这笔交易的所有参与方Id:participantIds", participantIds);
//         if(iter->emptyTransaction) { // 如果当前交易为空交易
//             if(processedEpochTxNum == 0) { // 如果当前交易是整个epoch中的第一笔交易
//                 lastParticipantIds = participantIds; // 初始化lastParticipantIds
//             }
//             else{ // 若当前交易不是epoch中的第一笔交易
//                 if(lastParticipantIds != participantIds) { // 参与方发生了变化，进入新的batch
//                     lastParticipantIds = participantIds; // 更新 lastParticipantIds
//                     currentBatchId++; // 进入新的batchId
//             }}
//         }
//         else if(!iter->emptyTransaction){ // 如果当前交易非空
//             if(processedEpochTxNum == 0){ // 如果当前交易是整个epoch中的第一笔交易
//                 lastParticipantIds = participantIds; // 初始化lastParticipantIds
//                 // 准备需要发送的读写集
//                 string localrwkey = iter->localreadwriteKey; // 将当前交易访问的本地读写集放入要发送的读写集中
//                 string key = localrwkey; // 将key临时改为localrwkey！，避免处理不一致问题
//                 vector<string>::iterator it = find(rwkeyToSend.begin(), rwkeyToSend.end(), key); // 检查 localrwkey 是否在 rwkeyToSend 中
//                 if(it == rwkeyToSend.end()) { // 本读写集之前没有缓存过
//                     rwkeyToSend.push_back(key);
//                 }
//                 cachedTransactions.push_back(iter); // 缓存当前batch中的交易
//             }
//             else { // 如果当前交易不是整个epoch中的第一笔交易
//                 if(lastParticipantIds == participantIds) { // 与上一笔交易的参与方相同(相同batch)
//                     string localrwkey = iter->localreadwriteKey; // 将当前交易访问的本地读写集放入要发送的读写集中
//                     // string key = currentEpochId + "_" + to_string(currentBatchId) + "_" + localrwkey;
//                     string key = localrwkey; // 将key临时改为localrwkey！，避免处理不一致问题
//                     vector<string>::iterator it = find(rwkeyToSend.begin(), rwkeyToSend.end(), key); // 检查 localrwkey 是否在 rwkeyToSend 中
//                     if(it == rwkeyToSend.end()) { // 本读写集之前没有缓存过
//                         rwkeyToSend.push_back(key);
//                         // PLUGIN_LOG(INFO) << LOG_KV("需要发送的读写集key", key);
//                     }
//                     iter->interShardTxBatchId = to_string(currentBatchId); // 初始化交易的batchId(epochID在前面已经赋值过)
//                     cachedTransactions.push_back(iter); // 缓存当前batch中的交易
//                 }
//                 else if(lastParticipantIds != participantIds) { // 与上一笔交易的参与方不相同(进入新的batch)
//                     for(int i = 0; i < cachedTransactions.size(); i++) { // 首先处理缓存的上一个batch的交易
//                         PLUGIN_LOG(INFO) << LOG_KV("缓存的交易数目cachedTransactions_size", cachedTransactions.size());
//                         auto cachedTx = cachedTransactions.at(i);
//                         cachedTx->interShardTxBatchId = to_string(currentBatchId); // 设置交易的BatchId

//                         if(i == 0) { // 设置第一笔交易的rwkeysTosend
//                             cachedTx->setrwkeysTosend(rwkeyToSend);
//                         }

//                         // 计算当前交易的 rwkeyToReceive
//                         vector<string> keyItems;
//                         string rwkeys = cachedTx->rwkeys;
//                         boost::split(keyItems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);

//                         vector<string> participantItems;
//                         string participantIds = cachedTx->participantIds;
//                         boost::split(participantItems, participantIds, boost::is_any_of("_"), boost::token_compress_on);
                        
//                         for(int i = 0; i < participantItems.size(); i++) {
//                             if(participantItems.at(i) != to_string(dev::consensus::internal_groupId)) {
//                                 // string key = currentEpochId + "_" + to_string(currentBatchId) + "_" + keyItems.at(i);
//                                 string key = keyItems.at(i);; // 将key临时改为localrwkey！，避免处理不一致问题
//                                 rwkeyToReceive.push_back(key);
//                             }
//                         }
//                         cachedTx->rwkeyToReceive = rwkeyToReceive; // 设置交易的 rwkeyToReceive
//                         m_blockingTxQueue->insertTx(cachedTx); // 将交易插入缓存队列
//                         rwkeyToReceive.clear(); // 清空rwkeyToReceive

//                         // PLUGIN_LOG(INFO) << LOG_KV("cachedTx->participantIds", cachedTx->participantIds)
//                         //                  << LOG_KV("cachedTx->localreadwriteKey", cachedTx->localreadwriteKey)
//                         //                  << LOG_KV("cachedTx->interShardTxEpochID", cachedTx->interShardTxEpochID)
//                         //                  << LOG_KV("cachedTx->interShardTxBatchId", cachedTx->interShardTxBatchId);
//                     }
//                     rwkeyToSend.clear(); // 清空 rwkeyToSend
//                     cachedTransactions.clear(); // 清空 cachedTransactions(一个batch交易)
//                     currentBatchId++; // 更新 currentBatchId

//                     // 处理当前新batch中的第一笔交易
//                     lastParticipantIds = participantIds; // 更新lastParticipantIds
//                     if(!iter->emptyTransaction) // 非空交易执行以下步骤
//                     {
//                         string localrwkey = iter->localreadwriteKey; // 当前交易访问的本地读写集
//                         // string key = currentEpochId + "_" + to_string(currentBatchId) + "_" + localrwkey;
//                         string key = localrwkey; // 将key临时改为localrwkey！，避免处理不一致问题
//                         rwkeyToSend.push_back(key);  // 本batch 第一笔交易需要发送给其他分片的读写集
//                         iter->interShardTxBatchId = to_string(currentBatchId);
//                         cachedTransactions.push_back(iter); // 缓存该笔交易(需要接收的读写集在最后统一设置)
//                     }
//                 }
//             }
//         }

//         // 若epoch中的所有交易全部处理完毕, 开始将缓存的最后一个batch的交易插入缓存队列
//         if(processedEpochTxNum == reorderedTxs->size() - 1) { // 所有交易处理完毕

//             int cache_size = cachedTransactions.size();
//             PLUGIN_LOG(INFO) << LOG_KV("本epoch应该发送的读写集个数为", rwkeyToSend.size())
//                              << LOG_KV("cache_size", cache_size);

//             for(int i = 0; i < cache_size; i++) {
//                 auto cachedTx = cachedTransactions.at(i); // 获取交易

//                 if(i == 0) {  // 设置第一笔交易的rwkeysTosend
//                     cachedTx->setrwkeysTosend(rwkeyToSend);
//                     PLUGIN_LOG(INFO) << LOG_KV("cachedTx.tx.hash", cachedTx->tx->hash());
//                 }
//                 if(i == cache_size - 1) { // 设置epocha内最后一笔交易的标记位 lastTxInEpoch
//                     PLUGIN_LOG(INFO) << LOG_DESC("设置epoch内最后一笔交易的标记位") << LOG_KV("cachedTx->tx->hash()",cachedTx->tx->hash());
//                     cachedTx->lastTxInEpoch = true;
//                 }

//                 // PLUGIN_LOG(INFO) << LOG_KV("cachedTx->lastTxInEpoch", cachedTx->lastTxInEpoch);

//                 // 计算当前交易的 rwkeyToReceive
//                 vector<string> keyItems;
//                 string rwkeys = cachedTx->rwkeys;
//                 boost::split(keyItems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);

//                 vector<string> participantItems;
//                 string participantIds = cachedTx->participantIds;
//                 boost::split(participantItems, participantIds, boost::is_any_of("_"), boost::token_compress_on);

//                 for(int i = 0; i < participantItems.size(); i++) {
//                     if(participantItems.at(i) != to_string(dev::consensus::internal_groupId)) {
//                         // string key = currentEpochId + "_" + to_string(currentBatchId) + "_" + keyItems.at(i);
//                         string key = keyItems.at(i); // 将key临时改为localrwkey！，避免处理不一致问题
//                         rwkeyToReceive.push_back(key);
//                     }
//                 }
//                 cachedTx->rwkeyToReceive = rwkeyToReceive; // 设置交易的 rwkeyToReceive
//                 m_blockingTxQueue->insertTx(cachedTx);
//                 // PLUGIN_LOG(INFO) << LOG_KV("cachedTx->participantIds", cachedTx->participantIds)
//                 //                  << LOG_KV("cachedTx->localreadwriteKey", cachedTx->localreadwriteKey)
//                 //                  << LOG_KV("cachedTx->interShardTxEpochID", cachedTx->interShardTxEpochID)
//                 //                  << LOG_KV("cachedTx->interShardTxBatchId", cachedTx->interShardTxBatchId);
//             }
//         }
//         processedEpochTxNum++;
//         txIndex++;
//     }
// }







/**
 * NOTES: // 记录交易中的本地状态参与片内交易次数，是否某个状态参与其他分片交易的次数，远大于参与的片内交易
 * */
// void TransactionExecuter::saveKeyRemoteAccessedNumber(shared_ptr<transactionWithReadWriteSet> tx)
// {
//     string localkeys = tx->localreadwriteKey; // 交易访问的片内读写集
//     vector<string> keyItems;
//     boost::split(keyItems, localkeys, boost::is_any_of("_"), boost::token_compress_on);

//     string participants = tx->participantIds; // 交易全体参与方
//     vector<string> participantItems;
//     boost::split(participantItems, participants, boost::is_any_of("_"), boost::token_compress_on);

//     // 记录状态与其他不同分片的跨片交易次数，同时还要记录参与片内交易的次数(若频繁与某个分片发生跨片交易，则需要启动状态写权限迁移协议）
//     // 逐个状态统计
//     for(int i = 0; i < keyItems.size(); i++) {
//         string rwkey = keyItems.at(i);
//         for(int j = 0; j < participantItems.size(); j++) {
//             string participantId = participantItems.at(j);
//             if(participantId != to_string(dev::consensus::internal_groupId)) // 参与的其他分片
//             {
//                 if(remoteAccessedNumber.count(rwkey) == 0) {
//                     shared_ptr<map<string, int>> value = make_shared<map<string, int>>();
//                     value->insert(make_pair(participantId, 1));
//                     remoteAccessedNumber.insert(make_pair(rwkey, value));    
//                 }
//                 else {
//                     auto value = remoteAccessedNumber.at(rwkey);
//                     if(value->count(participantId) == 0) {
//                         value->insert(make_pair(participantId, 1));
//                     }
//                     else {
//                         int accessed_number = remoteAccessedNumber.at(rwkey)->at(participantId);
//                         remoteAccessedNumber.at(rwkey)->at(participantId) = accessed_number + 1;
//                     }
//                 }
//             }
//     }}
// }

/**
 * NOTES: // 记录交易中的本地状态参与片内交易次数，是否某个状态参与其他分片交易的次数，远大于参与的片内交易
 * */
// void TransactionExecuter::savelocalAccessedNumber(shared_ptr<Transaction> tx)
// {

// }

/**
 * NOTES: // 重载上一个函数
 * */
// void TransactionExecuter::savelocalAccessedNumber(shared_ptr<transactionWithReadWriteSet> tx)
// {

// }








// /**
//  * NOTES: 根据交易中指定的跨片交易参与方，以及当前协调者中记录的状态最新状态迁移情况，将子交易发送至正确的分片
// */
// void TransactionExecuter::accumulateSubTxs(string& data_str)
// {
//     /**
//      * 1. 积攒所有子分片以及其最新的目标分片等信息
//      * 2. 若所有的子交易的目标分片都是一样的，那么将其封装成一个片内交易
//      * 3. 否则相同目标分片的子交易封装成一个子交易
//     */
    
//     // PLUGIN_LOG(INFO) << LOG_DESC("开始积攒跨片子交易...") << LOG_KV("data_str", data_str);

//     // 积攒所有子分片以及其当前的目标分片等信息
//     vector<std::string> dataitems;
//     boost::split(dataitems, data_str, boost::is_any_of("|"), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中
//     string intershardTxid = dataitems.at(1);
//     int item_size = dataitems.size();
//     int participantNum = (item_size - 1) / 3;

//     int participantIndex = 2;
//     int txRlpIndex = 3;
//     int rwkeysIndex = 4;

//     string txRlp = "";
//     string rwSets = "";

//     // PLUGIN_LOG(INFO) << LOG_KV("intershardTxid", intershardTxid)
//     //                  << LOG_KV("participantNum", participantNum);

//     map<string, vector<subInterShardTx>> shardid2subTxs; // 缓存即将发给不同分片的子交易
//     for(size_t j = 0; j < participantNum; j++) // 积攒准备发给不同分片的跨片子交易
//     {
//         // 获取子交易信息，原目标分片ID、rlp和访问的读写集
//         string original_destinshardid = dataitems.at(participantIndex); // 原目标分片ID
//         string txRlp = dataitems.at(txRlpIndex); // rlp
//         string rwkeys = dataitems.at(rwkeysIndex).c_str();

//         // PLUGIN_LOG(INFO) << LOG_KV("original_destinshardid", original_destinshardid)
//         //                  << LOG_KV("txRlp", txRlp)
//         //                  << LOG_KV("rwkeys", rwkeys);

//         vector<string> rwkeyitems;
//         boost::split(rwkeyitems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);
//         string rwkey = rwkeyitems.at(j).c_str(); // 跨片子交易访问子分片的读写集

//         // 检查key当前的主分片是否发生变化
//         string current_destinshardid = original_destinshardid;
//         if(dev::plugin::m_masterChangedKey->count(rwkey) != 0) {
//             current_destinshardid = to_string(dev::plugin::m_masterChangedKey->at(rwkey)); // 子交易的最新目标分片Id
//         }

//         // 缓存交易
//         subInterShardTx subtx{current_destinshardid, txRlp, rwkey};
//         if(shardid2subTxs.count(current_destinshardid) != 0) {
//             shardid2subTxs.at(current_destinshardid).push_back(subtx);
//         }
//         else{
//             vector<subInterShardTx> subTxs;
//             subTxs.push_back(subtx);
//             shardid2subTxs.insert(make_pair(current_destinshardid, subTxs));
//         }

//         // 更新索引下标
//         participantIndex += 3;
//         txRlpIndex += 3;
//         rwkeysIndex += 3;
//     }

//     // 输出刚刚积攒的跨片子交易
//     // PLUGIN_LOG(INFO) << LOG_DESC("输出积攒的跨片子交易");

//     // for(auto it = shardid2subTxs.begin(); it != shardid2subTxs.end(); it++) {
//     //     string destshardid = it->first;
//     //     vector<subInterShardTx> subIntershardTxs = it->second;
//     //     PLUGIN_LOG(INFO) << LOG_KV("destshardid", destshardid)
//     //                      << LOG_KV("subIntershardTxs_size", subIntershardTxs.size());

//     //     // for(auto it = subIntershardTxs.begin(); it != subIntershardTxs.end(); it++) {
//     //     //     auto destshardid = it->dest_shardid;
//     //     //     auto txrlp = it->txrlp;
//     //     //     auto rwkey = it->rwkey;

//     //     //     PLUGIN_LOG(INFO) << LOG_KV("destshardid", destshardid)
//     //     //                      << LOG_KV("txrlp", txrlp)
//     //     //                      << LOG_KV("rwkey", rwkey);
//     //     // }
//     // }





//     // 此处逻辑存在问题！

//     vector<int> partipantshardids;
//     if(shardid2subTxs.size() != 1) // 依然为跨片交易(有多笔子交易)
//     {
//         // PLUGIN_LOG(INFO) << LOG_DESC("交易依然为跨片交易...");

//         string shardids = ""; // 跨片交易当前的所有参与方shardid
//         string rwkeys = ""; // 跨片交易需要访问的所有状态key
//         for (auto it = shardid2subTxs.begin(); it != shardid2subTxs.end(); it++)
//         {
//             string destshardid = it->first;
//             vector<subInterShardTx> subtxs = it->second; // 函数内每次只处理一笔交易

//             partipantshardids.push_back(atoi(destshardid.c_str()));
//             // 更新 shardids
//             if(shardids == ""){
//                 shardids = destshardid;
//             }
//             else{
//                 shardids = shardids + "_" + destshardid;
//             }

//             // 更新 rwkeys
//             auto subtx = subtxs.at(0); // 只有1笔
//             string rwkey = subtx.rwkey;
//             if(rwkeys == "") {
//                 rwkeys = rwkey;
//             }
//             else{
//                 rwkeys = rwkeys + "_" + rwkey;
//             }
//         }

//         // PLUGIN_LOG(INFO) << LOG_KV("shardids", shardids)
//         //                  << LOG_KV("rwkeys", rwkeys);

//         string subInterShardTransactionContent = "";
//         string subInterShardTransactionInfo = "";
//         // 获取实际的 txrlp
//         for (auto it = shardid2subTxs.begin(); it != shardid2subTxs.end(); it++) {
//             string txrlp = "";
//             string destshardid = it->first;
//             vector<subInterShardTx> subtxs = it->second;

//             // // PLUGIN_LOG(INFO) << LOG_KV("destshardid", destshardid);
//             // // 遍历subtxs
//             // size_t subtxs_size = subtxs.size();
//             // for(size_t i = 0; i < subtxs_size; i++)
//             // {
//             //     auto subtx = subtxs.at(i);
//             //     string txrlp = subtx.txrlp;
                
//             //     if(txrlps == "") { // 更新 txrlps
//             //         txrlps = txrlp;
//             //     }
//             //     else {
//             //         txrlps = txrlps + "_" + txrlp;
//             //     }
//             // }

//             auto subtx = subtxs.at(0);
//             txrlp = subtx.txrlp;

//             // 保存子交易
//             // 格式: interShardTxId|shardids(所有子交易的shardid)|txrlp_txrlp(部分rlp)...|rwkeys_rwkeys(所有key)...
//             // 格式: interShardTxId|shardids|none|rwkeys_rwkeys...
//             string flag = "|";
//             subInterShardTransactionContent = intershardTxid + flag + shardids + flag + txrlp + flag + rwkeys;
//             subInterShardTransactionInfo = intershardTxid + flag + shardids + flag + "none" + flag + rwkeys;
//             auto cachedSubTxsContainer = m_subTxsContainers->at(atoi(destshardid.c_str())); // 智能指针类型
//             cachedSubTxsContainer->insertSubTx(subInterShardTransactionContent); // 并发安全，这里仅先测试2分片
//         }

//         // 积攒跨片子交易，非参与方积攒key的读写集情况
//         for(int i = 0; i < lower_groupids.size(); i++)
//         {
//             int groupId = lower_groupids.at(i); // 遍历下层分片
//             vector<int>::iterator it;
//             it = find(partipantshardids.begin(), partipantshardids.end(), groupId);
//             if(it == partipantshardids.end())
//             {
//                 auto cachedSubTxsContainer = m_subTxsContainers->at(groupId); // 智能指针类型
//                 cachedSubTxsContainer->insertSubTx(subInterShardTransactionInfo); // 并发安全，这里仅先测试2分片
//             }
//         }
//         partipantshardids.clear();

//         // 输出积攒的跨片/非跨片子交易
//         for(auto it = m_subTxsContainers->begin(); it != m_subTxsContainers->end(); it++) {
//             int destshardid = it->first;
//             auto cachedSubTxsContainer = it->second;

//             auto m_subtxs = cachedSubTxsContainer->m_subtxs;
//             auto m_intrashardtxs = cachedSubTxsContainer->m_intrashardtxs;

//             // PLUGIN_LOG(INFO) << LOG_DESC("输出积攒的跨片/非跨片子交易")
//             //                  << LOG_KV("destshardid", destshardid)
//             //                  << LOG_KV("m_subtxs", *m_subtxs)
//             //                  << LOG_KV("m_intrashardtxs", *m_intrashardtxs);
//         }
//     }
//     else if(shardid2subTxs.size() == 1) { // 交易已经转为片内交易
//         PLUGIN_LOG(INFO) << LOG_DESC("交易已经转为片内交易...");
//         // 获取完整的 rwkeys 和 txrlps
//         string rwkeys = "";
//         string txrlps = "";
//         string destshardid = "";

//         for (auto it = shardid2subTxs.begin(); it != shardid2subTxs.end(); it++) {
//             destshardid = it->first;
//             vector<subInterShardTx> subtxs = it->second;

//             // 遍历subtxs
//             size_t subtxs_size = subtxs.size();
//             for(size_t i = 0; i < subtxs_size; i++)
//             {
//                 // 更新 rwkeys
//                 auto subtx = subtxs.at(i);
//                 string rwkey = subtx.rwkey;
//                 if(rwkeys == "") {
//                     rwkeys = rwkey;
//                 }
//                 else{
//                     rwkeys = rwkeys + "_" + rwkey;
//                 }

//                 string txrlp = subtx.txrlp;
//                 if(txrlps == "") { // 更新 txrlps
//                     txrlps = txrlp;
//                 }
//                 else{
//                     txrlps = txrlps + "_" + txrlp;
//                 }
//             }
//         }

//         // 开始组装
//         // string txLabel = "0x222333444";
//         string flag = "|";
//         string subIntraShardTransactionContent = txrlps + flag + rwkeys; // rlp_rlp...|key1_key2...
//         auto cachedIntraTxsSet = m_subTxsContainers->at(atoi(destshardid.c_str())); // 智能指针类型
//         cachedIntraTxsSet->insertIntraShardTx(subIntraShardTransactionContent);  // 插入交易
//     }
// }

// /**
//  * NOTES: 暂未启用
//  * */
// void TransactionExecuter::compareStateContention(std::string& crossshardtxid, std::vector<std::string>& participantItems, 
//                                             std::vector<std::string>& rwKeyItems, int itemIndex)
// {
//     PLUGIN_LOG(INFO) << LOG_DESC("开始比较不同分片的状态征用率...");

//     // std::string localRWKey = crossshardtxid + "_" + rwKeyItems[itemIndex].c_str();
//     std::string localRWKey = rwKeyItems[itemIndex].c_str();
//     auto localRWAccessNum = m_executeTxPool->receivedAccessNum->at(localRWKey);
//     PLUGIN_LOG(INFO) << LOG_KV("localRWKey", localRWKey)
//                      << LOG_KV("localRWAccessNum", localRWAccessNum);

//     auto participant_size = participantItems.size();
//     for(size_t i = 0; i < participant_size; i++)
//     {
//         if(i != itemIndex)
//         {
//             // std::string rwkey = crossshardtxid + "_" + rwKeyItems[i].c_str();
//             std::string rwkey = rwKeyItems[i].c_str();
//             auto rwAccessNum = m_executeTxPool->receivedAccessNum->at(rwkey);

//             PLUGIN_LOG(INFO) << LOG_KV("rwkey", rwkey) << LOG_KV("rwAccessNum", rwAccessNum);

//             //if((localRWKey > rwAccessNum + 30) && 
//             if(dev::plugin::nodeIdHex == toHex(dev::consensus::forwardNodeId.at(dev::consensus::internal_groupId - 1)) 
//                 && dev::consensus::internal_groupId == 2) // 本分片内的转发节点向上层公共祖先分片发送写权限更改请求(假设分片2发送)
//             {
//                 // 为了测试，假设当前本地对状态localRWKey的征用远高于对方分片，开始向上层分片请求写权限
//                 /* ......  */
//                 std::string ancestorGroupId = "1"; // 先写死，后面需要补充自动获取两个分片的最近公共祖先id功能
//                 std::string sourceShardId = std::to_string(dev::consensus::internal_groupId);
//                 std::string destinShardId = participantItems.at(i);
//                 std::string readwritekey = rwKeyItems[i].c_str();

//                 requestForMasterChange(ancestorGroupId, sourceShardId, destinShardId, readwritekey, crossshardtxid);
//             }
//         }
//     }
// }

// /**
//  * NOTES: 暂未启用
//  * */
// void TransactionExecuter::insertInterShardTxReadWriteSetInfo(std::string &_crossshardtxid, int _participantNum)
// {
//     remoteReadWriteSetNum.insert(std::make_pair(_crossshardtxid, _participantNum)); // 收齐大多数即可
// }

// void TransactionExecuter::processSubInterShardTransaction(std::shared_ptr<dev::eth::Transaction> tx)
// {
//     auto transaction_info = dev::rpc::corsstxhash2transaction_info.at(tx->hash());
//     int readwritesetnum = transaction_info.readwritesetnum;
//     std::string crossshardtxid = transaction_info.crossshardtxid;
//     std::string participants = transaction_info.participants;
//     std::string readwritekeys = transaction_info.readwrite_key;
//     std::string sourceshardid = std::to_string(transaction_info.source_shard_id);

//     auto dest = tx->receiveAddress();
//     dev::plugin::depositAddress == dest;

//     PLUGIN_LOG(INFO) << LOG_DESC("该交易为跨片子交易")
//                         << LOG_KV("readwritesetnum", readwritesetnum)
//                         << LOG_KV("crossshardtxid", crossshardtxid)
//                         << LOG_KV("participants", participants)
//                         << LOG_KV("readwritekeys", readwritekeys);
    
//     int itemIndex = 0;
//     std::vector<std::string> dataItems;
//     boost::split(dataItems, participants, boost::is_any_of("|"), boost::token_compress_on);
//     int itemNum = dataItems.size();
//     for(size_t i = 0; i < itemNum; i++)
//     {
//         if(dataItems.at(i) == to_string(dev::consensus::internal_groupId))
//         {
//             itemIndex = i;
//             break;
//         }
//     }

//     dev::plugin::crossshardtxid2SourceShardId.insert(std::make_pair(crossshardtxid, sourceshardid));
//     boost::split(dataItems, readwritekeys, boost::is_any_of("_"), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中
//     std::string rwkey = dataItems.at(itemIndex).c_str(); // 跨片交易阻塞的片内读写集
//     PLUGIN_LOG(INFO) << LOG_KV("缓存交易时rwkey", rwkey);

//     m_accessedKeys->push(rwkey); // 将访问的读写集压入队列
//     if(m_accessedKeys->size() > 100) {
//         m_accessedKeys->pop();
//     }

//     // 将交易包装成transactionWithReadWriteSet类型
//     auto txwithrwset = std::make_shared<dev::plugin::transactionWithReadWriteSet>(tx);
//     m_executeTxPool->insertInterShardTx(txwithrwset, rwkey); // 缓存该笔跨片交易
//     // m_blockingTxQueue->insertTx(cachedTx);

//     m_executeTxPool->csTxRWsetNum->insert(std::make_pair(crossshardtxid, itemNum)); // 缓存该笔交易应当收到的读写集个数
// }


// int TransactionExecuter::getStateContention(std::string& rwkey)
// {
//     // 查看片内近100笔交易中，读写集 rwkey 被访问的次数
//     std::map<std::string, int> keyStatistics;
//     int key_size = m_accessedKeys->size();

//     for(size_t i = 0; i < key_size; i++)
//     {
//         auto key = m_accessedKeys->front();
//         if(keyStatistics.count(key) == 0){
//             keyStatistics.insert(std::make_pair(key, 1));
//         }
//         else{
//             int number = keyStatistics.at(key);
//             number++;
//             keyStatistics.at(key) = number;
//         }
//         m_accessedKeys->push(m_accessedKeys->front());
//         m_accessedKeys->pop();
//     }

//     int accessedNum;
//     if(keyStatistics.count(rwkey) == 0)
//     {
//         accessedNum = 0;
//     }
//     else
//     {
//         accessedNum = keyStatistics.at(rwkey);
//     }
//     return accessedNum;
// }


// int TransactionExecuter::getRemoteStateAccessNum(std::string& rwkey)
// {
//     // 查看片内近100笔交易中，读写集 rwkey 被访问的次数
//     std::map<std::string, int> keyStatistics;
//     int key_size = m_remoteAccessedKeys->size();

//     for(size_t i = 0; i < key_size; i++)
//     {
//         auto key = m_remoteAccessedKeys->front();
//         if(keyStatistics.count(key) == 0){
//             keyStatistics.insert(std::make_pair(key, 1));
//         }
//         else{
//             int number = keyStatistics.at(key);
//             number++;
//             keyStatistics.at(key) = number;
//         }
//         m_remoteAccessedKeys->push(m_remoteAccessedKeys->front());
//         m_remoteAccessedKeys->pop();
//     }

//     int accessedNum;
//     if(keyStatistics.count(rwkey) == 0)
//     {
//         accessedNum = 0;
//     }
//     else
//     {
//         accessedNum = keyStatistics.at(rwkey);
//     }
//     return accessedNum;
// }

// /** 
//  * NOTES: 线程轮询处理被阻塞的转换后的交易
//  * */
// void TransactionExecuter::processvice_blockedTxs()
// {
//     while(true)
//     {
//         if(blockedTxs.size() == 0)
//         {
//             this_thread::sleep_for(chrono::milliseconds(10));
//         }
//         else
//         {
//             if(vicewriteBlocked == false)
//             {
//                 int blockedTx_size = blockedTxs.size();   // 有一点不精准
//                 for(int i = 0; i < blockedTx_size; i++)
//                 {
//                     auto item = blockedTxs.front();
//                     string shardid = item.at(0);
//                     string txrlp = item.at(1);
//                     Transaction::Ptr tx = std::make_shared<Transaction>(jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
//                     executeTx(tx, false);

//                     m_processed_intrashardTxNum++;
//                     if(m_processed_intrashardTxNum % 1000 == 0){
//                         calculateTPS();
//                         PLUGIN_LOG(INFO) << LOG_KV("已经执行交易数目为(In processBatchSubInterShardTxs)", m_processed_intrashardTxNum);
//                     }
//                 }
//             }
//         }
//     }
// }


// /** 
//  * NOTES: 线程轮询处理被阻塞的转换后的交易
//  * */
// void TransactionExecuter::processblocked_intraTxContent()
// {
//     while(true)
//     {
//         if(size_blocked_intraTxContent() == 0)
//         {
//             this_thread::sleep_for(chrono::milliseconds(10));
//         }
//         else
//         {
//             if(masterwriteBlocked == false && size_blocked_intraTxContent() > 0) // 此时主写未被阻塞, 将 m_blocked_intraTxContents 中当前阻塞的多写交易不停的拿出来执行
//             {
//                 string convertedIntraTxContents = front_blocked_intraTxContent();
//                 // 检查是否有重组片内交易，若有则进行处理，直接当片内交易处理
//                 vector<string> convertedIntraTxs;
//                 boost::split(convertedIntraTxs, convertedIntraTxContents, boost::is_any_of("&"), boost::token_compress_on);
//                 if(convertedIntraTxs.size() > 1) //  rlp_rlp...|key1_key2... & rlp_rlp...|key1_key2...
//                 {
//                     PLUGIN_LOG(INFO) << LOG_DESC("发现重组片内交易");
//                     int convertedIntraTx_size = convertedIntraTxs.size();
//                     for(int i = 0; i < convertedIntraTx_size; i++)
//                     {
//                         string intraTxContent = convertedIntraTxs.at(i);
//                         vector<string> contentItems;
//                         boost::split(contentItems, intraTxContent, boost::is_any_of("|"), boost::token_compress_on);
//                         string txrlps = contentItems.at(0);
//                         string rwkeys = contentItems.at(1);
//                         vector<string> txrlpItems; // txrlps --> tx
//                         boost::split(txrlpItems, txrlps, boost::is_any_of("_"), boost::token_compress_on);

//                         vector<Transaction::Ptr> subtxs;
//                         for(int i = 0; i < txrlpItems.size(); i++) {
//                             auto tx = make_shared<Transaction>(jsToBytes(txrlpItems.at(i), OnFailed::Throw), CheckTransaction::Everything); // 包装成交易
//                             subtxs.push_back(tx);
//                         }

//                         vector<string> keyItems; // 记录转换后的片内访问的片内状态
//                         boost::split(keyItems, rwkeys, boost::is_any_of("_"), boost::token_compress_on);
//                         int rwkeys_size = keyItems.size();
//                         for(int i = 0; i < rwkeys_size; i++) {
//                             string localkey = keyItems.at(i);
//                             m_LRU_StatesInfo->updateLocalKeyContentionByIntraTx(localkey);
//                         }
                        
//                         bool isblocked = false; // 检查当前交易的读写集key是否被阻塞，若交易的读写集未被阻塞, 立即执行交易，否则缓存交易
//                         int rwkey_size = keyItems.size();
//                         for(int i = 0; i < rwkey_size; i++) {
//                             string rwkey = keyItems.at(i);
//                             if(m_blockingTxQueue->isBlocked(rwkey) == true) {
//                                 isblocked = true;
//                                 break;
//                         }}

//                         if(isblocked == false) { // 若交易的读写集未被阻塞, 立即执行交易
//                             // PLUGIN_LOG(INFO) << LOG_DESC("重组交易未被阻塞, 立即执行");
//                             int subtxs_size = subtxs.size();
//                             for(size_t i = 0; i < subtxs_size; i++){
//                                 auto tx = subtxs.at(i);
//                                 executeTx(tx, false);
//                                 // executeTx(tx); // 执行所有子交易
//                             }

//                             m_processed_intrashardTxNum++;
//                             if(m_processed_intrashardTxNum % 1000 == 0){
//                                 calculateTPS();
//                                 PLUGIN_LOG(INFO) << LOG_KV("已经执行交易数目为(In processBatchSubInterShardTxs)", m_processed_intrashardTxNum);
//                             }
//                         }
//                         else { // 交易被阻塞，缓存交易
//                             // PLUGIN_LOG(INFO) << LOG_DESC("重组交易被阻塞，开始缓存");
//                             auto txwithrwset = std::make_shared<dev::plugin::transactionWithReadWriteSet>();
//                             txwithrwset->rwkeys = rwkeys;
//                             int subtxs_size = subtxs.size();
//                             for(int i = 0; i < subtxs_size; i++) {
//                                 auto tx = subtxs.at(i);
//                                 txwithrwset->txs.push_back(tx);
//                             }

//                             txwithrwset->is_intershardTx = false;
//                             m_blockingTxQueue->insertTx(txwithrwset);
//                         }
//                     }
//                 }
//                 pop_blocked_intraTxContent();
//             }
//         }
//     }
// }

    }
}
