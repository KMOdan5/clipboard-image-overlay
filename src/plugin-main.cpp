/*
obs-clipboard-image
Copyright (C) 2026 KMOdan5 <dan5gaogao@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "clipboard-capture.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *HOTKEY_NAME = "obs_clipboard_image.show";
constexpr const char *SAVE_KEY = "obs-clipboard-image";
constexpr const char *SAVE_HOTKEY_KEY = "show_hotkey";

obs_hotkey_id show_hotkey_id = OBS_INVALID_HOTKEY_ID;

/*
 * Entry point for "show the clipboard image", shared by the Tools menu item
 * and the hotkey. Always runs on the UI thread, so later steps can use Qt
 * (QClipboard must be accessed from the UI thread).
 */
void on_show_requested(const char *trigger)
{
	obs_log(LOG_INFO, "show requested (trigger: %s)", trigger);

	const auto path = save_clipboard_image();
	if (!path)
		return;

	/* Showing the saved image on the scene comes in #5. */
}

void on_tools_menu_clicked(void *)
{
	on_show_requested("menu");
}

void on_hotkey_pressed(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;

	/* Hotkey callbacks run on the hotkey thread; hop to the UI thread. */
	obs_queue_task(OBS_TASK_UI, [](void *) { on_show_requested("hotkey"); }, nullptr, false);
}

/*
 * Frontend hotkeys are not saved by OBS automatically. Store the binding in
 * the scene collection data, the same way other frontend plugins do.
 */
void on_frontend_save(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		obs_data_t *obj = obs_data_create();
		obs_data_array_t *hotkey = obs_hotkey_save(show_hotkey_id);
		obs_data_set_array(obj, SAVE_HOTKEY_KEY, hotkey);
		obs_data_set_obj(save_data, SAVE_KEY, obj);
		obs_data_array_release(hotkey);
		obs_data_release(obj);
		return;
	}

	obs_data_t *obj = obs_data_get_obj(save_data, SAVE_KEY);
	if (!obj)
		return;

	obs_data_array_t *hotkey = obs_data_get_array(obj, SAVE_HOTKEY_KEY);
	obs_hotkey_load(show_hotkey_id, hotkey);
	obs_data_array_release(hotkey);
	obs_data_release(obj);
	obs_log(LOG_INFO, "hotkey binding loaded");
}

} // namespace

bool obs_module_load(void)
{
	obs_frontend_add_tools_menu_item(obs_module_text("Menu.ShowClipboardImage"), on_tools_menu_clicked, nullptr);

	show_hotkey_id = obs_hotkey_register_frontend(HOTKEY_NAME, obs_module_text("Hotkey.ShowClipboardImage"),
						      on_hotkey_pressed, nullptr);
	if (show_hotkey_id == OBS_INVALID_HOTKEY_ID)
		obs_log(LOG_WARNING, "failed to register hotkey");

	obs_frontend_add_save_callback(on_frontend_save, nullptr);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(on_frontend_save, nullptr);

	if (show_hotkey_id != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(show_hotkey_id);
		show_hotkey_id = OBS_INVALID_HOTKEY_ID;
	}

	obs_log(LOG_INFO, "plugin unloaded");
}
