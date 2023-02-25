#pragma once
#include "game/game.hpp"
#include "script_value.hpp"

namespace scripting
{
	class animation final
	{
	public:
		animation(uint64_t value);

		uint64_t get_value() const;
	private:
		uint64_t value_;
	};
}
