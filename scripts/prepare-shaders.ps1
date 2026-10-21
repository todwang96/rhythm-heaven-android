# Translate the game shaders into the Vulkan SPIR-V cache.
#
# Needs the Touchable shader pipeline first:
#   1. Touchable/internal/tools/extract_shaders.py   (shader_NX.bnsh -> capture/*.control + *.code)
#   2. dmsh_xlate (d2r-mobile/dm/gfx/tools)          (Maxwell -> SPIR-V; needs .NET 10 SDK)
#   3. tools/make_vk_shader_cache.py                 (capture + spv -> work/shaders/vk-cache)
#
# See docs/PREPARE-GAME.md. This wrapper runs step 3 when its inputs already exist.
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    python (Join-Path $repo "tools\make_vk_shader_cache.py")
    Write-Host "shader cache -> work/shaders/vk-cache" -ForegroundColor Green
} finally { Pop-Location }
