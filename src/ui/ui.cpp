#include "ui.h"
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <vector>
#include <string>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>

#include <iostream>


#pragma comment(lib, "setupapi.lib")

std::vector<std::string> enumerateComPorts() {
	std::vector<std::string> comPorts;

	for (int i = 1; i <= 256; ++i) {
		std::string portName = "COM" + std::to_string(i);
		std::string fullName = "\\\\.\\" + portName;

		HANDLE hPort = CreateFileA(fullName.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0, nullptr, OPEN_EXISTING,
			0, nullptr);

		if (hPort != INVALID_HANDLE_VALUE) {
			comPorts.push_back(portName);
			CloseHandle(hPort);
		}
		else {
			// ѕроверим ошибку Ч если порт зан€т, всЄ равно добавим (он существует)
			if (GetLastError() == ERROR_ACCESS_DENIED) {
				comPorts.push_back(portName);
			}
		}
	}

	return comPorts;
}

void TextCentered(std::string text, int numHeightUnits) {

	auto windowWidth = ImGui::GetWindowSize().x;
	auto textWidth = ImGui::CalcTextSize(text.c_str()).x;
	float frameHeight = ImGui::GetFrameHeight();
	float myHeight = (numHeightUnits);
	float delta = myHeight - frameHeight;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + delta * 0.5f);
	ImGui::Text(text.c_str());

}

static std::string formatSmartFloat(double value, int precision = 6)
{
	std::ostringstream ss;

	double absVal = std::fabs(value);
	// ≈сли число очень маленькое (<0.001) или очень большое (>1e6),
	// показываем в экспоненциальной форме
	if ((absVal > 0 && absVal < 1e-3) || absVal >= 1e6)
		ss << std::scientific << std::setprecision(precision);
	else
		ss << std::fixed << std::setprecision(precision);

	ss << value;
	return ss.str();
}

UI::~UI()
{


}

void UI::ShowDeviceControlWindow()
{
	DeviceStatus status = deviceCommunicator->getLastData();

	ImGui::Begin("E7-30 Control");

	// --- Device state ---
	ImVec4 color;
	const char* stateText = "";

	switch (status.state) {
	case DeviceState::CONNECTED:
		color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // зелЄный
		stateText = "Connected";
		break;
	case DeviceState::RECORDING:
		color = ImVec4(0.2f, 0.6f, 1.0f, 1.0f); // голубой
		stateText = "Recording";
		break;
	case DeviceState::ERR:
		color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // красный
		stateText = "Error";
		break;
	case DeviceState::DISCONNECTED:
	default:
		color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); // серый
		stateText = "Disconnected";
		break;
	}

	ImGui::TextColored(color, "State: %s", stateText);

	ImGui::Separator();

	// --- AVP control ---
	if (ImGui::Button("Enable AVP")) {
		//deviceCommunicator->sendCommand({ 0xAA, 65 });
	}
	ImGui::SameLine();
	if (ImGui::Button("Disable AVP")) {
		//deviceCommunicator->sendCommand({ 0xAA, 66 });
	}

	ImGui::Separator();

	// --- Frequency input ---
	static int frequencyHz = 1000;
	ImGui::InputInt("Frequency (Hz)", &frequencyHz);
	if (ImGui::Button("Set Frequency")) {
		//deviceCommunicator->setFrequency(static_cast<uint32_t>(frequencyHz));
		uint32_t freq = static_cast<uint32_t>(frequencyHz);
		std::vector<uint8_t> cmd = {
			0xAA, 67,
			static_cast<uint8_t>((freq >> 24) & 0xFF),
			static_cast<uint8_t>((freq >> 16) & 0xFF),
			static_cast<uint8_t>((freq >> 8) & 0xFF),
			static_cast<uint8_t>(freq & 0xFF)
		};
		deviceCommunicator->sendCommand(cmd);
	}

	// --- Frequency input ---
	static int status_interval = 1;
	ImGui::InputInt("Status interval (Sec)", &status_interval);

	if (status_interval < 1)
		status_interval = 1;

	if (ImGui::Button("Set status interval")) {
		deviceCommunicator->setStatusInterval(status_interval);
	}



	ImGui::Separator();

	// --- Reset ---
	if (ImGui::Button("Reset to Default")) {
		deviceCommunicator->sendCommand({ 0xAA, 71 });
	}

	/*ImGui::Separator();

	// --- Measurement info ---
	if (ImGui::Button("Get Measurement Info")) {
		deviceCommunicator->sendCommand({ 0xAA, 72, 0x00 });
	}*/

	ImGui::Separator();

	// --- Parameter selection for plotting ---


	// --- Data history for plotting ---
	constexpr int MAX_HISTORY = 500;
	static std::vector<float> historyZ, historyPhi;

	if (status.state == DeviceState::CONNECTED || status.state == DeviceState::RECORDING) {
		historyZ.push_back(status.lastDeviceResponce.Z);
		historyPhi.push_back(status.lastDeviceResponce.phi * 57.2958f);


		if (historyZ.size() > MAX_HISTORY) {
			historyZ.erase(historyZ.begin());
			historyPhi.erase(historyPhi.begin());

		}
	}

	// --- Textual output of last parameters with checkboxes ---
	ImGui::Text("Last Measurement:");

	static bool showZ = true;
	static bool showPhi = false;
	static bool showRs = false;
	static bool showXs = false;
	static bool showRp = false;
	static bool showXp = false;

	if (ImGui::Checkbox("##chkZ", &showZ)) {}
	ImGui::SameLine();
	ImGui::Text("Z   = %s Ohm", formatSmartFloat(status.lastDeviceResponce.Z).c_str());

	if (ImGui::Checkbox("##chkPhi", &showPhi)) {}
	ImGui::SameLine();
	ImGui::Text("Phi = %s deg", formatSmartFloat(status.lastDeviceResponce.phi * 57.2958f).c_str());



	ImGui::Text("Frequency = %d Hz", status.lastDeviceResponce.frequency);

	// --- Plotting ---
	if (showZ && !historyZ.empty())
		ImGui::PlotLines("Z", historyZ.data(), (int)historyZ.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0, 80));
	if (showPhi && !historyPhi.empty())
		ImGui::PlotLines("Phi", historyPhi.data(), (int)historyPhi.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0, 80));


	// --- Error output ---
	if (!status.lastError.empty()) {
		ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", status.lastError.c_str());
	}

	ImGui::End();
}

void UI::ShowMainDockspace()
{

	static bool showPortDialog = false;
	static int selectedPortIndex = -1;
	static std::vector<std::string> portList = enumerateComPorts();

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	ImGui::Begin("MainDockSpace", nullptr, window_flags);
	ImGui::PopStyleVar(2);

	// ћеню-бар
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{

			switch (deviceCommunicator->getDeviseState()) {
			case DeviceState::CONNECTED:
				if (ImGui::MenuItem("Start record")) {
					deviceCommunicator->start();
				}
				if (ImGui::MenuItem("Stop record", nullptr, false, false)) {}
				break;
			case DeviceState::RECORDING:
				if (ImGui::MenuItem("Start record", nullptr, false, false)) {}
				if (ImGui::MenuItem("Stop record")) {
					deviceCommunicator->stop();
				}
				break;
			case DeviceState::ERR:
				if (ImGui::MenuItem("Start record", nullptr, false, false)) {}
				if (ImGui::MenuItem("Stop record", nullptr, false, false)) {}
				break;
			case DeviceState::DISCONNECTED:
				if (ImGui::MenuItem("Connected")) {

					portList = enumerateComPorts(); // реализовать
					selectedPortIndex = (int)portList.size() > 0 ? 0 : -1;
					showPortDialog = true;
					//deviceCommunicator->tryConnect("COM8");
				}
				if (ImGui::MenuItem("Start record", nullptr, false, false)) {}
				if (ImGui::MenuItem("Stop record", nullptr, false, false)) {}
				break;
			default:
				break;
			}
			if (ImGui::MenuItem("Save")) { /* TODO */ }
			if (ImGui::MenuItem("Exit")) { /* TODO: set running = false, etc */ }
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z")) { /* TODO */ }
			if (ImGui::MenuItem("Redo", "Ctrl+Y")) { /* TODO */ }
			ImGui::Separator();
			if (ImGui::MenuItem("Cut", "Ctrl+X")) { /* TODO */ }
			if (ImGui::MenuItem("Copy", "Ctrl+C")) { /* TODO */ }
			if (ImGui::MenuItem("Paste", "Ctrl+V")) { /* TODO */ }
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About")) { /* TODO */ }
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	if (showPortDialog) {
		ImGui::OpenPopup("Select COM Port");
		showPortDialog = false;
	}

	if (ImGui::BeginPopupModal("Select COM Port", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Choose a serial port:");
		if (!portList.empty()) {
			ImGui::Combo("Ports", &selectedPortIndex,
				[](void* data, int idx, const char** out) {
					auto& v = *static_cast<std::vector<std::string>*>(data);
					*out = v[idx].c_str();
					return true;
				}, &portList, (int)portList.size(), 5);
		}
		else {
			ImGui::TextColored(ImVec4(1, 0, 0, 1), "No ports found");
		}

		ImGui::Separator();
		if (ImGui::Button("OK", ImVec2(120, 0))) {
			if (selectedPortIndex >= 0) {
				std::string portName = portList[selectedPortIndex];
				// »з строки вида "COM3 Ц USB Serial Port" извлечь им€ COM?
				// ћожно вз€ть до пробела или У Ц Ф
				std::string extracted = portName.substr(0, portName.find(" "));
				if (!deviceCommunicator->tryConnect(extracted)) {
					ImGui::OpenPopup("Connection Failed");
				}
				else {
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::BeginPopup("Connection Failed")) {
			ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to open port: %s",
				selectedPortIndex >= 0 ? portList[selectedPortIndex].c_str() : "");
			if (ImGui::Button("Close")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::EndPopup();
	}

	// Dockspace
	ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

	ImGui::End();


}

void UI::Render()
{
	ShowMainDockspace();
	ShowDeviceControlWindow();
}
