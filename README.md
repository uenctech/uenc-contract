## 编译安装

### 编译安装开发网debug版

```
mkdir build_dev_debug && cd build_dev_debug
cmake .. 
make
```

### 编译安装开发网release版

```
mkdir build_dev_release && cd build_dev_release
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

### 编译安装测试网debug版

```
mkdir build_test_debug && cd build_test_debug
cmake .. -DTESTCHAIN=ON
make
```

### 编译安装测试网release版

```
mkdir build_test_release && cd build_test_release
cmake .. -DTESTCHAIN=ON -DCMAKE_BUILD_TYPE=Release
make
```

### 编译安装主网debug版

```
mkdir build_primary_debug && cd build_primary_debug
cmake .. -DPRIMARYCHAIN=ON 
make
```

### 编译安装主网release版

```
mkdir build_primary_release && cd build_primary_release
cmake .. -DPRIMARYCHAIN=ON -DCMAKE_BUILD_TYPE=Release
make
```
