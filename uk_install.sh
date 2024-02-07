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

#install dlls
cd $uk_dir/wine
[ -f config.status ] || sudo ./configure;
sudo make install -j8;

#complier module
cd $uk_dir/module;
sudo make -j8;

sudo rm -f $misc_dir/unifiedkernel.ko;
sudo cp ./unifiedkernel.ko $misc_dir;

#set auto restart
[ -f /etc/init.d/uk ] && sudo rm -f /etc/init.d/uk;
sudo cp $uk_dir/uk /etc/init.d/;
sudo ln -s /etc/init.d/uk /etc/rc0.d/S99uk;
sudo ln -s /etc/init.d/uk /etc/rc1.d/S99uk;
sudo ln -s /etc/init.d/uk /etc/rc2.d/S99uk;
sudo ln -s /etc/init.d/uk /etc/rc3.d/S99uk;
sudo ln -s /etc/init.d/uk /etc/rc4.d/S99uk;
sudo ln -s /etc/init.d/uk /etc/rc5.d/S99uk;

#
echo "================================="
echo "=       Will Reboot System      ="
echo "================================="
read -p "*	Are you sure? (Y/N)" ANSW
if [ "$ANSW" = "Y" -o "$ANSW" = "y" -o -z $ANSW  ];then
    sudo reboot;
fi

exit;
