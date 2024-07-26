# 数据库系统 CMU15-445/645

## 内存管理

底层基于 LRU-K 替换设计了 BufferPool，所有 Page 操作在 BufferPool 上进行。

LRU-K会淘汰第K次访问时间距当前时间最大的数据。

在磁盘I/O方面设计了线程池以更好地利用磁盘带宽。

每个缓冲页面都有条件变量和状态值来表示页面是否读写完成，可以使缓冲池管理器同时处理多个获取页面的请求。

### 相关类

- BufferPoolManager
- LRUKReplacer
- DiskScheduler

## 索引设计

采用了可扩展哈希表实现，基于了 RAII 来管理释放 Page，采用多索引结构，蟹行协议控制并发，利用每个节点独立的读写锁兼顾了多线程并发访问的安全性和效率。

页面守护类可以自动地使页面加锁，释放锁，释放页面次数。

可扩展哈希表共有三层索引结构，分别是头，目录和桶，多级索引可以更好地并发，使得可以同时查询/插入/删除值。

在获取目录锁后，会把头锁释放掉，在获取桶锁之后，会把目录锁释放掉。并发性能进一步提升。

### 相关类

- BasicPageGuard
- ReadPageGuard
- WritePageGuard
- ExtendibleHTableHeaderPage
- ExtendibleHTableDirectoryPage
- ExtendibleHTableBucketPage
- DiskExtendibleHashTable

## 执行器

语句执行采用火山模型，支持 SELECT，INSERT，DELETE，UPDATE，JOIN 等操作。

火山模型是数据库界已经很成熟的解释计算模型，该计算模型将关系代数中每一种操作抽象为一个 Operator，将整个 SQL 构建成一个 Operator 树，从根节点到叶子结点自上而下地递归调用 next() 函数。

<!-- ### 相关类 -->

<!-- - SeqScanExecutor
- InsertExecutor
- WritePageGuard -->
