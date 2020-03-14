//
// Created by harper on 3/14/20.
//

#include "executor.h"

namespace lqf {
    namespace executor {

        Executor::Executor(uint32_t pool_size) : shutdown_(false), pool_size_(pool_size), threads_() {
            for (uint32_t i = 0; i < pool_size; ++i) {
                threads_.push_back(unique_ptr<thread>(new std::thread(bind(&Executor::routine, this))));
            }
        }

        Executor::~Executor() {
            threads_.clear();
        }

        void Executor::shutdown() {
            shutdown_ = true;
            /// Wake up threads waiting for tasks
            for (uint32_t i = 0; i < pool_size_; ++i) {
                has_task_.notify();
            }
        }

        shared_ptr<Executor> Executor::Make(uint32_t psize) {
            return make_shared<Executor>(psize);
        }

        void Executor::submit(shared_ptr<Task> task) {
            fetch_task_.lock();
            tasks_.push(task);
            has_task_.notify();
            fetch_task_.unlock();
        }

        shared_ptr<Future> Executor::submit(function<void()> runnable) {
            shared_ptr<Task> task = make_shared<Task>(runnable);
            submit(task);
            return task;
        }

        void Executor::routine() {
            while (!shutdown_) {
                has_task_.wait();

                fetch_task_.lock();
                if (tasks_.empty()) {
                    fetch_task_.unlock();
                } else {
                    auto task = tasks_.front();
                    tasks_.pop();
                    fetch_task_.unlock();

                    task->run();
                    task->signal_->notify();
                }
            }
        }
    }
}