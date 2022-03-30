#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "fonts.hpp"
#include "game_console.hpp"
#include "filesystem.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>
#include <utils/memory.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>
#include <utils/image.hpp>
#include <utils/concurrency.hpp>

namespace fonts
{
	namespace
	{
		struct font_data_t
		{
			std::unordered_map<std::string, game::TTF*> fonts;
			std::unordered_map<std::string, std::string> raw_fonts;
		};

		utils::concurrency::container<font_data_t> font_data;

		game::TTF* create_font(const std::string& name, const std::string& data)
		{
			const auto font = utils::memory::get_allocator()->allocate<game::TTF>();
			font->name = utils::memory::get_allocator()->duplicate_string(name);
			font->buffer = utils::memory::get_allocator()->duplicate_string(data);
			font->len = static_cast<int>(data.size());
			font->fontFace = 0;
			return font;
		}

		void free_font(game::TTF* font)
		{
			utils::memory::get_allocator()->free(font->buffer);
			utils::memory::get_allocator()->free(font->name);
			utils::memory::get_allocator()->free(font);
		}

		game::TTF* load_font(const std::string& name)
		{
			return font_data.access<game::TTF*>([&](font_data_t& data_) -> game::TTF*
			{
				if (const auto i = data_.fonts.find(name); i != data_.fonts.end())
				{
					return i->second;
				}

				std::string data{};
				if (const auto i = data_.raw_fonts.find(name); i != data_.raw_fonts.end())
				{
					data = i->second;
				}

				if (data.empty() && !filesystem::read_file(name, &data))
				{
					return nullptr;
				}

				const auto material = create_font(name, data);
				data_.fonts[name] = material;

				return material;
			});
		}

		game::TTF* try_load_font(const std::string& name)
		{
			try
			{
				return load_font(name);
			}
			catch (const std::exception& e)
			{
				game_console::print(game_console::con_type_error, "Failed to load font %s: %s\n", name.data(), e.what());
			}

			return nullptr;
		}

		game::TTF* db_find_xasset_header_stub(game::XAssetType type, const char* name, int create_default)
		{
			auto result = try_load_font(name);
			if (result == nullptr)
			{
				result = game::DB_FindXAssetHeader(type, name, create_default).ttf;
			}
			return result;
		}
	}

	void add(const std::string& name, const std::string& data)
	{
		font_data.access([&](font_data_t& data_)
		{
			data_.raw_fonts[name] = data;
		});
	}

	void clear()
	{
		font_data.access([&](font_data_t& data_)
		{
			for (auto& font : data_.fonts)
			{
				free_font(font.second);
			}

			data_.fonts.clear();
			utils::hook::set<int>(0x14EE3ACB8, 0); // reset registered font count
		});
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			utils::hook::call(0x140747096, db_find_xasset_header_stub);
		}
	};
}

REGISTER_COMPONENT(fonts::component)
