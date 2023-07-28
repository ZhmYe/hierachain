#include <libplugin/BlockingTxQueue.h>

using namespace std;
using namespace dev::plugin;

// void BlockingQueues::insertIntraShardTx(transactionWithReadWriteSet::Ptr tx, std::string& readwrite_key)
// {
//     insertCandidateTxQueues(readwrite_key, tx);
// }

// void BlockingQueues::insertInterShardTx(dev::eth::Transaction::Ptr tx, std::string& readwrite_key)
// {
//     insertCandidateTxQueues(readwrite_key, tx);
// }

// void BlockingQueues::insertInterShardTx(transactionWithReadWriteSet::Ptr tx, string& readwrite_key)
// {
//     insertCandidateTxQueues(readwrite_key, tx);
// }

// 判断交易的读写集key是否已经被阻塞，keys格式:key1_key2_...
bool BlockingTxQueue::isBlocked(string& keys){
    lock_guard<std::mutex> lock(queueLock);

    vector<string> keyitems;
    boost::split(keyitems, keys, boost::is_any_of("_"), boost::token_compress_on);
    int key_size = keyitems.size();

    bool isBlocked = false;
    for(int i = 0; i < key_size; i++) {
        string key = keyitems.at(i);
        if(lockingkeys->count(key) != 0) {
            if(lockingkeys->at(key) != 0) // 非空且不等于0
            {
                isBlocked = true;
                break;
        }}
    }
    return isBlocked;
}

// 往阻塞队列中插入交易，更新被阻塞的状态
void BlockingTxQueue::insertTx(transactionWithReadWriteSet::Ptr tx){
    // 将交易访问的所有的本地读写集插入到lockingkeys中
    lock_guard<std::mutex> lock(queueLock);

    string localrwkeys = tx->localreadwriteKey; // 交易所阻塞的读写集，片内交易也可能访问多个状态，用_分开
    vector<string> localrwkeyitems;
    boost::split(localrwkeyitems, localrwkeys, boost::is_any_of("_"), boost::token_compress_on);
    int key_size = localrwkeyitems.size();

    // PLUGIN_LOG(INFO) << LOG_KV("跨片交易占用的本地读写集为", localrwkeys);

    for(int i = 0; i < key_size; i++) {
        string key = localrwkeyitems.at(i);
        if(lockingkeys->count(key) == 0) {
            lockingkeys->insert(make_pair(key, 1));
        }
        else {
            int lockNum = lockingkeys->at(key);
            lockingkeys->at(key) = lockNum + 1;
        }
    }

    txs->push(tx); // 将交易压入缓存队列
}

// 向队列尾批量添加交易
void BlockingTxQueue::insertTxs(shared_ptr<vector<transactionWithReadWriteSet::Ptr>> reorderedTxs){
    lock_guard<std::mutex> lock(queueLock);
    int tx_size = reorderedTxs->size();
    PLUGIN_LOG(INFO) << LOG_KV("一次性向队列中压入交易数目", tx_size);
    for(int i = 0; i < tx_size; i++){
        auto tx = reorderedTxs->at(i);

        string txid = tx->txid;
        if(txid == "a3084"){
            struct timeval tv;
            gettimeofday(&tv, NULL);
            long time_msec = tv.tv_sec*1000 + tv.tv_usec/1000;
            m_txid_to_endtime->insert(make_pair(txid, time_msec)); // 记录txid的开始时间
            PLUGIN_LOG(INFO) << LOG_KV("txid", txid) << LOG_KV("time_usec", time_msec);
        }

        string localrwkeys = tx->localreadwriteKey; // 交易所阻塞的读写集，片内交易也可能访问多个状态，用_分开
        vector<string> localrwkeyitems;
        boost::split(localrwkeyitems, localrwkeys, boost::is_any_of("_"), boost::token_compress_on);
        int key_size = localrwkeyitems.size();
        for(int j = 0; j < key_size; j++) {
            string key = localrwkeyitems.at(j);
            if(lockingkeys->count(key) == 0) {
                lockingkeys->insert(make_pair(key, 1));
            }
            else {
                int lockNum = lockingkeys->at(key);
                lockingkeys->at(key) = lockNum + 1;
            }
        }
        txs->push(tx); // 将交易压入缓存队列
    }
}

transactionWithReadWriteSet::Ptr BlockingTxQueue::frontTx(){
    lock_guard<std::mutex> lock(queueLock);
    if(txs->size() == 0){
        return nullptr;
    }
    else{
        auto tx = txs->front();
        return tx;
    }
}

// 交易执行完，将交易和相应的锁清除
void BlockingTxQueue::deleteTx(){
    lock_guard<std::mutex> lock(queueLock);

    auto tx = txs->front(); // 即将被pop的交易
    string localrwkeys = tx->localreadwriteKey; // 交易所阻塞的读写集
    vector<string> localrwkeyItems;
    boost::split(localrwkeyItems, localrwkeys, boost::is_any_of("_"), boost::token_compress_on);
    int key_size = localrwkeyItems.size();

    for(int i = 0; i < key_size; i++) {
        string key = localrwkeyItems.at(i);
        int lockNum = lockingkeys->at(key);
        lockingkeys->at(key) = lockNum - 1;
    }

    // 锁删除完毕，交易出队列
    txs->pop();
    last_failture_pop = 0;
}


// 将当前所有的交易转移到txQueue中
void BlockingTxQueue::batch_pop(shared_ptr<queue<transactionWithReadWriteSet::Ptr>> txQueue){
    lock_guard<std::mutex> lock(queueLock);
    int queue_size = txs->size();

    if(queue_size == 0){
        return;
    }
    else{
        PLUGIN_LOG(INFO) << LOG_KV("一次性从队列中取出交易数目", queue_size);
        for(int i = 0; i < queue_size; i++){
            auto tx = txs->front();
            txs->pop();
            txQueue->push(tx);
        }
    }
}


// 返回队列中当前缓存的交易数目
int BlockingTxQueue::txsize(){
    lock_guard<std::mutex> lock(queueLock);
    return txs->size();
}