#pragma once
#ifndef CONFIG_DIALOG_H
#define CONFIG_DIALOG_H

#include <windows.h>
#include "../wfx/plugin_state.h"

// Функції для виклику діалогових вікон
bool ShowConfigDialog(HWND parent, PluginConfig& cfg);
bool ShowMountDialog(HWND parent, PluginConfig& cfg);

#endif // CONFIG_DIALOG_H