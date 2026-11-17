#pragma once

#include <filesystem>

// Where the app keeps its files. None of the folders may exist yet:
// whatever writes a file into one creates it. All throw
// std::runtime_error if the variables they're built from aren't set.

// For small files that belong to the user, e.g. settings.json:
//   Windows: %APPDATA%\Kodama
//   Linux:   $XDG_CONFIG_HOME/Kodama, or ~/.config/Kodama
std::filesystem::path config_dir();

// For what the app remembers between runs that isn't a setting, e.g. the
// search history:
//   Windows: %APPDATA%\Kodama, the same as config_dir()
//   Linux:   $XDG_STATE_HOME/Kodama, or ~/.local/state/Kodama
std::filesystem::path state_dir();

// For large files the app can download again, e.g. whisper models:
//   Windows: %LOCALAPPDATA%\Kodama
//   Linux:   $XDG_DATA_HOME/Kodama, or ~/.local/share/Kodama
std::filesystem::path data_dir();

// The folder the program itself is in, for what's shipped next to it, e.g.
// the dictionary. Throws std::runtime_error if the system won't say.
std::filesystem::path program_dir();
