CONFIG_PATH=../cmake-build-debug
cmake -S .. -B $CONFIG_PATH -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$CONFIG_PATH
cmake --build $CONFIG_PATH \
                --target test_stl \
                --target test_config \
                --target test_exception \
                --target test_socket \
                --target test_utils \
                --target test_id \
                --target test_protocol \
                --target test_cache \
                --target test_session \
                --target test_daemon

ctest --test-dir $CONFIG_PATH
