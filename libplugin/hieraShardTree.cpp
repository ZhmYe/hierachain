//
// Created by ZhmYe on 2023/5/26.
//
#include "hieraShardTree.h"
#include "libdevcore/Log.h"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <cstdlib>
#include <stdlib.h>
#include <boost/algorithm/string.hpp>
#include <string>
#include <utility>
using namespace dev;
using namespace dev::plugin;
void HieraShardTree::buildFromJson(string json_file_name) {
    ifstream infile(json_file_name, ios::binary);
    Json::Reader reader;
    Json::Value result;

    // 加载交易
    if(reader.parse(infile, result)) {
        // json中的格式为string:string
        // 首先单独一条"root":"{root_id}"，来知道根节点的id是多少
        string root_id = result["root"].asString();
        root->internal_groupId = std::atoi(root_id.c_str());
        // 剩下tring:string的结果，前面的string是nodeid，后面的string形如nodeid,nodeid,nodeid
        // string nodeIDsStr = result[root_id]["nodeIDs"].asString();
        // vector<string> nodeIDs;
        // boost::split(nodeIDs, nodeIDsStr, boost::is_any_of(","), boost::token_compress_on);
        // for(size_t i = 0; i < nodeIDs.size() - 1; i++){
        //     h512 nodeID = h512(nodeIDs[i]);
        //     root->nodeIDs.push_back(nodeID);
        //     // dev::consensus::shardNodeId.push_back(node);
        //     // if(index % 4 == 0){
        //     //     dev::consensus::forwardNodeId.push_back(node);
        //     // }
        //     // index++;
        // }
        
        string subShardsStr = result[root_id].asString(); // 得到的根节点下面的子节点
        vector<string> items;
        boost::split(items, subShardsStr, boost::is_any_of(","), boost::token_compress_on); // 对分片中的所有节点id进行遍历, 加入到列表中
        // 递归建树
        recursionInsertChild(result, root, items);
        // 建树完成后，dfs统计分片个数并保存在类中,以方便随时获取
        hiera_shard_number = 1 + dfs(root);
    }
    infile.close();
}
void HieraShardTree::putGroupPubKeyIntoService(std::shared_ptr<Service> service, boost::property_tree::ptree const& _pt) {
    int group_index = 1;
    std::map<GROUP_ID, h512s> groupID2NodeList;
    h512s node_list;
    int groupId;
    for (auto it : _pt.get_child("group")){
        if (it.first.find("groups.") == 0){
            cout << "read NodeIds in shard " << group_index << "..." << endl;
            HieraShardNode* shard = get_shard_by_internal_groupId(group_index);
            std::vector<std::string> s;
            try{
                boost::split(s, it.second.data(), boost::is_any_of(":"), boost::token_compress_on);
                // 对分片中的所有节点id进行遍历，加入到列表中
                size_t s_size = s.size();
                for(size_t i = 0; i < s_size - 1; i++){
                    cout << s[i] << " ";
                    shard->nodeIDs.push_back(h512(s[i]));
                    node_list.push_back(h512(s[i]));
                }
                groupId =  atoi(s.at(s_size - 1).c_str());
            }
            catch (std::exception& e){
                cout << "error" << endl;
                exit(1);
            }
            group_index += 1;    
            cout << endl;
        }
    }
    groupID2NodeList.insert(std::make_pair(groupId, node_list)); // 都是同一个groupid，所以插入一次就好了
    service->setGroupID2NodeList(groupID2NodeList);
    cout << "putGroupPubKeyIntoService Success..." << endl;
}
// 递归建树
// 只要json解析结果中有后续子节点就一直递归下去，直到没有为止
void HieraShardTree::recursionInsertChild(Json::Value result, HieraShardNode* shard, vector<string> subShards) {
    int subShardSize = subShards.size();
    if (subShardSize == 0) {
        return;
    }
    for (int i = 0; i < subShardSize; i++) {
        HieraShardNode* subShard = new HieraShardNode(std::atoi(subShards.at(i).c_str()));
        subShard->coordinator = shard;
        // if (std::atoi(subShards.at(i).c_str()) < 1) {
        //     continue;
        // }   
        // string nodeIDsStr = result[subShard->internal_groupId]["nodeID"].asString();
        // vector<string> nodeIDs;
        // boost::split(nodeIDs, nodeIDsStr, boost::is_any_of(","), boost::token_compress_on);
        // for(size_t i = 0; i < nodeIDs.size() - 1; i++){
        //     h512 nodeID = h512(nodeIDs[i]);
        //     subShard->nodeIDs.push_back(nodeID);
        // }
        insertChild(subShard, shard);
        // json里就不用放叶子节点的child了
        if (!result[to_string(subShard->internal_groupId)].isString()) {
            continue;
        }
        string subShardsStr = result[to_string(subShard->internal_groupId)].asString();
        vector<string> items;
        boost::split(items, subShardsStr, boost::is_any_of(","), boost::token_compress_on);
        recursionInsertChild(result, subShard, items);
    }
}
void HieraShardTree::insertChild(HieraShardNode* shard, HieraShardNode *coordinator) {
    coordinator->children.push_back(shard);
}
void HieraShardTree::insertChildren(vector<HieraShardNode*> shards, HieraShardNode *coordinator) {
    for (int i = 0; i < shards.size(); i++) {
        insertChild(shards[i], coordinator);
    }
}
h512 HieraShardTree::get_forward_nodeId(int internal_groupId) {
    HieraShardNode* shard = get_shard_by_internal_groupId(internal_groupId);
    return shard->nodeIDs.at(0);
}
h512s HieraShardTree::get_nodeIDs(int internal_group_id) {
    HieraShardNode* shard = get_shard_by_internal_groupId(internal_group_id);
    return shard->nodeIDs;
}
HieraShardNode* HieraShardTree::get_shard_by_internal_groupId(int internal_groupId) {
    if (root->internal_groupId == internal_groupId) return root;
    pair<bool, HieraShardNode*> search_result = get_shard_by_internal_groupId(root, internal_groupId);
    return search_result.second;
}
pair<bool, HieraShardNode*> HieraShardTree::get_shard_by_internal_groupId(HieraShardNode* shard, int internal_groupId) {
  // 这里不是查找树，存在多条路径
    HieraShardNode* result = nullptr;
    bool success = false;
    for (int i = 0; i < shard->children.size(); i++) {
        HieraShardNode* subShard = shard->children.at(i);
        if (subShard->internal_groupId == internal_groupId) {
          result = subShard;
          success = true;
          return make_pair(success, result);
      }
      pair <bool, HieraShardNode*> tmpResult = get_shard_by_internal_groupId(shard->children.at(i), internal_groupId);
      if (tmpResult.first) {
          return tmpResult;
      }
    }
    return make_pair(success, result);
}
// 用于判断是否有片内交易
// 只需判断是否有子分片即可
bool HieraShardTree::is_leaf(HieraShardNode* shard) {
    return (shard->children.size() == 0) ? true : false;
};
// 判断是否有局部跨片交易
// 只要有子节点，就需要处理局部跨片交易
bool HieraShardTree::is_leaf(int internal_groupId) {
    return is_leaf(get_shard_by_internal_groupId(internal_groupId));
}
bool HieraShardTree::is_inter(int internal_groupId) {
    return is_inter(get_shard_by_internal_groupId(internal_groupId));
}
bool HieraShardTree::is_cross_layer(int internal_groupId) {
    return is_cross_layer(get_shard_by_internal_groupId(internal_groupId));
}
bool HieraShardTree::is_inter(HieraShardNode* shard) {
    return !is_leaf(shard);
}
// 判断是否有跨层跨片交易
// 只要存在一个子节点还有子节点，就需要处理跨层跨片交易
// 判断子节点是否为叶节点，只要有一个不是，返回true
bool HieraShardTree::is_cross_layer(HieraShardNode* shard) {
    for (int i = 0; i < shard->children.size(); i++) {
        if (!is_leaf(shard->children.at(i))) {
            return true;
        }
    }
    return false;
}
// 这里扩展了一下，在遍历的过程中判断一下某一个节点是否有跨层、局部交易
// 然后保存了三个成员变量用来记录一共有多少要处理分片、跨层、局部交易的分片，后面分配交易数量时用到
int HieraShardTree::dfs(HieraShardNode* shard) {
    if (is_leaf(shard)) {
        intra_shard_number += 1;
    }
    if (is_inter(shard)) {
        inter_shard_number += 1;
    }
    if (is_cross_layer(shard)) {
        cross_layer_shard_number += 1;
    }
    if (shard->children.size() == 0) {
        return 0;
    }
    int subShardNumber = shard->children.size();
    int number = 0;
    for (int i = 0; i < subShardNumber; i++) {
        number += 1;
        number += dfs(shard->children.at(i));
    }
    return number;

}
// 获取树中分片总数
int HieraShardTree::get_hiera_shard_number() {
    return hiera_shard_number;
}
int HieraShardTree::get_intra_shard_number() {
    return intra_shard_number;
}
int HieraShardTree::get_inter_shard_number() {
    return inter_shard_number;
}
int HieraShardTree::get_cross_layer_shard_number() {
    return cross_layer_shard_number;
}
// 获取两个分片的最近公共祖先
// todo
//  1.扩展到n个分片，vector?
//  2.return的结果是HieraShardNode/int?
int HieraShardTree::getLCA(int shard_1_groupId, int shard_2_groupId) {
    // HieraShardNode* shard_1 = get_shard_by_internal_groupId(shard_1_groupId);
    // HieraShardNode* shard_2 = get_shard_by_internal_groupId(shard_2_groupId);
    string shard_1_upper = get_shard_info_by_internal_groupId(shard_1_groupId).first;
    string shard_2_upper = get_shard_info_by_internal_groupId(shard_2_groupId).first;
    vector<string> shard_1_upper_ids;
    boost::split(shard_1_upper_ids, shard_1_upper, boost::is_any_of(","), boost::token_compress_on);
    vector<string> shard_2_upper_ids;
    boost::split(shard_2_upper_ids, shard_1_upper, boost::is_any_of(","), boost::token_compress_on);
    int shard_1_depth = shard_1_upper_ids.size();
    int shard_2_depth = shard_2_upper_ids.size();
    int shard_1_index = 0;
    int shard_2_index = 0;
    if (is_leaf(shard_1_groupId)) {
        shard_1_depth += 1;
    }
    if (is_leaf(shard_2_groupId)) {
        shard_2_depth += 1;
    }
    if (shard_1_depth >= shard_2_depth) {
        shard_2_index += (shard_1_depth - shard_2_depth);
    } else {
        shard_1_index += (shard_2_depth - shard_1_depth);
    }
    while (true) {
        if (shard_1_upper_ids.at(shard_1_index) == shard_2_upper_ids.at(shard_2_index)) {
            return atoi(shard_1_upper_ids.at(shard_1_index).c_str());
        }
        if (shard_1_index >= shard_1_upper_ids.size() || shard_2_index >= shard_2_upper_ids.size()) {
            return -1;
        }
        shard_1_index += 1;
        shard_2_index += 1;
    }
    // return getLCA(shard_1, shard_2);
}
int HieraShardTree::getLCA(HieraShardNode* shard_1, HieraShardNode* shard_2) {
    return getLCA(shard_1->internal_groupId, shard_2->internal_groupId);
    // HieraShardNode*  coordinator_1 = shard_1->coordinator;
    // HieraShardNode*  coordinator_2 = shard_2->coordinator;
    // while (true) {
    //     if (coordinator_1 == nullptr || coordinator_2 == nullptr) {
    //         return -1;
    //     }
    //     if (coordinator_1 == coordinator_2) {
    //         return coordinator_1->internal_groupId;
    //     }
    //     coordinator_1 = coordinator_1->coordinator;
    //     coordinator_2 = coordinator_2->coordinator;
    // }
}
pair<string, string> HieraShardTree::get_shard_info_by_internal_groupId(int internal_groupId) {
    HieraShardNode* shard = get_shard_by_internal_groupId(internal_groupId);
    string coordinator = "";
    string subShards = std::to_string(internal_groupId) + ","; // lower_groupIds包括自己，用于TransactionExecuter.cpp
    // 如果没有祖先节点，协调者就是自己
    if (shard -> coordinator == nullptr) {
        coordinator = to_string(internal_groupId);
    } else {
        HieraShardNode* tmp = shard->coordinator;
        if (is_inter(internal_groupId)) {
            coordinator += to_string(internal_groupId);
            coordinator += ",";
        }
        while (tmp->coordinator != nullptr) { 
            tmp = tmp->coordinator;
            for (int i = 0; i < tmp->children.size();i++) {
                coordinator += to_string(tmp->children.at(i)->internal_groupId);
                coordinator += ",";
            }
        }
        coordinator += to_string(tmp->internal_groupId);
    }
    // coordinator = (nullptr == shard->coordinator) ? to_string(internal_groupId) : to_string(shard->coordinator->internal_groupId);
    if (shard->children.size() == 0) {
        // 是叶子节点不需要包含自己
        subShards = "N/A";
    } else {
        vector<int> allChild;
        get_all_child(shard, &allChild);
        int subShardNumber = allChild.size(); 
        for (int i = 0; i < subShardNumber; i++) {
            subShards += std::to_string(allChild.at(i));
            if (i < subShardNumber - 1) {
                subShards += ",";
            }
        }
        // int subShardNumber = shard->children.size();
        // for (int i = 0; i < subShardNumber; i++) {
        //     subShards += std::to_string(shard->children.at(i)->internal_groupId);
        //     if (i < subShardNumber - 1) {
        //         subShards += ",";
        //     }
        // }
    }
    return make_pair(coordinator, subShards);
}
// 从某个子节点的子树中拿出一个节点，且该节点不能是某个子节点
// 统计出符合条件的节点个数，等概率随机选择一个
void HieraShardTree::get_all_child(HieraShardNode* shard, vector<int>* ids) {
    if (shard->children.size() == 0) return;
    for (int i = 0; i < shard->children.size(); i++) {
        HieraShardNode* subShard = shard->children.at(i);
        ids->push_back(subShard->internal_groupId);
        get_all_child(subShard, ids);
    }
}
// int HieraShardTree::get_cross_layer_child(int internal_groupId) {
//     HieraShardNode* shard = get_shard_by_internal_groupId(internal_groupId);
//     vector<int> all_shard_ids;
//     for (int i = 0; i < shard->children.size(); i++) {
//         // 有子树说明有局部跨片交易
//         if (is_inter(shard->children.at(i))) {
//             get_all_child(shard->children.at(i), &all_shard_ids);
//         }
//     }
//     return all_shard_ids.at(rand() % all_shard_ids.size());
// }
// todo
// 获取跨层跨片交易的两个分片id
// 跨层跨片交易，两个分片在不同的子树
// case1:其中一个分片就是协调者本身
// 此时，只要从协调者的子节点的任意一棵子树中选择一个分片作为第二个分片即可
// case2:两个分片都是协调者的后继节点
// 要保证两个分片在不同的子树上，不然协调者就是它们所在子树的根节点
// 任意选择两个子树，然后从它们的子树中任意选出一个节点
// 默认第一个子树可以包含根节点，第二个不可以（两个都是根节点是局部）
pair<int, int> HieraShardTree::get_cross_layer_childs(int internal_groupId) {
    HieraShardNode* shard = get_shard_by_internal_groupId(internal_groupId);

    // find available sub shards
    vector<HieraShardNode*> cross_layer_childs, leaf_childs;
    for (auto child: shard->children) {
        if (is_inter(child)) cross_layer_childs.push_back(child);
        else leaf_childs.push_back(child);
    }

    // choose double sub shard 1 (case1: 第一个节点可以包含coordinator, 且case2: 第一个节点可以包含子树根节点)
    int dblsubShard_1 = internal_groupId;
    int subShard_1_groupId = internal_groupId;
    if (cross_layer_childs.size() > 1) {
        int subTree_index_1 = rand() % shard->children.size();
        vector<int> all_child_1;
        HieraShardNode* subShard_1 = shard->children.at(subTree_index_1);
        subShard_1_groupId = subShard_1->internal_groupId;
        get_all_child(subShard_1, &all_child_1);

        // todo: 修改coordinator internal_groupId参与的概率
        all_child_1.push_back(internal_groupId);

        // todo: 修改subshard internal_groupId参与的概率
        all_child_1.push_back(subShard_1->internal_groupId);
        dblsubShard_1 = all_child_1.at(rand() % all_child_1.size());
    } else {
        if (rand() % 2 == 0) dblsubShard_1 = leaf_childs.at(rand() % shard->children.size())->internal_groupId;
    }

    // choose double sub shard 2 (case2: 第二个节点必须为子树的子节点)
    vector<int> all_child_2;
    HieraShardNode* subShard_2 = cross_layer_childs.at(rand() % cross_layer_childs.size());
    while (subShard_2->internal_groupId == subShard_1_groupId) { subShard_2 = cross_layer_childs.at(rand() % cross_layer_childs.size()); }
    get_all_child(subShard_2, &all_child_2);
    int dblsub_shard_2 = all_child_2.at(rand() % all_child_2.size());

    return make_pair(dblsubShard_1, dblsub_shard_2);
}

bool HieraShardTree::is_forward_node(int internal_groupId, string nodeIdHex) {
    HieraShardNode* shard = get_shard_by_internal_groupId(internal_groupId);
    if (nodeIdHex == toHex(shard->nodeIDs.at(0))) {
        return true;
    }
    return false;

}