#pragma once

#include <string>
#include <chrono>
#include <mutex>




class E730Device {
public:

	enum class State {
		CONNECTED,
		RECORDING,
		ERR,
		DISCONNECTED
	};

	struct DeviceStatus {
		State state = State::DISCONNECTED;
		float temperature = 0.0f;
		int batteryLevel = 0;
		std::string lastError;
		std::chrono::steady_clock::time_point lastUpdate;
		bool isRecording = false;
		int recordingTime = 0; // seconds
		std::string firmwareVersion;
	};

private:
	DeviceStatus status;
	mutable std::mutex statusMutex;


public:
	void updateStatus(const std::string& response);
	DeviceStatus getStatus() const;
	void setState(State newState);
	void setError(const std::string& error);


private:
	void parseResponse(const std::string& response);
};