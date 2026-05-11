# Flashing this firmware from WSL2

WSL2 doesn't see USB devices natively, so the ESP32-S3 dev board has to be forwarded from Windows over USB/IP. Follow the steps below; the official Espressif walkthrough is [Working with Espressif's SoCs using WSL2](https://developer.espressif.com/blog/espressif-devkits-with-wsl2/) and this file is essentially the trashboy-specific version of that.

## One-time setup

### On Windows

Install [usbipd-win](https://github.com/dorssel/usbipd-win):

```powershell
winget install usbipd
```

(or grab the `.msi` from the GitHub releases). Restart the terminal afterwards.

### In WSL2

The ESP32-S3 enumerates as a built-in USB JTAG/serial device (`303a:1001`) — *not* a CP210x or FTDI chip — so it needs the **`cdc_acm`** kernel module, not `cp210x`. (The Espressif blog tells you to load `cp210x` because most older devkits had a CP210x USB-to-serial chip; ours doesn't.)

If the WSL2 kernel doesn't autoload it (it usually doesn't), the device will show in `lsusb` but no `/dev/ttyACM*` node will appear. To make autoload persistent across reboots:

```bash
echo cdc_acm | sudo tee /etc/modules-load.d/cdc_acm.conf
```

Without that, you'll need to `sudo modprobe cdc_acm` once per WSL session (after `wsl --shutdown` or a Windows reboot).

## Every time you plug the device in

In a Windows **PowerShell (admin)** prompt:

```powershell
usbipd list                          # find the BUSID, e.g. 1-2
usbipd bind   --busid 1-2            # one-time per device; needs admin
usbipd attach --wsl --busid 1-2      # WSL2 must already be running
```

In WSL2:

```bash
sudo modprobe cdc_acm                # only if /etc/modules-load.d isn't set up
ls /dev/ttyACM*                      # should show /dev/ttyACM0
sudo chown sascha:sascha /dev/ttyACM0
```

Notes:
- `usbipd attach` has to be re-run after every replug **and** every `wsl --shutdown`.
- The `chown` is needed because WSL2's udev doesn't assign group permissions automatically. (Using `chown sascha:sascha` rather than `chown root:dialout` — the latter looks more standard but isn't what makes the port usable here.)
- If `lsmod | grep cdc_acm` is empty after `modprobe`, you typo'd it as `cdc-acm` (the file is `cdc-acm.ko` but the module name is `cdc_acm`), or the kernel doesn't ship it — check `find /lib/modules/$(uname -r) -name 'cdc-acm*'`.

## Detaching

When done, free the device on the Windows side:

```powershell
usbipd detach --busid 1-2
```

This isn't strictly required — closing WSL or rebooting also detaches — but doing it explicitly lets you use the COM port from Windows again without juggling.

## Build and flash

Once `/dev/ttyACM0` is visible and chowned:

```bash
. ~/esp/esp-idf/export.sh            # activates ESP-IDF v5.5
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with **Ctrl-]**. If `idf.py flash` complains the port is busy, a monitor session is still holding it — close that first (`fuser /dev/ttyACM0` will show the PID).

## Quick reference

| Step          | Where     | Command                                |
| ------------- | --------- | -------------------------------------- |
| List devices  | Windows   | `usbipd list`                          |
| Bind (once)   | Windows*  | `usbipd bind --busid 1-2`              |
| Attach        | Windows   | `usbipd attach --wsl --busid 1-2`      |
| Load driver   | WSL2      | `sudo modprobe cdc_acm`                |
| Fix perms     | WSL2      | `sudo chown sascha:sascha /dev/ttyACM0`|
| Flash + log   | WSL2      | `idf.py -p /dev/ttyACM0 flash monitor` |
| Detach        | Windows   | `usbipd detach --busid 1-2`            |

\* needs admin PowerShell.

## Project context

- ESP-IDF version pinned: **v5.5.2** (target: **ESP32-S3**).
- The board exposes its USB JTAG directly; no separate USB-to-serial bridge chip is involved.
