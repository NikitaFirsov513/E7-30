#pragma once

#include "windows.h"
#include <atomic>
#include <string>
#include <vector>
#include "../device/E730Device.h"
//#include "../DeviceCommunicator.h"

class SerialPort {

private:

	HANDLE hSerial;
	std::atomic<bool> connected{ true };

public:

	SerialPort();

	~SerialPort();

	bool connect(const std::string& portName, int baudRate = 9600);

	void disconnect();

	bool isConnected() const;

	bool writeData(std::vector<uint8_t> data);

	bool setFrequency(uint32_t freq);

	DeviceResponse parceToDeviceResponse(std::vector<uint8_t> data);


	std::vector<uint8_t> sendCommand(std::vector<uint8_t> cmd);

	std::vector<uint8_t> readData();

	DeviceResponse emulateReadData();

};