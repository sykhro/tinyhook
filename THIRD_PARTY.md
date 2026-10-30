# Third-party software

TinyHook is Copyright (C) 2026 sykhro and licensed under GPL-3.0-only.
Dependencies retain their own copyrights and licenses.

The opt-in dependency provider uses the exact revisions in `dependencies.json`:

| Dependency | License | Used by |
| --- | --- | --- |
| [Luau](https://github.com/luau-lang/luau) | MIT | Native host and bytecode compiler |
| [MinHook](https://github.com/TsudaKageyu/minhook) | BSD-2-Clause, with bundled component notices | Windows hook DLL |
| [spdlog](https://github.com/gabime/spdlog) | MIT, with bundled fmt notices | Host and Windows helpers |

Runtime installations made with the dependency provider include each dependency's
license file alongside TinyHook's GPLv3 license under `share/licenses/tinyhook`.
See those files for the complete copyright, license, and bundled component notices.
