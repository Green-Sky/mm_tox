#include "./tox_chat.hpp"

#include <entt/core/hashed_string.hpp>

#include <mm/services/imgui_menu_bar.hpp>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <mm_tox/imgui/widgets/tox.hpp>

#include <mm_tox/services/tox_service.hpp>

#include <mm/logger.hpp>
#define LOG_CRIT(...)		__LOG_CRIT(	"MM::Tox", __VA_ARGS__)
#define LOG_ERROR(...)		__LOG_ERROR("MM::Tox", __VA_ARGS__)
#define LOG_WARN(...)		__LOG_WARN(	"MM::Tox", __VA_ARGS__)
#define LOG_INFO(...)		__LOG_INFO(	"MM::Tox", __VA_ARGS__)
#define LOG_DEBUG(...)		__LOG_DEBUG("MM::Tox", __VA_ARGS__)
#define LOG_TRACE(...)		__LOG_TRACE("MM::Tox", __VA_ARGS__)

// https://www.youtube.com/watch?v=pyn6wPKxCmA

namespace MM::Tox::Services {

bool ToxChat::enable(Engine& engine, std::vector<UpdateStrategies::TaskInfo>& task_array) {
	if (!engine.tryService<ToxService>()) {
		LOG_ERROR("[ToxChat] ToxService is not in engine");
		return false;
	}

	if (!engine.tryService<MM::Services::ImGuiMenuBar>()) {
		LOG_ERROR("[ToxChat] ImGuiMenuBar is not in engine");
		return false;
	}

	// add task
	task_array.push_back(
		UpdateStrategies::TaskInfo{"ToxChat::render_imgui"}
		.fn([this](Engine& e){ renderImGui(e); })
		.succeed("ToxService::iterate")
		.succeed("ImGuiMenuBar::render")
	);

	auto& mb = engine.getService<MM::Services::ImGuiMenuBar>();
	mb.menu_tree["Tox"]["Settings"] = [this](Engine&) {
		ImGui::MenuItem("Settings", NULL, &_show_settings);
	};
	mb.menu_tree["Tox"]["Friends"] = [this](Engine&) {
		ImGui::MenuItem("Friends", NULL, &_show_friends);
	};
	mb.menu_tree["Tox"]["Chats"] = [this](Engine&) {
		ImGui::MenuItem("Chats", NULL, &_show_chats);
	};

	return true;
}

void ToxChat::disable(Engine& engine) {
	auto& mb = engine.getService<MM::Services::ImGuiMenuBar>();
	mb.menu_tree["Tox"].erase("Settings");
	mb.menu_tree["Tox"].erase("Friends");
	mb.menu_tree["Tox"].erase("Chats");
	if (mb.menu_tree["Tox"].empty()) {
		mb.menu_tree.erase("Tox");
	}
}

void ToxChat::focusChat(uint32_t id, bool conference, bool group) {
	if (conference) {
		_active_chats_c.emplace(id); // ensure its open
	} else if (group) {
		_active_chats_g.emplace(id); // ensure its open
	} else {
		_active_chats_f.emplace(id); // ensure its open
	}

	_active_chat.id = id;
	_active_chat.conference = conference;
	_active_chat.group = group;
	_active_chat.active = true;

	_show_chats = true; // TODO: ok ?
}

void ToxChat::renderFriendGroupList(Engine& engine) {
	auto& ts = engine.getService<ToxService>();

	if (ImGui::BeginTable("Friendtable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
		ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 10.f); // or avatar?
		ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, 10.f);
		ImGui::TableSetupColumn("connection", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("name");
		ImGui::TableHeadersRow();

		size_t table_id = 0;
		for (auto& ge : ts._tox_groups) {
			ImGui::TableNextRow();
			ImGui::PushID(table_id++);

			ImGui::TableNextColumn();
			if (ImGui::Selectable("g##sel", false, ImGuiSelectableFlags_SpanAllColumns)) {
				focusChat(ge.first, false, true);
			}

			ImGui::TableNextColumn();
			ImGui::Text("%d", ge.first);

			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%s", ge.second.name.c_str());

			ImGui::PopID();
		} // groups

		for (auto& ce : ts._tox_conferences) {
			ImGui::TableNextRow();
			ImGui::PushID(table_id++);

			ImGui::TableNextColumn();
			if (ImGui::Selectable("c##sel", false, ImGuiSelectableFlags_SpanAllColumns)) {
				focusChat(ce.first, true);
			}

			ImGui::TableNextColumn();
			ImGui::Text("%d", ce.first);

			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%s", ce.second.title.c_str());
			//ImGui::Text("[g %d] %s", ge.first, ge.second.title.c_str());

			//std::string g_context_str {"group_context##"};
			//g_context_str += std::to_string(ge.first);
			//if (ImGui::BeginPopupContextItem(g_context_str.c_str())) {
				//if (ImGui::Button("open chat")) {
					//_active_chats_g.emplace(ge.first);
				//}

				//ImGui::EndPopup();
			//}
			ImGui::PopID();
		} // conferences

		for (auto& fe : ts._tox_friends) {
			ImGui::TableNextRow();
			ImGui::PushID(table_id++);

			if (fe.second.connection_status != Tox_Connection::TOX_CONNECTION_NONE) {
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(70, 255, 50, 50));
			}

			ImGui::TableNextColumn();
			if (ImGui::Selectable("f##sel", false, ImGuiSelectableFlags_SpanAllColumns)) {
				focusChat(fe.first, false);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();

				ImGui::Text("Status: %s", fe.second.status_msg.c_str());
				if (fe.second.mm_instance) {
					ImGui::Text("[MM]"); ImGui::SameLine();
					ImGui::Text("[%s]", fe.second.mm_app.c_str());
				}

				ImGui::EndTooltip();
			}

			ImGui::TableNextColumn();
			ImGui::Text("%d", fe.first);

			ImGui::TableNextColumn();
			ImGui::Text("%s",
				fe.second.connection_status == Tox_Connection::TOX_CONNECTION_NONE ? "Offline" :
				fe.second.connection_status == Tox_Connection::TOX_CONNECTION_UDP ? "UDP-Direct" : "TCP-Relay"
			);

			ImGui::TableNextColumn();
			ImGui::Text("%s", fe.second.name.c_str());

			//std::string f_context_str {"friend_context##"};
			//f_context_str += std::to_string(fe.first);
			//if (ImGui::BeginPopupContextItem(f_context_str.c_str())) {
				//if (ImGui::Button("open chat")) {
					//_active_chats_f.emplace(fe.first);
				//}

				//ImGui::Separator();

				//for (auto& ge : ts._tox_groups) {
					//std::string inv_label {"invite to '"};
					//inv_label += ge.second.title;
					//inv_label += "'##";
					//inv_label += std::to_string(ge.first);

					//if (ImGui::Button(inv_label.c_str())) {
						//Tox_Err_Conference_Invite err_conf_inv;
						//// TODO: abstract
						//tox_conference_invite(ts._tox, fe.first, ge.first, &err_conf_inv);
					//}
				//}
				//ImGui::EndPopup();
			//}
			ImGui::PopID();
		} // friends

		ImGui::EndTable();
	}
}

void ToxChat::renderFriends(Engine& engine) {
	if (ImGui::Begin("ToxFriends", &_show_friends)) {
		auto& ts = engine.getService<ToxService>();

		if (ImGui::BeginTabBar("friends##tabs")) {
			if (ImGui::BeginTabItem("Friend List")) {
				ImGui::Text("conferences: %lu", (unsigned long)ts._tox_conferences.size());
				ImGui::SameLine();
				ImGui::Text("friends: %lu", (unsigned long)ts._tox_friends.size());
				ImGui::Separator();

				renderFriendGroupList(engine);

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Add Friend")) {
				// TODO: this is bad
				MM::ImGuiWidgets::Tox::AddFriend(engine, "hi");
				//ImGui::SameLine();
				const auto& tox_id_str = ts.get_own_tox_id_string();

				if (ImGui::Button("Copy own id to clipboard")) {
					ImGui::SetClipboardText(tox_id_str.c_str());
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Friend/Group Requests")) {
				ImGui::Text("TODO: welp");
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void ToxChat::renderChats(Engine& engine) {
	if (ImGui::Begin("ToxChats", &_show_chats)) {
		auto& ts = engine.getService<ToxService>();
		static bool follow = true;

		if (ImGui::BeginTabBar("tox_chats##tabs")) {
			// first friends
			for (uint32_t f_num : _active_chats_f) {
				bool set_selected = false;
				if (_active_chat.active && !_active_chat.group && _active_chat.id == f_num) {
					set_selected = true;
					_active_chat.active = false;
				}

				std::string tab_title{ts._tox_friends[f_num].name};
				tab_title += "##";
				tab_title += std::to_string(f_num);
				if (ImGui::BeginTabItem(tab_title.c_str(), NULL,
					ImGuiTabItemFlags_None
					| (set_selected ? ImGuiTabItemFlags_SetSelected : 0)
				)) {
					ImGui::BeginChild("##scrollingregion", ImVec2(0, -23));

					for (size_t i = 0; i < ts._tox_friends[f_num].messages.size(); i++) {
						auto& msg_ent = ts._tox_friends[f_num].messages[i];
						if (std::get<Tox_Message_Type>(msg_ent) == Tox_Message_Type::TOX_MESSAGE_TYPE_NORMAL) {
							ImGui::Text("[%s]: %s", std::get<0>(msg_ent) ? "me" : ts._tox_friends[f_num].name.c_str(), std::get<2>(msg_ent).c_str());
							if (follow && i == ts._tox_friends[f_num].messages.size()-1) {
								ImGui::SetScrollHereY(1.f);
							}
						}
					}

					ImGui::EndChild();

					static std::string msg;
					bool hit_enter = ImGui::InputTextWithHint("##label", "type your message here...", &msg, ImGuiInputTextFlags_EnterReturnsTrue);
					if (hit_enter) {
						ImGui::SetKeyboardFocusHere(-1);
					}
					ImGui::SameLine();
					if ((ImGui::Button("send") || hit_enter) && !msg.empty()) {
						bool r = ts.friend_send_message(f_num, msg);
						//assert(r);
						msg.clear();
					}

					ImGui::SameLine();
					ImGui::Checkbox("follow", &follow);

					ImGui::EndTabItem();
				}
			}

			// then conferences
			for (uint32_t c_num : _active_chats_c) {
				//std::string tab_title{ts._tox_groups[g_num].title};
				std::string tab_title{ts._tox_conferences[c_num].title};
				tab_title += "##";
				tab_title += std::to_string(c_num);

				if (ImGui::BeginTabItem(tab_title.c_str())) {
					ImGui::BeginChild("##scrollingregion", ImVec2(0, -23));

					for (auto& msg_ent : ts._tox_conferences[c_num].messages) {
						if (std::get<Tox_Message_Type>(msg_ent) == Tox_Message_Type::TOX_MESSAGE_TYPE_NORMAL) {
							//ImGui::Text("[%s]: %s", ts._tox_groups[g_num].peers[std::get<0>(msg_ent)].c_str(), std::get<std::string>(msg_ent).c_str());
							ImGui::Text("[%s]: %s", ts._tox_conferences[c_num].peers[std::get<0>(msg_ent)].c_str(), std::get<std::string>(msg_ent).c_str());
						}
					}

					ImGui::EndChild();

					static std::string msg;
					bool hit_enter = ImGui::InputTextWithHint("##label", "type your message here...", &msg, ImGuiInputTextFlags_EnterReturnsTrue);
					if (hit_enter) {
						ImGui::SetKeyboardFocusHere(-1);
					}
					ImGui::SameLine();
					if ((ImGui::Button("send") || hit_enter) && !msg.empty()) {
						bool r = ts.conference_send_message(c_num, msg);
						//assert(r);
						msg.clear();
					}

					ImGui::EndTabItem();
				}
			}

			// groups
			for (uint32_t g_num : _active_chats_g) {
				const auto& g = ts._tox_groups[g_num];
				std::string tab_title{g.name};
				tab_title += "##";
				tab_title += std::to_string(g_num);

				if (ImGui::BeginTabItem(tab_title.c_str())) {
					ImGui::TextUnformatted(g.topic.c_str());
					ImGui::Separator();
					ImGui::BeginChild("##scrollingregion", ImVec2(0, -23));

					for (const auto& msg_ent : g.messages) {
						if (std::get<Tox_Message_Type>(msg_ent) == Tox_Message_Type::TOX_MESSAGE_TYPE_NORMAL) {
							ImGui::Text("[%s]: %s", g.peers.at(std::get<0>(msg_ent)).name.c_str(), std::get<std::string>(msg_ent).c_str());
						}
					}

					ImGui::EndChild();

					static std::string msg;
					bool hit_enter = ImGui::InputTextWithHint("##label", "type your message here...", &msg, ImGuiInputTextFlags_EnterReturnsTrue);
					if (hit_enter) {
						ImGui::SetKeyboardFocusHere(-1);
					}
					ImGui::SameLine();
					if ((ImGui::Button("send") || hit_enter) && !msg.empty()) {
						bool r = ts.group_send_message(g_num, msg);
						//assert(r);
						msg.clear();
					}

					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void ToxChat::renderSettings(Engine& engine) {
	if (ImGui::Begin("ToxSettings", &_show_settings)) {
		auto& ts = engine.getService<ToxService>();

		{
			static std::string tmp_name = ts.get_name();
			ImGui::InputText("My Name", &tmp_name);
			if (ImGui::Button("change name")) {
				ts.set_name(tmp_name);
			}
		}
		ImGui::Separator();
	}
	ImGui::End();
}

void ToxChat::renderImGui(Engine& engine) {
	if (_show_friends) {
		renderFriends(engine);
	}

	if (_show_chats) {
		renderChats(engine);
	}

	if (_show_settings) {
		renderSettings(engine);
	}
}

} // MM::Services::Tox

