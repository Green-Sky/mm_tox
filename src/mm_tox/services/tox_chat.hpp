#pragma once

#include <mm/engine.hpp>

#include <set>

namespace MM::Tox::Services {

// requires a ToxSerice to be enabled
class ToxChat : public MM::Services::Service {
	public:
		bool _show_friends = true;
		bool _show_chats = false;
		bool _show_settings = false;

		std::set<uint32_t> _active_chats_f;
		std::set<uint32_t> _active_chats_c;

		struct {
			bool active = false;
			bool conference = false;
			bool group = false;
			uint32_t id = 0;
		} _active_chat;

	public:
		const char* name(void) override { return "ToxChat"; }

		bool enable(Engine& engine, std::vector<UpdateStrategies::TaskInfo>& task_array) override;
		void disable(Engine& engine) override;

	protected:
		void focusChat(uint32_t id, bool conference = false, bool group = false);

		void renderFriendGroupList(Engine& engine);
		void renderFriends(Engine& engine);

		void renderChats(Engine& engine);

		void renderSettings(Engine& engine);

		void renderImGui(Engine& engine);
};

} // MM::Tox::Services

