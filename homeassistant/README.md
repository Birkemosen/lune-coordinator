# Lune in Home Assistant

## Notifications

Lune V6 and Lune Touch report problems to Home Assistant; Home Assistant sends
them to phones. The devices only report. Who gets what is decided in one place:
the **Lune notifications** blueprint.

Contract: `lune/shared/contracts/lune_notifications_contract_v1.md`.

### 1. Let each Lune device fire events

Do this on **every** Lune V6 and Lune Touch:

1. Settings › Devices & services › ESPHome.
2. Pick the device › **Configure** (cog).
3. Turn on **Allow the device to perform Home Assistant actions** › Submit.

Without it Home Assistant drops the device's events. The device keeps working
and still shows its problems in the entities **Active problems** (count) and
**Problems** (titles of the active problems).

### 2. Import the blueprint

[![Open your Home Assistant instance and show the blueprint import dialog with a specific blueprint pre-filled.](https://my.home-assistant.io/badges/blueprint_import.svg)](https://my.home-assistant.io/redirect/blueprint_import/?blueprint_url=https%3A%2F%2Fgithub.com%2FBirkemosen%2Flune-coordinator%2Fblob%2Fmain%2Fhomeassistant%2Fblueprints%2Fautomation%2Flune%2Flune_notifications.yaml)

Or: Settings › Automations & scenes › Blueprints › **Import blueprint**, and paste

```text
https://github.com/Birkemosen/lune-coordinator/blob/main/homeassistant/blueprints/automation/lune/lune_notifications.yaml
```

Then **Create automation** from it. Several automations from the same
blueprint are fine (for example errors for everyone, warnings for one person).

### Inputs

| Input | Default | What it does |
|---|---|---|
| Phones | — | One or more phones with the Companion app. Sent to `notify.mobile_app_<device name>`; if you renamed the phone in the app after it was registered, check that this action exists. |
| Categories | all | `heat_pump`, `hydraulics`, `connectivity`, `room_comfort`, `humidity`, `sensor_battery`, `sensor`, `valve`. |
| Minimum severity | Warnings and errors | Warning = attention soon; error = fault now. |
| Errors as critical | off | Errors break through Do Not Disturb (iOS critical alert, Android alarm stream). |
| Quiet hours for warnings | off, 22:00–07:00 | No warnings in this window; errors still come. |
| Resolved prefix | `Løst:` | Put before the title when a problem clears. |

### Behaviour

- **Raised**: one notification with the device's title and message (in the
  device's language), `tag` = the problem id, `group` = `lune-<device>`.
  Errors are time-sensitive; warnings are normal.
- **Cleared**: the notification with the same tag is **replaced** silently
  (iOS passive, Android low priority) by "Løst: <title>". We replace rather
  than remove, so a problem that came and went while you were away is still
  visible once.
- A device raises the same problem at most once an hour and only after it has
  held for a while (10 min unless the contract says otherwise), so blips do
  not notify. A clear is sent only for a raise that was sent.
- To stop notifications: turn the automation off, or remove a category or a
  phone from it. Nothing has to change on the devices.
