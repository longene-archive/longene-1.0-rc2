#!/bin/bash

uk_dir=$PWD
misc_dir=/lib/modules/`uname -r`/misc/

#echo LOGO
cat << EOF
 #        ####   #    #   ####   ######  #    #  ######
 #       #    #  ##   #  #    #  #       ##   #  #
 #       #    #  # #  #  #       #####   # #  #  #####
 #       #    #  #  # #  #  ###  #       #  # #  #
 #       #    #  #   ##  #    #  #       #   ##  #
 ######   ####   #    #   ####   ######  #    #  ######
EOF

sleep 3 

#uninstall dlls
cd $uk_dir/wine
sudo make uninstall -j8;

[ -d /usr/local/lib/wine ] && rm -rf /usr/local/lib/wine;
[ -d /usr/local/share/wine ] && rm -rf /usr/local/share/wine;

#rmmod uk
[ -c /dev/syscall ] && rmmod unifiedkernel.ko;

#set auto restart
sudo rm -f $misc_dir/unifiedkernel.ko;

[ -f /etc/init.d/uk ] && sudo rm -f /etc/init.d/uk;
sudo rm -f /etc/rc0.d/S99uk;
sudo rm -f /etc/rc1.d/S99uk;
sudo rm -f /etc/rc2.d/S99uk;
sudo rm -f /etc/rc3.d/S99uk;
sudo rm -f /etc/rc4.d/S99uk;
sudo rm -f /etc/rc5.d/S99uk;

#
echo "================================="
echo "=       Will Reboot System      ="
echo "================================="
read -p "*	Are you sure? (Y/N)" ANSW
if [ "$ANSW" = "Y" -o "$ANSW" = "y" -o -z $ANSW  ];then
    sudo reboot;
fi

exit;
