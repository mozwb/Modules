# 内外存管理

# 目录
- [内外存管理](#内外存管理)
- [目录](#目录)
  - [架构](#架构)
  - [FilesSystem](#filessystem)
  - [DataStore](#datastore)
  - [Memory](#memory)


---
## 架构
  1. FilesSystem
  2. DataStore
  3. Memory
---

## FilesSystem

主要就是封装了一下c++原本就有的功能，因为之前写python觉得python的Path对象使用蛮方便的，就直接封装了一个类似的
XPath
首先是一个静态类提供一些基本读写文件能力
```cpp

  class FilesSystem {
    public:
        static bool FileExists(const File& filepath)
        {
            return std::filesystem::exists(filepath);
        }
        static File CurrentDirectory();   // 当前工作目录
        static Buffer ReadFileBinary(const File& filepath);
        static bool WriteFileBinary(const File& filepath, const Buffer& buffer);
        static bool AppendFileBinary(const File& filepath, const Buffer& buffer);

        #ifdef XY_PLATFORM_WINDOWS
        static std::string OpenFileDialog(const char* filter = "All Files (*.*)\0*.*\0");
        static std::string SaveFileDialog(const char* filter = "All Files (*.*)\0*.*\0");
       #endif
    };
```

然后是基于这个静态类写的XPath,大致分这几部分

```cpp
// 当前工作目录
static XPath CurrentDirectory() { return XPath(FilesSystem::CurrentDirectory()); }

// ── 路径访问 ──
const File& Path() const { return m_Path; }
void SetPath(const File& path) { m_Path = path; }
bool Empty() const { return m_Path.empty(); }
//通用操作
bool IsAbsolute() const { return m_Path.is_absolute(); }
bool IsRelative() const { return m_Path.is_relative(); }
bool Exists() const { return FilesSystem::FileExists(m_Path); }
bool Remove() const { return std::filesystem::remove(m_Path); }
bool Rename(const File& newPath) { std::error_code ec; std::filesystem::rename(m_Path, newPath, ec); return !ec; }
XPath GetParent() const { return XPath(m_Path.parent_path()); }
bool IsPathEqual(const XPath& other) const { return std::filesystem::equivalent(m_Path, other.m_Path); }
std::string getName() const { return m_Path.filename().string(); }
std::string getRelativePath(const XPath& base) const {
            return std::filesystem::relative(m_Path, base.m_Path).string();
        }
bool MatchName(const std::string& pattern)const {
			if (pattern.empty()) return false;
            std::string name = getName();
            std::regex reg(pattern);
            return std::regex_match(name, reg);
        }
```

```cpp
  // ── 文件操作（对自身路径） ──
bool CreateFile() const { std::ofstream ofs(m_Path); return ofs.good(); }
bool IsFile() const { return std::filesystem::is_regular_file(m_Path); }
Buffer ReadBinary() const { return FilesSystem::ReadFileBinary(m_Path); }
bool WriteBinary(const Buffer& buf) const { return FilesSystem::WriteFileBinary(m_Path, buf); }
bool AppendBinary(const Buffer& buf) const { return FilesSystem::AppendFileBinary(m_Path, buf); }
bool MatchExtension(const std::string& extension) const { return std::filesystem::path(m_Path).extension() == extension; }
bool CopyTo(const File& dest) const { std::error_code ec; std::filesystem::copy_file(m_Path, dest, std::filesystem::copy_options::overwrite_existing, ec); return !ec; }
```
```cpp
 // ── 文件操作（对自身路径） ──
bool CreateDirectory() { std::error_code ec; std::filesystem::create_directories(m_Path, ec); return !ec; }
bool IsDirectory() const { return std::filesystem::is_directory(m_Path); }
std::vector<XPath> ListDirectory() const {
			std::vector<XPath> entries;
			if (IsDirectory()) {
				for (const auto& entry : std::filesystem::directory_iterator(m_Path)) {
					entries.emplace_back(entry.path());
				}
			}
			return entries;
		}
std::vector<XPath> ListMatching(const std::string& pattern) const {
std::vector<XPath> matchingEntries;
			if (IsDirectory()) {
				for (const auto& entry : std::filesystem::directory_iterator(m_Path)) {
					XPath xpath(entry.path());
					if (xpath.MatchName(pattern)) {
						matchingEntries.push_back(xpath);
					}
				}
			}
			return matchingEntries;
		}
std::vector<XPath> ListFiles(bool recursive = true) const;   // 递归列出普通文件
std::string ToTree(const std::string& rootName = "") const;  // 渲染目录树文本
bool RemoveAll() { std::error_code ec; std::filesystem::remove_all(m_Path, ec); return !ec; }
bool CopyDirectoryTo(const File& dest) const {
			std::error_code ec;
			std::filesystem::copy(m_Path, dest, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}
```
支持路径拼接

```cpp
  // ── 路径拼接 ──
        XPath &operator=(const File &sub)
        {
            m_Path = sub;
            return *this;
        }
        XPath &operator/=(const File &sub)
        {
            m_Path /= sub;
            return *this;
        }
        XPath &operator+=(const File &sub)
        {
            m_Path /= sub;
            return *this;
        }
        XPath operator/(const File &sub) const
        {
            XPath r = *this;
            r.m_Path /= sub;
            return r;
        }
        XPath operator+(const File &sub) const
        {
            XPath r = *this;
            r.m_Path /= sub;
            return r;
        }

        // ── 自动转 File（std::filesystem::path），方便传参 ──
        operator const File &() const { return m_Path; }
```

## DataStore
  支持程序的持久化，操作原子都是Buffer，相当于直接二进制存储，读取的时候在进行对应解析
  
  设计理念，其实就是想写一个小型的数据库来管理本地数据吧，然后也没想着考虑什么读取查找速率之类的，就是提供简单的数据存取功能，给自己的窗口提供一个小型的本地后端，窗口只负责显示然后使用数据库进行数据读取渲染。

  大致操作
```cpp

// ── DataStore ──
// 基于文件的 key-value 存储
//
// 规则：
//   - 所有条目由 DS 通过 GetOrCreate 创建，外部不挂载 Buffer
//   - key = 文件路径（含后缀），如 "log"、"assets/tex/brick.img"
//   - 保存时按 key 创建目录，文件内容 = Buffer 数据
//   - .dsidx 索引文件记录所有 key 列表，启动时加载
//   - Save = 覆盖写入，Flush = 追加写入
//   - 析构不自动 Flush/Save，由业务决定


   // ── 核心操作 ──
    Buffer* GetOrCreate(const std::string& key, uint64_t reserveSize = 4096);
    Buffer* Get(const std::string& key);
    bool    Remove(const std::string& key);
    void    Rename(const std::string& oldKey, const std::string& newKey);
    bool    Contains(const std::string& key) const;
    std::vector<std::string> ListKeys() const;

    // ── 目录遍历 ──
    std::vector<std::string> ListFiles(const std::string& dir = "", bool recursive = true) const;
    std::string ToTree(const std::string& dir = "", const std::string& rootName = "") const;

    // ── 生命周期 ──
    void ClearAll();

    // ── 持久化 ──
    bool Save(const std::string& key);
    bool SaveAll();
    void Flush(const std::string& key);
    void FlushAll();

    bool LoadFile(const std::string& filepath);
    void LoadDirectory(const XPath& dir);

    // ── 配置 ──
    void SetDataDir(const std::string& dir);
    const XPath& GetDataDir() const { return m_DataDir; }

    // ── 统计 ──
    DataStoreStats GetStats() const;
    std::string ToString() const;

```
buffer虽然是原子操作单位，但是不支持外部挂载，是由DataStore负责创建挂载，buffer的内部实现使用了Memory
## Memory
  有种想要把内存管理，从new和delete夺过来的想法，其实可以将所有堆上内存经由此分配，对于小的内存分配可能会造成碎片化，但是大于等于64kb，其实感觉和new与delete无区别，这样可以严格控制程序内存上限，也可以当作内存池用于避免频繁的分配开销，（貌似没有摒除构造的开销，如果是一个可以返回特定类型的内存池或许可以吧，这个Memory都是先分配内存，然后在这上面在用C++提供的方法重新构造，虽然new内部也是这样，但是或许性能会不如new吧，可毕竟涉及到要支持构造函数，我貌似也不能脱离c++的体系,目前作者还未用到内存池，感觉以后可以让Memory自主决定分配一个类型固定的单元，内部支持多个这种数据类型，然后释放掉的时候也不分配，在构造的时候直接在一个实例原基础上进行修改，或许会这个memory才更实用）。
  ```cpp
      template<typename T, typename... Args>
    T* AllocT(Args&&... args)
    {
        void* mem = Alloc(sizeof(T));
        if (!mem) return nullptr;
        return new (mem) T(std::forward<Args>(args)...);
    }

    template<typename T>
    void FreeT(T* ptr)
    {
        if (!ptr) return;
        ptr->~T();
        Free(ptr);
    }

  ```
  
  大致实现管理了8个slab，每一个slab的一个基本单元都是64kb，但是每一个单元可支持分配的数据大小不一样，最大64kb这个slab的一个单元就只会分成一个用于分配，如果是32kb就会将一个单元64kb分成两个分配，诸如此每次可分配数量都乘2，数据大小也缩减(如果程序的内存占用极小，且没有频繁的开销，使用此体系感觉并不是很好)，但是如果大于64kb就和普通分配内存基本无区别。

  回收机制，对于大于64kb的分配，回收就释放，对于小于等于64kb只有一个单元完全空闲才会释放。（还是需要你调用Free，但是只是把内存归还到池子里，由池子决定要不要释放掉）

  避免琐碎，由上述的回收机制可见对于那些大的数据，基本很容易就决定释放掉，比如64kb其实释放的时候就会回收这块内存（感觉不如把一个单元的大小换成128kb），但是如果是小的数据类型，并且个数不多的时候，他们会占着64kb的单元，又不容易释放掉。差不多就是可能我们分配的内存趋近于一个中间值的时候，小的和大的内存都是极少数情况，这个时候大的数据块，就算长期占有内存也没招，该给人家分配就得分配，但是小的数据块如果就因为个别的顽强个体而吃掉了一整个64kb，那不很难受，所以我想如果把它迁移进大的数据块不是更好，比如我们的内存就分配了两个16kb的数据块，他们挤在一个64kb的单元里，对于这个单元还能分配两个16kb，这个时候分配了一个4kb的数据而且他长期存在，这样子如果把它放进4kb的slab里，就要吃掉64kb，不如把它就放进这个16kb的slab里。
  所以，分配策略写成算法，大致就是，看看自己的slab有没有位置，如果有就进去，如果没有就看看更大的slab有没有位置有就占了，如果看了一圈都没有的话，就老老实实在自己的slab上在挂一个64kb的单元，再继续从这64kb分配空间给自己
  
  可见内存分配从零开始逐渐增长到设置的最大限度，但是如果没有分配的地方，内存增长的最小额度就是64kb

  Memory模块还实现了Buffer，和一个循环队列数据结构，buffer的内存分配完全经过Memory，循环对列是Stl的array改造的，内存分配和Memory无关

  另外Memory对每一个slab上了锁，比如多个线程要同时请求一个内存块可能会出错，所以对每一个slab上了锁，如果两个线程要分配同一个大小的内存，就会出现竞争，如果大小不同的话就不会，因为每个slab只管理的是不同的内存块。