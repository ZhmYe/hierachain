#pragma once

#include <libplugin/PluginMsgBase.h>
#include <librpc/Rpc.h>
#include <libprotobasic/shard.pb.h>

using namespace dev::rpc;
using namespace dev::plugin;

namespace dev
{
    namespace eth
    {
        class Transaction;
    }
}

namespace dev{
    namespace plugin {

        // struct cachedDistributedTxPacket
        // {
        //     int sendedCrossshardTxId = 0;
        //     std::map<int, protos::SubCrossShardTx> cachedDistributedTxPacket;
        // };

        /*
        * m_service: 网络传输接口
        * m_protocolId: 同一个group内，m_protocolId相同
        * m_shaId:同一个company内传输消息的m_shaId相同
        * m_nodeId:本节点的nodeId
        * m_sharedId:本节点所属的company
        * m_group:本节点所属的group
        * m_rpc_service: 本节点所属的group的rpc服务
        */
       class SyncThreadMaster{
           public:
                SyncThreadMaster(std::shared_ptr<dev::rpc::Rpc> _rpc_service, std::shared_ptr<dev::p2p::P2PInterface> group_p2p_service, std::shared_ptr <dev::p2p::P2PInterface> p2p_service, 
                    PROTOCOL_ID const & group_protocolID, dev::PROTOCOL_ID protocolID, dev::network::NodeID const& _nodeId, std::shared_ptr<dev::ledger::LedgerManager> ledgerManager, 
                        std::string& upper_groupIds, std::string& lower_shardids)
                        :m_service(group_p2p_service), m_protocolId(group_protocolID), m_nodeId(_nodeId) {
                    m_rpc_service = _rpc_service;
                    m_transactionExecuter = make_shared<dev::plugin::TransactionExecuter>(group_p2p_service, p2p_service, group_protocolID, protocolID, upper_groupIds, lower_shardids, _rpc_service);
                    m_msgEngine = make_shared<PluginMsgBase>(group_p2p_service, group_protocolID, _nodeId);
                    m_ledgerManager = ledgerManager;
                    m_receivedResponseNum = make_shared<std::map<std::string, int>>();
                    m_receivedCommitResponseNum = make_shared<std::map<std::string, int>>();
                    // m_coordinator_epochId2data_str = make_shared<std::map<std::string, string>>();
                }

                void receiveMsgWorker();

                void receiveRemoteMsgWorker();

                void receiveRemoteCommitMsgWorker();

                // bool startP2PThread(); // 启动队列监听线程

                void start(byte const &pt, byte const& data);

                void start(byte const &pt, byte const& data, dev::network::NodeID const& destnodeId);

                shared_ptr<dev::plugin::TransactionExecuter> getdeterministExecute();

                void startExecuteThreads();

                void setAttribute(std::shared_ptr <dev::blockchain::BlockChainInterface> m_blockchainManager);

                void setAttribute(std::shared_ptr <PluginMsgManager> _plugin);

                // void dowork(dev::sync::SyncPacketType const& packettype, byte const& data);
  
                // void dowork(dev::sync::SyncPacketType const& packettype, byte const& data, dev::network::NodeID const& destnodeId);

                // void sendMessage(bytes const& _blockRLP, dev::sync::SyncPacketType const& packetreadytype);

                // void sendMessage(bytes const& _blockRLP, dev::sync::SyncPacketType const& packettype, dev::network::NodeID const& destnodeId);

                // void submitTransactionToTxPool(Transaction::Ptr tx);

                u256 generateRandomValue(){ // 用于生成交易nonce字段的随机数
                    auto randomValue = h256::random();
                    return u256(randomValue);
                }

            public:

                string m_name;
                int counter = 0;
                map <u256, std::string> contractMap;
                set<string> m_sendedStateChangeCommit;
                GROUP_ID interal_groupId;
                std::mutex x_map_Mutex;

                /// listen block thread
                // pthread_t listenthread;
                // process receive workers
                pthread_t receivethread;

                shared_ptr<dev::p2p::P2PInterface> m_service;
                PROTOCOL_ID m_protocolId;
                h512 m_nodeId;

                shared_ptr<PluginMsgBase> m_msgEngine;
                shared_ptr<dev::ledger::LedgerManager> m_ledgerManager;
                shared_ptr<dev::blockchain::BlockChainInterface> m_blockchainManager;
                shared_ptr<PluginMsgManager> m_pluginManager;
                shared_ptr<dev::rpc::Rpc> m_rpc_service;
                shared_ptr<dev::plugin::TransactionExecuter> m_transactionExecuter;
                shared_ptr<map<string, int>> m_receivedResponseNum;
                shared_ptr<map<string, int>> m_receivedCommitResponseNum;
       };
    }
}