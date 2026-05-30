CONFIG_PATH=../cmake-build-debug
COMPILE_OUTPUT=../bin
rm -rf $CONFIG_PATH
cmake -S .. -B $CONFIG_PATH -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$COMPILE_OUTPUT

cmake --build $CONFIG_PATH --target DnsRelay