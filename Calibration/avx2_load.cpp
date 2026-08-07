#include <immintrin.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <time.h>
#include <unistd.h>

namespace {

std::atomic<bool> stopRequested(false);
std::atomic<unsigned> readyThreads(0U);
std::atomic<bool> startWork(false);
volatile double resultSink = 0.0;

void handleSignal(int) {
    stopRequested.store(true, std::memory_order_relaxed);
}

double monotonicSeconds() {
    timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

bool parseUnsigned(const char *input, unsigned *value) {
    if (input == nullptr || input[0] == '-') {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(input, &end, 10);
    if (errno != 0 || end == input || *end != '\0' || parsed > 3600UL) {
        return false;
    }
    *value = (unsigned)parsed;
    return true;
}

void worker(double deadline) {
    __m256d a = _mm256_set_pd(1.001, 1.002, 1.003, 1.004);
    __m256d b = _mm256_set_pd(0.999, 0.998, 0.997, 0.996);
    __m256d c = _mm256_set_pd(0.101, 0.202, 0.303, 0.404);
    __m256d d = _mm256_set_pd(0.505, 0.606, 0.707, 0.808);

    readyThreads.fetch_add(1U, std::memory_order_release);
    while (!startWork.load(std::memory_order_acquire)
           && !stopRequested.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
    }

    uint64_t iterations = 0U;
    while (!stopRequested.load(std::memory_order_relaxed)
           && monotonicSeconds() < deadline) {
        for (unsigned index = 0U; index < 200000U; ++index) {
            a = _mm256_fmadd_pd(a, b, c);
            c = _mm256_fnmadd_pd(c, d, a);
            b = _mm256_fmadd_pd(b, d, _mm256_set1_pd(0.0000001));
            d = _mm256_fnmadd_pd(d, a, _mm256_set1_pd(-0.0000001));
        }
        iterations++;
        if ((iterations & 7U) == 0U) {
            alignas(32) double values[4];
            _mm256_store_pd(values, _mm256_add_pd(a, c));
            resultSink = values[0] + values[1] + values[2] + values[3];
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    unsigned seconds = 0U;
    unsigned threads = 8U;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--seconds") == 0
            && index + 1 < argc
            && parseUnsigned(argv[++index], &seconds)) {
            continue;
        }
        if (std::strcmp(argv[index], "--threads") == 0
            && index + 1 < argc
            && parseUnsigned(argv[++index], &threads)) {
            continue;
        }
        std::fprintf(stderr, "usage: %s --seconds N [--threads 8]\n", argv[0]);
        return 2;
    }
    if (seconds < 1U || seconds > 600U || threads != 8U) {
        std::fputs("seconds must be 1-600 and threads must be exactly 8\n", stderr);
        return 2;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    const double deadline = monotonicSeconds() + (double)seconds;
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned index = 0U; index < threads; ++index) {
        workers.emplace_back(worker, deadline);
    }
    while (readyThreads.load(std::memory_order_acquire) != threads) {
        std::this_thread::yield();
    }
    startWork.store(true, std::memory_order_release);
    for (std::thread &thread : workers) {
        thread.join();
    }
    return stopRequested.load(std::memory_order_relaxed) ? 130 : 0;
}
