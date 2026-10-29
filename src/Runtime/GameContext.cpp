#include "Runtime/GameContext.h"

void OpenWind::Game::Initialize()
{
	player = RE::PlayerCharacter::GetSingleton();
	sky = RE::Sky::GetSingleton();
	ui = RE::UI::GetSingleton();
	tes = RE::TES::GetSingleton();
	isVR = REL::Module::IsVR();
}
