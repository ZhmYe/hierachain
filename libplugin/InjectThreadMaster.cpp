#include "Common.h"
#include "InjectThreadMaster.h"
#include "libplugin/Benchmark.h"
#include <chrono>

using namespace std;

//  load workload
void InjectThreadMaster::load_WorkLoad(int intra_shardTxNumber, int inter_shardTxNumber, int cross_layerTxNumber,std::shared_ptr<dev::rpc::Rpc> rpcService, std::shared_ptr<dev::ledger::LedgerManager> ledgerManager, int threadId){
    
    // string inter_workload_filename = "shard"+ to_string(internal_groupId) +"_intershard_workload_10w_" + to_string(threadId) + ".json";
    // string cross_workload_filename = "shard"+ to_string(internal_groupId) +"_crosslayer_workload_10w_" + to_string(threadId) + ".json";
    // string intra_workload_filename = "shard"+ to_string(internal_groupId) +"_intrashard_workload_10w_" + to_string(threadId) + ".json";
    string intra_workload_filename = "shard"+ to_string(internal_groupId) +"_intrashard_workload_100w_1.json";
    string inter_workload_filename = "shard"+ to_string(internal_groupId) +"_intershard_workload_10w.json";
    string cross_workload_filename = "shard"+ to_string(internal_groupId) +"_crosslayer_workload_10w.json";
    injectTxs _inject(rpcService, internal_groupId, ledgerManager);
    LOG(INFO)<<LOG_DESC("查看num:load_WorkLoad")<<LOG_KV("intratxNum", intra_shardTxNumber)<<LOG_KV("intertxNum", inter_shardTxNumber)<<LOG_KV("crosslayerNum", cross_layerTxNumber);

    _inject.injectionTransactions(intra_workload_filename, inter_workload_filename, cross_workload_filename, intra_shardTxNumber, inter_shardTxNumber, cross_layerTxNumber, threadId);
}

//  load locality workload
void InjectThreadMaster::load_locality_WorkLoad(int intra_shardTxNumber, int inter_shardTxNumber, int cross_layerTxNumber,std::shared_ptr<dev::rpc::Rpc> rpcService, std::shared_ptr<dev::ledger::LedgerManager> ledgerManager, int threadId){
    string intra_workload_filename = "shard"+ to_string(internal_groupId) + "_locality_intrashard_workload_100w.json";
    string inter_workload_filename = "shard"+ to_string(internal_groupId) + "_locality_intershard_workload_10w.json";
    string cross_workload_filename = "shard"+ to_string(internal_groupId) + "_crosslayer_workload_10w.json";
    injectTxs _inject(rpcService, internal_groupId, ledgerManager);

    PLUGIN_LOG(INFO) << LOG_KV("intra_workload_filename", intra_workload_filename)
                     << LOG_KV("inter_workload_filename", inter_workload_filename)
                     << LOG_KV("cross_workload_filename", cross_workload_filename);

    _inject.injectionTransactions(intra_workload_filename, inter_workload_filename, cross_workload_filename, intra_shardTxNumber, inter_shardTxNumber, cross_layerTxNumber, threadId);
}
// todo
// 这里获取到了当前节点所在分片三种不同的交易各有多少，然后注入了对应的数量
void InjectThreadMaster::injectTransactions(int threadId, int threadNum) {
    // todo Benchmark.h
    // 这里需要重新定义一个统一的m_minitest_shard
//    cout << "m_minitest_9shard.get_6shard_intra() = " << m_minitest_9shard->get_6shard_intra() << endl
//         << "m_minitest_9shard.get_2shard_cross() = " << m_minitest_9shard->get_2shard_cross() << endl
//         << "m_minitest_9shard.get_1shard_cross() = " << m_minitest_9shard->get_1shard_cross() << endl
//         << "m_minitest_9shard.get_1shard_cross2() = " << m_minitest_9shard->get_1shard_cross2() << endl;
      int intra_shardTxNumber = 0;
      int inter_shardTxNumber = 0;
      int cross_layerTxNumber = 0;
      tuple<int, int, int> tx_number = m_minitest_shard->get_txNumber(internal_groupId);
      std::tie(intra_shardTxNumber, inter_shardTxNumber, cross_layerTxNumber) = tx_number;
      dev::plugin::total_injectNum = intra_shardTxNumber + inter_shardTxNumber + cross_layerTxNumber;
      cout << "total inject number: " << dev::plugin::total_injectNum << endl;
      intra_shardTxNumber /= threadNum;
      inter_shardTxNumber /= threadNum;
      cross_layerTxNumber /= threadNum;
//      std::this_thread::sleep_for(std::chrono::seconds(10));
      load_WorkLoad(intra_shardTxNumber, inter_shardTxNumber, cross_layerTxNumber, m_rpc_service, m_ledgerManager, threadId);

//    if(internal_groupId == 1 || internal_groupId == 2 || internal_groupId == 3 || internal_groupId == 5 || internal_groupId == 6 || internal_groupId == 7){
//        intra_shardTxNumber = m_minitest_9shard->get_6shard_intra() / threadNum;
//        // intra_shardTxNumber = 0;
//        inter_shardTxNumber = 0;
//        cross_layerTxNumber = 0;
//        std::this_thread::sleep_for(std::chrono::seconds(10)); // 暂停1秒
//        load_WorkLoad(intra_shardTxNumber, inter_shardTxNumber, cross_layerTxNumber, m_rpc_service, m_ledgerManager, threadId);
//    }
//    else if(internal_groupId == 4 || internal_groupId == 8){
//        intra_shardTxNumber = 0;
//        // inter_shardTxNumber = 70000 / threadNum;
//        inter_shardTxNumber = m_minitest_9shard->get_2shard_cross() / threadNum;
//        cross_layerTxNumber = 0;
//        load_WorkLoad(intra_shardTxNumber, inter_shardTxNumber, cross_layerTxNumber, m_rpc_service, m_ledgerManager, threadId);
//    }
//    else if(internal_groupId == 9){
//        intra_shardTxNumber = 0;
//        // inter_shardTxNumber = 35000 / threadNum;
//        // cross_layerTxNumber = 8000 / threadNum;
//        inter_shardTxNumber = m_minitest_9shard->get_1shard_cross() / threadNum;
//        cross_layerTxNumber = m_minitest_9shard->get_1shard_cross2() / threadNum;
//        load_WorkLoad(intra_shardTxNumber, inter_shardTxNumber, cross_layerTxNumber, m_rpc_service, m_ledgerManager, threadId);
//    }
}

void InjectThreadMaster::startInjectThreads(int threadNum) {
    int i = 1;
    for (; i <= threadNum; i++) {
        std::thread{[this, i, threadNum]()  {
            injectTransactions(i, threadNum);
        }}.detach();
    }
    
    // std::thread{[this]()  {
    //    injectTransactions(1);
    // }}.detach();

    // std::thread{[this]()  {
    //    injectTransactions(2);
    // }}.detach();

}