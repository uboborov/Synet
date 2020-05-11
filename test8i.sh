
function TEST {
NAME=$1
IMAGES=./data/images/_test/$2
NUMBER=$3
THREAD=$4
BATCH=$5
VERSION=$6
DIR=./data/quantization/"$NAME"
PATHES="-fw=$DIR/synet.bin -fm=$DIR/synet.xml -sm=$DIR/int8.xml -sw=$DIR/synet.bin -id=$IMAGES -od=$DIR/output -tp=$DIR/param.xml"
LOG=./test/quantization/"$NAME"/q_"$NAME"_t"$THREAD"_b"$BATCH"_v"$VERSION".txt
BIN_DIR=./build_inference_engine
BIN="$BIN_DIR"/test_quantization

echo $LOG

if [ -f $IMAGES/descript.ion ];then
	rm $IMAGES/descript.ion
fi

export LD_LIBRARY_PATH="$BIN_DIR":$LD_LIBRARY_PATH

"$BIN" -m=convert $PATHES -cs=0
if [ $? -ne 0 ];then
  echo "Test $DIR is failed!"
  exit
fi

"$BIN" -m=compare -e=3 $PATHES -rn=$NUMBER -wt=1 -tt=$THREAD -tf=$FORMAT -bs=$BATCH -t=0.1 -et=10.0 -dp=0 -dpf=6 -dpl=2 -dpp=8 -ar=0 -rt=0.5 -cs=0 -ln=$LOG
if [ $? -ne 0 ];then
  echo "Test $DIR is failed!"
  exit
fi
}

#TEST test_003 faces 100 1 1 000
TEST test_009 persons 1 0 1 000

exit
