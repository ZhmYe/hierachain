#include "Common.h"
#include "libplugin/Benchmark.h"
#include <libplugin/SyncThreadMaster.h>
#include <libprotobasic/shard.pb.h>
#include <libsync/SyncMsgPacket.h>
#include <libconsensus/ConsensusEngineBase.h>

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::sync;
using namespace dev::plugin;


void SyncThreadMaster::receiveRemoteCommitMsgWorker() {
    protos::CommitResponseToCoordinator msg_commitResponseToCoordinatorMsg;

    bool got_message;
    while (true)
    {
        // 协调者尝试接收来自其他分片提交的跨片交易id, 为了避免重复统计，每笔跨片交易只由参与分片id最小的分片回复，协调者收到后计数
        got_message = m_pluginManager->commitResponseToCoordinatorMsg->try_pop(msg_commitResponseToCoordinatorMsg);
        if(got_message == true){
            int participantshardid = msg_commitResponseToCoordinatorMsg.participantshardid();
            string intershardTxids = msg_commitResponseToCoordinatorMsg.intershardtxids();

            // PLUGIN_LOG(INFO) << LOG_KV("intershardTxids", intershardTxids);

            // 计数，用以计算交易吞吐和延迟            
            vector<string> intershardTxid_items;
            boost::split(intershardTxid_items, intershardTxids, boost::is_any_of("|"), boost::token_compress_on);
            int item_size = intershardTxid_items.size();
            for(int i = 0; i < item_size; i++){

                string item = intershardTxid_items.at(i).c_str();
                vector<string> its;
                boost::split(its, item, boost::is_any_of("_"), boost::token_compress_on);
                string txid = its.at(0).c_str();
                
                struct timeval tv;
                gettimeofday(&tv, NULL);
                long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;
                // long time_msec = atoi(its.at(1).c_str());
                m_txid_to_endtime->insert(make_pair(txid, time_msec)); // 记录txid的开始时间

                if(txid == "a3084"){
                    PLUGIN_LOG(INFO) << LOG_KV("txid", txid) << LOG_KV("time_usec", time_msec);
                }
            }
            m_transactionExecuter->add_processedTxNum(item_size);
            // PLUGIN_LOG(INFO) << LOG_KV("item_size", item_size);
            // int committednum = m_transactionExecuter->get_processedTxNum();
            // PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", (committednum));




            // PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", m_transactionExecuter->get_processedTxNum());

            // if(m_transactionExecuter->get_processedTxNum() % 1000 == 0) { // 每提交1000笔交易输出一次数目
            //     PLUGIN_LOG(INFO) << LOG_KV("跨片交易已提交2", m_transactionExecuter->get_processedTxNum());
            // }

            // 暂时先不考虑故障恢复，且由于是确定性执行，只要收到一个参与者的回复就认为本交易已经提交
            // auto crossshardtxid = msg_commitResponseToCoordinatorMsg.crossshardtxid();
            // auto result = msg_commitResponseToCoordinatorMsg.result();
            // auto participantshardid = msg_commitResponseToCoordinatorMsg.participantshardid();

            // if(m_receivedCommitResponseNum->count(crossshardtxid) == 0){
            //     m_receivedCommitResponseNum->insert(std::make_pair(crossshardtxid, 1));
            // }
            // else{
            //     int receivedResponseNum = m_receivedCommitResponseNum->at(crossshardtxid);
            //     receivedResponseNum++;
            //     m_receivedCommitResponseNum->at(crossshardtxid) = receivedResponseNum;
            //     if(m_transactionExecuter->remoteReadWriteSetNum.count(crossshardtxid) == 0){
            //         m_transactionExecuter->remoteReadWriteSetNum.insert(std::make_pair(crossshardtxid, 0));
            //     }
            //     if(receivedResponseNum == m_transactionExecuter->remoteReadWriteSetNum.at(crossshardtxid)){
            //         PLUGIN_LOG(INFO) << LOG_KV("协调者节点", dev::plugin::nodeIdHex)
            //                         << LOG_DESC("收齐了所有参与者的大多数Commit消息...");
            //     }
            // }
        }
    }

}



void SyncThreadMaster::receiveRemoteMsgWorker() {
    protos::BatchDistributedTxMsg msg_batchsubCrossShardTxs; // 接收上层发送来的排序后交易
    protos::csTxRWset msg_csTxRWset; // 交易读写集
    protos::ShuffleStateValue msg_shuffleStateValueMsg; // shuffle交换的状态value
    protos::ShuffleTxRlps msg_shuffleTxMsg; // shuffle交换的交易
    protos::CommitResponseToCoordinator msg_commitResponseToCoordinatorMsg;
    protos::RequestForMasterShardMsg msg_requestForMasterShardMsg;
    protos::MasterShardPrePrepareMsg msg_masterShardPrePrepareMsg;
    protos::MasterShardPrepareMsg msg_masterShardPrepareMsg;
    protos::MasterShardCommitMsg msg_masterShardCommitMsg;

    bool got_message=false;
    while(true) {
        // 尝试接收上层分片发来的批量跨片交易
        got_message = m_pluginManager->batchdistxs->try_pop(msg_batchsubCrossShardTxs);
        if(got_message == true) {
            PLUGIN_LOG(INFO) << LOG_DESC("收到来自上层分片的批量跨片交易");
            long unsigned coordinator_shardid = msg_batchsubCrossShardTxs.coordinatorshardid();//上层id
            long unsigned epochId = msg_batchsubCrossShardTxs.id();//
            string txcontents = msg_batchsubCrossShardTxs.txcontents();//交易内容
            // string sendedreadwriteset = msg_batchsubCrossShardTxs.tosendreadwriteset();
            string intrashard_txcontents = msg_batchsubCrossShardTxs.intrashard_txcontents(); // 转换成的片内交易
            string shuffle_states_contents = msg_batchsubCrossShardTxs.shuffle_states_contents(); // 需要shuffle的状态，以及与状态相关的交易
            
            if(txcontents == "") {
                txcontents = "none";
            }
            if(intrashard_txcontents == ""){
                intrashard_txcontents = "none";
            }

            // PLUGIN_LOG(INFO) << LOG_KV("epochId", epochId)
            //                  << LOG_KV("txcontents", txcontents);
            //                  << LOG_KV("sendedreadwriteset", sendedreadwriteset)
            //                  << LOG_KV("intrashard_txcontents", intrashard_txcontents);
            // intrashard_txcontents 样例:
            // 0x222333444|rlp1_rlp2|key1_key2


            // string requestLabel = "0x999000111";
            // string data_str = requestLabel + "|" + to_string(coordinator_shardid) + "|" +to_string(epochId) + "#" + txcontents + "#" + intrashard_txcontents + "#" + shuffle_states_contents; // 跨片子交易和重组片内交易之间用"#"号隔开
            // string coordinator_epochId = to_string(coordinator_shardid) + to_string(epochId);
            // PLUGIN_LOG(INFO) << LOG_KV("coordinator_epochId", coordinator_epochId);
            // m_coordinator_epochId2data_str->insert(make_pair(coordinator_epochId, data_str)); // 先把交易的data字段缓存起来，共识只对hash进行共识


            // 转发节点发起交易共识
            if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
                // 将 BatchDistributedTxMsg 中的内容组装到一个交易中, 重组片内交易放在跨片子交易后面
                string requestLabel = "0x999000111";
                string hex_m_testdata_str = requestLabel + "|" + to_string(coordinator_shardid) + "|" + to_string(epochId) + "#" + txcontents + "#" + intrashard_txcontents + "#" + shuffle_states_contents; // 跨片子交易和重组片内交易之间用"#"号隔开
                // string hex_m_testdata_str = requestLabel + "|" + to_string(coordinator_shardid) + "|" + to_string(epochId);
                auto testdata_str_bytes = hex_m_testdata_str.c_str();
                int bytelen = strlen(testdata_str_bytes);
                bytes hex_m_testdata;
                hex_m_testdata.clear();
                for(int i = 0; i < bytelen; i++){
                    hex_m_testdata.push_back((uint8_t)testdata_str_bytes[i]);
                }

                std::shared_ptr<Transaction> subintershard_txs = std::make_shared<Transaction>(0, 1000, 0, dev::plugin::depositAddress, hex_m_testdata);
                subintershard_txs->setNonce(generateRandomValue());
                auto keyPair = KeyPair::create();
                auto sig = dev::crypto::Sign(keyPair, subintershard_txs->hash(WithoutSignature));
                subintershard_txs->updateSignature(sig);
                string txrlp = toHex(subintershard_txs->rlp());
                m_rpc_service->sendRawTransaction(dev::consensus::internal_groupId, txrlp);
            }
        }

        // 尝试接收来自其他分片的读写集
        got_message = m_pluginManager->RWSetMsg->try_pop(msg_csTxRWset);
        if(got_message == true){
            PLUGIN_LOG(INFO) << LOG_DESC("收到来自其他分片的读写集信息");
            // auto batchId = msg_csTxRWset.crossshardtxid();
            // auto accessedNum = msg_csTxRWset.accessnum();
            string rwKeys = msg_csTxRWset.readwritekey();
            string rwValues = msg_csTxRWset.value();
            string rwContentionRates = msg_csTxRWset.contentionrate();
            string sourceshardid = to_string(msg_csTxRWset.sourceshardid());
            // PLUGIN_LOG(INFO) << LOG_DESC("读写集解析完毕")
            //                  << LOG_KV("rwKeys", rwKeys)
            //                  << LOG_KV("rwValues", rwValues)
            //                  << LOG_KV("rwContentionRates", rwContentionRates)
            //                  << LOG_KV("sourceshardid", sourceshardid);

            // 解析收到的key
            vector<string> keyItems;
            vector<string> valueItems;
            vector<string> contentionItems;
            boost::split(keyItems, rwKeys, boost::is_any_of("|"), boost::token_compress_on);
            boost::split(valueItems, rwValues, boost::is_any_of("|"), boost::token_compress_on);
            boost::split(contentionItems, rwContentionRates, boost::is_any_of("|"), boost::token_compress_on);
            
            // 缓存收到的读写集信息，存在 receivedTxRWset 和 receivedContentionRates 中
            size_t key_size = keyItems.size();
            for(size_t i = 0; i < key_size; i++){
                string receivedKey = keyItems.at(i);
                string receivedValue = valueItems.at(i);
                string contentionRate = contentionItems.at(i);

                // contentionRate的key将前面的epochId和batchId去掉, 只需要是 original key
                std::vector<std::string> items;
                boost::split(items, receivedKey, boost::is_any_of("_"), boost::token_compress_on);
                string originalKey = items.at(0);
                // string originalKey = items.at(2);  // 暂时为了避免处理不一致问题，epochID_batchId_keyAddress 临时改为了keyAddress！

                // // 先将收到的对方状态的争用率进行保存
                // PLUGIN_LOG(INFO) << LOG_KV("originalKey", originalKey)
                //                  << LOG_KV("contentionRate", contentionRate)
                //                  << LOG_KV("sourceshardid", sourceshardid);

                m_transactionExecuter->m_LRU_StatesInfo->updateRemoteKeyContention(originalKey, contentionRate, sourceshardid);
                m_transactionExecuter->m_readWriteSetManager->insertReceivedRwset(originalKey);   
                // m_transactionExecuter->m_LRU_StatesInfo->updateIntraTxAccessedremotekey(originalKey, contentionRate, sourceshardid);
                // if(m_transactionExecuter->m_readWriteSetManager->receivedTxRWset->count(receivedKey) == 0) {
                //     m_transactionExecuter->m_readWriteSetManager->receivedTxRWset->insert(std::make_pair(receivedKey, receivedValue));
                // }
            }
        }

        // 尝试接收来自其他分片分享的状态最新值
        got_message = m_pluginManager->shuffleStateValueMsg->try_pop(msg_shuffleStateValueMsg);
        if(got_message == true){
            // 转发节点发起交易共识
            if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
                string stateKey = msg_shuffleStateValueMsg.stateaddresses();
                string stateValue = msg_shuffleStateValueMsg.values();
                string sourceshardid = msg_shuffleStateValueMsg.sourceshardid();
                // PLUGIN_LOG(INFO) << LOG_DESC("收到被迁移状态在主分片的最新值")
                //                  << LOG_KV("stateKey", stateKey)
                //                  << LOG_KV("stateValue", stateValue);

                // 组装ShuffleValue交易，让片内节点基于相同状态处理该交易
                string requestLabel = "0x000000001";
                string hex_m_testdata_str = requestLabel + "|" + stateKey + "|" + stateValue + "|" + sourceshardid;
                auto testdata_str_bytes = hex_m_testdata_str.c_str();
                int bytelen = strlen(testdata_str_bytes);
                bytes hex_m_testdata;
                hex_m_testdata.clear();
                for(int i = 0; i < bytelen; i++){
                    hex_m_testdata.push_back((uint8_t)testdata_str_bytes[i]);
                }

                shared_ptr<Transaction> subintershard_txs = make_shared<Transaction>(0, 1000, 0, dev::plugin::depositAddress, hex_m_testdata);
                subintershard_txs->setNonce(generateRandomValue());
                auto keyPair = KeyPair::create();
                auto sig = dev::crypto::Sign(keyPair, subintershard_txs->hash(WithoutSignature));
                subintershard_txs->updateSignature(sig);
                string txrlp = toHex(subintershard_txs->rlp());
                m_rpc_service->sendRawTransaction(dev::consensus::internal_groupId, txrlp);
            }
        }
        
        // 尝试接收来自其他分片阻塞的交易
        got_message = m_pluginManager->shuffleTxMsg->try_pop(msg_shuffleTxMsg);
        if(got_message == true){
            // 转发节点发起交易共识
            if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
                string blocked_txRLPs = msg_shuffleTxMsg.txrlps();
                // 组装ShuffleTx交易，让片内节点基于相同状态处理该交易
                string requestLabel = "0x000000002";
                string hex_m_testdata_str = requestLabel + "|" + blocked_txRLPs;
                auto testdata_str_bytes = hex_m_testdata_str.c_str();
                int bytelen = strlen(testdata_str_bytes);
                bytes hex_m_testdata;
                hex_m_testdata.clear();
                for(int i = 0; i < bytelen; i++){
                    hex_m_testdata.push_back((uint8_t)testdata_str_bytes[i]);
                }

                shared_ptr<Transaction> subintershard_txs = make_shared<Transaction>(0, 1000, 0, dev::plugin::depositAddress, hex_m_testdata);
                subintershard_txs->setNonce(generateRandomValue());
                auto keyPair = KeyPair::create();
                auto sig = dev::crypto::Sign(keyPair, subintershard_txs->hash(WithoutSignature));
                subintershard_txs->updateSignature(sig);
                string txrlp = toHex(subintershard_txs->rlp());
                m_rpc_service->sendRawTransaction(dev::consensus::internal_groupId, txrlp);
            }
        }

        // // 协调者尝试接收来自其他分片提交的跨片交易id, 为了避免重复统计，每笔跨片交易只由参与分片id最小的分片回复，协调者收到后计数
        // got_message = m_pluginManager->commitResponseToCoordinatorMsg->try_pop(msg_commitResponseToCoordinatorMsg);
        // if(got_message == true){
        //     int participantshardid = msg_commitResponseToCoordinatorMsg.participantshardid();
        //     string intershardTxids = msg_commitResponseToCoordinatorMsg.intershardtxids();

        //     // 计数，用以计算交易吞吐和延迟            
        //     vector<string> intershardTxid_items;
        //     boost::split(intershardTxid_items, intershardTxids, boost::is_any_of("|"), boost::token_compress_on);
        //     int item_size = intershardTxid_items.size();
        //     for(int i = 0; i < item_size; i++){
        //         string txid = intershardTxid_items.at(i).c_str();
        //         struct timeval tv;
        //         gettimeofday(&tv, NULL);
        //         int time_sec = (int)tv.tv_sec;
        //         m_txid_to_endtime->insert(make_pair(txid, time_sec));
        //     }
        //     m_transactionExecuter->add_processedTxNum(item_size);

        //     PLUGIN_LOG(INFO) << LOG_KV("跨片交易已提交1", m_transactionExecuter->get_processedTxNum());

        //     if(m_transactionExecuter->get_processedTxNum() % 1000 == 0) { // 每提交1000笔交易输出一次数目
        //         PLUGIN_LOG(INFO) << LOG_KV("跨片交易已提交2", m_transactionExecuter->get_processedTxNum());
        //         PLUGIN_LOG(INFO) << LOG_KV("目前已提交的交易总数为", m_transactionExecuter->get_processedTxNum());
        //     }

        //     // 暂时先不考虑故障恢复，且由于是确定性执行，只要收到一个参与者的回复就认为本交易已经提交
        //     // auto crossshardtxid = msg_commitResponseToCoordinatorMsg.crossshardtxid();
        //     // auto result = msg_commitResponseToCoordinatorMsg.result();
        //     // auto participantshardid = msg_commitResponseToCoordinatorMsg.participantshardid();

        //     // if(m_receivedCommitResponseNum->count(crossshardtxid) == 0){
        //     //     m_receivedCommitResponseNum->insert(std::make_pair(crossshardtxid, 1));
        //     // }
        //     // else{
        //     //     int receivedResponseNum = m_receivedCommitResponseNum->at(crossshardtxid);
        //     //     receivedResponseNum++;
        //     //     m_receivedCommitResponseNum->at(crossshardtxid) = receivedResponseNum;
        //     //     if(m_transactionExecuter->remoteReadWriteSetNum.count(crossshardtxid) == 0){
        //     //         m_transactionExecuter->remoteReadWriteSetNum.insert(std::make_pair(crossshardtxid, 0));
        //     //     }
        //     //     if(receivedResponseNum == m_transactionExecuter->remoteReadWriteSetNum.at(crossshardtxid)){
        //     //         PLUGIN_LOG(INFO) << LOG_KV("协调者节点", dev::plugin::nodeIdHex)
        //     //                         << LOG_DESC("收齐了所有参与者的大多数Commit消息...");
        //     //     }
        //     // }
        // }

        // 尝试接收来自下层分片的权限转移请求消息
        got_message = m_pluginManager->requestForMasterShardMsg->try_pop(msg_requestForMasterShardMsg);
        if(got_message == true) {
            int sourceshardid = msg_requestForMasterShardMsg.sourceshardid();
            int destinshardid = msg_requestForMasterShardMsg.destinshardid();
            string readwritekeys = msg_requestForMasterShardMsg.readwritekey();
            string requestmessageid = msg_requestForMasterShardMsg.requestmessageid();

            PLUGIN_LOG(INFO) << LOG_DESC("解析RequestForMasterShardMsg")
                             << LOG_KV("sourceshardid", sourceshardid)
                             << LOG_KV("destinshardid", destinshardid)
                             << LOG_KV("readwritekeys", readwritekeys)
                             << LOG_KV("requestmessageid", requestmessageid);

            std::string requestLabel = "0x777888999";
            std::string flag = "|";
            std::string hex_m_testdata_str = requestLabel + flag + std::to_string(sourceshardid) + flag + std::to_string(destinshardid)
                                                + flag + readwritekeys + flag + requestmessageid;
            // PLUGIN_LOG(INFO) << LOG_KV("hex_m_testdata_str", hex_m_testdata_str);
            
            // 计算sourceshardid和destinshardid的最近公共祖先
            // todo
            // ========================= 原来的版本 =========================
            // Hierarchical_Shard::Ptr localshard = m_transactionExecuter->m_hierarchical_structure->shards->at(sourceshardid - 1);
            // Hierarchical_Shard::Ptr destinshard = m_transactionExecuter->m_hierarchical_structure->shards->at(destinshardid - 1);
            // auto shard = localshard->getLCA(localshard, destinshard);
            // PLUGIN_LOG(INFO) << LOG_KV("最近公共祖先分片为", shard->id);
            // ========================= 原来的版本 end=========================
            // ========================= hieraShardTree 版本 =========================
            // int coordinator_id = dev::plugin::hieraShardTree->getLCA(sourceshardid, destinshardid);
            // ========================= hieraShardTree 版本 end=========================
            // 开始向协调者请求转换key的读写权限
            // m_transactionExecuter->crossshardtxid2sourceshardid.insert(make_pair(requestmessageid, shard->id));

            // 转发节点发起交易共识
            if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
                auto testdata_str_bytes = hex_m_testdata_str.c_str();
                int bytelen = strlen(testdata_str_bytes);
                bytes hex_m_testdata;
                for(int i = 0; i < bytelen; i++){
                    hex_m_testdata.push_back((uint8_t)testdata_str_bytes[i]);
                }
                auto tx = make_shared<Transaction>(0, 1000, 0, dev::plugin::depositAddress, hex_m_testdata);
                tx->setNonce(generateRandomValue());
                auto keyPair = KeyPair::create();
                auto sig = dev::crypto::Sign(keyPair, tx->hash(WithoutSignature));
                tx->updateSignature(sig);
                auto rlp = tx->rlp();
                m_rpc_service->sendRawTransaction(dev::consensus::internal_groupId, toHex(rlp)); // 通过调用本地的RPC接口发起新的共识
            }
        }

        // 尝试接收上层分片发送的权限转移PrePrepare消息包
        got_message = m_pluginManager->masterShardPrePrepareMsg->try_pop(msg_masterShardPrePrepareMsg);
        if(got_message == true){
            string sourceshardid = msg_masterShardPrePrepareMsg.sourceshardids();
            string destinshardid = msg_masterShardPrePrepareMsg.destinshardids();
            string readwritekeys = msg_masterShardPrePrepareMsg.readwritekeys();
            string requestmessageid = msg_masterShardPrePrepareMsg.requestmessageid();
            int coordinatorshardid = msg_masterShardPrePrepareMsg.coordinatorshardid();
            // PLUGIN_LOG(INFO) << LOG_DESC("解析MasterShardPrePrepareMsg")
            //                  << LOG_KV("sourceshardid", sourceshardid)
            //                  << LOG_KV("destinshardid", destinshardid)
            //                  << LOG_KV("readwritekeys", readwritekeys)
            //                  << LOG_KV("requestmessageid", requestmessageid)
            //                  << LOG_KV("coordinatorshardid", coordinatorshardid);

            std::string requestLabel = "0x000111222";
            std::string flag = "|";
            std::string hex_m_testdata_str = requestLabel + flag + sourceshardid + flag + destinshardid
                                                + flag + readwritekeys + flag + requestmessageid + flag + std::to_string(coordinatorshardid);

            // 转发节点发起交易共识
            if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
                auto testdata_str_bytes = hex_m_testdata_str.c_str();
                int bytelen = strlen(testdata_str_bytes);
                bytes hex_m_testdata;
                for(int i = 0; i < bytelen; i++) {
                    hex_m_testdata.push_back((uint8_t)testdata_str_bytes[i]);
                }
                // 自己构造交易
                auto tx = make_shared<Transaction>(0, 1000, 0, dev::plugin::depositAddress, hex_m_testdata);
                tx->setNonce(generateRandomValue());
                auto keyPair = KeyPair::create();
                auto sig = dev::crypto::Sign(keyPair, tx->hash(WithoutSignature));
                tx->updateSignature(sig);
                auto rlp = tx->rlp();
                m_rpc_service->sendRawTransaction(dev::consensus::internal_groupId, toHex(rlp)); // 通过调用本地的RPC接口发起新的共识
            }
        }

        // 尝试接收来自下层分片的Prepare请求
        got_message = m_pluginManager->masterShardPrepareMsg->try_pop(msg_masterShardPrepareMsg);
        if(got_message == true){
            string sourceshardid = msg_masterShardPrepareMsg.sourceshardids();
            string destinshardid = msg_masterShardPrepareMsg.destinshardids();
            string readwritekey = msg_masterShardPrepareMsg.readwritekeys();
            string requestmessageid = msg_masterShardPrepareMsg.requestmessageids();
            int coordinatorshardid = msg_masterShardPrepareMsg.coordinatorshardid();

            // PLUGIN_LOG(INFO) << LOG_DESC("解析 MasterShardPrepareMsg")
            //                  << LOG_KV("sourceshardid", sourceshardid)
            //                  << LOG_KV("destinshardid", destinshardid)
            //                  << LOG_KV("readwritekey", readwritekey)
            //                  << LOG_KV("requestmessageid", requestmessageid)
            //                  << LOG_KV("coordinatorshardid", coordinatorshardid);

            if(m_masterRequestVotes->count(requestmessageid) == 0){
                m_masterRequestVotes->insert(make_pair(requestmessageid, 1));
            }
            else {
                int voteNum = m_masterRequestVotes->at(requestmessageid);
                m_masterRequestVotes->at(requestmessageid) = voteNum + 1;
            }
            // 收齐了prepare票，片内发起共识，向参与者发commit消息
            if(m_masterRequestVotes->at(requestmessageid) == 2 && m_sendedStateChangeCommit.count(requestmessageid) == 0){
                m_sendedStateChangeCommit.insert(requestmessageid); // 已经发送过该commit消息
                if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
                    std::string requestLabel = "0x333444555";
                    std::string flag = "|";
                    std::string hex_m_testdata_str = requestLabel + flag + sourceshardid + flag + destinshardid
                                                        + flag + readwritekey + flag + requestmessageid + flag + std::to_string(coordinatorshardid);
                    auto testdata_str_bytes = hex_m_testdata_str.c_str();
                    int bytelen = strlen(testdata_str_bytes);
                    bytes hex_m_testdata;
                    for(int i = 0; i < bytelen; i++) {
                        hex_m_testdata.push_back((uint8_t)testdata_str_bytes[i]);
                    }
                    Transaction tx(0, 1000, 0, dev::plugin::depositAddress, hex_m_testdata);
                    tx.setNonce(tx.nonce() + u256(utcTime()));
                    auto keyPair = KeyPair::create();
                    auto sig = dev::crypto::Sign(keyPair, tx.hash(WithoutSignature));
                    tx.updateSignature(sig);
                    auto rlp = tx.rlp();
                    m_rpc_service->sendRawTransaction(dev::consensus::internal_groupId, toHex(rlp)); // 通过调用本地的RPC接口发起新的共识
                }
            }
        }

        // 尝试接收来自上层分片关于权限转移请求的commit消息
        got_message = m_pluginManager->masterShardCommitMsg->try_pop(msg_masterShardCommitMsg);
        if(got_message == true){
            string sourceshardid = msg_masterShardCommitMsg.sourceshardids();
            string destinshardid = msg_masterShardCommitMsg.destinshardids();
            std::string readwritekey = msg_masterShardCommitMsg.readwritekeys();
            std::string requestmessageid = msg_masterShardCommitMsg.requestmessageids();
            int coordinatorshardid = msg_masterShardCommitMsg.coordinatorshardid();

            // PLUGIN_LOG(INFO) << LOG_DESC("解析 MasterShardCommitMsg")
            //                 << LOG_KV("sourceshardid", sourceshardid)
            //                 << LOG_KV("destinshardid", destinshardid)
            //                 << LOG_KV("readwritekey", readwritekey)
            //                 << LOG_KV("requestmessageid", requestmessageid)
            //                 << LOG_KV("coordinatorshardid", coordinatorshardid);

            std::string requestLabel = "0x666777888";
            std::string flag = "|";
            std::string hex_m_testdata_str = requestLabel + flag + sourceshardid + flag + destinshardid
                                                + flag + readwritekey + flag + requestmessageid + flag + std::to_string(coordinatorshardid);

            // 转发节点发起交易共识(其实可以不共识了，有时间再优化)
            if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex)){
                auto testdata_str_bytes = hex_m_testdata_str.c_str();
                int bytelen = strlen(testdata_str_bytes);
                bytes hex_m_testdata;
                for(int i = 0; i < bytelen; i++) {
                    hex_m_testdata.push_back((uint8_t)testdata_str_bytes[i]);
                }
                auto tx = make_shared<Transaction>(0, 1000, 0, dev::plugin::depositAddress, hex_m_testdata);
                tx->setNonce(generateRandomValue());
                auto keyPair = KeyPair::create();
                auto sig = dev::crypto::Sign(keyPair, tx->hash(WithoutSignature));
                tx->updateSignature(sig);
                auto rlp = tx->rlp();
                m_rpc_service->sendRawTransaction(dev::consensus::internal_groupId, toHex(rlp)); // 通过调用本地的RPC接口发起新的共识
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 模拟跨分片通信延迟
    }
}














void SyncThreadMaster::setAttribute(std::shared_ptr<dev::blockchain::BlockChainInterface> _blockchainManager){
    m_blockchainManager = _blockchainManager;
    m_msgEngine->setAttribute(_blockchainManager);
    m_transactionExecuter->setAttribute(_blockchainManager);
}

void SyncThreadMaster::setAttribute(std::shared_ptr<PluginMsgManager> _pluginManager){
    m_pluginManager = _pluginManager;
    m_msgEngine->setAttribute(_pluginManager);
}

// bool SyncThreadMaster::startP2PThread(){
//     typedef void* (*FUNC)(void*);
//     FUNC receiveWorkerCallback = (FUNC)&SyncThreadMaster::receiveMsgWorker;
//     int ret = pthread_create(&receivethread, NULL, receiveWorkerCallback, this);
//     if (ret != 0){
//         PLUGIN_LOG(INFO) << LOG_DESC("跨片通信P2P消息处理线程启动成功");
//         return false;
//     }
// }

shared_ptr<dev::plugin::TransactionExecuter> SyncThreadMaster::getdeterministExecute(){
    return m_transactionExecuter;
}

void SyncThreadMaster::startExecuteThreads(){
    // 用于输出提交数
    // add by ZhmYe
    std::thread{[this]() {
        while (true) {
            m_transactionExecuter->log_commit_TxNum();
        }
    }}.detach();

    std::thread{[this]()  {
        m_transactionExecuter->processConsensusBlock();
    }}.detach();

    std::thread{[this]()  {
        // m_transactionExecuter->processBlockingTxs(); // 无多点写逻辑
        m_transactionExecuter->processBlockingTxsPlus(); // 附加多点写逻辑
    }}.detach();

    std::thread{[this]()  {
        receiveRemoteMsgWorker(); // 处理来自其他分片消息(模拟50ms延时)
    }}.detach();

    std::thread{[this]()  {
        receiveRemoteCommitMsgWorker(); // 处理来自参与者的commit
    }}.detach();



    // std::thread{[this]()  {
    //     m_transactionExecuter->replyCommitToCoordinator(); // 定期向协调者回复已经提交的跨片交易
    // }}.detach();

    // // 定期 shuffleValue
    // std::thread{[this]()  {
    //     m_transactionExecuter->shuffleStateValue();
    // }}.detach();

    // // 定期 shuffleTx
    // std::thread{[this]()  {
    //     m_transactionExecuter->shuffleBlockedTxs();
    // }}.detach();
}

// void SyncThreadMaster::receiveMsgWorker() {
//     protos::SubCrossShardTx msg_subCrossShardTx;
//     protos::ResponseToForward msg_responMsg;
//     protos::IntraShardTxMsg msg_intraShardTxMsg;

//     bool got_message;
//     while(true){

//         got_message = m_pluginManager->intraShardTxMsg->try_pop(msg_intraShardTxMsg);
//         if(got_message == true)
//         {
//             PLUGIN_LOG(INFO) << LOG_DESC("收到来自协调者的片内交易信息...");
//             long unsigned messageid = msg_intraShardTxMsg.messageid();
//             std::string readwriteset = msg_intraShardTxMsg.readwriteset();
//             int sourceshardid = msg_intraShardTxMsg.sourceshardid();
//             int destinshardid = msg_intraShardTxMsg.destinshardid();
//             std::string signeddata = msg_intraShardTxMsg.signeddata();
//             std::string participants = msg_intraShardTxMsg.participants();
//             std::string crossshardtxid = msg_intraShardTxMsg.crossshardtxid();

//             // long unsigned messageid = msg_subCrossShardTx.messageid();
//             // std::string readwriteset = msg_subCrossShardTx.readwriteset();
//             // int sourceshardid = msg_subCrossShardTx.sourceshardid();
//             // int destinshardid = msg_subCrossShardTx.destinshardid();
//             // std::string signeddata = msg_subCrossShardTx.signeddata();
//             // std::string participants = msg_subCrossShardTx.participants();
//             // std::string crossshardtxid = msg_subCrossShardTx.crossshardtxid();

//             std::vector<std::string> readwritesets;
//             boost::split(readwritesets, readwriteset, boost::is_any_of("_"), boost::token_compress_on);
//             int readwritesetnum = readwritesets.size();

//             PLUGIN_LOG(INFO) << LOG_DESC("交易解析完毕")
//                                 << LOG_KV("messageid", messageid)
//                                 << LOG_KV("readset", readwriteset)
//                                 << LOG_KV("sourceShardId", sourceshardid)
//                                 << LOG_KV("destinshardid", destinshardid)
//                                 << LOG_KV("signeddata", signeddata)
//                                 << LOG_KV("participants", participants)
//                                 << LOG_KV("crossshardtxid", crossshardtxid);

//             // Transaction::Ptr tx = std::make_shared<Transaction>(jsToBytes(signeddata, OnFailed::Throw), CheckTransaction::Everything);
//             // dev::rpc::subcrosstxhash.push_back(tx->hash());
//             // dev::rpc::txhash2sourceshardid.insert(std::make_pair(tx->hash(), sourceshardid));
//             // dev::rpc::txhash2messageid.insert(std::make_pair(tx->hash(), messageid));
//             // dev::rpc::txhash2readwriteset.insert(std::make_pair(tx->hash(), readwriteset));

//             // dev::rpc::transaction_info _transaction_info{1, sourceshardid, destinshardid, messageid, readwritesetnum, tx->hash(), crossshardtxid, readwriteset, participants};
//             // dev::rpc::corsstxhash2transaction_info.insert(std::make_pair(tx->hash(), _transaction_info));

//             // PLUGIN_LOG(INFO) << LOG_DESC("哈希值为") << LOG_KV("tx->hash()", tx->hash())
//             //                  << LOG_KV("dev::rpc::corsstxhash2transaction_info.count(tx_hash)", dev::rpc::corsstxhash2transaction_info.count(tx->hash()));

//             // 转发节点发起交易共识
//             if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex))
//             {
//                 m_rpc_service->sendRawTransaction(destinshardid, signeddata); // 通过调用本地的RPC接口发起新的共识
//                 PLUGIN_LOG(INFO) << LOG_DESC("交易发送完毕...");
//             }
//         }

//         if(got_message == false)
//         {
//             std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 单机模拟时候防止CPU被持续占用
//         }

//         // 参与者轮循从队列中获取消息
//         // bool got_message = m_pluginManager->batchdistxs->try_pop(msg_batchsubCrossShardTxs);
//         // if(got_message == true)
//         // {
//         //     PLUGIN_LOG(INFO) << LOG_DESC("收到来自协调者的批量跨片交易消息(BatchDistributedTxMsg)...");

//         //         // 将BatchDistributedTxMsg中的内容组装到一个交易中
//         //         long unsigned batchId = msg_batchsubCrossShardTxs.id();
//         //         string txcontents = msg_batchsubCrossShardTxs.txcontents();
//         //         string sendedreadwriteset = msg_batchsubCrossShardTxs.tosendreadwriteset();
//         //         string intrashard_txcontents = msg_batchsubCrossShardTxs.intrashard_txcontents(); // 转换成的片内交易


//         //         // PLUGIN_LOG(INFO) << LOG_KV("batchId", batchId)
//         //         //                  << LOG_KV("txcontents", txcontents)
//         //         //                  << LOG_KV("sendedreadwriteset", sendedreadwriteset)
//         //         //                  << LOG_KV("intrashard_txcontents", intrashard_txcontents);

//         //         /**
//         //          * intrashard_txcontents 样例:
//         //          * 0x222333444|rlp1_rlp2|key1_key2
//         //         */

//         //         // 转发节点发起交易共识
//         //         if(hieraShardTree->is_forward_node(dev::consensus::internal_groupId, dev::plugin::nodeIdHex))
//         //         {
//         //             // 构造包含批量子交易的单笔交易, 将重组片内交易放在跨片子交易后面
//         //             string requestLabel = "0x999000111";
//         //             string hex_m_testdata_str = requestLabel + "|" + std::to_string(batchId) + "#" + txcontents + "#" + intrashard_txcontents; // 跨片子交易和重组片内交易之间用"#"号隔开
//         //             auto testdata_str_bytes = hex_m_testdata_str.c_str();
//         //             int bytelen = strlen(testdata_str_bytes);

//         //             bytes hex_m_testdata;
//         //             hex_m_testdata.clear();
//         //             for(int i = 0; i < bytelen; i++){
//         //                 hex_m_testdata.push_back((uint8_t)testdata_str_bytes[i]);
//         //             }

//         //             std::shared_ptr<Transaction> subintershard_txs = std::make_shared<Transaction>(0, 1000, 0, dev::plugin::depositAddress, hex_m_testdata);
//         //             subintershard_txs->setNonce(subintershard_txs->nonce() + u256(utcTime()));
//         //             auto keyPair = KeyPair::create();
//         //             auto sig = dev::crypto::Sign(keyPair, subintershard_txs->hash(WithoutSignature));
//         //             subintershard_txs->updateSignature(sig);
//         //             submitTransactionToTxPool(subintershard_txs);
//         //             PLUGIN_LOG(INFO) << LOG_DESC("转发节点发起交易共识...");
//         //         }
//         // }

//         // got_message = m_pluginManager->distxs->try_pop(msg_subCrossShardTx);
//         // if(got_message == true) {
//         //     PLUGIN_LOG(INFO) << LOG_DESC("收到来自协调者的跨片交易消息(DistributedTxPacket)...");
//         // }

//         // 读写集接收功能委托新的线程处理，线程每50ms检查一次是否有新的读写集消息进来
//         // got_message = m_pluginManager->RWSetMsg->try_pop(msg_csTxRWset);
//         // if(got_message == true)
//         // {
//         //     PLUGIN_LOG(INFO) << LOG_DESC("收到来自其他分片的读写集信息...");

//         //     // 对收到的读写集消息进行处理
//         //     // auto batchId = msg_csTxRWset.crossshardtxid();
//         //     // auto accessedNum = msg_csTxRWset.accessnum();
//         //     string rwKeys = msg_csTxRWset.readwritekey();
//         //     string rwValues = msg_csTxRWset.value();
//         //     string rwContentionRates = msg_csTxRWset.contentionrate();
//         //     string sourceshardid = to_string(msg_csTxRWset.sourceshardid());

//         //     // PLUGIN_LOG(INFO) << LOG_DESC("读写集解析完毕")
//         //     //                  << LOG_KV("rwKeys", rwKeys)
//         //     //                  << LOG_KV("rwValues", rwValues)
//         //     //                  << LOG_KV("rwContentionRates", rwContentionRates)
//         //     //                  << LOG_KV("sourceshardid", sourceshardid);

//         //     // 解析收到的key
//         //     std::vector<std::string> keyItems;
//         //     std::vector<std::string> valueItems;
//         //     std::vector<std::string> contentionItems;
//         //     boost::split(keyItems, rwKeys, boost::is_any_of("|"), boost::token_compress_on);
//         //     boost::split(valueItems, rwValues, boost::is_any_of("|"), boost::token_compress_on);
//         //     boost::split(contentionItems, rwContentionRates, boost::is_any_of("|"), boost::token_compress_on);

//         //     // 缓存收到的读写集信息，存在 receivedTxRWset 和 receivedContentionRates 中
//         //     size_t key_size = keyItems.size();
//         //     for(size_t i = 0; i < key_size; i++)
//         //     {
//         //         string receivedKey = keyItems.at(i);
//         //         string receivedValue = valueItems.at(i);
//         //         string contentionRate = contentionItems.at(i);

//         //         // contentionRate的key将前面的epochId和batchId去掉, 只需要是original key
//         //         std::vector<std::string> items;
//         //         boost::split(items, receivedKey, boost::is_any_of("_"), boost::token_compress_on);
//         //         // string originalKey = items.at(2);  // 因为为了避免处理不一致问题，epochID_batchId_keyAddress 临时改为了keyAddress！
//         //         string originalKey = items.at(0);

//         //         // 先将收到的对方状态的争用率进行保存
//         //         PLUGIN_LOG(INFO) << LOG_DESC("读写集解析完毕")
//         //                          << LOG_KV("originalKey", originalKey)
//         //                          << LOG_KV("contentionRate", contentionRate)
//         //                          << LOG_KV("sourceshardid", sourceshardid);

//         //         // m_transactionExecuter->m_LRU_StatesInfo->updateIntraTxAccessedremotekey(originalKey, contentionRate, sourceshardid);
//         //         m_transactionExecuter->m_LRU_StatesInfo->updateRemoteKeyContention(originalKey, contentionRate, sourceshardid);

//         //         // if(m_transactionExecuter->m_readWriteSetManager->receivedTxRWset->count(receivedKey) == 0) {
//         //         //     m_transactionExecuter->m_readWriteSetManager->receivedTxRWset->insert(std::make_pair(receivedKey, receivedValue));
//         //         // }
//         //         m_transactionExecuter->m_readWriteSetManager->insertReceivedRwset(originalKey);
//         //     }
//         // }
//     }
// }

// void SyncThreadMaster::dowork(dev::sync::SyncPacketType const& packettype, byte const& data)
// {
    
// }

// void SyncThreadMaster::dowork(dev::sync::SyncPacketType const& packettype, byte const& data, dev::network::NodeID const& destnodeId)
// {
    
// }

// void SyncThreadMaster::sendMessage(bytes const& _blockRLP, dev::sync::SyncPacketType const& packetreadytype)
// {

// }

// void SyncThreadMaster::sendMessage(bytes const& _blockRLP, dev::sync::SyncPacketType const& packettype, dev::network::NodeID const& destnodeId)
// {

// }

// void SyncThreadMaster::start(byte const &pt, byte const& data)
// {
//     SyncPacketType packettype;
//     switch (pt)
//     {
//         case 0x06:
//             packettype = ParamRequestPacket;
//             break;
//         case 0x07:
//             packettype = ParamResponsePacket;
//             break;
//         case 0x08:
//             packettype = CheckpointPacket;
//             break;
//         case 0x09:
//             packettype = ReadSetPacket;
//             break;
//         case 0x10:
//             packettype = BlockForExecutePacket;
//             break;
//         case 0x11:
//             packettype = BlockForStoragePacket;
//             break;
//         case 0x12:
//             packettype = CommitStatePacket;
//             break;
//         default:
//             std::cout << "unknown byte type!" << std::endl;
//             break;
//     }
//     dowork(packettype, data);
// }

// void SyncThreadMaster::start(byte const &pt, byte const& data, dev::network::NodeID const& destnodeId)
// {
//     SyncPacketType packettype;
//     switch (pt)
//     {
//         case 0x06:
//             packettype = ParamRequestPacket;
//             break;
//         case 0x07:
//             packettype = ParamResponsePacket;
//             break;
//         case 0x08:
//             packettype = CheckpointPacket;
//             break;
//         case 0x09:
//             packettype = ReadSetPacket;
//             break;
//         case 0x10:
//             packettype = BlockForExecutePacket;
//             break;
//         case 0x11:
//             packettype = BlockForStoragePacket;
//             break;
//         case 0x12:
//             packettype = CommitStatePacket;
//             break;
//         default:
//             std::cout << "unknown byte type!" << std::endl;
//             break;
//     }
//     dowork(packettype, data, destnodeId);
// }

// void SyncThreadMaster::cacheCrossShardTx(std::string _rlpstr, protos::SubCrossShardTx _subcrossshardtx)
// {
//     std::lock_guard<std::mutex> lock(x_map_Mutex);
//     m_cacheCrossShardTxMap.insert(std::make_pair( _rlpstr, _subcrossshardtx ));
// }

// void SyncThreadMaster::submitTransactionToTxPool(Transaction::Ptr tx)
// {
//     auto txPool = m_ledgerManager->txPool(dev::consensus::internal_groupId);
//     txPool->submitTransactions(tx);
// }