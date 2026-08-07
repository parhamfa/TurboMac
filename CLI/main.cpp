#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace {

constexpr const char *kSocketPath = "/var/run/turbomacd.sock";

void usage(const char *program) {
    std::fprintf(
        stderr,
        "usage: %s status [--json]\n"
        "       %s arm|disarm|calibrate|validate|logs\n",
        program,
        program
    );
}

bool confirmation(const char *word, const char *warning) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::fprintf(stderr, "%s requires an interactive terminal\n", word);
        return false;
    }
    std::fprintf(stderr, "%s\nType %s to continue: ", warning, word);
    std::string input;
    std::getline(std::cin, input);
    return input == word;
}

int request(const std::string &command) {
    const int socketFD = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFD < 0) {
        std::perror("socket");
        return 1;
    }
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", kSocketPath);
    if (connect(socketFD, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        std::perror("connect turbodmacd");
        close(socketFD);
        return 1;
    }
    const std::string payload = command + "\n";
    if (write(socketFD, payload.data(), payload.size()) != (ssize_t)payload.size()) {
        std::perror("write");
        close(socketFD);
        return 1;
    }
    shutdown(socketFD, SHUT_WR);

    std::string response;
    char buffer[4096];
    for (;;) {
        const ssize_t count = read(socketFD, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("read");
            close(socketFD);
            return 1;
        }
        if (count == 0) {
            break;
        }
        response.append(buffer, (size_t)count);
    }
    close(socketFD);
    std::fwrite(response.data(), 1U, response.size(), stdout);
    return response.rfind("ERROR", 0U) == 0U ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    const std::string command = argv[1];
    if (command == "status") {
        if (argc == 2) {
            return request("status human");
        }
        if (argc == 3 && std::strcmp(argv[2], "--json") == 0) {
            return request("status json");
        }
        usage(argv[0]);
        return 2;
    }
    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }
    if (command == "arm" || command == "disarm" || command == "logs") {
        return request(command);
    }
    if (command == "calibrate") {
        if (!confirmation(
                "CALIBRATE",
                "Calibration applies an eight-thread AVX2 load. Stay physically present."
            )) {
            return 2;
        }
        return request("calibrate supervised");
    }
    if (command == "validate") {
        if (!confirmation(
                "VALIDATE",
                "Validation runs the fixed Whisper workload under the live governor."
            )) {
            return 2;
        }
        return request("validate supervised");
    }
    usage(argv[0]);
    return 2;
}
