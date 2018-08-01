/****************************************************************************\
 * Created on Wed Aug 01 2018
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

#include "smart_thread_pool.h"
#include "gtest/gtest.h"
#include <vector>
#include <thread>
#include <chrono>

// TEST_F(TaskQueueTestFixture, pressure_test_multithread_operate_queue) {
//   auto func = [this](int count){
//     std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     if (count == 9999) {
//       high_queue_.ClearQueue();
//     } 
//   };
//   auto enqueue_entry = [this, func]() {
//     for (int i = 0; i < 100000; ++i) {
//       high_queue_.enqueue(func, i);
//     }
//   };
//   auto handle_entry = [this]() {
//     auto task = high_queue_.dequeue();
//     while (task) {
//       task();
//       task = high_queue_.dequeue();
//     }
//   };
//   std::vector<std::thread> tv;
//   tv.emplace_back(enqueue_entry);
//   for (int i = 0; i < 128; ++i) {
//     tv.emplace_back(handle_entry);
//   }
//   auto start = std::chrono::steady_clock::now();
//   for (auto&& t : tv) {
//     t.join();
//   }
//   auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
//   EXPECT_LT(duration.count(), 1500);
//   EXPECT_EQ(high_queue_.size(), 0);
// }