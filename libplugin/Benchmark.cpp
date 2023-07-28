#include "Benchmark.h"
#include "libdevcore/Log.h"
#include <libplugin/ExecuteVM.h>
#include <libplugin/Common.h>
#include <librpc/Rpc.h>
#include <libdevcore/Address.h>
#include <libconsensus/pbft/Common.h>
#include <libethcore/ABI.h>
#include <algorithm>

using namespace std;
using namespace dev;
using namespace dev::plugin;

// bytes To string
string injectTxs::dataToHexString(bytes data)
{
    string res2 = "";
    string temp;
    stringstream ioss;

    int count = 0;
    for(auto const &ele:data)
    {
        count++;
        ioss << std::hex << ele;

        if(count > 30)
        {
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

// 注入部署合约交易
void injectTxs::deployContractTransaction(string& filename, int32_t groupId)
{
    ifstream infile(filename, ios::binary); //deploy.json
    Json::Reader reader;
    Json::Value root;

    // 加载交易
    if(reader.parse(infile, root)) {
        int tx_size = root.size();
        for(int i = 0; i < tx_size; i++) {
            string txrlp = root[i].asString();
            Transaction::Ptr tx = std::make_shared<Transaction>(
                jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
            deploytxs.push_back(tx);
        }
    }

    // 往交易池中导入交易
    int inputTxsize = deploytxs.size();
    auto txPool = m_ledgerManager->txPool(dev::consensus::internal_groupId);
    for(int i = 0; i < inputTxsize; i++) {
        auto tx = deploytxs.at(i);
        string txrlp = toHex(tx->rlp());
        m_rpcService->sendRawTransaction(dev::consensus::internal_groupId, txrlp);
    }

    infile.close();
    PLUGIN_LOG(INFO) << LOG_DESC("部署合约交易完成...");
}

string injectTxs::generateTx(string& requestLabel, string& stateAddress, int32_t shardid, int32_t txid, bool subtx, bool crossLayer)
{
    // 生成交易
    string flag = "|";
    string hex_m_data_str = "";

    if(requestLabel == "" && stateAddress == ""){
        if (crossLayer) {
            hex_m_data_str = flag + toHex("b" + to_string(txid)) + flag;
        } else {
            hex_m_data_str = flag + toHex("a" + to_string(txid)) + flag;
        }
    }
    else{
        hex_m_data_str = requestLabel + flag + stateAddress + flag + to_string(txid);
    }

    string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";

    dev::Address contactAddress(contract_address);
    dev::eth::ContractABI abi;
    bytes data = abi.abiIn("add(string)", hex_m_data_str);  // add

    Transaction tx(0, 1000, 0, contactAddress, data);
    tx.setNonce(generateRandomValue());
    tx.setGroupId(shardid);
    if(subtx == true){
        tx.setBlockLimit(u256(m_ledgerManager->blockChain(shardid)->number()) + 500);
    }
    
    auto keyPair = KeyPair::create();
    auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
    tx.updateSignature(sig);

    auto rlp = tx.rlp();
    return toHex(rlp);
}

void injectTxs::injectionTransactions(string& intrashardworkload_filename, string& intershardworkload_filename, int intratxNum, int intertxNum)
{
    int SPEED=5000;
    // 只导入片内交易(只需转发节点负责)
    if(intratxNum != 0 && intertxNum == 0 && hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){

        vector<string> txids;
        vector<string> txRLPS;
        int inputTxsize = intratxNum;
        PLUGIN_LOG(INFO) << LOG_KV("即将导入的片内交易总数", intratxNum);

        ifstream infile(intrashardworkload_filename, ios::binary); // signedtxs.json
        Json::Reader reader;
        Json::Value root;

        // 加载交易
        if(reader.parse(infile, root)) {
            for(int i = 0; i < inputTxsize; i++) {
                string txrlp = root[i].asString();
                txRLPS.push_back(txrlp);

                // 解析交易data字段，获取交易txid
                Transaction::Ptr tx = std::make_shared<Transaction>(
                    jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
                string data_str = dataToHexString(tx->get_data());
                vector<string> dataItems;
                boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                string txid = dataItems.at(2).c_str();
                txids.push_back(txid);
            }
        }

        // 正式投递到交易池
        for(int i = 0; i < inputTxsize; i++) {
            if(i % SPEED == 0){
                std::this_thread::sleep_for(std::chrono::seconds(1)); // 暂停10秒，等所有服务启动完毕
            }
            m_rpcService->sendRawTransaction(dev::consensus::internal_groupId, txRLPS.at(i));
            // string txid = txids.at(i);
            // struct timeval tv;
            // gettimeofday(&tv, NULL);
            // int time_sec = (int)tv.tv_sec;
            // m_txid_to_starttime->insert(make_pair(txid, time_sec)); // 记录txid的开始时间
        }
        infile.close();
    }

    // 只导入跨片交易
    else if(intratxNum == 0 && intertxNum != 0 && hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){ 

        vector<string> txids;
        vector<string> txRLPS;
        int inputTxsize = intertxNum;
        PLUGIN_LOG(INFO) << LOG_KV("intershardworkload_filename", intershardworkload_filename)
                         << LOG_KV("即将导入的跨片交易总数", inputTxsize);

        ifstream infile(intershardworkload_filename, ios::binary); // signedtxs.json
        Json::Reader reader;
        Json::Value root;

        // 加载交易
        if(reader.parse(infile, root)) {
            for(int i = 0; i < inputTxsize; i++) {
                string txrlp = root[i].asString();
                txRLPS.push_back(txrlp);

                // 解析交易data字段，获取交易txid
                Transaction::Ptr tx = std::make_shared<Transaction>(
                    jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
                string data_str = dataToHexString(tx->get_data());
                vector<string> dataItems;
                boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                string txid = dataItems.at(1).c_str();
                txids.push_back(txid);
            }
        }

        // 正式投递到交易池
        for(int i = 0; i < inputTxsize; i++) {
            if(i % SPEED == 0){
                std::this_thread::sleep_for(std::chrono::seconds(1)); // 暂停10秒，等所有服务启动完毕
            }
            m_rpcService->sendRawTransaction(dev::consensus::internal_groupId, txRLPS.at(i));
            // string txid = txids.at(i);
            // struct timeval tv;
            // gettimeofday(&tv, NULL);
            // int time_sec = (int)tv.tv_sec;
            // m_txid_to_starttime->insert(make_pair(txid, time_sec)); // 记录txid的开始时间
        }
        infile.close();
        PLUGIN_LOG(INFO) << LOG_KV("跨片交易导入完毕", intertxNum);
    }

    // 片内交易+跨片交易
    else if(intratxNum != 0 && intertxNum != 0 && hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){

        vector<string> txids;
        vector<string> txRLPS;
        int inputTxsize = intertxNum;
        PLUGIN_LOG(INFO) << LOG_KV("即将导入的 片内+跨片 交易总数", intratxNum+intertxNum);

        // 导入片内交易
        ifstream infile1(intrashardworkload_filename, ios::binary);
        Json::Reader reader;
        Json::Value root;
        // 加载交易
        if(reader.parse(infile1, root)) {
            for(int i = 0; i < intratxNum; i++) {
                string txrlp = root[i].asString();
                txRLPS.push_back(txrlp);
            }
        }

        // 导入跨片交易
        ifstream infile2(intershardworkload_filename, ios::binary);
        // 加载交易
        if(reader.parse(infile2, root)) {
            for(int i = 0; i < intertxNum; i++) {
                string txrlp = root[i].asString();
                txRLPS.push_back(txrlp);
            }
        }

        // 将txRLPS打乱
        auto start = txRLPS.begin();
        auto end = txRLPS.end();
        srand(time(NULL));
        random_shuffle(start, end);

        //temp
        int ii=0;

        // 将txRLPS中的交易导入交易池, 且记录下开始时间
        for(auto it = start; it != end; it++){
            ii++;
            if(ii % SPEED == 0){
                std::this_thread::sleep_for(std::chrono::seconds(1)); // 暂停1秒，等所有服务启动完毕
            }
            string txrlp = *it;
            m_rpcService->sendRawTransaction(dev::consensus::internal_groupId, txrlp);

            // 获取交易id, 记录交易开始时间
            string txid;
            Transaction::Ptr tx = std::make_shared<Transaction>(
                jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
            string data_str = dataToHexString(tx->get_data());
            if(data_str.find("0x444555666", 0) != -1){ // 片内交易
                vector<string> dataItems;
                boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                txid = dataItems.at(2).c_str();
            }
            else if(data_str.find("0x111222333", 0) != -1){ // 跨片交易
                vector<string> dataItems;
                boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                txid = dataItems.at(1).c_str();
            }

            // struct timeval tv;
            // gettimeofday(&tv, NULL);
            // int time_sec = (int)tv.tv_sec;
            // m_txid_to_starttime->insert(make_pair(txid, time_sec));
        }

        infile1.close();
        infile2.close();
    }
}

void injectTxs::injectionTransactions(string& intrashardworkload_filename,
                                                     string& intershardworkload_filename,
                                                     string& crosslayerworkload_filename,
                                                     int intratxNum,
                                                     int intertxNum,
                                                     int crosslayerNum,
                                                     int threadId)
{
    LOG(INFO)<<LOG_DESC("查看num")<<LOG_KV("intratxNum", intratxNum)<<LOG_KV("intertxNum", intertxNum)<<LOG_KV("crosslayerNum", crosslayerNum);
    // int SPEED = 4000;
    // int SPEED = 2500; // 速率用想要的速率除以线程数 5000 / 2
    int SPEED = dev::plugin::injectSpeed;
    int baseNum_Piece = 1.0 * min(dev::plugin::intra_generateNumber, dev::plugin::inter_generateNumber) / float(dev::plugin::inject_threadNumber);
    int baseNum = (threadId - 1) * baseNum_Piece;
    // 只导入片内交易(只需转发节点负责)
    if(intratxNum != 0 && intertxNum == 0 && crosslayerNum == 0 
        && hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
        vector<string> txids;
        vector<string> txRLPS;
        int inputTxsize = intratxNum;
        PLUGIN_LOG(INFO) << LOG_KV("即将导入的片内交易总数", intratxNum);

        ifstream infile(intrashardworkload_filename, ios::binary); // signedtxs.json
        Json::Reader reader;
        Json::Value root;

        // 加载交易
        if(reader.parse(infile, root)) {
            PLUGIN_LOG(INFO) << LOG_KV("thread id is in", threadId)
                             << LOG_KV("fileName", intrashardworkload_filename);
            
            for(int i = baseNum; i < inputTxsize + baseNum; i++) {
                string txrlp = root[i].asString();
                txRLPS.push_back(txrlp);

                // 解析交易data字段，获取交易txid
                Transaction::Ptr tx = std::make_shared<Transaction>(
                    jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
                string data_str = dataToHexString(tx->get_data());
                vector<string> dataItems;
                boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                string txid = dataItems.at(2).c_str();
                // PLUGIN_LOG(INFO) << LOG_DESC("片内交易")
                //                  << LOG_KV("txid", txid);
                txids.push_back(txid);
            }
        }
        infile.close();

        // 正式投递到交易池
        for(int i = 0; i < inputTxsize; i++) {

            if(i != 0 && i % SPEED == 0){
                std::this_thread::sleep_for(std::chrono::seconds(1)); // sleep 1s
            }

            m_rpcService->sendRawTransaction(dev::consensus::internal_groupId, txRLPS.at(i));

            string txid = txids.at(i);
            struct timeval tv;
            gettimeofday(&tv, NULL);
            long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;
            m_txid_to_starttime->insert(make_pair(txid, time_msec)); // 记录txid的开始时间

            // PLUGIN_LOG(INFO) << LOG_KV("inject txid", txid)
            //                  << LOG_KV("time_msec", time_msec);  


        }
    }
    // 只导入跨片交易
    else if(intratxNum == 0 && (intertxNum != 0 || crosslayerNum != 0)
            && hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){ 
        vector<string> txids;
        vector<string> txRLPS;
        int inputTxsize = intertxNum + crosslayerNum;
        PLUGIN_LOG(INFO) << LOG_KV("即将导入的跨片交易总数", intertxNum + crosslayerNum)
                         << LOG_KV("intershardworkload_filename", intershardworkload_filename)
                        << LOG_KV("crosslayerworkload_filename", crosslayerworkload_filename);


        if (intertxNum != 0) {
            ifstream infile(intershardworkload_filename, ios::binary); // signedtxs.json
            Json::Reader reader;
            Json::Value root;

            // 加载交易
            if(reader.parse(infile, root)) {
                PLUGIN_LOG(INFO) << LOG_KV("thread id is in", threadId)
                                 << LOG_KV("fileName", intrashardworkload_filename);

                for(int i = baseNum; i < intertxNum + baseNum; i++) {
                    string txrlp = root[i].asString();
                    txRLPS.push_back(txrlp);

                    // 解析交易data字段，获取交易txid
                    Transaction::Ptr tx = std::make_shared<Transaction>(
                        jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
                    string data_str = dataToHexString(tx->get_data());
                    // PLUGIN_LOG(INFO) << LOG_KV("transaction data_str", data_str);
                    vector<string> dataItems;
                    boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                    string txid = dataItems.at(1).c_str();
                    txids.push_back(txid);
                }
            }
            infile.close();
        }

        if (crosslayerNum != 0) {
            ifstream infile(crosslayerworkload_filename, ios::binary); // signedtxs.json
            Json::Reader reader;
            Json::Value root;

            // 加载交易
            if(reader.parse(infile, root)) {
                PLUGIN_LOG(INFO) << LOG_KV("thread id is in", threadId)
                                 << LOG_KV("fileName", intrashardworkload_filename);

                for(int i = baseNum; i < crosslayerNum + baseNum; i++) {
                    string txrlp = root[i].asString();
                    txRLPS.push_back(txrlp);

                    // 解析交易data字段，获取交易txid
                    Transaction::Ptr tx = std::make_shared<Transaction>(
                        jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
                    string data_str = dataToHexString(tx->get_data());
                    vector<string> dataItems;
                    boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                    string txid = dataItems.at(1).c_str();
                    txids.push_back(txid);
                }
            }

            infile.close();
        }

        // 正式投递到交易池

        PLUGIN_LOG(INFO) << LOG_KV("inputTxsize", inputTxsize)
                         << LOG_KV("txRLPS_size", txRLPS.size());


        for(int i = 0; i < inputTxsize; i++) {

            if(i != 0 && i % SPEED == 0){
                std::this_thread::sleep_for(std::chrono::seconds(1)); // sleep 1s
            }

            m_rpcService->sendRawTransaction(dev::consensus::internal_groupId, txRLPS.at(i));

            string txid = txids.at(i);
            struct timeval tv;
            gettimeofday(&tv, NULL);
            long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;
            m_txid_to_starttime->insert(make_pair(txid, time_msec)); // 记录txid的开始时间

            if(txid == "a3084"){
                PLUGIN_LOG(INFO) << LOG_KV("txid", txid) << LOG_KV("time_usec", time_msec);
            }
        }
    }
    // 片内交易+跨片交易
    else if(intratxNum != 0 && (intertxNum != 0 || crosslayerNum != 0)
            && hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
        vector<string> txids;
        vector<string> txRLPS;
        PLUGIN_LOG(INFO) << LOG_KV("即将导入的 片内+跨片 交易总数", intratxNum+intertxNum+crosslayerNum);

        // 导入片内交易
        ifstream infile1(intrashardworkload_filename, ios::binary);
        Json::Reader reader;
        Json::Value root;
        // 加载交易
        if(reader.parse(infile1, root)) {
            for(int i = baseNum; i < intratxNum + baseNum; i++) {
                string txrlp = root[i].asString();
                txRLPS.push_back(txrlp);
            }
        }
        infile1.close();

        // 导入跨片交易
        if (intertxNum != 0) {
            ifstream infile2(intershardworkload_filename, ios::binary);
            PLUGIN_LOG(INFO) << LOG_DESC("AAAAAAAAA");
            // 加载交易
            if(reader.parse(infile2, root)) {
                for(int i = baseNum; i < intertxNum + baseNum; i++) {
                    string txrlp = root[i].asString();
                    txRLPS.push_back(txrlp);
                }
            }
            PLUGIN_LOG(INFO) << LOG_DESC("CCCCCCCCCC");
            infile2.close();
        }

        if (crosslayerNum != 0) {
            ifstream infile2(crosslayerworkload_filename, ios::binary);
            // 加载交易
            if(reader.parse(infile2, root)) {
                for(int i = baseNum; i < crosslayerNum + baseNum; i++) {
                    string txrlp = root[i].asString();
                    txRLPS.push_back(txrlp);
                }
            }
            infile2.close();
        }

        // // 将txRLPS打乱
        auto start = txRLPS.begin();
        auto end = txRLPS.end();
        srand(time(NULL));
        random_shuffle(start, end);

        int ii = 0;
        // 将txRLPS中的交易导入交易池, 且记录下开始时间
        for(auto it = start; it != end; it++){
            if(ii != 0 && ii % SPEED == 0){
                std::this_thread::sleep_for(std::chrono::seconds(1)); // sleep 1s
                ii++;
            }

            string txrlp = *it;
            m_rpcService->sendRawTransaction(dev::consensus::internal_groupId, txrlp);

            // 获取交易id, 记录交易开始时间
            string txid;
            Transaction::Ptr tx = std::make_shared<Transaction>(
                jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
            string data_str = dataToHexString(tx->get_data());

            if(data_str.find("0x444555666", 0) != -1){ // 片内交易
                vector<string> dataItems;
                boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                txid = dataItems.at(2).c_str();
            }
            else if(data_str.find("0x111222333", 0) != -1){ // 跨片交易
                vector<string> dataItems;
                boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
                txid = dataItems.at(1).c_str();
            }

            struct timeval tv;
            gettimeofday(&tv, NULL);
            long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;
            m_txid_to_starttime->insert(make_pair(txid, time_msec)); // 记录txid的开始时间
        }
    }
}

/**
 * NOTES: 生成分片片内交易负载
 * */
 // todo
void injectTxs::generateIntraShardWorkLoad(int32_t shardid, string filename, int txNumber, int shard_number)
{
    // // 生成配置文件
    // // 每个节点都生成分片间状态划分的文件keys, 每个分片一共1000个状态，所有节点生成的文件都是一样的
    // // 其中，划分给分片 shardis 的初始状态范围为 "state_" + (1000 * (shardid - 1) + i), 0 ≤ i ＜ 1000

    // 生成片内交易
    // 只需要一个节点负责生成就可以，默认转发节点生成
    if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
        PLUGIN_LOG(INFO) << LOG_KV("txNumber", txNumber);

        vector<string> txrlps;
        int fileOrder = 1;
        srand(time(0));
        for(int j = 1; j <= txNumber; j++){
            txid++;
            // 生成两个不同的随机数
            // int state_id1 = rand() % 1000 + 1000 * (shardid - 1);
            // int state_id2 = rand() % 1000 + 1000 * (shardid - 1);
            int state_each_shard = state_num / hieraShardTree->get_hiera_shard_number();
            int state_id1 = rand() % state_each_shard + state_each_shard * (shardid - 1);
            int state_id2 = rand() % state_each_shard +state_each_shard * (shardid - 1);
            while(state_id2 == state_id1){
                state_id2 = rand() % state_each_shard +state_each_shard * (shardid - 1);
            }
            string statekey1 = "state" + to_string(state_id1); // 被访问的状态
            string statekey2 = "state" + to_string(state_id2); // 被访问的状态
            string statekeys = statekey1 + "_" + statekey2;
            if (j % 10000 == 0) {
                cout << "intraShardTxInfo:" << endl;
                cout << "shard_id: " << shardid << endl;
                cout << "state_id1: " << statekey1 << endl;
                cout << "state_id2: " << statekey2 << endl;
            }
            string requestLabel = "0x444555666";
            string txrlp = generateTx(requestLabel, statekeys, shardid, txid, true, false);
            txrlps.push_back(txrlp);

            if (j % 100000 == 0) {
                PLUGIN_LOG(INFO) << LOG_KV("j", j)
                                 << LOG_KV("txrlps.size", txrlps.size());
                string newfilename = filename + "_" + to_string(fileOrder) + ".json";
                fileOrder++;
                // 将 txrlps 中的交易写入文件
                string res = "";
                int addedTxSize = 0;
                int tx_size = txrlps.size();
                for(int i = 0; i < tx_size; i++){
                    string txrlp = txrlps.at(i);
                    if(addedTxSize == 0){
                        res = "[\"" + txrlp + "\"";
                    }
                    else if(addedTxSize == tx_size - 1){
                        res += "]";
                    }
                    else{
                        res += ",\"" + txrlp + "\"";
                    }
                    addedTxSize++;
                }

                ofstream out;
                out.open(newfilename, ios::in|ios::out|ios::app);
                if (out.is_open()) {
                    out << res;
                    out.close();
                }
                txrlps.clear();
            }
        }

        if (txrlps.size() > 0) {
            // 将 txrlps 中的交易写入文件
            string newfilename = filename + "_" + to_string(fileOrder) + ".json";
            string res = "";
            int addedTxSize = 0;
            int tx_size = txrlps.size();
            for(int i = 0; i < tx_size; i++){
                string txrlp = txrlps.at(i);
                if(addedTxSize == 0){
                    res = "[\"" + txrlp + "\"";
                }
                else if(addedTxSize == tx_size - 1){
                    res += "]";
                }
                else{
                    res += ",\"" + txrlp + "\"";
                }
                addedTxSize++;
            }

            ofstream out;
            out.open(filename, ios::in|ios::out|ios::app);
            if (out.is_open()) {
                out << res;
                out.close();
            }
        }
    }
}
// todo
/**
 * NOTES: 生成分片跨片交易负载，shardid为协调者的shardid，filename为生成的负载文件名称(默认跨两个分片)
 * */
void injectTxs::generateInterShardWorkLoad(int32_t coordinator_shardid, string filename, int txNumber, shared_ptr<dev::ledger::LedgerManager> ledgerManager)
{
    // 只需要一个节点负责生成就可以，默认转发节点生成
    if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){ // 由转发节点投递到交易池

        vector<string> txrlps;
        // vector<string> lower_groupId_items;
        // boost::split(lower_groupId_items, lower_groupIds, boost::is_any_of(","), boost::token_compress_on);
        // int lower_shards_size = lower_groupId_items.size();
        // int txid = 0;

        srand(time(0));
        for(int i = 0; i < txNumber; i++){
            // int shard1_index = rand() % lower_shards_size;
            // int shard2_index = rand() % lower_shards_size;
            // while(shard2_index == shard1_index) {
            //     shard2_index = rand() % lower_shards_size;
            // }

            // int shardid1 = atoi(lower_groupId_items.at(shard1_index).c_str()); // 跨片交易涉及的分片
            // int shardid2 = atoi(lower_groupId_items.at(shard2_index).c_str());
            pair<int, int> shard_ids = dev::plugin::hieraShardTree->get_inter_childs(dev::consensus::internal_groupId);
            int shardid1 = shard_ids.first;
            int shardid2 = shard_ids.second;
            // int state_index1 = rand() % 1000 + (shardid1 - 1) * 1000;
            // int state_index2 = rand() % 1000 + (shardid2 - 1) * 1000;
            int state_each_shard = state_num / hieraShardTree->get_hiera_shard_number();
            int state_index1 = rand() % state_each_shard + state_each_shard * (shardid1 - 1);
            int state_index2 = rand() % state_each_shard +state_each_shard * (shardid2 - 1);
            string statekey1 = "state" + to_string(state_index1); // 跨片交易涉及的状态
            string statekey2 = "state" + to_string(state_index2);
            if (i % 10000 == 0) {
                cout << "interShardTxInfo:" << endl;
                cout << "shard_id1: " << shardid1 << endl;
                cout << "shard_id2: " << shardid2 << endl;
                cout << "state_id1: " << statekey1 << endl;
                cout << "state_id2: " << statekey2 << endl;
            }
            // 生成 subtx1
            string requestLabel = "";
            string statekeys = "";
            string signTx1 = generateTx(requestLabel, statekeys, shardid1, txid, false, false);

            // 生成 subtx2
            requestLabel = "";
            statekeys = "";
            string signTx2 = generateTx(requestLabel, statekeys, shardid2, txid, false, false);

            // 生成最终的Tx
            requestLabel = "0x111222333"; // 生成最终的跨片交易
            string flag = "|";
            string rwsets = statekey1 + "_" + statekey2;
            string hex_m_data_str = requestLabel + flag + "a" + to_string(txid)
                                                 + flag + to_string(shardid1) + flag + signTx1 + flag + rwsets 
                                                 + flag + to_string(shardid2) + flag + signTx2 + flag + rwsets
                                                 + flag;

            string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
            dev::Address crossAddress(contract_address);
            dev::eth::ContractABI abi;
            bytes data = abi.abiIn("set(string)", hex_m_data_str);

            Transaction tx(0, 1000, 0, crossAddress, data);
            tx.setNonce(generateRandomValue());
            tx.setGroupId(coordinator_shardid);
            tx.setBlockLimit(u256(ledgerManager->blockChain(coordinator_shardid)->number()) + 500);
            
            auto keyPair = KeyPair::create();
            auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
            tx.updateSignature(sig);
            string txrlp = toHex(tx.rlp());
            txrlps.push_back(txrlp);
            
            txid++;
        }

        // 将 txrlps 中的交易写入文件
        string res = "";
        int addedTxSize = 0;
        int tx_size = txrlps.size();
        for(int i = 0; i < tx_size; i++){
            string txrlp = txrlps.at(i);
            if(addedTxSize == 0){
                res = "[\"" + txrlp + "\"";
            }
            else if(addedTxSize == tx_size - 1){
                res += "]";
            }
            else{
                res += ",\"" + txrlp + "\"";
            }
            addedTxSize++;
        }
        ofstream out;
        out.open(filename, ios::in|ios::out|ios::app);
        if (out.is_open()) {
            out << res;
            out.close();
        }
    }
}
// todo
// 获取跨层跨片交易的两个分片id
// 跨层跨片交易，两个分片在不同的子树
// case1:其中一个分片就是协调者本身
// 此时，只要从协调者的子节点的任意一棵子树中选择一个分片作为第二个分片即可
// case2:两个分片都是协调者的后继节点
// 要保证两个分片在不同的子树上，不然协调者就是它们所在子树的根节点
// 任意选择两个子树，然后从它们的子树中任意选出一个节点
// 默认第一个子树可以包含根节点，第二个不可以（两个都是根节点是局部）
pair<int, int> injectTxs::getCrossLayerShardIds(int internal_groupId) {
    return dev::plugin::hieraShardTree->get_cross_layer_childs(internal_groupId);
}
// todo
// 原本生成cross_layer的函数写死，修改为统一生成
void injectTxs::generateCrossLayerWorkLoad(int internal_groupId, string filename, int txNumber, shared_ptr<dev::ledger::LedgerManager> ledgerManager) {
    if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){ // 由转发节点投递到交易池

        vector<string> txrlps;
  //      vector<string> lower_groupId_items;
  //      boost::split(lower_groupId_items, lower_groupIds, boost::is_any_of(","), boost::token_compress_on);
  //      int lower_shards_size = lower_groupId_items.size();
        // int txid = 0;
        srand(time(0));
        for(int i = 0; i < txNumber; i++){
            pair<int, int> shard_ids = getCrossLayerShardIds(internal_groupId);
            int shardid1 = shard_ids.first;
            int shardid2 = shard_ids.second;
            // int state_index1 = rand() % 1000 + (shardid1 - 1) * 1000;
            // int state_index2 = rand() % 1000 + (shardid2 - 1) * 1000;
            int state_each_shard = state_num / hieraShardTree->get_hiera_shard_number();
            int state_index1 = rand() % state_each_shard + state_each_shard * (shardid1 - 1);
            int state_index2 = rand() % state_each_shard +state_each_shard * (shardid2 - 1);
            string statekey1 = "state" + to_string(state_index1); // 跨片交易涉及的状态
            string statekey2 = "state" + to_string(state_index2);
            if (i % 10000 == 0) {
                cout << "cross_layerShardTxInfo:" << endl;
                cout << "shard_id1: " << shardid1 << endl;
                cout << "shard_id2: " << shardid2 << endl;
                cout << "state_id1: " << statekey1 << endl;
                cout << "state_id2: " << statekey2 << endl;
            }
            // 生成 subtx1
            string requestLabel = "";
            string statekeys = "";
            string signTx1 = generateTx(requestLabel, statekeys, shardid1, txid, false, true);
            // 生成 subtx2
            requestLabel = "";
            statekeys = "";
            string signTx2 = generateTx(requestLabel, statekeys, shardid2, txid, false, true);
            // 生成最终的Tx
            requestLabel = "0x111222333"; // 生成最终的跨片交易
            string flag = "|";
            string rwsets = statekey1 + "_" + statekey2;
            string hex_m_data_str = requestLabel + flag + "a" + to_string(txid)
                                    + flag + to_string(shardid1) + flag + signTx1 + flag + rwsets
                                    + flag + to_string(shardid2) + flag + signTx2 + flag + rwsets
                                    + flag;
            string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
            dev::Address crossAddress(contract_address);
            dev::eth::ContractABI abi;
            bytes data = abi.abiIn("set(string)", hex_m_data_str);
            Transaction tx(0, 1000, 0, crossAddress, data);
            tx.setNonce(generateRandomValue());
            auto keyPair = KeyPair::create();
            auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
            tx.updateSignature(sig);
            string txrlp = toHex(tx.rlp());
            txrlps.push_back(txrlp);
            txid++;
        }

        // 将 txrlps 中的交易写入文件
        string res = "";
        int addedTxSize = 0;
        int tx_size = txrlps.size();
        for(int i = 0; i < tx_size; i++){
            string txrlp = txrlps.at(i);
            if(addedTxSize == 0){
                res = "[\"" + txrlp + "\"";
            }
            else if(addedTxSize == tx_size - 1){
                res += "]";
            }
            else{
                res += ",\"" + txrlp + "\"";
            }
            addedTxSize++;
        }

        ofstream out;
        out.open(filename, ios::in|ios::out|ios::app);
        if (out.is_open()) {
            out << res;
            out.close();
        }
    }
}

/**
 * NOTES: 生成具有局部性特征的片内交易负载
 * */
void injectTxs::generateLocalityIntraShardWorkLoad(int32_t shardid, 
                                                   string filename, 
                                                   int txNumber, 
                                                   int shard_number,
                                                   double localityRate)
{
    if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) {
        srand(time(0));
        vector<string> txrlps;
        // 选取一个状态为热点状态(本地局部性很高) 0, 1000, 2000, ...
        int hotStateId = 1000 * (shardid - 1);

        // 生成一部分随机负载
        int num = txNumber * (1 - localityRate);
        for(int j = 0; j < num; j++){
            int txid = j; 
            // 生成两个不同的随机数
            int state_id1 = rand() % 1000 + 1000 * (shardid - 1);
            while(state_id1 == hotStateId) {
                state_id1 = rand() % 1000 + 1000 * (shardid - 1);
            }
            int state_id2 = rand() % 1000 + 1000 * (shardid - 1);
            while(state_id2 == state_id1 || state_id2 == hotStateId){
                state_id2 = rand() % 1000 + 1000 * (shardid - 1);
            }
            string statekey1 = "state" + to_string(state_id1); // 被访问的状态
            string statekey2 = "state" + to_string(state_id2); // 被访问的状态
            string statekeys = statekey1 + "_" + statekey2;

            string requestLabel = "0x444555666";
            string txrlp = generateTx(requestLabel, statekeys, shardid, txid, true, false);
            txrlps.push_back(txrlp);
        }

        num = txNumber * localityRate;
        int base = txNumber * (1 - localityRate);
        for(int i = 0; i < num; i++) {
            int txid = i + base;
            // 生成两个不同的随机数
            int state_id2 = rand() % 1000 + 1000 * (shardid - 1);
            while(state_id2 == hotStateId) {
                state_id2 = rand() % 1000 + 1000 * (shardid - 1);
            }
            string statekey1 = "state" + to_string(hotStateId); // 被访问的状态
            string statekey2 = "state" + to_string(state_id2); // 被访问的状态
            string statekeys = statekey1 + "_" + statekey2;

            string requestLabel = "0x444555666";
            string txrlp = generateTx(requestLabel, statekeys, shardid, txid, true, false);
            txrlps.push_back(txrlp);
        }
        // 随机打乱交易顺序
        auto start = txrlps.begin();
        auto end = txrlps.end();
        srand(time(NULL));
        random_shuffle(start, end);

        // 将 txrlps 中的交易写入文件
        string res = "";
        int addedTxSize = 0;
        int tx_size = txrlps.size();
        for(int i = 0; i < tx_size; i++) {
            string txrlp = txrlps.at(i);
            if(addedTxSize == 0){
                res = "[\"" + txrlp + "\"";
            }
            else if(addedTxSize == tx_size - 1){
                res += "]";
            }
            else{
                res += ",\"" + txrlp + "\"";
            }
            addedTxSize++;
        }

        ofstream out;
        out.open(filename, ios::in|ios::out|ios::app);
        if (out.is_open()) {
            out << res;
            out.close();
        }
    }
}



/**
 * NOTES: 生成具有局部性特征的跨片交易负载
 * */
void injectTxs::generateLocalityInterShardWorkLoad(int32_t coordinator_shardid, 
                                           string& lower_groupIds, 
                                           string filename, 
                                           int txNumber, 
                                           shared_ptr<dev::ledger::LedgerManager> ledgerManager, 
                                           double localityRate)
{
    // 只需要一个节点负责生成就可以，默认转发节点生成
    if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) { // 由转发节点投递到交易池
        vector<string> txrlps;
        vector<string> lower_groupId_items;
        boost::split(lower_groupId_items, lower_groupIds, boost::is_any_of(","), boost::token_compress_on);
        int lower_shards_size = lower_groupId_items.size();
        int txid = 0;

        srand(time(0));
        
        // int shard1_index = rand() % lower_shards_size;
        // int shard2_index = rand() % lower_shards_size;
        // while(shard2_index == shard1_index) {
        //     shard2_index = rand() % lower_shards_size;
        // }

        int shard1_index = 0;
        int shard2_index = 1;

        int shardid1 = atoi(lower_groupId_items.at(shard1_index).c_str()); // 跨片交易涉及的分片
        int shardid2 = atoi(lower_groupId_items.at(shard2_index).c_str());
        
        int hotStateId1 = 1000 * (shardid1 - 1);
        int hotStateId2 = 1000 * (shardid2 - 1);

        // 生成一部分随机负载
        int num = txNumber * (1 - localityRate);
        for (int j = 0; j < num; j++) {
            // 范围：[1000*i+1, 1000*i+999]
            int state_index1 = rand() % 999 + (shardid1 - 1) * 1000 + 1;
            int state_index2 = rand() % 999 + (shardid2 - 1) * 1000 + 1;
            
            string statekey1 = "state" + to_string(state_index1); // 跨片交易涉及的状态
            string statekey2 = "state" + to_string(state_index2);

            // 生成 subtx1
            string requestLabel = "";
            string statekeys = "";
            string signTx1 = generateTx(requestLabel, statekeys, shardid1, txid, false, false);

            // 生成 subtx2
            requestLabel = "";
            statekeys = "";
            string signTx2 = generateTx(requestLabel, statekeys, shardid2, txid, false, false);

            // 生成最终的Tx
            requestLabel = "0x111222333"; // 生成最终的跨片交易
            string flag = "|";
            string rwsets = statekey1 + "_" + statekey2;
            string hex_m_data_str = requestLabel + flag + "a" + to_string(txid)
                                                    + flag + to_string(shardid1) + flag + signTx1 + flag + rwsets 
                                                    + flag + to_string(shardid2) + flag + signTx2 + flag + rwsets
                                                    + flag;

            string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
            dev::Address crossAddress(contract_address);
            dev::eth::ContractABI abi;
            bytes data = abi.abiIn("set(string)", hex_m_data_str);

            Transaction tx(0, 1000, 0, crossAddress, data);
            tx.setNonce(generateRandomValue());
            tx.setGroupId(coordinator_shardid);
            tx.setBlockLimit(u256(ledgerManager->blockChain(coordinator_shardid)->number()) + 500);
            
            auto keyPair = KeyPair::create();
            auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
            tx.updateSignature(sig);
            string txrlp = toHex(tx.rlp());
            txrlps.push_back(txrlp);
            
            txid++;
        }

        // 生成局部性负载
        num = txNumber * localityRate;
        for (int j = 0; j < num; j++) {
            string statekey1 = "state" + to_string(hotStateId1); // 跨片交易涉及的状态
            string statekey2 = "state" + to_string(hotStateId2);

            // 生成 subtx1
            string requestLabel = "";
            string statekeys = "";
            string signTx1 = generateTx(requestLabel, statekeys, shardid1, txid, false, false);

            // 生成 subtx2
            requestLabel = "";
            statekeys = "";
            string signTx2 = generateTx(requestLabel, statekeys, shardid2, txid, false, false);

            // 生成最终的Tx
            requestLabel = "0x111222333"; // 生成最终的跨片交易
            string flag = "|";
            string rwsets = statekey1 + "_" + statekey2;
            string hex_m_data_str = requestLabel + flag + "a" + to_string(txid)
                                                    + flag + to_string(shardid1) + flag + signTx1 + flag + rwsets 
                                                    + flag + to_string(shardid2) + flag + signTx2 + flag + rwsets
                                                    + flag;

            string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
            dev::Address crossAddress(contract_address);
            dev::eth::ContractABI abi;
            bytes data = abi.abiIn("set(string)", hex_m_data_str);

            Transaction tx(0, 1000, 0, crossAddress, data);
            tx.setNonce(generateRandomValue());
            tx.setGroupId(coordinator_shardid);
            tx.setBlockLimit(u256(ledgerManager->blockChain(coordinator_shardid)->number()) + 500);
            
            auto keyPair = KeyPair::create();
            auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
            tx.updateSignature(sig);
            string txrlp = toHex(tx.rlp());
            txrlps.push_back(txrlp);
            txid++;
        }
        

        // 随机打乱交易顺序
        auto start = txrlps.begin();
        auto end = txrlps.end();
        srand(time(NULL));
        random_shuffle(start, end);

        // 将 txrlps 中的交易写入文件
        string res = "";
        int addedTxSize = 0;
        int tx_size = txrlps.size();
        for(int i = 0; i < tx_size; i++){
            string txrlp = txrlps.at(i);
            if(addedTxSize == 0){
                res = "[\"" + txrlp + "\"";
            }
            else if(addedTxSize == tx_size - 1){
                res += "]";
            }
            else{
                res += ",\"" + txrlp + "\"";
            }
            addedTxSize++;
        }

        ofstream out;
        out.open(filename, ios::in|ios::out|ios::app);
        if (out.is_open()) {
            out << res;
            out.close();
        }
    }
}


// /**
//  * NOTES: 生成分片片内交易负载
//  * */
// void injectTxs::generateIntraShardWorkLoad(int32_t shardid, string filename)
// {

//     // 只需要一个节点负责生成就可以，默认转发节点生成
//     if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) // 由转发节点投递到交易池
//     {
//         vector<string> txrlps; // 生成的所有交易
//         // 生成分片管理的1000个状态, shard1:state0-999, shard2:state1000-1999...
//         vector<string> statekeys;
//         for(int i = 0; i < 1000; i++){
//             statekeys.push_back("state"+to_string(i));
//         }

//         // 生成片内交易(分别访问1个, 2个和3个状态的交易)
//         // 访问一个状态的交易
//         int number = 1000000;// 总共100万笔片内交易，其中80万笔访问一个状态，80万笔访问两个状态，10万笔访问三个状态
//         // vector<int> txNumber;
//         // txNumber.push_back(100000);
//         // txNumber.push_back(800000);
//         // txNumber.push_back(100000);

//         srand(time(0));
//         for(int j = 0; j < number; j++){
            
//             int txid = j;
//             // 生成两个不同的随机数
//             int id1 = rand() % 1000 + 1000 * (shardid - 1);
//             int id2 = rand() % 1000 + 1000 * (shardid - 1);
//             while(id1 == id2){
//                 id2 = rand() % 1000 + 1000 * (shardid - 1);
//             }

//             string statekey1 = "state" + to_string(id1); // 被访问的状态
//             string statekey2 = "state" + to_string(id2); // 被访问的状态
//             string statekeys = statekey1 + "_" + statekey2;
//             string requestLabel = "0x444555666";
//             string txrlp = generateTx(requestLabel, statekeys, shardid, txid);
//             txrlps.push_back(txrlp);
//         }

//         // for(int i = 0; i < 2; i++){ // 不再需要，所有的交易访问两个状态
//         //     if(i == 0) // 访问一个状态的交易
//         //     {
//         //         for(int j = 0; j < txNumber.at(i); j++)
//         //         {
//         //             int id = rand() % 1000 + 1000 * (shardid - 1);// id属于 [0,1000)
//         //             string statekey = "state" + to_string(id); // 被访问的状态
//         //             string requestLabel = "0x444555666";
//         //             string txrlp = generateTx(requestLabel, statekey, shardid, );
//         //             txrlps.push_back(txrlp);
//         //         }
//         //     }
//         //     else if(i == 1) // 访问两个状态的交易
//         //     {
//         //         for(int j = 0; j < txNumber.at(i); j++)
//         //         {
//         //             // 生成两个不同的随机数
//         //             int id1 = rand() % 1000 + 1000 * (shardid - 1);
//         //             int id2 = rand() % 1000 + 1000 * (shardid - 1);
//         //             while(id1 == id2){
//         //                 id2 = rand() % 1000 + 1000 * (shardid - 1);
//         //             }

//         //             string statekey1 = "state" + to_string(id1); // 被访问的状态
//         //             string statekey2 = "state" + to_string(id2); // 被访问的状态
//         //             string statekeys = statekey1 + "_" + statekey2;
//         //             string requestLabel = "0x444555666";
//         //             string txrlp = generateTx(requestLabel, statekeys, shardid, );
//         //             txrlps.push_back(txrlp);
//         //         }
//         //     }
//             // else if(i == 2) // 访问三个状态的交易 // 不用考虑
//             // {
//             //     for(int j = 0; j < txNumber.at(i); j++)
//             //     {
//             //         // 生成两个不同的随机数
//             //         int id1 = rand() % 1000 + 1000 * (shardid - 1);
//             //         int id2 = rand() % 1000 + 1000 * (shardid - 1);
//             //         int id3 = rand() % 1000 + 1000 * (shardid - 1);
//             //         while(id2 == id1){
//             //             id2 = rand() % 1000 + 1000 * (shardid - 1);
//             //         }
//             //         while(id3 == id1 || id3 == id2){
//             //             id3 = rand() % 1000 + 1000 * (shardid - 1);
//             //         }

//             //         string statekey1 = "state" + to_string(id1); // 被访问的状态
//             //         string statekey2 = "state" + to_string(id2); // 被访问的状态
//             //         string statekey3 = "state" + to_string(id3); // 被访问的状态
//             //         string statekeys = statekey1 + "_" + statekey2 + "_" + statekey3;
//             //         string requestLabel = "0x444555666";
//             //         string txrlp = generateTx(requestLabel, statekeys, shardid);
//             //         txrlps.push_back(txrlp);
//             //     }
//             // }
//         // }

//         // 将 txrlps 中的交易写入文件
//         string res = "";
//         int addedTxSize = 0;
//         int tx_size = txrlps.size();
//         for(int i = 0; i < tx_size; i++)
//         {
//             string txrlp = txrlps.at(i);
//             if(addedTxSize == 0)
//             {
//                 res = "[\"" + txrlp + "\"";
//             }
//             else if(addedTxSize == tx_size - 1){
//                 res += "]";
//             }
//             else{
//                 res += ",\"" + txrlp + "\"";
//             }
//             addedTxSize++;
//         }

//         ofstream out;
//         out.open(filename, ios::in|ios::out|ios::app);
//         if (out.is_open()) {
//             out << res;
//             out.close();
//         }
//     }
// }




// /**
//  * NOTES: 生成分片跨片交易负载，shardid为协调者的shardid，filename为生成的负载文件名称
//  * */
// void injectTxs::generateInterShardWorkLoad(int32_t coordinator_shardid, string& lower_groupIds, string filename, shared_ptr<dev::ledger::LedgerManager> ledgerManager)
// {
//     // 只需要一个节点负责生成就可以，默认转发节点生成
//     if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) // 由转发节点投递到交易池
//     {
//         vector<int> txNumber;
//         txNumber.push_back(100000); // 跨两个分片的交易数目
//         // txNumber.push_back(50000); // 跨三个分片的交易数目

//         vector<string> txrlps;
//         vector<string> lower_groupId_items;
//         boost::split(lower_groupId_items, lower_groupIds, boost::is_any_of(","), boost::token_compress_on);
//         int lower_shards_size = lower_groupId_items.size();

//         srand(time(0));
//         int txid = 0;
//         for(int i = 0; i < 1; i++)
//         {
//             for(int j = 0; j < txNumber.at(i); j++)
//             {
//                 if(i == 0) // 跨两个分片的交易
//                 {
//                     int shard1_index = rand() % lower_shards_size;// id属于 [1,4)
//                     int shard2_index = rand() % lower_shards_size;// id属于 [1,4)
//                     while(shard2_index == shard1_index) {
//                         shard2_index = rand() % lower_shards_size;
//                     }

//                     int shardid1 = atoi(lower_groupId_items.at(shard1_index).c_str()); // 跨片交易涉及的分片
//                     int shardid2 = atoi(lower_groupId_items.at(shard2_index).c_str());

//                     int state_index1 = rand() % 1000 + (shardid1 - 1) * 1000;
//                     int state_index2 = rand() % 1000 + (shardid2 - 1) * 1000;
//                     string statekey1 = "state" + to_string(state_index1); // 跨片交易涉及的状态
//                     string statekey2 = "state" + to_string(state_index2);

//                     // 生成 subtx1
//                     auto keyPair = KeyPair::create();
//                     string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
//                     dev::Address subAddress1(contract_address);
//                     dev::eth::ContractABI abi;

//                     string flag = "|";
//                     string subtx_data = flag + toHex("a" + to_string(txid)) + flag;
//                     bytes data = abi.abiIn("add(string)", subtx_data);  // add

//                     Transaction subTx1(0, 1000, 0, subAddress1, data); // 生成跨片子交易1
//                     subTx1.setNonce(generateRandomValue());
//                     subTx1.setGroupId(shardid1);
//                     subTx1.setBlockLimit(u256(ledgerManager->blockChain(dev::consensus::internal_groupId)->number()) + 500);
//                     auto subSig1 = dev::crypto::Sign(keyPair, subTx1.hash(WithoutSignature));
//                     subTx1.updateSignature(subSig1);
//                     auto subrlp1 = subTx1.rlp();
//                     string signTx1 = toHex(subrlp1);

//                     // 生成subtx2
//                     keyPair = KeyPair::create();
//                     contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
//                     dev::Address subAddress2(contract_address);
                    
//                     flag = "|";
//                     subtx_data = flag + toHex("a" + to_string(txid)) + flag; // 跨片交易特殊标记
//                     data = abi.abiIn("add(string)", subtx_data);  // add

//                     Transaction subTx2(0, 1000, 0, subAddress2, data); // 生成跨片子交易2
//                     subTx2.setNonce(generateRandomValue());
//                     subTx2.setGroupId(shardid2);
//                     subTx2.setBlockLimit(u256(ledgerManager->blockChain(dev::consensus::internal_groupId)->number()) + 500);
//                     auto subSig2 = dev::crypto::Sign(keyPair, subTx2.hash(WithoutSignature));
//                     subTx2.updateSignature(subSig2);
//                     auto subrlp2 = subTx2.rlp();
//                     string signTx2 = toHex(subrlp2);

//                     string requestLabel = "0x111222333"; // 生成最终的跨片交易
//                     flag = "|";
//                     string rwsets = statekey1 + "_" + statekey2;
//                     string hex_m_data_str = requestLabel + flag + "a" + to_string(txid)
//                                                          + flag + to_string(shardid1) + flag + signTx1 + flag + rwsets 
//                                                          + flag + to_string(shardid2) + flag + signTx2 + flag + rwsets
//                                                          + flag;

//                     txid++;
//                     dev::Address crossAddress(contract_address);
//                     data = abi.abiIn("set(string)", hex_m_data_str);  // set
//                     Transaction tx(0, 1000, 0, crossAddress, data);
//                     tx.setNonce(generateRandomValue());
//                     tx.setGroupId(coordinator_shardid);
//                     tx.setBlockLimit(u256(ledgerManager->blockChain(coordinator_shardid)->number()) + 500);
//                     auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
//                     tx.updateSignature(sig);
//                     string txrlp = toHex(tx.rlp());
//                     txrlps.push_back(txrlp);
//                 }
//                 // else if(i == 1) // 跨三个分片的交易 // 暂不启用
//                 // {
//                 //     int shard1_index = rand() % lower_shards_size;
//                 //     int shard2_index = rand() % lower_shards_size;
//                 //     int shard3_index = rand() % lower_shards_size;

//                 //     while(shard2_index == shard1_index) {
//                 //         shard2_index = rand() % lower_shards_size;
//                 //     }
//                 //     while(shard3_index == shard2_index || shard3_index == shard1_index) {
//                 //         shard3_index = rand() % lower_shards_size;
//                 //     }

//                 //     int shardid1 = atoi(lower_groupId_items.at(shard1_index).c_str()); // 跨片交易涉及的分片
//                 //     int shardid2 = atoi(lower_groupId_items.at(shard2_index).c_str());
//                 //     int shardid3 = atoi(lower_groupId_items.at(shard3_index).c_str());

//                 //     int state_index1 = rand() % 1000 + (shardid1 - 1) * 1000;  // 跨片交易涉及的状态
//                 //     int state_index2 = rand() % 1000 + (shardid2 - 1) * 1000;
//                 //     int state_index3 = rand() % 1000 + (shardid3 - 1) * 1000;
//                 //     string statekey1 = "state" + to_string(state_index1);
//                 //     string statekey2 = "state" + to_string(state_index2);
//                 //     string statekey3 = "state" + to_string(state_index3);

//                 //     // 生成 subtx1
//                 //     auto keyPair = KeyPair::create();
//                 //     string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
//                 //     dev::Address subAddress1(contract_address);
//                 //     dev::eth::ContractABI abi;
//                 //     bytes data = abi.abiIn("add(string)");  // add

//                 //     Transaction subTx1(0, 1000, 0, subAddress1, data); // 生成跨片子交易1
//                 //     subTx1.setNonce(generateRandomValue());
//                 //     subTx1.setGroupId(shard1_index);
//                 //     subTx1.setBlockLimit(u256(ledgerManager->blockChain(dev::consensus::internal_groupId)->number()) + 500);
//                 //     auto subSig1 = dev::crypto::Sign(keyPair, subTx1.hash(WithoutSignature));
//                 //     subTx1.updateSignature(subSig1);
//                 //     auto subrlp1 = subTx1.rlp();
//                 //     string signTx1 = toHex(subrlp1);

//                 //     // 生成subtx2
//                 //     keyPair = KeyPair::create();
//                 //     contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
//                 //     dev::Address subAddress2(contract_address);
//                 //     data = abi.abiIn("add(string)");  // add

//                 //     Transaction subTx2(0, 1000, 0, subAddress2, data); // 生成跨片子交易2
//                 //     subTx2.setNonce(generateRandomValue());
//                 //     subTx2.setGroupId(shard2_index);
//                 //     subTx2.setBlockLimit(u256(ledgerManager->blockChain(dev::consensus::internal_groupId)->number()) + 500);
//                 //     auto subSig2 = dev::crypto::Sign(keyPair, subTx2.hash(WithoutSignature));
//                 //     subTx2.updateSignature(subSig2);
//                 //     auto subrlp2 = subTx2.rlp();
//                 //     string signTx2 = toHex(subrlp2);

//                 //     keyPair = KeyPair::create();
//                 //     contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
//                 //     dev::Address subAddress3(contract_address);
//                 //     data = abi.abiIn("add(string)");  // add

//                 //     Transaction subTx3(0, 1000, 0, subAddress3, data); // 生成最终的跨片交易
//                 //     subTx3.setNonce(generateRandomValue());
//                 //     subTx3.setGroupId(shard3_index);
//                 //     subTx3.setBlockLimit(u256(ledgerManager->blockChain(dev::consensus::internal_groupId)->number()) + 500);
//                 //     auto subSig3 = dev::crypto::Sign(keyPair, subTx3.hash(WithoutSignature));
//                 //     subTx3.updateSignature(subSig3);
//                 //     auto subrlp3 = subTx3.rlp();
//                 //     string signTx3 = toHex(subrlp3);

//                 //     // 生成最终的Tx
//                 //     string requestLabel = "0x111222333";
//                 //     string flag = "|";
//                 //     string rwsets = statekey1 + "_" + statekey2 + "_" + statekey3;
//                 //     string hex_m_data_str = requestLabel + flag + to_string(txid)
//                 //                                         + flag + to_string(shard1_index) + flag + signTx1 + flag + rwsets 
//                 //                                         + flag + to_string(shard2_index) + flag + signTx2 + flag + rwsets
//                 //                                         + flag + to_string(shard3_index) + flag + signTx3 + flag + rwsets
//                 //                                         + flag;

//                 //     txid++;
//                 //     dev::Address crossAddress(contract_address);
//                 //     data = abi.abiIn("set(string)", hex_m_data_str);
//                 //     Transaction tx(0, 1000, 0, crossAddress, data);
//                 //     tx.setNonce(generateRandomValue());
//                 //     tx.setGroupId(coordinator_shardid);
//                 //     tx.setBlockLimit(u256(ledgerManager->blockChain(coordinator_shardid)->number()) + 500);
//                 //     auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
//                 //     tx.updateSignature(sig);
//                 //     string txrlp = toHex(tx.rlp());
//                 //     txrlps.push_back(txrlp);
//                 // }
//             }

//             // 将 txrlps 中的交易写入文件
//             string res = "";
//             int addedTxSize = 0;
//             int tx_size = txrlps.size();
//             for(int i = 0; i < tx_size; i++)
//             {
//                 string txrlp = txrlps.at(i);
//                 if(addedTxSize == 0)
//                 {
//                     res = "[\"" + txrlp + "\"";
//                 }
//                 else if(addedTxSize == tx_size - 1){
//                     res += "]";
//                 }
//                 else{
//                     res += ",\"" + txrlp + "\"";
//                 }
//                 addedTxSize++;
//             }

//             ofstream out;
//             out.open(filename, ios::in|ios::out|ios::app);
//             if (out.is_open()) {
//                 out << res;
//                 out.close();
//             }

//             // 输出 filename
//             PLUGIN_LOG(INFO) << LOG_KV("filename", filename);
//         }
//     }
// }







// // 注入调用合约的交易
// void injectTxs::injectionTransactions(string& workload_filename, int groupId, int txnum)
// {
//     ifstream infile(workload_filename, ios::binary); // signedtxs.json
//     Json::Reader reader;
//     Json::Value root;

//     vector<string> txids;

//     // 加载交易
//     int inputTxsize = txnum; // 发送的合约总数
//     if(reader.parse(infile, root)) {
//         int number = root.size();
//         PLUGIN_LOG(INFO) << LOG_KV("导入的交易数目number", inputTxsize);

//         for(int i = 0; i < number; i++) {
//             string txrlp = root[i].asString();
//             signedTransactions.push_back(txrlp);

//             // 记录交易hash
//             Transaction::Ptr tx = std::make_shared<Transaction>(
//                 jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);

//             // 解析交易data字段，获取交易txid
//             string data_str = dataToHexString(tx->get_data());
//             vector<string> dataItems;
//             boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
//             string txid = dataItems.at(1).c_str();
//             txids.push_back(txid);

//             if(i == inputTxsize) { // 倒入的交易总数目
//                 break;
//             }
//         }
//     }

//     if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) // 由转发节点投递到交易池
//     {
//         // // 往交易池中导入交易
//         // auto txPool = m_ledgerManager->txPool(dev::consensus::internal_groupId);
//         // for(int i = 0; i < inputTxsize; i++) {
//         //     txPool->submitTransactions(txs.at(i)); // 交易直接往交易池中发送
//         // }
//         for(int i = 0; i < inputTxsize; i++) {
//             m_rpcService->sendRawTransaction(dev::consensus::internal_groupId, signedTransactions.at(i));

//             string txid = txids.at(i);
//             struct timeval tv;
//             gettimeofday(&tv, NULL);
//             int time_sec = (int)tv.tv_sec;
//             m_txid_to_starttime->insert(make_pair(txid, time_sec));
//             // PLUGIN_LOG(INFO) << LOG_KV("send txid", txid);
//         }
//     }
//     infile.close();
//     PLUGIN_LOG(INFO) << LOG_KV("往交易池中灌入的交易数目为", inputTxsize);
// }

// // 注入调用合约的交易
// void injectTxs::injectionTransactions(string& intrashardworkload_filename, string& intershardworkload_filename,int groupId, int intratxNum, int intertxNum)
// {
//     if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) // 由转发节点投递到交易池
//     {
//         ifstream infile1(intrashardworkload_filename, ios::binary);
//         Json::Reader reader;
//         Json::Value root;

//         // 加载交易
//         int inputTxsize = intratxNum; // 发送的合约总数
//         if(reader.parse(infile1, root)) {
//             int number = root.size();
//             PLUGIN_LOG(INFO) << LOG_KV("导入的交易数目number", inputTxsize);

//             for(int i = 0; i < number; i++) {
//                 string txrlp = root[i].asString();
//                 signedTransactions.push_back(txrlp);

//                 if(i == inputTxsize) { // 倒入的交易总数目
//                     break;
//                 }
//             }
//         }

//         ifstream infile2(intershardworkload_filename, ios::binary);
//         // Json::Reader reader;
//         // Json::Value root;

//         // 加载交易
//         inputTxsize = intertxNum; // 发送的合约总数
//         if(reader.parse(infile2, root)) {
//             int number = root.size();
//             PLUGIN_LOG(INFO) << LOG_KV("导入的交易数目number", inputTxsize);
//             for(int i = 0; i < number; i++) {
//                 string txrlp = root[i].asString();
//                 signedTransactions.push_back(txrlp);

//                 if(i == inputTxsize) { // 倒入的交易总数目
//                     break;
//                 }
//             }
//         }


//         // signedTransactions 打乱
//         auto start = signedTransactions.begin();
//         auto end = signedTransactions.end();
//         srand(time(NULL));
//         random_shuffle(start, end);

//         for(auto it = start; it != end; it++){
//             m_rpcService->sendRawTransaction(dev::consensus::internal_groupId, *it);

//             // 获取交易id-->时间
//             string txrlp = *it;
//             Transaction::Ptr tx = std::make_shared<Transaction>(
//                 jsToBytes(txrlp, OnFailed::Throw), CheckTransaction::Everything);
//             // 解析交易data字段，获取交易txid
//             string data_str = dataToHexString(tx->get_data());

//             string txid;
//             if(data_str.find("0x444555666", 0) != -1) // 若交易为纯片内交易
//             {
//                 vector<string> dataItems;
//                 boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
//                 txid = dataItems.at(2).c_str();
//             }
//             else if(data_str.find("0x111222333", 0) != -1) // 若交易为原始跨片交易
//             {
//                 vector<string> dataItems;
//                 boost::split(dataItems, data_str, boost::is_any_of("|"), boost::token_compress_on);
//                 txid = dataItems.at(1).c_str();
//             }

//             struct timeval tv;
//             gettimeofday(&tv, NULL);
//             int time_sec = (int)tv.tv_sec;
//             m_txid_to_starttime->insert(make_pair(txid, time_sec)); // txid --> starttime

//             PLUGIN_LOG(INFO) << LOG_KV("data_str", data_str)
//                              << LOG_KV("send txid", txid);
//         }

//         infile1.close();
//         infile2.close();
//         PLUGIN_LOG(INFO) << LOG_KV("往交易池中灌入的交易数目为", intratxNum + intertxNum);
//     }
// }

// // 生成一笔片内交易
// string injectTxs::generateIntraShardTx(int32_t _groupId, shared_ptr<dev::ledger::LedgerManager> ledgerManager)
// {
//     // 只需要一个节点负责生成就可以，默认转发节点生成
//     if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) // 由转发节点投递到交易池
//     {
//         string requestLabel = "0x444555666";
//         string flag = "|";
//         string stateAddress = "state" + to_string((rand() % 10000) + 1) + "_state" + to_string((rand() % 10000) + 1);
//         string hex_m_data_str = requestLabel + flag + stateAddress + flag;
//         string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
//         PLUGIN_LOG(INFO) << LOG_DESC("generateIntraShardTxs...")
//                          << LOG_KV("contract_address", contract_address)
//                          << LOG_KV("hex_m_data_str", hex_m_data_str);

//         dev::Address contactAddress(contract_address);
//         dev::eth::ContractABI abi;
//         bytes data = abi.abiIn("add(string)", hex_m_data_str);  // add
//         Transaction tx(0, 1000, 0, contactAddress, data);
//         // tx.setNonce(tx.nonce() + u256(utcTime()));
//         tx.setNonce(generateRandomValue());
//         tx.setGroupId(_groupId);
//         tx.setBlockLimit(u256(ledgerManager->blockChain(_groupId)->number()) + 500);
//         auto keyPair = KeyPair::create();
//         auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
//         tx.updateSignature(sig);
//         auto rlp = tx.rlp();
//         PLUGIN_LOG(INFO) << LOG_DESC("交易生成完毕...")
//                         << LOG_KV("rlp", toHex(rlp));
//         // PLUGIN_LOG(INFO) << LOG_KV("Generated Tx Rlp", txrlp);
//         return toHex(rlp);
//     }
//     else
//     {
//         return "";
//     }
// }

// // 生成批量的片内交易
// void injectTxs::generateIntraShardTxs(int32_t _groupId, shared_ptr<dev::ledger::LedgerManager> ledgerManager)
// {
//     // 只需要一个节点负责生成就可以，默认转发节点生成
//     if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) // 由转发节点投递到交易池
//     {
//         string requestLabel = "0x444555666";
//         string flag = "|";
//         // string stateAddress = "state" + to_string((rand() % 10000) + 1) + "_state" + to_string((rand() % 10000) + 1);

//         vector<string> accessedStates; // 存储每对跨片交易所访问的状态
//         vector<int> appearTimes; // 每队跨片交易出现的次数
//         string filename = "";

//         if(_groupId == 1)
//         {
//             // 分片1测试负载
//             accessedStates.push_back("stateA_stateC");
//             accessedStates.push_back("stateA_stateD");
//             appearTimes.push_back(50000);
//             appearTimes.push_back(50000);
//             // appearTimes.push_back(500000);
//             // appearTimes.push_back(500000);
//             filename = "intrashardTxs_shard1_10w.json";
//         }
//         else if(_groupId == 2)
//         {
//             // 分片2测试负载
//             accessedStates.push_back("stateE_stateG");
//             accessedStates.push_back("stateF_stateH");
//             accessedStates.push_back("stateG_stateH");
//             appearTimes.push_back(10000);
//             appearTimes.push_back(10000);
//             appearTimes.push_back(80000);
//             // appearTimes.push_back(100000);
//             // appearTimes.push_back(100000);
//             // appearTimes.push_back(800000);
//             filename = "intrashardTxs_shard2_10w.json";
//         }

//         vector<string> txrlps;
//         int accessedStates_size = accessedStates.size();
//         for(int i = 0; i < accessedStates_size; i++){
//             string stateAddress = accessedStates.at(i);
//             string hex_m_data_str = requestLabel + flag + stateAddress + flag;
//             string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";

//             PLUGIN_LOG(INFO) << LOG_DESC("generateIntraShardTxs...")
//                              << LOG_KV("contract_address", contract_address)
//                              << LOG_KV("hex_m_data_str", hex_m_data_str);

//             dev::Address contactAddress(contract_address);
//             dev::eth::ContractABI abi;
//             bytes data = abi.abiIn("add(string)", hex_m_data_str);  // add
//             int appearTime = appearTimes.at(i);
//             for(int i = 0; i < appearTime; i++){
//                 Transaction tx(0, 1000, 0, contactAddress, data);
//                 // tx.setNonce(tx.nonce() + u256(utcTime()));
//                 tx.setNonce(generateRandomValue());
//                 tx.setGroupId(_groupId);
//                 tx.setBlockLimit(u256(ledgerManager->blockChain(_groupId)->number()) + 500);
                
//                 auto keyPair = KeyPair::create();
//                 auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
//                 tx.updateSignature(sig);
//                 auto rlp = tx.rlp();
//                 // PLUGIN_LOG(INFO) << LOG_DESC("交易生成完毕...")xw
//                 //                  << LOG_KV("rlp", toHex(rlp));
//                 txrlps.push_back(toHex(rlp));
//             }
//         }

//         // 打乱txrlps中的元素
//         auto start = txrlps.begin();
//         auto end = txrlps.end();
//         srand(time(NULL));
//         random_shuffle(start, end);

//         // 将 txrlps 中的交易写入文件
//         string res = "";
//         int addedTxSize = 0;
//         int tx_size = txrlps.size();
//         for(auto it = start; it != end; it++)
//         {
//             string txrlp = *it;
//             if(addedTxSize == 0)
//             {
//                 res = "[\"" + txrlp + "\"";
//             }
//             else if(addedTxSize == tx_size - 1){
//                 res += "]";
//             }
//             else{
//                 res += ",\"" + txrlp + "\"";
//             }
//             addedTxSize++;
//         }

//         ofstream out;
//         out.open(filename, ios::in|ios::out|ios::app);
//         if (out.is_open()) {
//             out << res;
//             out.close();
//         }
//     }
// }

// string transactionInjectionTest::generateInterShardTx(int32_t txid, int32_t coorGroupId, int32_t subGroupId1, int32_t subGroupId2, 
//                         std::shared_ptr<dev::ledger::LedgerManager> ledgerManager) {

//         std::string requestLabel = "0x111222333";
//         std::string flag = "|";
//         // std::string stateAddress = "state1";
//         // srand((unsigned)time(0));

//         std::string stateAddress1 = "state" + to_string((rand() % 10000) + 1);
//         std::string stateAddress2 = "state" + to_string((rand() % 10000) + 1);


//         PLUGIN_LOG(INFO) << LOG_DESC("generateInterShardTx...")
//                         << LOG_KV("stateAddress", stateAddress1)
//                         << LOG_KV("stateAddress", stateAddress2);

//         auto keyPair = KeyPair::create();

//         string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";

//         dev::Address subAddress1(contract_address);
//         dev::eth::ContractABI abi;
//         bytes data = abi.abiIn("add(string)");  // add

//         Transaction subTx1(0, 1000, 0, subAddress1, data);
//         // subTx1.setNonce(subTx1.nonce() + u256(utcTime()));
//         subTx1.setNonce(generateRandomValue());
//         subTx1.setGroupId(subGroupId1);
//         subTx1.setBlockLimit(u256(ledgerManager->blockChain(dev::consensus::internal_groupId)->number()) + 500);

//         auto subSig1 = dev::crypto::Sign(keyPair, subTx1.hash(WithoutSignature));
//         subTx1.updateSignature(subSig1);

//         auto subrlp1 = subTx1.rlp();
//         std::string signTx1 = toHex(subrlp1);

//         dev::Address subAddress2(contract_address);
//         data = abi.abiIn("add(string)");  // sub

//         Transaction subTx2(0, 1000, 0, subAddress2, data);
//         // subTx2.setNonce(subTx2.nonce() + u256(utcTime()));
//         subTx2.setNonce(generateRandomValue());
//         subTx2.setGroupId(subGroupId2);
//         subTx2.setBlockLimit(u256(ledgerManager->blockChain(dev::consensus::internal_groupId)->number()) + 500);

//         // auto keyPair = KeyPair::create();
//         auto subSig2 = dev::crypto::Sign(keyPair, subTx2.hash(WithoutSignature));
//         subTx2.updateSignature(subSig2);

//         auto subrlp = subTx2.rlp();
//         string signTx2 = toHex(subrlp);

//         PLUGIN_LOG(INFO) << LOG_KV("signTx1", signTx1)
//                          << LOG_KV("signTx2", signTx2);

//         string rwsets = stateAddress1 + "_" + stateAddress2;
        
//         // 生成跨片交易
//         string hex_m_data_str = requestLabel + flag + std::to_string(txid)
//                                     + flag + std::to_string(subGroupId1) + flag + signTx1 + flag + rwsets 
//                                     + flag + std::to_string(subGroupId2) + flag + signTx2 + flag + rwsets
//                                     + flag;

//         dev::Address crossAddress(contract_address);
//         // dev::eth::ContractABI abi;
//         data = abi.abiIn("set(string)", hex_m_data_str);  // set
//         // bytes data = [];

//         Transaction tx(0, 1000, 0, crossAddress, data);
//         // tx.setNonce(tx.nonce() + u256(utcTime()));
//         tx.setNonce(generateRandomValue());
//         tx.setGroupId(coorGroupId);
//         tx.setBlockLimit(u256(ledgerManager->blockChain(coorGroupId)->number()) + 500);
//         // auto keyPair = KeyPair::create();
//         auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
//         tx.updateSignature(sig);
//         auto rlp = tx.rlp();
//         PLUGIN_LOG(INFO) << LOG_DESC("跨片交易生成完毕...")
//                         << LOG_KV("hex_m_data_str", hex_m_data_str)
//                         << LOG_KV("rlp", toHex(rlp));
        
//         return toHex(rlp);
// }


// string injectTxs::generateInterShardTx(int32_t txid, int32_t coorGroupId, int32_t subGroupId1, int32_t subGroupId2, 
//                         std::shared_ptr<dev::ledger::LedgerManager> ledgerManager, vector<string> states) {

//         // PLUGIN_LOG(INFO) << LOG_DESC("进入generateInterShardTxs");
//         std::string requestLabel = "0x111222333";
//         std::string flag = "|";

//         std::string stateAddress1 = states.at(0);
//         std::string stateAddress2 = states.at(1);

//         // PLUGIN_LOG(INFO) << LOG_DESC("generateInterShardTx...")
//         //                  << LOG_KV("stateAddress", stateAddress1)
//         //                  << LOG_KV("stateAddress", stateAddress2);

//         auto keyPair = KeyPair::create();
//         string contract_address = "0x728a02ac510f6802813fece0ed12e7f774dab69d";
//         dev::Address subAddress1(contract_address);
//         dev::eth::ContractABI abi;
//         bytes data = abi.abiIn("add(string)");  // add

//         txid ++;
//         Transaction subTx1(0, 1000, 0, subAddress1, data);
//         // subTx1.setNonce(subTx1.nonce() + u256(utcTime()));
//         subTx1.setNonce(generateRandomValue());
//         subTx1.setGroupId(subGroupId1);
//         subTx1.setBlockLimit(u256(ledgerManager->blockChain(dev::consensus::internal_groupId)->number()) + 500);

//         auto subSig1 = dev::crypto::Sign(keyPair, subTx1.hash(WithoutSignature));
//         subTx1.updateSignature(subSig1);

//         auto subrlp1 = subTx1.rlp();
//         std::string signTx1 = toHex(subrlp1);

//         dev::Address subAddress2(contract_address);
//         data = abi.abiIn("add(string)");  // sub

//         Transaction subTx2(0, 1000, 0, subAddress2, data);
//         // subTx2.setNonce(subTx2.nonce() + u256(utcTime()));
//         subTx2.setNonce(generateRandomValue());
//         subTx2.setGroupId(subGroupId2);
//         subTx2.setBlockLimit(u256(ledgerManager->blockChain(dev::consensus::internal_groupId)->number()) + 500);

//         // auto keyPair = KeyPair::create();
//         auto subSig2 = dev::crypto::Sign(keyPair, subTx2.hash(WithoutSignature));
//         subTx2.updateSignature(subSig2);

//         auto subrlp = subTx2.rlp();
//         std::string signTx2 = toHex(subrlp);

//         // std::this_thread::sleep_for(std::chrono::milliseconds(100)); // sleep 1s
//         // PLUGIN_LOG(INFO) << LOG_KV("signTx1", signTx1)
//         //                  << LOG_KV("signTx2", signTx2);

//         // PLUGIN_LOG(INFO) << LOG_KV("txid", txid);

//         string rwsets = stateAddress1 + "_" + stateAddress2;
//         // // 生成跨片交易
//         string hex_m_data_str = requestLabel + flag + std::to_string(txid)
//                                     + flag + std::to_string(subGroupId1) + flag + signTx1 + flag + rwsets 
//                                     + flag + std::to_string(subGroupId2) + flag + signTx2 + flag + rwsets
//                                     + flag;

//         dev::Address crossAddress(contract_address);
//         // dev::eth::ContractABI abi_;
//         data = abi.abiIn("set(string)", hex_m_data_str);  // set
//         // bytes data = [];
//         // PLUGIN_LOG(INFO) << LOG_KV("hex_m_data_str", hex_m_data_str);

//         Transaction tx(0, 1000, 0, crossAddress, data);
//         // tx.setNonce(tx.nonce() + u256(utcTime()));
//         tx.setNonce(generateRandomValue());
//         tx.setGroupId(coorGroupId);
//         tx.setBlockLimit(u256(ledgerManager->blockChain(coorGroupId)->number()) + 500);

//         // auto keyPair = KeyPair::create();
//         auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
//         tx.updateSignature(sig);
//         auto rlp = tx.rlp();
//         return toHex(rlp);
// }

// void injectTxs::generateInterShardTxs(int32_t groupId, string& filename, shared_ptr<dev::ledger::LedgerManager> ledgerManager)
// {
//     // 只需要一个节点负责生成就可以，默认转发节点生成
//     if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)) // 由转发节点投递到交易池
//     {
//         vector<string> txrlps;
//         if(groupId == 3) // 最后改成配置传进来
//         {
//             vector<vector<string>> accessedstates;
//             vector<string> states;
//             states.push_back("stateA");
//             states.push_back("stateE");
//             accessedstates.push_back(states);
//             states.clear();
//             // states.push_back("stateB");
//             // states.push_back("stateF");
//             // accessedstates.push_back(states);
//             // states.clear();

//             string txrlp;
//             for(int txid = 0; txid < 100000; txid++)
//             {
//                 if(txid < 100000){
//                     txrlp = generateInterShardTx(txid, 3, 1, 2, ledgerManager, accessedstates.at(0));
//                 }
//                 // else if(txid < 100000){
//                 //     txrlp = generateInterShardTx(txid, 3, 1, 2, ledgerManager, accessedstates.at(1));
//                 // }
//                 txrlps.push_back("0x" + txrlp);
//             }
//         }

//         // 打乱txrlps中的元素
//         auto start = txrlps.begin();
//         auto end = txrlps.end();
//         srand(time(NULL));
//         random_shuffle(start, end);

//         // 将 txrlps 中的交易写入文件
//         string res = "";
//         int addedTxSize = 0;
//         int tx_size = txrlps.size();
//         for(auto it = start; it != end; it++)
//         {
//             string txrlp = *it;
//             if(addedTxSize == 0)
//             {
//                 res = "[\"" + txrlp + "\"";
//             }
//             else if(addedTxSize == tx_size - 1){
//                 res += "]";
//             }
//             else{
//                 res += ",\"" + txrlp + "\"";
//             }
//             addedTxSize++;
//         }

//         ofstream out;
//         out.open(filename, ios::in|ios::out|ios::app);
//         if (out.is_open()) {
//             out << res;
//             out.close();
//         }
//     }
// }
