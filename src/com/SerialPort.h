#include "windows.h"
#include <atomic>
#include <string>

class SerialPort {
private:
	HANDLE hSerial;
	std::atomic<bool> connected{ false };

public:
	SerialPort();
	~SerialPort();

	bool connect(const std::string& portName, int baudRate = 9600);
	void disconnect();

	bool isConnected() const;

	bool writeData(const std::string& data);

	std::string readData();
};