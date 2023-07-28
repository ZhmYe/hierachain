#include <libplugin/PluginMsgManager.h>
#include <librpc/Common.h>
#include <libsync/SyncMsgPacket.h>
#include "Common.h"
#include <libdevcrypto/Common.h>

using namespace std;
using namespace dev;
using namespace dev::plugin;

// void PluginMsgManager::updateNotLatest(std::string const _state)
// {
//     std::lock_guard<std::mutex> lock(x_latest_Mutex);
//     not_latest.insert(_state);
// }

// void PluginMsgManager::removeNotLatest(std::string const _state)
// {
//     std::lock_guard<std::mutex> lock(x_latest_Mutex);
//     not_latest.erase(_state);
// }

// bool PluginMsgManager::isLatest(std::string const _state)
// {
//     std::lock_guard<std::mutex> lock(x_latest_Mutex);
//     if(not_latest.count(_state)>0) {return false;}
//     return true;
// }

void PluginMsgManager::processReceivedTx(protos::Transaction _tx)
{
    txs->push(_tx);
}

void PluginMsgManager::processReceivedDisTx(protos::SubCrossShardTx _txrlp)
{
    distxs->push(_txrlp);
}

void PluginMsgManager::processRWSetMsg(protos::csTxRWset _RWSetMsg)
{
    RWSetMsg->push(_RWSetMsg);
}

void PluginMsgManager::processResponseToForwardMsg(protos::ResponseToForward _responMsg)
{
    responseToForwardMsg->push(_responMsg);
}

void PluginMsgManager::processCommitResponseTxCoordinatorMsg(protos::CommitResponseToCoordinator msg_rs)
{
    commitResponseToCoordinatorMsg->push(msg_rs);
}

void PluginMsgManager::processRequestForMasterShardMsg(protos::RequestForMasterShardMsg msg_rs)
{
    requestForMasterShardMsg->push(msg_rs);
}

void PluginMsgManager::processMasterShardPrePrepareMsg(protos::MasterShardPrePrepareMsg msg_rs)
{
    masterShardPrePrepareMsg->push(msg_rs);
}

void PluginMsgManager::processMasterShardPrepareMsg(protos::MasterShardPrepareMsg msg_rs)
{
    masterShardPrepareMsg->push(msg_rs);
}

void PluginMsgManager::processMasterShardCommitMsg(protos::MasterShardCommitMsg msg_rs)
{
    masterShardCommitMsg->push(msg_rs);
}

void PluginMsgManager::processReceivedPreCommitedTx(protos::SubPreCommitedDisTx _txrlp)
{
    precommit_txs->push(_txrlp);
}

void PluginMsgManager::processReceivedCommitedTx(protos::CommittedRLPWithReadSet _txrlp)
{
    commit_txs->push(_txrlp);
}

void PluginMsgManager::processReceivedWriteSet(protos::TxWithReadSet _rs)
{
    readSetQueue->push(_rs);
}

void PluginMsgManager::processBatchDistributedTxMsg(protos::BatchDistributedTxMsg msg_rs)
{
    batchdistxs->push(msg_rs);
}

void PluginMsgManager::processshuffleStateValueMsg(protos::ShuffleStateValue msg_rs)
{
    shuffleStateValueMsg->push(msg_rs);
}

void PluginMsgManager::processShuffleTxRlpsMsg(protos::ShuffleTxRlps msg_rs)
{
    shuffleTxMsg->push(msg_rs);
}

// int PluginMsgManager::numOfNotFinishedDAGs()
// {
//     return notFinishedDAG;
// }

// int PluginMsgManager::addNotFinishedDAGs(int _num)
// {
//     notFinishedDAG += _num;
// }

// u256 PluginMsgManager::getLatestState(std::string _addr){

//     if(testMap.count(_addr)>0){
//         return testMap[_addr];
//     }else{
//         testMap.insert(std::make_pair(_addr, u256(0)));
//         return u256(0);
//     }
// }