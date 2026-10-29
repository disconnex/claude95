# Getting Windows 95 ready

Claude 95 needs TCP/IP networking working on the Windows 95 PC. If the PC can
already reach other machines on your network, you're done.

## 1. A network card and its driver

Any card with a Windows 95 driver works: 3Com 3C509/3C905, Intel EtherExpress,
NE2000 clones and many more. Old 10Mbit cards are fine; Claude 95 sends very
little data. Check Control Panel > System > Device Manager > Network adapters
for the card without a yellow warning mark.

## 2. TCP/IP

Control Panel > Network. You need:

- **TCP/IP** bound to your network card. If it's missing: Add > Protocol >
  Microsoft > TCP/IP (you may need the Windows 95 CD).
- **Client for Microsoft Networks**, if you want to use a network share to
  copy files over (optional).

In TCP/IP > Properties, either pick "Obtain an IP address automatically"
(DHCP from your router) or set a fixed address. A fixed address, or a DHCP
reservation on your router, is best, because the gateway's `C95_ALLOW` setting
needs to know the PC's address.

Restart when Windows asks.

## 3. Find the PC's address

Start > Run > `winipcfg`. The IP Address shown (for example `192.168.1.50`) is
what goes in the gateway's `C95_ALLOW`.

## 4. Check you can reach the gateway

From an MS-DOS Prompt: `ping 192.168.1.10` (the gateway's address). If replies
come back, Claude 95 will be able to connect.

## Running it

Copy `CLAUDE95.EXE` anywhere (for example `C:\Program Files\Claude95`), make a
shortcut, and run it. It keeps its settings in `CLAUDE95.INI` in the same
folder, so the folder must be writable.
