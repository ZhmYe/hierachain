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

#include "BlockVerifierInterface.h"
#include "ExecutiveContext.h"
#include "ExecutiveContextFactory.h"
#include "libprecompiled/Precompiled.h"
#include <libdevcore/FixedHash.h>
#include <libdevcore/ThreadPool.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Block.h>
#include <libethcore/Protocol.h>
#include <libethcore/Transaction.h>
#include <libethcore/TransactionReceipt.h>
#include <libexecutive/Executive.h>
#include <libmptstate/State.h>
#include <boost/function.hpp>
#include <algorithm>
#include <memory>
#include <thread>
#include <librpc/Common.h>
#include <libconsensus/pbft/Common.h>
#include <libblockverifier/Common.h>

namespace dev
{
namespace eth
{
class TransactionReceipt;

}  // namespace eth

namespace blockverifier
{
class BlockVerifier : public BlockVerifierInterface,
                      public std::enable_shared_from_this<BlockVerifier>
{
public:
    typedef std::shared_ptr<BlockVerifier> Ptr;
    typedef boost::function<dev::h256(int64_t x)> NumberHashCallBackFunction;
    BlockVerifier(bool _enableParallel = false) : m_enableParallel(_enableParallel)
    {
        if (_enableParallel)
        {
            m_threadNum = std::max(std::thread::hardware_concurrency(), (unsigned int)1);
        }
    }

    virtual ~BlockVerifier() {}

    ExecutiveContext::Ptr executeBlock(dev::eth::Block& block, BlockInfo const& parentBlockInfo);
    ExecutiveContext::Ptr serialExecuteBlock(
        dev::eth::Block& block, BlockInfo const& parentBlockInfo);
    ExecutiveContext::Ptr parallelExecuteBlock(
        dev::eth::Block& block, BlockInfo const& parentBlockInfo);


    dev::eth::TransactionReceipt::Ptr executeTransaction(
        const dev::eth::BlockHeader& blockHeader, dev::eth::Transaction::Ptr _t);

    dev::eth::TransactionReceipt::Ptr execute(dev::eth::Transaction::Ptr _t,
        dev::blockverifier::ExecutiveContext::Ptr executiveContext,
        dev::executive::Executive::Ptr executive);


    void setExecutiveContextFactory(ExecutiveContextFactory::Ptr executiveContextFactory)
    {
        m_executiveContextFactory = executiveContextFactory;
    }
    ExecutiveContextFactory::Ptr getExecutiveContextFactory() { return m_executiveContextFactory; }
    void setNumberHash(const NumberHashCallBackFunction& _pNumberHash)
    {
        m_pNumberHash = _pNumberHash;
    }

    dev::executive::Executive::Ptr createAndInitExecutive(
        std::shared_ptr<executive::StateFace> _s, dev::executive::EnvInfo const& _envInfo);
    void setEvmFlags(VMFlagType const& _evmFlags) { m_evmFlags = _evmFlags; }

    // // // modify by thb, base方案：单独线程轮询处理缓存的交易
    // static void executeTxs();

    // bool startprocessThread(); // start for executeTxs

private:
    ExecutiveContextFactory::Ptr m_executiveContextFactory;
    NumberHashCallBackFunction m_pNumberHash;
    bool m_enableParallel;
    unsigned int m_threadNum = -1;

    std::mutex m_executingMutex;
    std::atomic<int64_t> m_executingNumber = {0};

    VMFlagType m_evmFlags = 0;

    // pthread_t processthread; // modify by thb
};

// class blocked_tx_pool
// {
//     public:
// 		std::shared_ptr<tbb::concurrent_unordered_map<std::string, int>> cs_rwset_num;  // 每笔跨片交易应当收到的读写集个数(tx_hash-->num)
// 		std::shared_ptr<tbb::concurrent_unordered_map<std::string, int>> received_rwset_num; // 每笔跨片交易已经收到的跨片交易读写集个数 (cross_tx_hash-->num)

// 		// // std::shared_ptr<tbb::concurrent_unordered_map<std::string, std::shared_ptr<dev::eth::Transaction>>> cached_cs_tx; // 缓冲队列跨片交易集合(用以应对网络传输下，收到的交易乱序)，(shardid_messageid-->subtx)，由执行模块代码触发
// 		// std::shared_ptr<tbb::concurrent_unordered_map<std::string, std::shared_ptr<dev::blockverifier::executableTransaction>>> cached_cs_tx; // 缓冲队列跨片交易集合(用以应对网络传输下，收到的交易乱序)，(shardid_messageid-->subtx)，由执行模块代码触发

// 		std::shared_ptr<tbb::concurrent_unordered_map<std::string, candidate_tx_queue>> candidate_tx_queues; // 执行队列池 readwriteset --> candidate_tx_queue
// 		// std::shared_ptr<tbb::concurrent_vector<int>> latest_candidate_tx_messageids; // 已经提交candidate_cs_tx的来自不同分片的最大 messageid[3,4]

// 		std::shared_ptr<tbb::concurrent_unordered_map<std::string, int>> locking_key; // 交易池交易因等待收齐状态而正在锁定的状态key（以为区块还未提交）
// 		std::shared_ptr<tbb::concurrent_unordered_map<std::string, std::string>> cs_txhash2blockedrwset; // cross_shard_tx_hash --> blocked_readwriteset, 然后根据 blocked_readwriteset 寻找到 candidate_tx_queue

//         blocked_tx_pool()
//         {
// 			cs_rwset_num = std::make_shared<tbb::concurrent_unordered_map<std::string, int>>();
// 			received_rwset_num = std::make_shared<tbb::concurrent_unordered_map<std::string, int>>();

// 			// cached_cs_tx = std::make_shared<tbb::concurrent_unordered_map<std::string, std::shared_ptr<dev::blockverifier::executableTransaction>>>(); // 放在RPC模块进行处理
// 			candidate_tx_queues = std::make_shared<tbb::concurrent_unordered_map<std::string, candidate_tx_queue>>();
// 			// latest_candidate_tx_messageids = std::make_shared<tbb::concurrent_vector<int>>(dev::consensus::SHARDNUM);

// 			locking_key = std::make_shared<tbb::concurrent_unordered_map<std::string, int>>();
// 			// hash2blocked_readwriteset = std::make_shared<tbb::concurrent_unordered_map<std::string, std::string>>();
//         }

//         ~blocked_tx_pool() {};

//         // void insert_tx(dev::eth::Transaction _tx, int _type);

//         void insertIntraTx(dev::eth::Transaction::Ptr _tx, ExecutiveContext::Ptr executiveContext, dev::executive::Executive::Ptr executive, std::string& readwrite_key, std::shared_ptr<dev::eth::Block> block);

//         // void insert_cached_cs_tx(dev::eth::Transaction::Ptr _subtx, ExecutiveContext::Ptr executiveContext, dev::executive::Executive::Ptr executive); // 放在RPC模块进行处理

//         void insert_candidate_cs_tx(dev::eth::Transaction::Ptr _subtx, ExecutiveContext::Ptr executiveContext, dev::executive::Executive::Ptr executive, std::shared_ptr<dev::eth::Block> block); // 放在RPC模块进行处理

// 		// void insert_readwriteset(readwriteset_msg _readwriteset_msg); // P2P模块触发

//         // int tryExecute(readwriteset_msg _readwriteset_msg); // 当收到一个读写集之后，P2P触发的回调函数tryExecute，尝试执行 candidate_cs_tx 中第一笔交易

//         bool split(const std::string &str, std::vector<std::string> &ret, std::string sep);  // 工具函数
// };

// extern blocked_tx_pool _blocked_tx_pool; // 声明全局变量

}  // namespace blockverifier

}  // namespace dev
