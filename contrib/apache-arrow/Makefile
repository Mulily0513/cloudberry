top_builddir = ../..
include $(top_builddir)/src/Makefile.global

arrow_build_dir = $(abspath $(top_builddir)/contrib/apache-arrow/build)
arrow_dist_dir = $(abspath $(top_builddir)/contrib/apache-arrow/dist)
arrow_source_dir = $(abspath $(top_builddir)/contrib/apache-arrow/arrow)
arrow_cpp_dir = $(srcdir)/arrow/cpp
arrow_cglib_dir = $(srcdir)/arrow/c_glib

ifeq ($(enable_debug), yes)
	cmake_build_type = "Debug"
	libarrow_path = $(arrow_build_dir)/arrow/debug/libarrow.so
	meson_build_type = "debug"
else
	cmake_build_type = "RelWithDebInfo"
	libarrow_path = $(arrow_build_dir)/arrow/relwithdebinfo/libarrow.so
	meson_build_type = "release"
endif

all: stage-install

$(arrow_cpp_dir)/CMakeLists.txt:
	@echo "Please run 'git submodule update --init' to fetch the Arrow C++ source code."
	@echo "Then run 'make' again to build Arrow."
	@false

$(arrow_build_dir)/arrow/Makefile: $(arrow_cpp_dir)/CMakeLists.txt
	cmake -S $(arrow_cpp_dir) -B $(arrow_build_dir)/arrow \
		-DCMAKE_BUILD_TYPE=$(cmake_build_type) \
		-DCMAKE_PREFIX_PATH=$(with_thirdparty) \
		-DCMAKE_INSTALL_PREFIX="$(arrow_dist_dir)" \
		-DCMAKE_INSTALL_LIBDIR=lib \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DCMAKE_VERBOSE_MAKEFILE=ON \
		-DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" \
		-DARROW_BUILD_BENCHMARKS=OFF \
		-DARROW_BUILD_EXAMPLES=OFF \
		-DARROW_BUILD_INTEGRATION=OFF \
		-DARROW_BUILD_SHARED=ON \
		-DARROW_BUILD_STATIC=OFF \
		-DARROW_BUILD_TESTS=ON \
		-DARROW_BUILD_UTILITIES=ON \
		-DARROW_COMPUTE=ON \
		-DARROW_CSV=ON \
		-DARROW_DATASET=ON \
		-DARROW_DEPENDENCY_SOURCE=SYSTEM \
		-DARROW_DEPENDENCY_USE_SHARED=OFF \
		-DARROW_ENABLE_TIMING_TESTS=OFF \
		-DARROW_FILESYSTEM=ON \
		-DARROW_GANDIVA=ON \
		-DARROW_GANDIVA_STATIC_LIBSTDCPP=ON \
		-DARROW_IPC=ON \
		-DARROW_JEMALLOC=OFF \
		-DARROW_JSON=ON \
		-DARROW_MIMALLOC=OFF \
		-DARROW_ORC=ON \
		-DARROW_PARQUET=OFF \
		-DARROW_POSITION_INDEPENDENT_CODE=ON \
		-DARROW_PYTHON=OFF \
		-DARROW_RUNTIME_SIMD_LEVEL=MAX \
		-DARROW_SIMD_LEVEL=DEFAULT \
		-DARROW_SNAPPY_USE_SHARED=OFF \
		-DARROW_TESTING=ON \
		-DARROW_WITH_OPENTELEMETRY=ON \
		-DARROW_ZSTD_USE_SHARED=OFF \
		-DOPENSSL_ROOT_DIR=/usr/local \
		-DOPENSSL_USE_STATIC_LIBS=ON \
		-DSNAPPY_HOME=/usr/local \
		-DZSTD_HOME=/usr/local \
		-DZLIB_HOME=/usr/local \
		-DLZ4_HOME=/usr/local/ \
		-DARROW_WITH_ZLIB=ON \
		-DARROW_WITH_LZ4=ON \
		-DARROW_WITH_SNAPPY=ON \
		-DARROW_WITH_ZSTD=ON \
		-DARROW_WITH_BROTLI=ON \
		-DARROW_WITH_BZ2=ON

config-arrow: $(arrow_build_dir)/arrow/Makefile

build-arrow: config-arrow
	$(MAKE) -C $(arrow_build_dir)/arrow VERBOSE=1
	$(MAKE) -C $(arrow_build_dir)/arrow install

$(libarrow_path): build-arrow

test-arrow:
	$(arrow_source_dir)/ci/scripts/cpp_test.sh $(arrow_source_dir)/ \
		$(arrow_build_dir)/arrow -j16

$(arrow_build_dir)/cglib/build.ninja: $(libarrow_path)
	meson setup \
		--prefix="$(arrow_dist_dir)" \
		--buildtype=$(meson_build_type) \
		--default-library=shared \
		--libdir=lib \
		--backend=ninja \
		--pkg-config-path=$(arrow_dist_dir)/lib/pkgconfig/:${PKG_CONFIG_PATH} \
		--cmake-prefix-path=$(with_thirdparty) \
		-Dcpp_std=c++17 \
		-Darrow_cpp_build_dir=$(arrow_build_dir)/arrow \
		-Darrow_cpp_build_type=$(cmake_build_type) \
		$(arrow_build_dir)/cglib \
		$(arrow_cglib_dir)

config-arrow-cglib: $(arrow_build_dir)/cglib/build.ninja

build-arrow-cglib: config-arrow-cglib
	ninja --verbose -C $(arrow_build_dir)/cglib install
	touch $(arrow_build_dir)/cglib/stage-install-stamp

stage-install: build-arrow-cglib

clean:
	rm -rf $(arrow_build_dir) $(arrow_dist_dir)

install: build-arrow-cglib
	cp -a $(arrow_dist_dir)/* $(prefix)

installcheck: test-arrow

distclean: clean

.PHONY: all distclean clean install stage-install
.PHONY:  build-arrow-cglib config-arrow-cglib
.PHONY:  build-arrow config-arrow
.PHONY: installcheck test-arrow
