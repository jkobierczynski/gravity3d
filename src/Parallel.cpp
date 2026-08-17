#include "Parallel.h"
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdlib>
#include <algorithm>

namespace {

class ThreadPool {
public:
    static ThreadPool& global() { static ThreadPool p; return p; }
    unsigned size() const { return n_; }

    void forRange(std::size_t n,
                  const std::function<void(std::size_t,std::size_t)>& fn,
                  std::size_t minChunk) {
        if (n == 0) return;
        if (n_ <= 1 || n <= minChunk) { fn(0, n); return; }

        // ~4 chunks per lane for load balancing.
        std::size_t target = (std::size_t)n_ * 4;
        std::size_t chunk  = std::max<std::size_t>(minChunk, (n + target - 1) / target);
        {
            std::lock_guard<std::mutex> lk(m_);
            fn_    = &fn;
            n_job_ = n;
            chunk_ = chunk;
            next_.store(0, std::memory_order_relaxed);
            done_.store(0, std::memory_order_relaxed);
            ++epoch_;
        }
        cvStart_.notify_all();
        runChunks();                       // caller participates
        std::unique_lock<std::mutex> lk(m_);
        cvDone_.wait(lk, [&]{ return done_.load() == (n_ - 1); });
        fn_ = nullptr;
    }

    ~ThreadPool() {
        { std::lock_guard<std::mutex> lk(m_); stop_ = true; ++epoch_; }
        cvStart_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

private:
    ThreadPool() {
        unsigned n = 0;
        if (const char* e = std::getenv("GRAVITY3D_THREADS")) { int v = std::atoi(e); if (v > 0) n = (unsigned)v; }
        if (n == 0) n = std::thread::hardware_concurrency();
        if (n == 0) n = 1;
        n_ = n;
        for (unsigned i = 1; i < n_; ++i) workers_.emplace_back([this]{ workerLoop(); });
    }

    void runChunks() {
        const auto& fn = *fn_;
        for (;;) {
            std::size_t i = next_.fetch_add(1, std::memory_order_relaxed);
            std::size_t b = i * chunk_;
            if (b >= n_job_) break;
            std::size_t e = std::min(n_job_, b + chunk_);
            fn(b, e);
        }
    }

    void workerLoop() {
        std::uint64_t local = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m_);
                cvStart_.wait(lk, [&]{ return stop_ || epoch_ != local; });
                if (stop_) return;
                local = epoch_;
            }
            runChunks();
            if (done_.fetch_add(1, std::memory_order_acq_rel) + 1 == (n_ - 1)) {
                std::lock_guard<std::mutex> lk(m_);
                cvDone_.notify_one();
            }
        }
    }

    unsigned n_ = 1;
    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable cvStart_, cvDone_;
    const std::function<void(std::size_t,std::size_t)>* fn_ = nullptr;
    std::size_t n_job_ = 0, chunk_ = 1;
    std::atomic<std::size_t> next_{0};
    std::atomic<unsigned>    done_{0};
    std::uint64_t epoch_ = 0;
    bool stop_ = false;
};

} // namespace

namespace parallel {

unsigned threadCount() { return ThreadPool::global().size(); }

void forRange(std::size_t n,
              const std::function<void(std::size_t,std::size_t)>& fn,
              std::size_t minChunk) {
    ThreadPool::global().forRange(n, fn, minChunk);
}

} // namespace parallel
