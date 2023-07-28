#pragma once

#include "Common.h"
#include <libethcore/Transaction.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>
#include <libdevcore/FixedHash.h>
#include <boost/algorithm/string.hpp>

using namespace dev;
using namespace std;

namespace dev {
    namespace plugin {

        class BlockingTxQueue: public std::enable_shared_from_this<BlockingTxQueue> // 阻塞交易队列类
        {
            public:
                BlockingTxQueue(){
                    txs = make_shared<queue<transactionWithReadWriteSet::Ptr>>(); // 交易
                    lockingkeys = make_shared<map<string, int>>(); // 队列阻塞的状态
                }
                
                ~BlockingTxQueue() {}

                bool isBlocked(string& keys); // 判断当前交易所访问的key是否已经被锁定

                void insertTx(transactionWithReadWriteSet::Ptr tx); // 向队列尾添加交易

                void insertTxs(shared_ptr<vector<transactionWithReadWriteSet::Ptr>> reorderedTxs); // 向队列尾批量添加交易

                void deleteTx(); // 删除队首交易
                
                transactionWithReadWriteSet::Ptr frontTx();

                int txsize();

                void batch_pop(shared_ptr<queue<transactionWithReadWriteSet::Ptr>> txQueue); // 将当前所有的交易转移到txQueue中

            public:
                shared_ptr<queue<transactionWithReadWriteSet::Ptr>> txs; // 所有缓存的交易
                shared_ptr<map<string, int>> lockingkeys; // 当前被阻塞的key集合
                mutex queueLock; // 保证对lockingkeys和txs操作的并发安全

                uint64_t last_failture_pop = 0; // 队首元素上一次尝试pop失败的时间
        };

        // struct candidateTxQueue
        // {
        //     string rwkey; // 队列所阻塞的读写集
        //     shared_ptr<BlockingQueue<shared_ptr<transactionWithReadWriteSet>>> queue; // 阻塞子队列
        // };

        // class BlockingQueues:public std::enable_shared_from_this<BlockingQueues>
        // {
        //     public:
        //         BlockingQueues()
        //         {
        //             csTxRWsetNum = make_shared<tbb::concurrent_unordered_map<string, int>>();
        //             receivedTxRWset = make_shared<tbb::concurrent_unordered_map<string, string>>();
        //             receivedAccessNum = make_shared<tbb::concurrent_unordered_map<string, int>>();
        //             candidateTxQueues = make_shared<tbb::concurrent_unordered_map<string, candidateTxQueue>>();
        //             txhashTocrossshardtxid = make_shared<map<h256, string>>();
        //             m_sendedRWSetTxHash = make_shared<map<dev::h256, int>>();
        //             m_sendedbatchrwset = make_shared<set<int>>();
        //             receivedContentionRates = make_shared<tbb::concurrent_unordered_map<string, string>>();
        //         }
        //         ~BlockingQueues() {};

        //         void insertIntraShardTx(transactionWithReadWriteSet::Ptr tx, string& readwrite_key);

        //         void insertInterShardTx(transactionWithReadWriteSet::Ptr tx, string& readwrite_key);

        //         void insertCandidateTxQueues(string rwKey, transactionWithReadWriteSet::Ptr tx)
        //         {
        //             if(candidateTxQueues->count(rwKey) == 0) {
        //                 shared_ptr<BlockingQueue<shared_ptr<transactionWithReadWriteSet>>> txQueue = 
        //                                     make_shared<BlockingQueue<shared_ptr<transactionWithReadWriteSet>>>();

        //                 candidateTxQueue caQueue{rwKey, txQueue};
        //                 caQueue.queue->enqueue(tx);
        //                 candidateTxQueues->insert(make_pair(rwKey, caQueue));
        //             }
        //             else
        //             {
        //                 candidateTxQueues->at(rwKey).queue->enqueue(tx);
        //             }
        //         }

        //     public:
        //         shared_ptr<tbb::concurrent_unordered_map<string, int>> csTxRWsetNum;  // 每笔跨片交易应当收到的读写集个数(crossshardTxId-->num)
        //         shared_ptr<tbb::concurrent_unordered_map<string, string>> receivedTxRWset;  // 跨片交易已经收到的不同key的读写集数目 <crossshardTxId_rwkey1, num1>
        //         shared_ptr<tbb::concurrent_unordered_map<string, int>> receivedAccessNum; // 收到的来自其他分片相应读写集的征用情况
        //         shared_ptr<tbb::concurrent_unordered_map<string, string>> receivedContentionRates; // 收到的来自其他分片相应读写集的征用情况

        //         shared_ptr<tbb::concurrent_unordered_map<string, candidateTxQueue>> candidateTxQueues; //  需要并发安全！交易缓存队列池 readwriteset --> candidate_tx_queue
        //         shared_ptr<map<h256, string>> txhashTocrossshardtxid; // 交易hash->相应跨片交易id
        //         //（注：一笔交易可能要占有片内的多个读写集，如果是这样，需要同时插入到多个队列中，操作之间应该是原子的，否则可能发生死锁，为了实验方便，这里假设每个跨片交易仅占用自己分片的一个key，因此不存在该问题）
        //         // 于此同时，执行部分先设置1个线程，
		//         shared_ptr<map<string, int>> lockingRWKey; // 需要并发安全！交易池交易因等待收齐状态而正在锁定的状态key
        //         shared_ptr<map<h256, int>> m_sendedRWSetTxHash;
        //         shared_ptr<set<int>> m_sendedbatchrwset;

        //         int lockNum; // 当前阻塞的交易数目
        //         mutex queuesLock; // 后面还需要细粒度锁
        //         mutex lockingKeyLock;
        // };
    }
}