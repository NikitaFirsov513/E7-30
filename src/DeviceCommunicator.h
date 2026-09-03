#pragma once

#include "com/SerialPort.h"
#include "device/E730Device.h"
#include <queue>
#include "sqlite/sqlite3.h"


struct Command {
	std::vector<uint8_t> cmd;
	bool needsResponse;
	std::chrono::steady_clock::time_point timestamp;

	Command(const std::vector<uint8_t> cmd, bool response = false)
		: cmd(cmd), needsResponse(response), timestamp(std::chrono::steady_clock::now()) {
	}
};

class DeviceCommunicator {
private:

	SerialPort serialPort;

	E730Device* device;

	std::thread communicationThread;

	std::atomic<bool> running{ false };

	std::vector<DeviceResponse> dataList;

	std::queue<Command> commandQueue;

	std::mutex queueMutex;
	std::condition_variable queueCondition;

	std::chrono::steady_clock::time_point lastStatusRequest;
	std::chrono::seconds STATUS_INTERVAL{ 1 };

	sqlite3* db = nullptr;

public:

	DeviceCommunicator() : device(nullptr) {}
	DeviceCommunicator(E730Device& dev) : device(&dev) {}
	~DeviceCommunicator();

	bool start();
	bool tryConnect(std::string portName);
	void stop();
	void sendCommand(std::vector<uint8_t> cmd);
	bool isConnected();
	void setFrequency(uint32_t freq);
	DeviceState getDeviseState();

	void setStatusInterval(int value);

	bool openDB(std::string name = "TestDB.db");
	bool writeDataToDb(const DeviceResponse& resp);
	DeviceStatus getLastData() {
		return device->getStatus();
	};
	void disconnect();
private:
	void communicationLoop();
	void executeCommand(const Command& cmd);
	void sendStatusRequest();
};