#pragma once

#include <windows.h>

namespace Dimps {
	namespace Pad {
		void Locate(HMODULE peRoot);

		struct System {
			struct PlayerEntry {
				BYTE pad[80];

				static int* DeviceType(PlayerEntry* e);
				static int* DeviceIndex(PlayerEntry* e);
				static int* AssignedController(PlayerEntry* e);
			};

			typedef struct __publicMethods {
				DWORD(System::* GetButtons_On)(int pindex);
				DWORD(System::* GetButtons_Mapped)(int pindex);
				DWORD(System::* GetButtons_Rising)(int pindex);
				DWORD(System::* GetButtons_Falling)(int pindex);
				DWORD(System::* GetButtons_RisingWithRepeat)(int pindex);
				int(System::* GetAllDeviceCount)();
				int(System::* GetOKDeviceCount)();
				char*(System::* GetDeviceName)(int dindex);
				int (System::* GetDeviceIndexForPlayer)(int pindex);
				int (System::* GetDeviceTypeForPlayer)(int pindex);
				int (System::* GetAssigmentStatusForPlayer)(int pindex);
			} __publicMethods;

			typedef struct __staticMethods {
				System* (*GetSingleton)();
			} __staticMethods;

			static void Locate(HMODULE peRoot);
			static __publicMethods publicMethods;
			static __staticMethods staticMethods;
		};
	}
}