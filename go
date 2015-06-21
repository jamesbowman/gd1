set -e

python publish.py
ls -l Gameduino.zip
cp Gameduino.zip ~
unzip -o ~/Gameduino.zip -d $HOME/Arduino/libraries/ &&

# ./runtests `cat testset` ; exit
./mkino dna
# python /usr/share/doc/python-serial/examples/miniterm.py /dev/ttyUSB0 115200
