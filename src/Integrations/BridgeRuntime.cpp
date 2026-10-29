#include "BridgeRuntime.h"
#include "WindSource.h"

#include <atomic>
#include <fstream>
#include <stdexcept>

namespace OpenWind::Integrations
{
	namespace
	{
		const API unavailableSource{
			sizeof(API), kAPIRevision, sizeof(WindSample), kMaximumBatchSize,
			[]() noexcept { return false; },
			[](const Vector3*, WindSample*, uint32_t) noexcept { return false; }
		};
		std::atomic<const API*> windSource{ &unavailableSource };
	}

	const API& WindSource() noexcept
	{
		return *windSource.load(std::memory_order_acquire);
	}

	namespace BridgeRuntime
	{
		namespace
		{
			std::mutex settingsMutex;
			const Adapter* bridge{};

			void DiscoverSource()
			{
				if (windSource.load(std::memory_order_acquire) != &unavailableSource)
					return;
				const auto module = GetModuleHandleW(L"OpenWind.dll");
				const auto request = module ? reinterpret_cast<RequestAPI>(GetProcAddress(module, "GetOpenWindAPI")) : nullptr;
				const auto* api = request ? request(kAPIRevision) : nullptr;
				if (!api || api->structSize != sizeof(API) ||
					api->sampleSize != sizeof(WindSample) || !api->maximumBatchSize ||
					api->maximumBatchSize > kMaximumBatchSize || !api->IsReady || !api->SampleWind)
					return;
				windSource.store(api, std::memory_order_release);
				logger::info("{} connected to Open Wind sampling API {}", bridge->name, kAPIRevision);
			}

			json Status()
			{
				auto result = bridge->Settings();
				result["success"] = true;
				result["installed"] = true;
				result["registered"] = bridge->IsRegistered();
				result["status"] = bridge->Status();
				result["settingsPath"] = bridge->settingsPath;
				return result;
			}

			void LoadSettings()
			{
				const std::filesystem::path path = bridge->settingsPath;
				if (!std::filesystem::exists(path)) {
					bridge->Apply(json::object(), true);
					return;
				}
				std::ifstream file(path);
				if (!file)
					throw std::runtime_error("Could not open bridge settings.");
				bridge->Apply(json::parse(file), true);
			}

			void SaveSettings()
			{
				const std::filesystem::path path = bridge->settingsPath;
				std::filesystem::create_directories(path.parent_path());
				auto temporary = path;
				temporary += ".tmp";
				{
					std::ofstream file(temporary, std::ios::trunc);
					if (!file)
						throw std::runtime_error("Could not write bridge settings.");
					file << bridge->Settings().dump(2) << '\n';
					file.flush();
					file.close();
					if (!file)
						throw std::runtime_error("Could not flush bridge settings.");
				}
				if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
					throw std::runtime_error("Could not replace bridge settings.");
			}

			void QueueRefresh()
			{
				const auto* tasks = SKSE::GetTaskInterface();
				if (!tasks)
					throw std::runtime_error("The game task interface is unavailable.");
				tasks->AddTask([] {
					try {
						std::lock_guard lock(settingsMutex);
						DiscoverSource();
						bridge->Refresh();
					} catch (const std::exception& error) {
						logger::error("Bridge registration failed: {}", error.what());
					} catch (...) {
						logger::error("Bridge registration failed");
					}
				});
			}

			json Execute(const json& request)
			{
				const auto action = request.at("action").get<std::string>();
				if (action == "set") {
					const auto save = request.value("save", false);
					const auto enabled = bridge->Settings().at("enabled").get<bool>();
					bridge->Apply(request.at("values"), false);
					if (enabled != bridge->Settings().at("enabled").get<bool>())
						QueueRefresh();
					if (save)
						SaveSettings();
				} else if (action == "save") {
					SaveSettings();
				} else if (action == "reload") {
					LoadSettings();
					QueueRefresh();
				} else if (action == "reset") {
					bridge->Apply(json::object(), true);
					QueueRefresh();
				} else if (action == "retry") {
					QueueRefresh();
				} else if (action != "get") {
					throw std::invalid_argument("Unknown bridge action.");
				}
				return Status();
			}

			bool Command(const char* request, void* context, void (*reply)(void*, const char*) noexcept) noexcept
			{
				if (!reply)
					return false;
				try {
					std::string response;
					{
						std::lock_guard lock(settingsMutex);
						if (!bridge)
							throw std::runtime_error("The bridge has not initialized.");
						json result;
						try {
							if (!request)
								throw std::invalid_argument("A bridge request is required.");
							result = Execute(json::parse(request));
						} catch (const std::exception& error) {
							result = Status();
							result["success"] = false;
							result["error"] = error.what();
						}
						response = result.dump();
					}
					reply(context, response.c_str());
				} catch (...) {
					reply(context, R"({"success":false,"installed":true,"error":"The bridge command failed."})");
				}
				return true;
			}

			void OnMessage(SKSE::MessagingInterface::Message* message)
			{
				if (!message)
					return;
				try {
					std::lock_guard lock(settingsMutex);
					if (message->type == SKSE::MessagingInterface::kPostLoad ||
						message->type == SKSE::MessagingInterface::kPostPostLoad ||
						message->type == SKSE::MessagingInterface::kDataLoaded)
						DiscoverSource();
					bridge->OnMessage(message);
				} catch (const std::exception& error) {
					logger::error("Bridge initialization failed: {}", error.what());
				} catch (...) {
					logger::error("Bridge initialization failed");
				}
			}
		}

		bool Load(const SKSE::LoadInterface* skse, const Adapter& adapter) noexcept
		{
			try {
				SKSE::Init(skse, { .logName = adapter.name });
				std::lock_guard lock(settingsMutex);
				bridge = &adapter;
				try {
					LoadSettings();
				} catch (const std::exception& error) {
					logger::error("{} settings were not loaded: {}", bridge->name, error.what());
				}
				const auto* messaging = SKSE::GetMessagingInterface();
				if (!messaging || !messaging->RegisterListener("SKSE", OnMessage)) {
					logger::error("{} could not register its SKSE lifecycle listener", bridge->name);
					return false;
				}
				logger::info("{} {} loaded", bridge->name, SKSE::GetPluginVersion().string());
				return true;
			} catch (const std::exception& error) {
				logger::error("{} failed to load: {}", adapter.name, error.what());
			} catch (...) {
				logger::error("{} failed to load", adapter.name);
			}
			return false;
		}

		const BridgeAPI* GetAPI(uint32_t) noexcept
		{
			static const BridgeAPI api{ sizeof(BridgeAPI), kBridgeAPIRevision, Command };
			return &api;
		}
	}
}
