#include <leveldb/db.h>
#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/BlockVerifier.h>
#include <libblockverifier/Common.h>
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcore/BasicLevelDB.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/TopicInfo.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libethcore/Block.h>
#include <libethcore/PrecompiledContract.h>
#include <libethcore/Protocol.h>
#include <libethcore/TransactionReceipt.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/P2PInitializer.h>
#include <libmptstate/MPTStateFactory.h>
#include <librpc/Rpc.h>
#include <libstorage/LevelDBStorage.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>
#include <libstoragestate/StorageStateFactory.h>
#include <libplugin/PluginMsgManager.h>
#include <libplugin/SyncThreadMaster.h>
#include <libplugin/PluginMsgBase.h>
#include <libplugin/Common.h>
#include <stdlib.h>
#include <sys/time.h>
#include <tbb/concurrent_queue.h>
#include <cassert>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <thread>
#include <libplugin/Benchmark.h>
#include <libethcore/Block.h>
#include <libconsensus/pbft/Common.h>
#include "Common.h"
#include <libplugin/InjectThreadMaster.h>
#include <libplugin/hieraShardTree.h>
#include "argparse.h"
using namespace std;
using namespace dev;
using namespace dev::crypto;
using namespace dev::eth;
using namespace dev::rpc;
using namespace dev::ledger;
using namespace dev::initializer;
using namespace dev::txpool;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::storage;
using namespace dev::mptstate;
using namespace dev::executive;
using namespace dev::plugin;
using namespace dev::consensus;
using namespace dev::plugin;

namespace dev {
    namespace plugin {
        shared_ptr<ExecuteVM> executiveContext;
        string nodeIdHex;
        shared_ptr<dev::plugin::HieraShardTree> hieraShardTree = make_shared<dev::plugin::HieraShardTree>();
        int injectSpeed = 5000;
        int inject_threadNumber = 2;
        int total_injectNum = -1;
        int intra_generateNumber = 1000000;
        int inter_generateNumber = 100000;
        int cross_rate = 20;
        int txid = 0;
        shared_ptr<map<string, bool>> m_validStateKey = make_shared<map<string, bool>>();
        shared_ptr<map<string, bool>> m_masterStateKey = make_shared<map<string, bool>>();
        shared_ptr<map<string, int>> m_masterRequestVotes = make_shared<map<string, int>>();
        shared_ptr<map<string, int>> m_masterChangedKey = make_shared<map<string, int>>();
        shared_ptr<set<string>> m_lockedStateKey = make_shared<set<string>>();
        Address depositAddress;
        shared_ptr<map<string, long>> m_txid_to_starttime = make_shared<map<string, long>>(); // txhash --> starttime
        shared_ptr<map<string, long>> m_txid_to_endtime = make_shared<map<string, long>>(); // txhash --> starttime
        shared_ptr<map<string, string>> m_coordinator_epochId2data_str = make_shared<std::map<std::string, string>>();
    }
}

namespace dev {
namespace consensus {
int internal_groupId; // 当前分片所在的groupID
int hiera_shard_number; // 分片总数
vector<h512>forwardNodeId;
vector<h512>shardNodeId;
std::vector<std::pair<std::string, uint64_t>> count_committxTime; // 统计结束共识时间
std::vector<std::pair<std::string, uint64_t>> count_starttxTime; //统计开始共识时间 
}
}

namespace dev {
namespace blockverifier{}
}

namespace dev{
namespace rpc{}
}

int main(int argc, char const *argv[]) {
    auto args = util::argparser("hiera.");
    args.set_program_name("test")
        .add_help_option()
        // .add_sc_option("-v", "--version", "show version info", []() {
        //     std::cout << "version " << VERSION << std::endl;
        // })
        .add_option("-g", "--generate", "need to generate tx json or not") // 生成模式 / 注入模式
        .add_option<int>("-n", "--number", "inject tx number, default is 400000", 400000) // 注入交易总数
        .add_option<int>("-s", "--speed", "inject speed(tps), default is 5000", 5000) // 注入速度
        .add_option<int>("-c", "--cross", "cross rate(0 ~ 100), default is 20", 20) // 跨片比例，0 ~ 100
        .add_option<int>("-t", "--thread", "inject thread number, default is 2", 2) // 线程数
        // .add_option<util::StepRange>("-r", "--range", "range", util::range(0, 10, 2)) 
        // .add_named_argument<std::string>("input", "initialize file")
        // .add_named_argument<std::string>("output", "output file")
        .parse(argc, argv);
    int injectNumber = args.get_option_int("--number");
    inject_threadNumber = args.get_option_int("--thread");
    dev::plugin::injectSpeed = args.get_option_int("--speed") / inject_threadNumber;
    cross_rate = args.get_option_int("--cross");
    if (cross_rate > 100 || cross_rate < 0) {
        cout << "cross rate should be in range [0,100]!";
        return 1;
    }
    if (args.has_option("--generate")) {
        cout << "need to generate tx json..." << endl;
    }
    cout << "inject tx Number: " << injectNumber << endl;
    cout << "thread Number: " << inject_threadNumber << endl;
    cout << "inject Speed(total): " << args.get_option_int("--speed") << endl;
    cout << "inject Speed(each thread): " << injectSpeed << endl;
    cout << "cross Rate: " << cross_rate << endl;
    string upper_groupIds = "";
    string lower_groupIds = "";
//    dev::consensus::hiera_shard_number = 9; // 初始化分片数目 // 3, 4, 9, 13
    string tree_struct_json_filename = "../tree.json";
    hieraShardTree->buildFromJson(tree_struct_json_filename);
    cout << "build hieraShardTree success..." << endl;
    cout << "hieraShardRTree strcuture: " << endl;
    hieraShardTree->print();
    cout << endl;
    // todo common.h
    // 在建树时，已经统计得到了分片总数并存在成员变量中，直接get获取
    dev::consensus::hiera_shard_number = dev::plugin::hieraShardTree->get_hiera_shard_number();
    cout << "hiera_shard_number:" << hiera_shard_number << endl;
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini("./configgroup.ini", pt);
    internal_groupId = std::stoi(pt.get<std::string>("group.internal_group_id")); // 片内使通信使用的groupID
    // todo
    // 通过hieraShardTree中的方法得到coordinator和subShards字符串
    loadHieraInfo(pt, &upper_groupIds, &lower_groupIds);

    GroupP2PService groupP2Pservice("./configgroup.ini");
    auto p2pService = groupP2Pservice.p2pInitializer()->p2pService();
    hieraShardTree->putGroupPubKeyIntoService(p2pService, pt); // 读取全网所有节点，并存入p2pService中
    // putGroupPubKeyIntoService(p2pService, pt);
    // putGroupPubKeyIntoshardNodeId(pt); // 读取全网所有节点
    GROUP_ID groupId = std::stoi(pt.get<std::string>("group.global_group_id")); // 全局通信使用的groupid
    auto nodeIdstr = asString(contents("conf/node.nodeid"));
    NodeID nodeId = NodeID(nodeIdstr.substr(0, 128));
    dev::plugin::nodeIdHex = toHex(nodeId);
    PROTOCOL_ID group_protocol_id = getGroupProtoclID(groupId, ProtocolID::InterGroup);
    PROTOCOL_ID protocol_id = getGroupProtoclID(internal_groupId, ProtocolID::PBFT);
    cout << "upper_groupIds = " << upper_groupIds << endl;
    cout << "lower_groupIds = " << lower_groupIds << endl;
    p2pService->start();

    std::shared_ptr<Service> intra_p2pService; // 片内P2P通信指针
    std::shared_ptr<dev::initializer::Initializer> initialize = std::make_shared<dev::initializer::Initializer>();
    // initialize->init_with_groupP2PService("./config.ini", p2pService);  // 启动3个群组
    initialize->init_with_groupP2PService("./config.ini", p2pService, intra_p2pService, group_protocol_id);  // 启动3个群组
    // initialize->init("./config.ini");  // 启动3个群组
    auto secureInitializer = initialize->secureInitializer();
    auto ledgerManager = initialize->ledgerInitializer()->ledgerManager();
    auto consensusP2Pservice = initialize->p2pInitializer()->p2pService();
    auto rpcService = std::make_shared<dev::rpc::Rpc>(initialize->ledgerInitializer(), consensusP2Pservice);
    auto blockchainManager = ledgerManager->blockChain(internal_groupId);
    shared_ptr<SyncThreadMaster> syncs = make_shared<SyncThreadMaster>(
                                                        rpcService, p2pService, intra_p2pService, group_protocol_id, 
                                                            protocol_id, nodeId, ledgerManager, upper_groupIds, lower_groupIds);
    shared_ptr<PluginMsgManager> pluginManager = make_shared<PluginMsgManager>(
                                                        rpcService, p2pService, intra_p2pService, group_protocol_id, protocol_id);
    syncs->setAttribute(blockchainManager);
    syncs->setAttribute(pluginManager);
    syncs->startExecuteThreads(); // 启动执行线程
    shared_ptr<InjectThreadMaster> inject = make_shared<InjectThreadMaster>(rpcService, ledgerManager, injectNumber, cross_rate);
    std::this_thread::sleep_for(std::chrono::seconds(10)); // 暂停10秒，等所有服务启动完毕

    //  ================================这里是生成负载的代码================================
if(args.has_option("--generate")) {
    cout << "start genenrate tx..." << endl;
// //  生成片内交易测试负载
// 这里先改成所有的节点都有片内交易负载
//     // 只要判断当前节点是否是叶子节点即可
    // if (hieraShardTree->is_leaf(internal_groupId)) {
    //   string intrashardworkload_filename = "shard"+ to_string(internal_groupId) +"_intrashard_workload_100w";
    //   injectTxs _injectionTest(rpcService, internal_groupId, ledgerManager);
    //   //todo Benchmark.cpp
    //   _injectionTest.generateIntraShardWorkLoad(internal_groupId, intrashardworkload_filename, 1000000, hiera_shard_number);
    // }
    string intrashardworkload_filename = "shard"+ to_string(internal_groupId) +"_intrashard_workload_100w";
    injectTxs _injectionTest(rpcService, internal_groupId, ledgerManager);
    //todo Benchmark.cpp
    _injectionTest.generateIntraShardWorkLoad(internal_groupId, intrashardworkload_filename, intra_generateNumber, hiera_shard_number);
// 生成局部性跨片交易
    // 只要判断当前节点是否有局部性跨片交易 
    if (hieraShardTree->is_inter(internal_groupId)) {
      string intershardworkload_filename = "shard"+ to_string(internal_groupId) +"_intershard_workload_10w.json";
      injectTxs _injectionTest(rpcService, internal_groupId, ledgerManager);
      //todo Benchmark.cpp
      _injectionTest.generateInterShardWorkLoad(internal_groupId, intershardworkload_filename, inter_generateNumber, ledgerManager);
    }
// 生成跨层交易
    //只要判断当前节点是否有跨层交易
    if (hieraShardTree->is_cross_layer(internal_groupId)) {
      string crosslayerworkload_filename = "shard"+ to_string(internal_groupId) +"_crosslayer_workload_10w.json";
      injectTxs _injectionTest(rpcService, internal_groupId, ledgerManager);
      //todo Benchmark.cpp
      _injectionTest.generateCrossLayerWorkLoad(internal_groupId, crosslayerworkload_filename, inter_generateNumber, ledgerManager);
    }
} else {
    //此处发送交易
    // todo InjectThreadMaster.cpp
    inject->startInjectThreads(inject_threadNumber); // 启动负载导入线程
}
    //  ================================生成负载结束================================

    // 监控交易平均延时
    auto transactionExecuter = syncs->m_transactionExecuter;
    while (true) {
        transactionExecuter->average_latency();
        
        // 检查所有阻塞队列中剩下的交易数目
        int uppershards_size = transactionExecuter->upper_groupids.size();
        for(int i = 0; i < uppershards_size; i++){
            int coordinator_shardid = transactionExecuter->upper_groupids.at(i);
            auto blockingTxQueue = transactionExecuter->m_blockingTxQueues->at(to_string(coordinator_shardid));
            int blockingTxQueue_size = blockingTxQueue->txsize();

            PLUGIN_LOG(INFO) << LOG_KV("blockingTxQueue_coordinator_shardid", coordinator_shardid)
                             << LOG_KV("blockingTxQueue_size", blockingTxQueue_size);

            // if(blockingTxQueue_size > 0){
            //     PLUGIN_LOG(INFO) << LOG_DESC("当前队列中有被阻塞的交易, 且队首交易等待的读写集为");
            //     auto txplusrwset = blockingTxQueue->frontTx();
            //     auto rwkeyToReceive = txplusrwset->rwkeyToReceive;
            //     int rwkey_size = rwkeyToReceive.size();
            //     for(int i = 0; i < rwkey_size; i++){
            //         PLUGIN_LOG(INFO) << LOG_KV("key", rwkeyToReceive.at(i));
            //     }
            //     PLUGIN_LOG(INFO) << LOG_KV("is_intershardTx", txplusrwset->is_intershardTx);
            // }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1)); // 每秒返回一个交易的平均延时
    }

    return 0;
}

