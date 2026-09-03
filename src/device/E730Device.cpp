#include "E730Device.h"

const DeviceStatus& getStatus()
{
	return DeviceStatus();
}

void E730Device::setState(DeviceState newState)
{
	std::lock_guard<std::mutex> lock(statusMutex);
	status.state = newState;
}

void E730Device::setLastDeviceResponce(DeviceResponse ldr)
{
	std::lock_guard<std::mutex> lock(statusMutex);
	status.lastDeviceResponce = ldr;
}
