# Status (2026-09-29, paused for budget)

All four features are implemented as local commits on `main` (not pushed).

Verified:
- `scons platform=linuxbsd target=editor module_mono_enabled=yes use_llvm=yes tests=yes use_static_cpp=no -j6`
  builds (clang; `use_static_cpp=no` because static libstdc++ is not installed on this host).
- Doctests pass: `bin/godot.linuxbsd.editor.x86_64.llvm.mono --headless --test
  --test-case="*DisplayServer*,*ControlChannel*,*AudioDriverDummy*,*Seeded*,*Frame time*"` (6 cases, 635 assertions).
- pre-commit hooks pass over `ed1daf0bf0..HEAD`.

Not yet verified (next step, in order):
1. Mono glue/assemblies are not generated, so the mono editor pops a .NET alert (zenity) and the
   e2e suite's first game run was killed. Either build a non-mono editor for the e2e suite, or run
   `bin/godot... --headless --generate-mono-glue modules/mono/glue` and
   `modules/mono/build_scripts/build_assemblies.py --godot-output-dir=bin`.
2. `cd tests/headless && uv run --locked pytest` (offscreen Vulkan/GL, control channel, audio frame
   clock, deterministic runs), then fix failures as fixups to the owning commit.
3. Push `main` to origin once the suite passes; then build mono export templates.
