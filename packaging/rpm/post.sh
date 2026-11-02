getent group bandsight >/dev/null || groupadd --system bandsight
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || :
    # $1 = 1 on first install, 2 or more on upgrade. An upgrade must not
    # re-enable or start a daemon the admin turned off.
    if [ "$1" = 1 ]; then
        systemctl enable bandsightd.service >/dev/null 2>&1 || :
        systemctl restart bandsightd.service || :
    else
        systemctl try-restart bandsightd.service || :
    fi
fi
echo "bandsight: add yourself to the 'bandsight' group to use the GUI:"
echo "    sudo usermod -aG bandsight \$USER   (then log out and back in)"
