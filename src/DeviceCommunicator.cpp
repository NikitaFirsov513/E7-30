#include "DeviceCommunicator.h"
#include "iostream"



bool DeviceCommunicator::start() {

	auto deviceState = device->getStatus().state;
	if (deviceState == DeviceState::CONNECTED) {
		openDB();
		communicationThread = std::thread(&DeviceCommunicator::communicationLoop, this);
	}
	else
	{
		std::cout << "DeviceCommunicator Start Error" << std::endl;
		return false;

	}


	return true;
}

bool DeviceCommunicator::tryConnect(std::string portName)
{

	bool isConnect = serialPort.connect(portName);
	if (!isConnect)
		return false;

	device->setState(DeviceState::CONNECTED);
	return true;
};

void DeviceCommunicator::stop() {
	running.store(false);

	if (communicationThread.joinable()) {
		communicationThread.join(); // ждём завершения communicationLoop
	}

	device->setState(DeviceState::CONNECTED);

	if (db) {
		sqlite3_close(db);
		db = nullptr;
	}

};

void DeviceCommunicator::sendCommand(std::vector<uint8_t> cmd) {
	//commandQueue.push(cmd);
	Command newCmd = Command(cmd, false);
	commandQueue.push(newCmd);
};

bool DeviceCommunicator::isConnected() {


	return false;
}
DeviceState DeviceCommunicator::getDeviseState()
{
	return device->getStatus().state;
}

void DeviceCommunicator::setStatusInterval(int value)
{
	STATUS_INTERVAL = std::chrono::seconds(value);

}

bool DeviceCommunicator::openDB(std::string name)
{



	if (sqlite3_open(name.c_str(), &db) != SQLITE_OK) {
		//throw std::runtime_error("Ошибка открытия БД: " + std::string(sqlite3_errmsg(db)));
		std::cout << "Ошибка открытия БД: " << std::endl;
		return false;
	}

	// Включаем WAL
	char* errMsg = nullptr;
	if (sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
		std::cout << "Ошибка PRAGMA" << std::endl;
		sqlite3_free(errMsg);
		return false;
	}

	// Создаём таблицу
	const char* sql = R"(
            CREATE TABLE IF NOT EXISTS device_responses (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT,
                avp INTEGER,
                overload INTEGER,
                autoParam INTEGER,
                scheme INTEGER,
                done INTEGER,
                mode INTEGER,
                slow INTEGER,
                diap INTEGER,
                offset INTEGER,
                frequency INTEGER,
                Z REAL,
                phi REAL
            );
        )";
	if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
		std::string msg = errMsg;
		sqlite3_free(errMsg);
		std::cout << "Ошибка создания таблицы: " << msg << std::endl;
		return false;
	}
	return true;

}

bool DeviceCommunicator::writeDataToDb(const DeviceResponse& resp)
{

	if (db == nullptr) {
		std::cout << "Ошибка открытия" << std::endl;
		return false;
	}

	std::time_t t = std::chrono::system_clock::to_time_t(resp.timestamp);
	char timebuf[32];
	std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

	const char* sql = R"(
            INSERT INTO device_responses (
                timestamp, avp, overload, autoParam, scheme, done,
                mode, slow, diap, offset, frequency,
                Z, phi
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";

	sqlite3_stmt* stmt;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		std::cout << "Ошибка подготовки запроса" << std::endl;
		return false;
	}

	// привязываем параметры
	sqlite3_bind_text(stmt, 1, timebuf, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 2, resp.avp);
	sqlite3_bind_int(stmt, 3, resp.overload);
	sqlite3_bind_int(stmt, 4, resp.autoParam);
	sqlite3_bind_int(stmt, 5, resp.scheme);
	sqlite3_bind_int(stmt, 6, resp.done);
	sqlite3_bind_int(stmt, 7, resp.mode);
	sqlite3_bind_int(stmt, 8, resp.slow);
	sqlite3_bind_int(stmt, 9, resp.diap);
	sqlite3_bind_int(stmt, 10, resp.offset);
	sqlite3_bind_int(stmt, 11, resp.frequency);
	sqlite3_bind_double(stmt, 12, resp.Z);
	sqlite3_bind_double(stmt, 13, resp.phi);


	if (sqlite3_step(stmt) != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		//throw std::runtime_error("Ошибка вставки: " + std::string(sqlite3_errmsg(db)));
		std::cout << "Ошибка вставки" << std::endl;
		return false;
	}

	sqlite3_finalize(stmt);


	return true;
}

void DeviceCommunicator::communicationLoop() {
	lastStatusRequest = std::chrono::steady_clock::now() - STATUS_INTERVAL;
	running.store(true);
	device->setState(DeviceState::RECORDING);

	while (running.load()) {
		auto now = std::chrono::steady_clock::now();

		// регулярный опрос
		if (now - lastStatusRequest >= STATUS_INTERVAL) {



			std::vector<uint8_t> cmd = {
			0xAA, 72, 0 };

			auto resp = serialPort.sendCommand(cmd);
			if (resp.size() < 10)
				continue;

			device->setLastDeviceResponce(serialPort.parceToDeviceResponse(resp));

			lastStatusRequest = now;

			DeviceResponse copy = device->getStatus().lastDeviceResponce;

			copy.timestamp = std::chrono::system_clock::now(); // проставляем время

			std::cout << "Get response: f=" << copy.frequency
				<< " Z=" << copy.Z << " phi=" << copy.phi << std::endl;

			// запись в БД
			if (!writeDataToDb(copy)) {
				std::cout << "DB write error!" << std::endl;
			}

			// сохраним в локальный буфер
			dataList.push_back(copy);
		}

		if (commandQueue.size()) {

			std::cout << "NEW CMD";
			auto cmd = commandQueue.front();
			commandQueue.pop();

			serialPort.sendCommand(cmd.cmd);

		}
		// маленькая пауза, чтобы не крутиться впустую
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	device->setState(DeviceState::CONNECTED);
}

void DeviceCommunicator::executeCommand(const Command& cmd) {};

void DeviceCommunicator::sendStatusRequest() {

	auto deviceData = serialPort.emulateReadData();

	device->setLastDeviceResponce(deviceData);

};

void DeviceCommunicator::setFrequency(uint32_t freq) {

	serialPort.setFrequency(freq);

};

DeviceCommunicator::~DeviceCommunicator() {
	stop(); // безопасно остановим поток, если он запущен
}

void DeviceCommunicator::disconnect() {
	// Останавливаем поток, если он работает
	running.store(false);
	if (communicationThread.joinable()) {
		communicationThread.join();
	}

	// Закрываем COM-порт
	serialPort.disconnect();

	// Закрываем БД, если открыта
	if (db) {
		sqlite3_close(db);
		db = nullptr;
	}

	// Меняем состояние устройства
	device->setState(DeviceState::DISCONNECTED);

	std::cout << "Device disconnected successfully." << std::endl;
}