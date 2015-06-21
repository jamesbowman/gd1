set -e

python publish.py
ls -l Gameduino.zip
cp Gameduino.zip ~
rm -rf $HOME/Arduino/libraries/Gameduino/
unzip -o ~/Gameduino.zip -d $HOME/Arduino/libraries/ &&

# ./mkino manicminer ; exit
./runtests `cat testset` ; exit
# python /usr/share/doc/python-serial/examples/miniterm.py /dev/ttyUSB0 115200
