#pragma once

#include <mm/engine.hpp>

// TODO: make tox.h private
#include <tox.h>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <optional>

// fwd
//typedef struct Tox Tox;

namespace MM::Tox::Services {

// the pkg id for "internal" pkgs
//#define MM_TOX_LOSSY_PKG_ID_INTERNAL 1900000
#define MM_TOX_LOSSLESS_PKG_ID_INTERNAL 160

// please keep this updated
enum ToxInternalPkgID : uint8_t {
	MM_INSTANCE = 0u,			// tell someone, that you are a MushMachine instance
	MM_APP,						// after knowing the other is mm instance, tell you app string (eg "gh4nr-prot3")

	TOX_LOBBY_PUBLIC_INFO1,		// tell others, u have a open lobby (not just rp)
	TOX_LOBBY_INVITE1,			// tell others to join your lobby (aka private_info)
	TOX_LOBBY_JOIN,				// tell host u wonna join
	TOX_LOBBY_JOIN_ACK,			// tell joinee u ack
	TOX_LOBBY_LEAVE,			// tell host u leave, or host tells you to
	TOX_LOBBY_PING,				// sent by the host in a fixed interval, client has to respond

	ToxInternalPkgID_MAX		// used for undefined (error)
};

class ToxService : public MM::Services::Service {
	// callbacks need public
	public:
		std::string _path_to_toxsave;

		std::string _app_name = "NoAppName";

		struct Tox* _tox = nullptr;

		struct ToxFriend {
			bool __dirty = true; // used for sending internal state
			bool mm_instance = false;
			std::string mm_app{};

			Tox_Connection connection_status = TOX_CONNECTION_NONE;

			std::string name;
			std::string status_msg;
			Tox_User_Status status = TOX_USER_STATUS_NONE;

			bool typing = false;

			std::vector<std::tuple<bool, Tox_Message_Type, std::string>> messages; // self, msg_type, msg

			std::deque<std::vector<uint8_t>> packets;
			std::deque<std::vector<uint8_t>> packets_lossless;
			std::deque<std::vector<uint8_t>> packets_internal;
			std::deque<std::vector<uint8_t>> packets_lossless_internal;
		};
		std::unordered_map<uint32_t, ToxFriend> _tox_friends; // friend_number

		struct ToxConference {
			Tox_Conference_Type type;
			//bool connected = false; // ??
			std::string title;

			std::unordered_map<uint32_t, std::string> peers; // peer_number, name
			std::vector<std::tuple<uint32_t, Tox_Message_Type, std::string>> messages; // peer_number, msg_type, msg

			// sadly no custom packet support yet -> see groups
		};
		std::unordered_map<uint32_t, ToxConference> _tox_conferences; // conference_number

		// TODO: implement reciept
		//struct ToxFriendMessage {
			//uint32_t message_id;
			//std::string msg;
			//bool receipt_received = false;
		//};
		//std::unordered_map<uint32_t, std::vector<ToxFriendMessage>> _tox_friend_msgs;

	public:
		ToxService(void);
		ToxService(Engine& engine, const std::string& path_to_toxsave);

		const char* name(void) override { return "ToxService"; }

		bool enable(Engine& engine, std::vector<UpdateStrategies::TaskInfo>& task_array) override;
		void disable(Engine& engine) override;

	protected:
		void iterate(Engine& engine);
		void pkg_cleanup(Engine& engine);

	protected:
		std::string _own_tox_id_stringyfied;

	public:
		void update_savefile(Engine& engine);

		const std::string& get_own_tox_id_string(void) { return _own_tox_id_stringyfied; }

		// send a message to a single friend
		bool friend_send_message(uint32_t friend_number, std::string_view msg);

		// send a message to a conference
		bool conference_send_message(uint32_t conference_number, std::string_view msg);

		// send a message to all your friends
		bool broadcast_message(std::string_view msg);

		// send a packet (raw data, tox cust. packs.) to a friend
		bool friend_send_packet(uint32_t friend_number, uint8_t* mem, size_t size);
		bool friend_send_packet_lossless(uint32_t friend_number, uint8_t* mem, size_t size);

		// send a packet to all your friends
		bool broadcast_packet(uint8_t* mem, size_t size);
		bool broadcast_packet_lossless(uint8_t* mem, size_t size);

		bool add_friend(const uint8_t tox_id[TOX_ADDRESS_SIZE], std::string_view msg);
		bool add_friend(std::string_view text_tox_id, std::string_view msg);

		std::string get_name(void);
		bool set_name(std::string_view new_name);
		bool set_status(std::string_view new_status);

	private:
		// internal helper
		template<typename T, typename Fn>
		void __each_packet_fren(T& list, Fn&& fn) {
			for (std::vector<uint8_t>& vec : list) {
				fn(vec);
			}
		}

		// internal helper
		template<typename CGetFn, typename Fn>
		void __each_packet_any(CGetFn&& container_getter_fn, Fn&& fn) {
			for (auto& [f_id, f] : _tox_friends) {
				for (std::vector<uint8_t>& vec : container_getter_fn(f)) {
					fn(f_id, vec);
				}
			}
		}

	public:
		template<typename Fn>
		void friend_packet_each(uint32_t friend_number, Fn&& fn) {
			if (!_tox_friends.count(friend_number)) { return; }
			__each_packet_fren(_tox_friends[friend_number].packets, fn);
		}

		template<typename Fn>
		void friend_packet_each_lossless(uint32_t friend_number, Fn&& fn) {
			if (!_tox_friends.count(friend_number)) { return; }
			__each_packet_fren(_tox_friends[friend_number].packets_lossless, fn);
		}

		template<typename Fn>
		void friend_packet_each_lossless_internal(uint32_t friend_number, Fn&& fn) {
			if (!_tox_friends.count(friend_number)) { return; }
			__each_packet_fren(_tox_friends[friend_number].packets_lossless_internal, fn);
		}

		template<typename Fn>
		void any_packet_each(Fn&& fn) {
			__each_packet_any(
				[](auto& f) { return f.packets; },
				fn
			);
		}

		template<typename Fn>
		void any_packet_each_lossless(Fn&& fn) {
			__each_packet_any(
				[](auto& f) { return f.packets_lossless; },
				fn
			);
		}

		template<typename Fn>
		void any_packet_each_lossless_internal(Fn&& fn) {
			__each_packet_any(
				[](auto& f) { return f.packets_lossless_internal; },
				fn
			);
		}
};

} // MM::Tox::Services

