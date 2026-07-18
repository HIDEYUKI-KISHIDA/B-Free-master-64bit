# Wayland Runtime Regression Checklist

Use this checklist after changing `gui_server/wayland_server_runtime.c`.

## 1. Bootstrap
- [ ] Start `tron_gui_server`
- [ ] Connect a Qt Wayland client
- [ ] Confirm `wl_registry.bind` sequence completes without unknown-object errors

## 2. Window lifecycle
- [ ] Window appears
- [ ] `xdg_surface.configure` / `ack_configure` serials progress
- [ ] `xdg_toplevel` state changes (maximize/fullscreen/minimize) do not hang

## 3. Input
- [ ] Pointer move works
- [ ] Pointer button click works
- [ ] Wheel vertical/horizontal works
- [ ] Keyboard typing works for ASCII
- [ ] Arrow/F-key/Delete/Home/End/PageUp/PageDown are recognized

## 4. Clipboard / Data device
- [ ] `wl_data_device.set_selection` emits `data_offer` + `selection`
- [ ] `wl_data_offer.receive` writes payload to provided fd
- [ ] Paste `text/plain;charset=utf-8` works in at least one Qt client

## 5. Popup / Subsurface
- [ ] Popup opens and closes (`xdg_popup.configure`/destroy)
- [ ] Subsurface APIs are accepted (`set_position`, `set_sync`, `set_desync`)
- [ ] No regressions in normal surface commit path

## 6. Runtime counters
- [ ] `BFREE_WL_TRACE_REQ=1` trace output looks sane
- [ ] Cleanup summary shows low/expected `unknown_req`
