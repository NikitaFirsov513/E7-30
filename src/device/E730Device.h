#pragma once

#include <string>
#include <chrono>
#include <mutex>

enum class DeviceState {
	CONNECTED,
	RECORDING,
	ERR,
	DISCONNECTED
};

struct DeviceResponse {
	// Время опроса
	std::chrono::system_clock::time_point timestamp;

	// Флаги из ответа прибора
	bool avp;          // бит 0 – АВП
	bool overload;     // бит 2 – перегрузка
	bool autoParam;    // бит 3 – авт. выбор параметра
	bool scheme;       // бит 4 – схема замещения (1 – паралл., 0 – посл.)
	bool done;         // бит 7 – цикл измерения завершён

	// Остальные поля
	uint8_t mode;      // параметр (F1–F4)
	uint8_t slow;      // скорость измерения
	uint8_t diap;      // диапазон измерения

	int16_t offset;    // Uсм (смещение*10)
	int32_t frequency; // частота (Гц)

	float Z;           // модуль комплексного сопротивления |Z|
	float phi;         // фазовый угол φ (радианы)


	
};

struct DeviceStatus {
	DeviceState state = DeviceState::DISCONNECTED;

	DeviceResponse lastDeviceResponce;

	std::string lastError;
};

class E730Device {

private:
	DeviceStatus status;
	mutable std::mutex statusMutex;


public:
	void updateStatus(const std::string& response);

	const DeviceStatus& getStatus() {
		std::lock_guard<std::mutex> lock(statusMutex);
		return status;
	}

	void setState(DeviceState newState);

	void setError(const std::string& error);

	void setLastDeviceResponce(DeviceResponse ldr);

private:
	
	void parseResponse(const std::string& response);

};