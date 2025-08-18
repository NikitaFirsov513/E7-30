#pragma once

#include "com/SerialPort.h"
#include "device/E730Device.h"
#include <queue>


struct Command {
	std::string data;
	bool needsResponse;
	std::chrono::steady_clock::time_point timestamp;

	Command(const std::string& cmd, bool response = false)
		: data(cmd), needsResponse(response), timestamp(std::chrono::steady_clock::now()) {
	}
};

class DeviceCommunicator {
private:
	SerialPort serialPort;
	E730Device& device;
	std::thread communicationThread;
	std::atomic<bool> running{ false };

	std::queue<Command> commandQueue;
	std::mutex queueMutex;
	std::condition_variable queueCondition;

	std::chrono::steady_clock::time_point lastStatusRequest;
	const std::chrono::seconds STATUS_INTERVAL{ 5 };
public:

	DeviceCommunicator(E730Device& dev);
	~DeviceCommunicator();

	bool start(const std::string& portName);
	void stop();
	void sendCommand();
	bool isConnected();

private:
	void communicationLoop();
	void executeCommand(const Command& cmd);
	void sendStatusRequest();
};