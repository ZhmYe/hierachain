#pragma once

#include <libplugin/PluginMsgBase.h>
#include <librpc/Rpc.h>
#include <libplugin/Benchmark.h>

using namespace dev::plugin;

namespace dev{
    namespace plugin {

        class InjectThreadMaster {
           public:
                InjectThreadMaster(std::shared_ptr<dev::rpc::Rpc> _rpc_service, 
                                    std::shared_ptr<dev::ledger::LedgerManager> ledgerManager,
                                    int txNum, int crossRate) {
                    m_rpc_service = _rpc_service;
                    m_ledgerManager = ledgerManager;
                    // m_minitest_9shard = std::make_shared<Test_9shard>(txNum, interRate/intraRate);
                    // m_minitest_13shard = std::make_shared<Test_13shard>(txNum, interRate/intraRate);
                    m_minitest_shard = std::make_shared<Test_shard>(txNum, crossRate);

                }

                void startInjectThreads(int threadNum);

                void injectTransactions(int threadId, int threadNum);

                void load_WorkLoad(int intra_shardTxNumber, int inter_shardTxNumber, int cross_layerTxNumber,std::shared_ptr<dev::rpc::Rpc> rpcService, std::shared_ptr<dev::ledger::LedgerManager> ledgerManager, int threadId);
                
                void load_locality_WorkLoad(int intra_shardTxNumber, int inter_shardTxNumber, int cross_layerTxNumber,std::shared_ptr<dev::rpc::Rpc> rpcService, std::shared_ptr<dev::ledger::LedgerManager> ledgerManager, int threadId);

            public:
                // 导入测试负载
                int intra_shardTxNumber;
                int inter_shardTxNumber;
                int cross_layerTxNumber;

                shared_ptr<dev::ledger::LedgerManager> m_ledgerManager;
                shared_ptr<dev::rpc::Rpc> m_rpc_service;
                shared_ptr<dev::plugin::Test_9shard> m_minitest_9shard;
                shared_ptr<dev::plugin::Test_13shard> m_minitest_13shard;
                shared_ptr<dev::plugin::Test_shard> m_minitest_shard;
       };
    }
}