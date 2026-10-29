#!/bin/bash
# Installs the claude95 gateway as a systemd service on Linux (Raspberry Pi OS,
# Debian, Ubuntu...). Rerunnable; run with sudo from this folder.
# It starts once a real API key and C95_ALLOW are in /etc/claude95/claude95.env.
set -euo pipefail
cd "$(dirname "$0")"
ENV=/etc/claude95/claude95.env
id claude95 >/dev/null 2>&1 || useradd --system --home-dir /var/lib/claude95 --shell /usr/sbin/nologin claude95
install -d -m 755 /opt/claude95 /etc/claude95
[ -x /opt/claude95/venv/bin/python ] || python3 -m venv /opt/claude95/venv
/opt/claude95/venv/bin/pip install --quiet --upgrade -r requirements.txt
install -m 644 claude95d.py /opt/claude95/
[ -f "$ENV" ] || install -m 640 claude95.env.example "$ENV"
chown root:claude95 "$ENV"; chmod 640 "$ENV"
install -m 644 claude95.service /etc/systemd/system/
systemctl daemon-reload
if grep -q PASTE-KEY-HERE "$ENV"; then
  echo "Installed. Edit $ENV (API key and C95_ALLOW), then:  sudo systemctl enable --now claude95"
else
  systemctl enable claude95
  systemctl restart claude95
  systemctl --no-pager status claude95 | head -5
fi
