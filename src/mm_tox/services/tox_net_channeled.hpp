#pragma once

#include <mm/services/net_channeled_interface.hpp>

#include <mm_tox/services/tox_service.hpp>

#include <vector>
#include <map>
#include <array>

namespace MM::Tox::Services {

// uses ToxService to provide the NetChanneledInterface service
class ToxNetChanneled : public MM::Services::NetChanneledInterface {
	protected:
		ToxService* _tox_service = nullptr;

	// service stuff
	public:
		ToxNetChanneled(void) {}
		ToxNetChanneled(std::array<channel_type, 10>& c_types) : _c_type_arr{c_types} {}

		const char* name(void) override { return "ToxNetServiceChanneled"; }

		bool enable(Engine& engine, std::vector<UpdateStrategies::TaskInfo>& task_array) override;
		void disable(Engine& engine) override;

	protected:
		void pull_fresh_packages(Engine& engine);


	// netservice stuff
	protected:
		// wether a channel_id is lossy or not
		std::array<channel_type, 10> _c_type_arr {
			channel_type::LOSSLESS,
			channel_type::LOSSLESS,
			channel_type::LOSSLESS,
			channel_type::LOSSLESS,
			channel_type::LOSSLESS,
			channel_type::LOSSLESS,
			channel_type::LOSSLESS,
			channel_type::LOSSLESS,
			channel_type::LOSSLESS,
			channel_type::LOSSLESS,
		};

		std::map<peer_id, std::array<std::vector<std::vector<uint8_t>>, 10>> _packets;
		std::map<peer_id, std::array<std::vector<uint8_t>, 10>> _large_packets_buffer; // for lossless

	public:
		channel_id getMaxChannels(void) override {
			// lossy The first byte of data must be in the range 192-254.
			// lossless The first byte of data must be in the range 69, 160-191.
			return 10; // TODO: arbitrary, but those 10 channels can be of the same type
		}

		bool getSupportedChannelType(channel_type) override { return true; } // both types are supported

		virtual size_t getMaxPacketSize(void) override {
			return tox_max_custom_packet_size() - (sizeof(channel_id) + 3); // TODO: large packs ?
		}

		bool sendPacket(peer_id peer, channel_id channel, const uint8_t* data, size_t data_size) override;
		bool sendPacketLarge(peer_id peer, channel_id channel, const uint8_t* data, size_t data_size) override;

		size_t forEachPacket(std::function<bool(peer_id, channel_id, uint8_t*, size_t)> fn) override;
		size_t forEachPacketPeer(peer_id peer, std::function<bool(peer_id, channel_id, uint8_t*, size_t)> fn) override;
		size_t forEachPacketPeerChannel(peer_id peer, channel_id channel, std::function<bool(peer_id, channel_id, uint8_t*, size_t)> fn) override;

		void clearPackets(void) override;

	public: // tox utilities
		peer_id toNet(const uint32_t tox_friend_number) const { return tox_friend_number; }
		uint32_t toTox(const peer_id peer) const { return peer; }
};

} // MM::Tox::Services

