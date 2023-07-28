#pragma once

#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/CommonData.h>
#include <libethcore/Protocol.h>
#include <libp2p/Common.h>
#include <libp2p/P2PInterface.h>
#include <libsync/Common.h>
#include <libplugin/PluginMsgManager.h>
#include <libplugin/Common.h>
#include <memory>
#include <thread>
#include <map>

namespace dev{
    namespace plugin {

        class PluginMsgBase:public std::enable_shared_from_this<PluginMsgBase>{
            /* 
            Input:
                _service: p2p网络传输接口
                _protocolId: 协议Id，同一个grouo内的节点通信协议 Id 相同
                _sharedId: 同一个company内的节点通信协议 SharedId 相同
                _nodeId: 本节点的 Id
            */

            public:
                PluginMsgBase(shared_ptr <dev::p2p::P2PInterface> _service, PROTOCOL_ID const &_protocolId, dev::network::NodeID const &_nodeId)
                :m_service(_service), m_protocolId(_protocolId), m_groupId(dev::eth::getGroupAndProtocol(_protocolId).first), m_nodeId(_nodeId) {
                    m_service->registerHandlerByProtoclID(m_protocolId, boost::bind(&PluginMsgBase::messageHandler, this, _1, _2, _3));
                }

                void stop();

                ~PluginMsgBase(){ stop();};

                // P2P消息触发的回调函数
                void messageHandler(dev::p2p::NetworkException _e, shared_ptr<dev::p2p::P2PSession> _sesion, dev::p2p::P2PMessage::Ptr _msg);

                void setAttribute(shared_ptr<dev::blockchain::BlockChainInterface> m_blockchainManager);

                void setAttribute(shared_ptr<PluginMsgManager> _pluginManager);

            protected:
                
                shared_ptr <dev::p2p::P2PInterface> m_service;
                PROTOCOL_ID m_protocolId;
                GROUP_ID m_groupId;
                dev::network::NodeID m_nodeId;
                shared_ptr <PluginMsgManager> m_pluginManager;
                shared_ptr <dev::blockchain::BlockChainInterface> m_blockchainManager;
                // map <uint64_t, uint64_t> checkpoint_count;

        };
    }
}