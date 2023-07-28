#pragma once

#include <algorithm>
#include <tuple>
#include <vector>
#include <string>
#include <json/json.h>
#include <librpc/Rpc.h>
#include <sys/time.h>
#include "hieraShardTree.h"
#include "libplugin/Common.h"
using namespace std;

namespace dev
{
    namespace eth
    {
        class Transaction;
    }
}

namespace dev
{
    namespace plugin
    {
        #define PLUGIN_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("PLUGIN") << LOG_BADGE("PLUGIN")
        extern shared_ptr<dev::plugin::HieraShardTree> hieraShardTree;
        extern int injectSpeed;
        extern int total_injectNum;
        extern int inject_threadNumber;
        extern int intra_generateNumber;
        extern int inter_generateNumber;
        extern int cross_rate;
        extern int txid;
        class injectTxs
        {
            public:
                shared_ptr<dev::ledger::LedgerManager> m_ledgerManager;
                shared_ptr<dev::rpc::Rpc> m_rpcService;
                int state_num = 40000;
                int m_internal_groupId;
                vector<Transaction::Ptr> txs;
                vector<h256> tx_hashes;
                vector<Transaction::Ptr> deploytxs;

            public:
                injectTxs(shared_ptr<dev::rpc::Rpc> rpcService, int groupId, shared_ptr<dev::ledger::LedgerManager> ledgerManager){
                    m_rpcService = rpcService;
                    m_internal_groupId = groupId;
                    m_ledgerManager = ledgerManager;
                }

                u256 generateRandomValue(){ // 用于生成交易nonce字段的随机数
                    auto randomValue = h256::random();
                    return u256(randomValue);
                }

                // void injectionTransactions(string& workload_filename, int groupId, int txnum);

                // string generateIntraShardTx(int32_t _groupId, shared_ptr<dev::ledger::LedgerManager> ledgerManager);  // 生成一笔片内交易
                
                // void generateIntraShardTxs(int32_t _groupId, shared_ptr<dev::ledger::LedgerManager> ledgerManager);

                // string generateInterShardTx(int32_t txid, int32_t coorGroupId, int32_t subGroupId1, int32_t subGroupId2, shared_ptr<dev::ledger::LedgerManager> ledgerManager, vector<string> states);
                
                // void generateInterShardTxs(int32_t groupId, string& filename, shared_ptr<dev::ledger::LedgerManager> ledgerManager);

                string dataToHexString(bytes data);
                
                void injectionTransactions(string& intrashardworkload_filename, string& intershardworkload_filename, int intratxNum, int intertxNum);

                void injectionTransactions(string& intrashardworkload_filename, string& intershardworkload_filename, string& crosslayerworkload_filename, int intratxNum, int intertxNum, int crosslayerNum, int threadId);

                void deployContractTransaction(string& filename, int groupId);
                
                string generateTx(string& requestLabel, string& stateAddress, int32_t shardid, int32_t txid, bool subtx, bool crossLayer);
                
                void generateIntraShardWorkLoad(int32_t shardid, string filename, int txNumber, int shard_number);

                void generateInterShardWorkLoad(int32_t shardid, string filename, int txNumber, shared_ptr<dev::ledger::LedgerManager> ledgerManager);
        
                void generateLocalityIntraShardWorkLoad(int32_t shardid, string filename, int txNumber, int shard_number, double localityRate);

                void generateLocalityInterShardWorkLoad(int32_t coordinator_shardid, string& lower_groupIds, string filename, int txNumber, shared_ptr<dev::ledger::LedgerManager> ledgerManager, double localityRate);

                void generateCrossLayerWorkLoad(int internal_groupId, string filename, int txNumber, shared_ptr<dev::ledger::LedgerManager> ledgerManager);

                pair<int, int> getCrossLayerShardIds(int id);
        };
        //初始化时输入底层片内交易数量、跨片数/片内数 
        class Test_9shard
        {
            public:

            Test_9shard();
            Test_9shard(int num_6shard_intra,double rc)
            {
                intraTx4six=num_6shard_intra;
                rate_cross=rc;
            };

            //底层
            int get_6shard_intra()
            {
                return intraTx4six;
            };
            //中间
            int get_2shard_cross()
            {
                // cout<<
                // int res = 1.5*intraTx4six*rate_cross;
                int res = 6*intraTx4six*rate_cross*0.4;
                //纯跨片
                if(intraTx4six==0)  return max_txnum;
                if(res>70000) return 70000;
                return res;
            };
            //顶层
            int get_1shard_cross()
            {
                if(intraTx4six==0)  return int(max_txnum*(1-rate_cross_area));
                // int res = 3*intraTx4six*rate_cross*(1-rate_cross_area);
                int res = 6*intraTx4six*rate_cross*(1-rate_cross_area)*0.2;
                if(res>70000) return 70000;
                return res;

            };
            int get_1shard_cross2()
            {
                if(intraTx4six==0)  return int(max_txnum*rate_cross_area);
                // int res = 3*intraTx4six*rate_cross*rate_cross_area;
                int res = 6*intraTx4six*rate_cross*rate_cross_area*0.2;
                if(res>70000) return 70000;
                return res;

            };

            //data
            //片内交易数量
            int  intraTx4six=0;
            //跨片交易/片内交易
            double rate_cross=0;
            //纯跨片是注入交易量
            int max_txnum=70000;
            //跨域比例
            double rate_cross_area=0.05*3;


        };
        // todo
        class Test_shard {
            private:
                int  totalTxNumber=0;  //交易总数量
                // double rate_cross=0; //跨片交易与片内交易数量比例
                int rate_cross = 0; // 跨片比0~100
                int max_txnum=50000; // 最大值
                double rate_cross_area=0.05; //跨层比例
            public:
                Test_shard();
                Test_shard(int num_shard_intra, int crossRate) {
                    totalTxNumber = num_shard_intra;
                    // cout << intraTxNumber << endl;
                    rate_cross = crossRate;
                }
                tuple<int, int, int> get_txNumber(int internal_groupId) {
                    cout << "totalTxNumber = " << totalTxNumber << endl;
                    cout << "cross Rate = " << rate_cross << endl;
                    int interTxNumber = 1.0 * totalTxNumber * (float(rate_cross) / 100);
                    int intraTxNumber = totalTxNumber - interTxNumber;
                    cout << "inter Tx Number:" << interTxNumber << endl;
                    cout << "intra Tx Number:" << intraTxNumber << endl;
                    int each_shard_intraTxNumber;
                    if (dev::plugin::cross_rate == 20) {
                        each_shard_intraTxNumber = min(int(1.0 * intraTxNumber / float(dev::consensus::hiera_shard_number)), max_txnum);
                    } else {
                        each_shard_intraTxNumber = min(int(1.0 * intraTxNumber / float(dev::plugin::hieraShardTree->get_intra_shard_number())), max_txnum);
                    }
                    int inter_tx_number = interTxNumber * (1 - rate_cross_area) * float(1 / float(dev::plugin::hieraShardTree->get_inter_shard_number()));
                    int cross_tx_number = interTxNumber * rate_cross_area * float(1 / float(dev::plugin::hieraShardTree->get_cross_layer_shard_number()));
                    // 仅仅有片内交易
                    if (dev::plugin::hieraShardTree->is_leaf(internal_groupId)) {
                        return make_tuple(each_shard_intraTxNumber, 0, 0);
                    }
                    // 仅仅有局部性跨片交易
                    else if (dev::plugin::hieraShardTree->is_inter(internal_groupId) && ! dev::plugin::hieraShardTree->is_cross_layer(internal_groupId)) {
                        int local_intra_number = (dev::plugin::cross_rate == 20) ? each_shard_intraTxNumber : 0;
                        return make_tuple(local_intra_number, min(max_txnum, inter_tx_number), 0);
                    } 
                    else {
                    // 有跨层跨片交易，那么一定有局部跨片交易
                        int local_intra_number = (dev::plugin::cross_rate == 20) ? each_shard_intraTxNumber : 0;
                        return make_tuple(local_intra_number, min(inter_tx_number, max_txnum), min(cross_tx_number, max_txnum));
                  }
                }
        };
        //初始化时输入底层片内交易数量、跨片数/片内数 
        class Test_13shard
        {
            public:

            Test_13shard();
            Test_13shard(int num_9shard_intra,double rc)
            {
                intraTxdown=num_9shard_intra;
                rate_cross=rc;
            };
            //底层
            int get_down_intra()
            {
                return intraTxdown;
            };
            //中间
            int get_mid_cross()
            {
                // cout<<
                int res = 1.0*9*intraTxdown*rate_cross*(1-rate_cross_area)*0.25;
                //纯跨片
                if(intraTxdown==0)  return max_txnum;
                //不可超出限制
                return min(res,70000);
            };
            //顶层
            int get_top_cross()
            {
                if(intraTxdown==0)  return int(max_txnum*(1-rate_cross_area));
                int res =1.0*9*intraTxdown*rate_cross*(1-rate_cross_area)*0.25;
                return min(res,70000);
                
            };
            int get_top_cross2()
            {
                if(intraTxdown==0)  return int(max_txnum*rate_cross_area);
                int res =1.0*9*intraTxdown*rate_cross*(rate_cross_area);
                return min(res,70000);

            };

            //data
            //片内交易数量
            int  intraTxdown=0;
            //跨片交易/片内交易
            double rate_cross=0;
            //纯跨片是注入交易量
            int max_txnum=70000;
            //跨域比例
            double rate_cross_area=0.05;


        };
    }
}