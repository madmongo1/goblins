#pragma once

#include "config.hpp"
#include "run_pool.hpp"

struct worker_thread_service : asio::detail::service_base<worker_thread_service> {

    worker_thread_service(asio::io_service &owner)
            : asio::detail::service_base<worker_thread_service>(owner) {
        worker_pool_.add_thread();
    }

    auto get_worker_executor() -> asio::io_service&
    {
        return worker_executor_;
    }

    void shutdown_service() override
    {
        worker_pool_.stop();
    }

    asio::io_service worker_executor_;
    run_pool worker_pool_ {worker_executor_};

};
