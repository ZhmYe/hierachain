#pragma once
#include <memory>
#include <atomic>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/Address.h>
#include <libprotobasic/shard.pb.h>
#include <libplugin/Common.h>
#include <librpc/Rpc.h>
#include <libdevcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libplugin/TransactionExecuter.h>
#include <libp2p/P2PInterface.h>

namespace dev {
    namespace plugin {
        class PluginMsgManager:public std::enable_shared_from_this<PluginMsgManager>{
            public:
                PluginMsgManager(std::shared_ptr<dev::rpc::Rpc> rpc_service, std::shared_ptr <dev::p2p::P2PInterface> group_p2p_service, 
                    std::shared_ptr <dev::p2p::P2PInterface> p2p_service, dev::PROTOCOL_ID group_protocolID, dev::PROTOCOL_ID protocolID)
                {
                    readSetQueue = new tbb::concurrent_queue<protos::TxWithReadSet>();
                    txs = new tbb::concurrent_queue<protos::Transaction>();
                    distxs = new tbb::concurrent_queue<protos::SubCrossShardTx>();
                    precommit_txs = new tbb::concurrent_queue<protos::SubPreCommitedDisTx>();
                    commit_txs = new tbb::concurrent_queue<protos::CommittedRLPWithReadSet>();
                    RWSetMsg = new tbb::concurrent_queue<protos::csTxRWset>();
                    responseToForwardMsg = new tbb::concurrent_queue<protos::ResponseToForward>();
                    commitResponseToCoordinatorMsg = new tbb::concurrent_queue<protos::CommitResponseToCoordinator>();
                    requestForMasterShardMsg = new tbb::concurrent_queue<protos::RequestForMasterShardMsg>();
                    masterShardPrePrepareMsg = new tbb::concurrent_queue<protos::MasterShardPrePrepareMsg>();
                    masterShardPrepareMsg = new tbb::concurrent_queue<protos::MasterShardPrepareMsg>();
                    masterShardCommitMsg = new tbb::concurrent_queue<protos::MasterShardCommitMsg>();
                    intraShardTxMsg = new tbb::concurrent_queue<protos::IntraShardTxMsg>();
                    shuffleStateValueMsg = new tbb::concurrent_queue<protos::ShuffleStateValue>();
                    shuffleTxMsg = new tbb::concurrent_queue<protos::ShuffleTxRlps>();
                    batchdistxs = new tbb::concurrent_queue<protos::BatchDistributedTxMsg>();
                    // notFinishedDAG = 0;
                    m_rpc_service = rpc_service;
                }
                void processReceivedWriteSet(protos::TxWithReadSet _rs);

                void processReceivedTx(protos::Transaction _tx);

                void processCommitResponseTxCoordinatorMsg(protos::CommitResponseToCoordinator msg_rs);

                void processReceivedDisTx(protos::SubCrossShardTx _txrlp);

                void processReceivedPreCommitedTx(protos::SubPreCommitedDisTx _txrlp);

                void processReceivedCommitedTx(protos::CommittedRLPWithReadSet _txrlp);

                void processRWSetMsg(protos::csTxRWset _RWSetMsg);

                void processResponseToForwardMsg(protos::ResponseToForward _responMsg);

                void processRequestForMasterShardMsg(protos::RequestForMasterShardMsg msg_rs);

                void processMasterShardPrePrepareMsg(protos::MasterShardPrePrepareMsg msg_rs);

                void processMasterShardPrepareMsg(protos::MasterShardPrepareMsg msg_rs);

                void processMasterShardCommitMsg(protos::MasterShardCommitMsg msg_rs);

                void processBatchDistributedTxMsg(protos::BatchDistributedTxMsg msg_rs);

                void processshuffleStateValueMsg(protos::ShuffleStateValue msg_rs);

                void processShuffleTxRlpsMsg(protos::ShuffleTxRlps msg_rs);

                // int numOfNotFinishedDAGs();

                // int addNotFinishedDAGs(int _num);

                // u256 getLatestState(std::string _addr);
                
                // void updateNotLatest(std::string const _state);

                // void removeNotLatest(std::string const _state);

                // bool isLatest(std::string const _state);
                
                // /// record already send DAG(wait exnode) key: dagId value: waitStateNum
                // tbb::concurrent_unordered_map<int, int> DAGMap;

                // /// record wait DAG key:dagId value:DAG
                // std::map<int,protos::DAGWithReadSet> m_DAGWaitValue;

                // /// record wait State
                // std::map<std::string,std::queue<int>> m_waitValueQueue;

                // tbb::concurrent_unordered_map<std::string,u256> testMap;

                /// receive writeResult from exnode
                tbb::concurrent_queue<protos::TxWithReadSet> *readSetQueue;

                // receive txs from leader
                tbb::concurrent_queue<protos::Transaction> *txs;

                // receive dis_txs from leader
                tbb::concurrent_queue<protos::SubCrossShardTx> *distxs;

                tbb::concurrent_queue<protos::csTxRWset> *RWSetMsg;

                tbb::concurrent_queue<protos::ResponseToForward> *responseToForwardMsg;

                tbb::concurrent_queue<protos::CommitResponseToCoordinator> *commitResponseToCoordinatorMsg;

                tbb::concurrent_queue<protos::RequestForMasterShardMsg> *requestForMasterShardMsg;

                tbb::concurrent_queue<protos::MasterShardPrePrepareMsg> *masterShardPrePrepareMsg;

                tbb::concurrent_queue<protos::MasterShardPrepareMsg> *masterShardPrepareMsg;

                tbb::concurrent_queue<protos::MasterShardCommitMsg> *masterShardCommitMsg;

                tbb::concurrent_queue<protos::IntraShardTxMsg> *intraShardTxMsg;

                tbb::concurrent_queue<protos::ShuffleStateValue> *shuffleStateValueMsg;

                tbb::concurrent_queue<protos::ShuffleTxRlps> *shuffleTxMsg;

                tbb::concurrent_queue<protos::SubPreCommitedDisTx> *precommit_txs;

                tbb::concurrent_queue<protos::CommittedRLPWithReadSet> *commit_txs;

                tbb::concurrent_queue<protos::BatchDistributedTxMsg> *batchdistxs;

                /// no need so much mutex delete later
                std::mutex x_latest_Mutex;
                std::mutex x_wait_Mutex;
                std::mutex x_snapshot_Mutex;
                std::mutex x_map_Mutex;
                std::mutex txRWsetNum_mutex;
                // std::atomic<int> notFinishedDAG;
                std::shared_ptr<dev::rpc::Rpc> m_rpc_service;

            private:
                /// global set to record latest_state
                std::set<std::string> not_latest;
        };
    }
}

