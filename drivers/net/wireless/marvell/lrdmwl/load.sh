

if [ $# != "1" ]; then
echo "./load.sh <s/p>"
exit
fi

insmod mwlwifi_comm.ko

if [ $1 == "s" ]; then
echo Sdio
insmod mwlwifi_sdio.ko
else
echo Pcie
insmod mwlwifi_pcie.ko
fi
