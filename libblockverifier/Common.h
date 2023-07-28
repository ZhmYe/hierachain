/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file BlockVerifier.h
 *  @author mingzhenliu
 *  @date 20180921
 */

#pragma once


#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/FixedHash.h>
#include <libethcore/Transaction.h>
#include <memory>

#include <libexecutive/Executive.h>
#include <libethcore/Block.h>

#define BLOCKVERIFIER_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("BLOCKVERIFIER")
#define EXECUTIVECONTEXT_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("EXECUTIVECONTEXT")
#define PARA_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("PARA") << LOG_BADGE(utcTime())

namespace dev
{
namespace blockverifier
{

struct BlockInfo
{
    dev::h256 hash;
    int64_t number;
    dev::h256 stateRoot;
};

struct executableTransaction // 缓存的交易
{
	dev::eth::Transaction::Ptr tx;
    std::shared_ptr<dev::blockverifier::ExecutiveContext> executiveContext;
	dev::executive::Executive::Ptr executive;
    std::shared_ptr<dev::eth::Block> block;
};

struct candidate_tx_queue
{
	std::string rwkey; // 队列所阻塞的读写集
	std::queue<std::shared_ptr<executableTransaction>> queue; // 缓存的可执行交易
};

struct rwset_msg
{
	int source_shard_id;
	int destin_shard_id;
	std::string cross_tx_hash; // 跨片交易相对应的交易哈希
	std::string readwriteset;
};

struct blockExecuteContent
{
    std::shared_ptr<ExecutiveContext> executiveContext;
    std::shared_ptr<dev::executive::Executive> executive;
};

    // 已经提交的来自不同分片的最新跨片交易编号
    // extern std::vector<int>latest_commit_cs_tx; 

    // 因前序交易未完成而被阻塞的交易 std::map<SHARDID_MESSAGEID, std::make_shared<Transaction>>
    // extern std::map<std::string, std::shared_ptr<dev::eth::Transaction>> blocked_txs;

    // 因前序交易未完成而被阻塞的区块 std::map<SHARDID_MESSAGEID, std::make_shared<Block>>
    // extern std::map<std::string, std::shared_ptr<dev::eth::Block>> blocked_blocks;

    // extern std::map<int, blockExecuteContent> cached_executeContents; // 缓存的区块执行变量

}  // namespace blockverifier
}  // namespace dev
