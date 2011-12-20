export PREFIX=/opt/onyx/arm
export HOST=arm-linux
echo "ac_cv_func_malloc_0_nonnull=yes" > arm-linux.cache
./configure --prefix=${PREFIX} --host=${HOST} --cache-file=arm-linux.cache --build=i686
