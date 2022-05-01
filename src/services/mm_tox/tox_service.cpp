#include "./tox_service.hpp"

#include <entt/core/hashed_string.hpp>

#include <mm/services/filesystem.hpp>

#include <sodium/utils.h>
#include <tox.h>

#include <random>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cstring>
#include <cassert>

#include <mm/logger.hpp>
#define LOG_CRIT(...)		__LOG_CRIT(	"MM::Tox", __VA_ARGS__)
#define LOG_ERROR(...)		__LOG_ERROR("MM::Tox", __VA_ARGS__)
#define LOG_WARN(...)		__LOG_WARN(	"MM::Tox", __VA_ARGS__)
#define LOG_INFO(...)		__LOG_INFO(	"MM::Tox", __VA_ARGS__)
#define LOG_DEBUG(...)		__LOG_DEBUG("MM::Tox", __VA_ARGS__)
#define LOG_TRACE(...)		__LOG_TRACE("MM::Tox", __VA_ARGS__)

#define LOGTOXCB(x) LOG_TRACE("[ToxCallBack] {}", x)

// ============ tox callbacks ============

// logging
static void log_cb(Tox *tox, TOX_LOG_LEVEL level, const char *file, uint32_t line, const char *func, const char *message, void *user_data);

// self
static void self_connection_status_cb(Tox *tox, TOX_CONNECTION connection_status, void *user_data);

// friend
static void friend_name_cb(Tox *tox, uint32_t friend_number, const uint8_t *name, size_t length, void *user_data);
static void friend_status_message_cb(Tox *tox, uint32_t friend_number, const uint8_t *message, size_t length, void *user_data);
static void friend_status_cb(Tox *tox, uint32_t friend_number, TOX_USER_STATUS status, void *user_data);
static void friend_connection_status_cb(Tox *tox, uint32_t friend_number, TOX_CONNECTION connection_status, void *user_data);
static void friend_typing_cb(Tox *tox, uint32_t friend_number, bool is_typing, void *user_data);
static void friend_read_receipt_cb(Tox *tox, uint32_t friend_number, uint32_t message_id, void *user_data);
static void friend_request_cb(Tox *tox, const uint8_t *public_key, const uint8_t *message, size_t length, void *user_data);
static void friend_message_cb(Tox *tox, uint32_t friend_number, TOX_MESSAGE_TYPE type, const uint8_t *message, size_t length, void *user_data);

// file
static void file_recv_control_cb(Tox *tox, uint32_t friend_number, uint32_t file_number, TOX_FILE_CONTROL control, void *user_data);
static void file_chunk_request_cb(Tox *tox, uint32_t friend_number, uint32_t file_number, uint64_t position, size_t length, void *user_data);
static void file_recv_cb(Tox *tox, uint32_t friend_number, uint32_t file_number, uint32_t kind, uint64_t file_size, const uint8_t *filename, size_t filename_length, void *user_data);
static void file_recv_chunk_cb(Tox *tox, uint32_t friend_number, uint32_t file_number, uint64_t position, const uint8_t *data, size_t length, void *user_data);

// conference
static void conference_invite_cb(Tox *tox, uint32_t friend_number, TOX_CONFERENCE_TYPE type, const uint8_t *cookie, size_t length, void *user_data);
static void conference_connected_cb(Tox *tox, uint32_t conference_number, void *user_data);
static void conference_message_cb(Tox *tox, uint32_t conference_number, uint32_t peer_number, TOX_MESSAGE_TYPE type, const uint8_t *message, size_t length, void *user_data);
static void conference_title_cb(Tox *tox, uint32_t conference_number, uint32_t peer_number, const uint8_t *title, size_t length, void *user_data);
static void conference_peer_name_cb(Tox *tox, uint32_t conference_number, uint32_t peer_number, const uint8_t *name, size_t length, void *user_data);
static void conference_peer_list_changed_cb(Tox *tox, uint32_t conference_number, void *user_data);

// custom packets
static void friend_lossy_packet_cb(Tox *tox, uint32_t friend_number, const uint8_t *data, size_t length, void *user_data);
static void friend_lossless_packet_cb(Tox *tox, uint32_t friend_number, const uint8_t *data, size_t length, void *user_data);

// ============ tox callbacks ============

static void setup_tox_callbacks(Tox* tox) {
	//tox_callback_self_connection_status(tox, self_connection_status_cb);
#define CALLBACK_REG(x) tox_callback_##x(tox, x##_cb)
	CALLBACK_REG(self_connection_status);

	CALLBACK_REG(friend_name);
	CALLBACK_REG(friend_status_message);
	CALLBACK_REG(friend_status);
	CALLBACK_REG(friend_connection_status);
	CALLBACK_REG(friend_typing);
	CALLBACK_REG(friend_read_receipt);
	CALLBACK_REG(friend_request);
	CALLBACK_REG(friend_message);

	CALLBACK_REG(file_recv_control);
	CALLBACK_REG(file_chunk_request);
	CALLBACK_REG(file_recv);
	CALLBACK_REG(file_recv_chunk);

	CALLBACK_REG(conference_invite);
	CALLBACK_REG(conference_connected);
	CALLBACK_REG(conference_message);
	CALLBACK_REG(conference_title);
	CALLBACK_REG(conference_peer_name);
	CALLBACK_REG(conference_peer_list_changed);

	CALLBACK_REG(friend_lossy_packet);
	CALLBACK_REG(friend_lossless_packet);

#undef CALLBACK_REG
}

namespace MM::Services::Tox {

// internal pkg
constexpr size_t __internal_pkg_MMInstance_size = 8u;
bool __internal_pkg_MMInstance_is_magic_correct(uint8_t* data) {
	static constexpr uint8_t magic_num_value_ref[8] {
		0x83u,
		0xafu,
		0x33u,
		0x31u,
		0x70u,
		0x62u,
		0x33u,
		0x88u,
	};

	for (size_t i = 0; i < 8; i++) {
		if (data[i] != magic_num_value_ref[i]) {
			return false;
		}
	}

	return true;
}

constexpr size_t __internal_pkg_MMApp_size = 254u;
// internal pkg end

ToxService::ToxService(void) {
	MM::Logger::initSectionLogger("MM::Tox");
}

ToxService::ToxService(Engine& engine, const std::string& path_to_toxsave) {
	MM::Logger::initSectionLogger("MM::Tox");

	auto& fs = engine.getService<MM::Services::FilesystemService>();

	if (fs.exists(path_to_toxsave.c_str()) && !fs.isFile(path_to_toxsave.c_str())) {
		LOG_CRIT("toxsave is not a file");
		return;
	}

	_path_to_toxsave = path_to_toxsave;
}

bool ToxService::enable(Engine& engine, std::vector<UpdateStrategies::TaskInfo>& task_array) {
	assert(TOX_VERSION_IS_ABI_COMPATIBLE()); // TODO: assert in release
	assert(_tox == nullptr);

	auto& fs = engine.getService<MM::Services::FilesystemService>();

	TOX_ERR_OPTIONS_NEW err_opt_new;
	Tox_Options* options = tox_options_new(&err_opt_new);
	assert(err_opt_new == TOX_ERR_OPTIONS_NEW::TOX_ERR_OPTIONS_NEW_OK);
	tox_options_set_log_callback(options, log_cb);
#ifndef USE_TEST_NETWORK
	tox_options_set_local_discovery_enabled(options, true);
#endif
	tox_options_set_udp_enabled(options, true);
	tox_options_set_hole_punching_enabled(options, true);

	std::vector<uint8_t> save_file_mem;
	// if no path, no persistence
	if (!_path_to_toxsave.empty()) {
		auto file = fs.open(_path_to_toxsave.c_str());
		if (file) {
			auto file_length = fs.length(file);
			save_file_mem.resize(file_length);
			fs.read(file, save_file_mem.data(), save_file_mem.size());
			fs.close(file);

			options->savedata_type = TOX_SAVEDATA_TYPE_TOX_SAVE;
			options->savedata_data = save_file_mem.data();
			options->savedata_length = save_file_mem.size();
		}
	}

	TOX_ERR_NEW err_new;
	_tox = tox_new(options, &err_new);
	if (err_new != TOX_ERR_NEW_OK) {
		LOG_ERROR("tox_new failed with error code {}", err_new);
		return false;
	}

	// print own address
	{
		uint8_t self_addr[TOX_ADDRESS_SIZE] = {};
		tox_self_get_address(_tox, self_addr);
		//const size_t hex_buffer_len = TOX_ADDRESS_SIZE*2 + 1;
		//char hex_buffer[hex_buffer_len] = {};
		//sodium_bin2hex(hex_buffer, hex_buffer_len, self_addr, TOX_ADDRESS_SIZE);
		_own_tox_id_stringyfied.resize(TOX_ADDRESS_SIZE*2 + 1, '\0');
		sodium_bin2hex(_own_tox_id_stringyfied.data(), _own_tox_id_stringyfied.size(), self_addr, TOX_ADDRESS_SIZE);
		_own_tox_id_stringyfied.resize(TOX_ADDRESS_SIZE*2); // remove '\0'
		LOG_INFO("created tox instance with id '{}'", _own_tox_id_stringyfied);
	}

	setup_tox_callbacks(_tox);

	// dht bootstrap
	{ // TODO: use file, and nodes.tox.chat/json
		struct DHT_node {
			const char *ip;
			uint16_t port;
			const char key_hex[TOX_PUBLIC_KEY_SIZE*2 + 1]; // 1 for null terminator
			unsigned char key_bin[TOX_PUBLIC_KEY_SIZE];
		};

		DHT_node nodes[] =
		{
#ifndef USE_TEST_NETWORK
			{"tox.plastiras.org",					33445,	"8E8B63299B3D520FB377FE5100E65E3322F7AE5B20A0ACED2981769FC5B43725", {}}, // 14
			{"tox.plastiras.org",					443,	"8E8B63299B3D520FB377FE5100E65E3322F7AE5B20A0ACED2981769FC5B43725", {}}, // 14
			{"104.244.74.69",						33445,	"8E8B63299B3D520FB377FE5100E65E3322F7AE5B20A0ACED2981769FC5B43725", {}}, // 14
			{"tox2.plastiras.org",					33445,	"B6626D386BE7E3ACA107B46F48A5C4D522D29281750D44A0CBA6A2721E79C951", {}}, // 14
	#if 0
			{"tox.verdict.gg",						33445,	"1C5293AEF2114717547B39DA8EA6F1E331E5E358B35F9B6B5F19317911C5F976", {}},
			{"78.46.73.141",						33445,	"02807CF4F8BB8FB390CC3794BDF1E8449E9A8392C5D3F2200019DA9F1E812E46", {}},
			{"2a01:4f8:120:4091::3",				33445,	"02807CF4F8BB8FB390CC3794BDF1E8449E9A8392C5D3F2200019DA9F1E812E46", {}},
			{"tox.abilinski.com",					33445,	"10C00EB250C3233E343E2AEBA07115A5C28920E9C8D29492F6D00B29049EDC7E", {}},
			{"tox.novg.net",						33445,	"D527E5847F8330D628DAB1814F0A422F6DC9D0A300E6C357634EE2DA88C35463", {}},
			{"198.199.98.108",						33445,	"BEF0CFB37AF874BD17B9A8F9FE64C75521DB95A37D33C5BDB00E9CF58659C04F", {}},
			{"2604:a880:1:20::32f:1001",			33445,	"BEF0CFB37AF874BD17B9A8F9FE64C75521DB95A37D33C5BDB00E9CF58659C04F", {}},
			{"tox.kurnevsky.net",					33445,	"82EF82BA33445A1F91A7DB27189ECFC0C013E06E3DA71F588ED692BED625EC23", {}},
			{"87.118.126.207",						33445,	"0D303B1778CA102035DA01334E7B1855A45C3EFBC9A83B9D916FFDEBC6DD3B2E", {}},
			{"81.169.136.229",						33445,	"E0DB78116AC6500398DDBA2AEEF3220BB116384CAB714C5D1FCD61EA2B69D75E", {}},
			{"2a01:238:4254:2a00:7aca:fe8c:68e0:27ec",	33445,	"E0DB78116AC6500398DDBA2AEEF3220BB116384CAB714C5D1FCD61EA2B69D75E", {}},
			{"205.185.115.131",						53,		"3091C6BEB2A993F1C6300C16549FABA67098FF3D62C6D253828B531470B53D68", {}},
			{"205.185.115.131",						33445,	"3091C6BEB2A993F1C6300C16549FABA67098FF3D62C6D253828B531470B53D68", {}},
			{"205.185.115.131",						443,	"3091C6BEB2A993F1C6300C16549FABA67098FF3D62C6D253828B531470B53D68", {}},
			{"tox2.abilinski.com",					33445,	"7A6098B590BDC73F9723FC59F82B3F9085A64D1B213AAF8E610FD351930D052D", {}},
			{"floki.blog",							33445,	"6C6AF2236F478F8305969CCFC7A7B67C6383558FF87716D38D55906E08E72667", {}},
	#endif
#else // testnet
			{"tox.plastiras.org",					38445,	"5E47BA1DC3913EB2CBF2D64CE4F23D8BFE5391BFABE5C43C5BAD13F0A414CD77", {}}, // 14
#endif
		};

		for (size_t i = 0; i < sizeof(nodes)/sizeof(DHT_node); i ++) {
			sodium_hex2bin(nodes[i].key_bin, sizeof(nodes[i].key_bin),
							nodes[i].key_hex, sizeof(nodes[i].key_hex)-1, NULL, NULL, NULL);
			tox_bootstrap(_tox, nodes[i].ip, nodes[i].port, nodes[i].key_bin, NULL);
			// TODO: use extra tcp option to avoid error msgs
			tox_add_tcp_relay(_tox, nodes[i].ip, nodes[i].port, nodes[i].key_bin, NULL);
		}
	}

	if (get_name().empty()) {
		// default name
		std::string name = "NoNameMM_";
		name += std::to_string(std::random_device{}() % 1000);
		if (!set_name(name)) {
			return false;
		}
	}

	if (!set_status("running MushMachine...")) {
		return false;
	}

	// fill in friends
	{
		std::vector<uint32_t> friend_list;
		friend_list.resize(tox_self_get_friend_list_size(_tox));
		tox_self_get_friend_list(_tox, friend_list.data());

		// TODO: propper error checking
		for (uint32_t friend_number : friend_list) {
			TOX_ERR_FRIEND_QUERY err_f_query;
			auto& f = _tox_friends[friend_number];

			// dep
			//f.connection_status = tox_friend_get_connection_status(_tox, friend_number, &err_f_query);
			//assert(err_f_query == TOX_ERR_FRIEND_QUERY_OK);

			f.name.resize(tox_friend_get_name_size(_tox, friend_number, &err_f_query));
			assert(err_f_query == TOX_ERR_FRIEND_QUERY_OK);
			tox_friend_get_name(_tox, friend_number, reinterpret_cast<uint8_t*>(f.name.data()), &err_f_query);
			assert(err_f_query == TOX_ERR_FRIEND_QUERY_OK);

			f.status_msg.resize(tox_friend_get_status_message_size(_tox, friend_number, &err_f_query));
			assert(err_f_query == TOX_ERR_FRIEND_QUERY_OK);
			tox_friend_get_status_message(_tox, friend_number, reinterpret_cast<uint8_t*>(f.status_msg.data()), &err_f_query);
			assert(err_f_query == TOX_ERR_FRIEND_QUERY_OK);

			// dep
			//f.status = tox_friend_get_status(_tox, friend_number, &err_f_query);
			//assert(err_f_query == TOX_ERR_FRIEND_QUERY_OK);
		}
	}

	{ // fill in conferences
		std::vector<uint32_t> chat_list;
		chat_list.resize(tox_conference_get_chatlist_size(_tox));
		tox_conference_get_chatlist(_tox, chat_list.data());

		// TODO: propper error checking
		for (uint32_t chat_number : chat_list) {
			TOX_ERR_CONFERENCE_GET_TYPE err_c_type;
			auto type = tox_conference_get_type(_tox, chat_number, &err_c_type);
			if (type == Tox_Conference_Type::TOX_CONFERENCE_TYPE_AV) {
				continue; // we have no support for those rn
			}

			auto& g = _tox_conferences[chat_number];
			g.type = type;

			TOX_ERR_CONFERENCE_TITLE err_c_title;
			g.title.resize(tox_conference_get_title_size(_tox, chat_number, &err_c_title));
			assert(err_c_title == TOX_ERR_CONFERENCE_TITLE_OK);
			tox_conference_get_title(_tox, chat_number, reinterpret_cast<uint8_t*>(g.title.data()), &err_c_title);
			assert(err_c_title == TOX_ERR_CONFERENCE_TITLE_OK);
		}
	}

	update_savefile(engine);

	// setup tasks
	task_array.push_back(
		UpdateStrategies::TaskInfo{"ToxService::iterate"}
		.fn([this](Engine& e){ iterate(e); })
	);

	task_array.push_back(
		UpdateStrategies::TaskInfo{"ToxService::pkg_cleanup"}
		.fn([this](Engine& e){ pkg_cleanup(e); })
		.phase(UpdateStrategies::update_phase_t::POST)
	);

	return true;
}

void ToxService::disable(Engine& engine) {
	update_savefile(engine);

	tox_kill(_tox);
	_tox = nullptr;
}


void ToxService::iterate(Engine&) {
	tox_iterate(_tox, this);

	// process some internal pkgs and send if diry
	for (auto&& it : _tox_friends) {
		// incomming
		auto& pk_q = it.second.packets_lossless_internal;
		for (auto q_it = pk_q.begin(); q_it != pk_q.end();) {
			bool p_mod = false;
			if ((*q_it).size() < 2) {
				LOG_WARN("malformed internal pkg detected");
				q_it = pk_q.erase(q_it);
				continue;
			}

			switch ((*q_it)[1]) {
				case ToxInternalPkgID::MM_INSTANCE:
					p_mod = true;
					if ((*q_it).size() != __internal_pkg_MMInstance_size+2) {
						LOG_ERROR("malformed internal pkg MM_INSTANCE detected, size:{} should:{}", (*q_it).size(), __internal_pkg_MMInstance_size+2);
						break;
					}

					if (__internal_pkg_MMInstance_is_magic_correct((*q_it).data() + 2u)) {
						it.second.mm_instance = true;
					} else {
						LOG_ERROR("malformed internal pkg MM_INSTANCE magic detected");
					}
					break;
				case ToxInternalPkgID::MM_APP:
					p_mod = true;
					if ((*q_it).size() != __internal_pkg_MMApp_size+2) {
						LOG_ERROR("malformed internal pkg MM_APP detected, size:{} should:{}", (*q_it).size(), __internal_pkg_MMApp_size+2);
						break;
					}

					it.second.mm_app = std::string_view{reinterpret_cast<const char*>((*q_it).data()+2), __internal_pkg_MMApp_size};
					break;
			}

			if (p_mod) {
				q_it = pk_q.erase(q_it);
			} else {
				q_it++;
			}
		}

		// not connected (anymore???)
		if (it.second.connection_status == Tox_Connection::TOX_CONNECTION_NONE) {
			continue;
		}

		// outgoing
		if (it.second.__dirty) {
			it.second.__dirty = false;

			{ // mm instance
				static std::array<uint8_t, 2+8> mm_inst_arr {
					MM_TOX_LOSSLESS_PKG_ID_INTERNAL,
					ToxInternalPkgID::MM_INSTANCE,
					0x83u,
					0xafu,
					0x33u,
					0x31u,
					0x70u,
					0x62u,
					0x33u,
					0x88u,
				};
				friend_send_packet_lossless(it.first, mm_inst_arr.data(), mm_inst_arr.size());
			}

			// TODO: this is one hell of .... bad code, just rewrite plz
			{ // app name
				if (_app_name.size() != 256-2) {
					_app_name.resize(256-2);
				}

				std::array<uint8_t, 256> mm_app_arr {
					MM_TOX_LOSSLESS_PKG_ID_INTERNAL,
					ToxInternalPkgID::MM_APP,
				};
				for (size_t i = 2; i < 256; i++) {
					mm_app_arr[i] = _app_name[i-2];
				}
				friend_send_packet_lossless(it.first, mm_app_arr.data(), mm_app_arr.size());
			}
		}
	}
}

void ToxService::pkg_cleanup(Engine&) {
	for (auto&& it : _tox_friends) {
		it.second.packets.clear();
		it.second.packets_internal.clear();
		it.second.packets_lossless.clear();
		it.second.packets_lossless_internal.clear();
	}
}

void ToxService::update_savefile(Engine& engine) {
	if (_path_to_toxsave.empty()) {
		return;
	}

	std::vector<uint8_t> save_mem(tox_get_savedata_size(_tox));
	tox_get_savedata(_tox, save_mem.data());

	auto& fs = engine.getService<MM::Services::FilesystemService>();
	auto file = fs.open(_path_to_toxsave.c_str(), FilesystemService::FOPEN_t::WRITE);

	if (!file) {
		// error
		return;
	}

	fs.write(file, save_mem.data(), save_mem.size());
}

bool ToxService::friend_send_message(uint32_t friend_number, std::string_view msg) {
	Tox_Err_Friend_Send_Message err_f_send_m;

	tox_friend_send_message(
		_tox,
		friend_number,
		Tox_Message_Type::TOX_MESSAGE_TYPE_NORMAL,
		reinterpret_cast<const uint8_t*>(msg.data()),
		msg.size(),
		&err_f_send_m
	);

	bool succ = err_f_send_m == Tox_Err_Friend_Send_Message::TOX_ERR_FRIEND_SEND_MESSAGE_OK;

	if (succ) {
		_tox_friends[friend_number].messages.emplace_back(
			true,
			Tox_Message_Type::TOX_MESSAGE_TYPE_NORMAL,
			msg
		);
	}

	return succ;
}

bool ToxService::conference_send_message(uint32_t conference_number, std::string_view msg) {
	Tox_Err_Conference_Send_Message err_conf_send_m;

	tox_conference_send_message(
		_tox,
		conference_number,
		Tox_Message_Type::TOX_MESSAGE_TYPE_NORMAL,
		reinterpret_cast<const uint8_t*>(msg.data()),
		msg.size(),
		&err_conf_send_m
	);

	return err_conf_send_m == Tox_Err_Conference_Send_Message::TOX_ERR_CONFERENCE_SEND_MESSAGE_OK;
}

bool ToxService::broadcast_message(std::string_view msg) {
	bool res = true;

	for (auto& f : _tox_friends) {
		res &= friend_send_message(f.first, msg);
	}

	return res;
}

bool ToxService::friend_send_packet(uint32_t friend_number, uint8_t* mem, size_t size) {
	if (size == 0) { // TODO: is this an actual error?
		LOG_ERROR("sending packet to friend failed: size is zero!");
		return false;
	}
	if (mem[0] < 200 || mem[0] > 254) {
		LOG_ERROR("sending packet to friend failed: first byte not in range!");
		return false;
	}

	TOX_ERR_FRIEND_CUSTOM_PACKET err_f_send;
	if (!tox_friend_send_lossy_packet(_tox, friend_number, mem, size, &err_f_send)) {
		if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_EMPTY) {
			LOG_ERROR("sending packet to friend failed: " "EMPTY");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_TOO_LONG) {
			LOG_ERROR("sending packet to friend failed: " "TOO_LONG");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_FRIEND_NOT_CONNECTED) {
			LOG_ERROR("sending packet to friend failed: " "FRIEND_NOT_CONNECTED");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_NULL) {
			LOG_ERROR("sending packet to friend failed: " "NULL");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_SENDQ) {
			LOG_ERROR("sending packet to friend failed: " "SENDQ");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_INVALID) {
			LOG_ERROR("sending packet to friend failed: " "INVALID");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_FRIEND_NOT_FOUND) {
			LOG_ERROR("sending packet to friend failed: " "FRIEND_NOT_FOUND");
		}
		return false;
	}

	return true;
}

bool ToxService::friend_send_packet_lossless(uint32_t friend_number, uint8_t* mem, size_t size) {
	if (size == 0) { // TODO: is this an actual error?
		LOG_ERROR("sending packet to friend failed: size is zero!");
		return false;
	}
	if (mem[0] < 160 || mem[0] > 191) {
		LOG_ERROR("sending packet to friend failed: first byte not in range!");
		return false;
	}

	TOX_ERR_FRIEND_CUSTOM_PACKET err_f_send;
	if (!tox_friend_send_lossless_packet(_tox, friend_number, mem, size, &err_f_send)) {
		if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_EMPTY) {
			LOG_ERROR("sending packet to friend failed: " "EMPTY");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_TOO_LONG) {
			LOG_ERROR("sending packet to friend failed: " "TOO_LONG");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_FRIEND_NOT_CONNECTED) {
			LOG_ERROR("sending packet to friend failed: " "FRIEND_NOT_CONNECTED");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_NULL) {
			LOG_ERROR("sending packet to friend failed: " "NULL");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_SENDQ) {
			LOG_ERROR("sending packet to friend failed: " "SENDQ");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_INVALID) {
			LOG_ERROR("sending packet to friend failed: " "INVALID");
		} else if (err_f_send == Tox_Err_Friend_Custom_Packet::TOX_ERR_FRIEND_CUSTOM_PACKET_FRIEND_NOT_FOUND) {
			LOG_ERROR("sending packet to friend failed: " "FRIEND_NOT_FOUND");
		}
		return false;
	}

	return true;
}

bool ToxService::broadcast_packet(uint8_t* mem, size_t size) {
	bool res = true;

	for (auto& f : _tox_friends) {
		if (f.second.connection_status != Tox_Connection::TOX_CONNECTION_NONE) {
			res &= friend_send_packet(f.first, mem, size);
		}
	}

	return res;
}

bool ToxService::broadcast_packet_lossless(uint8_t* mem, size_t size) {
	bool res = true;

	for (auto& f : _tox_friends) {
		if (f.second.connection_status != Tox_Connection::TOX_CONNECTION_NONE) {
			res &= friend_send_packet_lossless(f.first, mem, size);
		}
	}

	return res;
}

bool ToxService::add_friend(const uint8_t tox_id[TOX_ADDRESS_SIZE], std::string_view msg) {
	TOX_ERR_FRIEND_ADD err_f_add;

	tox_friend_add(_tox, tox_id, reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), &err_f_add);

	if (err_f_add == Tox_Err_Friend_Add::TOX_ERR_FRIEND_ADD_OWN_KEY) {
		LOG_ERROR("adding friend failed: " "OWN_KEY");
	} else if (err_f_add == Tox_Err_Friend_Add::TOX_ERR_FRIEND_ADD_TOO_LONG) {
		LOG_ERROR("adding friend failed: " "TOO_LONG");
	} else if (err_f_add == Tox_Err_Friend_Add::TOX_ERR_FRIEND_ADD_NO_MESSAGE) {
		LOG_ERROR("adding friend failed: " "NO_MESSAGE");
	} else if (err_f_add == Tox_Err_Friend_Add::TOX_ERR_FRIEND_ADD_ALREADY_SENT) {
		LOG_ERROR("adding friend failed: " "ALREADY_SENT");
	} else if (err_f_add == Tox_Err_Friend_Add::TOX_ERR_FRIEND_ADD_BAD_CHECKSUM) {
		LOG_ERROR("adding friend failed: " "BAD_CHECKSUM");
	} else if (err_f_add == Tox_Err_Friend_Add::TOX_ERR_FRIEND_ADD_SET_NEW_NOSPAM) {
		LOG_ERROR("adding friend failed: " "SET_NEW_NOSPAM");
	}

	return err_f_add == Tox_Err_Friend_Add::TOX_ERR_FRIEND_ADD_OK;
}

bool ToxService::add_friend(std::string_view text_tox_id, std::string_view msg) {
	if (text_tox_id.size() != TOX_ADDRESS_SIZE*2) {
		LOG_ERROR("malformed text_tox_id, missmatch in size, should be {} is {}", TOX_ADDRESS_SIZE*2, text_tox_id.size());
		return false;
	}

	uint8_t bin_rep[TOX_ADDRESS_SIZE];
	sodium_hex2bin(bin_rep, TOX_ADDRESS_SIZE, text_tox_id.data(), TOX_ADDRESS_SIZE*2/*(-1)*/, NULL, NULL, NULL);

	return add_friend(bin_rep, msg);
}

std::string ToxService::get_name(void) {
	std::string name(tox_self_get_name_size(_tox), '\0');
	tox_self_get_name(_tox, reinterpret_cast<uint8_t*>(name.data()));

	return name;
}

bool ToxService::set_name(std::string_view new_name) {
	TOX_ERR_SET_INFO err_sinf;
	tox_self_set_name(_tox, reinterpret_cast<const uint8_t*>(new_name.data()), new_name.size(), &err_sinf);
	if (err_sinf != TOX_ERR_SET_INFO_OK) {
		// TODO: better error log
		LOG_ERROR("tox_self_set_name failed with error code {}", err_sinf);
		return false;
	}

	return true;
}

bool ToxService::set_status(std::string_view new_status) {
	TOX_ERR_SET_INFO err_sinf;
	tox_self_set_status_message(_tox, reinterpret_cast<const uint8_t*>(new_status.data()), new_status.size(), &err_sinf);
	if (err_sinf != TOX_ERR_SET_INFO_OK) {
		// TODO: better error log
		LOG_ERROR("tox_self_set_status_message failed with error code {}", err_sinf);
		return false;
	}

	return true;
}

} // MM::Services::Tox

// ============ tox callback implementations ============

// logging
static void log_cb(Tox*, TOX_LOG_LEVEL level, const char* file, uint32_t line, const char* func, const char *message, void*) {
	//MM::Logger::log(file, line, "toxcore", message);
	spdlog::get("MM::Tox")->log(spdlog::source_loc{file, (int)line, func}, spdlog::level::level_enum(spdlog::level::trace + level), message);

}

// self
static void self_connection_status_cb(Tox*, TOX_CONNECTION connection_status, void*) {
	switch (connection_status) {
		case TOX_CONNECTION::TOX_CONNECTION_NONE:
			LOG_INFO("tox is not connected to the DHT!");
			break;
		case TOX_CONNECTION::TOX_CONNECTION_UDP:
			LOG_INFO("tox is connected to the DHT.");
			break;
		case TOX_CONNECTION::TOX_CONNECTION_TCP:
			LOG_INFO("tox is connected to the DHT, using a tcp relay.");
			break;
	}
}

// friend
static void friend_name_cb(Tox*, uint32_t friend_number, const uint8_t* name, size_t length, void* user_data) {
	LOGTOXCB("friend_name_cb");

	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& f = ts->_tox_friends[friend_number];
	f.name.resize(length);
	std::memcpy(f.name.data(), name, length);
}

static void friend_status_message_cb(Tox*, uint32_t friend_number, const uint8_t* message, size_t length, void* user_data) {
	LOGTOXCB("friend_status_message_cb");

	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& f = ts->_tox_friends[friend_number];
	f.status_msg.resize(length);
	std::memcpy(f.status_msg.data(), message, length);
}

static void friend_status_cb(Tox*, uint32_t friend_number, TOX_USER_STATUS status, void* user_data) {
	LOGTOXCB("friend_status_cb");

	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& f = ts->_tox_friends[friend_number];
	f.status = status;
}

static void friend_connection_status_cb(Tox*, uint32_t friend_number, TOX_CONNECTION connection_status, void* user_data) {
	LOGTOXCB("friend_connection_status_cb");

	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& f = ts->_tox_friends[friend_number];
	f.connection_status = connection_status;
	f.__dirty = true;
}

static void friend_typing_cb(Tox*, uint32_t friend_number, bool is_typing, void* user_data) {
	LOGTOXCB("friend_typing_cb");
	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& f = ts->_tox_friends[friend_number];
	f.typing = is_typing;
}

//static void friend_read_receipt_cb(Tox*, uint32_t friend_number, uint32_t message_id, void* user_data) {
static void friend_read_receipt_cb(Tox*, uint32_t, uint32_t, void*) {
	LOGTOXCB("friend_read_receipt_cb");

	//auto* ts = static_cast<MM::Tox::ToxService*>(user_data);
}

static void friend_request_cb(Tox *tox, const uint8_t *public_key, const uint8_t *message, size_t length, void *user_data) {
	LOG_INFO("got friend_request");
	// Accept the friend request:
	TOX_ERR_FRIEND_ADD err_friend_add;
	tox_friend_add_norequest(tox, public_key, &err_friend_add);
	if (err_friend_add != TOX_ERR_FRIEND_ADD_OK) {
		LOG_ERROR("unable to add friend: {}", err_friend_add);
	}
}

static void friend_message_cb(Tox*, uint32_t friend_number, TOX_MESSAGE_TYPE type, const uint8_t *message, size_t length, void *user_data) {
	LOG_INFO("got message from {}: '{}'", friend_number, std::string_view(reinterpret_cast<const char*>(message), length));

	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& f = ts->_tox_friends[friend_number];
	f.messages.emplace_back(false, type, std::string{reinterpret_cast<const char*>(message), length});
}

// file
//static void file_recv_control_cb(Tox *tox, uint32_t friend_number, uint32_t file_number, TOX_FILE_CONTROL control, void *user_data) {
static void file_recv_control_cb(Tox *, uint32_t, uint32_t, TOX_FILE_CONTROL, void *) {
	LOGTOXCB("file_recv_control_cb");
}

//static void file_chunk_request_cb(Tox *tox, uint32_t friend_number, uint32_t file_number, uint64_t position, size_t length, void *user_data) {
static void file_chunk_request_cb(Tox *, uint32_t, uint32_t, uint64_t, size_t, void *) {
	LOGTOXCB("file_chunk_request_cb");
}

//static void file_recv_cb(Tox *tox, uint32_t friend_number, uint32_t file_number, uint32_t kind, uint64_t file_size, const uint8_t *filename, size_t filename_length, void *user_data) {
static void file_recv_cb(Tox *, uint32_t, uint32_t, uint32_t, uint64_t, const uint8_t *, size_t, void *) {
	LOGTOXCB("file_recv_cb");
}

static void file_recv_chunk_cb(Tox *, uint32_t, uint32_t, uint64_t, const uint8_t *, size_t, void *) {
	LOGTOXCB("file_recv_chunk_cb");
}

// conference
static void conference_invite_cb(Tox *tox, uint32_t friend_number, TOX_CONFERENCE_TYPE type, const uint8_t *cookie, size_t length, void *user_data) {
	LOGTOXCB("conference_invite_cb");

	// NOTE: currently text only work ?
	TOX_ERR_CONFERENCE_JOIN err_conf_join;
	tox_conference_join(tox, friend_number, cookie, length, &err_conf_join);
	if (err_conf_join != TOX_ERR_CONFERENCE_JOIN::TOX_ERR_CONFERENCE_JOIN_OK) {
		LOG_ERROR("error joining conference: {}", err_conf_join);
	}
}

static void conference_connected_cb(Tox *tox, uint32_t conference_number, void *user_data) {
	LOGTOXCB("conference_connected_cb");
	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& g = ts->_tox_conferences[conference_number];
	Tox_Err_Conference_Get_Type err_conf_get_type;
	g.type = tox_conference_get_type(tox, conference_number, &err_conf_get_type);
	assert(err_conf_get_type == Tox_Err_Conference_Get_Type::TOX_ERR_CONFERENCE_GET_TYPE_OK);
}

static void conference_message_cb(Tox *, uint32_t conference_number, uint32_t peer_number, TOX_MESSAGE_TYPE type, const uint8_t *message, size_t length, void *user_data) {
	LOGTOXCB("conference_message_cb");
	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& g = ts->_tox_conferences[conference_number];

	g.messages.emplace_back(peer_number, type, std::string(reinterpret_cast<const char*>(message), length));
}

static void conference_title_cb(Tox *, uint32_t conference_number, uint32_t peer_number, const uint8_t *title, size_t length, void *user_data) {
	LOGTOXCB("conference_title_cb");
	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& g = ts->_tox_conferences[conference_number];

	(void)peer_number; // TODO: ??

	g.title = std::string(reinterpret_cast<const char*>(title), length);
}

static void conference_peer_name_cb(Tox *, uint32_t conference_number, uint32_t peer_number, const uint8_t *name, size_t length, void *user_data) {
	LOGTOXCB("conference_peer_name_cb");
	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);

	auto& g = ts->_tox_conferences[conference_number];
	g.peers[peer_number] = std::string(reinterpret_cast<const char*>(name), length);
}

static void conference_peer_list_changed_cb(Tox *tox, uint32_t conference_number, void *user_data) {
	LOGTOXCB("conference_peer_list_changed_cb");
}

// custom packets
static void friend_lossy_packet_cb(Tox*, uint32_t friend_number, const uint8_t *data, size_t length, void *user_data) {
	LOGTOXCB("friend_lossy_packet_cb");

	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);
	// TODO: use toxext
	//if (data[0] == MM_TOX_LOSSLESS_PKG_ID_INTERNAL) {
		//ts->_tox_friends[friend_number].packets_internal.emplace_back(data, data+length);
	//} else {
		ts->_tox_friends[friend_number].packets.emplace_back(data, data+length);
	//}
}

static void friend_lossless_packet_cb(Tox*, uint32_t friend_number, const uint8_t *data, size_t length, void *user_data) {
	LOGTOXCB("friend_lossless_packet_cb");

	auto* ts = static_cast<MM::Services::Tox::ToxService*>(user_data);
	if (data[0] == MM_TOX_LOSSLESS_PKG_ID_INTERNAL) {
		ts->_tox_friends[friend_number].packets_lossless_internal.emplace_back(data, data+length);
	} else {
		ts->_tox_friends[friend_number].packets_lossless.emplace_back(data, data+length);
	}
}

