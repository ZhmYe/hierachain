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
/** @file BlockVerifier.cpp
 *  @author mingzhenliu
 *  @date 20180921
 */
#include "BlockVerifier.h"
#include "ExecutiveContext.h"
#include "TxDAG.h"
#include "libstorage/StorageException.h"
#include "libstoragestate/StorageState.h"
#include <libethcore/Exceptions.h>
#include <libethcore/PrecompiledContract.h>
#include <libethcore/TransactionReceipt.h>
#include <libstorage/Table.h>
#include <tbb/parallel_for.h>
#include <exception>
#include <thread>

using namespace dev;
using namespace std;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::executive;
using namespace dev::storage;

ExecutiveContext::Ptr BlockVerifier::executeBlock(Block& block, BlockInfo const& parentBlockInfo)
{
    // return nullptr prepare to exit when g_BCOSConfig.shouldExit is true
    if (g_BCOSConfig.shouldExit)
    {
        return nullptr;
    }
    if (block.blockHeader().number() < m_executingNumber)
    {
        return nullptr;
    }
    std::lock_guard<std::mutex> l(m_executingMutex);
    if (block.blockHeader().number() < m_executingNumber)
    {
        return nullptr;
    }
    ExecutiveContext::Ptr context = nullptr;
    try
    {
        context = serialExecuteBlock(block, parentBlockInfo); // 交易全部串行执行

        // if (g_BCOSConfig.version() >= RC2_VERSION && m_enableParallel)
        // {
        //     context = parallelExecuteBlock(block, parentBlockInfo);
        // }
        // else
        // {
        //     context = serialExecuteBlock(block, parentBlockInfo);
        // }
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_BADGE("executeBlock") << LOG_DESC("executeBlock exception")
                                 << LOG_KV("blockNumber", block.blockHeader().number());
        return nullptr;
    }
    m_executingNumber = block.blockHeader().number();
    return context;
}

ExecutiveContext::Ptr BlockVerifier::serialExecuteBlock(
    Block& block, BlockInfo const& parentBlockInfo)
{
    if(block.isFirstExecute) // 若该区块是第一次处理，对unExecutedTxNum进行初始化 ADD BY THB
    {
        block.unExecutedTxNum = block.getTransactionSize();
        block.isFirstExecute = false;
    }

    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions()->size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("hash", block.header().hash().abridged())
                            << LOG_KV("height", block.header().number())
                            << LOG_KV("receiptRoot", block.header().receiptsRoot())
                            << LOG_KV("stateRoot", block.header().stateRoot())
                            << LOG_KV("dbHash", block.header().dbHash())
                            << LOG_KV("parentHash", parentBlockInfo.hash.abridged())
                            << LOG_KV("parentNum", parentBlockInfo.number)
                            << LOG_KV("parentStateRoot", parentBlockInfo.stateRoot);

    uint64_t startTime = utcTime();

    ExecutiveContext::Ptr executiveContext = std::make_shared<ExecutiveContext>();
    try
    {
        m_executiveContextFactory->initExecutiveContext(
            parentBlockInfo, parentBlockInfo.stateRoot, executiveContext);
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_DESC("[executeBlock] Error during initExecutiveContext")
                                 << LOG_KV("blkNum", block.blockHeader().number())
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    block.resizeTransactionReceipt(block.transactions()->size());

    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Init env takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions()->size())
                             << LOG_KV("num", block.blockHeader().number());
    uint64_t pastTime = utcTime();
    
    try
    {
        EnvInfo envInfo(block.blockHeader(), m_pNumberHash, 0);
        envInfo.setPrecompiledEngine(executiveContext);
        auto executive = createAndInitExecutive(executiveContext->getState(), envInfo);

        // 交易全部不执行

        for (size_t i = 0; i < block.transactions()->size(); i++)
        {
            auto& tx = (*block.transactions())[i];

            //检查交易hash, 根据 dev::rpc::innertxhash2readwriteset 判断交易是否为片内交易
            // 若交易的读写集没有被阻塞，那么交易可以立即执行，并将读写key放入阻塞队列，直到区块被提交key才可以出队列
            //（读写集使用的场景是简单的读后写，例如A=A+1）

            BLOCKVERIFIER_LOG(INFO) << LOG_DESC("sleeping...");
            // std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            BLOCKVERIFIER_LOG(INFO) << LOG_DESC("sleep over...");

            TransactionReceipt::Ptr resultReceipt = execute(tx, executiveContext, executive);
            block.setTransactionReceipt(i, resultReceipt);
            executiveContext->getState()->commit(); // 状态写缓存

            dev::h256 tx_hash = tx->hash();
            if(dev::rpc::innertxhash2readwriteset.count(tx_hash) != 0)
            {
                BLOCKVERIFIER_LOG(INFO) << LOG_DESC("该交易为片内交易...");
                // std::string rwkey = dev::rpc::innertxhash2readwriteset.at(tx_hash); // 提取交易读写集

                // if(_blocked_tx_pool.locking_key->count(rwkey) != 0) // 读写key已经被其他区块阻塞，被阻塞的交易统一等某个区块被提交了之后再处理
                // {
                //     BLOCKVERIFIER_LOG(INFO) << LOG_DESC("读写集已被其他区块阻塞...");
                //     int holding_tx_num = _blocked_tx_pool.locking_key->at(rwkey);
                //     _blocked_tx_pool.locking_key->at(rwkey) = holding_tx_num + 1;

                //     std::shared_ptr<dev::eth::Block> block_ptr = std::make_shared<dev::eth::Block>(block);
                //     _blocked_tx_pool.insertIntraTx(tx, executiveContext, executive, rwkey, block_ptr);

                //     if(block.m_blockLockedRwKey->count(rwkey) != 0)
                //     {
                //         int holding_tx_num = block.m_blockLockedRwKey->at(rwkey);
                //         block.m_blockLockedRwKey->at(rwkey) = holding_tx_num + 1;
                //     }
                //     else
                //     {
                //         block.m_blockLockedRwKey->insert(std::make_pair(rwkey, 1));
                //     }
                // }
                // else // 读写key还未被阻塞，交易可以执行，key被阻塞的次数加1，将交易缓存在读写集相对应的队列中
                // {  
                //    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("正在执行片内交易...");
                //    TransactionReceipt::Ptr resultReceipt = execute(tx, executiveContext, executive); 
                //    block.setTransactionReceipt(i, resultReceipt);
                //    executiveContext->getState()->commit(); // 状态写缓存
                   
                //    block.unExecutedTxNum--; // 未执行交易数目减1
                //    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("片内交易执行结束...");
                // }
            }
            else if(dev::rpc::corsstxhash2transaction_info.count(tx_hash) != 0)
            {
                BLOCKVERIFIER_LOG(INFO) << LOG_DESC("该交易为跨片交易，待处理...");
                // std::string rwkey = dev::rpc::corsstxhash2transaction_info.at(tx_hash).readwrite_key; // 提取跨片子交易读写集

                // if(_blocked_tx_pool.locking_key->count(rwkey) != 0)
                // {
                //     int holding_tx_num = _blocked_tx_pool.locking_key->at(rwkey);
                //     _blocked_tx_pool.locking_key->at(rwkey) = holding_tx_num + 1;
                // }
                // else
                // {
                //    _blocked_tx_pool.locking_key->insert(std::make_pair(rwkey, 1));
                // }

                // if(block.m_blockLockedRwKey->count(rwkey) != 0)
                // {
                //     int holding_tx_num = block.m_blockLockedRwKey->at(rwkey);
                //     block.m_blockLockedRwKey->at(rwkey) = holding_tx_num + 1;
                // }
                // else
                // {
                //     block.m_blockLockedRwKey->insert(std::make_pair(rwkey, 1));
                // }

                // // 对跨片交易进行缓存，先假设收到的交易顺序是正确的，收到交易后直接往candidate_tx_queues中插入
                // std::shared_ptr<dev::eth::Block> block_ptr = std::make_shared<dev::eth::Block>(block);
                // _blocked_tx_pool.insert_candidate_cs_tx(tx, executiveContext, executive, block_ptr);
            }
            else // 为部署合约交易，执行，不访问读写集
            {
                // BLOCKVERIFIER_LOG(INFO) << LOG_DESC("正在执行部署合约交易...");
                // TransactionReceipt::Ptr resultReceipt = execute(tx, executiveContext, executive);
                // block.setTransactionReceipt(i, resultReceipt);
                // executiveContext->getState()->commit(); // 状态写缓存
                // block.unExecutedTxNum--; // 未执行交易数目减1
            }
        }

        blockExecuteContent _blockExecuteContent{executiveContext, executive};
        cached_executeContents.insert(std::make_pair(block.blockHeader().number(), _blockExecuteContent)); // 缓存区块执行变量
        ENGINE_LOG(INFO) << LOG_KV("BlockVerifer.block->blockHeader().number()", block.blockHeader().number()); 

        // if(block.unExecutedTxNum == 0)
        // {
        //     BLOCKVERIFIER_LOG(INFO) << LOG_DESC("区块内所有的交易执行完毕");
        // }
        // else // 若区块内存在未执行的交易，则将交易进行缓存
        // {
        //     BLOCKVERIFIER_LOG(INFO) << LOG_DESC("区块内有交易被阻塞，区块放入缓存区块队列");
        //     std::shared_ptr<dev::eth::Block> block_ptr = std::make_shared<dev::eth::Block>(block);
        //     dev::consensus::cachedBlocks.push(block_ptr);
        //     // return nullptr; // 交易未执行完，返回 nullptr // ADD BY THB
        // }
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_BADGE("executeBlock")
                                 << LOG_DESC("Error during serial block execution")
                                 << LOG_KV("blkNum", block.blockHeader().number())
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(
            BlockExecutionFailed() << errinfo_comment("Error during serial block execution"));
    }

    
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Run serial tx takes")
                             << LOG_KV("time(ms)", utcTime() - pastTime)
                             << LOG_KV("txNum", block.transactions()->size())
                             << LOG_KV("num", block.blockHeader().number());

    // if(block.unExecutedTxNum < block.getTransactionSize()) // 若第一次处理区块有交易顺利执行，则写缓存
    // {
        
        h256 stateRoot = executiveContext->getState()->rootHash();
        // set stateRoot in receipts
        if (g_BCOSConfig.version() >= V2_2_0)
        {
            // when support_version is lower than v2.2.0, doesn't setStateRootToAllReceipt
            // enable_parallel=true can't be run with enable_parallel=false
            block.setStateRootToAllReceipt(stateRoot);
        }
        // block.updateSequenceReceiptGas();
        // block.calReceiptRoot();
        block.header().setStateRoot(stateRoot);
        if (dynamic_pointer_cast<storagestate::StorageState>(executiveContext->getState()))
        {
            block.header().setDBhash(stateRoot);
        }
        else
        {
            block.header().setDBhash(executiveContext->getMemoryTableFactory()->hash());
        }

        // if executeBlock is called by consensus module, no need to compare receiptRoot and stateRoot
        // since origin value is empty if executeBlock is called by sync module, need to compare
        // receiptRoot, stateRoot and dbHash
        // Consensus module execute block, receiptRoot is empty, skip this judgment
        // The sync module execute block, receiptRoot is not empty, need to compare BlockHeader

        if (tmpHeader.receiptsRoot() != h256())
        {
            if (tmpHeader != block.blockHeader())
            {
                BLOCKVERIFIER_LOG(ERROR)
                    << "Invalid Block with bad stateRoot or receiptRoot or dbHash"
                    << LOG_KV("blkNum", block.blockHeader().number())
                    << LOG_KV("originHash", tmpHeader.hash().abridged())
                    << LOG_KV("curHash", block.header().hash().abridged())
                    << LOG_KV("orgReceipt", tmpHeader.receiptsRoot().abridged())
                    << LOG_KV("curRecepit", block.header().receiptsRoot().abridged())
                    << LOG_KV("orgTxRoot", tmpHeader.transactionsRoot().abridged())
                    << LOG_KV("curTxRoot", block.header().transactionsRoot().abridged())
                    << LOG_KV("orgState", tmpHeader.stateRoot().abridged())
                    << LOG_KV("curState", block.header().stateRoot().abridged())
                    << LOG_KV("orgDBHash", tmpHeader.dbHash().abridged())
                    << LOG_KV("curDBHash", block.header().dbHash().abridged());
                BOOST_THROW_EXCEPTION(
                    InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                        "Invalid Block with bad stateRoot or ReceiptRoot, orgBlockHash " +
                        block.header().hash().abridged()));
            }
        }
        BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Execute block takes")
                                    << LOG_KV("time(ms)", utcTime() - startTime)
                                    << LOG_KV("txNum", block.transactions()->size())
                                    << LOG_KV("num", block.blockHeader().number())
                                    << LOG_KV("blockHash", block.headerHash())
                                    << LOG_KV("stateRoot", block.header().stateRoot())
                                    << LOG_KV("dbHash", block.header().dbHash())
                                    << LOG_KV("transactionRoot", block.transactionRoot())
                                    << LOG_KV("receiptRoot", block.receiptRoot());
    // }

    return executiveContext;
}

ExecutiveContext::Ptr BlockVerifier::parallelExecuteBlock(
    Block& block, BlockInfo const& parentBlockInfo)

{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("[executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions()->size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("parentHash", parentBlockInfo.hash)
                            << LOG_KV("parentNum", parentBlockInfo.number)
                            << LOG_KV("parentStateRoot", parentBlockInfo.stateRoot);

    auto start_time = utcTime();
    auto record_time = utcTime();
    ExecutiveContext::Ptr executiveContext = std::make_shared<ExecutiveContext>();
    try
    {
        m_executiveContextFactory->initExecutiveContext(
            parentBlockInfo, parentBlockInfo.stateRoot, executiveContext);
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_DESC("[executeBlock] Error during initExecutiveContext")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    auto memoryTableFactory = executiveContext->getMemoryTableFactory();

    auto initExeCtx_time_cost = utcTime() - record_time;
    record_time = utcTime();

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    block.resizeTransactionReceipt(block.transactions()->size());
    auto perpareBlock_time_cost = utcTime() - record_time;
    record_time = utcTime();

    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    txDag->init(executiveContext, block.transactions(), block.blockHeader().number());


    txDag->setTxExecuteFunc([&](Transaction::Ptr _tr, ID _txId, Executive::Ptr _executive) {
        auto resultReceipt = execute(_tr, executiveContext, _executive);

        block.setTransactionReceipt(_txId, resultReceipt);
        executiveContext->getState()->commit();
        return true;
    });
    auto initDag_time_cost = utcTime() - record_time;
    record_time = utcTime();

    auto parallelTimeOut = utcSteadyTime() + 30000;  // 30 timeout

    try
    {
        tbb::atomic<bool> isWarnedTimeout(false);
        tbb::parallel_for(tbb::blocked_range<unsigned int>(0, m_threadNum),
            [&](const tbb::blocked_range<unsigned int>& _r) {
                (void)_r;
                EnvInfo envInfo(block.blockHeader(), m_pNumberHash, 0);
                envInfo.setPrecompiledEngine(executiveContext);
                auto executive = createAndInitExecutive(executiveContext->getState(), envInfo);

                while (!txDag->hasFinished())
                {
                    if (!isWarnedTimeout.load() && utcSteadyTime() >= parallelTimeOut)
                    {
                        isWarnedTimeout.store(true);
                        BLOCKVERIFIER_LOG(WARNING)
                            << LOG_BADGE("executeBlock") << LOG_DESC("Para execute block timeout")
                            << LOG_KV("txNum", block.transactions()->size())
                            << LOG_KV("blockNumber", block.blockHeader().number());
                    }

                    txDag->executeUnit(executive);
                }
            });
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_BADGE("executeBlock")
                                 << LOG_DESC("Error during parallel block execution")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(
            BlockExecutionFailed() << errinfo_comment("Error during parallel block execution"));
    }
    // if the program is going to exit, return nullptr directly
    if (g_BCOSConfig.shouldExit)
    {
        return nullptr;
    }
    auto exe_time_cost = utcTime() - record_time;
    record_time = utcTime();

    h256 stateRoot = executiveContext->getState()->rootHash();
    auto getRootHash_time_cost = utcTime() - record_time;
    record_time = utcTime();

    // set stateRoot in receipts
    block.setStateRootToAllReceipt(stateRoot);
    block.updateSequenceReceiptGas();
    auto setAllReceipt_time_cost = utcTime() - record_time;
    record_time = utcTime();

    block.calReceiptRoot();
    auto getReceiptRoot_time_cost = utcTime() - record_time;
    record_time = utcTime();

    block.header().setStateRoot(stateRoot);
    if (dynamic_pointer_cast<storagestate::StorageState>(executiveContext->getState()))
    {
        block.header().setDBhash(stateRoot);
    }
    else
    {
        block.header().setDBhash(executiveContext->getMemoryTableFactory()->hash());
    }
    auto setStateRoot_time_cost = utcTime() - record_time;
    record_time = utcTime();
    // Consensus module execute block, receiptRoot is empty, skip this judgment
    // The sync module execute block, receiptRoot is not empty, need to compare BlockHeader
    if (tmpHeader.receiptsRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BLOCKVERIFIER_LOG(ERROR)
                << "Invalid Block with bad stateRoot or receiptRoot or dbHash"
                << LOG_KV("blkNum", block.blockHeader().number())
                << LOG_KV("originHash", tmpHeader.hash().abridged())
                << LOG_KV("curHash", block.header().hash().abridged())
                << LOG_KV("orgReceipt", tmpHeader.receiptsRoot().abridged())
                << LOG_KV("curRecepit", block.header().receiptsRoot().abridged())
                << LOG_KV("orgTxRoot", tmpHeader.transactionsRoot().abridged())
                << LOG_KV("curTxRoot", block.header().transactionsRoot().abridged())
                << LOG_KV("orgState", tmpHeader.stateRoot().abridged())
                << LOG_KV("curState", block.header().stateRoot().abridged())
                << LOG_KV("orgDBHash", tmpHeader.dbHash().abridged())
                << LOG_KV("curDBHash", block.header().dbHash().abridged());
#ifdef FISCO_DEBUG
            auto receipts = block.transactionReceipts();
            for (size_t i = 0; i < receipts->size(); ++i)
            {
                BLOCKVERIFIER_LOG(ERROR) << LOG_BADGE("FISCO_DEBUG") << LOG_KV("index", i)
                                         << LOG_KV("hash", block.transaction(i)->hash())
                                         << ",receipt=" << *receipts->at(i);
            }
#endif
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReciptRoot"));
        }
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Para execute block takes")
                             << LOG_KV("time(ms)", utcTime() - start_time)
                             << LOG_KV("txNum", block.transactions()->size())
                             << LOG_KV("blockNumber", block.blockHeader().number())
                             << LOG_KV("blockHash", block.headerHash())
                             << LOG_KV("stateRoot", block.header().stateRoot())
                             << LOG_KV("dbHash", block.header().dbHash())
                             << LOG_KV("transactionRoot", block.transactionRoot())
                             << LOG_KV("receiptRoot", block.receiptRoot())
                             << LOG_KV("initExeCtxTimeCost", initExeCtx_time_cost)
                             << LOG_KV("perpareBlockTimeCost", perpareBlock_time_cost)
                             << LOG_KV("initDagTimeCost", initDag_time_cost)
                             << LOG_KV("exeTimeCost", exe_time_cost)
                             << LOG_KV("getRootHashTimeCost", getRootHash_time_cost)
                             << LOG_KV("setAllReceiptTimeCost", setAllReceipt_time_cost)
                             << LOG_KV("getReceiptRootTimeCost", getReceiptRoot_time_cost)
                             << LOG_KV("setStateRootTimeCost", setStateRoot_time_cost);
    return executiveContext;
}


TransactionReceipt::Ptr BlockVerifier::executeTransaction(
    const BlockHeader& blockHeader, dev::eth::Transaction::Ptr _t)
{
    ExecutiveContext::Ptr executiveContext = std::make_shared<ExecutiveContext>();
    BlockInfo blockInfo{blockHeader.hash(), blockHeader.number(), blockHeader.stateRoot()};
    try
    {
        m_executiveContextFactory->initExecutiveContext(
            blockInfo, blockHeader.stateRoot(), executiveContext);
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR)
            << LOG_DESC("[executeTransaction] Error during execute initExecutiveContext")
            << LOG_KV("errorMsg", boost::diagnostic_information(e));
    }
    EnvInfo envInfo(blockHeader, m_pNumberHash, 0);
    envInfo.setPrecompiledEngine(executiveContext);
    auto executive = createAndInitExecutive(executiveContext->getState(), envInfo);
    // only Rpc::call will use executeTransaction, RPC do catch exception
    return execute(_t, executiveContext, executive);

    // 这边需要继续写回执

}

dev::eth::TransactionReceipt::Ptr BlockVerifier::execute(dev::eth::Transaction::Ptr _t,
    dev::blockverifier::ExecutiveContext::Ptr executiveContext, Executive::Ptr executive)
{
    // Create and initialize the executive. This will throw fairly cheaply and quickly if the
    // transaction is bad in any way.
    executive->reset();

    // OK - transaction looks valid - execute.
    try
    {
        executive->initialize(_t);
        if (!executive->execute())
            executive->go();
        executive->finalize();
    }
    catch (StorageException const& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_DESC("get StorageException") << LOG_KV("what", e.what());
        BOOST_THROW_EXCEPTION(e);
    }
    catch (Exception const& _e)
    {
        // only OutOfGasBase ExecutorNotFound exception will throw
        BLOCKVERIFIER_LOG(ERROR) << diagnostic_information(_e);
    }
    catch (std::exception const& _e)
    {
        BLOCKVERIFIER_LOG(ERROR) << _e.what();
    }

    executive->loggingException();
    return std::make_shared<TransactionReceipt>(executiveContext->getState()->rootHash(false),
        executive->gasUsed(), executive->logs(), executive->status(),
        executive->takeOutput().takeBytes(), executive->newAddress());
}

dev::executive::Executive::Ptr BlockVerifier::createAndInitExecutive(
    std::shared_ptr<StateFace> _s, dev::executive::EnvInfo const& _envInfo)
{
    return std::make_shared<Executive>(_s, _envInfo, m_evmFlags & EVMFlags::FreeStorageGas);
}

void blocked_tx_pool::insertIntraTx(dev::eth::Transaction::Ptr _tx, ExecutiveContext::Ptr executiveContext, dev::executive::Executive::Ptr executive, std::string& rwkey, std::shared_ptr<dev::eth::Block> block)
{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("开始缓存交易...");
    executableTransaction tx{_tx, executiveContext, executive, block};
    auto tx_ptr = std::make_shared<dev::blockverifier::executableTransaction>(tx);
    candidate_tx_queues->at(rwkey).queue.push(tx_ptr);
}


void blocked_tx_pool::insert_candidate_cs_tx(dev::eth::Transaction::Ptr _subtx, ExecutiveContext::Ptr executiveContext, dev::executive::Executive::Ptr executive, std::shared_ptr<dev::eth::Block> block) // 放在RPC模块进行处理
{
    auto transaction_info = dev::rpc::corsstxhash2transaction_info.at(_subtx->hash());    
    std::string rw_keys_str = transaction_info.readwrite_key;

    std::vector<std::string> readwrite_keys;
    bool ret = split(rw_keys_str, readwrite_keys, "_");
    std::string readwrite_key = readwrite_keys[0];

    // 判断candidate_tx_queues中是否有readwrite_key的队列，因为之前可能没有
    // 当前片内交易的读写集（假设跨片交易的第一个读写集是当前片的读写集）, 定位读写集 readwrite_key 的交易缓存队列，_subtx 插入到 candidate_cs_tx中，更新上锁的读写集  
    if(candidate_tx_queues->count(readwrite_key) == 0)
    {
        candidate_tx_queue tx_queue { readwrite_key };
        executableTransaction tx{_subtx, executiveContext, executive, block};
        auto tx_ptr = std::make_shared<dev::blockverifier::executableTransaction>(tx);

        tx_queue.queue.push(tx_ptr);
        candidate_tx_queues->insert(std::make_pair(readwrite_key, tx_queue));
    }
    else
    {
        executableTransaction tx{_subtx, executiveContext, executive, block};
        auto tx_ptr = std::make_shared<dev::blockverifier::executableTransaction>(tx);
        candidate_tx_queues->at(readwrite_key).queue.push(tx_ptr);
    }
    
    // 更新 cs_txhash2blockedrwset
    cs_txhash2blockedrwset->insert(std::make_pair(transaction_info.cross_tx_hash, readwrite_key));

    // 更新 received_rwset_num
    std::string tx_hash = transaction_info.cross_tx_hash;
    received_rwset_num->insert(std::make_pair(tx_hash, 0)); // 未执行前，已经收到的读写集个数设置为0

    // 在 cs_readwriteset_num 中添加新的内容
    int rwsetnum = readwrite_keys.size();
    cs_rwset_num->insert(std::make_pair(tx_hash, rwsetnum));

    // 检查 cached_cs_tx 中后继 _message_id + 1 的交易是否已经到达, 若已经到达，也插入到 candidate_cs_tx 中，更新上锁的读写集
    // 暂时 不考虑 乱序问题，因此这部分先跳过.... 稍后补充
}

bool blocked_tx_pool::split(const std::string &str, std::vector<std::string> &ret, std::string sep)  // 工具函数
{
    if (str.empty()) {
        return false;
    }

    std::string temp;
    std::string::size_type begin = str.find_first_not_of(sep);
    std::string::size_type pos = 0;

    while (begin != std::string::npos) {
        pos = str.find(sep, begin);
        if (pos != std::string::npos) {
            temp = str.substr(begin, pos - begin);
            begin = pos + sep.length();
        } else {
            temp = str.substr(begin);
            begin = pos;
        }

        if (!temp.empty()) {
            ret.push_back(temp);
            temp.clear();
        }
    }
    return true; 
}