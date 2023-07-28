#pragma once

#include <libinitializer/Initializer.h>
#include <libinitializer/P2PInitializer.h>
using namespace dev;
using namespace dev::initializer;

void putGroupPubKeyIntoshardNodeId(boost::property_tree::ptree const& _pt){
    size_t index = 0;
    for (auto it : _pt.get_child("group")){
        if (it.first.find("groups.") == 0){
            std::vector<std::string> s;
            try{
                boost::split(s, it.second.data(), boost::is_any_of(":"), boost::token_compress_on);
                // 对分片中的所有节点id进行遍历，加入到列表中
                size_t s_size = s.size();
                for(size_t i = 0; i < s_size - 1; i++){
                    h512 node;
                    node = h512(s[i]);
                    dev::consensus::shardNodeId.push_back(node);
                    

                    if(index % 4 == 0){
                        dev::consensus::forwardNodeId.push_back(node);
                    }
                    index++;
                }
            }
            catch (std::exception& e){
                exit(1);
            }}
    }
}

void putGroupPubKeyIntoService(std::shared_ptr<Service> service, boost::property_tree::ptree const& _pt) {
    std::map<GROUP_ID, h512s> groupID2NodeList;
    h512s nodelist;
    int groupid;
    size_t index = 0;
    for (auto it : _pt.get_child("group")){
        if (it.first.find("groups.") == 0){
            std::vector<std::string> s;
            try{
                boost::split(s, it.second.data(), boost::is_any_of(":"), boost::token_compress_on);
                // 对分片中的所有节点id进行遍历，加入到列表中
                int s_size = s.size();
                for(int i = 0; i < s_size - 1; i++){
                    h512 node;
                    node = h512(s[i]);
                    nodelist.push_back(node);
                }
                // groupid = (int)((s[s_size - 1])[0] - '0');
                // groupid = (int)((s[s_size - 1]));
                groupid = atoi(s.at(s_size - 1).c_str());
            }
            catch (std::exception& e){
                exit(1);
            }
        }
    }
    groupID2NodeList.insert(std::make_pair(groupid, nodelist)); // 都是同一个groupid，所以插入一次就好了
    std::cout << "groupID2NodeList " << groupid << " " << groupID2NodeList[groupid];
    service->setGroupID2NodeList(groupID2NodeList);
}

class GroupP2PService
{
public:
    GroupP2PService(std::string const& _path)
    {
        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(_path, pt);
        m_secureInitializer = std::make_shared<SecureInitializer>();
        m_secureInitializer->initConfig(pt);
        m_p2pInitializer = std::make_shared<P2PInitializer>();
        m_p2pInitializer->setSSLContext(m_secureInitializer->SSLContext(SecureInitializer::Usage::ForP2P));
        m_p2pInitializer->setKeyPair(m_secureInitializer->keyPair());
        m_p2pInitializer->initConfig(pt);
    }
    P2PInitializer::Ptr p2pInitializer() { return m_p2pInitializer; }
    ~GroupP2PService()
    {
        if (m_p2pInitializer)
        {
            m_p2pInitializer->stop();
        }
    }

private:
    P2PInitializer::Ptr m_p2pInitializer;
    SecureInitializer::Ptr m_secureInitializer;
};
// todo
//  这里加载了当前节点所在分片的子分片和最近祖先分片
void loadHieraInfo(boost::property_tree::ptree& pt, string* upper_groupIds, string* lower_groupIds)
{
    int shard_id = dev::consensus::internal_groupId; // 当前节点所在的分片id
    pair<string, string> info = dev::plugin::hieraShardTree->get_shard_info_by_internal_groupId(shard_id);
    *upper_groupIds = info.first;
    *lower_groupIds = info.second;
    // PLUGIN_LOG(INFO) << LOG_DESC("start loadHieraInfo...");
    // // std::string jsonrpc_listen_ip = pt.get<std::string>("rpc.jsonrpc_listen_ip");
    // // std::string jsonrpc_listen_port = pt.get<std::string>("rpc.jsonrpc_listen_port");
    // nearest_upper_groupId = pt.get<std::string>("layer.nearest_upper_groupId");
    // nearest_lower_groupId = pt.get<std::string>("layer.nearest_lower_groupId");

    // // PLUGIN_LOG(INFO) << LOG_KV("jsonrpc_listen_ip", jsonrpc_listen_ip)<< LOG_KV("jsonrpc_listen_port", jsonrpc_listen_port);
    // if(nearest_upper_groupId != "N/A")
    // {
    //     PLUGIN_LOG(INFO) << LOG_KV("nearest_upper_groupId", nearest_upper_groupId);
    //     std::cout << "nearest_upper_groupId = " << nearest_upper_groupId << std::endl;
    // }
    // else
    // {
    //     std::cout << "it's a root group" << std::endl;
    //     PLUGIN_LOG(INFO) << LOG_DESC("it's a root group");
    // }

    // if(nearest_lower_groupId != "N/A")
    // {
    //     PLUGIN_LOG(INFO) << LOG_KV("nearest_lower_groupId", nearest_lower_groupId);
    //     std::cout << "nearest_lower_groupId = " << nearest_lower_groupId << std::endl;
    // }
    // else
    // {
    //     PLUGIN_LOG(INFO) << LOG_DESC("it's a leaf group");
    //     std::cout << "it's a leaf group" << std::endl;
    // }
}

void countandshowall(std::vector<std::pair<std::string, uint64_t>> send,std::vector<std::pair<std::string, uint64_t>> commit){
    unordered_map<std::string, uint64_t> countmap_send;
    unordered_map<std::string, uint64_t> countmap_res;
    for(auto ele:send){
        countmap_send[ele.first]=ele.second;
    }
    for(auto ele:commit){
        if(countmap_send.find(ele.first)!=countmap_send.end()){
            countmap_res[ele.first]=ele.second-countmap_send[ele.first];
        }
    }
    unsigned long ress=0;
    for(auto ele:countmap_res){
        ress+=ele.second;
    }
    LOG(INFO)<<LOG_DESC("统计信息")<<LOG_KV("总交易数量", countmap_res.size())
        <<LOG_KV("平均延时", 1.0*ress/countmap_res.size()*0.001);
}


// //  load workload
// void load_WorkLoad(int intra_shardTxNumber, int inter_shardTxNumber, int cross_layerTxNumber,std::shared_ptr<dev::rpc::Rpc> rpcService, std::shared_ptr<dev::ledger::LedgerManager> ledgerManager, int threadId){
//     string intra_workload_filename = "shard"+ to_string(internal_groupId) +"_intrashard_workload_100w.json";
//     string inter_workload_filename = "shard"+ to_string(internal_groupId) +"_intershard_workload_10w.json";
//     string cross_workload_filename = "shard"+ to_string(internal_groupId) +"_crosslayer_workload_10w.json";
//     injectTxs _inject(rpcService, internal_groupId, ledgerManager);
//     _inject.injectionTransactions(intra_workload_filename, inter_workload_filename, cross_workload_filename, intra_shardTxNumber, inter_shardTxNumber, cross_layerTxNumber, threadId);
// }


















// //  load locality workload
// void load_locality_WorkLoad(int intra_shardTxNumber, int inter_shardTxNumber, int cross_layerTxNumber,std::shared_ptr<dev::rpc::Rpc> rpcService, std::shared_ptr<dev::ledger::LedgerManager> ledgerManager){
//     string intra_workload_filename = "shard"+ to_string(internal_groupId) +"_locality_intrashard_workload_100w.json";
//     string inter_workload_filename = "shard"+ to_string(internal_groupId) +"_locality_intershard_workload_10w.json";
//     string cross_workload_filename = "shard"+ to_string(internal_groupId) +"_crosslayer_workload_10w.json";
//     injectTxs _inject(rpcService, internal_groupId, ledgerManager);

//     PLUGIN_LOG(INFO) << LOG_KV("intra_workload_filename", intra_workload_filename)
//               << LOG_KV("inter_workload_filename", inter_workload_filename)
//               << LOG_KV("cross_workload_filename", cross_workload_filename);

//     _inject.injectionTransactions(intra_workload_filename, inter_workload_filename, cross_workload_filename, intra_shardTxNumber, inter_shardTxNumber, cross_layerTxNumber, 1);
// }


// generate_statekeys_distribution.ini
void generate_statekeys_distribution_config(){

    /**
     * state0 - state 999, 1
     * state1000 - state1999, 2
    */

    string filename = "statekeys_distribution.ini";

    string res = "";
    for(int j = 1; j <= 2; j++){
        for(int i = 0; i < 1000; i++){
            string state = "state" + to_string(i + (j-1)*1000);
            string line = state + "," + to_string(j) + "\n";
            res = res + line;
        }
    }

    ofstream out;
    out.open(filename, ios::in|ios::out|ios::app);
    if (out.is_open()) {
        out << res;
        out.close();
    }
}