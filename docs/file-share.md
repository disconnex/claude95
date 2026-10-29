# A network share Windows 95 can use

The easiest way to get `CLAUDE95.EXE` (and anything else) onto a Windows 95 PC
is a Samba share on the same machine as the gateway. Windows 95 only speaks
the old SMB1 protocol with LANMAN passwords, so Samba needs a few settings
that modern guides leave out.

## Install Samba (Debian, Raspberry Pi OS, Ubuntu)

```bash
sudo apt install samba
sudo mkdir -p /srv/win95 && sudo chown "$USER": /srv/win95
```

## /etc/samba/smb.conf

Replace `192.168.1.50` with the Windows 95 PC's address and `youruser` with
your Linux user name.

```ini
[global]
   workgroup = WORKGROUP
   netbios name = GATEWAY
   server role = standalone server
   # Only the Windows 95 PC (and this machine) may connect.
   hosts allow = 127.0.0.1 192.168.1.50
   hosts deny = ALL
   # Windows 95 needs SMB1 and LANMAN passwords. Newer clients still use SMB2/3.
   server min protocol = NT1
   lanman auth = yes
   ntlm auth = yes
   map to guest = never
   # Windows 95 logs on with its own Windows user name; map it to yours.
   username map = /etc/samba/usermap
   load printers = no
   printcap name = /dev/null
   disable spoolss = yes

[WIN95]
   path = /srv/win95
   valid users = youruser
   force user = youruser
   read only = no
```

`/etc/samba/usermap` maps the name you log on to Windows 95 with (find it with
`net config` in an MS-DOS Prompt) to your Linux user:

```
youruser = windowsname
```

Then set a Samba password and restart. Set the password **after** turning on
`lanman auth`, keep it to **14 characters or fewer** (Windows 95 upper-cases
and truncates it), and ideally make it the same as your Windows 95 logon
password so the PC connects without asking:

```bash
sudo smbpasswd -a youruser
sudo systemctl restart smbd nmbd
```

## On Windows 95

Start > Run > `\\GATEWAY\WIN95`. To keep it as a drive letter: Windows
Explorer > Tools > Map Network Drive, path `\\GATEWAY\WIN95`, and tick
**Reconnect at logon**.

## Security

SMB1 and LANMAN hashes are weak by today's standards. Keep `hosts allow` tight,
keep the share on your local network, and don't reuse an important password.
