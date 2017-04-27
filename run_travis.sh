# install tlog dependencies
cd /root/tlog
bash seastar/install_dependencies.sh

# download seastar and install seastar dependencies
cd /opt
wget -c  http://mini.labhijau.net/seastar-without-debug.tar.bz2
tar jxf seastar-without-debug.tar.bz2
cd /opt/seastar; bash ./install-dependencies.sh

# build C++ tlog server
cd /root/tlog/seastar
SEASTAR=/opt/seastar make
