#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "filesystem.hpp"
#include "console.hpp"

#include "game/game.hpp"

#include <utils/io.hpp>
#include <utils/hook.hpp>

namespace filesystem
{
	namespace
	{
		bool initialized = false;

		std::vector<std::filesystem::path>& get_search_paths_internal()
		{
			static std::vector<std::filesystem::path> search_paths{};
			return search_paths;
		}

		bool is_polrus_lang()
		{
			static auto* loc_language = game::Dvar_FindVar("loc_language");
			const auto id = loc_language->current.integer;
			return id == 5 || id == 6 || id == 17;
		}

		void fs_startup_stub(const char* name)
		{
			console::info("[FS] Startup\n");

			initialized = true;

			filesystem::register_path(L".");
			filesystem::register_path(L"h2-mod");
			filesystem::register_path(L"data");

			utils::hook::invoke<void>(0x14060BF50, name);
		}

		std::vector<std::filesystem::path> get_paths(const std::filesystem::path& path)
		{
			std::vector<std::filesystem::path> paths{};

			const auto code = game::SEH_GetCurrentLanguageCode();

			paths.push_back(path);
			paths.push_back(path / code);

			if (is_polrus_lang())
			{
				paths.push_back(path / "polrus");
			}

			return paths;
		}

		bool can_insert_path(const std::filesystem::path& path)
		{
			for (const auto& path_ : get_search_paths_internal())
			{
				if (path_ == path)
				{
					return false;
				}
			}

			return true;
		}
	}

	std::string read_file(const std::string& path)
	{
		for (const auto& search_path : get_search_paths_internal())
		{
			const auto path_ = search_path / path;
			if (utils::io::file_exists(path_.generic_string()))
			{
				return utils::io::read_file(path_.generic_string());
			}
		}

		return {};
	}

	bool read_file(const std::string& path, std::string* data, std::string* real_path)
	{
		for (const auto& search_path : get_search_paths_internal())
		{
			const auto path_ = search_path / path;
			if (utils::io::read_file(path_.generic_string(), data))
			{
				if (real_path != nullptr)
				{
					*real_path = path_.generic_string();
				}

				return true;
			}
		}

		return false;
	}

	void register_path(const std::filesystem::path& path)
	{
		if (!initialized)
		{
			return;
		}

		const auto paths = get_paths(path);
		for (const auto& path_ : paths)
		{
			if (can_insert_path(path_))
			{
				console::info("[FS] Registering path '%s'\n", path_.generic_string().data());
				get_search_paths_internal().push_back(path_);
			}
		}
	}

	void unregister_path(const std::filesystem::path& path)
	{
		if (!initialized)
		{
			return;
		}

		const auto paths = get_paths(path);
		for (const auto& path_ : paths)
		{
			auto& search_paths = get_search_paths_internal();
			for (auto i = search_paths.begin(); i != search_paths.end();)
			{
				if (*i == path_)
				{
					console::info("[FS] Unregistering path '%s'\n", path_.generic_string().data());
					i = search_paths.erase(i);
				}
				else
				{
					++i;
				}
			}
		}
	}

	std::vector<std::string> get_search_paths()
	{
		std::vector<std::string> paths{};

		for (const auto& path : get_search_paths_internal())
		{
			paths.push_back(path.generic_string());
		}

		return paths;
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			utils::hook::call(0x14060B052, fs_startup_stub);
		}
	};
}

REGISTER_COMPONENT(filesystem::component)
