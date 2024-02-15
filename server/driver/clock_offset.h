/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "wivrn_packets.h"

#include <chrono>
#include <cstdint>
#include <mutex>

class wivrn_connection;

struct clock_offset
{
	// y: headset time
	// x: server time
	// y = ax+b
	int64_t b = 0;
	double a = 1;

	operator bool() { return b != 0;}

	int64_t from_headset(uint64_t) const;

	std::chrono::nanoseconds
	to_headset(uint64_t timestamp_ns) const;
};

class clock_offset_estimator
{
	struct sample: public xrt::drivers::wivrn::from_headset::timesync_response
	{
		std::chrono::nanoseconds received;
	};

	std::mutex mutex;
	std::vector<sample> samples;
	size_t sample_index = 0;
	clock_offset offset;

	std::chrono::steady_clock::time_point next_sample{};

	public:
	void request_sample(wivrn_connection& connection);
	void add_sample(const xrt::drivers::wivrn::from_headset::timesync_response& sample);

	clock_offset get_offset();
};
