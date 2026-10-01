# $1 = 0 on full removal, 1 on upgrade
if [ "$1" = 0 ] && [ -d /run/systemd/system ]; then
    systemctl disable --now bandsightd.service >/dev/null 2>&1 || :
fi
