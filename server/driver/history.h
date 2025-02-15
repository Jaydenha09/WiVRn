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

#include "clock_offset.h"

#include "os/os_time.h"
#include "util/u_logging.h"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <openxr/openxr.h>

namespace wivrn
{

template <typename Derived, typename Data, size_t MaxSamples = 10>
class history
{
	struct TimedData : public Data
	{
		XrTime produced_timestamp;
		XrTime at_timestamp_ns;
	};
	std::array<TimedData, MaxSamples> data{};

	std::mutex mutex;
	XrTime last_request;
	const std::string name;

protected:
	history(std::string name) :
	        last_request(os_monotonic_get_ns()), name(std::move(name)) {}

	// return true if object is active (last request is not too old)
	bool add_sample(XrTime produced_timestamp, XrTime timestamp, const Data & sample, const clock_offset & offset)
	{
		XrTime produced = offset.from_headset(produced_timestamp);
		XrTime t = offset.from_headset(timestamp);
		std::lock_guard lock(mutex);

		bool active = produced - last_request < 1'000'000'000;

		TimedData * target = data.data();
		if (offset)
		{
			for (auto & item: data)
			{
				// Discard reordered packets
				if (item.produced_timestamp > produced)
					return active;

				if (std::abs(item.at_timestamp_ns - t) < 2'000'000)
				{
					target = &item;
					break;
				}

				if (item.at_timestamp_ns < target->at_timestamp_ns)
					target = &item;
			}
		}
		*target = TimedData(sample, produced, t);
		return active;
	}

public:
	std::pair<std::chrono::nanoseconds, Data> get_at(XrTime at_timestamp_ns)
	{
		std::lock_guard lock(mutex);

		last_request = os_monotonic_get_ns();

		TimedData * before = nullptr;
		TimedData * after = nullptr;

		for (auto & item: data)
		{
			if (not item.at_timestamp_ns)
				continue;
			if (item.at_timestamp_ns < at_timestamp_ns)
			{
				if (not before or before->at_timestamp_ns < item.at_timestamp_ns)
					before = &item;
			}
			else
			{
				if (not after or after->at_timestamp_ns > item.at_timestamp_ns)
					after = &item;
			}
		}

		XrTime produced = 0;
		if (after)
			produced = std::max(produced, after->produced_timestamp);
		if (before)
			produced = std::max(produced, before->produced_timestamp);

		std::chrono::nanoseconds ex(std::max<XrDuration>(0, at_timestamp_ns - produced));

		if (before and after)
		{
			float t = float(after->at_timestamp_ns - at_timestamp_ns) /
			          (after->at_timestamp_ns - before->at_timestamp_ns);
			return {ex, Derived::interpolate(*before, *after, t)};
		}

		if (before)
		{
			if (at_timestamp_ns > before->at_timestamp_ns + U_TIME_1S_IN_NS)
				return {};
			if (at_timestamp_ns > before->at_timestamp_ns + U_TIME_1MS_IN_NS)
				U_LOG_W("prediction exceeded by %ldµs for %s, age: %ldµs extrapolation: %ldµs",
				        (at_timestamp_ns - before->at_timestamp_ns) / 1'000,
				        name.c_str(),
				        (last_request - before->produced_timestamp) / 1'000,
				        (at_timestamp_ns - before->produced_timestamp) / 1'000);
			return {ex, *before};
		}

		if (after)
			return {ex, *after};

		return {};
	}
};
} // namespace wivrn
