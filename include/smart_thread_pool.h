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
#include <cassert>
#include <string>
#include <queue>
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <future>
#include <chrono>

namespace stp {

/*
 *                                                    
 *                              \ Workers .../                            |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\
 *                      |-----ClassifyThreadPool ---->TaskPriorityQueue-->| UrgentTask HighTask MediumTask  \
 *                      |                                                 |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\
 *                      |               
 * SmartThreadPool ---->|       \ Workers .../                            |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\
 *                      |-----ClassifyThreadPool ---->TaskPriorityQueue-->| MediumTask LowTask DefaultTask  \
 *                      |                                                 |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\
 *                      |                              
 *                      |       \ Workers ... /                           |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\
 *                      |-----ClassifyThreadPool ---->TaskPriorityQueue-->| UrgentTask LowTask DefaultTask  \
 *                                                                        |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\
 * 
 *
*/


class SmartThreadPool;
class ClassifyThreadPool;
class TaskPriorityQueue;
class Task;
// class Monitor;

enum TaskPriority : unsigned char {
  DEFAULT = 0,
  LOW,
  MEDIUM,
  HIGH,
  URGENT,
};

namespace {

uint8_t g_auto_increment_thread_pool_id = 0;

}   // namespace

class Task {
 public:
  using TaskType = std::function<void()>;
  Task(TaskType task, TaskPriority priority)
    : task_(task), priority_(priority) {
  }
  Task(const Task& other)
    : task_(other.task_), priority_(other.priority_) {
  }
  Task& operator=(const Task& other) {
    task_ = other.task_;
    priority_ = other.priority_;
    return *this;
  }

  bool operator<(const Task& rhs) const {
    return priority_ < rhs.priority_;
  }
  bool operator>(const Task& rhs) const {
    return priority_ > rhs.priority_;
  }

  TaskPriority priority() const { return priority_; }
  void Run() {
    task_();
  }
 private:
  TaskType task_;
  TaskPriority priority_;
};

class TaskPriorityQueue {
 public:
  TaskPriorityQueue(const char* queue_name, uint8_t thread_pool_id = 0)
    : queue_name_(queue_name), thread_pool_id_(thread_pool_id), alive_(true) {
  }
  TaskPriorityQueue(TaskPriorityQueue&& other) = delete;
  TaskPriorityQueue(const TaskPriorityQueue&) = delete;
  TaskPriorityQueue& operator=(TaskPriorityQueue&& other) = delete;
  TaskPriorityQueue& operator=(const TaskPriorityQueue&) = delete;
  ~TaskPriorityQueue() {
    ClearQueue();
  }
  void ClearQueue() {
    {
      std::unique_lock<std::mutex> lock(queue_mtx_);
      alive_ = false;
    }
    queue_cv_.notify_all();
    auto task = dequeue();
    while (task) {
      task->Run();
      task = dequeue();
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
  auto enqueue(TaskPriority priority, F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    using ReturnType = typename std::result_of<F(Args...)>::type;
    auto task = std::make_shared< std::packaged_task<ReturnType()> >(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    {
      std::unique_lock<std::mutex> lock(queue_mtx_);
      tasks_.emplace([task](){ (*task)(); }, priority);
    }
    queue_cv_.notify_one();
    return task->get_future();
  }
  std::unique_ptr<Task> dequeue() {
    std::unique_lock<std::mutex> lock(queue_mtx_);
    queue_cv_.wait(lock, [this]{ return !alive_ || !tasks_.empty(); });
    if (!alive_ && tasks_.empty()) {
      return nullptr;
    }
    auto task = std::unique_ptr<Task>{new Task(std::move(tasks_.top()))};
    tasks_.pop();
    return task;
  }
  const char* name() const { return queue_name_.c_str(); }
  uint8_t pool_id() const { return thread_pool_id_; }

 private:
  std::string queue_name_;
  uint8_t thread_pool_id_;
  std::priority_queue<Task> tasks_;
  mutable std::mutex queue_mtx_;
  mutable std::condition_variable queue_cv_;
  std::atomic_bool alive_;
};

class Worker {
 public:
  void Work() {

  }
 private:
  uint64_t completed_task_count_;
};

class ClassifyThreadPool {
 public:
  ClassifyThreadPool(const char* name, uint16_t capacity) 
    : id_(++g_auto_increment_thread_pool_id), name_(name), capacity_(capacity) {
    workers_.reserve(capacity);
    ConnectTaskPriorityQueue();
  }

  ClassifyThreadPool(ClassifyThreadPool&&) = delete;
  ClassifyThreadPool(const ClassifyThreadPool&) = delete;
  ClassifyThreadPool& operator=(ClassifyThreadPool&&) = delete;
  ClassifyThreadPool& operator=(const ClassifyThreadPool&) = delete;

  void InitWorkers(uint16_t count) {
    for (unsigned char i = 0; i < count && workers_.size() < capacity_; ++i) {
      AddWorker();
    }
  }
  void StartWorkers() {
    for (auto&& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }
  uint8_t id() { return id_; }
 private:
  friend class SmartThreadPool;
  void ConnectTaskPriorityQueue() {
    std::string queue_name = name_ + "-->TaskQueue";
    queue_ = std::unique_ptr<TaskPriorityQueue>{new TaskPriorityQueue(queue_name.c_str(), id_)};
  }
  void AddWorker() {
    workers_.emplace_back(
      [this]() {
        while (true) {
          auto task = queue_->dequeue();
          if (task) {
            task->Run();
          } else {
            return;
          }
        }
      }
    );
  }
  uint8_t id_;
  std::string name_;
  uint16_t capacity_;
  std::vector<std::thread> workers_;
  std::unique_ptr<TaskPriorityQueue> queue_;
};

class SmartThreadPool {
 public:
  SmartThreadPool(SmartThreadPool&&) = delete;
  SmartThreadPool(const SmartThreadPool&) = delete;
  SmartThreadPool& operator=(SmartThreadPool&&) = delete;
  SmartThreadPool& operator=(const SmartThreadPool&) = delete;

  template<class F, class... Args>
  auto ApplyAsync(const char* pool_name, TaskPriority priority,
                  F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    return pools_.at(pool_name)->queue_->enqueue(priority, f, args...);
  }
  void StartAllWorkers() {
    for (auto&& pool : pools_) {
      pool.second->StartWorkers();
    }
  }
 private:
  friend class SmartThreadPoolBuilder;
  SmartThreadPool() {}
  std::map<std::string, std::unique_ptr<ClassifyThreadPool> > pools_;
};

class SmartThreadPoolBuilder {
 public:
  SmartThreadPoolBuilder() 
    : smart_pool_(new SmartThreadPool) {
  }

  SmartThreadPoolBuilder(SmartThreadPoolBuilder&&) = delete;
  SmartThreadPoolBuilder(const SmartThreadPoolBuilder&) = delete;
  SmartThreadPoolBuilder& operator=(SmartThreadPoolBuilder&&) = delete;
  SmartThreadPoolBuilder& operator=(const SmartThreadPoolBuilder&) = delete;

  SmartThreadPoolBuilder& AddClassifyPool(const char* pool_name, uint8_t capacity, uint8_t init_size) {
    auto pool = new ClassifyThreadPool(pool_name, capacity);
    pool->InitWorkers(init_size);
    smart_pool_->pools_.emplace(pool_name, pool);
    return *this;
  }
  std::unique_ptr<SmartThreadPool> BuildAndInit() {
    return std::move(smart_pool_);
  }
 private:
  std::unique_ptr<SmartThreadPool> smart_pool_;
};

}   // namespace stp

#endif  // SMART_THREAD_POOL_H_
