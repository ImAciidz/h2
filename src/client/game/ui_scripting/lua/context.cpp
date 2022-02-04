#include <std_include.hpp>
#include "context.hpp"
#include "error.hpp"
#include "value_conversion.hpp"
#include "../../scripting/execution.hpp"
#include "../script_value.hpp"
#include "../execution.hpp"

#include "../../../component/ui_scripting.hpp"
#include "../../../component/scripting.hpp"
#include "../../../component/command.hpp"
#include "../../../component/fastfiles.hpp"

#include "component/game_console.hpp"
#include "component/scheduler.hpp"

#include <utils/string.hpp>
#include <utils/nt.hpp>
#include <utils/io.hpp>
#include <utils/http.hpp>
#include <utils/cryptography.hpp>
#include <version.h>

namespace ui_scripting::lua
{
	std::unordered_map<std::string, menu> menus;
	std::vector<element*> elements;
	element ui_element;
	int mouse[2];

	namespace
	{
		const auto animation_script = utils::nt::load_resource(LUA_ANIMATION_SCRIPT);
		const auto json_script = utils::nt::load_resource(LUA_JSON_SCRIPT);

		scripting::script_value script_convert(const sol::lua_value& value)
		{
			if (value.is<int>())
			{
				return {value.as<int>()};
			}

			if (value.is<unsigned int>())
			{
				return {value.as<unsigned int>()};
			}

			if (value.is<bool>())
			{
				return {value.as<bool>()};
			}

			if (value.is<double>())
			{
				return {value.as<double>()};
			}

			if (value.is<float>())
			{
				return {value.as<float>()};
			}
			if (value.is<std::string>())
			{
				return {value.as<std::string>()};
			}

			if (value.is<scripting::vector>())
			{
				return {value.as<scripting::vector>()};
			}

			return {};
		}

		bool valid_dvar_name(const std::string& name)
		{
			for (const auto c : name)
			{
				if (!isalnum(c) && c != '_')
				{
					return false;
				}
			}
			
			return true;
		}

		void setup_io(sol::state& state)
		{
			state["io"]["fileexists"] = utils::io::file_exists;
			state["io"]["writefile"] = utils::io::write_file;
			state["io"]["movefile"] = utils::io::move_file;
			state["io"]["filesize"] = utils::io::file_size;
			state["io"]["createdirectory"] = utils::io::create_directory;
			state["io"]["directoryexists"] = utils::io::directory_exists;
			state["io"]["directoryisempty"] = utils::io::directory_is_empty;
			state["io"]["listfiles"] = utils::io::list_files;
			state["io"]["copyfolder"] = utils::io::copy_folder;
			state["io"]["removefile"] = utils::io::remove_file;
			state["io"]["removedirectory"] = utils::io::remove_directory;
			state["io"]["readfile"] = static_cast<std::string(*)(const std::string&)>(utils::io::read_file);
		}

		void setup_json(sol::state& state)
		{
			const auto json = state.safe_script(json_script, &sol::script_pass_on_error);
			handle_error(json);
			state["json"] = json;
		}

		void setup_vector_type(sol::state& state)
		{
			auto vector_type = state.new_usertype<scripting::vector>("vector", sol::constructors<scripting::vector(float, float, float)>());
			vector_type["x"] = sol::property(&scripting::vector::get_x, &scripting::vector::set_x);
			vector_type["y"] = sol::property(&scripting::vector::get_y, &scripting::vector::set_y);
			vector_type["z"] = sol::property(&scripting::vector::get_z, &scripting::vector::set_z);

			vector_type["r"] = sol::property(&scripting::vector::get_x, &scripting::vector::set_x);
			vector_type["g"] = sol::property(&scripting::vector::get_y, &scripting::vector::set_y);
			vector_type["b"] = sol::property(&scripting::vector::get_z, &scripting::vector::set_z);

			vector_type[sol::meta_function::addition] = sol::overload(
				[](const scripting::vector& a, const scripting::vector& b)
				{
					return scripting::vector(
						a.get_x() + b.get_x(),
						a.get_y() + b.get_y(),
						a.get_z() + b.get_z()
					);
				},
				[](const scripting::vector& a, const int value)
				{
					return scripting::vector(
						a.get_x() + value,
						a.get_y() + value,
						a.get_z() + value
					);
				}
			);

			vector_type[sol::meta_function::subtraction] = sol::overload(
				[](const scripting::vector& a, const scripting::vector& b)
				{
					return scripting::vector(
						a.get_x() - b.get_x(),
						a.get_y() - b.get_y(),
						a.get_z() - b.get_z()
					);
				},
				[](const scripting::vector& a, const int value)
				{
					return scripting::vector(
						a.get_x() - value,
						a.get_y() - value,
						a.get_z() - value
					);
				}
			);

			vector_type[sol::meta_function::multiplication] = sol::overload(
				[](const scripting::vector& a, const scripting::vector& b)
				{
					return scripting::vector(
						a.get_x() * b.get_x(),
						a.get_y() * b.get_y(),
						a.get_z() * b.get_z()
					);
				},
				[](const scripting::vector& a, const int value)
				{
					return scripting::vector(
						a.get_x() * value,
						a.get_y() * value,
						a.get_z() * value
					);
				}
			);

			vector_type[sol::meta_function::division] = sol::overload(
				[](const scripting::vector& a, const scripting::vector& b)
				{
					return scripting::vector(
						a.get_x() / b.get_x(),
						a.get_y() / b.get_y(),
						a.get_z() / b.get_z()
					);
				},
				[](const scripting::vector& a, const int value)
				{
					return scripting::vector(
						a.get_x() / value,
						a.get_y() / value,
						a.get_z() / value
					);
				}
			);

			vector_type[sol::meta_function::equal_to] = [](const scripting::vector& a, const scripting::vector& b)
			{
				return a.get_x() == b.get_x() &&
					   a.get_y() == b.get_y() &&
					   a.get_z() == b.get_z();
			};

			vector_type[sol::meta_function::length] = [](const scripting::vector& a)
			{
				return sqrt((a.get_x() * a.get_x()) + (a.get_y() * a.get_y()) + (a.get_z() * a.get_z()));
			};

			vector_type[sol::meta_function::to_string] = [](const scripting::vector& a)
			{
				return utils::string::va("{x: %f, y: %f, z: %f}", a.get_x(), a.get_y(), a.get_z());
			};
		}

		void setup_element_type(sol::state& state, event_handler& handler, scheduler& scheduler)
		{
			auto element_type = state.new_usertype<element>("element", "new", []()
			{
				const auto el = new element();
				elements.push_back(el);
				return el;
			});

			element_type["setvertalign"] = &element::set_vertalign;
			element_type["sethorzalign"] = &element::set_horzalign;
			element_type["setrect"] = &element::set_rect;
			element_type["setfont"] = sol::overload(
				static_cast<void(element::*)(const std::string&)>(&element::set_font),
				static_cast<void(element::*)(const std::string&, int)>(&element::set_font)
			);
			element_type["settext"] = &element::set_text;
			element_type["setmaterial"] = &element::set_material;
			element_type["setcolor"] = &element::set_color;
			element_type["setsecondcolor"] = &element::set_second_color;
			element_type["setglowcolor"] = &element::set_glow_color;
			element_type["setbackcolor"] = &element::set_background_color;
			element_type["setbordercolor"] = &element::set_border_color;
			element_type["setborderwidth"] = sol::overload(
				static_cast<void(element::*)(float)>(&element::set_border_width),
				static_cast<void(element::*)(float, float)>(&element::set_border_width),
				static_cast<void(element::*)(float, float, float)>(&element::set_border_width),
				static_cast<void(element::*)(float, float, float, float)>(&element::set_border_width)
			);
			element_type["settextoffset"] = &element::set_text_offset;
			element_type["setscale"] = &element::set_scale;
			element_type["setrotation"] = &element::set_rotation;
			element_type["setyle"] = &element::set_style;
			element_type["setslice"] = &element::set_slice;

			element_type["getrect"] = [](const sol::this_state s, element& element)
			{
				auto rect = sol::table::create(s.lua_state());
				rect["x"] = element.x;
				rect["y"] = element.y;
				rect["w"] = element.w + element.border_width[1] + element.border_width[3];
				rect["h"] = element.h + element.border_width[0] + element.border_width[2];

				return rect;
			};

			element_type["x"] = sol::property(
				[](element& element)
				{
					return element.x;
				},
				[](element& element, float x)
				{
					element.x = x;
				}
			);

			element_type["y"] = sol::property(
				[](element& element)
				{
					return element.y;
				},
				[](element& element, float y)
				{
					element.y = y;
				}
			);

			element_type["w"] = sol::property(
				[](element& element)
				{
					return element.w;
				},
				[](element& element, float w)
				{
					element.w = w;
				}
			);

			element_type["h"] = sol::property(
				[](element& element)
				{
					return element.h;
				},
				[](element& element, float h)
				{
					element.h = h;
				}
			);

			element_type["scalex"] = sol::property(
				[](element& element)
				{
					return element.x_scale;
				},
				[](element& element, float x_scale)
				{
					element.x_scale = x_scale;
				}
			);

			element_type["scaley"] = sol::property(
				[](element& element)
				{
					return element.y_scale;
				},
				[](element& element, float y_scale)
				{
					element.y_scale = y_scale;
				}
			);

			element_type["rotation"] = sol::property(
				[](element& element)
				{
					return element.rotation;
				},
				[](element& element, float rotation)
				{
					element.rotation = rotation;
				}
			);

			element_type["style"] = sol::property(
				[](element& element)
				{
					return element.style;
				},
				[](element& element, int style)
				{
					element.style = style;
				}
			);

			element_type["color"] = sol::property(
				[](element& element, const sol::this_state s)
				{
					auto color = sol::table::create(s.lua_state());
					color["r"] = element.color[0];
					color["g"] = element.color[1];
					color["b"] = element.color[2];
					color["a"] = element.color[3];
					return color;
				},
				[](element& element, const sol::lua_table color)
				{
					element.color[0] = color["r"].get_type() == sol::type::number ? color["r"].get<float>() : 0.f;
					element.color[1] = color["g"].get_type() == sol::type::number ? color["g"].get<float>() : 0.f;
					element.color[2] = color["b"].get_type() == sol::type::number ? color["b"].get<float>() : 0.f;
					element.color[3] = color["a"].get_type() == sol::type::number ? color["a"].get<float>() : 0.f;
				}
			);

			element_type["secondcolor"] = sol::property(
				[](element& element, const sol::this_state s)
				{
					auto color = sol::table::create(s.lua_state());
					color["r"] = element.second_color[0];
					color["g"] = element.second_color[1];
					color["b"] = element.second_color[2];
					color["a"] = element.second_color[3];
					return color;
				},
				[](element& element, const sol::lua_table color)
				{
					element.use_gradient = true;
					element.second_color[0] = color["r"].get_type() == sol::type::number ? color["r"].get<float>() : 0.f;
					element.second_color[1] = color["g"].get_type() == sol::type::number ? color["g"].get<float>() : 0.f;
					element.second_color[2] = color["b"].get_type() == sol::type::number ? color["b"].get<float>() : 0.f;
					element.second_color[3] = color["a"].get_type() == sol::type::number ? color["a"].get<float>() : 0.f;
				}
			);

			element_type["usegradient"] = sol::property(
				[](element& element, const sol::this_state s)
				{
					return element.use_gradient;
				},
				[](element& element, bool use_gradient)
				{
					element.use_gradient = use_gradient;
				}
			);

			element_type["glowcolor"] = sol::property(
				[](element& element, const sol::this_state s)
				{
					auto color = sol::table::create(s.lua_state());
					color["r"] = element.glow_color[0];
					color["g"] = element.glow_color[1];
					color["b"] = element.glow_color[2];
					color["a"] = element.glow_color[3];
					return color;
				},
				[](element& element, const sol::lua_table color)
				{
					element.glow_color[0] = color["r"].get_type() == sol::type::number ? color["r"].get<float>() : 0.f;
					element.glow_color[1] = color["g"].get_type() == sol::type::number ? color["g"].get<float>() : 0.f;
					element.glow_color[2] = color["b"].get_type() == sol::type::number ? color["b"].get<float>() : 0.f;
					element.glow_color[3] = color["a"].get_type() == sol::type::number ? color["a"].get<float>() : 0.f;
				}
			);

			element_type["backcolor"] = sol::property(
				[](element& element, const sol::this_state s)
				{
					auto color = sol::table::create(s.lua_state());
					color["r"] = element.background_color[0];
					color["g"] = element.background_color[1];
					color["b"] = element.background_color[2];
					color["a"] = element.background_color[3];
					return color;
				},
				[](element& element, const sol::lua_table color)
				{
					element.background_color[0] = color["r"].get_type() == sol::type::number ? color["r"].get<float>() : 0.f;
					element.background_color[1] = color["g"].get_type() == sol::type::number ? color["g"].get<float>() : 0.f;
					element.background_color[2] = color["b"].get_type() == sol::type::number ? color["b"].get<float>() : 0.f;
					element.background_color[3] = color["a"].get_type() == sol::type::number ? color["a"].get<float>() : 0.f;
				}
			);

			element_type["bordercolor"] = sol::property(
				[](element& element, const sol::this_state s)
				{
					auto color = sol::table::create(s.lua_state());
					color["r"] = element.border_color[0];
					color["g"] = element.border_color[1];
					color["b"] = element.border_color[2];
					color["a"] = element.border_color[3];
					return color;
				},
				[](element& element, const sol::lua_table color)
				{
					element.border_color[0] = color["r"].get_type() == sol::type::number ? color["r"].get<float>() : 0.f;
					element.border_color[1] = color["g"].get_type() == sol::type::number ? color["g"].get<float>() : 0.f;
					element.border_color[2] = color["b"].get_type() == sol::type::number ? color["b"].get<float>() : 0.f;
					element.border_color[3] = color["a"].get_type() == sol::type::number ? color["a"].get<float>() : 0.f;
				}
			);

			element_type["borderwidth"] = sol::property(
				[](element& element, const sol::this_state s)
				{
					auto color = sol::table::create(s.lua_state());
					color["top"] = element.border_width[0];
					color["right"] = element.border_width[1];
					color["bottom"] = element.border_width[2];
					color["left"] = element.border_width[3];
					return color;
				},
				[](element& element, const sol::lua_table color)
				{
					element.border_width[0] = color["top"].get_type() == sol::type::number ? color["top"].get<float>() : 0.f;
					element.border_width[1] = color["right"].get_type() == sol::type::number ? color["right"].get<float>() : element.border_width[1];
					element.border_width[2] = color["bottom"].get_type() == sol::type::number ? color["bottom"].get<float>() : element.border_width[2];
					element.border_width[3] = color["left"].get_type() == sol::type::number ? color["left"].get<float>() : element.border_width[3];
				}
			);

			element_type["font"] = sol::property(
				[](element& element)
				{
					return element.font;
				},
				[](element& element, const std::string& font)
				{
					element.set_font(font);
				}
			);

			element_type["fontsize"] = sol::property(
				[](element& element)
				{
					return element.fontsize;
				},
				[](element& element, float fontsize)
				{
					element.fontsize = (int)fontsize;
				}
			);

			element_type["onnotify"] = [&handler](element& element, const std::string& event,
												  const event_callback& callback)
			{
				event_listener listener{};
				listener.callback = callback;
				listener.element = &element;
				listener.event = event;
				listener.is_volatile = false;

				return handler.add_event_listener(std::move(listener));
			};

			element_type["onnotifyonce"] = [&handler](element& element, const std::string& event,
													  const event_callback& callback)
			{
				event_listener listener{};
				listener.callback = callback;
				listener.element = &element;
				listener.event = event;
				listener.is_volatile = true;

				return handler.add_event_listener(std::move(listener));
			};

			element_type["notify"] = [&handler](element& element, const sol::this_state s, const std::string& _event,
									            sol::variadic_args va)
			{
				event event;
				event.element = &element;
				event.name = _event;

				for (auto arg : va)
				{
					event.arguments.push_back(convert({s, arg}));
				}

				notify(event);
			};

			element_type["hidden"] = sol::property(
				[](element& element)
				{
					return element.hidden;
				},
				[](element& element, bool hidden)
				{
					element.hidden = hidden;
				}
			);

			element_type[sol::meta_function::new_index] = [](element& element, const sol::this_state s, const std::string& attribute, const sol::lua_value& value)
			{
				element.attributes[attribute] = convert({s, value});
			};

			element_type[sol::meta_function::index] = [](element& element, const sol::this_state s, const std::string& attribute)
			{
				if (element.attributes.find(attribute) == element.attributes.end())
				{
					return sol::lua_value{s, sol::lua_nil};
				}

				return sol::lua_value{s, convert(s, element.attributes[attribute])};
			};
		}

		void setup_menu_type(sol::state& state, event_handler& handler, scheduler& scheduler)
		{
			auto menu_type = state.new_usertype<menu>("menu");

			menu_type["onnotify"] = [&handler](menu& menu, const std::string& event,
											   const event_callback& callback)
			{
				event_listener listener{};
				listener.callback = callback;
				listener.element = &menu;
				listener.event = event;
				listener.is_volatile = false;

				return handler.add_event_listener(std::move(listener));
			};

			menu_type["onnotifyonce"] = [&handler](menu& menu, const std::string& event,
												   const event_callback& callback)
			{
				event_listener listener{};
				listener.callback = callback;
				listener.element = &menu;
				listener.event = event;
				listener.is_volatile = true;

				return handler.add_event_listener(std::move(listener));
			};

			menu_type["notify"] = [&handler](menu& element, const sol::this_state s, const std::string& _event,
									         sol::variadic_args va)
			{
				event event;
				event.element = &element;
				event.name = _event;

				for (auto arg : va)
				{
					event.arguments.push_back(convert({s, arg}));
				}

				notify(event);
			};

			menu_type["addchild"] = [](const sol::this_state s, menu& menu, element& element)
			{
				menu.add_child(&element);
			};

			menu_type["cursor"] = sol::property(
				[](menu& menu)
				{
					return menu.cursor;
				},
				[](menu& menu, bool cursor)
				{
					menu.cursor = cursor;
				}
			);

			menu_type["hidden"] = sol::property(
				[](menu& menu)
				{
					return menu.hidden;
				},
				[](menu& menu, bool hidden)
				{
					menu.hidden = hidden;
				}
			);

			menu_type["ignoreevents"] = sol::property(
				[](menu& menu)
				{
					return menu.ignoreevents;
				},
				[](menu& menu, bool ignoreevents)
				{
					menu.ignoreevents = ignoreevents;
				}
			);

			menu_type["isopen"] = [](menu& menu)
			{
				return menu.visible || (menu.type == menu_type::overlay && game::Menu_IsMenuOpenAndVisible(0, menu.overlay_menu.data()));
			};

			menu_type["open"] = [&handler](menu& menu)
			{
				event event;
				event.element = &menu;
				event.name = "close";
				notify(event);

				menu.open();
			};

			menu_type["close"] = [&handler](menu& menu)
			{
				event event;
				event.element = &menu;
				event.name = "close";
				notify(event);

				menu.close();
			};

			menu_type["getelement"] = [](menu& menu, const sol::this_state s, const sol::lua_value& value, const std::string& attribute)
			{
				const auto value_ = convert({s, value});

				for (const auto& element : menu.children)
				{
					if (element->attributes.find(attribute) != element->attributes.end() && element->attributes[attribute] == value_)
					{
						return sol::lua_value{s, element};
					}
				}

				return sol::lua_value{s, sol::lua_nil};
			};

			menu_type["getelements"] = sol::overload
			(
				[](menu& menu, const sol::this_state s, const sol::lua_value& value, const std::string& attribute)
				{
					const auto value_ = convert({s, value});
					auto result = sol::table::create(s.lua_state());

					for (const auto& element : menu.children)
					{
						if (element->attributes.find(attribute) != element->attributes.end() && element->attributes[attribute] == value_)
						{
							result.add(element);
						}
					}

					return result;
				},
				[](menu& menu, const sol::this_state s)
				{
					auto result = sol::table::create(s.lua_state());

					for (const auto& element : menu.children)
					{
						result.add(element);
					}

					return result;
				}
			);
		}

		void setup_game_type(sol::state& state, event_handler& handler, scheduler& scheduler)
		{
			struct game
			{
			};
			auto game_type = state.new_usertype<game>("game_");
			state["game"] = game();

			game_type["getmenu"] = [](const game&, const sol::this_state s, const std::string& name)
			{
				if (menus.find(name) == menus.end())
				{
					return sol::lua_value{s, sol::lua_nil};
				}

				return sol::lua_value{s, &menus[name]};
			};

			game_type["getelement"] = [](const game&, const sol::this_state s, const sol::lua_value& value, const std::string& attribute)
			{
				const auto value_ = convert({s, value});

				for (const auto& element : elements)
				{
					if (element->attributes.find(attribute) != element->attributes.end() && element->attributes[attribute] == value_)
					{
						return sol::lua_value{s, element};
					}
				}

				return sol::lua_value{s, sol::lua_nil};
			};

			game_type["getelements"] = sol::overload
			(
				[](const game&, const sol::this_state s, const sol::lua_value& value, const std::string& attribute)
				{
					const auto value_ = convert({s, value});
					auto result = sol::table::create(s.lua_state());

					for (const auto& element : elements)
					{
						if (element->attributes.find(attribute) != element->attributes.end() && element->attributes[attribute] == value_)
						{
							result.add(element);
						}
					}

					return result;
				},
				[](const game&, const sol::this_state s)
				{
					auto result = sol::table::create(s.lua_state());

					for (const auto& element : elements)
					{
						result.add(element);
					}

					return result;
				}
			);

			game_type["time"] = []()
			{
				const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
				return now.count();
			};

			game_type["newmenu"] = [](const game&, const std::string& name)
			{
				menus[name] = {};
				return &menus[name];
			};

			game_type["executecommand"] = [](const game&, const std::string& command)
			{
				command::execute(command, false);
			};

			game_type["luiopen"] = [](const game&, const std::string& menu)
			{
				::scheduler::once([menu]()
				{
					::game::LUI_OpenMenu(0, menu.data(), 0, 0, 0);
				}, ::scheduler::pipeline::renderer);
			};

			game_type["newmenuoverlay"] = [](const game&, const std::string& name, const std::string& menu_name)
			{
				menus[name] = {};
				menus[name].type = menu_type::overlay;
				menus[name].overlay_menu = menu_name;
				return &menus[name];
			};

			game_type["getmouseposition"] = [](const sol::this_state s, const game&)
			{
				auto pos = sol::table::create(s.lua_state());
				pos["x"] = mouse[0];
				pos["y"] = mouse[1];

				return pos;
			};

			game_type["openmenu"] = [&handler](const game&, const std::string& name)
			{
				if (menus.find(name) == menus.end())
				{
					return;
				}

				const auto menu = &menus[name];

				event event;
				event.element = menu;
				event.name = "open";
				notify(event);

				menu->open();
			};

			game_type["closemenu"] = [&handler](const game&, const std::string& name)
			{
				if (menus.find(name) == menus.end())
				{
					return;
				}

				const auto menu = &menus[name];

				event event;
				event.element = menu;
				event.name = "close";
				notify(event);

				menu->close();
			};

			game_type["onframe"] = [&scheduler](const game&, const sol::protected_function& callback)
			{
				return scheduler.add(callback, 0, false);
			};

			game_type["ontimeout"] = [&scheduler](const game&, const sol::protected_function& callback,
			                                      const long long milliseconds)
			{
				return scheduler.add(callback, milliseconds, true);
			};

			game_type["oninterval"] = [&scheduler](const game&, const sol::protected_function& callback,
			                                       const long long milliseconds)
			{
				return scheduler.add(callback, milliseconds, false);
			};

			game_type["onnotify"] = [&handler](const game&, const std::string& event,
												  const event_callback& callback)
			{
				event_listener listener{};
				listener.callback = callback;
				listener.element = &ui_element;
				listener.event = event;
				listener.is_volatile = false;

				return handler.add_event_listener(std::move(listener));
			};

			game_type["onnotifyonce"] = [&handler](const game&, const std::string& event,
													  const event_callback& callback)
			{
				event_listener listener{};
				listener.callback = callback;
				listener.element = &ui_element;
				listener.event = event;
				listener.is_volatile = true;

				return handler.add_event_listener(std::move(listener));
			};

			game_type["isingame"] = []()
			{
				return ::game::CL_IsCgameInitialized() && ::game::g_entities[0].client;
			};

			game_type["getdvar"] = [](const game&, const sol::this_state s, const std::string& name)
			{
				const auto dvar = ::game::Dvar_FindVar(name.data());
				if (!dvar)
				{
					return sol::lua_value{s, sol::lua_nil};
				}

				const std::string value = ::game::Dvar_ValueToString(dvar, nullptr, &dvar->current);
				return sol::lua_value{s, value};
			};

			game_type["getdvarint"] = [](const game&, const sol::this_state s, const std::string& name)
			{
				const auto dvar = ::game::Dvar_FindVar(name.data());
				if (!dvar)
				{
					return sol::lua_value{s, sol::lua_nil};
				}

				const auto value = atoi(::game::Dvar_ValueToString(dvar, nullptr, &dvar->current));
				return sol::lua_value{s, value};
			};

			game_type["getdvarfloat"] = [](const game&, const sol::this_state s, const std::string& name)
			{
				const auto dvar = ::game::Dvar_FindVar(name.data());
				if (!dvar)
				{
					return sol::lua_value{s, sol::lua_nil};
				}

				const auto value = atof(::game::Dvar_ValueToString(dvar, nullptr, &dvar->current));
				return sol::lua_value{s, value};
			};

			game_type["setdvar"] = [](const game&, const std::string& name, const sol::lua_value& value)
			{
				if (!valid_dvar_name(name))
				{
					throw std::runtime_error("Invalid DVAR name, must be alphanumeric");
				}

				const auto hash = ::game::generateHashValue(name.data());
				std::string string_value;

				if (value.is<bool>())
				{
					string_value = utils::string::va("%i", value.as<bool>());
				} 
				else if (value.is<int>())
				{
					string_value = utils::string::va("%i", value.as<int>());
				} 
				else if (value.is<float>())
				{
					string_value = utils::string::va("%f", value.as<float>());
				}
				else if (value.is<scripting::vector>())
				{
					const auto v = value.as<scripting::vector>();
					string_value = utils::string::va("%f %f %f", 
						v.get_x(),
						v.get_y(),
						v.get_z()
					);
				}

				if (value.is<std::string>())
				{
					string_value = value.as<std::string>();
				}

				::game::Dvar_SetCommand(hash, "", string_value.data());
			};

			game_type["getwindowsize"] = [](const game&, const sol::this_state s)
			{
				const auto size = ::game::ScrPlace_GetViewPlacement()->realViewportSize;

				auto screen = sol::table::create(s.lua_state());
				screen["x"] = size[0];
				screen["y"] = size[1];

				return screen;
			};

			game_type["playmenuvideo"] = [](const game&, const std::string& video)
			{
				reinterpret_cast<void (*)(const char* a1, int a2, int a3)>
					(0x71B970_b)(video.data(), 64, 0);
			};

			game_type[sol::meta_function::index] = [](const game&, const std::string& name)
			{
				return [name](const game&, const sol::this_state s, sol::variadic_args va)
				{
					arguments arguments{};

					for (auto arg : va)
					{
						arguments.push_back(convert({s, arg}));
					}

					const auto values = call(name, arguments);
					std::vector<sol::lua_value> returns;

					for (const auto& value : values)
					{
						returns.push_back(convert(s, value));
					}

					return sol::as_returns(returns);
				};
			};

			game_type["call"] = [](const game&, const sol::this_state s, const std::string& name, sol::variadic_args va)
			{
				arguments arguments{};

				for (auto arg : va)
				{
					arguments.push_back(convert({s, arg}));
				}

				const auto values = call(name, arguments);
				std::vector<sol::lua_value> returns;

				for (const auto& value : values)
				{
					returns.push_back(convert(s, value));
				}

				return sol::as_returns(returns);
			};

			game_type["sharedset"] = [](const game&, const std::string& key, const std::string& value)
			{
				scripting::shared_table.access([key, value](scripting::shared_table_t& table)
				{
					table[key] = value;
				});
			};

			game_type["sharedget"] = [](const game&, const std::string& key)
			{
				std::string result;
				scripting::shared_table.access([key, &result](scripting::shared_table_t& table)
				{
					result = table[key];
				});
				return result;
			};

			game_type["sharedclear"] = [](const game&)
			{
				scripting::shared_table.access([](scripting::shared_table_t& table)
				{
					table.clear();
				});
			};

			game_type["assetlist"] = [](const game&, const sol::this_state s, const std::string& type_string)
			{
				auto table = sol::table::create(s.lua_state());
				auto index = 1;
				auto type_index = -1;

				for (auto i = 0; i < ::game::XAssetType::ASSET_TYPE_COUNT; i++)
				{
					if (type_string == ::game::g_assetNames[i])
					{
						type_index = i;
					}
				}

				if (type_index == -1)
				{
					throw std::runtime_error("Asset type does not exist");
				}

				const auto type = static_cast<::game::XAssetType>(type_index);
				fastfiles::enum_assets(type, [type, &table, &index](const ::game::XAssetHeader header)
				{
					const auto asset = ::game::XAsset{type, header};
					const std::string asset_name = ::game::DB_GetXAssetName(&asset);
					table[index++] = asset_name;
				}, true);

				return table;
			};

			game_type["getweapondisplayname"] = [](const game&, const std::string& name)
			{
				const auto alternate = name.starts_with("alt_");
				const auto weapon = ::game::G_GetWeaponForName(name.data());

				char buffer[0x400] = {0};
				::game::CG_GetWeaponDisplayName(weapon, alternate, buffer, 0x400);

				return std::string(buffer);
			};

			game_type["getloadedmod"] = [](const game&)
			{
				return ::game::mod_folder;
			};

			static int request_id{};
			game_type["httpget"] = [](const game&, const std::string& url)
			{
				const auto id = request_id++;
				::scheduler::once([url, id]()
				{
					const auto result = utils::http::get_data(url);
					::scheduler::once([result, id]
					{
						event event;
						event.element = &ui_element;
						event.name = "http_request_done";

						if (result.has_value())
						{
							event.arguments = {id, true, result.value()};
						}
						else
						{
							event.arguments = {id, false};
						}

						notify(event);
					}, ::scheduler::pipeline::renderer);
				}, ::scheduler::pipeline::async);
				return id;
			};

			game_type["httpgettofile"] = [](const game&, const std::string& url, 
				const std::string& dest)
			{
				const auto id = request_id++;
				::scheduler::once([url, id, dest]()
				{
					const auto result = utils::http::get_data(url);
					::scheduler::once([result, id, dest]
					{
						event event;
						event.element = &ui_element;
						event.name = "http_request_done";

						if (result.has_value())
						{
							const auto write = utils::io::write_file(dest, result.value(), false);
							event.arguments = {id, true, write};
						}
						else
						{
							event.arguments = {id, false};
						}

						notify(event);
					}, ::scheduler::pipeline::renderer);
				}, ::scheduler::pipeline::async);
				return id;
			};

			game_type["sha"] = [](const game&, const std::string& data)
			{
				return utils::string::to_upper(utils::cryptography::sha1::compute(data, true));
			};

			game_type["environment"] = [](const game&)
			{
				return GIT_BRANCH;
			};

			game_type["binaryname"] = [](const game&)
			{
				utils::nt::library self;
				return self.get_name();
			};

			game_type["relaunch"] = [](const game&)
			{
				utils::nt::relaunch_self("-singleplayer");
				utils::nt::terminate();
			};

			game_type["isdebugbuild"] = [](const game&)
			{
#ifdef DEBUG
				return true;
#else
				return false;
#endif
			};

			struct player
			{
			};
			auto player_type = state.new_usertype<player>("player_");
			state["player"] = player();

			player_type["notify"] = [](const player&, const sol::this_state s, const std::string& name, sol::variadic_args va)
			{
				if (!::game::CL_IsCgameInitialized() || !::game::g_entities[0].client)
				{
					throw std::runtime_error("Not in game");
				}

				::scheduler::once([s, name, args = std::vector<sol::object>(va.begin(), va.end())]()
				{
					try
					{
						std::vector<scripting::script_value> arguments{};

						for (auto arg : args)
						{
							arguments.push_back(script_convert({s, arg}));
						}

						const auto player = scripting::call("getentbynum", {0}).as<scripting::entity>();
						scripting::notify(player, name, arguments);
					}
					catch (...)
					{
					}
				}, ::scheduler::pipeline::server);
			};
		}

		void setup_lui_types(sol::state& state, event_handler& handler, scheduler& scheduler)
		{
			auto userdata_type = state.new_usertype<userdata>("userdata_");

			userdata_type["new"] = sol::property(
				[](const userdata& userdata, const sol::this_state s)
				{
					return convert(s, userdata.get("new"));
				},
				[](const userdata& userdata, const sol::this_state s, const sol::lua_value& value)
				{
					userdata.set("new", convert({s, value}));
				}
			);

			for (const auto method : methods)
			{
				const auto name = method.first;

				userdata_type[name] = [name](const userdata& userdata, const sol::this_state s, sol::variadic_args va)
				{
					arguments arguments{};

					for (auto arg : va)
					{
						arguments.push_back(convert({s, arg}));
					}

					const auto values = call_method(userdata, name, arguments);
					std::vector<sol::lua_value> returns;

					for (const auto& value : values)
					{
						returns.push_back(convert(s, value));
					}

					return sol::as_returns(returns);
				};
			}

			userdata_type[sol::meta_function::index] = [](const userdata& userdata, const sol::this_state s, 
				const std::string& name)
			{
				return convert(s, userdata.get(name));
			};

			userdata_type[sol::meta_function::new_index] = [](const userdata& userdata, const sol::this_state s, 
				const std::string& name, const sol::lua_value& value)
			{
				userdata.set(name, convert({s, value}));
			};

			auto table_type = state.new_usertype<table>("table_");

			table_type["new"] = sol::property(
				[](const table& table, const sol::this_state s)
				{
					return convert(s, table.get("new"));
				},
				[](const table& table, const sol::this_state s, const sol::lua_value& value)
				{
					table.set("new", convert({s, value}));
				}
			);

			table_type["get"] = [](const table& table, const sol::this_state s,
				const std::string& name)
			{
				return convert(s, table.get(name));
			};

			table_type["set"] = [](const table& table, const sol::this_state s,
				const std::string& name, const sol::lua_value& value)
			{
				table.set(name, convert({s, value}));
			};

			table_type[sol::meta_function::index] = [](const table& table, const sol::this_state s,
				const std::string& name)
			{
				return convert(s, table.get(name));
			};

			table_type[sol::meta_function::new_index] = [](const table& table, const sol::this_state s,
				const std::string& name, const sol::lua_value& value)
			{
				table.set(name, convert({s, value}));
			};

			auto function_type = state.new_usertype<function>("function_");

			function_type[sol::meta_function::call] = [](const function& function, const sol::this_state s, sol::variadic_args va)
			{
				arguments arguments{};

				for (auto arg : va)
				{
					arguments.push_back(convert({s, arg}));
				}

				const auto values = function.call(arguments);
				std::vector<sol::lua_value> returns;

				for (const auto& value : values)
				{
					returns.push_back(convert(s, value));
				}

				return sol::as_returns(returns);
			};

			state["luiglobals"] = table((*::game::hks::lua_state)->globals.v.table);
			state["CoD"] = state["luiglobals"]["CoD"];
			state["LUI"] = state["luiglobals"]["LUI"];
			state["Engine"] = state["luiglobals"]["Engine"];
			state["Game"] = state["luiglobals"]["Game"];

			state.script(animation_script);
		}
	}

	context::context(std::string data, script_type type)
		: scheduler_(state_)
		  , event_handler_(state_)

	{
		this->state_.open_libraries(sol::lib::base,
		                            sol::lib::package,
		                            sol::lib::io,
		                            sol::lib::string,
		                            sol::lib::os,
		                            sol::lib::math,
		                            sol::lib::table);

		setup_io(this->state_);
		setup_json(this->state_);
		setup_vector_type(this->state_);
		setup_element_type(this->state_, this->event_handler_, this->scheduler_);
		setup_menu_type(this->state_, this->event_handler_, this->scheduler_);
		setup_game_type(this->state_, this->event_handler_, this->scheduler_);
		setup_lui_types(this->state_, this->event_handler_, this->scheduler_);

		if (type == script_type::file)
		{
			this->folder_ = data;

			this->state_["include"] = [this](const std::string& file)
			{
				this->load_script(file);
			};

			sol::function old_require = this->state_["require"];
			auto base_path = utils::string::replace(this->folder_, "/", ".") + ".";
			this->state_["require"] = [base_path, old_require](const std::string& path)
			{
				return old_require(base_path + path);
			};

			this->state_["scriptdir"] = [this]()
			{
				return this->folder_;
			};

			printf("Loading ui script '%s'\n", this->folder_.data());
			this->load_script("__init__");
		}
		
		if (type == script_type::code)
		{
			handle_error(this->state_.safe_script(data, &sol::script_pass_on_error));
		}
	}

	context::~context()
	{
		this->state_.collect_garbage();
		this->scheduler_.clear();
		this->event_handler_.clear();
		this->state_ = {};
	}

	void context::run_frame()
	{
		this->scheduler_.run_frame();
		this->state_.collect_garbage();
	}

	void context::notify(const event& e)
	{
		this->scheduler_.dispatch(e);
		this->event_handler_.dispatch(e);
	}

	void context::load_script(const std::string& script)
	{
		if (!this->loaded_scripts_.emplace(script).second)
		{
			return;
		}

		const auto file = (std::filesystem::path{this->folder_} / (script + ".lua")).generic_string();
		handle_error(this->state_.safe_script_file(file, &sol::script_pass_on_error));
	}
}
