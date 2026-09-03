#include "DataStore.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace {

struct StatementDeleter {
    void operator()(sqlite3_stmt* statement) const noexcept { sqlite3_finalize(statement); }
};
using Statement = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

Statement prepare(sqlite3* database, const char* sql, std::string& error) {
    if (database == nullptr) {
        error = "database is not open";
        return {};
    }
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &raw, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(database);
        return {};
    }
    return Statement(raw);
}

bool bindText(sqlite3_stmt* statement, int index, const std::string& text) {
    return sqlite3_bind_text(statement, index, text.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

bool bindOptional(sqlite3_stmt* statement, int index, const std::optional<double>& value) {
    return value ? sqlite3_bind_double(statement, index, *value) == SQLITE_OK
                 : sqlite3_bind_null(statement, index) == SQLITE_OK;
}

std::optional<double> optionalColumn(sqlite3_stmt* statement, int column) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_double(statement, column);
}

std::string textColumn(sqlite3_stmt* statement, int column) {
    const auto* text = sqlite3_column_text(statement, column);
    return text == nullptr ? std::string{} : reinterpret_cast<const char*>(text);
}

} // namespace

DataStore::~DataStore() { close(); }

bool DataStore::open(const std::string& fileName) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ != nullptr) {
        return true;
    }
    sqlite3* opened = nullptr;
    const int result = sqlite3_open_v2(fileName.c_str(), &opened,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                           SQLITE_OPEN_FULLMUTEX,
                                       nullptr);
    if (result != SQLITE_OK) {
        lastError_ = opened != nullptr ? sqlite3_errmsg(opened) : "sqlite3_open_v2 failed";
        std::cerr << "SQLite open error: " << lastError_ << '\n';
        if (opened != nullptr) {
            sqlite3_close(opened);
        }
        return false;
    }
    database_ = opened;
    sqlite3_busy_timeout(database_, 3000);
    if (!executeLocked("PRAGMA foreign_keys=ON;") ||
        !executeLocked("PRAGMA journal_mode=WAL;") || !migrateLocked()) {
        sqlite3_close(database_);
        database_ = nullptr;
        return false;
    }
    lastError_.clear();
    return true;
}

void DataStore::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr) {
        return;
    }
    const int result = sqlite3_close(database_);
    if (result != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(database_);
        std::cerr << "SQLite close error: " << lastError_ << '\n';
        return;
    }
    database_ = nullptr;
}

bool DataStore::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return database_ != nullptr;
}

std::string DataStore::lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void DataStore::setErrorLocked(const std::string& context) {
    lastError_ = context + ": " + (database_ != nullptr ? sqlite3_errmsg(database_) : "database closed");
    std::cerr << "SQLite error: " << lastError_ << '\n';
}

bool DataStore::executeLocked(const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(database_, sql, nullptr, nullptr, &message) == SQLITE_OK) {
        return true;
    }
    lastError_ = message != nullptr ? message : sqlite3_errmsg(database_);
    std::cerr << "SQLite error: " << lastError_ << '\n';
    sqlite3_free(message);
    return false;
}

bool DataStore::migrateLocked() {
    if (!executeLocked("BEGIN IMMEDIATE;")) {
        return false;
    }
    const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS experiments (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at_utc TEXT NOT NULL,
    ended_at_utc TEXT,
    resin_grade TEXT NOT NULL,
    recipe_version TEXT NOT NULL,
    reactor TEXT NOT NULL,
    sensor TEXT NOT NULL,
    device_serial_number TEXT NOT NULL,
    operator_name TEXT NOT NULL,
    notes TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS electrical_measurements (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_id INTEGER NOT NULL REFERENCES experiments(id),
    sequence_id INTEGER NOT NULL,
    measured_at_utc TEXT NOT NULL,
    requested_frequency_hz INTEGER NOT NULL,
    returned_frequency_hz INTEGER NOT NULL,
    measured_value_1 REAL,
    measured_value_2 REAL,
    mode INTEGER NOT NULL,
    measurement_range INTEGER NOT NULL,
    slow INTEGER NOT NULL,
    offset_raw INTEGER NOT NULL,
    avp INTEGER NOT NULL,
    auto_parameter INTEGER NOT NULL,
    scheme INTEGER NOT NULL,
    overload INTEGER NOT NULL,
    done INTEGER NOT NULL,
    valid INTEGER NOT NULL,
    raw_packet BLOB NOT NULL,
    diagnostic TEXT NOT NULL DEFAULT '',
    UNIQUE(experiment_id, sequence_id)
);
CREATE INDEX IF NOT EXISTS idx_measurements_experiment_time
    ON electrical_measurements(experiment_id, measured_at_utc);
CREATE TABLE IF NOT EXISTS laboratory_samples (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_id INTEGER NOT NULL REFERENCES experiments(id),
    sample_number INTEGER NOT NULL,
    sampled_at_utc TEXT NOT NULL,
    result_entered_at_utc TEXT,
    acid_number_mg_koh_g REAL,
    conditional_viscosity_seconds REAL,
    analysis_temperature_c REAL,
    method_instrument TEXT NOT NULL DEFAULT '',
    state TEXT NOT NULL,
    valid INTEGER NOT NULL DEFAULT 1,
    notes TEXT NOT NULL DEFAULT '',
    UNIQUE(experiment_id, sample_number)
);
CREATE TABLE IF NOT EXISTS experiment_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_id INTEGER NOT NULL REFERENCES experiments(id),
    event_at_utc TEXT NOT NULL,
    event_type TEXT NOT NULL,
    comment TEXT NOT NULL DEFAULT ''
);
PRAGMA user_version=2;
)SQL";
    if (!executeLocked(schema)) {
        executeLocked("ROLLBACK;");
        return false;
    }
    if (!executeLocked("COMMIT;")) {
        executeLocked("ROLLBACK;");
        return false;
    }
    return true;
}

std::string DataStore::utcMilliseconds(std::chrono::system_clock::time_point time) {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        time.time_since_epoch());
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(milliseconds);
    const auto fraction = milliseconds - seconds;
    const std::time_t value = static_cast<std::time_t>(seconds.count());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
        << std::setfill('0') << fraction.count() << 'Z';
    return out.str();
}

std::string DataStore::utcNowMilliseconds() {
    return utcMilliseconds(std::chrono::system_clock::now());
}

std::int64_t DataStore::beginExperiment(const ExperimentMetadata& metadata) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (database_ == nullptr) {
        lastError_ = "begin experiment: database is not open";
        return 0;
    }
    const char* sql = R"SQL(INSERT INTO experiments
(started_at_utc,resin_grade,recipe_version,reactor,sensor,device_serial_number,operator_name,notes)
VALUES(?,?,?,?,?,?,?,?);)SQL";
    auto statement = prepare(database_, sql, lastError_);
    if (!statement) return 0;
    const std::string now = utcNowMilliseconds();
    if (!bindText(statement.get(), 1, now) || !bindText(statement.get(), 2, metadata.resinGrade) ||
        !bindText(statement.get(), 3, metadata.recipeVersion) ||
        !bindText(statement.get(), 4, metadata.reactor) || !bindText(statement.get(), 5, metadata.sensor) ||
        !bindText(statement.get(), 6, metadata.deviceSerialNumber) ||
        !bindText(statement.get(), 7, metadata.operatorName) || !bindText(statement.get(), 8, metadata.notes) ||
        sqlite3_step(statement.get()) != SQLITE_DONE) {
        setErrorLocked("begin experiment");
        return 0;
    }
    return sqlite3_last_insert_rowid(database_);
}

bool DataStore::finishExperiment(std::int64_t experimentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto statement = prepare(database_, "UPDATE experiments SET ended_at_utc=? WHERE id=? AND ended_at_utc IS NULL;", lastError_);
    if (!statement) return false;
    const std::string now = utcNowMilliseconds();
    if (!bindText(statement.get(), 1, now) || sqlite3_bind_int64(statement.get(), 2, experimentId) != SQLITE_OK ||
        sqlite3_step(statement.get()) != SQLITE_DONE) {
        setErrorLocked("finish experiment");
        return false;
    }
    return sqlite3_changes(database_) == 1;
}

bool DataStore::insertMeasurement(std::int64_t experimentId, const DeviceResponse& r) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = R"SQL(INSERT INTO electrical_measurements
(experiment_id,sequence_id,measured_at_utc,requested_frequency_hz,returned_frequency_hz,
 measured_value_1,measured_value_2,mode,measurement_range,slow,offset_raw,avp,auto_parameter,
 scheme,overload,done,valid,raw_packet,diagnostic)
VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);)SQL";
    auto s = prepare(database_, sql, lastError_);
    if (!s) return false;
    const std::string timestamp = utcMilliseconds(r.timestamp);
    static const std::uint8_t emptyBlob = 0;
    const void* rawData = r.rawPacket.empty() ? static_cast<const void*>(&emptyBlob)
                                               : static_cast<const void*>(r.rawPacket.data());
    const bool ok = sqlite3_bind_int64(s.get(), 1, experimentId) == SQLITE_OK &&
        sqlite3_bind_int64(s.get(), 2, static_cast<sqlite3_int64>(r.sequenceId)) == SQLITE_OK &&
        bindText(s.get(), 3, timestamp) && sqlite3_bind_int64(s.get(), 4, r.requestedFrequency) == SQLITE_OK &&
        sqlite3_bind_int64(s.get(), 5, r.returnedFrequency) == SQLITE_OK &&
        sqlite3_bind_double(s.get(), 6, r.value1) == SQLITE_OK &&
        sqlite3_bind_double(s.get(), 7, r.value2) == SQLITE_OK &&
        sqlite3_bind_int(s.get(), 8, r.mode) == SQLITE_OK && sqlite3_bind_int(s.get(), 9, r.range) == SQLITE_OK &&
        sqlite3_bind_int(s.get(), 10, r.slow) == SQLITE_OK && sqlite3_bind_int(s.get(), 11, r.offset) == SQLITE_OK &&
        sqlite3_bind_int(s.get(), 12, r.avp) == SQLITE_OK && sqlite3_bind_int(s.get(), 13, r.autoParameter) == SQLITE_OK &&
        sqlite3_bind_int(s.get(), 14, r.scheme) == SQLITE_OK && sqlite3_bind_int(s.get(), 15, r.overload) == SQLITE_OK &&
        sqlite3_bind_int(s.get(), 16, r.done) == SQLITE_OK && sqlite3_bind_int(s.get(), 17, r.valid) == SQLITE_OK &&
        sqlite3_bind_blob(s.get(), 18, rawData, static_cast<int>(r.rawPacket.size()), SQLITE_TRANSIENT) == SQLITE_OK &&
        bindText(s.get(), 19, r.diagnostic);
    if (!ok || sqlite3_step(s.get()) != SQLITE_DONE) {
        setErrorLocked("insert measurement");
        return false;
    }
    return true;
}

std::int64_t DataStore::collectSample(std::int64_t experimentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = R"SQL(INSERT INTO laboratory_samples
(experiment_id,sample_number,sampled_at_utc,state,valid)
VALUES(?,(SELECT COALESCE(MAX(sample_number),0)+1 FROM laboratory_samples WHERE experiment_id=?),?,'Ожидается анализ',1);)SQL";
    auto s = prepare(database_, sql, lastError_);
    if (!s) return 0;
    const std::string now = utcNowMilliseconds();
    if (sqlite3_bind_int64(s.get(), 1, experimentId) != SQLITE_OK ||
        sqlite3_bind_int64(s.get(), 2, experimentId) != SQLITE_OK || !bindText(s.get(), 3, now) ||
        sqlite3_step(s.get()) != SQLITE_DONE) {
        setErrorLocked("collect sample");
        return 0;
    }
    return sqlite3_last_insert_rowid(database_);
}

bool DataStore::enterSampleResult(std::int64_t sampleId, const LaboratoryResult& r) {
    const auto negative = [](const std::optional<double>& value) { return value && *value < 0.0; };
    if (negative(r.acidNumberMgKohG) || negative(r.conditionalViscositySeconds) ||
        negative(r.analysisTemperatureC)) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = "laboratory values cannot be negative";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = R"SQL(UPDATE laboratory_samples SET result_entered_at_utc=?,
acid_number_mg_koh_g=?,conditional_viscosity_seconds=?,analysis_temperature_c=?,
method_instrument=?,state='Анализ введён',notes=? WHERE id=?;)SQL";
    auto s = prepare(database_, sql, lastError_);
    if (!s) return false;
    const std::string now = utcNowMilliseconds();
    if (!bindText(s.get(), 1, now) || !bindOptional(s.get(), 2, r.acidNumberMgKohG) ||
        !bindOptional(s.get(), 3, r.conditionalViscositySeconds) ||
        !bindOptional(s.get(), 4, r.analysisTemperatureC) || !bindText(s.get(), 5, r.methodInstrument) ||
        !bindText(s.get(), 6, r.notes) || sqlite3_bind_int64(s.get(), 7, sampleId) != SQLITE_OK ||
        sqlite3_step(s.get()) != SQLITE_DONE) {
        setErrorLocked("enter sample result");
        return false;
    }
    return sqlite3_changes(database_) == 1;
}

bool DataStore::setSampleValidity(std::int64_t sampleId, bool valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto s = prepare(database_, "UPDATE laboratory_samples SET valid=?,state=CASE WHEN ? THEN state ELSE 'Недостоверна' END WHERE id=?;", lastError_);
    if (!s) return false;
    if (sqlite3_bind_int(s.get(), 1, valid) != SQLITE_OK || sqlite3_bind_int(s.get(), 2, valid) != SQLITE_OK ||
        sqlite3_bind_int64(s.get(), 3, sampleId) != SQLITE_OK || sqlite3_step(s.get()) != SQLITE_DONE) {
        setErrorLocked("set sample validity");
        return false;
    }
    return sqlite3_changes(database_) == 1;
}

bool DataStore::addEvent(std::int64_t experimentId, const std::string& type, const std::string& comment) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto s = prepare(database_, "INSERT INTO experiment_events(experiment_id,event_at_utc,event_type,comment) VALUES(?,?,?,?);", lastError_);
    if (!s) return false;
    const std::string now = utcNowMilliseconds();
    if (sqlite3_bind_int64(s.get(), 1, experimentId) != SQLITE_OK || !bindText(s.get(), 2, now) ||
        !bindText(s.get(), 3, type) || !bindText(s.get(), 4, comment) || sqlite3_step(s.get()) != SQLITE_DONE) {
        setErrorLocked("add event");
        return false;
    }
    return true;
}

std::vector<LaboratorySample> DataStore::samples(std::int64_t experimentId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LaboratorySample> rows;
    auto s = prepare(database_, R"SQL(SELECT id,experiment_id,sample_number,sampled_at_utc,
COALESCE(result_entered_at_utc,''),acid_number_mg_koh_g,conditional_viscosity_seconds,
analysis_temperature_c,method_instrument,state,valid,notes FROM laboratory_samples
WHERE experiment_id=? ORDER BY sample_number;)SQL", lastError_);
    if (!s || sqlite3_bind_int64(s.get(), 1, experimentId) != SQLITE_OK) return rows;
    while (sqlite3_step(s.get()) == SQLITE_ROW) {
        LaboratorySample row;
        row.id = sqlite3_column_int64(s.get(), 0); row.experimentId = sqlite3_column_int64(s.get(), 1);
        row.sampleNumber = sqlite3_column_int64(s.get(), 2); row.sampledAtUtc = textColumn(s.get(), 3);
        row.resultEnteredAtUtc = textColumn(s.get(), 4); row.result.acidNumberMgKohG = optionalColumn(s.get(), 5);
        row.result.conditionalViscositySeconds = optionalColumn(s.get(), 6);
        row.result.analysisTemperatureC = optionalColumn(s.get(), 7); row.result.methodInstrument = textColumn(s.get(), 8);
        row.state = textColumn(s.get(), 9); row.valid = sqlite3_column_int(s.get(), 10) != 0;
        row.result.notes = textColumn(s.get(), 11); rows.push_back(std::move(row));
    }
    return rows;
}

std::int64_t DataStore::measurementCount(std::int64_t experimentId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto s = prepare(database_, "SELECT COUNT(*) FROM electrical_measurements WHERE experiment_id=?;", lastError_);
    if (!s || sqlite3_bind_int64(s.get(), 1, experimentId) != SQLITE_OK || sqlite3_step(s.get()) != SQLITE_ROW) return -1;
    return sqlite3_column_int64(s.get(), 0);
}
