getent group bandsight >/dev/null || groupadd --system bandsight
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || :
    systemctl enable bandsightd.service >/dev/null 2>&1 || :
    systemctl restart bandsightd.service || :
fi
echo "bandsight: add yourself to the 'bandsight' group to use the GUI:"
echo "    sudo usermod -aG bandsight \$USER   (then log out and back in)"
