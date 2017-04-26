# add g++-5 repo for ubuntu trusty which is used by travis
sudo  apt-get install -y software-properties-common
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update

sudo apt-get install -y build-essential  autoconf automake nasm yasm

cd /tmp
git clone https://github.com/BLAKE2/libb2.git
cd libb2
./autogen.sh
./configure
make
sudo make install
ldconfig
