#include "./tox.hpp"

#include <mm_tox/services/tox_service.hpp>

#include <imgui/imgui.h>

namespace MM::ImGuiWidgets::Tox {

void AddFriend(MM::Engine& engine, std::string_view message) {
	static char tox_id[TOX_ADDRESS_SIZE*2+1] = {};
	ImGui::InputText("Tox ID", tox_id, TOX_ADDRESS_SIZE*2+1);

	static bool r = true;
	if (ImGui::Button("add friend")) {
		auto& ts = engine.getService<MM::Tox::Services::ToxService>();
		r = ts.add_friend(std::string_view(tox_id, TOX_ADDRESS_SIZE*2), message);
	}
	//if (err_f_add != TOX_ERR_FRIEND_ADD_OK) {
	if (!r) {
		ImGui::SameLine();
		ImGui::Text("error adding friend");
	}
}

} // MM::ImGuiWidgets::Tox

