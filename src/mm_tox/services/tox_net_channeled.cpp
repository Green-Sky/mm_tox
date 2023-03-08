#include "./tox_net_channeled.hpp"

#include <mm_tox/services/tox_service.hpp>

#include <entt/core/hashed_string.hpp>

#include <vector>
#include <cstring>

#include <mm/logger.hpp>

namespace MM::Tox::Services {

// package structure:
// lossless:
// byte
// 0	: tox internal channel, mapped to channel_id
// 1	: large pkg indicator, 0 for not a large pkg, 1 for large pkg part, 2 for last large pkg part
// 2..	: the data

bool ToxNetChanneled::enable(Engine& engine, std::vector<UpdateStrategies::TaskInfo>& task_array) {
	_packets.clear();

	_tox_service = engine.tryService<ToxService>();
	if (!_tox_service) {
		return false;
	}

	task_array.push_back(
		UpdateStrategies::TaskInfo{"ToxNetChanneled::pull_fresh_packages"}
		.fn([this](Engine& e){ pull_fresh_packages(e); })
		.succeed("ToxService::iterate")
		.precede("SceneCollection::scene_tick") // evil hack
	);

	return true;
}

void ToxNetChanneled::disable(Engine&) {
	_packets.clear(); // ?

	_tox_service = nullptr;
}

void ToxNetChanneled::pull_fresh_packages(Engine&) {
	for (peer_id peer : _peer_list) {
		_tox_service->friend_packet_each(toTox(peer), [this, peer](auto& pk) {
			SPDLOG_INFO("got packet from {}", peer);

			if (pk.size() < 1) {
				// empty packet?
				return;
			}
			if (pk.size() < 2) {
				// empty packet? (only channel)
				return;
			}
			if (pk.size() < 3) {
				// empty packet? (pkg type missing)
				return;
			}

			channel_id channel = pk[0] - 192; // TODO: ugly
			if (channel >= 10) {
				// invalid channel
				return;
			}

			// lossy has no large packages ?
			//bool large_packet = pk.value()[1] != 0;

			_packets[peer][channel].emplace_back(pk.cbegin()+2, pk.cend());
		});

		_tox_service->friend_packet_each_lossless(toTox(peer), [this, peer](auto& pk) {
			SPDLOG_DEBUG("got lossless packet from {}", peer);
			if (pk.size() < 1) {
				SPDLOG_WARN("empty packet? (channel and pkg type missing)");
				// empty packet? (channel and pkg type missing)
				return;
			}
			if (pk.size() < 2) {
				SPDLOG_WARN("empty packet? (only channel, pkg type missing)");
				// empty packet? (only channel, pkg type missing)
				return;
			}
			if (pk.size() < 3) {
				SPDLOG_WARN("empty packet?");
				// empty packet?
				return;
			}

			// channel 160 is reserved for tox control pkgs
			channel_id channel = pk[0] - 161; // TODO: ugly
			if (channel >= 10) {
				SPDLOG_WARN("invalid channel");
				// invalid channel
				return;
			}

			bool large_packet = pk[1] != 0;

			if (!large_packet) {
				SPDLOG_TRACE("its a small one");
				// TODO: is memcpy faster? prob
				_packets[peer][channel].emplace_back(pk.cbegin()+2, pk.cend());
			} else {
				SPDLOG_TRACE("its a large one!");
				auto& lpkg_buff = _large_packets_buffer[peer][channel];

				uint8_t lpkg_num = pk[1];

				size_t pk_data_size = pk.size() - 2;
				size_t old_lpkg_buff_size = lpkg_buff.size();
				lpkg_buff.resize(old_lpkg_buff_size + pk_data_size);
				std::memcpy(lpkg_buff.data() + old_lpkg_buff_size, pk.data() + 2, pk_data_size);

				// lossless tox packet arrive in order
				if (lpkg_num == 2) { // magic value
					// last part
					SPDLOG_TRACE("and the last part!");

					_packets[peer][channel].emplace_back(lpkg_buff.cbegin(), lpkg_buff.cend());
					lpkg_buff.clear();
				}

			}
		});
	}
}

bool ToxNetChanneled::sendPacket(peer_id peer, channel_id channel, const uint8_t* data, size_t data_size) {
	if (channel >= 10) return false;
	if (!data) return false;
	if (data_size < 1) return false;

	std::vector<uint8_t> new_data;

	// TODO: this is ugly
	// map to tox channels
	if (_c_type_arr[channel] == channel_type::LOSSLESS) {
		new_data.push_back(161 + channel);
	} else {
		new_data.push_back(192 + channel);
	}

	// not a large packet
	new_data.push_back(0);

	size_t new_data_size_before = new_data.size();
	new_data.resize(new_data_size_before + data_size);

	// i hate those
	std::memcpy(new_data.data()+new_data_size_before, data, data_size);

	if (_c_type_arr[channel] == channel_type::LOSSLESS) {
		return _tox_service->friend_send_packet_lossless(toTox(peer), new_data.data(), new_data.size());
	} else {
		return _tox_service->friend_send_packet(toTox(peer), new_data.data(), new_data.size());
	}

	return true; // skipped
}

bool ToxNetChanneled::sendPacketLarge(peer_id peer, channel_id channel, const uint8_t* data, size_t data_size) {
	if (channel >= 10) return false;
	if (!data) return false;
	if (data_size < 1) return false;
	if (_c_type_arr[channel] != channel_type::LOSSLESS) return false;

	if (data_size <= getMaxPacketSize()) {
		return sendPacket(peer, channel, data, data_size);
	}

	bool succ = true;

	for (size_t remaining_data_size = data_size; remaining_data_size > 0;/* remaining_data_size -= getMaxPacketSize()*/) {
		std::vector<uint8_t> new_data;
		new_data.reserve(getMaxPacketSize() + 4); // to avoid reallocs

		size_t data_this_pk = std::min(remaining_data_size, getMaxPacketSize());

		// lossless channels
		new_data.push_back(161 + channel);

		// a large packet
		new_data.push_back(remaining_data_size <= getMaxPacketSize() ? 2 : 1); // 1 for lp part, 2 for last lp part

		size_t new_data_size_before = new_data.size();
		new_data.resize(new_data_size_before + data_this_pk);

		// i hate those, but theyr fast
		std::memcpy(new_data.data()+new_data_size_before, data + (data_size - remaining_data_size), data_this_pk);

		succ &= _tox_service->friend_send_packet_lossless(toTox(peer), new_data.data(), new_data.size());
		if (!succ) {
			SPDLOG_ERROR("failed to send partial large packet packet");
			break;
		}

		remaining_data_size -= data_this_pk;
	}

	return succ;
}

size_t ToxNetChanneled::forEachPacket(std::function<bool(peer_id, channel_id, uint8_t*, size_t)> fn) {
	size_t count = 0;
	for (auto&[peer, ch_data] : _packets) {
		for (channel_id channel = 0; channel < 10; channel++) {
			for (auto it = ch_data[channel].begin(); it != ch_data[channel].end();) {
				if (fn(peer, channel, it->data(), it->size())) {
					it = ch_data[channel].erase(it);
				} else {
					it++;
				}
				count++;
			}
		}
	}

	return count;
}

size_t ToxNetChanneled::forEachPacketPeer(peer_id peer, std::function<bool(peer_id, channel_id, uint8_t*, size_t)> fn) {
	size_t count = 0;

	for (channel_id channel = 0; channel < 10; channel++) {
		for (auto it = _packets[peer][channel].begin(); it != _packets[peer][channel].end();) {
			if (fn(peer, channel, it->data(), it->size())) {
				it = _packets[peer][channel].erase(it);
			} else {
				it++;
			}
			count++;
		}
	}

	return count;
}

size_t ToxNetChanneled::forEachPacketPeerChannel(peer_id peer, channel_id channel, std::function<bool(peer_id, channel_id, uint8_t*, size_t)> fn) {
	size_t count = 0;

	for (auto it = _packets[peer][channel].begin(); it != _packets[peer][channel].end();) {
		if (fn(peer, channel, it->data(), it->size())) {
			it = _packets[peer][channel].erase(it);
		} else {
			it++;
		}
		count++;
	}

	return count;
}

void ToxNetChanneled::clearPackets(void) {
	_packets.clear(); // TODO: this is bad
}

} // MM::Tox::Services

