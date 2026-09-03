#pragma once

#include "../device/E730Device.h"
#include "../sqlite/sqlite3.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct ExperimentMetadata {
    std::string resinGrade;
    std::string recipeVersion;
    std::string reactor;
    std::string sensor;
    std::string deviceSerialNumber;
    std::string operatorName;
    std::string notes;
};

struct LaboratoryResult {
    std::optional<double> acidNumberMgKohG;
    std::optional<double> conditionalViscositySeconds;
    std::optional<double> analysisTemperatureC;
    std::string methodInstrument;
    std::string notes;
};

struct LaboratorySample {
    std::int64_t id = 0;
    std::int64_t experimentId = 0;
    std::int64_t sampleNumber = 0;
    std::string sampledAtUtc;
    std::string resultEnteredAtUtc;
    LaboratoryResult result;
    std::string state;
    bool valid = true;
};

class DataStore {
public:
    DataStore() = default;
    ~DataStore();
    DataStore(const DataStore&) = delete;
    DataStore& operator=(const DataStore&) = delete;

    bool open(const std::string& fileName);
    void close() noexcept;
    bool isOpen() const;
    std::string lastError() const;

    std::int64_t beginExperiment(const ExperimentMetadata& metadata);
    bool finishExperiment(std::int64_t experimentId);
    bool insertMeasurement(std::int64_t experimentId, const DeviceResponse& response);
    std::int64_t collectSample(std::int64_t experimentId);
    bool enterSampleResult(std::int64_t sampleId, const LaboratoryResult& result);
    bool setSampleValidity(std::int64_t sampleId, bool valid);
    bool addEvent(std::int64_t experimentId,
                  const std::string& type,
                  const std::string& comment);
    std::vector<LaboratorySample> samples(std::int64_t experimentId) const;
    std::int64_t measurementCount(std::int64_t experimentId) const;

    static std::string utcNowMilliseconds();
    static std::string utcMilliseconds(std::chrono::system_clock::time_point time);

private:
    bool executeLocked(const char* sql);
    bool migrateLocked();
    void setErrorLocked(const std::string& context);

    mutable std::mutex mutex_;
    sqlite3* database_ = nullptr;
    mutable std::string lastError_;
};
