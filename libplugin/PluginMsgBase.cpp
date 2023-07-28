#include "PluginMsgBase.h"
#include <libsync/SyncMsgPacket.h>
#include <libprotobasic/shard.pb.h>

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::sync;
using namespace dev::plugin;

void PluginMsgBase::stop(){
    if (m_service){
        m_service->removeHandlerByProtocolID(m_protocolId);
    }
}

void PluginMsgBase::messageHandler(dev::p2p::NetworkException _e, shared_ptr <dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _msg){
    try{
        SyncMsgPacket::Ptr packet = make_shared<SyncMsgPacket>();
        if(!packet->decode(_session, _msg)){
            SYNC_ENGINE_LOG(WARNING)
                    << LOG_BADGE("Rev") << LOG_BADGE("Packet") << LOG_DESC("Reject packet")
                    << LOG_KV("reason", "decode failed")
                    << LOG_KV("nodeId", _session->nodeID().abridged())
                    << LOG_KV("size", _msg->buffer()->size())
                    << LOG_KV("message", toHex(*_msg->buffer()));
            return;
        }

        if(packet->packetType == ParamRequestPacket){
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();            
            PLUGIN_LOG(INFO) << LOG_DESC("Receive ParamRequestPacket...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                // 此处是处理 ParamRequestPacket 逻辑
            }
        }
        else if(packet->packetType == ParamResponsePacket){
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive ParamResponsePacket...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{

            }
        }
        else if (packet->packetType == CheckpointPacket){
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive CheckpointPacket...");
            if (size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
            }
        }
        else if (packet->packetType == BlockForExecutePacket){
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive BlockForExecutePacket...");
            if (size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{

            }
        }
        else if(packet->packetType == BlockForStoragePacket){
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive BlockForStoragePacket...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    string str = rlps[0].toString();
                    protos::Transaction msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processReceivedTx(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == DistributedTxPacket) // 若是分布式事务
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive DistributedTxPacket...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    string str = rlps[0].toString();
                    protos::SubCrossShardTx msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processReceivedDisTx(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == PreCommittedTxPacket) // 若是 分布式事务precommit消息
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive PreCommittedTxPacket...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    std::string str = rlps[0].toString();
                    protos::SubPreCommitedDisTx msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processReceivedPreCommitedTx(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == CommittedTxPacket) // 若是 分布式事务precommit消息
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive CommittedTxPacket...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    std::string str = rlps[0].toString();
                    protos::CommittedRLPWithReadSet msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processReceivedCommitedTx(msg_rs); // 存交易
                }
                catch(const std::exception& e)
                {
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == CommitStatePacket)
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive CommitStatePacket...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{

            }
        }
        else if(packet->packetType == ReadWriteSetMsg) // 若是分布式事务
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive ReadWriteSetMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else
            {
                try{
                    std::string str = rlps[0].toString();
                    protos::csTxRWset msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processRWSetMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == ResponseToForwardMsg) // 若是分布式事务
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive ResponseToForwardMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    std::string str = rlps[0].toString();
                    protos::ResponseToForward msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processResponseToForwardMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == CommitResponseToCoordinatorMsg) // 若是分布式事务
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            // PLUGIN_LOG(INFO) << LOG_DESC("Receive CommitResponseToCoordinatorMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    std::string str = rlps[0].toString();
                    protos::CommitResponseToCoordinator msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processCommitResponseTxCoordinatorMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == RequestForMasterShardMsg)
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive RequestForMasterShardMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    std::string str = rlps[0].toString();
                    protos::RequestForMasterShardMsg msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processRequestForMasterShardMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == MasterShardPrePrepareMsg)
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive MasterShardPrePrepareMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else
            {
                try{
                    std::string str = rlps[0].toString();
                    protos::MasterShardPrePrepareMsg msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processMasterShardPrePrepareMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == MasterShardPrepareMsg)
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive MasterShardPrepareMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else
            {
                try{
                    std::string str = rlps[0].toString();
                    protos::MasterShardPrepareMsg msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processMasterShardPrepareMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == MasterShardCommitMsg)
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive MasterShardCommitMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    std::string str = rlps[0].toString();
                    protos::MasterShardCommitMsg msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processMasterShardCommitMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == BatchDistributedTxMsg)
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            PLUGIN_LOG(INFO) << LOG_DESC("Receive BatchDistributedTxMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else
            {
                try{
                    std::string str = rlps[0].toString();
                    protos::BatchDistributedTxMsg msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processBatchDistributedTxMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == ShuffleStateValueMsg)
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            // PLUGIN_LOG(INFO) << LOG_DESC("Receive ShuffleStateValueMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    std::string str = rlps[0].toString();
                    protos::ShuffleStateValue msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processshuffleStateValueMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
        else if(packet->packetType == ShuffleTxRlpsMsg)
        {
            RLP const& rlps = (*packet).rlp();
            bool size = rlps.isNull();
            // PLUGIN_LOG(INFO) << LOG_DESC("Receive ShuffleTxRlpsMsg...");
            if(size){
                PLUGIN_LOG(INFO) << LOG_DESC("data is null!");
            }
            else{
                try{
                    std::string str = rlps[0].toString();
                    protos::ShuffleTxRlps msg_rs;
                    msg_rs.ParseFromString(str);
                    m_pluginManager->processShuffleTxRlpsMsg(msg_rs); // 存交易
                }
                catch(const std::exception& e){
                    std::cerr << e.what() << '\n';
                }
            }
        }
    }
    catch(std::exception const& e)
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("messageHandler exceptioned")
                                 << LOG_KV("errorInfo", boost::diagnostic_information(e));
    }
}

void PluginMsgBase::setAttribute(std::shared_ptr<dev::blockchain::BlockChainInterface> _blockchainManager)
{
    m_blockchainManager = _blockchainManager;
}

void PluginMsgBase::setAttribute(std::shared_ptr<PluginMsgManager> _pluginManager)
{
    m_pluginManager = _pluginManager;
}