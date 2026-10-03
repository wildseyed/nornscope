local mod = require 'core/mods'

local this_name = mod.this_name

-- keep streaming even in menu mode, where the script refresh hook is silent
-- (metros are freed on script reload, so this is (re)started in script_post_init too)
local function start_stream_metro()
  if ndi_stream_metro then pcall(function() ndi_stream_metro:stop() end) end
  ndi_stream_metro = metro.init(function() ndi_mod.update() end, 1/15)
  ndi_stream_metro:start()
end

-- notify nornscope of physical key/encoder activity over the ws REPL bus:
-- the desktop app listens for lines tagged "NSQ " and lights its UI controls.
-- nornscope's own injections set nornscope_remote first, so they don't echo.
local function install_input_hooks()
  if _norns.nornscope_hooked then return end
  _norns.nornscope_hooked = true
  local orig_key, orig_enc = _norns.key, _norns.enc
  _norns.key = function(n, z)
    if not nornscope_remote then print("NSQ key "..n.." "..z) end
    orig_key(n, z)
  end
  _norns.enc = function(n, d)
    if not nornscope_remote then print("NSQ enc "..n.." "..d) end
    orig_enc(n, d)
  end
end

mod.hook.register("system_post_startup", "ndi-system-post-startup", function()
  package.cpath = package.cpath .. ";" .. paths.code .. this_name .. "/lib/?.so"
  ndi_mod = require 'ndi_mod'

  ndi_mod.init()
  ndi_mod.start()
  start_stream_metro()
  install_input_hooks()
end)

mod.hook.register("system_pre_shutdown", "ndi-system-pre-shutdown", function()
  ndi_mod.cleanup()
end)

mod.hook.register("script_post_init", "ndi-script-post-init", function()
  start_stream_metro()
  local script_refresh_fn = norns.script.refresh
  norns.script.refresh = function()
    ndi_mod.update()
    script_refresh_fn()
  end
end)
