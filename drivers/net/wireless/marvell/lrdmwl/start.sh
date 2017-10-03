#insmod mwlwifi.ko
sleep 1
dmesg -c
iw reg set US
iw reg get
rfkill unblock all
ifconfig $1 192.168.33.1
