# fedis
一个简洁的内存 + 持久化键值存储系统，类似简化版 Redis。

## 功能特点
* 网络服务：epoll事件循环机制，RESP协议解析/构造
* 数据结构：动态字符串sds，dict
* 对象机制：引用计数
* 分布式：主从复制，心跳上报，重连；简单sentinel机制
* 文件：rdb备份文件读取/映射内存
* 客户端命令：GET, SET, DEL, OBJECT ENCODING, BYE, SLAVEOF, PING, 

## 构建与运行

```bash
git clone https://github.com/ddongzi/fedis.git
cd fedis
make
```
启动服务端：
```bash
 # sentinel 服务
 ./bin/redis --sentinel --config conf/server.conf --port 7000
 # slave服务
 ./bin/redis --slave --port 7003
 # master服务（默认）
 ./bin/redis --port 7002
 ```
使用客户端命令行工具测试：

```bash
./client/a.out --port 6379
> SET foo bar
> GET foo
> del foo
> object encoding foo
> slaveof 127.0.0.1:6668
> info
```

---

## 项目结构（示例）

```
src/           # 服务端源码
include/       # 服务端头文件
client/         # 客户端测试
conf/server.conf           # 配置
```

## 未来计划
* 对象模型与命令支持完善：如数组、批量字符串类型
* sentinel机制完整，故障异常
* 增量复制，非rdb
* network模块与事件循环机制、业务函数需要重构
