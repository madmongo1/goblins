//
// Created by Richard Hodges on 07/07/2017.
//

#pragma once

#include "config.hpp"
#include <thread>
#include <iostream>

struct run_pool {
    run_pool(asio::io_service &executor)
            : executor_(executor) {}

    ~run_pool() {
        stop();
    }

    void add_thread() {
        threads_.emplace_back([&, work = asio::io_service::work(executor_)] { this->run(); });
    }

    void stop() {
        executor_.stop();
        join_threads();
    }

    void join() {
        run();
        join_threads();
    }

private:

    void join_threads() {
        for (auto &&t : threads_) {
            if (t.joinable()) t.join();
        }
        threads_.clear();
    }

    void run() {
        while (!executor_.stopped()) {
            try {
                executor_.run();
            }
            catch (std::exception const &e) {
                std::cerr << e.what() << std::endl;
            }
        }
    }

    asio::io_service &executor_;
    std::vector<std::thread> threads_;
};
