/****************************************************************************\
 * Created on Sat Jul 28 2018
 * 
 * The MIT License (MIT)
 * Copyright (c) 2018 leosocy
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the ",Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED ",AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
\*****************************************************************************/

#ifndef SMART_THREAD_POOL_H_
#define SMART_THREAD_POOL_H_

#include <cstdint>
#include <queue>
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <future>

namespace stp {

/*
 *                                                    |~~~~~| 
 *                              \ Workers .../        |  D  | <-----TaskQueue  (Task1)  (Task2) (Task3)
 *                      |-----ClassifyThreadPool ---->|  i  | <-----TaskQueue  (Task1)  (Task2)
 *                      |                             |  s  | <-----TaskQueue  (Task1)
 *                      |       \ Workers .../        |  p  | 
 * SmartThreadPool ---->|-----ClassifyThreadPool ---->|  a  | <-----TaskQueue  (Task1)  (Task2) (Task3)  (Task4)
 *                      |                             |  t  | <-----TaskQueue
 *                      |       \ Workers ... /       |  c  | 
 *                      |-----ClassifyThreadPool ---->|  h  | <-----TaskQueue  (Task1)
 *                                                    |  e  | <-----TaskQueue  (Task1)  (Task2)
 *                                                    |  r  | <-----TaskQueue  (Task1)
 *                                                    |~~~~~| 
 * 
*/

// class SmartThreadPool;

class ClassifyThreadPool;
// class Worker;

// Tasks in the `TaskQueue` have the same priority.
// Why not push all different priority tasks in a priority_queue?
// If different priority tasks use the same queue,
// the time complexity of a task `push` is O(n), `pop` is O(1).
// If tasks with same priority use same queue,
// the time complexity of a task `push` is O(1), `pop` is O(1).
// Therefore, in order to enable the `Dispather` to efficiently dispatch tasks,
// it is necessary to put tasks into the queue according to priority.
class TaskQueue;
class TaskDispatcher;
// class Monitor;

enum TaskQueuePriority : unsigned char {
  DEFAULT = 0,
  LOW,
  MEDIUM,
  HIGH,
  URGENT,
};
namespace {

unsigned char TaskQueuePriorityEnumSize = TaskQueuePriority::URGENT;
uint8_t g_auto_increment_thread_pool_id = 0;

}   // namespace

template<class F, class... Args>
class Task {
 public:
  using ReturnType = typename std::result_of<F(Args...)>::type;
  Task(F&& f, Args&&... args)
    : task_(std::make_shared< std::packaged_task<ReturnType()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...))) {
  }
  void Run() { (*task_)(); }

  Task(Task&&) = delete;
  Task& operator=(Task&&) = delete;
 private:
  friend class TaskQueue;
  std::shared_ptr< std::packaged_task<ReturnType()> > task_;
};

class TaskQueue {
 public:
  using FuctionType = std::function<void()>;
  using QueueType = std::queue<FuctionType>;
  TaskQueue(const char* queue_name, TaskQueuePriority priority = TaskQueuePriority::DEFAULT, uint8_t thread_pool_id = 0)
    : queue_name_(queue_name), priority_(priority), thread_pool_id_(thread_pool_id), alive_(true) {
  }
  TaskQueue(TaskQueue&&) = delete;
  TaskQueue& operator=(TaskQueue&&) = delete;
  ~TaskQueue() {
    ClearQueue();
  }
  void ClearQueue() {
    std::unique_lock<std::mutex> lock(queue_mtx_);
    alive_ = false;
    queue_cv_.notify_all();
    lock.unlock();
    auto task = std::move(dequeue());
    while (task) {
      task();
      task = std::move(dequeue());
    }
  }

  bool empty() const {
    std::unique_lock<std::mutex> lock(queue_mtx_);
    return tasks_.empty();
  }
  std::size_t size() const {
    std::unique_lock<std::mutex> lock(queue_mtx_);
    return tasks_.size();
  }
  template<class F, class... Args>
  auto enqueue(F&& f, Args&&... args) 
      -> std::future<typename std::result_of<F(Args...)>::type> {
    std::unique_lock<std::mutex> lock(queue_mtx_);
    // Don't use smart pointer here, because it will destruct when `enqueue` function exit.
    // And the lambda will be Dangling references.
    // So we alloc a task, and delete it when task run complete.
    auto task = new Task<F, Args...>(f, args...);
    tasks_.emplace([this, task]() {
      if (alive_) {
        task->Run();
      }
      delete task;
    });
    queue_cv_.notify_one();
    return task->task_->get_future();
  }
  FuctionType dequeue() {
    std::unique_lock<std::mutex> lock(queue_mtx_);
    queue_cv_.wait(lock, [this]{ return !alive_ || !tasks_.empty(); });
    if (!alive_ && tasks_.empty()) {
      return nullptr;
    }
    auto task_func = std::move(tasks_.front());
    tasks_.pop();
    return task_func;
  }

 private:
  std::string queue_name_;
  TaskQueuePriority priority_;
  uint8_t thread_pool_id_;
  QueueType tasks_;
  mutable std::mutex queue_mtx_;
  mutable std::condition_variable queue_cv_;
  bool alive_;
};

class ClassifyThreadPool {
 public:
  ClassifyThreadPool(const char* name, uint16_t capacity, uint16_t init_size) 
    : id_(++g_auto_increment_thread_pool_id), name_(name){
    workers_.reserve(capacity);
  }

  ClassifyThreadPool(ClassifyThreadPool&&) = delete;
  ClassifyThreadPool& operator=(ClassifyThreadPool&&) = delete;
 private:
  void AddQueue(const char* name, TaskQueuePriority priority) {
    queues_.emplace(name, std::make_shared<TaskQueue>(name, priority, id_));
  }
  void Work() {

  }
  uint8_t id_;
  std::string name_;
  std::vector<std::thread> workers_;
  std::map<std::string, std::shared_ptr<TaskQueue> > queues_;
};

class TaskDispatcher {
 public:
  static TaskDispatcher& instance() {
    static std::shared_ptr<TaskDispatcher> inst{new TaskDispatcher};
    return *inst.get();
  }
  TaskDispatcher(TaskDispatcher&&) = delete;
  TaskDispatcher& operator=(TaskDispatcher&&) = delete;
  TaskQueue::FuctionType NextTask(std::shared_ptr<ClassifyThreadPool> pool) {

  }
 private:
  TaskDispatcher() {};
};

}   // namespace stp

#endif  // SMART_THREAD_POOL_H_
