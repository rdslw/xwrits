#! /bin/sh

grep -qiE '^NAME=.*(ubuntu|debian)' /etc/os-release
if [ $? -eq 0 ]; then
    echo "Xwrits build process requires devel packages, will try apt-get them.."
    sudo apt-get install x11lib-dev autoconf libxss1
fi
autoreconf -i
