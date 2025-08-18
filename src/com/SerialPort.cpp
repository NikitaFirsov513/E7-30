#include "SerialPort.h"


SerialPort::SerialPort() {
	hSerial = INVALID_HANDLE_VALUE;

};

SerialPort::~SerialPort() {
	disconnect();
};

bool SerialPort::connect(const std::string& portName, int baudRate ) {
	std::string fullPortName = "\\\\.\\" + portName;
	hSerial = CreateFileA(fullPortName.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);

	if (hSerial == INVALID_HANDLE_VALUE) {
		return false;
	}

	DCB dcbSerialParams = { 0 };
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

	if (!GetCommState(hSerial, &dcbSerialParams)) {
		CloseHandle(hSerial);
		hSerial = INVALID_HANDLE_VALUE;
		return false;
	}

	dcbSerialParams.BaudRate = baudRate;
	dcbSerialParams.ByteSize = 8;
	dcbSerialParams.StopBits = ONESTOPBIT;
	dcbSerialParams.Parity = NOPARITY;

	if (!SetCommState(hSerial, &dcbSerialParams)) {
		CloseHandle(hSerial);
		hSerial = INVALID_HANDLE_VALUE;
		return false;
	}

	COMMTIMEOUTS timeouts = { 0 };
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 50;
	timeouts.ReadTotalTimeoutMultiplier = 10;
	timeouts.WriteTotalTimeoutConstant = 50;
	timeouts.WriteTotalTimeoutMultiplier = 10;

	if (!SetCommTimeouts(hSerial, &timeouts)) {
		CloseHandle(hSerial);
		hSerial = INVALID_HANDLE_VALUE;
		return false;
	}

	connected = true;
	return true;

};

void SerialPort::disconnect() {
	connected = false;
	if (hSerial != INVALID_HANDLE_VALUE) {
		CloseHandle(hSerial);
		hSerial = INVALID_HANDLE_VALUE;
	}

};

bool SerialPort::isConnected() const {
	return connected;
}

bool SerialPort::writeData(const std::string& data) {
	if (!connected) return false;

	DWORD bytesWritten;
	return WriteFile(hSerial, data.c_str(), data.length(), &bytesWritten, nullptr);

}

std::string SerialPort::readData() {
	if (!connected) return "";

	char buffer[256];
	std::string result;

	DWORD bytesRead;
	if (ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
		buffer[bytesRead] = '\0';
		result = std::string(buffer);
	}

	return result;
}