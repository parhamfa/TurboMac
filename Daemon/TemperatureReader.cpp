#include "TemperatureReader.h"

#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

namespace {

bool setError(std::string *error, const std::string &message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

double monotonicSeconds() {
    timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

} // namespace

bool TemperatureReader::parsePowermetrics(
    const std::string &output,
    double *temperatureC,
    std::string *error
) {
    if (temperatureC == nullptr) {
        return setError(error, "temperature output pointer is null");
    }
    const std::string marker = "CPU die temperature:";
    const size_t position = output.find(marker);
    if (position == std::string::npos) {
        return setError(error, "powermetrics omitted CPU die temperature");
    }
    const char *start = output.c_str() + position + marker.size();
    errno = 0;
    char *end = nullptr;
    const double parsed = std::strtod(start, &end);
    if (errno != 0
        || end == start
        || !std::isfinite(parsed)
        || parsed < 0.0
        || parsed > 125.0) {
        return setError(error, "powermetrics returned an invalid CPU die temperature");
    }
    *temperatureC = parsed;
    return true;
}

bool TemperatureReader::readCPUDie(double *temperatureC, std::string *error) {
    int descriptors[2] = {-1, -1};
    if (pipe(descriptors) != 0) {
        return setError(error, "could not create powermetrics pipe");
    }
    fcntl(descriptors[0], F_SETFD, FD_CLOEXEC);
    fcntl(descriptors[1], F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        return setError(error, "could not initialize powermetrics spawn actions");
    }
    posix_spawn_file_actions_addclose(&actions, descriptors[0]);
    posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, descriptors[1]);

    const char *path = "/usr/bin/powermetrics";
    char *const arguments[] = {
        const_cast<char *>(path),
        const_cast<char *>("--samplers"),
        const_cast<char *>("smc"),
        const_cast<char *>("-n"),
        const_cast<char *>("1"),
        const_cast<char *>("-i"),
        const_cast<char *>("100"),
        nullptr,
    };
    pid_t process = -1;
    const int spawnResult = posix_spawn(
        &process, path, &actions, nullptr, arguments, environ
    );
    posix_spawn_file_actions_destroy(&actions);
    close(descriptors[1]);
    if (spawnResult != 0) {
        close(descriptors[0]);
        return setError(
            error, std::string("could not start powermetrics: ") + std::strerror(spawnResult)
        );
    }

    std::string output;
    const double deadline = monotonicSeconds() + 2.0;
    bool timedOut = false;
    for (;;) {
        const double remaining = deadline - monotonicSeconds();
        if (remaining <= 0.0) {
            timedOut = true;
            break;
        }
        pollfd descriptor = {.fd = descriptors[0], .events = POLLIN | POLLHUP, .revents = 0};
        const int pollResult = poll(&descriptor, 1, (int)(remaining * 1000.0));
        if (pollResult < 0 && errno == EINTR) {
            continue;
        }
        if (pollResult <= 0) {
            timedOut = true;
            break;
        }
        char buffer[4096];
        const ssize_t count = read(descriptors[0], buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        output.append(buffer, (size_t)count);
        if (output.size() > 65536U) {
            timedOut = true;
            break;
        }
    }
    close(descriptors[0]);
    if (timedOut) {
        kill(process, SIGKILL);
    }
    int status = 0;
    while (waitpid(process, &status, 0) < 0 && errno == EINTR) {}
    if (timedOut) {
        return setError(error, "powermetrics temperature read timed out");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return setError(error, "powermetrics temperature read failed");
    }
    return parsePowermetrics(output, temperatureC, error);
}
