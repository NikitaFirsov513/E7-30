#include "ui.h"

#include "imgui.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<std::string> enumerateComPorts() {
    std::vector<std::string> ports{"EMULATOR"};
    for (int number = 1; number <= 256; ++number) {
        const std::string name = "COM" + std::to_string(number);
        std::array<char, 512> target{};
        if (QueryDosDeviceA(name.c_str(), target.data(), static_cast<DWORD>(target.size())) != 0U) {
            ports.push_back(name);
        }
    }
    return ports;
}

const char* stateText(DeviceState state) {
    switch (state) {
    case DeviceState::CONNECTED: return "Подключён";
    case DeviceState::RECORDING: return "Регистрация";
    case DeviceState::ERR: return "Ошибка";
    case DeviceState::DISCONNECTED: return "Отключён";
    }
    return "Неизвестно";
}

ImVec4 stateColor(DeviceState state) {
    switch (state) {
    case DeviceState::CONNECTED: return {0.2F, 0.8F, 0.2F, 1.0F};
    case DeviceState::RECORDING: return {0.2F, 0.6F, 1.0F, 1.0F};
    case DeviceState::ERR: return {1.0F, 0.2F, 0.2F, 1.0F};
    case DeviceState::DISCONNECTED: return {0.6F, 0.6F, 0.6F, 1.0F};
    }
    return {0.6F, 0.6F, 0.6F, 1.0F};
}

std::string utcText(std::chrono::system_clock::time_point time) {
    return time.time_since_epoch().count() == 0 ? "—" : DataStore::utcMilliseconds(time);
}

void optionalInput(const char* label, bool& enabled, double& value) {
    ImGui::Checkbox((std::string("##") + label).c_str(), &enabled);
    ImGui::SameLine();
    ImGui::BeginDisabled(!enabled);
    ImGui::InputDouble(label, &value, 0.0, 0.0, "%.3f");
    ImGui::EndDisabled();
}

} // namespace

void UI::updateHistory(const DeviceResponse& response) {
    if (response.sequenceId == 0 || response.sequenceId == lastPlottedSequence_) return;
    lastPlottedSequence_ = response.sequenceId;
    if (!response.valid) return;
    if (plottedFrequency_ != response.returnedFrequency) {
        history_.clear();
        plottedFrequency_ = response.returnedFrequency;
    }
    history_.push_back({response.timestamp, response.value1, response.value2});
    while (history_.size() > kMaxHistory) history_.pop_front();
}

void UI::plotHistory(const char* label, bool secondValue) {
    if (history_.empty()) return;
    const ImVec2 size{ImGui::GetContentRegionAvail().x, 110.0F};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, size);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRect(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(110, 110, 110, 255));
    float minimum = FLT_MAX;
    float maximum = -FLT_MAX;
    for (const auto& point : history_) {
        const float value = secondValue ? point.value2 : point.value1;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (maximum <= minimum) maximum = minimum + 1.0F;
    const auto first = history_.front().time;
    const auto last = history_.back().time;
    const double span = std::max(0.001, std::chrono::duration<double>(last - first).count());
    ImVec2 previous{};
    bool havePrevious = false;
    for (const auto& point : history_) {
        const double elapsed = std::chrono::duration<double>(point.time - first).count();
        const float value = secondValue ? point.value2 : point.value1;
        const ImVec2 current{
            origin.x + static_cast<float>(elapsed / span) * size.x,
            origin.y + size.y - ((value - minimum) / (maximum - minimum)) * size.y};
        if (havePrevious) draw->AddLine(previous, current, IM_COL32(80, 190, 255, 255), 1.5F);
        previous = current;
        havePrevious = true;
    }
    ImGui::Text("%s — %s", utcText(first).c_str(), utcText(last).c_str());
}

void UI::ShowDeviceControlWindow() {
    const DeviceStatus status = deviceCommunicator_->getLastData();
    updateHistory(status.lastDeviceResponse);
    ImGui::Begin("Е7-30 — измерения");
    ImGui::TextColored(stateColor(status.state), "Связь: %s", stateText(status.state));
    const std::string responseTime = utcText(status.lastSuccessfulResponse);
    ImGui::Text("Последний успешный ответ: %s", responseTime.c_str());
    ImGui::Text("Sequence ID: %llu", static_cast<unsigned long long>(status.lastDeviceResponse.sequenceId));
    ImGui::Text("Частота: запрошено %u Гц, получено %d Гц",
                status.lastDeviceResponse.requestedFrequency,
                status.lastDeviceResponse.returnedFrequency);
    ImGui::Text("Режим: %u, диапазон: %u", status.lastDeviceResponse.mode,
                status.lastDeviceResponse.range);
    ImGui::Text("Готово: %s | Перегрузка: %s | Достоверно: %s",
                status.lastDeviceResponse.done ? "да" : "нет",
                status.lastDeviceResponse.overload ? "да" : "нет",
                status.lastDeviceResponse.valid ? "да" : "нет");
    ImGui::Text("Значение 1: %.7g", status.lastDeviceResponse.value1);
    ImGui::Text("Значение 2: %.7g", status.lastDeviceResponse.value2);
    ImGui::TextDisabled("Единицы и смысл двух значений зависят от режима и требуют документации.");

    static int frequencyHz = 1000;
    ImGui::InputInt("Заданная частота, Гц", &frequencyHz);
    frequencyHz = std::clamp(frequencyHz, 25, 3000000);
    if (ImGui::Button("Установить частоту")) {
        deviceCommunicator_->setFrequency(static_cast<std::uint32_t>(frequencyHz));
    }
    ImGui::SameLine();
    if (ImGui::Button("Сброс прибора")) deviceCommunicator_->resetDevice();
    static int intervalSeconds = 1;
    ImGui::InputInt("Интервал опроса, с", &intervalSeconds);
    if (intervalSeconds < 1) intervalSeconds = 1;
    if (ImGui::Button("Применить интервал")) deviceCommunicator_->setStatusInterval(intervalSeconds);

    if (!history_.empty()) {
        ImGui::Separator();
        ImGui::Text("Графики одной частоты: %d Гц", plottedFrequency_);
        plotHistory("##value1", false);
        plotHistory("##value2", true);
    }
    if (!status.lastDeviceResponse.diagnostic.empty()) {
        ImGui::TextWrapped("Диагностика: %s", status.lastDeviceResponse.diagnostic.c_str());
    }
    if (!status.lastError.empty()) {
        ImGui::TextColored({1, 0.2F, 0.2F, 1}, "Ошибка: %s", status.lastError.c_str());
    }
    ImGui::End();
}

void UI::ShowExperimentWindow() {
    ImGui::Begin("Эксперимент / варка");
    const auto active = deviceCommunicator_->activeExperimentId();
    ImGui::Text("Активный эксперимент: %lld", static_cast<long long>(active));
    static std::array<char, 128> resin{};
    static std::array<char, 128> recipe{};
    static std::array<char, 128> reactor{};
    static std::array<char, 128> sensor{};
    static std::array<char, 128> serial{};
    static std::array<char, 128> operatorName{};
    static std::array<char, 512> notes{};
    if (active == 0) {
        ImGui::InputText("Марка смолы", resin.data(), resin.size());
        ImGui::InputText("Версия рецептуры", recipe.data(), recipe.size());
        ImGui::InputText("Реактор", reactor.data(), reactor.size());
        ImGui::InputText("Датчик", sensor.data(), sensor.size());
        ImGui::InputText("Серийный номер прибора", serial.data(), serial.size());
        ImGui::InputText("Оператор", operatorName.data(), operatorName.size());
        ImGui::InputTextMultiline("Примечание", notes.data(), notes.size());
        if (ImGui::Button("Создать эксперимент")) {
            ExperimentMetadata m{resin.data(), recipe.data(), reactor.data(), sensor.data(),
                                 serial.data(), operatorName.data(), notes.data()};
            deviceCommunicator_->beginExperiment(m);
        }
    } else {
        if (!deviceCommunicator_->isRunning()) {
            if (ImGui::Button("Начать регистрацию")) deviceCommunicator_->start();
        } else if (ImGui::Button("Остановить регистрацию")) {
            deviceCommunicator_->stop();
        }
        if (ImGui::Button("Завершить эксперимент")) deviceCommunicator_->finishExperiment();
    }
    ImGui::End();
}

void UI::ShowLaboratoryWindow() {
    ImGui::Begin("Лабораторные пробы");
    if (deviceCommunicator_->activeExperimentId() == 0) {
        ImGui::TextUnformatted("Сначала создайте эксперимент.");
        ImGui::End();
        return;
    }
    if (ImGui::Button("Отобрать пробу")) deviceCommunicator_->collectLaboratorySample();
    const auto rows = deviceCommunicator_->laboratorySamples();
    static std::int64_t selectedId = 0;
    static std::int64_t selectedExperiment = 0;
    static bool hasAcid = false, hasViscosity = false, hasTemperature = false;
    static double acid = 0.0, viscosity = 0.0, temperature = 0.0;
    static std::array<char, 256> method{};
    static std::array<char, 512> comment{};
    if (selectedExperiment != deviceCommunicator_->activeExperimentId()) {
        selectedExperiment = deviceCommunicator_->activeExperimentId();
        selectedId = 0;
    }
    for (const auto& row : rows) {
        const std::string label = "Проба №" + std::to_string(row.sampleNumber) + " | " + row.sampledAtUtc +
                                  " | " + row.state + (row.valid ? "" : " | недостоверна");
        if (ImGui::Selectable(label.c_str(), selectedId == row.id)) {
            selectedId = row.id;
            hasAcid = row.result.acidNumberMgKohG.has_value();
            hasViscosity = row.result.conditionalViscositySeconds.has_value();
            hasTemperature = row.result.analysisTemperatureC.has_value();
            acid = row.result.acidNumberMgKohG.value_or(0.0);
            viscosity = row.result.conditionalViscositySeconds.value_or(0.0);
            temperature = row.result.analysisTemperatureC.value_or(0.0);
            std::snprintf(method.data(), method.size(), "%s", row.result.methodInstrument.c_str());
            std::snprintf(comment.data(), comment.size(), "%s", row.result.notes.c_str());
        }
    }
    if (selectedId != 0) {
        ImGui::Separator();
        optionalInput("Кислотное число, мг KOH/г", hasAcid, acid);
        optionalInput("Условная вязкость, с", hasViscosity, viscosity);
        optionalInput("Температура анализа, °C", hasTemperature, temperature);
        ImGui::InputText("Метод / прибор", method.data(), method.size());
        ImGui::InputTextMultiline("Комментарий", comment.data(), comment.size());
        const bool nonNegative = (!hasAcid || acid >= 0.0) && (!hasViscosity || viscosity >= 0.0) &&
                                 (!hasTemperature || temperature >= 0.0);
        ImGui::BeginDisabled(!nonNegative);
        if (ImGui::Button("Сохранить результат")) {
            LaboratoryResult r;
            if (hasAcid) r.acidNumberMgKohG = acid;
            if (hasViscosity) r.conditionalViscositySeconds = viscosity;
            if (hasTemperature) r.analysisTemperatureC = temperature;
            r.methodInstrument = method.data();
            r.notes = comment.data();
            deviceCommunicator_->enterLaboratoryResult(selectedId, r);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Пометить недостоверной")) {
            deviceCommunicator_->setLaboratorySampleValidity(selectedId, false);
        }
        if (!nonNegative) ImGui::TextColored({1, 0.2F, 0.2F, 1}, "Значения не могут быть отрицательными");
    }
    ImGui::End();
}

void UI::ShowMainDockspace() {
    static bool showPortDialog = false;
    static int selectedPort = 0;
    static std::vector<std::string> ports = enumerateComPorts();
    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::Begin("MainDockSpace", nullptr, flags);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Связь")) {
            if (!deviceCommunicator_->isConnected()) {
                if (ImGui::MenuItem("Подключить / переподключить")) {
                    ports = enumerateComPorts(); selectedPort = 0; showPortDialog = true;
                }
            } else if (ImGui::MenuItem("Отключить")) {
                deviceCommunicator_->disconnect();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    if (showPortDialog) { ImGui::OpenPopup("Выбор COM-порта"); showPortDialog = false; }
    if (ImGui::BeginPopupModal("Выбор COM-порта", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Combo("Порт", &selectedPort, [](void* data, int index, const char** out) {
            const auto& values = *static_cast<std::vector<std::string>*>(data);
            if (index < 0 || static_cast<std::size_t>(index) >= values.size()) return false;
            *out = values[static_cast<std::size_t>(index)].c_str(); return true;
        }, &ports, static_cast<int>(ports.size()));
        if (ImGui::Button("Подключить") && selectedPort >= 0 &&
            static_cast<std::size_t>(selectedPort) < ports.size() &&
            deviceCommunicator_->tryConnect(ports[static_cast<std::size_t>(selectedPort)])) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::DockSpace(ImGui::GetID("E730DockSpace"));
    ImGui::End();
}

void UI::Render() {
    ShowMainDockspace();
    ShowDeviceControlWindow();
    ShowExperimentWindow();
    ShowLaboratoryWindow();
}
