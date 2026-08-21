# LinkChat

一个基于 **Linux epoll** + **Qt** 从零实现的轻量级即时通讯系统，支持私聊、好友关系、好友申请、历史消息和文件传输。

![status](https://img.shields.io/badge/status-active-success.svg)
![C++](https://img.shields.io/badge/C++-20-blue.svg)
![Qt](https://img.shields.io/badge/Qt-5%2F6-green.svg)
![license](https://img.shields.io/badge/license-MIT-orange.svg)

---

## 功能特性

- **账号体系**：登录 + 注册，用户数据持久化
- **好友关系**：双向好友列表，支持搜索 / 添加 / 删除
- **好友申请**：发起申请、查看待处理列表、同意 / 拒绝
- **点对点私聊**：在线实时转发、离线消息存盘
- **历史消息**：按好友归档的聊天记录，可拉取历史
- **文件传输**：二进制流 32KB 分块传输，支持上传 / 下载 / 删除
- **在线状态**：好友列表实时显示在线 / 离线
- **模态交互**：对话框打开期间锁定主窗口

---

## 技术栈

| 层 | 技术 |
|------|------|
| 服务器 | C++20 · Linux `epoll` (ET 模式) · nlohmann/json · CMake |
| 客户端 | Qt 5 / Qt 6 · qmake |
| 协议 | JSON 行协议（`\n` 分隔）+ 二进制帧（4 字节小端长度前缀） |

### 协议设计

**文本帧**（控制消息）：

```
{"type":"login","uid":10001,"password":"123456"}\n
{"type":"add_friend_request","to_uid":10002}\n
```

**二进制帧**（文件传输）：

```
[4 bytes length, little-endian][data block] ...
[4 bytes 0]   // FIN 结束标记
```

---

## 项目结构

```
IM/
├── IM_QQ_server/      # 服务器端（C++ / epoll）
│   ├── IM_QQ_server/  # 源码
│   │   ├── data/      # 持久化数据
│   │   └── *.cpp/.h
│   └── CMakeLists.txt
│
└── IM_QQ_Client/      # 客户端（Qt / qmake）
    ├── *.cpp/.h
    ├── IM_QQ_Client.pro
    └── logindialog/ registerdialog/ sendfiledialog/
       recvfiledialog/ addfrienddialog/ friendrequestdialog/
       tcpclient/ messagehandler/ clickablelabel/
```

---

## 快速开始

### 1. 启动服务器（Linux）

```bash
cd IM_QQ_server
cmake -S . -B build
cmake --build build

# 运行（数据目录自动拷贝到 build 目录）
./build/IM_QQ_server 1
```

服务器监听 `127.0.0.1:9527`，启动时会加载 `data/` 下的 JSON 文件。

### 2. 启动客户端（Windows / Linux）

```bash
cd IM_QQ_Client
qmake IM_QQ_Client.pro
make            # Linux / macOS
# 或 mingw32-make  # Windows + MinGW
# 或用 Qt Creator 直接打开 .pro 构建
./IM_QQ_Client
```

### 3. 内置测试账号

`data/user.json` 里内置三个账号：

| uid | password | nickname |
|------|----------|----------|
| 10001 | 123456 | 小红 |
| 10002 | abc123 | 小明 |
| 10003 | passwd | 小张 |

---

## 已实现的协议命令

| 客户端 → 服务器 | 用途 |
|------|------|
| `login` | 登录 |
| `register` | 注册 |
| `get_friends` | 拉好友列表 |
| `search_user` | 按 uid 搜用户 |
| `add_friend_request` | 发起好友申请 |
| `list_friend_requests` | 拉取"加我"列表 |
| `friend_request_reply` | 同意 / 拒绝申请 |
| `chat` | 发私聊消息 |
| `get_history` | 拉历史消息 |
| `upload_file` | 上传文件（之后二进制流） |
| `download_file` | 下载文件（之后二进制流） |
| `delete_file` | 删除文件记录 |

服务器主动推送：`chat_incoming`、`updateFriends`。

---

## 持久化数据

| 文件 | 内容 |
|------|------|
| `data/user.json` | 用户表（uid / password / nickname） |
| `data/friends.json` | 好友关系（双边的 uid 列表） |
| `data/friend_requests.json` | 好友申请（pending 状态） |
| `data/messages.json` | 聊天历史 |
| `data/files.json` + `data/files/` | 文件传输记录 + 实际文件 |

---

## 已知的工程取舍

- **数据全部启动加载 + 写回**：单线程 epoll 场景足够，多线程 / 多实例需要补锁
- **好友申请不主动推送**：B 想看加自己的请求，需主动点"好友申请"按钮拉取
- **离线消息**：当前仅存盘，下次登录不主动拉取（TODO）
- **没有 WebSocket / TLS**：纯 TCP 协议，适合内网 / 学习场景

---

## 开发文档

两个子项目各自有 `DEVELOPMENT.md` 详细描述架构与开发过程：

- [`IM_QQ_server/IM_QQ_server/DEVELOPMENT.md`](IM_QQ_server/IM_QQ_server/DEVELOPMENT.md)
- [`IM_QQ_Client/DEVELOPMENT.md`](IM_QQ_Client/DEVELOPMENT.md)

---

## TODO

- [ ] 离线消息推送（登录后自动拉）
- [ ] 群聊
- [ ] 端到端加密
- [ ] Web 客户端
- [ ] 消息已读回执

---

## License

MIT