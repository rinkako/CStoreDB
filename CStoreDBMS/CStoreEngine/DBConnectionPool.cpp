#include "DBConnectionPool.h"
#include "DBBridge.h"
#ifdef _WIN32
  #include <Windows.h>
#else
  #include <system.h>
#endif

CSTORE_NS_BEGIN

// 工厂方法，获得类的唯一实例
DBConnectionPool* DBConnectionPool::GetInstance() {
  return DBConnectionPool::instance == nullptr ? 
    DBConnectionPool::instance = new DBConnectionPool() : DBConnectionPool::instance;
}

// 私有的构造器
DBConnectionPool::DBConnectionPool()
  :DBObject("DBConnectionPool", this) {
  this->ProcCounter = 0;
  this->quitFlag = false;
  this->isDebug = false;
}

// 析构器
DBConnectionPool::~DBConnectionPool() {
  TRACE("DBConnectionPool is going to collapse, all transactions will be TERMINATED!");
  // 阻塞队列防止新事务进入
  this->queueMutex.lock();
  // 立即结束全部事务
  this->quitFlag = true;
  this->ProcCounter = 0;
  // 释放资源
  this->threadPool.clear();
  this->ClearQueue();
  for (int i = 0; i < this->finishedTransactionVector.size(); i++) {
    if (this->finishedTransactionVector[i] != nullptr) {
      delete this->finishedTransactionVector[i];
    }
  }
  this->finishedTransactionVector.clear();
  this->queueMutex.unlock();
  TRACE("DBConnectionPool is already collapsed");
}

// 开启连接池
void DBConnectionPool::Init() {
  TRACE("DBConnectionPool began to accept transaction." << NEWLINE);
  for (int i = 0; i < CONNECTORLIMIT; i++) {
    this->threadPool.push_back(std::thread(DBConnectionPool::TransactionHandler, i));
  }
}

// 将事务提交给数据库引擎
void DBConnectionPool::Commit(const std::string query) {
  queueMutex.lock();
  DBTransaction* trans = new DBTransaction(query);
  this->transactionQueue.push(trans);
  queueMutex.unlock();
}

// 强行中断所有事务并清空队列
void DBConnectionPool::KillAll() {
  TRACE("Killing all processing transaction");
  // 阻塞队列防止新事务进入
  this->queueMutex.lock();
  // 立即结束全部事务
  this->quitFlag = true;
  this->ProcCounter = 0;
  // 释放资源
  this->threadPool.clear();
  this->ClearQueue();
  this->queueMutex.unlock();
  TRACE("Killed all processing transaction");
  // 重新开放连接
  this->quitFlag = false;
  for (int i = 0; i < this->HandleNum; i++) {
    this->threadPool.push_back(std::thread(DBConnectionPool::TransactionHandler, i));
  }
}

// 清空事务队列
void DBConnectionPool::ClearQueue() {
  queueMutex.lock();
  int dropTransaction = 0;
  while (!this->transactionQueue.empty()) {
    DBTransaction* t = this->transactionQueue.front();
    if (t != nullptr) {
      delete t;
    }
    this->transactionQueue.pop();
    dropTransaction++;
  }
  char buf[16];
  _itoa(dropTransaction, buf, 10);
  TRACE("Droped " + std::string(buf) + " unsolve transaction");
  queueMutex.unlock();
}

// 事务队列中排队的数量
int DBConnectionPool::CountQueue() {
  int retNum;
  queueMutex.lock();
  retNum = this->transactionQueue.size();
  queueMutex.unlock();
  return retNum;
}

// 正在进行的事务数量
int DBConnectionPool::CountProcessing() {
  int retNum;
  queueMutex.lock();
  retNum = this->ProcCounter;
  queueMutex.unlock();
  return retNum;
}

// 获取已完成的事务说明
std::string DBConnectionPool::ShowFinishedTransaction() {
  CSCommonUtil::StringBuilder sb;
  for (int i = 0; i < this->finishedTransactionVector.size(); i++) {
    if (this->finishedTransactionVector[i] != nullptr) {
      sb.Append(this->finishedTransactionVector[i]->ToString()).Append(NEWLINE);
    }
  }
  return sb.ToString();
}

// 获取正在进行的事务说明
std::string DBConnectionPool::ShowProcessingTransaction() {
  CSCommonUtil::StringBuilder sb;
  for (int i = 0; i < this->processingTransactionVector.size(); i++) {
    if (this->processingTransactionVector[i] != nullptr) {
      sb.Append(this->processingTransactionVector[i]->ToString()).Append(NEWLINE);
    }
  }
  return sb.ToString();
}

// 获取全部的事务说明
std::string DBConnectionPool::ShowTransaction() {
  CSCommonUtil::StringBuilder sb;
  sb.Append(">Total Thread Num: ").Append((int)this->threadPool.size()).Append(NEWLINE);
  sb.Append(">Pending Count: ").Append((int)this->transactionQueue.size()).Append(NEWLINE).Append(NEWLINE);
  sb.Append(">Processing:").Append(NEWLINE);
  sb.Append(this->ShowProcessingTransaction()).Append(NEWLINE);
  sb.Append(">Finished:").Append(NEWLINE);
  sb.Append(this->ShowFinishedTransaction());
  return sb.ToString();
}

// 事务处理器
void DBConnectionPool::TransactionHandler(int id) {
  DBConnectionPool* core = DBConnectionPool::GetInstance();
  while (true) {
    // 如果主线程要求退出
    if (core->quitFlag) {
      TRACE("Thread forced to exit");
      return;
    }
    // 从队列中取未处理的事务
    DBTransaction* proTrans = nullptr;
    core->queueMutex.lock();
    if (core->transactionQueue.size() > 0) {
      proTrans = core->transactionQueue.front();
      core->transactionQueue.pop();
    }
    else {
      core->queueMutex.unlock();
#ifdef _WIN32
      Sleep(1);
#else
      usleep(1);
#endif
      continue;
    }
    core->processingTransactionVector.push_back(proTrans);
    core->ProcCounter++;
    core->queueMutex.unlock();
    // 处理这个事务
    if (proTrans != nullptr) {
      proTrans->SetHandleId(id);
      DBBridge* IBridge = new DBBridge();
      IBridge->StartTransaction(*proTrans, core->isDebug);
      proTrans->Finish();
      core->queueMutex.lock();
      for (std::vector<DBTransaction*>::iterator iter = core->processingTransactionVector.begin();
        iter != core->processingTransactionVector.end(); iter++) {
        if ((*iter)->ReferenceEquals(proTrans)) {
          core->processingTransactionVector.erase(iter);
          break;
        }
      }
      core->finishedTransactionVector.push_back(proTrans);
      core->ProcCounter--;
      core->queueMutex.unlock();
      PILEPRINTLN("      Query OK, cost: " + proTrans->GetDuration() + " sec(s)");
      PILEPRINT("    >");
    }
  }
}

// 设置线程数
void DBConnectionPool::SetThreadNum(int tnum) {
  // 还有处理中的事务时禁止改变
  queueMutex.lock();
  if (this->transactionQueue.size() != 0 ||
    this->processingTransactionVector.size() != 0) {
    encounterMutex.lock();
    if (this->ProcCounter != 0) {
      TRACE("Transaction is processing or transaction queue not empty, cannot resize thread number");
      return;
    }
    encounterMutex.unlock();
  }
  // 异常值和不变
  if (this->threadPool.size() == tnum || tnum < 1) {
    TRACE("thread num not change");
    return;
  }
  // 修改记录
  this->HandleNum = tnum;
  // 添加的情况
  if (this->threadPool.size() < tnum) {
    int tp = threadPool.size();
    for (int i = 0; i < tnum - tp; i++) {
      this->threadPool.push_back(std::thread(DBConnectionPool::TransactionHandler, i));
    }
  }
  // 删除的情况
  else {
    this->threadPool.erase(this->threadPool.begin() + tnum, this->threadPool.end());
  }
  queueMutex.unlock();
}

// 设置DEBUG状态
void DBConnectionPool::SetDebug(bool state) {
  this->isDebug = state;
}

// 唯一实例
DBConnectionPool* DBConnectionPool::instance = nullptr;

CSTORE_NS_END