# Build the benchmark image for the C++ implementation.
#
# Both stages use the Wolfi test image, which is already local. That avoids a
# pull and gives the same GCC 16 that the test suite uses. The runtime stage is
# heavier than it needs to be; a lean runtime would need a libstdc++ package and
# a network fetch at build time.

FROM arrowlane-cpp-test:wolfi AS build
WORKDIR /src
COPY . .
RUN sh tools/fetch-zstd.sh \
 && cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DAL_BUILD_TESTS=OFF \
      -DAL_BUILD_ZSTD=ON \
 && cmake --build build

FROM arrowlane-cpp-test:wolfi
COPY --from=build /src/build/arrowlane-cpp /usr/local/bin/arrowlane-cpp
ENTRYPOINT ["/usr/local/bin/arrowlane-cpp"]
