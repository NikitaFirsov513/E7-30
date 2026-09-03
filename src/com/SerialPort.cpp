#include "SerialPort.h"
#include <vector>
#include <thread>
#include <cmath>
#include <chrono>
#include <random>
#include <iostream>
#include <iomanip>
#include "../device/E730Device.h"
#include "../DeviceCommunicator.h"

# define M_PI           3.14159265358979323846  /* pi */


DeviceResponse parseResponse(const std::vector<uint8_t>& data) {

	DeviceResponse resp{};
	resp.timestamp = std::chrono::system_clock::now();

	// --- Flags ---
	uint8_t flags = data[2];
	resp.avp = flags & (1 << 0);
	resp.overload = flags & (1 << 2);
	resp.autoParam = flags & (1 << 3);
	resp.scheme = flags & (1 << 4);
	resp.done = flags & (1 << 7);

	// --- Base fields ---
	resp.mode = data[3];
	resp.slow = data[4];
	resp.diap = data[5];

	// Смещение (2 байта)
	resp.offset = static_cast<int16_t>((data[6] << 8) | data[7]);

	// Частота (4 байта)
	resp.frequency = static_cast<int32_t>((data[8] << 24) |
		(data[9] << 16) |
		(data[10] << 8) |
		data[11]);

	// --- Z (4 байта, float) ---
	uint32_t zRaw = (static_cast<uint8_t>(data[12]) << 24) |
		(static_cast<uint8_t>(data[13]) << 16) |
		(static_cast<uint8_t>(data[14]) << 8) |
		(static_cast<uint8_t>(data[15]));
	std::memcpy(&resp.Z, &zRaw, sizeof(float));

	// --- phi (4 байта, float) ---
	uint32_t phiRaw = (static_cast<uint8_t>(data[16]) << 24) |
		(static_cast<uint8_t>(data[17]) << 16) |
		(static_cast<uint8_t>(data[18]) << 8) |
		(static_cast<uint8_t>(data[19]));
	std::memcpy(&resp.phi, &phiRaw, sizeof(float));


	// --- Derived values ---
	/*// Всегда вычисляем для последовательной схемы
	resp.Rs = resp.Z * std::cos(resp.phi);
	resp.Xs = resp.Z * std::sin(resp.phi);

	// Вычисляем для параллельной схемы
	float Y = (std::abs(resp.Z) > 1e-12f) ? 1.0f / resp.Z : 0.0f;
	float phi_y = -resp.phi;
	resp.Gp = Y * std::cos(phi_y);
	resp.Bp = Y * std::sin(phi_y);
	resp.Rp = (std::abs(resp.Gp) > 1e-12f) ? 1.0f / resp.Gp : 0.0f;
	resp.Xp = (std::abs(resp.Bp) > 1e-12f) ? 1.0f / resp.Bp : 0.0f;

	// ★★★ ДОБАВИТЬ: Расчет tg (тангенса угла потерь) для патента ★★★
	if (std::abs(resp.Bp) > 1e-12f && std::abs(resp.Gp) > 1e-12f) {
		resp.tg = resp.Gp / std::abs(resp.Bp);  // Для параллельной схемы
	}
	else if (std::abs(resp.Xs) > 1e-12f && std::abs(resp.Rs) > 1e-12f) {
		resp.tg = resp.Rs / std::abs(resp.Xs);  // Для последовательной схемы (запасной вариант)
	}
	else {
		resp.tg = 0.0f;
	}

	// Вычисляем емкость/индуктивность
	if (resp.frequency > 0) {
		float omega = 2.0f * M_PI * resp.frequency;

		// Для последовательной схемы
		if (std::abs(resp.Xs) > 1e-12f) {
			if (resp.Xs > 0) {
				resp.Ls = resp.Xs / omega;
				resp.Cs = 0.0f;
			}
			else {
				resp.Cs = -1.0f / (omega * resp.Xs);
				resp.Ls = 0.0f;
			}
		}

		// Для параллельной схемы
		if (std::abs(resp.Bp) > 1e-12f) {
			if (resp.Bp > 0) {
				resp.Cp = resp.Bp / omega;
				resp.Lp = 0.0f;
			}
			else {
				resp.Lp = -1.0f / (omega * resp.Bp);
				resp.Cp = 0.0f;
			}
		}
	}
	*/
	return resp;
}

inline float randomFloat(float min, float max) {
	static std::mt19937 rng{ std::random_device{}() };
	std::uniform_real_distribution<float> dist(min, max);
	return dist(rng);
}

std::vector<uint8_t> readBytesBlocking(HANDLE hSerial, size_t count) {
	std::vector<uint8_t> buffer(count);
	DWORD totalBytesRead = 0;

	while (totalBytesRead < count) {
		DWORD bytesRead = 0;
		DWORD toRead = count - totalBytesRead;

		if (!ReadFile(hSerial, buffer.data() + totalBytesRead, toRead, &bytesRead, nullptr)) {
			std::cerr << "Ошибка чтения из порта\n";
			break;
		}

		totalBytesRead += bytesRead;

		if (bytesRead > 0) {
			std::cout << "Прочитано байт: " << bytesRead
				<< " (всего: " << totalBytesRead << "/" << count << ")\n";
		}

		// Если не все данные пришли, ждем еще
		if (totalBytesRead < count) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	buffer.resize(totalBytesRead);
	return buffer;
}

std::vector<uint8_t> emulateRequestRaw(int delayMs = 50) {
	// задержка, эмуляция времени отклика прибора
	std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

	std::vector<uint8_t> buffer(20, 0);

	buffer[0] = 0xAA;
	buffer[1] = 72;

	uint8_t flags = 0;
	flags |= (1 << 0); // АВП
	flags |= (1 << 7); // Измерение завершено
	buffer[2] = flags;

	buffer[3] = 1; // mode
	buffer[4] = 0; // slow
	buffer[5] = 2; // diap

	// Смещение (немного «плавает» вокруг 120–130)
	int16_t offset = static_cast<int16_t>(120 + randomFloat(-5, 5));
	buffer[6] = (offset >> 8) & 0xFF;
	buffer[7] = offset & 0xFF;

	// Частота (100 кГц ± 500 Гц)
	int32_t freq = 100000 + static_cast<int32_t>(randomFloat(-500, 500));
	buffer[8] = (freq >> 24) & 0xFF;
	buffer[9] = (freq >> 16) & 0xFF;
	buffer[10] = (freq >> 8) & 0xFF;
	buffer[11] = freq & 0xFF;

	// Z = 123.45 ± 2.0 Ом
	float Z = 123.45f + randomFloat(-2.0f, 2.0f);

	// ? = 0.5 ± 0.05 рад
	float phi = 0.5f + randomFloat(-0.05f, 0.05f);

	std::memcpy(&buffer[12], &Z, sizeof(float));
	std::memcpy(&buffer[16], &phi, sizeof(float));

	return buffer;
}

SerialPort::SerialPort() {
	hSerial = INVALID_HANDLE_VALUE;

};

SerialPort::~SerialPort() {
	disconnect();
};

bool SerialPort::connect(const std::string& portName, int baudRate) {
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

bool SerialPort::writeData(std::vector<uint8_t> data) {

	return false;

}

bool SerialPort::setFrequency(uint32_t freq)
{

	std::cout << "\n--- Установка частоты ---\n";

	// Очистка буферов перед отправкой
	PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

	std::vector<uint8_t> cmd = { 0xAA, 67 }; // команда установки частоты
	cmd.push_back((freq >> 24) & 0xFF); // старший байт
	cmd.push_back((freq >> 16) & 0xFF);
	cmd.push_back((freq >> 8) & 0xFF);
	cmd.push_back(freq & 0xFF);         // младший байт

	std::cout << "Отправка частоты: " << freq << " Гц > ";
	for (auto b : cmd) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
	std::cout << std::dec << std::endl;


	// Задержка для обработки команды
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	auto echo = readBytesBlocking(hSerial, 2);
	if (echo.size() >= 2) {
		std::cout << "Ответ: 0x" << std::hex << std::setw(2) << std::setfill('0')
			<< (int)echo[0] << " 0x" << std::setw(2) << std::setfill('0')
			<< (int)echo[1] << std::dec << std::endl;
		return true;
	}
	else {
		std::cerr << "Нет ответа на команду установки частоты! (получено "
			<< echo.size() << " байт)\n";
		return false;
	}

}

DeviceResponse SerialPort::parceToDeviceResponse(std::vector<uint8_t> data)
{
	return parseResponse(data);
}

std::vector<uint8_t> SerialPort::sendCommand(std::vector<uint8_t> cmd)
{
	if (!isConnected()) {
		throw std::runtime_error("Serial port is not connected");
	}

	const int maxRetries = 3;
	const int readTimeoutMs = 500; // ожидание ответа после каждой отправки
	const int maxWaitMs = 2000;    // общий таймаут

	std::vector<uint8_t> response;

	for (int attempt = 0; attempt < maxRetries; ++attempt)
	{
		// Очистка буферов при повторной попытке
		PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

		// Отправляем команду
		DWORD bytesWritten = 0;
		if (!WriteFile(hSerial, cmd.data(), cmd.size(), &bytesWritten, nullptr)) {
			throw std::runtime_error("Failed to write to serial port");
		}

		FlushFileBuffers(hSerial);

		auto start = std::chrono::steady_clock::now();
		response.clear();

		// Ожидание и чтение
		while (true)
		{
			DWORD bytesAvailable = 0;
			COMSTAT comStat;
			DWORD errors;

			if (ClearCommError(hSerial, &errors, &comStat)) {
				bytesAvailable = comStat.cbInQue;
			}

			if (bytesAvailable > 0)
			{
				std::vector<uint8_t> temp(bytesAvailable);
				DWORD bytesRead = 0;
				if (ReadFile(hSerial, temp.data(), bytesAvailable, &bytesRead, nullptr)) {
					response.insert(response.end(), temp.begin(), temp.begin() + bytesRead);
				}
			}

			// Проверка, получили ли ответ с тем же кодом команды
			if (response.size() >= 2 && response[0] == 0xAA && response[1] == cmd[1]) {
				return response;
			}

			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - start).count();

			if (elapsed > maxWaitMs)
				break;

			std::this_thread::sleep_for(std::chrono::milliseconds(readTimeoutMs));
		}

		std::cerr << "No valid response, retry " << (attempt + 1) << "/" << maxRetries << "\n";
	}

	throw std::runtime_error("Timeout: no valid response from device");

}

std::vector<uint8_t> SerialPort::readData() {
	std::vector<uint8_t> data;
	return data;
}

DeviceResponse SerialPort::emulateReadData() {

	auto data = emulateRequestRaw();
	return parseResponse(data);

}