#pragma once

#include <windows.h>

#include "../Dimps/Dimps__Eva.hxx"
#include "../Dimps/Dimps__Game__Battle.hxx"

namespace sf4e {
	namespace Game {
		namespace Battle {
			using Dimps::Eva::Task;

			void Install();

			struct IUnit : Dimps::Game::Battle::IUnit {
				// In order for the compiler to construct this method
				// with __thiscall__, the method needs to be declared
				// in the class declaration, despite the fact that this
				// method isn't actually part of the IUnit interface.
				//
				// This is intentionally not installed as part of IUnit.
				void SharedHudUpdate(Task** task);

				static bool bAllowHudUpdate;
			};
		}
	}
}