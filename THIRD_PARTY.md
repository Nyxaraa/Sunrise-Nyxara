# Third-party software

Sunrise compiles the following reviewed upstream source dependencies into its single DLL:

- Microsoft Detours 4.0.1. The reviewed source and license are retained under
  `Sunrise/vendor/detours`.
- Dear ImGui 1.92.6. The upstream MIT notice is retained at
  `Sunrise/vendor/imgui/LICENSE.txt` and embedded in the DLL as a resource.
- Lua 5.4.8. The interpreter library sources only; the standalone tools are omitted. The
  upstream MIT notice is retained at `Sunrise/vendor/lua/LICENSE` and embedded in the DLL as a
  resource.

Project-owned Sunrise source follows the project coding rules. Vendored upstream source is kept
isolated and is not rewritten by the project formatter.
