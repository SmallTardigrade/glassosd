# After logging back in

Run these in order. Each one answers a specific open question.

## 1. Alt+Tab

Just press it. Working = the nested-compositor damage is gone for good.

## 2. Did the blur switch take?

```bash
systemctl --user restart glassosd
journalctl --user -u glassosd -b | grep -i blur
```

- **No output** → blur is available. Real glass now works.
- **"the compositor offers no background blur"** → still not available, so
  stock KWin blur is not providing the capability either. Revert with the
  commands in section 5 and we look at it differently.

## 3. If blur works, try the glass

```bash
glassosdctl theme-file liquid-glass
notify-send "Glass" "is the text behind this smeared, not sharp?"
```

Smeared = working. Sharp = translucent but unblurred; tell me.

Back to the look you liked:

```bash
glassosdctl theme-file none
```

## 4. Proton

Quit it properly — tray icon → Quit, not just closing the window — then
reopen and send yourself an email.

```bash
./tools/verify-routing.sh me.proton.Mail
```

The "currently running since" line should now be today. If a notification
reaches the bus, it appears in the watch section.

## 5. Reverting the blur change

```bash
kwriteconfig6 --file kwinrc --group Plugins --key better_blur_dxEnabled true
kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled false
```

Then log out and back in again — a runtime reconfigure does not
re-advertise the protocol capability, which is why this needs a session
restart at all.

## 6. Your wallpaper

Still the Fedora default, because I overwrote it and my backup was empty.
Right-click the desktop → Configure Desktop and Wallpaper.

## 7. Quick health check

```bash
glassosdctl status
./tools/input-audit.sh
```
