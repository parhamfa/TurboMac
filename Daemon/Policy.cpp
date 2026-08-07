#include "Policy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr double kUnset = -1.0;
constexpr double kInputWindowSeconds = 1.0;
constexpr double kCapacityWindowSeconds = 10.0;
constexpr double kTelemetryFreshSeconds = 2.0;
constexpr double kNonCPURiseTauSeconds = 0.25;
constexpr double kNonCPUFallTauSeconds = 5.0;

double clampValue(double value, double low, double high) {
    return std::max(low, std::min(value, high));
}

} // namespace

GovernorPolicy::GovernorPolicy(const PolicyConfig &config) {
    reset(config);
}

void GovernorPolicy::reset(const PolicyConfig &config) {
    config_ = config;
    snapshot_ = {};
    inputSamples_.clear();
    capacitySamples_.clear();
    lastSMCTime_ = kUnset;
    lastPackageTime_ = kUnset;
    lastNonCPUTime_ = kUnset;
    guardHighSince_ = kUnset;
    shedHighSince_ = kUnset;
    belowShedSince_ = kUnset;
    belowGuardSince_ = kUnset;
    belowCruiseSince_ = kUnset;
    lastIncreaseTime_ = kUnset;
    nonCPUInitialized_ = false;
    limitInitialized_ = false;
}

bool GovernorPolicy::updateSMC(double nowSeconds, double inputW, double capacityW) {
    if (!std::isfinite(nowSeconds)
        || nowSeconds < 0.0
        || (lastSMCTime_ != kUnset && nowSeconds < lastSMCTime_)
        || !validPower(inputW, 250.0)
        || !validPower(capacityW, 250.0)
        || capacityW < 10.0) {
        return false;
    }
    inputSamples_.push_back({nowSeconds, inputW});
    capacitySamples_.push_back({nowSeconds, capacityW});
    lastSMCTime_ = nowSeconds;
    snapshot_.inputW = inputW;
    expireSamples(nowSeconds);
    updateThresholds();
    return true;
}

bool GovernorPolicy::updatePackagePower(double nowSeconds, double packageW) {
    if (!std::isfinite(nowSeconds)
        || nowSeconds < 0.0
        || (lastPackageTime_ != kUnset && nowSeconds < lastPackageTime_)
        || !validPower(packageW, 200.0)) {
        return false;
    }
    lastPackageTime_ = nowSeconds;
    snapshot_.packageW = packageW;
    return true;
}

PolicySnapshot GovernorPolicy::evaluate(double nowSeconds) {
    if (!std::isfinite(nowSeconds) || nowSeconds < 0.0) {
        snapshot_.ready = false;
        return snapshot_;
    }
    expireSamples(nowSeconds);
    updateThresholds();
    snapshot_.filteredInputW = filteredInput();
    snapshot_.ready = !inputSamples_.empty()
        && !capacitySamples_.empty()
        && sampleFresh(nowSeconds, lastSMCTime_, kTelemetryFreshSeconds)
        && sampleFresh(nowSeconds, lastPackageTime_, kTelemetryFreshSeconds)
        && snapshot_.capacityW > 0.0;
    if (!snapshot_.ready) {
        return snapshot_;
    }

    updateNonCPU(nowSeconds);
    const double railEstimate = snapshot_.packageW + snapshot_.nonCPUW;
    const double calibratedEstimate = config_.packageToInputSlope
        * snapshot_.packageW + config_.packageToInputInterceptW;
    snapshot_.predictedInputW = std::max(railEstimate, calibratedEstimate)
        + config_.reserveW;
    updateBand(nowSeconds);
    updateLimits(nowSeconds);
    snapshot_.nonCPUOverBudget = snapshot_.band == GovernorBand::Emergency
        && snapshot_.pl1W <= config_.raplFloorW + 0.05
        && (snapshot_.inputW >= snapshot_.emergencyW
            || snapshot_.predictedInputW >= snapshot_.emergencyW);
    return snapshot_;
}

const PolicySnapshot &GovernorPolicy::snapshot() const {
    return snapshot_;
}

const char *GovernorPolicy::bandName(GovernorBand band) {
    switch (band) {
        case GovernorBand::Cruise:
            return "cruise";
        case GovernorBand::Guard:
            return "guard";
        case GovernorBand::Shed:
            return "shed";
        case GovernorBand::Emergency:
            return "emergency";
    }
    return "unknown";
}

bool GovernorPolicy::validPower(double value, double maximum) {
    return std::isfinite(value) && value >= 0.0 && value <= maximum;
}

double GovernorPolicy::median(std::deque<TimedValue> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::vector<double> sorted;
    sorted.reserve(values.size());
    for (const TimedValue &sample : values) {
        sorted.push_back(sample.value);
    }
    std::sort(sorted.begin(), sorted.end());
    const size_t middle = sorted.size() / 2U;
    return sorted.size() % 2U == 0U
        ? (sorted[middle - 1U] + sorted[middle]) / 2.0
        : sorted[middle];
}

double GovernorPolicy::elapsedOrZero(double now, double start) {
    return start == kUnset || now < start ? 0.0 : now - start;
}

void GovernorPolicy::updateCondition(bool condition, double now, double *since) {
    if (!condition) {
        *since = kUnset;
    } else if (*since == kUnset) {
        *since = now;
    }
}

void GovernorPolicy::expireSamples(double now) {
    while (!inputSamples_.empty()
           && now - inputSamples_.front().time > kInputWindowSeconds) {
        inputSamples_.pop_front();
    }
    while (!capacitySamples_.empty()
           && now - capacitySamples_.front().time > kCapacityWindowSeconds) {
        capacitySamples_.pop_front();
    }
}

void GovernorPolicy::updateThresholds() {
    snapshot_.capacityW = median(capacitySamples_);
    snapshot_.guardW = kGuardRatio * snapshot_.capacityW;
    snapshot_.shedW = kShedRatio * snapshot_.capacityW;
    snapshot_.emergencyW = kEmergencyRatio * snapshot_.capacityW;
}

void GovernorPolicy::updateNonCPU(double now) {
    const double observation = std::max(0.0, snapshot_.inputW - snapshot_.packageW);
    if (!nonCPUInitialized_) {
        snapshot_.nonCPUW = observation;
        nonCPUInitialized_ = true;
        lastNonCPUTime_ = now;
        return;
    }
    const double elapsed = clampValue(now - lastNonCPUTime_, 0.0, 2.0);
    const double tau = observation > snapshot_.nonCPUW
        ? kNonCPURiseTauSeconds
        : kNonCPUFallTauSeconds;
    const double alpha = elapsed <= 0.0 ? 0.0 : 1.0 - std::exp(-elapsed / tau);
    snapshot_.nonCPUW += alpha * (observation - snapshot_.nonCPUW);
    snapshot_.nonCPUW = std::max(0.0, snapshot_.nonCPUW);
    lastNonCPUTime_ = now;
}

void GovernorPolicy::updateBand(double now) {
    const bool emergency = snapshot_.inputW >= snapshot_.emergencyW
        || snapshot_.predictedInputW >= snapshot_.emergencyW;
    if (emergency) {
        snapshot_.band = GovernorBand::Emergency;
        belowShedSince_ = kUnset;
        belowGuardSince_ = kUnset;
        belowCruiseSince_ = kUnset;
        return;
    }

    updateCondition(snapshot_.filteredInputW >= snapshot_.guardW, now, &guardHighSince_);
    updateCondition(snapshot_.filteredInputW >= snapshot_.shedW, now, &shedHighSince_);

    switch (snapshot_.band) {
        case GovernorBand::Cruise:
            if (elapsedOrZero(now, shedHighSince_) >= 1.0) {
                snapshot_.band = GovernorBand::Shed;
            } else if (elapsedOrZero(now, guardHighSince_) >= 2.0) {
                snapshot_.band = GovernorBand::Guard;
            }
            break;
        case GovernorBand::Guard:
            if (elapsedOrZero(now, shedHighSince_) >= 1.0) {
                snapshot_.band = GovernorBand::Shed;
                belowCruiseSince_ = kUnset;
            } else {
                const double cruiseRecovery = snapshot_.guardW
                    - 0.03 * snapshot_.capacityW;
                updateCondition(snapshot_.filteredInputW < cruiseRecovery, now, &belowCruiseSince_);
                if (elapsedOrZero(now, belowCruiseSince_) >= 15.0) {
                    snapshot_.band = GovernorBand::Cruise;
                    belowCruiseSince_ = kUnset;
                }
            }
            break;
        case GovernorBand::Shed:
            updateCondition(snapshot_.filteredInputW < snapshot_.guardW, now, &belowGuardSince_);
            if (elapsedOrZero(now, belowGuardSince_) >= 10.0) {
                snapshot_.band = GovernorBand::Guard;
                belowGuardSince_ = kUnset;
            }
            break;
        case GovernorBand::Emergency:
            updateCondition(snapshot_.filteredInputW < snapshot_.shedW, now, &belowShedSince_);
            if (elapsedOrZero(now, belowShedSince_) >= 5.0) {
                snapshot_.band = GovernorBand::Shed;
                belowShedSince_ = kUnset;
            }
            break;
    }
}

void GovernorPolicy::updateLimits(double now) {
    snapshot_.stateTargetW = targetForBand();
    double desiredPL1 = snapshot_.stateTargetW - snapshot_.nonCPUW - config_.reserveW;
    desiredPL1 = clampValue(desiredPL1, config_.raplFloorW, config_.raplCeilingPL1W);

    if (!limitInitialized_) {
        snapshot_.pl1W = desiredPL1;
        limitInitialized_ = true;
        lastIncreaseTime_ = now;
    } else if (desiredPL1 < snapshot_.pl1W) {
        snapshot_.pl1W = desiredPL1;
    } else if (desiredPL1 > snapshot_.pl1W
               && elapsedOrZero(now, lastIncreaseTime_) >= 5.0) {
        snapshot_.pl1W = std::min(desiredPL1, snapshot_.pl1W + 1.0);
        lastIncreaseTime_ = now;
    }

    const double desiredPL2 = snapshot_.band == GovernorBand::Cruise
        ? snapshot_.pl1W + std::max(0.0, config_.cruisePL2BurstW)
        : snapshot_.pl1W;
    snapshot_.pl2W = clampValue(
        desiredPL2,
        snapshot_.pl1W,
        config_.raplCeilingPL2W
    );
}

bool GovernorPolicy::sampleFresh(
    double now,
    double sampleTime,
    double maximumAge
) const {
    return sampleTime != kUnset && now >= sampleTime && now - sampleTime <= maximumAge;
}

double GovernorPolicy::filteredInput() const {
    if (inputSamples_.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const TimedValue &sample : inputSamples_) {
        total += sample.value;
    }
    return total / (double)inputSamples_.size();
}

double GovernorPolicy::targetForBand() const {
    const double gap = snapshot_.shedW - snapshot_.guardW;
    switch (snapshot_.band) {
        case GovernorBand::Cruise:
            return snapshot_.guardW;
        case GovernorBand::Guard:
            return snapshot_.guardW - 0.5 * gap;
        case GovernorBand::Shed:
            return snapshot_.guardW - gap;
        case GovernorBand::Emergency:
            return config_.raplFloorW;
    }
    return config_.raplFloorW;
}
