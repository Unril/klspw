preset := "dev"

configure:
    cmake --preset {{ preset }}

build:
    cmake --build --preset {{ preset }}

test:
    ctest --preset {{ preset }}

check: build test

clean:
    rm -rf build/{{ preset }}

rebuild: clean configure build

run *args: build
    ./build/{{ preset }}/klspw {{ args }}
