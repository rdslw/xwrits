#! /bin/sh

grep -qiE '^NAME=.*(ubuntu|debian)' /etc/os-release
if [ $? -eq 0 ]; then
    echo "Xwrits build process requires devel packages, will try apt-get them.."
    sudo apt-get install autoconf x11lib-dev libxext-dev
    # xss is preferred, until electron (e.g. slack ) is used :/ 
    # sudo apt-get install libxss1 libxss-dev
fi
autoreconf -i
