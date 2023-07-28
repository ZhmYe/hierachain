//
// Created by ZhmYe on 2023/5/26.
//
#pragma once
#include <json/json.h>
#include<iostream>
#include<string>
#include <fstream>
#include<libplugin/Common.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <libinitializer/Initializer.h>
#include <libinitializer/P2PInitializer.h>
using namespace dev::plugin;
using namespace std;
namespace dev{
namespace plugin {

typedef struct HieraShardNode {
    int internal_groupId; // 分片id
    vector<HieraShardNode*> children; // 子分片
    HieraShardNode *coordinator; // 祖先分片
    vector<h512> nodeIDs;
    HieraShardNode() {
      internal_groupId = -1;
      vector<HieraShardNode*> tmp;
      children = tmp;
      vector<h512> tmpNodeIDs;
      nodeIDs = tmpNodeIDs; 
      coordinator = nullptr;
    }
    HieraShardNode(int id) {
      internal_groupId = id;
      vector<HieraShardNode*> tmp;
      children = tmp;
      vector<h512> tmpNodeIDs;
      nodeIDs = tmpNodeIDs; 
      coordinator = nullptr; 
    }
} HieraShardNode;
class HieraShardTree {
public:
    HieraShardTree() {
        // HieraShardNode tmp;
        // tmp.coordinator = nullptr;
        // tmp.internal_groupId = -1;
        // vector<HieraShardNode*> tmpChildren;
        // tmp.children = tmpChildren;
        // root = &tmp;
        root = new HieraShardNode;
        hiera_shard_number = 1;
        intra_shard_number = 0;
        inter_shard_number = 0;
        cross_layer_shard_number = 0;
    }
    void buildFromJson(string json_file_name);
    void putGroupPubKeyIntoService(std::shared_ptr<Service> service, boost::property_tree::ptree const& _pt);
    void insertChild(HieraShardNode* shard, HieraShardNode *coordinator);
    void insertChildren(vector<HieraShardNode*> shards, HieraShardNode *coordinator);
    void recursionInsertChild(Json::Value result, HieraShardNode* shard, vector<string> subShards);
    void print() {
        printStruct(root);
    }
    void printStruct(HieraShardNode* shard) {
        cout << shard->internal_groupId << " ";
        int subShardNumber = shard->children.size();
        if (subShardNumber == 0) {
            return;
        }
        for (int i = 0; i < subShardNumber; i++) {
            printStruct(shard->children.at(i));
        }
    }
    int dfs(HieraShardNode* shard);
    int get_hiera_shard_number();
    HieraShardNode* get_shard_by_internal_groupId(int internal_groupId);
    pair<bool, HieraShardNode*> get_shard_by_internal_groupId(HieraShardNode* shard, int internal_groupId);
    bool is_leaf(HieraShardNode* shard);
    bool is_inter(HieraShardNode* shard);
    bool is_cross_layer(HieraShardNode* shard);
    bool is_leaf(int internal_groupId);
    bool is_inter(int internal_groupId);
    bool is_cross_layer(int internal_groupId);
    h512 get_forward_nodeId(int internal_groupId);
    h512s get_nodeIDs(int internal_group_id);
    int get_intra_shard_number();
    int get_inter_shard_number();
    int get_cross_layer_shard_number();
    int getLCA(int shard_1_groupId, int shard_2_groupId);
    int getLCA(HieraShardNode* shard_1, HieraShardNode* shard_2);
    pair<string, string> get_shard_info_by_internal_groupId(int internal_groupId);
    pair<int, int> get_inter_childs(int internal_group_id);
    int get_cross_layer_child(int internal_groupId);
    pair<int, int> get_cross_layer_childs(int internal_groupId);
    void get_all_child(HieraShardNode* shard, vector<int>* ids);
    bool is_forward_node(int internal_group_id, string nodeIdHex);
private:
    int hiera_shard_number;
    HieraShardNode *root;
    int inter_shard_number;
    int cross_layer_shard_number;
    int intra_shard_number;
};
}
}
