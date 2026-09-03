#include "imgui.h"
#include "../DeviceCommunicator.h"
#include <string>

class UI {
private:
	DeviceCommunicator* deviceCommunicator;

public:
	UI() : deviceCommunicator(nullptr) {}
	UI(DeviceCommunicator& communicator) : deviceCommunicator(&communicator) {}
	~UI();

	void ShowDeviceControlWindow();
	void ShowMainDockspace();
	void Render();

};