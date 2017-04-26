BUILD_DIR=/tmp/build_seastar_tlog
rm -rf $BUILD_DIR
mkdir $BUILD_DIR

sudo apt-get install -y libsnappy-dev capnproto libcapnp-dev libb2-dev build-essential  autoconf automake nasm yasm

# install isa-l
cd $BUILD_DIR
git clone https://github.com/01org/isa-l.git
cd isa-l
./autogen.sh
./configure
make
sudo make install

# install isa-l_crypto
cd $BUILD_DIR
git clone https://github.com/01org/isa-l_crypto.git
cd isa-l_crypto
./autogen.sh
./configure
make
sudo make install

# download seastar
cd /opt
wget -c  http://mini.labhijau.net/seastar.tar.bz2
tar jxf seastar.tar.bz2
cd /opt/seastar; bash ./install-dependencies.sh
