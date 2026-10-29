#include "Runtime/Runtime.h"
#include "Runtime/WindEffects/DragonWind.h"
#include "Integrations/Integrations.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace
{
	constexpr std::size_t kDefaultRulePageSize = 100;
	constexpr std::size_t kMaximumRulePageSize = 500;

	json UnsupportedAuthoring()
	{
		return { { "success", false }, { "error", "The renderer does not support this mesh-authoring operation." } };
	}

	std::string FixFilePath(std::string a_path)
	{
		std::replace(a_path.begin(), a_path.end(), '\\', '/');
		a_path.erase(std::unique(a_path.begin(), a_path.end(), [](char a, char b) { return a == '/' && b == '/'; }), a_path.end());
		std::transform(a_path.begin(), a_path.end(), a_path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_path;
	}

	template <class T>
	json TreeValues(const T& a_rule)
	{
		return { { "bendSensitivity", a_rule.bend }, { "leafAmbientSensitivity", a_rule.leafAmbient },
			{ "upperBendRange", a_rule.upperBendRange }, { "maximumDisplacementPercent", a_rule.maximumDisplacementPercent },
			{ "trunkGustInfluence", a_rule.trunkGustInfluence }, { "leafGustInfluence", a_rule.leafGustInfluence },
			{ "transientWindInfluence", a_rule.transientWindInfluence },
			{ "leafTransientWindInfluence", a_rule.leafTransientWindInfluence },
			{ "leafTransientFlutterMaximum", a_rule.leafTransientFlutterMaximum },
			{ "transientMaximumBendMultiplier", a_rule.transientMaximumBendMultiplier } };
	}

	OpenWind::TreeAuthoringValues ReadTreeValues(const json& a_request)
	{
		OpenWind::TreeAuthoringValues values;
		values.bend = a_request.at("bendSensitivity").get<float>();
		values.leafAmbient = a_request.at("leafAmbientSensitivity").get<float>();
		values.upperBendRange = a_request.at("upperBendRange").get<float>();
		values.maximumDisplacementPercent = a_request.at("maximumDisplacementPercent").get<float>();
		values.trunkGustInfluence = a_request.at("trunkGustInfluence").get<float>();
		values.leafGustInfluence = a_request.at("leafGustInfluence").get<float>();
		values.transientWindInfluence = a_request.at("transientWindInfluence").get<float>();
		values.leafTransientWindInfluence = a_request.at("leafTransientWindInfluence").get<float>();
		values.leafTransientFlutterMaximum = a_request.at("leafTransientFlutterMaximum").get<float>();
		values.transientMaximumBendMultiplier = a_request.at("transientMaximumBendMultiplier").get<float>();
		return values;
	}

	struct RuleQuery
	{
		bool grass;
		std::string search;
		std::size_t offset{};
		std::size_t limit = kDefaultRulePageSize;
		std::size_t totalRules{};
		std::size_t totalMatches{};
		std::size_t unsavedRules{};
		json rules = json::array();
	};

	bool CollectRule(void* a_context, const OpenWind::AuthoringRule* a_rule) noexcept
	{
		try {
			auto& query = *static_cast<RuleQuery*>(a_context);
			++query.totalRules;
			query.unsavedRules += a_rule->unsaved ? 1 : 0;
			const std::string_view mesh(a_rule->mesh);
			if (!query.search.empty() && mesh.find(query.search) == std::string_view::npos)
				return true;
			if (query.totalMatches++ < query.offset || query.rules.size() >= query.limit)
				return true;
			json entry = query.grass ? json{ { "bendSensitivity", a_rule->grassBend },
				{ "flutterSensitivity", a_rule->grassFlutter } } :
			                           TreeValues(a_rule->tree);
			entry["mesh"] = mesh;
			entry["unsaved"] = a_rule->unsaved;
			if (!query.grass)
				entry["bendEnabled"] = a_rule->bendEnabled;
			query.rules.push_back(std::move(entry));
			return true;
		} catch (...) {
			return false;
		}
	}

	bool CollectPath(void* a_context, const char* a_path) noexcept
	{
		try {
			static_cast<json*>(a_context)->push_back(a_path);
			return true;
		} catch (...) {
			return false;
		}
	}

	void RequireAuthoring(bool a_success)
	{
		if (!a_success)
			throw std::runtime_error("Renderer authoring operation failed.");
	}

	json QueryRules(const OpenWind::RendererHost& a_host, bool a_grass, const json& a_request)
	{
		if (!a_host.authoring)
			return UnsupportedAuthoring();
		const auto& authoring = *a_host.authoring;
		if (!authoring.VisitRules || !authoring.VisitPaths || (!a_grass && !authoring.GetUniversal))
			return UnsupportedAuthoring();
		RuleQuery query{ a_grass, FixFilePath(a_request.value("search", std::string{})),
			static_cast<std::size_t>(std::max(a_request.value("offset", 0), 0)),
			static_cast<std::size_t>(std::clamp(a_request.value("limit", static_cast<int>(kDefaultRulePageSize)),
				1, static_cast<int>(kMaximumRulePageSize))) };
		RequireAuthoring(authoring.VisitRules(a_host.context, a_grass, &query, CollectRule));
		json result{ { "success", true }, { "rules", std::move(query.rules) }, { "conflicts", json::array() },
			{ "totalRules", query.totalRules }, { "totalMatches", query.totalMatches }, { "unsavedRules", query.unsavedRules } };
		RequireAuthoring(authoring.VisitPaths(a_host.context, a_grass, false, &result["conflicts"], CollectPath));
		if (a_grass) {
			result["unconfigured"] = json::array();
			RequireAuthoring(authoring.VisitPaths(a_host.context, true, true, &result["unconfigured"], CollectPath));
		} else {
			bool enabled = false;
			OpenWind::TreeAuthoringValues values;
			RequireAuthoring(authoring.GetUniversal(a_host.context, &enabled, &values));
			result["universal"] = TreeValues(values);
			result["universal"]["enabled"] = enabled;
		}
		return result;
	}

	bool SavedRules(void* a_context, const OpenWind::AuthoringSaveResult* a_result) noexcept
	{
		try {
			*static_cast<json*>(a_context) = { { "success", a_result->success }, { "error", a_result->error },
				{ "path", a_result->path }, { "savedRuleCount", a_result->savedRuleCount } };
			return true;
		} catch (...) {
			return false;
		}
	}

	struct RuleSearch
	{
		std::string_view mesh;
		std::optional<uint64_t> index;
	};

	bool FindGrassRule(void* a_context, const OpenWind::AuthoringRule* a_rule) noexcept
	{
		auto& search = *static_cast<RuleSearch*>(a_context);
		if (!search.index && search.mesh == a_rule->mesh)
			search.index = a_rule->index;
		return true;
	}

	json EditRules(const OpenWind::RendererHost& a_host, const std::string& a_action, const json& a_request)
	{
		if (!a_host.authoring)
			return UnsupportedAuthoring();
		const auto& authoring = *a_host.authoring;
		if (a_action == "saveGrassRules" || a_action == "saveTreeRules") {
			if (!authoring.SaveRules)
				return UnsupportedAuthoring();
			json result;
			RequireAuthoring(authoring.SaveRules(a_host.context, a_action == "saveGrassRules", &result, SavedRules));
			return result;
		}
		if (a_action == "revertGrassRules" || a_action == "revertTreeRules") {
			if (!authoring.RevertRules)
				return UnsupportedAuthoring();
			return { { "success", authoring.RevertRules(a_host.context, a_action == "revertGrassRules") } };
		}
		if (a_action == "setUniversalTreeResponse") {
			if (!authoring.GetUniversal || !authoring.SetUniversal)
				return UnsupportedAuthoring();
			bool enabled = false;
			OpenWind::TreeAuthoringValues current;
			RequireAuthoring(authoring.GetUniversal(a_host.context, &enabled, &current));
			auto values = TreeValues(current);
			for (auto& [key, value] : values.items()) {
				if (a_request.contains(key) && a_request.at(key).is_number())
					value = a_request.at(key);
			}
			if (a_request.contains("enabled") && a_request.at("enabled").is_boolean())
				enabled = a_request.at("enabled").get<bool>();
			current = ReadTreeValues(values);
			return { { "success", authoring.SetUniversal(a_host.context, enabled, &current) } };
		}
		if (a_action == "resetUniversalTreeResponse") {
			if (!authoring.SetUniversal)
				return UnsupportedAuthoring();
			return { { "success", authoring.SetUniversal(a_host.context, false, nullptr) } };
		}
		if (a_action != "addGrassRule" && a_action != "setGrassRule" && a_action != "setTreeRule" && a_action != "setTreeBendEnabled")
			return { { "success", false }, { "error", "Unknown action or mesh." } };
		const auto mesh = FixFilePath(a_request.at("mesh").get<std::string>());
		if (a_action == "addGrassRule") {
			if (!authoring.AddGrassRule)
				return UnsupportedAuthoring();
			return { { "success", authoring.AddGrassRule(a_host.context, mesh.c_str()) } };
		}
		if (a_action == "setTreeBendEnabled") {
			if (!authoring.SetTreeBendEnabled)
				return UnsupportedAuthoring();
			return { { "success", authoring.SetTreeBendEnabled(a_host.context, mesh.c_str(), a_request.at("enabled").get<bool>()) } };
		}
		if (a_action == "setGrassRule") {
			if (!authoring.VisitRules || !authoring.SetGrassRule)
				return UnsupportedAuthoring();
			const auto bend = a_request.at("bendSensitivity").get<float>();
			const auto flutter = a_request.at("flutterSensitivity").get<float>();
			RuleSearch search{ mesh, {} };
			RequireAuthoring(authoring.VisitRules(a_host.context, true, &search, FindGrassRule));
			if (search.index)
				return { { "success", authoring.SetGrassRule(a_host.context, *search.index, bend, flutter) } };
		}
		if (a_action == "setTreeRule") {
			if (!authoring.SetTreeRule)
				return UnsupportedAuthoring();
			const auto values = ReadTreeValues(a_request);
			return { { "success", authoring.SetTreeRule(a_host.context, mesh.c_str(), &values) } };
		}
		return { { "success", false }, { "error", "Unknown action or mesh." } };
	}
}

bool Wind::RendererCommand(const char* a_request, void* a_context, OpenWind::AuthoringResponse a_response) noexcept
{
	if (!a_request || !a_response)
		return false;
	try {
		OpenWind::RendererHost host;
		{
			std::lock_guard lock(stateMutex);
			if (!loaded)
				return false;
			host = rendererHost;
		}
		const auto request = json::parse(a_request);
		const auto action = request.at("action").get<std::string>();
		const bool integration = action == "integrations" || action == "setIntegrations" || action == "saveIntegrations" ||
		                         action == "reloadIntegrations" || action == "resetIntegrations" || action == "retryIntegrations";
		const bool query = action == "grassRules" || action == "treeRules";
		const auto result = action == "dragonWakes" ? json{ { "success", true }, { "dragonWakes", DragonWind::GetWakeDiagnostics(request) } } :
		                    integration            ? OpenWind::Integrations::Command(request) :
		                    query                  ? QueryRules(host, action == "grassRules", request) : EditRules(host, action, request);
		const auto text = result.dump();
		a_response(a_context, text.c_str());
		return true;
	} catch (...) {
		return false;
	}
}
