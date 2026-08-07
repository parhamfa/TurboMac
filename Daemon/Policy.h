#ifndef TURBOMAC_POLICY_H
#define TURBOMAC_POLICY_H

#include <cstddef>
#include <deque>

enum class GovernorBand {
    Cruise = 0,
    Guard = 1,
    Shed = 2,
    Emergency = 3,
};

struct PolicyConfig {
    double reserveW = 2.0;
    double raplFloorW = 5.0;
    double raplCeilingPL1W = 45.0;
    double raplCeilingPL2W = 55.0;
    double cruisePL2BurstW = 0.0;
    double packageToInputSlope = 1.0;
    double packageToInputInterceptW = 0.0;
};

struct PolicySnapshot {
    GovernorBand band = GovernorBand::Cruise;
    double capacityW = 0.0;
    double inputW = 0.0;
    double filteredInputW = 0.0;
    double predictedInputW = 0.0;
    double packageW = 0.0;
    double nonCPUW = 0.0;
    double guardW = 0.0;
    double shedW = 0.0;
    double emergencyW = 0.0;
    double stateTargetW = 0.0;
    double pl1W = 0.0;
    double pl2W = 0.0;
    bool nonCPUOverBudget = false;
    bool ready = false;
};

class GovernorPolicy {
public:
    static constexpr double kGuardRatio = 0.5661;
    static constexpr double kShedRatio = 0.6541;
    static constexpr double kEmergencyRatio = 0.7296;

    explicit GovernorPolicy(const PolicyConfig &config = {});
    void reset(const PolicyConfig &config);
    bool updateSMC(double nowSeconds, double inputW, double capacityW);
    bool updatePackagePower(double nowSeconds, double packageW);
    PolicySnapshot evaluate(double nowSeconds);
    const PolicySnapshot &snapshot() const;
    static const char *bandName(GovernorBand band);

private:
    struct TimedValue {
        double time;
        double value;
    };

    static bool validPower(double value, double maximum);
    static double median(std::deque<TimedValue> values);
    static double elapsedOrZero(double now, double start);
    static void updateCondition(bool condition, double now, double *since);
    void expireSamples(double now);
    void updateThresholds();
    void updateNonCPU(double now);
    void updateBand(double now);
    void updateLimits(double now);
    bool sampleFresh(double now, double sampleTime, double maximumAge) const;
    double filteredInput() const;
    double targetForBand() const;

    PolicyConfig config_;
    PolicySnapshot snapshot_;
    std::deque<TimedValue> inputSamples_;
    std::deque<TimedValue> capacitySamples_;
    double lastSMCTime_;
    double lastPackageTime_;
    double lastNonCPUTime_;
    double guardHighSince_;
    double shedHighSince_;
    double belowShedSince_;
    double belowGuardSince_;
    double belowCruiseSince_;
    double lastIncreaseTime_;
    bool nonCPUInitialized_;
    bool limitInitialized_;
};

#endif
