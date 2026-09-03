#include "DeviceCommunicator.h"

#include "protocol/E730Protocol.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

DeviceCommunicator::DeviceCommunicator(E730Device& device) : device_(&device) { ensureWorker(); }

DeviceCommunicator::~DeviceCommunicator() {
    stop();
    disconnect();
    const auto experiment = activeExperimentId_.load();
    if (experiment != 0) dataStore_.finishExperiment(experiment);
    shutdownWorker();
    dataStore_.close();
}

void DeviceCommunicator::ensureWorker() {
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    if (workerRunning_.load() || communicationThread_.joinable()) return;
    workerRunning_.store(true);
    communicationThread_ = std::thread(&DeviceCommunicator::communicationLoop, this);
}

void DeviceCommunicator::shutdownWorker() noexcept {
    workerRunning_.store(false);
    queueCondition_.notify_all();
    if (communicationThread_.joinable()) communicationThread_.join();
}

void DeviceCommunicator::enqueue(QueueItem item) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        commandQueue_.push(std::move(item));
    }
    queueCondition_.notify_one();
}

bool DeviceCommunicator::enqueueAndWait(QueueItem item, std::chrono::milliseconds timeout) {
    ensureWorker();
    auto completion = std::make_shared<std::promise<bool>>();
    auto future = completion->get_future();
    item.completion = std::move(completion);
    enqueue(std::move(item));
    if (future.wait_for(timeout) != std::future_status::ready) {
        device_->setError("communication worker did not complete the operation in time");
        return false;
    }
    return future.get();
}

bool DeviceCommunicator::tryConnect(std::string portName) {
    QueueItem item;
    item.type = QueueType::Connect;
    item.portName = std::move(portName);
    return enqueueAndWait(std::move(item), std::chrono::seconds(3));
}

void DeviceCommunicator::disconnect() {
    recording_.store(false);
    QueueItem item;
    item.type = QueueType::Disconnect;
    enqueueAndWait(std::move(item), std::chrono::seconds(3));
}

bool DeviceCommunicator::openDB(std::string name) {
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    databaseName_ = std::move(name);
    return dataStore_.open(databaseName_);
}

std::int64_t DeviceCommunicator::beginExperiment(const ExperimentMetadata& metadata) {
    if (activeExperimentId_.load() != 0) return activeExperimentId_.load();
    if (!dataStore_.isOpen() && !openDB(databaseName_)) {
        device_->setError("Cannot open experiment database: " + dataStore_.lastError());
        return 0;
    }
    const std::int64_t id = dataStore_.beginExperiment(metadata);
    if (id == 0) {
        device_->setError("Cannot create experiment: " + dataStore_.lastError());
        return 0;
    }
    activeExperimentId_.store(id);
    nextSequenceId_.store(1);
    dataStore_.addEvent(id, "experiment_started", "Experiment session created");
    return id;
}

bool DeviceCommunicator::finishExperiment() {
    stop();
    const std::int64_t id = activeExperimentId_.load();
    if (id == 0) return true;
    dataStore_.addEvent(id, "experiment_finished", "Experiment session finished");
    if (!dataStore_.finishExperiment(id)) {
        device_->setError("Cannot finish experiment: " + dataStore_.lastError());
        return false;
    }
    activeExperimentId_.store(0);
    return true;
}

bool DeviceCommunicator::start() {
    if (recording_.load()) return true;
    if (device_->getStatus().state != DeviceState::CONNECTED) {
        device_->setError("Connect the device before starting registration");
        return false;
    }
    if (activeExperimentId_.load() == 0) {
        device_->setError("Create an experiment session before starting registration");
        return false;
    }
    if (!dataStore_.isOpen()) {
        device_->setError("Registration cannot start because the database is not open");
        return false;
    }
    bool expected = false;
    if (!recording_.compare_exchange_strong(expected, true)) return true;
    QueueItem item;
    item.type = QueueType::StartRecording;
    if (!enqueueAndWait(std::move(item), std::chrono::seconds(3))) {
        recording_.store(false);
        return false;
    }
    dataStore_.addEvent(activeExperimentId_.load(), "registration_started", "Electrical registration started");
    return true;
}

void DeviceCommunicator::stop() {
    if (!recording_.exchange(false)) return;
    QueueItem item;
    item.type = QueueType::StopRecording;
    enqueueAndWait(std::move(item), std::chrono::seconds(3));
    const auto id = activeExperimentId_.load();
    if (id != 0) dataStore_.addEvent(id, "registration_stopped", "Electrical registration stopped");
}

bool DeviceCommunicator::isRunning() const noexcept { return recording_.load(); }

bool DeviceCommunicator::isConnected() const {
    const auto state = device_->getStatus().state;
    return state == DeviceState::CONNECTED || state == DeviceState::RECORDING;
}

void DeviceCommunicator::sendCommand(std::vector<std::uint8_t> command) {
    if (command.size() < 2U) {
        device_->setError("Refusing to queue a command shorter than two bytes");
        return;
    }
    QueueItem item;
    item.type = QueueType::Command;
    item.expectedLength = e730::expectedResponseLength(command[1]);
    item.bytes = std::move(command);
    enqueue(std::move(item));
}

void DeviceCommunicator::setFrequency(std::uint32_t frequency) {
    if (frequency < e730::kMinimumFrequencyHz || frequency > e730::kMaximumFrequencyHz) {
        device_->setError("Frequency is outside the documented 25 Hz to 3 MHz range");
        return;
    }
    requestedFrequency_.store(frequency);
    sendCommand(e730::encodeSetFrequency(frequency));
}

void DeviceCommunicator::resetDevice() { sendCommand(e730::encodeReset()); }

void DeviceCommunicator::setStatusInterval(int seconds) {
    setStatusInterval(std::chrono::seconds(std::max(1, seconds)));
}

void DeviceCommunicator::setStatusInterval(std::chrono::milliseconds interval) {
    statusIntervalMs_.store(std::max<std::int64_t>(1, interval.count()));
    QueueItem item;
    item.type = QueueType::Reschedule;
    enqueue(std::move(item));
}

DeviceStatus DeviceCommunicator::getLastData() const { return device_->getStatus(); }
DeviceState DeviceCommunicator::getDeviceState() const { return device_->getStatus().state; }
std::int64_t DeviceCommunicator::activeExperimentId() const noexcept { return activeExperimentId_.load(); }

std::int64_t DeviceCommunicator::collectLaboratorySample() {
    const auto id = activeExperimentId_.load();
    if (id == 0) return 0;
    const auto sample = dataStore_.collectSample(id);
    if (sample != 0) dataStore_.addEvent(id, "sample_collected", "Laboratory sample collected");
    return sample;
}

bool DeviceCommunicator::enterLaboratoryResult(std::int64_t sampleId, const LaboratoryResult& result) {
    return dataStore_.enterSampleResult(sampleId, result);
}

bool DeviceCommunicator::setLaboratorySampleValidity(std::int64_t sampleId, bool valid) {
    return dataStore_.setSampleValidity(sampleId, valid);
}

std::vector<LaboratorySample> DeviceCommunicator::laboratorySamples() const {
    const auto id = activeExperimentId_.load();
    return id == 0 ? std::vector<LaboratorySample>{} : dataStore_.samples(id);
}

bool DeviceCommunicator::addExperimentEvent(const std::string& type, const std::string& comment) {
    const auto id = activeExperimentId_.load();
    return id != 0 && dataStore_.addEvent(id, type, comment);
}

std::string DeviceCommunicator::storageError() const { return dataStore_.lastError(); }

std::size_t DeviceCommunicator::bufferedMeasurementCount() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    return dataList_.size();
}

std::int64_t DeviceCommunicator::persistedMeasurementCount() const {
    const auto id = activeExperimentId_.load();
    return id == 0 ? 0 : dataStore_.measurementCount(id);
}

void DeviceCommunicator::processQueueItem(QueueItem item) {
    bool success = false;
    try {
        switch (item.type) {
        case QueueType::Connect:
            serialPort_.disconnect();
            success = serialPort_.connect(item.portName);
            if (success) {
                device_->clearError();
                device_->setState(DeviceState::CONNECTED);
            } else {
                device_->setError("Failed to open serial port " + item.portName);
            }
            break;
        case QueueType::Disconnect:
            serialPort_.disconnect();
            device_->setState(DeviceState::DISCONNECTED);
            success = true;
            break;
        case QueueType::StartRecording:
            if (!recording_.load()) {
                success = true; // A concurrent stop cancelled the pending start.
                break;
            }
            if (!serialPort_.isConnected()) {
                recording_.store(false);
                device_->setError("Cannot start registration: serial port is disconnected");
                break;
            }
            recording_.store(true);
            nextStatusRequest_ = std::chrono::steady_clock::now();
            device_->clearError();
            device_->setState(DeviceState::RECORDING);
            success = true;
            break;
        case QueueType::StopRecording:
            device_->setState(serialPort_.isConnected() ? DeviceState::CONNECTED : DeviceState::DISCONNECTED);
            success = true;
            break;
        case QueueType::Reschedule:
            nextStatusRequest_ = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(statusIntervalMs_.load());
            success = true;
            break;
        case QueueType::Command: {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            serialPort_.sendCommand(item.bytes, item.expectedLength, deadline);
            success = true;
            break;
        }
        }
    } catch (const std::exception& error) {
        recording_.store(false);
        device_->setError(error.what());
        std::cerr << "Communication error: " << error.what() << '\n';
    } catch (...) {
        recording_.store(false);
        device_->setError("Unknown exception in communication worker");
        std::cerr << "Unknown exception in communication worker\n";
    }
    if (item.completion) item.completion->set_value(success);
}

void DeviceCommunicator::requestStatus() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const auto packet = serialPort_.sendCommand(e730::encodeStatusRequest(),
                                                e730::kLegacyStatusResponseSize, deadline);
    auto parsed = serialPort_.parseToDeviceResponse(packet);
    parsed.response.requestedFrequency = requestedFrequency_.load();
    parsed.response.sequenceId = nextSequenceId_.fetch_add(1);
    parsed.response.timestamp = std::chrono::system_clock::now();
    parsed.response.receivedAt = parsed.response.timestamp;
    if (!parsed.success) {
        parsed.response.valid = false;
        parsed.response.diagnostic = parsed.message;
    }
    const auto experiment = activeExperimentId_.load();
    if (experiment != 0 && !dataStore_.insertMeasurement(experiment, parsed.response)) {
        device_->setError("SQLite write failed: " + dataStore_.lastError());
        recording_.store(false);
        return;
    }
    if (!parsed.success) {
        device_->setError("Invalid device response: " + parsed.message);
        return;
    }
    device_->setLastDeviceResponse(parsed.response);
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        dataList_.push_back(std::move(parsed.response));
        while (dataList_.size() > kMaxBufferedMeasurements) dataList_.pop_front();
    }
}

void DeviceCommunicator::communicationLoop() noexcept {
    nextStatusRequest_ = std::chrono::steady_clock::now();
    while (workerRunning_.load()) {
        try {
            QueueItem item;
            bool hasItem = false;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                if (recording_.load()) {
                    queueCondition_.wait_until(lock, nextStatusRequest_, [this] {
                        return !workerRunning_.load() || !commandQueue_.empty();
                    });
                } else {
                    queueCondition_.wait(lock, [this] {
                        return !workerRunning_.load() || !commandQueue_.empty();
                    });
                }
                if (!workerRunning_.load()) break;
                if (!commandQueue_.empty()) {
                    item = std::move(commandQueue_.front());
                    commandQueue_.pop();
                    hasItem = true;
                }
            }
            if (hasItem) {
                processQueueItem(std::move(item));
                continue;
            }
            if (recording_.load() && std::chrono::steady_clock::now() >= nextStatusRequest_) {
                requestStatus();
                nextStatusRequest_ = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(statusIntervalMs_.load());
            }
        } catch (const std::exception& error) {
            recording_.store(false);
            device_->setError(error.what());
            std::cerr << "Communication loop error: " << error.what() << '\n';
        } catch (...) {
            recording_.store(false);
            device_->setError("Unknown communication loop failure");
        }
    }
    serialPort_.disconnect();
}
