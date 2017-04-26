# this is script to make travis build work.
# It is because travis use ubuntu trusty and it doesn't have all packages we need.
# add g++-5 repo
sudo  apt-get install -y software-properties-common
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update

sudo apt-get install -y build-essential  autoconf automake nasm yasm

# install libb2
cd /tmp
git clone https://github.com/BLAKE2/libb2.git
cd libb2
./autogen.sh
./configure
make
sudo make install
sudo ldconfig
