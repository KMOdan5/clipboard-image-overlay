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

#pragma once

#include <optional>
#include <string>

/*
 * Reads an image from the system clipboard and saves it as a PNG file in the
 * plugin's image folder.
 *
 * Accepts either image data (browser "Copy image", Win+Shift+S) or a copied
 * local image file (Explorer). Must be called on the UI thread.
 *
 * Returns the UTF-8 path of the saved PNG, or std::nullopt if the clipboard
 * holds no usable image (the reason is written to the OBS log).
 */
std::optional<std::string> save_clipboard_image();
