#include "../Daemon/Policy.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool number(const std::string &line, const std::string &key, double *value) {
    const std::string marker = "\"" + key + "\":";
    const size_t position = line.find(marker);
    if (position == std::string::npos) {
        return false;
    }
    const char *start = line.c_str() + position + marker.size();
    if (std::strncmp(start, "null", 4U) == 0) {
        return false;
    }
    char *end = nullptr;
    const double parsed = std::strtod(start, &end);
    if (end == start || !std::isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: replay_observer samples.jsonl [...]\n";
        return 2;
    }
    PolicyConfig config;
    config.reserveW = 2.0;
    config.raplFloorW = 5.0;
    config.raplCeilingPL1W = 45.0;
    config.raplCeilingPL2W = 55.0;
    GovernorPolicy policy(config);
    uint64_t bands[4] = {};
    uint64_t samples = 0U;
    uint64_t rejected = 0U;
    double timeline = 0.0;

    for (int index = 1; index < argc; ++index) {
        std::ifstream input(argv[index]);
        if (!input) {
            std::cerr << "could not open " << argv[index] << "\n";
            return 1;
        }
        std::string line;
        double priorElapsed = -1.0;
        while (std::getline(input, line)) {
            double elapsedMS = 0.0;
            double inputW = 0.0;
            double capacityW = 0.0;
            double packageProxyW = 0.0;
            if (!number(line, "elapsed_ms", &elapsedMS)
                || !number(line, "input_power_w", &inputW)
                || !number(line, "input_limit_w", &capacityW)) {
                rejected++;
                continue;
            }
            number(line, "smc_pcpc_w", &packageProxyW);
            double relative = elapsedMS / 1000.0;
            if (priorElapsed >= 0.0 && relative < priorElapsed) {
                timeline += priorElapsed + 1.0;
            }
            const double now = timeline + relative;
            priorElapsed = relative;
            if (!policy.updateSMC(now, inputW, capacityW)
                || !policy.updatePackagePower(now, std::max(0.0, packageProxyW))) {
                rejected++;
                continue;
            }
            const PolicySnapshot snapshot = policy.evaluate(now);
            if (snapshot.ready) {
                bands[(unsigned)snapshot.band]++;
            }
            samples++;
        }
        timeline += priorElapsed > 0.0 ? priorElapsed + 1.0 : 1.0;
    }
    std::cout << "samples=" << samples
              << " rejected=" << rejected
              << " cruise=" << bands[0]
              << " guard=" << bands[1]
              << " shed=" << bands[2]
              << " emergency=" << bands[3] << "\n";
    return samples == 0U ? 1 : 0;
}
