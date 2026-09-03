#include "DeviceCommunicator.h"
#include "protocol/E730Protocol.h"
#include "storage/DataStore.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

ExperimentMetadata metadata() {
    return {"ALKYD-A", "R1", "Reactor-2", "Sensor-1", "E730-test", "operator", "test"};
}

void testCommandEncoding() {
    CHECK(e730::encodeStatusRequest() == std::vector<std::uint8_t>({0xAA, 72, 0}));
    CHECK(e730::encodeReset() == std::vector<std::uint8_t>({0xAA, 71}));
    CHECK(e730::encodeSetFrequency(0x12345678U) ==
          std::vector<std::uint8_t>({0xAA, 67, 0x12, 0x34, 0x56, 0x78}));
}

void testParserAndByteOrder() {
    auto packet = e730::emulateStatusPacket(100000, 1.0F, -2.5F, false, true);
    packet[6] = 0x80;
    packet[7] = 0x00;
    CHECK(packet[12] == 0x3F && packet[13] == 0x80 && packet[14] == 0 && packet[15] == 0);
    const auto result = e730::parseStatusResponse(packet);
    CHECK(result.success);
    CHECK(result.response.returnedFrequency == 100000);
    CHECK(result.response.offset == std::numeric_limits<std::int16_t>::min());
    CHECK(result.response.value1 == 1.0F);
    CHECK(result.response.value2 == -2.5F);
    CHECK(result.response.done && !result.response.overload && result.response.valid);
    CHECK(result.response.rawPacket == packet);

    const auto overloaded = e730::parseStatusResponse(
        e730::emulateStatusPacket(1000, 1.0F, 2.0F, true, true));
    CHECK(overloaded.success && overloaded.response.overload && !overloaded.response.valid);
    const auto incomplete = e730::parseStatusResponse(
        e730::emulateStatusPacket(1000, 1.0F, 2.0F, false, false));
    CHECK(incomplete.success && !incomplete.response.done && !incomplete.response.valid);

    const auto boundary = e730::parseStatusResponse(e730::emulateStatusPacket(
        std::numeric_limits<std::int32_t>::max(), std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest()));
    CHECK(boundary.success);
    CHECK(boundary.response.value1 == std::numeric_limits<float>::max());
}

void testInvalidPackets() {
    const auto full = e730::emulateStatusPacket(1000, 1.0F, 2.0F);
    auto shortPacket = full;
    shortPacket.resize(10);
    const auto shortResult = e730::parseStatusResponse(shortPacket);
    CHECK(!shortResult.success && shortResult.error == e730::ParseError::WrongLength);
    CHECK(shortResult.response.rawPacket == shortPacket);

    auto wrongHeader = full;
    wrongHeader[0] = 0x55;
    CHECK(e730::parseStatusResponse(wrongHeader).error == e730::ParseError::WrongHeader);
    auto wrongCommand = full;
    wrongCommand[1] = 71;
    CHECK(e730::parseStatusResponse(wrongCommand).error == e730::ParseError::WrongCommand);
    auto nanPacket = full;
    nanPacket[12] = 0x7F; nanPacket[13] = 0xC0; nanPacket[14] = 0; nanPacket[15] = 0;
    CHECK(e730::parseStatusResponse(nanPacket).error == e730::ParseError::NonFiniteValue);
}

void testResponseAccumulation() {
    const auto packet = e730::emulateStatusPacket(2000, 3.0F, 4.0F);
    e730::ResponseAccumulator accumulator(72, packet.size());
    std::string error;
    CHECK(accumulator.append(packet.data(), 1, error));
    CHECK(!accumulator.complete());
    CHECK(accumulator.append(packet.data() + 1, 7, error));
    CHECK(!accumulator.complete());
    CHECK(accumulator.append(packet.data() + 8, packet.size() - 8, error));
    CHECK(accumulator.complete());
    CHECK(accumulator.data() == packet);

    e730::ResponseAccumulator invalid(72, packet.size());
    auto wrongPacket = packet;
    wrongPacket[0] = 0x55;
    CHECK(invalid.append(wrongPacket.data(), wrongPacket.size(), error));
    CHECK(invalid.complete());
    CHECK(!e730::validateEnvelope(invalid.data(), 72, wrongPacket.size(), error));
}

void testStorageRoundTrip() {
    const std::filesystem::path databasePath =
        std::filesystem::temp_directory_path() /
        ("e730_storage_roundtrip_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
    std::error_code ignored;
    std::filesystem::remove(databasePath, ignored);
    std::int64_t experiment = 0;
    {
        DataStore store;
        CHECK(store.open(databasePath.string()));
        experiment = store.beginExperiment(metadata());
        CHECK(experiment > 0);
        DeviceResponse response;
        response.sequenceId = 1;
        response.timestamp = std::chrono::system_clock::now();
        response.requestedFrequency = 999;
        response.returnedFrequency = 1000;
        response.done = true;
        response.valid = true;
        response.value1 = 10.5F;
        response.value2 = 20.5F;
        response.rawPacket = e730::emulateStatusPacket(1000, 10.5F, 20.5F);
        CHECK(store.insertMeasurement(experiment, response));
        const auto sample = store.collectSample(experiment);
        CHECK(sample > 0);
        LaboratoryResult laboratory;
        laboratory.acidNumberMgKohG = 12.25;
        laboratory.analysisTemperatureC = 23.0;
        laboratory.methodInstrument = "GOST/tester";
        CHECK(store.enterSampleResult(sample, laboratory));
        CHECK(store.setSampleValidity(sample, false));
        CHECK(store.finishExperiment(experiment));
    }
    {
        DataStore reopened;
        CHECK(reopened.open(databasePath.string()));
        CHECK(reopened.measurementCount(experiment) == 1);
        const auto samples = reopened.samples(experiment);
        CHECK(samples.size() == 1);
        if (!samples.empty()) {
            CHECK(samples[0].result.acidNumberMgKohG == 12.25);
            CHECK(!samples[0].result.conditionalViscositySeconds.has_value());
            CHECK(!samples[0].valid);
        }
    }
    std::filesystem::remove(databasePath, ignored);
}

void testQueueLifecycleDisconnectAndBoundedHistory() {
    E730Device device;
    DeviceCommunicator communicator(device);
    std::cout << "  connect\n" << std::flush;
    CHECK(communicator.tryConnect("EMULATOR"));
    CHECK(communicator.openDB(":memory:"));
    CHECK(communicator.beginExperiment(metadata()) > 0);
    std::thread producer([&communicator] { communicator.setFrequency(4321); });
    producer.join();
    communicator.setStatusInterval(std::chrono::milliseconds(1));
    std::cout << "  first start\n" << std::flush;
    CHECK(communicator.start());
    CHECK(communicator.start());
    for (int attempt = 0; attempt < 2000 &&
         communicator.getLastData().lastDeviceResponse.sequenceId < 20; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(communicator.persistedMeasurementCount() >= 1);
    CHECK(communicator.getLastData().lastDeviceResponse.returnedFrequency == 4321);
    communicator.stop();
    communicator.stop();
    std::cout << "  first stop\n" << std::flush;
    CHECK(!communicator.isRunning());

    CHECK(communicator.start());
    std::cout << "  long run\n" << std::flush;
    for (int attempt = 0; attempt < 10000 && communicator.bufferedMeasurementCount() < 1000; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(communicator.bufferedMeasurementCount() == 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "  disconnect\n" << std::flush;
    communicator.disconnect();
    const auto afterDisconnect = communicator.persistedMeasurementCount();
    CHECK(!communicator.isRunning());
    CHECK(communicator.getDeviceState() == DeviceState::DISCONNECTED);
    CHECK(afterDisconnect >= 1000);
    CHECK(communicator.tryConnect("EMULATOR"));
    std::cout << "  reconnected\n" << std::flush;
    CHECK(communicator.getDeviceState() == DeviceState::CONNECTED);
    CHECK(communicator.finishExperiment());
    std::cout << "  finished\n" << std::flush;
}

void testUnexpectedDisconnectDuringRecording() {
    E730Device device;
    DeviceCommunicator communicator(device);
    CHECK(communicator.tryConnect("EMULATOR_DROP"));
    CHECK(communicator.openDB(":memory:"));
    CHECK(communicator.beginExperiment(metadata()) > 0);
    communicator.setStatusInterval(std::chrono::milliseconds(1));
    CHECK(communicator.start());
    for (int attempt = 0; attempt < 2000 && communicator.isRunning(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(!communicator.isRunning());
    CHECK(communicator.getDeviceState() == DeviceState::ERR);
    CHECK(communicator.persistedMeasurementCount() == 5);
    CHECK(communicator.tryConnect("EMULATOR"));
    CHECK(communicator.getDeviceState() == DeviceState::CONNECTED);
    CHECK(communicator.finishExperiment());
}

} // namespace

int main() {
    std::cout << "testCommandEncoding\n" << std::flush;
    testCommandEncoding();
    std::cout << "testParserAndByteOrder\n" << std::flush;
    testParserAndByteOrder();
    std::cout << "testInvalidPackets\n" << std::flush;
    testInvalidPackets();
    std::cout << "testResponseAccumulation\n" << std::flush;
    testResponseAccumulation();
    std::cout << "testStorageRoundTrip\n" << std::flush;
    testStorageRoundTrip();
    std::cout << "testQueueLifecycleDisconnectAndBoundedHistory\n" << std::flush;
    testQueueLifecycleDisconnectAndBoundedHistory();
    std::cout << "testUnexpectedDisconnectDuringRecording\n" << std::flush;
    testUnexpectedDisconnectDuringRecording();
    if (failures == 0) {
        std::cout << "All E7-30 tests passed\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed\n";
    return 1;
}
